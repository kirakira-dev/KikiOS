/*
 * Raspberry Pi Platform Info
 *
 * Supports Pi Zero 2W, Pi 3 B, Pi 3 B+ via runtime detection.
 */

#include "../hal.h"
#include "usb/usb_types.h"

// Pi system timer (1MHz free-running counter)
#define PI_SYSTIMER_LO  (*(volatile uint32_t *)0x3F003004)

// Mailbox registers
#define PERI_BASE       0x3F000000
#define MAILBOX_BASE    (PERI_BASE + 0x00B880)
#define MAILBOX_READ    (*(volatile uint32_t *)(MAILBOX_BASE + 0x00))
#define MAILBOX_STATUS  (*(volatile uint32_t *)(MAILBOX_BASE + 0x18))
#define MAILBOX_WRITE   (*(volatile uint32_t *)(MAILBOX_BASE + 0x20))
#define MAILBOX_FULL    0x80000000
#define MAILBOX_EMPTY   0x40000000
#define MAILBOX_CH_PROP 8

// Property tags
#define TAG_GET_BOARD_REVISION  0x00010002
#define TAG_GET_ARM_MEMORY      0x00010005
#define TAG_END                 0x00000000

// Cache maintenance
#define CACHE_LINE_SIZE 64

static void cache_clean_range(const void *start, uint32_t len) {
    uintptr_t addr = (uintptr_t)start & ~(CACHE_LINE_SIZE - 1);
    uintptr_t end = (uintptr_t)start + len;
    while (addr < end) {
        asm volatile("dc cvac, %0" : : "r"(addr) : "memory");
        addr += CACHE_LINE_SIZE;
    }
    asm volatile("dsb sy" ::: "memory");
}

static void cache_invalidate_range(void *start, uint32_t len) {
    uintptr_t addr = (uintptr_t)start & ~(CACHE_LINE_SIZE - 1);
    uintptr_t end = (uintptr_t)start + len;
    while (addr < end) {
        asm volatile("dc civac, %0" : : "r"(addr) : "memory");
        addr += CACHE_LINE_SIZE;
    }
    asm volatile("dsb sy" ::: "memory");
}

// Mailbox buffer (16-byte aligned)
static volatile uint32_t __attribute__((aligned(16))) mbox_buf[36];

// Cached board info
static uint32_t board_revision = 0;
static uint32_t arm_mem_base = 0;
static uint32_t arm_mem_size = 0;
static int board_detected = 0;

// Detect board via mailbox
static void detect_board(void) {
    if (board_detected) return;

    // Query board revision
    uint32_t idx = 0;
    mbox_buf[idx++] = 0;                    // Size (filled later)
    mbox_buf[idx++] = 0;                    // Request
    mbox_buf[idx++] = TAG_GET_BOARD_REVISION;
    mbox_buf[idx++] = 4;                    // Buffer size
    mbox_buf[idx++] = 0;                    // Request
    mbox_buf[idx++] = 0;                    // Will contain revision
    mbox_buf[idx++] = TAG_GET_ARM_MEMORY;
    mbox_buf[idx++] = 8;                    // Buffer size
    mbox_buf[idx++] = 0;                    // Request
    mbox_buf[idx++] = 0;                    // Base address
    mbox_buf[idx++] = 0;                    // Size
    mbox_buf[idx++] = TAG_END;
    mbox_buf[0] = idx * 4;                  // Total size

    // Clean cache
    cache_clean_range((void *)mbox_buf, sizeof(mbox_buf));
    asm volatile("dmb sy" ::: "memory");

    // Send to mailbox
    uint32_t bus_addr = ((uint32_t)(uint64_t)mbox_buf) | 0xC0000000;

    // Wait for mailbox to be not full
    while (MAILBOX_STATUS & MAILBOX_FULL) {
        asm volatile("dmb sy" ::: "memory");
    }
    MAILBOX_WRITE = (bus_addr & 0xFFFFFFF0) | MAILBOX_CH_PROP;
    asm volatile("dmb sy" ::: "memory");

    // Wait for response
    uint32_t data;
    do {
        while (MAILBOX_STATUS & MAILBOX_EMPTY) {
            asm volatile("dmb sy" ::: "memory");
        }
        data = MAILBOX_READ;
        asm volatile("dmb sy" ::: "memory");
    } while ((data & 0xF) != MAILBOX_CH_PROP);

    // Invalidate cache to see response
    cache_invalidate_range((void *)mbox_buf, sizeof(mbox_buf));

    // Check success
    if (mbox_buf[1] == 0x80000000) {
        board_revision = mbox_buf[5];
        arm_mem_base = mbox_buf[9];
        arm_mem_size = mbox_buf[10];
    }

    board_detected = 1;
}

