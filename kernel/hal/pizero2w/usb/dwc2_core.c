/*
 * DWC2 Core Functions
 * Low-level initialization, cache ops, mailbox, port control
 */

#include "dwc2_core.h"
#include "dwc2_regs.h"
#include "../../hal.h"
#include "../../../printf.h"
#include "../../../string.h"

// Global USB state
usb_state_t usb_state = {0};

// DMA buffer for USB transfers (64-byte aligned for Cortex-A53 cache line size)
uint8_t __attribute__((aligned(64))) dma_buffer[512];

// Mailbox buffer (16-byte aligned)
volatile uint32_t __attribute__((aligned(16))) mbox_buf[36];

// ============================================================================
// Memory Barriers
// ============================================================================

inline void dmb(void) {
    asm volatile("dmb sy" ::: "memory");
}

inline void dsb(void) {
    asm volatile("dsb sy" ::: "memory");
}

// ============================================================================
// Timing Functions
// ============================================================================

// Use kernel's timer-based sleep (interrupts must be enabled first!)
extern void sleep_ms(uint32_t ms);

void usleep(uint32_t us) {
    uint32_t start = hal_get_time_us();
    while ((hal_get_time_us() - start) < us) {
        // Busy wait
    }
}

void msleep(uint32_t ms) {
    // Use kernel's sleep_ms which uses timer ticks (works now that IRQs are enabled)
    sleep_ms(ms);
}

// ============================================================================
// Cache Operations (required for DMA on real hardware)
// ============================================================================

void clean_data_cache_range(uintptr_t start, size_t length) {
    uintptr_t line_size;
    // Read cache line size from CTR_EL0
    asm volatile("mrs %0, ctr_el0" : "=r" (line_size));
    // Extract DminLine (bits [19:16]), encoded as log2(words)
    uint32_t dminline = (line_size >> 16) & 0xF;
    // Convert to bytes: 4 * 2^dminline
    size_t step = 4 << dminline;

    uintptr_t addr = start & ~(step - 1);
    uintptr_t end = start + length;

    // Clean data cache by virtual address to point of coherency
    while (addr < end) {
        asm volatile("dc cvac, %0" :: "r" (addr));
        addr += step;
    }
    asm volatile("dsb sy" ::: "memory");
}

void invalidate_data_cache_range(uintptr_t start, size_t length) {
    uintptr_t line_size;
    asm volatile("mrs %0, ctr_el0" : "=r" (line_size));
    uint32_t dminline = (line_size >> 16) & 0xF;
    size_t step = 4 << dminline;

    uintptr_t addr = start & ~(step - 1);
    uintptr_t end = start + length;

    // Use clean-and-invalidate (dc civac) instead of just invalidate (dc ivac)
    // This is safer because dc ivac on dirty lines has undefined behavior
    // dc civac first writes back dirty data, then invalidates
    while (addr < end) {
        asm volatile("dc civac, %0" :: "r" (addr));
        addr += step;
    }
    asm volatile("dsb sy" ::: "memory");
}

// ============================================================================
// Address Conversion
// ============================================================================

uint32_t arm_to_bus(void *ptr) {
    return ((uint32_t)(uint64_t)ptr) | 0xC0000000;
}

// ============================================================================
// Mailbox Functions
// ============================================================================

static int mbox_write(uint32_t channel, uint32_t data) {
    // 1 second timeout
    uint32_t start = hal_get_time_us();
    while (MAILBOX_STATUS & MAILBOX_FULL) {
        dmb();
        if ((hal_get_time_us() - start) > 1000000) {
            printf("[USB] mbox_write: timeout waiting for mailbox\n");
            return -1;
        }
    }
    dmb();
    MAILBOX_WRITE = (data & 0xFFFFFFF0) | (channel & 0xF);
    dmb();
    return 0;
}

static uint32_t mbox_read(uint32_t channel) {
    uint32_t data;
    // 1 second timeout
    uint32_t start = hal_get_time_us();

    while ((hal_get_time_us() - start) < 1000000) {
        while (MAILBOX_STATUS & MAILBOX_EMPTY) {
            dmb();
            if ((hal_get_time_us() - start) > 1000000) {
                printf("[USB] mbox_read: timeout waiting for data\n");
                return 0xFFFFFFFF;
            }
        }
        dmb();
        data = MAILBOX_READ;
        dmb();
        if ((data & 0xF) == channel) {
            return data & 0xFFFFFFF0;
        }
    }
    printf("[USB] mbox_read: timeout, wrong channel\n");
    return 0xFFFFFFFF;
}

