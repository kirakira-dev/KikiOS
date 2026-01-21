/*
 * KikiOS Pi Zero 2W Interrupt Controller
 *
 * The BCM2837 has a quirky two-tier interrupt system:
 * - Per-core "ARM Local" controller at 0x40000000 handles timers, mailboxes
 * - Legacy "VideoCore" controller at 0x3F00B200 handles peripherals (USB, etc)
 *
 * When a peripheral fires, it signals the VideoCore IC, which then signals
 * the ARM Local controller on bit 8, which finally raises the CPU IRQ line.
 */

#include "../hal.h"
#include "../../printf.h"
#include "../../string.h"
#include "../../process.h"

void led_init(void);
void led_toggle(void);

static void on_timer_tick(void);

/*
 * Per-core controller register block (QA7 ARM Control Block)
 * Documented in BCM2836 QA7 Rev 3.4
 */
#define CORE_CTRL_BASE      0x40000000

static volatile uint32_t *const core_timer_cfg     = (uint32_t *)(CORE_CTRL_BASE + 0x00);
static volatile uint32_t *const core_timer_scale   = (uint32_t *)(CORE_CTRL_BASE + 0x08);
static volatile uint32_t *const core_gpu_route     = (uint32_t *)(CORE_CTRL_BASE + 0x0C);
static volatile uint32_t *const core0_timer_ctl    = (uint32_t *)(CORE_CTRL_BASE + 0x40);
static volatile uint32_t *const core0_irq_src      = (uint32_t *)(CORE_CTRL_BASE + 0x60);

/* Bits in core0_irq_src */
#define CORE_IRQ_PHYS_SECURE    0x01
#define CORE_IRQ_PHYS_NONSEC    0x02
#define CORE_IRQ_HYP_TIMER      0x04
#define CORE_IRQ_VIRT_TIMER     0x08
#define CORE_IRQ_MBOX0          0x10
#define CORE_IRQ_MBOX1          0x20
#define CORE_IRQ_MBOX2          0x40
#define CORE_IRQ_MBOX3          0x80
#define CORE_IRQ_PERIPHERAL     0x100
#define CORE_IRQ_PMU            0x200

/*
 * VideoCore interrupt controller (legacy BCM2835 design)
 * Three banks: basic (0-7), bank1 (0-31), bank2 (32-63)
 */
#define VC_IRQ_BASE         0x3F00B200

static volatile uint32_t *const vc_irq_basic       = (uint32_t *)(VC_IRQ_BASE + 0x00);
static volatile uint32_t *const vc_irq_bank1       = (uint32_t *)(VC_IRQ_BASE + 0x04);
static volatile uint32_t *const vc_irq_bank2       = (uint32_t *)(VC_IRQ_BASE + 0x08);
static volatile uint32_t *const vc_fiq_ctl         = (uint32_t *)(VC_IRQ_BASE + 0x0C);
static volatile uint32_t *const vc_enable_bank1    = (uint32_t *)(VC_IRQ_BASE + 0x10);
static volatile uint32_t *const vc_enable_bank2    = (uint32_t *)(VC_IRQ_BASE + 0x14);
static volatile uint32_t *const vc_enable_basic    = (uint32_t *)(VC_IRQ_BASE + 0x18);
static volatile uint32_t *const vc_disable_bank1   = (uint32_t *)(VC_IRQ_BASE + 0x1C);
static volatile uint32_t *const vc_disable_bank2   = (uint32_t *)(VC_IRQ_BASE + 0x20);
static volatile uint32_t *const vc_disable_basic   = (uint32_t *)(VC_IRQ_BASE + 0x24);

/*
 * The "basic pending" register has a weird layout with shortcuts:
 * [7:0]   - ARM-specific sources
 * [8]     - "something pending in bank1"
 * [9]     - "something pending in bank2"
 * [10:14] - shortcuts to bank1 sources 7,9,10,18,19
 * [15:20] - shortcuts to bank2 sources 21,22,23,24,25,30
 */
#define VC_BASIC_ARM_MASK       0x000000FF
#define VC_BASIC_BANK1_FLAG     0x00000100
#define VC_BASIC_BANK2_FLAG     0x00000200

/* Which bank1/bank2 IRQs appear as shortcuts (in order of bits 10-14, 15-20) */
static const uint8_t bank1_fast_irqs[] = { 7, 9, 10, 18, 19 };
static const uint8_t bank2_fast_irqs[] = { 21, 22, 23, 24, 25, 30 };

/*
 * Our unified IRQ numbering:
 *   0-7:   ARM local sources (timers, mailboxes, etc)
 *   8-39:  VideoCore bank1 (peripheral IRQs 0-31)
 *   40-71: VideoCore bank2 (peripheral IRQs 32-63)
 */
#define TOTAL_IRQS          72
#define IRQ_TIMER_NS        1       /* Non-secure physical timer */
#define IRQ_VC_USB          (8 + 9) /* USB is bank1 IRQ 9 */