// Get board type from revision code
// Returns: 0x8=3B, 0xD=3B+, 0x12=Zero2W, etc.
static int get_board_type(void) {
    if (!board_detected) detect_board();

    // New-style revision (bit 23 set)
    if (board_revision & (1 << 23)) {
        return (board_revision >> 4) & 0xFF;
    }
    // Old-style revision - just return raw
    return board_revision & 0xFF;
}

const char *hal_platform_name(void) {
    if (!board_detected) detect_board();

    int type = get_board_type();

    switch (type) {
        case 0x04: return "Raspberry Pi 2 Model B";
        case 0x08: return "Raspberry Pi 3 Model B";
        case 0x09: return "Raspberry Pi Zero";
        case 0x0C: return "Raspberry Pi Zero W";
        case 0x0D: return "Raspberry Pi 3 Model B+";
        case 0x0E: return "Raspberry Pi 3 Model A+";
        case 0x11: return "Raspberry Pi 4 Model B";
        case 0x12: return "Raspberry Pi Zero 2 W";
        case 0x13: return "Raspberry Pi 400";
        case 0x14: return "Raspberry Pi Compute Module 4";
        case 0x17: return "Raspberry Pi 5";
        default:   return "Raspberry Pi (Unknown)";
    }
}

// Get RAM size in bytes
uint64_t hal_get_ram_size(void) {
    if (!board_detected) detect_board();
    return (uint64_t)arm_mem_size;
}

// Get board revision code (for debugging)
uint32_t hal_get_board_revision(void) {
    if (!board_detected) detect_board();
    return board_revision;
}

void hal_wfi(void) {
    asm volatile("wfi");
}

// Microsecond timer - reads directly from Pi system timer
// Available very early in boot, no initialization required
uint32_t hal_get_time_us(void) {
    return PI_SYSTIMER_LO;
}

// CPU Info - BCM2710 with Cortex-A53 cores
const char *hal_get_cpu_name(void) {
    return "Cortex-A53";
}

uint32_t hal_get_cpu_freq_mhz(void) {
    return 1000;  // Pi Zero 2W runs at 1GHz
}

int hal_get_cpu_cores(void) {
    return 4;  // Pi Zero 2W has 4 cores
}

// USB Device List
int hal_usb_get_device_count(void) {
    return usb_state.num_devices;
}

int hal_usb_get_device_info(int idx, uint16_t *vid, uint16_t *pid,
                            char *name, int name_len) {
    if (idx < 0 || idx >= usb_state.num_devices) {
        return -1;
    }

    usb_device_t *dev = &usb_state.devices[idx];

    // VID/PID not stored in current implementation - return 0
    if (vid) *vid = 0;
    if (pid) *pid = 0;

    // Generate name from device type
    if (name && name_len > 0) {
        const char *desc;
        if (dev->is_hub) {
            desc = "USB Hub";
        } else if (usb_state.keyboard_addr == dev->address) {
            desc = "USB Keyboard";
        } else {
            desc = "USB Device";
        }

        // Copy name
        int i;
        for (i = 0; desc[i] && i < name_len - 1; i++) {
            name[i] = desc[i];
        }
        name[i] = '\0';
    }

    return 0;
}
