/*
 * KikiOS SD Card Driver for Raspberry Pi Zero 2W
 * Clean-room implementation based on:
 * - BCM2835 ARM Peripherals (publicly available from Broadcom)
 * - SD Physical Layer Simplified Specification v3.00
 * - SDHCI Specification v3.00
 */

#include "../hal.h"
#include "../../printf.h"
#include "../../string.h"
#include "../../memory.h"

/* LED for disk activity indicator - rate limited to ~20Hz */
extern void led_toggle(void);
extern uint64_t hal_timer_get_ticks(void);

static void disk_activity_led(void) {
    static uint64_t last_toggle = 0;
    uint64_t now = hal_timer_get_ticks();
    /* Toggle at most every 3 ticks (30ms) = ~17Hz */
    if (now - last_toggle >= 3) {
        led_toggle();
        last_toggle = now;
    }
}

/* BCM2710 (Pi Zero 2W) peripheral base address */
#define BCM_PERIPH_BASE     0x3F000000

/* EMMC controller is at offset 0x300000 */
#define SDHCI_BASE          (BCM_PERIPH_BASE + 0x300000)

/* SDHCI Register offsets (from SDHCI spec) */
#define REG_ARG2            0x00
#define REG_BLKSIZECNT      0x04
#define REG_ARG1            0x08
#define REG_CMDTM           0x0C
#define REG_RSP0            0x10
#define REG_RSP1            0x14
#define REG_RSP2            0x18
#define REG_RSP3            0x1C
#define REG_DATA            0x20
#define REG_STATUS          0x24
#define REG_CTRL0           0x28
#define REG_CTRL1           0x2C
#define REG_INTR            0x30
#define REG_INTR_MASK       0x34
#define REG_INTR_EN         0x38
#define REG_CTRL2           0x3C
#define REG_SLOTISR_VER     0xFC

/* GPIO controller for pin muxing */
#define GPIO_BASE           (BCM_PERIPH_BASE + 0x200000)

/* Mailbox for VideoCore communication */
#define MBOX_BASE           (BCM_PERIPH_BASE + 0xB880)

/* Helper to read/write SDHCI registers */
static inline uint32_t sdhci_read(uint32_t reg) {
    return *(volatile uint32_t *)(SDHCI_BASE + reg);
}

static inline void sdhci_write(uint32_t reg, uint32_t val) {
    *(volatile uint32_t *)(SDHCI_BASE + reg) = val;
}

/* GPIO API from gpio.c */
extern void gpio_set_function(int pin, int func);
extern void gpio_set_pull_mask(uint32_t pins_mask, int bank, int pull);

/* Mailbox register access */
static inline uint32_t mbox_read_reg(uint32_t reg) {
    return *(volatile uint32_t *)(MBOX_BASE + reg);
}

static inline void mbox_write_reg(uint32_t reg, uint32_t val) {
    *(volatile uint32_t *)(MBOX_BASE + reg) = val;
}

/* Memory barrier for ARM */
static inline void mem_barrier(void) {
    __asm__ volatile("dsb sy" ::: "memory");
}

/* Cache maintenance for DMA/GPU coherency */
#define CACHE_LINE_SIZE 64

static void cache_clean(const void *start, uint32_t len) {
    uintptr_t addr = (uintptr_t)start & ~(CACHE_LINE_SIZE - 1);
    uintptr_t end = (uintptr_t)start + len;
    while (addr < end) {
        __asm__ volatile("dc cvac, %0" : : "r"(addr) : "memory");
        addr += CACHE_LINE_SIZE;
    }
    __asm__ volatile("dsb sy" ::: "memory");
}

static void cache_invalidate(void *start, uint32_t len) {
    uintptr_t addr = (uintptr_t)start & ~(CACHE_LINE_SIZE - 1);
    uintptr_t end = (uintptr_t)start + len;
    while (addr < end) {
        // Use clean-and-invalidate - safer for dirty lines
        __asm__ volatile("dc civac, %0" : : "r"(addr) : "memory");
        addr += CACHE_LINE_SIZE;
    }
    __asm__ volatile("dsb sy" ::: "memory");
}

/* Simple microsecond delay (approximate, ~1GHz CPU) */
static void delay_us(uint32_t us) {
    for (uint32_t i = 0; i < us * 300; i++) {
        __asm__ volatile("nop");
    }
}

/* SD card state */
static struct {
    int ready;
    int is_sdhc;           /* 1 = SDHC/SDXC (block addressing), 0 = SDSC (byte addressing) */
    uint32_t rca;          /* Relative Card Address */
    uint32_t clk_base;     /* Base clock frequency in Hz */
} card;

/*
 * DMA support for EMMC
 * Uses BCM2837 DMA controller for fast block transfers
 */
#define DMA_BASE            0x3F007000
#define EMMC_DMA_CHANNEL    4   /* Use channel 4 for EMMC (0 is used for FB) */
#define EMMC_DREQ           11  /* EMMC peripheral DREQ number */