int usb_set_power(int on) {
    usb_debug("[USB] Setting power %s\n", on ? "ON" : "OFF");

    uint32_t idx = 0;
    mbox_buf[idx++] = 8 * 4;        // Message size
    mbox_buf[idx++] = 0;            // Request

    // Set power state tag
    mbox_buf[idx++] = 0x00028001;   // Tag: set power state
    mbox_buf[idx++] = 8;            // Value size
    mbox_buf[idx++] = 8;            // Request
    mbox_buf[idx++] = DEVICE_ID_USB_HCD;  // Device ID: USB HCD
    mbox_buf[idx++] = on ? 3 : 0;   // State: on + wait, or off

    mbox_buf[idx++] = 0;            // End tag

    // Clean cache so GPU sees our writes
    clean_data_cache_range((uintptr_t)mbox_buf, sizeof(mbox_buf));
    dmb();

    mbox_write(MAILBOX_CH_PROP, arm_to_bus((void *)mbox_buf));
    mbox_read(MAILBOX_CH_PROP);
    dmb();

    // Invalidate cache so we see GPU's response
    invalidate_data_cache_range((uintptr_t)mbox_buf, sizeof(mbox_buf));

    if (mbox_buf[1] != 0x80000000) {
        printf("[USB] Power control failed: %08x\n", mbox_buf[1]);
        return -1;
    }

    uint32_t state = mbox_buf[6];
    if (on && (state & 0x3) != 1) {
        printf("[USB] USB did not power on: %08x\n", state);
        return -1;
    }

    usb_debug("[USB] Power %s successful\n", on ? "ON" : "OFF");
    return 0;
}

// ============================================================================
// Core Reset and Initialization
// ============================================================================

int usb_core_reset(void) {
    usb_debug("[USB] Core reset...\n");

    usb_debug("[USB] Reading GRSTCTL...\n");
    uint32_t val = GRSTCTL;
    usb_debug("[USB] GRSTCTL = %08x\n", val);

    // Wait for AHB master idle
    usb_debug("[USB] Waiting for AHB idle...\n");
    int timeout = 100000;
    while (!(GRSTCTL & GRSTCTL_AHBIDLE) && timeout--) {
        usleep(1);
    }
    if (timeout <= 0) {
        printf("[USB] Timeout waiting for AHB idle\n");
        return -1;
    }
    usb_debug("[USB] AHB idle OK\n");

    // Trigger core soft reset
    usb_debug("[USB] Triggering soft reset...\n");
    GRSTCTL = GRSTCTL_CSFTRST;
    dsb();
    usb_debug("[USB] Soft reset triggered\n");

    // Wait for reset to complete (hardware clears the bit)
    usb_debug("[USB] Waiting for reset complete...\n");
    timeout = 100000;
    while ((GRSTCTL & GRSTCTL_CSFTRST) && timeout--) {
        usleep(1);
    }
    if (timeout <= 0) {
        printf("[USB] Timeout waiting for reset complete\n");
        return -1;
    }
    usb_debug("[USB] Reset complete\n");

    // Wait for AHB idle again
    usb_debug("[USB] Waiting for AHB idle again...\n");
    timeout = 100000;
    while (!(GRSTCTL & GRSTCTL_AHBIDLE) && timeout--) {
        usleep(1);
    }
    if (timeout <= 0) {
        printf("[USB] Timeout waiting for AHB idle after reset\n");
        return -1;
    }
    usb_debug("[USB] AHB idle again OK\n");

    // Wait a bit for things to settle
    usb_debug("[USB] Settling...\n");
    msleep(100);

    usb_debug("[USB] Core reset complete\n");
    return 0;
}

int usb_flush_fifos(void) {
    // Flush all TxFIFOs
    GRSTCTL = GRSTCTL_TXFFLSH | GRSTCTL_TXFNUM_ALL;
    dsb();

    int timeout = 10000;
    while ((GRSTCTL & GRSTCTL_TXFFLSH) && timeout--) {
        usleep(1);
    }
    if (timeout <= 0) {
        printf("[USB] TxFIFO flush timeout\n");
        return -1;
    }

    // Flush RxFIFO
    GRSTCTL = GRSTCTL_RXFFLSH;
    dsb();

    timeout = 10000;
    while ((GRSTCTL & GRSTCTL_RXFFLSH) && timeout--) {
        usleep(1);
    }
    if (timeout <= 0) {
        printf("[USB] RxFIFO flush timeout\n");
        return -1;
    }

    msleep(1);
    return 0;
}

