/*
 * Raspberry Pi Zero 2W Framebuffer Driver
 *
 * Uses VideoCore mailbox to request framebuffer from GPU
 *
 * Pi Zero 2W (BCM2710) memory map:
 * - ARM physical addresses: 0x00000000 - 0x3FFFFFFF
 * - Peripherals: 0x3F000000 (mapped from bus 0x7E000000)
 * - Kernel loads at: 0x80000
 *
 * Mailbox is at 0x3F00B880 (ARM physical)
 */

#include "../hal.h"

// Forward declarations for printf (may not be available early)
extern void uart_putc(char c);

// Framebuffer info
static hal_fb_info_t fb_info = {0};
static uint32_t virtual_height = 0;  // Total virtual framebuffer height
static uint32_t current_scroll_y = 0;  // Current scroll offset

// Mailbox registers (ARM physical addresses for Pi Zero 2W / BCM2710)
#define MAILBOX_BASE        0x3F00B880

#define MAILBOX_READ        (*(volatile uint32_t *)(MAILBOX_BASE + 0x00))
#define MAILBOX_STATUS      (*(volatile uint32_t *)(MAILBOX_BASE + 0x18))
#define MAILBOX_WRITE       (*(volatile uint32_t *)(MAILBOX_BASE + 0x20))

#define MAILBOX_FULL        0x80000000
#define MAILBOX_EMPTY       0x40000000

// Mailbox channels
#define MAILBOX_CH_PROP     8   // Property tags (ARM -> VC)

// Property tags
#define TAG_END             0x00000000
#define TAG_SET_PHYS_WH     0x00048003  // Set physical display width/height
#define TAG_SET_VIRT_WH     0x00048004  // Set virtual display width/height
#define TAG_SET_DEPTH       0x00048005  // Set bits per pixel
#define TAG_SET_PIXEL_ORDER 0x00048006  // Set pixel order (RGB vs BGR)
#define TAG_SET_VIRT_OFFSET 0x00048009  // Set virtual offset (for hardware scroll)
#define TAG_ALLOCATE_FB     0x00040001  // Allocate framebuffer
#define TAG_GET_PITCH       0x00040008  // Get bytes per row

// Must be 16-byte aligned for mailbox
static volatile uint32_t __attribute__((aligned(16))) mailbox_buffer[36];

// Memory barrier
static inline void dmb(void) {
    asm volatile("dmb sy" ::: "memory");
}

static inline void dsb(void) {
    asm volatile("dsb sy" ::: "memory");
}

// Cache maintenance for GPU coherency
#define CACHE_LINE_SIZE 64

static void cache_clean(const void *start, uint32_t len) {
    uintptr_t addr = (uintptr_t)start & ~(CACHE_LINE_SIZE - 1);
    uintptr_t end = (uintptr_t)start + len;
    while (addr < end) {
        asm volatile("dc cvac, %0" : : "r"(addr) : "memory");
        addr += CACHE_LINE_SIZE;
    }
    dsb();
}

static void cache_invalidate(void *start, uint32_t len) {
    uintptr_t addr = (uintptr_t)start & ~(CACHE_LINE_SIZE - 1);
    uintptr_t end = (uintptr_t)start + len;
    while (addr < end) {
        // Use clean-and-invalidate - safer for dirty lines
        asm volatile("dc civac, %0" : : "r"(addr) : "memory");
        addr += CACHE_LINE_SIZE;
    }
    dsb();
}

// Debug output (works before printf is available)
static void debug_putc(char c) {
    // Use UART for early debug - Mini UART at 0x3F215040
    // Note: UART must be initialized first, or this does nothing
    volatile uint32_t *uart_io = (volatile uint32_t *)0x3F215040;
    volatile uint32_t *uart_lsr = (volatile uint32_t *)0x3F215054;

    // Wait for TX ready (bit 5 of LSR)
    while (!(*uart_lsr & 0x20));
    *uart_io = c;
}

static void debug_puts(const char *s) {
    while (*s) {
        if (*s == '\n') debug_putc('\r');
        debug_putc(*s++);
    }
}

static void debug_hex(uint32_t val) {
    const char *hex = "0123456789ABCDEF";
    debug_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        debug_putc(hex[(val >> i) & 0xF]);
    }
}