/* DMA register offsets */
#define DMA_CS              0x00
#define DMA_CONBLK_AD       0x04
#define DMA_ENABLE          0xFF0

/* DMA CS bits */
#define DMA_CS_ACTIVE       (1 << 0)
#define DMA_CS_END          (1 << 1)
#define DMA_CS_INT          (1 << 2)
#define DMA_CS_ERROR        (1 << 8)
#define DMA_CS_PRIORITY(x)  (((x) & 0xF) << 16)
#define DMA_CS_PANIC_PRI(x) (((x) & 0xF) << 20)
#define DMA_CS_WAIT_WRITES  (1 << 28)
#define DMA_CS_RESET        (1 << 31)

/* DMA Transfer Info bits */
#define DMA_TI_INTEN        (1 << 0)
#define DMA_TI_WAIT_RESP    (1 << 3)
#define DMA_TI_DEST_INC     (1 << 4)
#define DMA_TI_DEST_DREQ    (1 << 6)
#define DMA_TI_SRC_INC      (1 << 8)
#define DMA_TI_SRC_DREQ     (1 << 10)
#define DMA_TI_PERMAP(x)    (((x) & 0x1F) << 16)

/* SDHCI interrupt bits (needed by DMA functions) */
#define INTR_CMD_DONE       (1 << 0)
#define INTR_DATA_DONE      (1 << 1)
#define INTR_WRITE_READY    (1 << 4)
#define INTR_READ_READY     (1 << 5)
#define INTR_ERR            0xFFFF0000

/* DMA Control Block (must be 32-byte aligned) */
typedef struct __attribute__((aligned(32))) {
    uint32_t ti;
    uint32_t source_ad;
    uint32_t dest_ad;
    uint32_t txfr_len;
    uint32_t stride;
    uint32_t nextconbk;
    uint32_t reserved[2];
} emmc_dma_cb_t;

static emmc_dma_cb_t __attribute__((aligned(32))) emmc_dma_cb;
static int emmc_dma_enabled = 0;

/* Convert ARM physical address to bus address for DMA */
static inline uint32_t arm_to_bus(void *ptr) {
    return ((uint32_t)(uint64_t)ptr) | 0xC0000000;
}

/* EMMC DATA register bus address */
#define EMMC_DATA_BUS       (0x7E300000 + REG_DATA)  /* VideoCore bus address */

static inline uint32_t emmc_dma_read(int reg) {
    return *(volatile uint32_t *)(DMA_BASE + EMMC_DMA_CHANNEL * 0x100 + reg);
}

static inline void emmc_dma_write(int reg, uint32_t val) {
    *(volatile uint32_t *)(DMA_BASE + EMMC_DMA_CHANNEL * 0x100 + reg) = val;
}

static inline uint32_t emmc_dma_read_global(int reg) {
    return *(volatile uint32_t *)(DMA_BASE + reg);
}

static inline void emmc_dma_write_global(int reg, uint32_t val) {
    *(volatile uint32_t *)(DMA_BASE + reg) = val;
}

static void emmc_dma_init(void) {
    /* Enable DMA channel */
    uint32_t enable = emmc_dma_read_global(DMA_ENABLE);
    emmc_dma_write_global(DMA_ENABLE, enable | (1 << EMMC_DMA_CHANNEL));
    mem_barrier();

    /* Reset the channel */
    emmc_dma_write(DMA_CS, DMA_CS_RESET);
    mem_barrier();

    /* Wait for reset with timeout */
    int timeout = 10000;
    while ((emmc_dma_read(DMA_CS) & DMA_CS_RESET) && --timeout > 0) {
        delay_us(1);
    }

    /* Clear any pending status */
    emmc_dma_write(DMA_CS, DMA_CS_END | DMA_CS_INT);
    mem_barrier();

    emmc_dma_enabled = 1;
    printf("[SD] DMA enabled on channel %d\n", EMMC_DMA_CHANNEL);
}

static int emmc_dma_wait(void) {
    /* Tight poll - DMA is fast, no need for delays */
    for (int i = 0; i < 10000000; i++) {
        uint32_t cs = emmc_dma_read(DMA_CS);
        if (!(cs & DMA_CS_ACTIVE)) {
            emmc_dma_write(DMA_CS, DMA_CS_END | DMA_CS_INT);
            mem_barrier();
            return (cs & DMA_CS_ERROR) ? -1 : 0;
        }
    }
    printf("[SD] DMA timeout\n");
    emmc_dma_write(DMA_CS, DMA_CS_END | DMA_CS_INT);
    return -1;
}

/*
 * DMA-based block read from EMMC
 * Uses DREQ pacing - DMA waits for EMMC to signal data ready
 */