static void (*dispatch_table[TOTAL_IRQS])(void);
static uint32_t tick_period_ms = 1;  // 1ms = 1000Hz polling (USB spec max)
static uint64_t tick_count = 0;

/* Count trailing zeros - returns bit position of lowest set bit, or 32 if zero */
static inline uint32_t ctz32(uint32_t v) {
    if (v == 0) return 32;
    uint32_t pos = 0;
    if ((v & 0x0000FFFF) == 0) { pos += 16; v >>= 16; }
    if ((v & 0x000000FF) == 0) { pos += 8;  v >>= 8;  }
    if ((v & 0x0000000F) == 0) { pos += 4;  v >>= 4;  }
    if ((v & 0x00000003) == 0) { pos += 2;  v >>= 2;  }
    if ((v & 0x00000001) == 0) { pos += 1; }
    return pos;
}

static inline void mem_barrier(void) {
    asm volatile("dsb sy" ::: "memory");
}

/*
 * Configure the per-core timer block
 * Uses the 19.2MHz crystal as source with 1:1 prescaler
 */
static void setup_core_timer_block(void) {
    /* Crystal source, no doubling */
    *core_timer_cfg = 0;
    mem_barrier();

    /* Prescaler for 1:1 ratio: value = 2^31 gives freq_out = freq_in */
    *core_timer_scale = 0x80000000;
    mem_barrier();

    /* Route peripheral (GPU) interrupts to this core */
    *core_gpu_route = 0;
    mem_barrier();

    /* Enable the non-secure physical timer interrupt */
    *core0_timer_ctl = CORE_IRQ_PHYS_NONSEC;
    mem_barrier();

    printf("[IRQ] Core timer block configured\n");
}

/*
 * Reset the VideoCore interrupt controller to a known state
 */
static void reset_videocore_ic(void) {
    /* Turn everything off */
    *vc_disable_basic = 0xFFFFFFFF;
    *vc_disable_bank1 = 0xFFFFFFFF;
    *vc_disable_bank2 = 0xFFFFFFFF;
    mem_barrier();

    /* No FIQ routing */
    *vc_fiq_ctl = 0;
    mem_barrier();

    printf("[IRQ] VideoCore IC reset\n");
}

/*
 * Process pending VideoCore peripheral interrupts
 * Called when CORE_IRQ_PERIPHERAL is set in the core IRQ source register
 */
static void service_peripheral_irqs(void) {
    uint32_t basic = *vc_irq_basic;

    /* ARM-local sources in bits 0-7 (rarely used) */
    uint32_t arm_pending = basic & VC_BASIC_ARM_MASK;
    while (arm_pending) {
        uint32_t n = ctz32(arm_pending);
        if (dispatch_table[n]) dispatch_table[n]();
        arm_pending &= ~(1u << n);
    }

    /*
     * Shortcut bits let us quickly see certain high-priority IRQs
     * without reading the full bank registers. Handle these first.
     */
    uint32_t shortcuts1 = (basic >> 10) & 0x1F;  /* bits 10-14 */
    for (uint32_t i = 0; shortcuts1 && i < 5; i++) {
        if (shortcuts1 & (1u << i)) {
            uint32_t irq_num = 8 + bank1_fast_irqs[i];
            if (dispatch_table[irq_num]) dispatch_table[irq_num]();
            shortcuts1 &= ~(1u << i);
        }
    }

    uint32_t shortcuts2 = (basic >> 15) & 0x3F;  /* bits 15-20 */
    for (uint32_t i = 0; shortcuts2 && i < 6; i++) {
        if (shortcuts2 & (1u << i)) {
            uint32_t irq_num = 40 + bank2_fast_irqs[i];
            if (dispatch_table[irq_num]) dispatch_table[irq_num]();
            shortcuts2 &= ~(1u << i);
        }
    }

    /* Bank 1 has more pending? Scan the full register */
    if (basic & VC_BASIC_BANK1_FLAG) {
        uint32_t b1 = *vc_irq_bank1;
        while (b1) {
            uint32_t n = ctz32(b1);
            uint32_t irq_num = 8 + n;
            if (dispatch_table[irq_num]) dispatch_table[irq_num]();
            b1 &= ~(1u << n);
        }
    }

    /* Same for bank 2 */
    if (basic & VC_BASIC_BANK2_FLAG) {
        uint32_t b2 = *vc_irq_bank2;
        while (b2) {
            uint32_t n = ctz32(b2);
            uint32_t irq_num = 40 + n;
            if (dispatch_table[irq_num]) dispatch_table[irq_num]();
            b2 &= ~(1u << n);
        }
    }
}

/*
 * Top-level IRQ handler, called from exception vectors
 */
void handle_irq(void) {
    uint32_t src = *core0_irq_src;

    /* Physical timer fired? */
    if (src & CORE_IRQ_PHYS_NONSEC) {
        if (dispatch_table[IRQ_TIMER_NS]) {
            dispatch_table[IRQ_TIMER_NS]();
        }
    }

    /* Something from the VideoCore? */
    if (src & CORE_IRQ_PERIPHERAL) {
        service_peripheral_irqs();
    }

    /* PMU could be handled here if needed */
}