// Write to mailbox (with timeout to prevent hangs)
static int mailbox_write(uint32_t channel, uint32_t data) {
    // Wait until mailbox is not full (1 second timeout)
    uint32_t start = hal_get_time_us();
    while (MAILBOX_STATUS & MAILBOX_FULL) {
        dmb();
        if ((hal_get_time_us() - start) > 1000000) {  // 1 second
            debug_puts("[HAL/FB] mailbox_write: timeout waiting for mailbox\n");
            return -1;
        }
    }
    dmb();

    // Write address (upper 28 bits) | channel (lower 4 bits)
    MAILBOX_WRITE = (data & 0xFFFFFFF0) | (channel & 0xF);
    dmb();
    return 0;
}

// Read from mailbox (with timeout to prevent hangs)
static uint32_t mailbox_read(uint32_t channel) {
    uint32_t data;
    // 1 second total timeout
    uint32_t start = hal_get_time_us();

    while ((hal_get_time_us() - start) < 1000000) {  // 1 second
        // Wait until mailbox is not empty
        while (MAILBOX_STATUS & MAILBOX_EMPTY) {
            dmb();
            if ((hal_get_time_us() - start) > 1000000) {
                debug_puts("[HAL/FB] mailbox_read: timeout waiting for data\n");
                return 0xFFFFFFFF;
            }
        }
        dmb();

        data = MAILBOX_READ;
        dmb();

        // Check if it's for our channel
        if ((data & 0xF) == channel) {
            return data & 0xFFFFFFF0;
        }
    }
    debug_puts("[HAL/FB] mailbox_read: timeout, wrong channel\n");
    return 0xFFFFFFFF;
}

// Convert ARM physical address to bus address for GPU
// On Pi Zero 2W: bus address = phys + 0xC0000000 (for cached)
// Or: bus address = phys + 0x40000000 (for uncached - what we want for mailbox)
static uint32_t arm_to_bus(void *ptr) {
    return ((uint32_t)(uint64_t)ptr) | 0xC0000000;
}

// Convert bus address to ARM physical
static void *bus_to_arm(uint32_t bus) {
    return (void *)(uint64_t)(bus & 0x3FFFFFFF);
}