static int read_data_blocks_dma(uint8_t *buf, uint32_t count) {
    uint32_t bytes = count * 512;

    /* Invalidate cache for destination buffer before DMA */
    cache_invalidate(buf, bytes);

    /* Set up DMA control block:
     * - Read from EMMC DATA register (fixed address)
     * - Write to buffer (incrementing address)
     * - Use DREQ pacing from EMMC peripheral
     */
    emmc_dma_cb.ti = DMA_TI_DEST_INC | DMA_TI_WAIT_RESP |
                     DMA_TI_SRC_DREQ | DMA_TI_PERMAP(EMMC_DREQ);
    emmc_dma_cb.source_ad = EMMC_DATA_BUS;
    emmc_dma_cb.dest_ad = arm_to_bus(buf);
    emmc_dma_cb.txfr_len = bytes;
    emmc_dma_cb.stride = 0;
    emmc_dma_cb.nextconbk = 0;

    /* Clean cache for control block */
    cache_clean(&emmc_dma_cb, sizeof(emmc_dma_cb));
    mem_barrier();

    /* Point DMA to control block and start */
    emmc_dma_write(DMA_CONBLK_AD, arm_to_bus(&emmc_dma_cb));
    mem_barrier();
    emmc_dma_write(DMA_CS, DMA_CS_ACTIVE | DMA_CS_PRIORITY(8) | DMA_CS_PANIC_PRI(15) | DMA_CS_WAIT_WRITES);

    /* Wait for DMA to complete */
    if (emmc_dma_wait() < 0) {
        printf("[SD] DMA read failed\n");
        return -1;
    }

    /* Wait for EMMC transfer complete - tight poll */
    uint32_t intr;
    for (int i = 0; i < 10000000; i++) {
        intr = sdhci_read(REG_INTR);
        if (intr & (INTR_DATA_DONE | INTR_ERR)) break;
    }

    sdhci_write(REG_INTR, INTR_DATA_DONE | INTR_ERR);

    if (intr & INTR_ERR) {
        printf("[SD] DMA transfer complete error: 0x%x\n", intr);
        return -1;
    }

    /* Invalidate cache so CPU sees DMA-written data */
    cache_invalidate(buf, bytes);

    return 0;
}

/* Mailbox property buffer - must be 16-byte aligned for GPU */
static uint32_t __attribute__((aligned(16))) prop_buf[32];

/*
 * VideoCore mailbox interface
 * Channel 8 is the property channel for ARM<->GPU communication
 */
#define MBOX_READ       0x00
#define MBOX_STATUS     0x18
#define MBOX_WRITE      0x20
#define MBOX_FULL       0x80000000
#define MBOX_EMPTY      0x40000000
#define MBOX_CHANNEL    8

static int mbox_call(void) {
    /* Convert ARM address to bus address for GPU */
    uint32_t addr = ((uint32_t)(uint64_t)prop_buf) | 0xC0000000;

    /* Clean cache so GPU sees our writes */
    cache_clean((void *)prop_buf, sizeof(prop_buf));
    mem_barrier();

    /* Wait for mailbox to have space */
    while (mbox_read_reg(MBOX_STATUS) & MBOX_FULL) {
        mem_barrier();
    }

    /* Write address + channel */
    mbox_write_reg(MBOX_WRITE, (addr & ~0xF) | MBOX_CHANNEL);
    mem_barrier();

    /* Wait for response */
    while (1) {
        while (mbox_read_reg(MBOX_STATUS) & MBOX_EMPTY) {
            mem_barrier();
        }
        mem_barrier();
        uint32_t resp = mbox_read_reg(MBOX_READ);
        if ((resp & 0xF) == MBOX_CHANNEL) {
            break;
        }
    }

    mem_barrier();
    /* Invalidate cache so we see GPU's response */
    cache_invalidate((void *)prop_buf, sizeof(prop_buf));

    return (prop_buf[1] == 0x80000000) ? 0 : -1;
}

/*
 * Power on the SD controller via VideoCore
 * Tag 0x28001 = Set Power State
 * Device 0 = SD Card
 */
static int power_on_sd(void) {
    prop_buf[0] = 32;           /* Total size */
    prop_buf[1] = 0;            /* Request */
    prop_buf[2] = 0x00028001;   /* Tag: Set Power State */
    prop_buf[3] = 8;            /* Value buffer size */
    prop_buf[4] = 8;            /* Request size */
    prop_buf[5] = 0;            /* Device: SD card */
    prop_buf[6] = 3;            /* State: ON + wait */
    prop_buf[7] = 0;            /* End tag */

    if (mbox_call() < 0) {
        printf("[SD] Power on mailbox call failed\n");
        return -1;
    }

    if ((prop_buf[6] & 3) != 1) {
        printf("[SD] SD controller did not power on\n");
        return -1;
    }

    return 0;
}

/*
 * Query the EMMC clock rate from VideoCore
 * Tag 0x30002 = Get Clock Rate
 * Clock 1 = EMMC
 */