// DWC2 registers for debug
#define USB_BASE_ADDR   0x3F980000
#define DWC2_GINTSTS    (*(volatile uint32_t *)(USB_BASE_ADDR + 0x014))
#define DWC2_GINTMSK    (*(volatile uint32_t *)(USB_BASE_ADDR + 0x018))
#define DWC2_GAHBCFG    (*(volatile uint32_t *)(USB_BASE_ADDR + 0x008))
#define DWC2_HPRT0      (*(volatile uint32_t *)(USB_BASE_ADDR + 0x440))
#define DWC2_HFNUM      (*(volatile uint32_t *)(USB_BASE_ADDR + 0x408))

/*
 * Timer tick handler - reschedules itself for the next interval
 */
static void on_timer_tick(void) {
    tick_count++;

    /* Reload for next tick */
    uint64_t freq;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    asm volatile("msr cntp_tval_el0, %0" :: "r"((freq * tick_period_ms) / 1000));

    // Heartbeat LED - toggle every 500ms (50 ticks) = 1Hz
    // (Disk activity will override with faster blinks during I/O)
    if ((tick_count % 50) == 0) {
        led_toggle();
    }

    // Poll USB keyboard every tick (10ms)
    // This is much more efficient than SOF-based polling (1000 IRQs/sec)
    hal_usb_keyboard_tick();

    // Preemptive scheduling - switch every 20 ticks (200ms timeslice)
    if ((tick_count % 20) == 0) {
        process_schedule_from_irq();
    }

    // NOTE: Cursor blink disabled on Pi - was interfering with USB keyboard
    // TODO: Investigate why console_blink_cursor() breaks USB on real hardware

    // Debug: check USB interrupt status every second (disabled)
    // if (tick_count % 100 == 0) {
    //     uint32_t gintsts = DWC2_GINTSTS;
    //     uint32_t hprt0 = DWC2_HPRT0;
    //     uint32_t hfnum = DWC2_HFNUM;
    //     printf("[TICK %llu] GINTSTS=%08x HPRT0=%08x HFNUM=%08x\n",
    //            tick_count, gintsts, hprt0, hfnum);
    // }
}

/* ========== Public HAL interface ========== */

void hal_irq_init(void) {
    for (int i = 0; i < TOTAL_IRQS; i++) {
        dispatch_table[i] = 0;
    }

    setup_core_timer_block();
    reset_videocore_ic();
    led_init();

    dispatch_table[IRQ_TIMER_NS] = on_timer_tick;

    printf("[IRQ] Pi interrupt system ready\n");
}

void hal_irq_enable(void) {
    asm volatile("msr daifclr, #2" ::: "memory");
}

void hal_irq_disable(void) {
    asm volatile("msr daifset, #2" ::: "memory");
}

void hal_irq_enable_irq(uint32_t irq) {
    if (irq < 8) {
        /* Core-local: only timer supported currently */
        if (irq == IRQ_TIMER_NS) {
            *core0_timer_ctl |= CORE_IRQ_PHYS_NONSEC;
            mem_barrier();
        }
    } else if (irq < 40) {
        *vc_enable_bank1 = 1u << (irq - 8);
        mem_barrier();
    } else if (irq < TOTAL_IRQS) {
        *vc_enable_bank2 = 1u << (irq - 40);
        mem_barrier();
    }
}

void hal_irq_disable_irq(uint32_t irq) {
    if (irq < 8) {
        if (irq == IRQ_TIMER_NS) {
            *core0_timer_ctl &= ~CORE_IRQ_PHYS_NONSEC;
            mem_barrier();
        }
    } else if (irq < 40) {
        *vc_disable_bank1 = 1u << (irq - 8);
        mem_barrier();
    } else if (irq < TOTAL_IRQS) {
        *vc_disable_bank2 = 1u << (irq - 40);
        mem_barrier();
    }
}

void hal_irq_register_handler(uint32_t irq, void (*handler)(void)) {
    if (irq < TOTAL_IRQS) {
        dispatch_table[irq] = handler;
    }
}

/* ========== Timer HAL ========== */

void hal_timer_init(uint32_t interval_ms) {
    tick_period_ms = interval_ms;
    tick_count = 0;

    uint64_t freq;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));

    printf("[TIMER] Clock: %llu Hz, period: %u ms\n", freq, interval_ms);

    /* Program countdown value */
    asm volatile("msr cntp_tval_el0, %0" :: "r"((freq * interval_ms) / 1000));

    /* Enable timer, don't mask output */
    asm volatile("msr cntp_ctl_el0, %0" :: "r"(1UL));

    printf("[TIMER] Generic timer running\n");
}

uint64_t hal_timer_get_ticks(void) {
    return tick_count;
}

void hal_timer_set_interval(uint32_t interval_ms) {
    tick_period_ms = interval_ms;
}