int hal_fb_init(uint32_t width, uint32_t height) {
    debug_puts("[HAL/FB] Pi framebuffer init\n");

    // Use 1080p on Pi - flat mode should make this fast enough
    (void)width;
    (void)height;
    width = 1920;
    height = 1080;

    // Build property message
    // Must be 16-byte aligned, and we pass the bus address
    uint32_t idx = 0;

    mailbox_buffer[idx++] = 0;              // Total size (filled in later)
    mailbox_buffer[idx++] = 0;              // Request code

    // Set physical display size
    mailbox_buffer[idx++] = TAG_SET_PHYS_WH;
    mailbox_buffer[idx++] = 8;              // Value buffer size
    mailbox_buffer[idx++] = 0;              // Request/response code
    mailbox_buffer[idx++] = width;          // Width
    mailbox_buffer[idx++] = height;         // Height

    // Set virtual display size (2x height for hardware scrolling)
    // This gives us a buffer we can scroll through without copying
    uint32_t virt_height = height * 2;
    mailbox_buffer[idx++] = TAG_SET_VIRT_WH;
    mailbox_buffer[idx++] = 8;
    mailbox_buffer[idx++] = 0;
    mailbox_buffer[idx++] = width;
    mailbox_buffer[idx++] = virt_height;

    // Set depth (bits per pixel)
    mailbox_buffer[idx++] = TAG_SET_DEPTH;
    mailbox_buffer[idx++] = 4;
    mailbox_buffer[idx++] = 0;
    mailbox_buffer[idx++] = 32;             // 32 bits per pixel

    // Set pixel order (0 = BGR, 1 = RGB)
    // Pi GPU often ignores RGB request and uses BGR internally,
    // so request BGR to match hardware behavior
    mailbox_buffer[idx++] = TAG_SET_PIXEL_ORDER;
    mailbox_buffer[idx++] = 4;
    mailbox_buffer[idx++] = 0;
    mailbox_buffer[idx++] = 0;              // BGR (matches Pi hardware)

    // Allocate framebuffer
    mailbox_buffer[idx++] = TAG_ALLOCATE_FB;
    mailbox_buffer[idx++] = 8;
    mailbox_buffer[idx++] = 0;
    mailbox_buffer[idx++] = 4096;           // Alignment (4K)
    mailbox_buffer[idx++] = 0;              // Will be filled with size

    // Get pitch
    mailbox_buffer[idx++] = TAG_GET_PITCH;
    mailbox_buffer[idx++] = 4;
    mailbox_buffer[idx++] = 0;
    mailbox_buffer[idx++] = 0;              // Will be filled with pitch

    // End tag
    mailbox_buffer[idx++] = TAG_END;

    // Set total size (in bytes)
    mailbox_buffer[0] = idx * 4;

    debug_puts("[HAL/FB] Sending mailbox request...\n");

    // Clean cache so GPU sees our writes
    cache_clean((void *)mailbox_buffer, sizeof(mailbox_buffer));
    dmb();

    // Send message to GPU
    uint32_t bus_addr = arm_to_bus((void *)mailbox_buffer);
    mailbox_write(MAILBOX_CH_PROP, bus_addr);

    // Wait for response
    mailbox_read(MAILBOX_CH_PROP);
    dmb();

    // Invalidate cache so we see GPU's response
    cache_invalidate((void *)mailbox_buffer, sizeof(mailbox_buffer));

    // Check response
    if (mailbox_buffer[1] != 0x80000000) {
        debug_puts("[HAL/FB] ERROR: Mailbox request failed! Code: ");
        debug_hex(mailbox_buffer[1]);
        debug_puts("\n");
        return -1;
    }

    debug_puts("[HAL/FB] Mailbox success!\n");

    // Parse response - find the allocate framebuffer response
    idx = 2;
    uint32_t fb_addr = 0;
    uint32_t fb_size = 0;
    uint32_t pitch = 0;

    while (mailbox_buffer[idx] != TAG_END) {
        uint32_t tag = mailbox_buffer[idx++];
        uint32_t size = mailbox_buffer[idx++];
        idx++;  // Skip request/response code

        if (tag == TAG_ALLOCATE_FB) {
            fb_addr = mailbox_buffer[idx];
            fb_size = mailbox_buffer[idx + 1];
        } else if (tag == TAG_GET_PITCH) {
            pitch = mailbox_buffer[idx];
        }

        idx += (size + 3) / 4;  // Move to next tag (size is in bytes, round up)
    }

    if (fb_addr == 0) {
        debug_puts("[HAL/FB] ERROR: No framebuffer allocated!\n");
        return -1;
    }

    // Convert bus address to ARM address
    fb_info.base = (uint32_t *)bus_to_arm(fb_addr);
    fb_info.width = width;
    fb_info.height = height;
    fb_info.pitch = pitch;
    virtual_height = virt_height;
    current_scroll_y = 0;

    debug_puts("[HAL/FB] Framebuffer at: ");
    debug_hex((uint32_t)(uint64_t)fb_info.base);
    debug_puts("\n");
    debug_puts("[HAL/FB] Size: ");
    debug_hex(fb_size);
    debug_puts("\n");
    debug_puts("[HAL/FB] Pitch: ");
    debug_hex(pitch);
    debug_puts("\n");

    // Clear entire virtual framebuffer (2x height) to black
    for (uint32_t i = 0; i < width * virt_height; i++) {
        fb_info.base[i] = 0x00000000;  // Black
    }

    debug_puts("[HAL/FB] Pi framebuffer ready!\n");
    return 0;
}

hal_fb_info_t *hal_fb_get_info(void) {
    return &fb_info;
}

// Hardware scroll - set the Y offset displayed on screen
// Returns 0 on success, -1 on failure
int hal_fb_set_scroll_offset(uint32_t y) {
    if (y >= virtual_height) return -1;
    if (y == current_scroll_y) return 0;  // No change needed

    // Build mailbox message to set virtual offset
    uint32_t idx = 0;
    mailbox_buffer[idx++] = 0;              // Size (filled in later)
    mailbox_buffer[idx++] = 0;              // Request code

    mailbox_buffer[idx++] = TAG_SET_VIRT_OFFSET;
    mailbox_buffer[idx++] = 8;              // Value buffer size
    mailbox_buffer[idx++] = 0;              // Request/response
    mailbox_buffer[idx++] = 0;              // X offset (always 0)
    mailbox_buffer[idx++] = y;              // Y offset

    mailbox_buffer[idx++] = TAG_END;
    mailbox_buffer[0] = idx * 4;

    // Clean cache so GPU sees our writes
    cache_clean((void *)mailbox_buffer, sizeof(mailbox_buffer));
    dmb();

    uint32_t bus_addr = arm_to_bus((void *)mailbox_buffer);
    mailbox_write(MAILBOX_CH_PROP, bus_addr);
    mailbox_read(MAILBOX_CH_PROP);
    dmb();

    // Invalidate cache so we see GPU's response
    cache_invalidate((void *)mailbox_buffer, sizeof(mailbox_buffer));

    if (mailbox_buffer[1] == 0x80000000) {
        current_scroll_y = y;
        return 0;
    }
    return -1;
}

uint32_t hal_fb_get_virtual_height(void) {
    return virtual_height;
}