static uint32_t query_emmc_clock(void) {
    prop_buf[0] = 32;
    prop_buf[1] = 0;
    prop_buf[2] = 0x00030002;   /* Tag: Get Clock Rate */
    prop_buf[3] = 8;
    prop_buf[4] = 4;
    prop_buf[5] = 1;            /* Clock: EMMC */
    prop_buf[6] = 0;
    prop_buf[7] = 0;

    if (mbox_call() < 0 || prop_buf[6] == 0) {
        /* Fallback to 100 MHz if query fails */
        return 100000000;
    }

    return prop_buf[6];
}

/*
 * Configure GPIO pins for SD card
 * Pi uses GPIO 48-53 for the built-in SD slot:
 *   GPIO 48 = CLK
 *   GPIO 49 = CMD
 *   GPIO 50-53 = DAT0-DAT3
 * All need to be set to ALT3 function with pull-ups
 */
static void setup_sd_gpio(void) {
    // Set all SD card pins to ALT3 function
    for (int pin = 48; pin <= 53; pin++) {
        gpio_set_function(pin, GPIO_ALT3);
    }

    // Enable pull-ups on GPIO 48-53 (bits 16-21 in bank 1)
    gpio_set_pull_mask(0x3F0000, 1, GPIO_PULL_UP);
}

/* Command flags for CMDTM register */
#define TM_CMD_INDEX(n)     ((n) << 24)
#define TM_RSP_NONE         (0 << 16)
#define TM_RSP_136          (1 << 16)  /* R2 response */
#define TM_RSP_48           (2 << 16)  /* R1, R3, R6, R7 */
#define TM_RSP_48_BUSY      (3 << 16)  /* R1b */
#define TM_CRC_EN           (1 << 19)
#define TM_DATA             (1 << 21)
#define TM_DATA_READ        (1 << 4)
#define TM_MULTI_BLK        (1 << 5)
#define TM_BLK_CNT_EN       (1 << 1)
#define TM_AUTO_CMD12       (1 << 2)  /* Auto CMD12 after multi-block transfer */

/*
 * Send a command to the SD card
 * Returns 0 on success, -1 on error
 * Response is stored in resp[] (up to 4 words)
 */
static int sd_command(uint32_t cmd_flags, uint32_t arg, uint32_t *resp) {
    int timeout;

    /* Clear pending interrupts */
    sdhci_write(REG_INTR, 0xFFFFFFFF);

    /* Wait for command line to be free */
    timeout = 100000;
    while ((sdhci_read(REG_STATUS) & 1) && --timeout > 0) {
        delay_us(1);
    }
    if (timeout == 0) {
        printf("[SD] Command line busy\n");
        return -1;
    }

    /* Send command */
    sdhci_write(REG_ARG1, arg);
    sdhci_write(REG_CMDTM, cmd_flags);

    /* Wait for command complete or error */
    timeout = 100000;
    uint32_t intr;
    while (--timeout > 0) {
        intr = sdhci_read(REG_INTR);
        if (intr & (INTR_CMD_DONE | INTR_ERR)) break;
        delay_us(1);
    }

    /* Clear command done interrupt */
    sdhci_write(REG_INTR, INTR_CMD_DONE | INTR_ERR);

    if (timeout == 0) {
        printf("[SD] Command timeout\n");
        return -1;
    }

    if (intr & INTR_ERR) {
        printf("[SD] Command error: 0x%x\n", intr >> 16);
        return -1;
    }

    /* Read response */
    if (resp) {
        resp[0] = sdhci_read(REG_RSP0);
        resp[1] = sdhci_read(REG_RSP1);
        resp[2] = sdhci_read(REG_RSP2);
        resp[3] = sdhci_read(REG_RSP3);
    }

    return 0;
}

/*
 * Send an application-specific command (ACMD)
 * First sends CMD55 to put card in app-command mode
 */
static int sd_app_command(uint32_t acmd_flags, uint32_t arg, uint32_t *resp) {
    uint32_t dummy[4];

    /* CMD55 = APP_CMD, tells card next command is application-specific */
    if (sd_command(TM_CMD_INDEX(55) | TM_RSP_48 | TM_CRC_EN, card.rca << 16, dummy) < 0) {
        return -1;
    }

    return sd_command(acmd_flags, arg, resp);
}

/*
 * Configure the SD clock divider
 * target_hz: desired clock frequency
 */