// ============================================================================
// Host Mode Setup
// ============================================================================

int usb_init_host(void) {
    usb_debug("[USB] Initializing host mode...\n");

    // Read hardware config to determine capabilities
    uint32_t hwcfg2 = GHWCFG2;
    uint32_t hwcfg3 = GHWCFG3;
    uint32_t hwcfg4 = GHWCFG4;

    usb_state.num_channels = ((hwcfg2 >> 14) & 0xF) + 1;
    uint32_t fifo_depth = (hwcfg3 >> 16) & 0xFFFF;

    usb_debug("[USB] HWCFG2: %08x, HWCFG3: %08x, HWCFG4: %08x\n", hwcfg2, hwcfg3, hwcfg4);
    usb_debug("[USB] Channels: %d, FIFO depth: %u words\n", usb_state.num_channels, fifo_depth);

    // Configure USB for host mode
    // Pi Zero 2W uses the internal DWC2 PHY
    uint32_t usbcfg = GUSBCFG;

    usb_debug("[USB] Initial GUSBCFG: %08x\n", usbcfg);

    // Clear mode forcing bits first
    usbcfg &= ~(GUSBCFG_FORCEDEVMODE | GUSBCFG_FORCEHOSTMODE);

    // Don't set PHYSEL - Pi uses the integrated HS PHY in FS mode
    // PHYSEL=0 means use the high-speed capable PHY
    usbcfg &= ~GUSBCFG_PHYSEL;

    // Use UTMI+ interface (not ULPI)
    usbcfg &= ~GUSBCFG_ULPI_UTMI_SEL;

    // 8-bit UTMI+ interface
    usbcfg &= ~GUSBCFG_PHYIF;

    GUSBCFG = usbcfg;
    dsb();
    msleep(10);

    // Now force host mode
    usbcfg |= GUSBCFG_FORCEHOSTMODE;
    GUSBCFG = usbcfg;
    dsb();

    usb_debug("[USB] Final GUSBCFG: %08x\n", GUSBCFG);

    // Wait for host mode (can take up to 25ms per spec)
    msleep(50);

    if (!(GINTSTS & GINTSTS_CURMODE)) {
        printf("[USB] Failed to enter host mode\n");
        return -1;
    }

    usb_debug("[USB] Host mode active\n");

    // Configure FIFOs
    // RxFIFO: 256 words (1024 bytes) - receives all IN data
    // Non-periodic TxFIFO: 256 words (1024 bytes) - control/bulk OUT
    // Periodic TxFIFO: 256 words (1024 bytes) - interrupt/isochronous OUT
    GRXFSIZ = 256;
    GNPTXFSIZ = (256 << 16) | 256;      // Size | Start address
    HPTXFSIZ = (256 << 16) | 512;       // Size | Start address
    dsb();

    // Flush FIFOs after sizing
    usb_flush_fifos();

    // Host configuration
    // Pi uses UTMI+ PHY at 60MHz - use FSLSPCLKSEL=0 (30/60 MHz mode)
    // Force FS/LS only mode to avoid split transaction issues with HS hubs
    HCFG = HCFG_FSLSPCLKSEL_30_60 | HCFG_FSLSUPP;
    dsb();

    // Frame interval for 60MHz PHY
    HFIR = 60000;
    dsb();

    // Configure AHB for DMA mode (interrupts enabled later after handler registered)
    // QEMU's DWC2 emulation only supports DMA mode, not slave mode
    GAHBCFG = GAHBCFG_DMA_EN;
    dsb();
    usb_debug("[USB] DMA mode enabled (GAHBCFG=%08x)\n", GAHBCFG);

    // Clear all pending interrupts
    GINTSTS = 0xFFFFFFFF;

    // Enable relevant interrupts
    // NOTE: SOF is NOT enabled - it fires 1000x/sec and kills performance
    // Keyboard polling is driven by timer tick instead
    GINTMSK = GINTSTS_PRTINT |      // Port interrupt
              GINTSTS_HCHINT |      // Host channel interrupt
              GINTSTS_DISCONNINT |  // Disconnect
              GINTSTS_CONIDSTSCHNG; // Connector ID change
    dsb();

    usb_debug("[USB] Host initialization complete\n");
    return 0;
}

// ============================================================================
// Port Control
// ============================================================================