static void set_sd_clock(uint32_t target_hz) {
    uint32_t ctrl1;
    int timeout;

    /* Wait for command/data lines to be idle */
    timeout = 10000;
    while ((sdhci_read(REG_STATUS) & 0x3) && --timeout > 0) {
        delay_us(1);
    }

    /* Disable clock */
    ctrl1 = sdhci_read(REG_CTRL1);
    ctrl1 &= ~(1 << 2);  /* Clear CLK_EN */
    sdhci_write(REG_CTRL1, ctrl1);
    delay_us(2000);

    /* Calculate divider */
    uint32_t div = card.clk_base / target_hz;
    if (card.clk_base % target_hz) div++;

    /* Round up to power of 2 */
    uint32_t shift = 0;
    while ((1u << shift) < div && shift < 10) shift++;
    div = (shift == 0) ? 0 : (1 << (shift - 1));

    /* Set divider (bits 8-15 = freq_select, bits 6-7 = upper bits) */
    ctrl1 &= ~0xFFE0;
    ctrl1 |= ((div & 0xFF) << 8) | (((div >> 8) & 0x3) << 6);
    sdhci_write(REG_CTRL1, ctrl1);
    delay_us(2000);

    /* Re-enable clock */
    ctrl1 |= (1 << 2);
    sdhci_write(REG_CTRL1, ctrl1);
    delay_us(2000);
}

/*
 * Read data after a read command
 */
static int read_data_block(uint8_t *buf, uint32_t bytes) {
    uint32_t *buf32 = (uint32_t *)buf;
    uint32_t words = bytes / 4;
    int timeout;
    uint32_t intr;

    /* Wait for read ready */
    timeout = 500000;
    while (--timeout > 0) {
        intr = sdhci_read(REG_INTR);
        if (intr & (INTR_READ_READY | INTR_ERR)) break;
        delay_us(1);
    }

    if (timeout == 0 || (intr & INTR_ERR)) {
        printf("[SD] Read timeout/error: 0x%x\n", intr);
        return -1;
    }

    sdhci_write(REG_INTR, INTR_READ_READY);

    /* Read data from FIFO */
    for (uint32_t i = 0; i < words; i++) {
        buf32[i] = sdhci_read(REG_DATA);
    }

    /* Wait for transfer complete */
    timeout = 100000;
    while (--timeout > 0) {
        intr = sdhci_read(REG_INTR);
        if (intr & (INTR_DATA_DONE | INTR_ERR)) break;
        delay_us(1);
    }

    sdhci_write(REG_INTR, INTR_DATA_DONE | INTR_ERR);

    if (timeout == 0 || (intr & INTR_ERR)) {
        printf("[SD] Transfer complete timeout/error\n");
        return -1;
    }

    return 0;
}

/*
 * Write data after a write command
 */
static int write_data_block(const uint8_t *buf, uint32_t bytes) {
    const uint32_t *buf32 = (const uint32_t *)buf;
    uint32_t words = bytes / 4;
    int timeout;
    uint32_t intr;

    /* Wait for write ready */
    timeout = 500000;
    while (--timeout > 0) {
        intr = sdhci_read(REG_INTR);
        if (intr & (INTR_WRITE_READY | INTR_ERR)) break;
        delay_us(1);
    }

    if (timeout == 0 || (intr & INTR_ERR)) {
        printf("[SD] Write timeout/error\n");
        return -1;
    }

    sdhci_write(REG_INTR, INTR_WRITE_READY);

    /* Write data to FIFO */
    for (uint32_t i = 0; i < words; i++) {
        sdhci_write(REG_DATA, buf32[i]);
    }

    /* Wait for transfer complete */
    timeout = 100000;
    while (--timeout > 0) {
        intr = sdhci_read(REG_INTR);
        if (intr & (INTR_DATA_DONE | INTR_ERR)) break;
        delay_us(1);
    }

    sdhci_write(REG_INTR, INTR_DATA_DONE | INTR_ERR);

    if (timeout == 0 || (intr & INTR_ERR)) {
        printf("[SD] Write complete timeout/error\n");
        return -1;
    }

    return 0;
}

/*
 * Read multiple data blocks (for CMD18)
 * Reads 'count' blocks of 512 bytes each
 */
static int read_data_blocks(uint8_t *buf, uint32_t count) {
    uint32_t *buf32 = (uint32_t *)buf;
    int timeout;
    uint32_t intr;

    for (uint32_t blk = 0; blk < count; blk++) {
        /* Wait for read ready */
        timeout = 500000;
        while (--timeout > 0) {
            intr = sdhci_read(REG_INTR);
            if (intr & (INTR_READ_READY | INTR_ERR)) break;
        }

        if (timeout == 0 || (intr & INTR_ERR)) {
            printf("[SD] Multi-read timeout/error at block %u: 0x%x\n", blk, intr);
            return -1;
        }

        sdhci_write(REG_INTR, INTR_READ_READY);

        /* Read 512 bytes (128 words) from FIFO */
        for (uint32_t i = 0; i < 128; i++) {
            *buf32++ = sdhci_read(REG_DATA);
        }
    }

    /* Wait for transfer complete */
    timeout = 100000;
    while (--timeout > 0) {
        intr = sdhci_read(REG_INTR);
        if (intr & (INTR_DATA_DONE | INTR_ERR)) break;
    }

    sdhci_write(REG_INTR, INTR_DATA_DONE | INTR_ERR);

    if (timeout == 0 || (intr & INTR_ERR)) {
        printf("[SD] Multi-read complete timeout/error\n");
        return -1;
    }

    return 0;
}

/*
 * Write multiple data blocks (for CMD25)
 * Writes 'count' blocks of 512 bytes each
 */
static int write_data_blocks(const uint8_t *buf, uint32_t count) {
    const uint32_t *buf32 = (const uint32_t *)buf;
    int timeout;
    uint32_t intr;

    for (uint32_t blk = 0; blk < count; blk++) {
        /* Wait for write ready */
        timeout = 500000;
        while (--timeout > 0) {
            intr = sdhci_read(REG_INTR);
            if (intr & (INTR_WRITE_READY | INTR_ERR)) break;
        }

        if (timeout == 0 || (intr & INTR_ERR)) {
            printf("[SD] Multi-write timeout/error at block %u\n", blk);
            return -1;
        }

        sdhci_write(REG_INTR, INTR_WRITE_READY);

        /* Write 512 bytes (128 words) to FIFO */
        for (uint32_t i = 0; i < 128; i++) {
            sdhci_write(REG_DATA, *buf32++);
        }
    }

    /* Wait for transfer complete */
    timeout = 100000;
    while (--timeout > 0) {
        intr = sdhci_read(REG_INTR);
        if (intr & (INTR_DATA_DONE | INTR_ERR)) break;
    }

    sdhci_write(REG_INTR, INTR_DATA_DONE | INTR_ERR);

    if (timeout == 0 || (intr & INTR_ERR)) {
        printf("[SD] Multi-write complete timeout/error\n");
        return -1;
    }

    return 0;
}

/*
 * Initialize the SD card and controller
 * Implements the SD card initialization sequence from the SD spec
 */