int usb_port_power_on(void) {
    usb_debug("[USB] Powering on port...\n");

    // Read current port status (preserve certain bits, clear W1C bits)
    uint32_t hprt = HPRT0;

    // Clear W1C bits so we don't accidentally clear them
    hprt &= ~(HPRT0_PRTENA | HPRT0_PRTCONNDET | HPRT0_PRTENCHNG | HPRT0_PRTOVRCURRCHNG);

    // Set port power
    hprt |= HPRT0_PRTPWR;
    HPRT0 = hprt;
    dsb();

    // Wait for power to stabilize
    msleep(50);

    usb_debug("[USB] Port power on, HPRT0: %08x\n", HPRT0);
    return 0;
}

int usb_port_reset(void) {
    usb_debug("[USB] Resetting port...\n");

    // Read port status
    uint32_t hprt = HPRT0;
    hprt &= ~(HPRT0_PRTENA | HPRT0_PRTCONNDET | HPRT0_PRTENCHNG | HPRT0_PRTOVRCURRCHNG);

    // Assert reset
    hprt |= HPRT0_PRTRST;
    HPRT0 = hprt;
    dsb();

    // Hold reset for 50ms (USB spec requires at least 10ms)
    msleep(50);

    // De-assert reset
    hprt = HPRT0;
    hprt &= ~(HPRT0_PRTENA | HPRT0_PRTCONNDET | HPRT0_PRTENCHNG | HPRT0_PRTOVRCURRCHNG);
    hprt &= ~HPRT0_PRTRST;
    HPRT0 = hprt;
    dsb();

    // Wait for port to become enabled
    msleep(20);

    hprt = HPRT0;
    usb_debug("[USB] After reset, HPRT0: %08x\n", hprt);

    if (!(hprt & HPRT0_PRTENA)) {
        printf("[USB] Port not enabled after reset\n");
        return -1;
    }

    // Get device speed
    usb_state.device_speed = (hprt & HPRT0_PRTSPD_MASK) >> HPRT0_PRTSPD_SHIFT;
    const char *speed_str[] = {"High", "Full", "Low"};
    usb_info("[USB] Device speed: %s\n", speed_str[usb_state.device_speed]);

    // Configure HCFG and HFIR based on PHY type
    // Pi uses UTMI+ PHY which runs at 60MHz, even for FS/LS devices
    // FSLSPCLKSEL = 0 means 30/60 MHz (for UTMI+ HS PHY)
    // FSLSPCLKSEL = 1 means 48 MHz (for dedicated FS PHY - NOT on Pi)
    // Force FS/LS only mode to avoid split transaction issues with HS hubs
    HCFG = HCFG_FSLSPCLKSEL_30_60 | HCFG_FSLSUPP;
    HFIR = 60000;  // 60MHz * 1ms = 60000 clocks per frame
    dsb();
    usb_debug("[USB] HCFG=%08x HFIR=%08x\n", HCFG, HFIR);

    return 0;
}

int usb_wait_for_device(void) {
    usb_debug("[USB] Waiting for device connection...\n");

    // Check if already connected
    uint32_t hprt = HPRT0;
    if (hprt & HPRT0_PRTCONNSTS) {
        usb_debug("[USB] Device already connected\n");
        usb_state.device_connected = 1;
        return 0;
    }

    // Wait up to 5 seconds for connection
    for (int i = 0; i < 50; i++) {
        hprt = HPRT0;
        if (hprt & HPRT0_PRTCONNSTS) {
            usb_debug("[USB] Device connected!\n");
            usb_state.device_connected = 1;
            // Clear connect detect
            HPRT0 = (hprt & ~(HPRT0_PRTENA | HPRT0_PRTENCHNG | HPRT0_PRTOVRCURRCHNG)) | HPRT0_PRTCONNDET;
            return 0;
        }
        msleep(100);
    }

    printf("[USB] No device connected\n");
    return -1;
}

// ============================================================================
// Channel Control
// ============================================================================

void usb_halt_channel(int ch) {
    uint32_t hcchar = HCCHAR(ch);

    if (!(hcchar & HCCHAR_CHENA)) {
        return;  // Already disabled
    }

    // Disable channel
    hcchar |= HCCHAR_CHDIS;
    hcchar &= ~HCCHAR_CHENA;
    HCCHAR(ch) = hcchar;
    dsb();

    // Wait for channel halted
    int timeout = 10000;
    while (!(HCINT(ch) & HCINT_CHHLTD) && timeout--) {
        usleep(1);
    }

    // Clear interrupt
    HCINT(ch) = 0xFFFFFFFF;
}