int hal_blk_init(void) {
    uint32_t resp[4];
    int timeout;
    uint32_t ctrl1;

    printf("[SD] Initializing...\n");

    memset(&card, 0, sizeof(card));

    /* Setup GPIO pins for SD interface */
    setup_sd_gpio();

    /* Power on the controller via VideoCore */
    if (power_on_sd() < 0) {
        return -1;
    }

    /* Query base clock */
    card.clk_base = query_emmc_clock();
    printf("[SD] Base clock: %u Hz\n", card.clk_base);

    /* Reset the controller */
    ctrl1 = sdhci_read(REG_CTRL1);
    ctrl1 |= (1 << 24);     /* Software reset */
    ctrl1 &= ~(1 << 2);     /* Disable clock */
    ctrl1 &= ~(1 << 0);     /* Disable internal clock */
    sdhci_write(REG_CTRL1, ctrl1);

    /* Wait for reset to complete */
    timeout = 10000;
    while ((sdhci_read(REG_CTRL1) & (7 << 24)) && --timeout > 0) {
        delay_us(100);
    }
    if (timeout == 0) {
        printf("[SD] Controller reset timeout\n");
        return -1;
    }

    /* Check SDHCI version (must be >= 2) */
    uint32_t ver = sdhci_read(REG_SLOTISR_VER);
    uint32_t sdhci_ver = (ver >> 16) & 0xFF;
    if (sdhci_ver < 2) {
        printf("[SD] Unsupported SDHCI version: %u\n", sdhci_ver);
        return -1;
    }

    /* Enable internal clock */
    ctrl1 = sdhci_read(REG_CTRL1);
    ctrl1 |= (1 << 0);
    sdhci_write(REG_CTRL1, ctrl1);

    /* Set initial clock to 400 kHz (required for card identification) */
    set_sd_clock(400000);

    /* Wait for clock stable */
    timeout = 10000;
    while (!(sdhci_read(REG_CTRL1) & (1 << 1)) && --timeout > 0) {
        delay_us(100);
    }
    if (timeout == 0) {
        printf("[SD] Clock not stable\n");
        return -1;
    }

    /* Enable SD clock output */
    ctrl1 = sdhci_read(REG_CTRL1);
    ctrl1 |= (1 << 2);
    sdhci_write(REG_CTRL1, ctrl1);
    delay_us(2000);

    /* Configure interrupts - disable hardware interrupts, poll status */
    sdhci_write(REG_INTR_EN, 0);
    sdhci_write(REG_INTR, 0xFFFFFFFF);
    sdhci_write(REG_INTR_MASK, 0xFFFFFFFF);

    /* Set data timeout to maximum */
    ctrl1 = sdhci_read(REG_CTRL1);
    ctrl1 |= (0xE << 16);
    sdhci_write(REG_CTRL1, ctrl1);

    /*
     * SD Card Initialization Sequence (from SD spec):
     * 1. CMD0 - Go Idle (reset card)
     * 2. CMD8 - Send Interface Condition (voltage check)
     * 3. ACMD41 - Send Op Cond (initialize, check SDHC)
     * 4. CMD2 - All Send CID (get card ID)
     * 5. CMD3 - Send Relative Addr (get RCA)
     * 6. CMD7 - Select Card (put in transfer mode)
     */

    /* CMD0: GO_IDLE_STATE - reset card to idle */
    if (sd_command(TM_CMD_INDEX(0) | TM_RSP_NONE, 0, NULL) < 0) {
        printf("[SD] CMD0 failed\n");
        return -1;
    }

    /* CMD8: SEND_IF_COND - voltage check + pattern */
    /* Arg: VHS=1 (2.7-3.6V), check pattern=0xAA */
    if (sd_command(TM_CMD_INDEX(8) | TM_RSP_48 | TM_CRC_EN, 0x1AA, resp) < 0) {
        /* CMD8 failed - might be SD v1 card, continue */
        printf("[SD] CMD8 failed (SD v1 card?)\n");
    } else {
        if ((resp[0] & 0xFFF) != 0x1AA) {
            printf("[SD] CMD8 pattern mismatch: 0x%x\n", resp[0]);
            return -1;
        }
    }

    /* ACMD41: SD_SEND_OP_COND - initialize and detect SDHC */
    /* Arg: HCS=1 (support SDHC), voltage window */
    timeout = 100;
    while (--timeout > 0) {
        if (sd_app_command(TM_CMD_INDEX(41) | TM_RSP_48, 0x40FF8000, resp) < 0) {
            printf("[SD] ACMD41 failed\n");
            return -1;
        }

        /* Check if card is ready (bit 31 set) */
        if (resp[0] & (1u << 31)) {
            card.is_sdhc = (resp[0] >> 30) & 1;
            printf("[SD] Card ready, SDHC=%d\n", card.is_sdhc);
            break;
        }

        delay_us(10000);
    }

    if (timeout == 0) {
        printf("[SD] Card init timeout\n");
        return -1;
    }

    /* Switch to 25 MHz for data transfer */
    set_sd_clock(25000000);

    /* CMD2: ALL_SEND_CID - get card identification */
    if (sd_command(TM_CMD_INDEX(2) | TM_RSP_136 | TM_CRC_EN, 0, resp) < 0) {
        printf("[SD] CMD2 failed\n");
        return -1;
    }

    /* CMD3: SEND_RELATIVE_ADDR - get card's RCA */
    if (sd_command(TM_CMD_INDEX(3) | TM_RSP_48 | TM_CRC_EN, 0, resp) < 0) {
        printf("[SD] CMD3 failed\n");
        return -1;
    }
    card.rca = (resp[0] >> 16) & 0xFFFF;
    printf("[SD] RCA: 0x%x\n", card.rca);

    /* CMD7: SELECT_CARD - put card in transfer state */
    if (sd_command(TM_CMD_INDEX(7) | TM_RSP_48_BUSY | TM_CRC_EN, card.rca << 16, resp) < 0) {
        printf("[SD] CMD7 failed\n");
        return -1;
    }

    /* For SDSC cards, set block length to 512 bytes */
    if (!card.is_sdhc) {
        if (sd_command(TM_CMD_INDEX(16) | TM_RSP_48 | TM_CRC_EN, 512, resp) < 0) {
            printf("[SD] CMD16 failed\n");
            return -1;
        }
    }

    /* Set block size in controller */
    sdhci_write(REG_BLKSIZECNT, 512);

    /* Try to enable 4-bit bus mode (ACMD6) */
    if (sd_app_command(TM_CMD_INDEX(6) | TM_RSP_48 | TM_CRC_EN, 2, resp) == 0) {
        /* Enable 4-bit mode in controller */
        uint32_t ctrl0 = sdhci_read(REG_CTRL0);
        ctrl0 |= (1 << 1);  /* DAT_WIDTH = 4 bits */
        sdhci_write(REG_CTRL0, ctrl0);
        printf("[SD] 4-bit mode enabled\n");
    }

    /*
     * Try to enable High Speed mode (CMD6)
     * Arg: 0x80FFFFF1 = Switch, Access Mode = High Speed (function 1)
     * Response is 512 bits (64 bytes) of switch status
     */
    sdhci_write(REG_BLKSIZECNT, (1 << 16) | 64);
    uint32_t cmd6_flags = TM_CMD_INDEX(6) | TM_RSP_48 | TM_CRC_EN | TM_DATA | TM_DATA_READ;
    if (sd_command(cmd6_flags, 0x80FFFFF1, resp) == 0) {
        /* Read 64 bytes of switch status (we don't really need it) */
        uint8_t switch_status[64];
        int hs_ok = 1;
        int hs_timeout = 100000;
        uint32_t intr;
        while (--hs_timeout > 0) {
            intr = sdhci_read(REG_INTR);
            if (intr & (INTR_READ_READY | INTR_ERR)) break;
        }
        if (hs_timeout > 0 && !(intr & INTR_ERR)) {
            sdhci_write(REG_INTR, INTR_READ_READY);
            uint32_t *buf32 = (uint32_t *)switch_status;
            for (int i = 0; i < 16; i++) {
                buf32[i] = sdhci_read(REG_DATA);
            }
            /* Wait for data done */
            hs_timeout = 10000;
            while (--hs_timeout > 0) {
                intr = sdhci_read(REG_INTR);
                if (intr & INTR_DATA_DONE) break;
            }
            sdhci_write(REG_INTR, INTR_DATA_DONE);
        } else {
            hs_ok = 0;
        }

        if (hs_ok) {
            /* Switch to 50 MHz for High Speed mode */
            set_sd_clock(50000000);
            printf("[SD] High Speed mode enabled (50 MHz)\n");
        }
    }

    card.ready = 1;
    printf("[SD] Initialization complete\n");

    /* Initialize DMA for faster block transfers */
    emmc_dma_init();

    return 0;
}

/*
 * Read sectors from the SD card
 * sector: starting sector number (512 bytes each)
 * buf: destination buffer
 * count: number of sectors to read
 */
int hal_blk_read(uint32_t sector, void *buf, uint32_t count) {
    if (!card.ready) {
        printf("[SD] Not initialized\n");
        return -1;
    }

    if (count == 0) return 0;

    /* Disk activity LED */
    disk_activity_led();

    /* SDHC uses block addresses, SDSC uses byte addresses */
    uint32_t addr = card.is_sdhc ? sector : (sector * 512);

    if (count == 1) {
        /* Single block read - use CMD17 */
        /* Note: DMA overhead too high for single 512-byte blocks, use FIFO */
        sdhci_write(REG_BLKSIZECNT, (1 << 16) | 512);

        uint32_t cmd = TM_CMD_INDEX(17) | TM_RSP_48 | TM_CRC_EN | TM_DATA | TM_DATA_READ;
        if (sd_command(cmd, addr, NULL) < 0) {
            printf("[SD] Read command failed at sector %u\n", sector);
            return -1;
        }

        if (read_data_block(buf, 512) < 0) {
            return -1;
        }
    } else {
        /* Multi-block read - use CMD18 with auto CMD12 */
        sdhci_write(REG_BLKSIZECNT, (count << 16) | 512);

        uint32_t cmd = TM_CMD_INDEX(18) | TM_RSP_48 | TM_CRC_EN | TM_DATA | TM_DATA_READ |
                       TM_MULTI_BLK | TM_BLK_CNT_EN | TM_AUTO_CMD12;
        if (sd_command(cmd, addr, NULL) < 0) {
            printf("[SD] Multi-read command failed at sector %u\n", sector);
            return -1;
        }

        /* Use DMA if available, fall back to FIFO */
        if (emmc_dma_enabled) {
            if (read_data_blocks_dma(buf, count) < 0) {
                return -1;
            }
        } else {
            if (read_data_blocks(buf, count) < 0) {
                return -1;
            }
        }
    }

    return 0;
}

/*
 * Write sectors to the SD card
 * sector: starting sector number
 * buf: source buffer
 * count: number of sectors to write
 */
int hal_blk_write(uint32_t sector, const void *buf, uint32_t count) {
    if (!card.ready) {
        printf("[SD] Not initialized\n");
        return -1;
    }

    if (count == 0) return 0;

    /* Disk activity LED */
    disk_activity_led();

    /* SDHC uses block addresses, SDSC uses byte addresses */
    uint32_t addr = card.is_sdhc ? sector : (sector * 512);

    if (count == 1) {
        /* Single block write - use CMD24 */
        sdhci_write(REG_BLKSIZECNT, (1 << 16) | 512);

        uint32_t cmd = TM_CMD_INDEX(24) | TM_RSP_48 | TM_CRC_EN | TM_DATA;
        if (sd_command(cmd, addr, NULL) < 0) {
            printf("[SD] Write command failed at sector %u\n", sector);
            return -1;
        }

        if (write_data_block(buf, 512) < 0) {
            return -1;
        }
    } else {
        /* Multi-block write - use CMD25 with auto CMD12 */
        sdhci_write(REG_BLKSIZECNT, (count << 16) | 512);

        uint32_t cmd = TM_CMD_INDEX(25) | TM_RSP_48 | TM_CRC_EN | TM_DATA |
                       TM_MULTI_BLK | TM_BLK_CNT_EN | TM_AUTO_CMD12;
        if (sd_command(cmd, addr, NULL) < 0) {
            printf("[SD] Multi-write command failed at sector %u\n", sector);
            return -1;
        }

        if (write_data_blocks(buf, count) < 0) {
            return -1;
        }
    }

    return 0;
}
