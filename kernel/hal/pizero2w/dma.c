/*
 * Raspberry Pi Zero 2W DMA Controller Driver
 *
 * Uses the BCM2837 DMA engine for fast memory-to-memory transfers.
 * Supports both 1D and 2D transfers (2D is great for framebuffer blitting).
 *
 * Reference: BCM2835 ARM Peripherals manual (section 4)
 * https://cs140e.sergio.bz/docs/BCM2837-ARM-Peripherals.pdf
 */

#include "../hal.h"
#include <stdint.h>

// Forward declaration for debug output
extern int printf(const char *fmt, ...);

// DMA peripheral base address (Pi Zero 2W / BCM2837)
#define DMA_BASE            0x3F007000

// DMA channel registers (offset from channel base)
#define DMA_CS              0x00    // Control and Status
#define DMA_CONBLK_AD       0x04    // Control Block Address
#define DMA_TI              0x08    // Transfer Information (read-only from CB)
#define DMA_SOURCE_AD       0x0C    // Source Address
#define DMA_DEST_AD         0x10    // Destination Address
#define DMA_TXFR_LEN        0x14    // Transfer Length
#define DMA_STRIDE          0x18    // 2D Stride
#define DMA_NEXTCONBK       0x1C    // Next Control Block
#define DMA_DEBUG           0x20    // Debug

// DMA enable register (global)
#define DMA_ENABLE          0xFF0

// CS register bits
#define DMA_CS_ACTIVE       (1 << 0)    // Activate DMA
#define DMA_CS_END          (1 << 1)    // Transfer complete
#define DMA_CS_INT          (1 << 2)    // Interrupt status
#define DMA_CS_DREQ         (1 << 3)    // DREQ state
#define DMA_CS_PAUSED       (1 << 4)    // DMA is paused
#define DMA_CS_DREQ_STOPS   (1 << 5)    // Paused by DREQ
#define DMA_CS_WAITING      (1 << 6)    // Waiting for writes
#define DMA_CS_ERROR        (1 << 8)    // Error occurred
#define DMA_CS_PRIORITY(x)  (((x) & 0xF) << 16)  // AXI priority
#define DMA_CS_PANIC_PRI(x) (((x) & 0xF) << 20)  // Panic priority
#define DMA_CS_WAIT_WRITES  (1 << 28)   // Wait for outstanding writes
#define DMA_CS_DISDEBUG     (1 << 29)   // Disable debug pause
#define DMA_CS_ABORT        (1 << 30)   // Abort current CB
#define DMA_CS_RESET        (1 << 31)   // Reset DMA channel

// Transfer Information (TI) bits
#define DMA_TI_INTEN        (1 << 0)    // Interrupt enable
#define DMA_TI_TDMODE       (1 << 1)    // 2D mode
#define DMA_TI_WAIT_RESP    (1 << 3)    // Wait for write response
#define DMA_TI_DEST_INC     (1 << 4)    // Destination address increment
#define DMA_TI_DEST_WIDTH   (1 << 5)    // Dest width (1=128-bit)
#define DMA_TI_DEST_DREQ    (1 << 6)    // Dest DREQ
#define DMA_TI_DEST_IGNORE  (1 << 7)    // Don't write to dest
#define DMA_TI_SRC_INC      (1 << 8)    // Source address increment
#define DMA_TI_SRC_WIDTH    (1 << 9)    // Source width (1=128-bit)
#define DMA_TI_SRC_DREQ     (1 << 10)   // Source DREQ
#define DMA_TI_SRC_IGNORE   (1 << 11)   // Don't read source
#define DMA_TI_BURST(x)     (((x) & 0xF) << 12)  // Burst length
#define DMA_TI_PERMAP(x)    (((x) & 0x1F) << 16) // Peripheral map
#define DMA_TI_WAITS(x)     (((x) & 0x1F) << 21) // Add wait cycles
#define DMA_TI_NO_WIDE      (1 << 26)   // No wide bursts

// DMA Control Block (must be 32-byte aligned)
typedef struct __attribute__((aligned(32))) {
    uint32_t ti;            // Transfer information
    uint32_t source_ad;     // Source address (bus address)
    uint32_t dest_ad;       // Destination address (bus address)
    uint32_t txfr_len;      // Transfer length (or YLENGTH << 16 | XLENGTH in 2D)
    uint32_t stride;        // 2D stride (D_STRIDE << 16 | S_STRIDE)
    uint32_t nextconbk;     // Next control block (bus address, 0 for none)
    uint32_t reserved[2];   // Padding to 32 bytes
} dma_cb_t;

// We use DMA channel 0 for framebuffer operations
// Channels 0-6 support full 2D mode, 7-14 are "lite" channels
#define FB_DMA_CHANNEL      0

// Static control block (must persist during transfer)
static dma_cb_t __attribute__((aligned(32))) dma_cb;

// Static fill value buffer (must persist during transfer, cache-line aligned)
static uint32_t __attribute__((aligned(64))) fill_value_buf[16];

// Flag to track if DMA is initialized
static int dma_initialized = 0;

// Memory barrier
static inline void dmb(void) {
    asm volatile("dmb sy" ::: "memory");
}

static inline void dsb(void) {
    asm volatile("dsb sy" ::: "memory");
}

// Clean data cache for a memory range (flush to RAM so DMA can see it)
// Cortex-A53 has 64-byte cache lines
#define CACHE_LINE_SIZE 64

static void cache_clean_range(const void *start, uint32_t len) {
    uintptr_t addr = (uintptr_t)start & ~(CACHE_LINE_SIZE - 1);  // Align down
    uintptr_t end = (uintptr_t)start + len;

    while (addr < end) {
        // DC CVAC: Data Cache Clean by Virtual Address to Point of Coherency
        asm volatile("dc cvac, %0" : : "r"(addr) : "memory");
        addr += CACHE_LINE_SIZE;
    }
    dsb();  // Ensure all cache operations complete before continuing
}

// Invalidate data cache for a memory range (so CPU sees DMA-written data)
static void cache_invalidate_range(void *start, uint32_t len) {
    uintptr_t addr = (uintptr_t)start & ~(CACHE_LINE_SIZE - 1);  // Align down
    uintptr_t end = (uintptr_t)start + len;

    while (addr < end) {
        // DC IVAC: Data Cache Invalidate by Virtual Address to Point of Coherency
        asm volatile("dc ivac, %0" : : "r"(addr) : "memory");
        addr += CACHE_LINE_SIZE;
    }
    dsb();  // Ensure all cache operations complete before continuing
}

// Convert ARM physical address to bus address for DMA
// On BCM2837, use 0xC0000000 alias (uncached, coherent)
static inline uint32_t phys_to_bus(void *ptr) {
    return ((uint32_t)(uint64_t)ptr) | 0xC0000000;
}

// Read DMA channel register
static inline uint32_t dma_read(int channel, int reg) {
    volatile uint32_t *addr = (volatile uint32_t *)(uintptr_t)(DMA_BASE + channel * 0x100 + reg);
    return *addr;
}

// Write DMA channel register
static inline void dma_write(int channel, int reg, uint32_t val) {
    volatile uint32_t *addr = (volatile uint32_t *)(uintptr_t)(DMA_BASE + channel * 0x100 + reg);
    *addr = val;
}

// Read global DMA register
static inline uint32_t dma_read_global(int reg) {
    volatile uint32_t *addr = (volatile uint32_t *)(uintptr_t)(DMA_BASE + reg);
    return *addr;
}

// Write global DMA register
static inline void dma_write_global(int reg, uint32_t val) {
    volatile uint32_t *addr = (volatile uint32_t *)(uintptr_t)(DMA_BASE + reg);
    *addr = val;
}

// Initialize DMA controller
int hal_dma_init(void) {
    // Enable DMA channel 0
    uint32_t enable = dma_read_global(DMA_ENABLE);
    dma_write_global(DMA_ENABLE, enable | (1 << FB_DMA_CHANNEL));
    dmb();

    // Reset the channel
    dma_write(FB_DMA_CHANNEL, DMA_CS, DMA_CS_RESET);
    dmb();

    // Wait for reset to complete (bit auto-clears) with timeout
    uint32_t start = hal_get_time_us();
    while (dma_read(FB_DMA_CHANNEL, DMA_CS) & DMA_CS_RESET) {
        dmb();
        if ((hal_get_time_us() - start) > 1000000) {  // 1 second
            printf("[DMA] hal_dma_init: DMA reset timeout\n");
            return -1;
        }
    }

    // Clear any pending interrupts/errors
    dma_write(FB_DMA_CHANNEL, DMA_CS, DMA_CS_END | DMA_CS_INT);
    dmb();

    dma_initialized = 1;
    return 0;
}

// Wait for DMA transfer to complete (with timeout to prevent hangs)
static int dma_wait(int channel) {
    uint32_t start = hal_get_time_us();
    dmb();
    while (dma_read(channel, DMA_CS) & DMA_CS_ACTIVE) {
        dmb();
        if ((hal_get_time_us() - start) > 5000000) {  // 5 second timeout for large transfers
            printf("[DMA] dma_wait: DMA completion timeout\n");
            dma_write(channel, DMA_CS, DMA_CS_END);
            dmb();
            return -1;
        }
    }
    // Clear END flag
    dma_write(channel, DMA_CS, DMA_CS_END);
    dmb();
    return 0;
}

// Simple 1D memory copy via DMA
// Both src and dst must be physical ARM addresses
int hal_dma_copy(void *dst, const void *src, uint32_t len) {
    if (!dma_initialized) return -1;
    if (len == 0) return 0;

    // Clean CPU cache so DMA sees current data in RAM
    cache_clean_range(src, len);

    // Wait for any previous transfer
    dma_wait(FB_DMA_CHANNEL);

    // Set up control block for linear copy
    dma_cb.ti = DMA_TI_SRC_INC | DMA_TI_DEST_INC | DMA_TI_WAIT_RESP;
    dma_cb.source_ad = phys_to_bus((void *)src);
    dma_cb.dest_ad = phys_to_bus(dst);
    dma_cb.txfr_len = len;
    dma_cb.stride = 0;
    dma_cb.nextconbk = 0;

    // Clean cache for control block so DMA hardware sees our writes
    cache_clean_range(&dma_cb, sizeof(dma_cb));

    // Point DMA to control block
    dma_write(FB_DMA_CHANNEL, DMA_CONBLK_AD, phys_to_bus(&dma_cb));
    dmb();

    // Start transfer
    dma_write(FB_DMA_CHANNEL, DMA_CS, DMA_CS_ACTIVE | DMA_CS_PRIORITY(8) | DMA_CS_PANIC_PRI(15));

    // Wait for completion
    dma_wait(FB_DMA_CHANNEL);

    return 0;
}

// 2D memory copy via DMA (perfect for framebuffer blitting)
// Copies a rectangular region from src to dst
// src_pitch/dst_pitch: bytes per row in source/destination buffers
// width: bytes per row to copy
// height: number of rows
int hal_dma_copy_2d(void *dst, uint32_t dst_pitch,
                    const void *src, uint32_t src_pitch,
                    uint32_t width, uint32_t height) {
    if (!dma_initialized) return -1;
    if (width == 0 || height == 0) return 0;

    // Clean CPU cache for entire source region so DMA sees current data
    cache_clean_range(src, src_pitch * height);

    // Wait for any previous transfer
    dma_wait(FB_DMA_CHANNEL);

    // Set up control block for 2D copy
    dma_cb.ti = DMA_TI_SRC_INC | DMA_TI_DEST_INC | DMA_TI_WAIT_RESP | DMA_TI_TDMODE;
    dma_cb.source_ad = phys_to_bus((void *)src);
    dma_cb.dest_ad = phys_to_bus(dst);

    // In 2D mode: TXFR_LEN = (YLENGTH << 16) | XLENGTH
    // XLENGTH = bytes per row to transfer
    // YLENGTH = number of rows - 1 (0 means 1 row)
    dma_cb.txfr_len = ((height - 1) << 16) | width;

    // STRIDE = (D_STRIDE << 16) | S_STRIDE (signed 16-bit values)
    // Stride = pitch - width (how many bytes to skip to get to next row)
    int16_t src_stride = (int16_t)(src_pitch - width);
    int16_t dst_stride = (int16_t)(dst_pitch - width);
    dma_cb.stride = ((uint32_t)(uint16_t)dst_stride << 16) | (uint16_t)src_stride;

    dma_cb.nextconbk = 0;

    // Clean cache for control block so DMA hardware sees our writes
    cache_clean_range(&dma_cb, sizeof(dma_cb));

    // Point DMA to control block
    dma_write(FB_DMA_CHANNEL, DMA_CONBLK_AD, phys_to_bus(&dma_cb));
    dmb();

    // Start transfer
    dma_write(FB_DMA_CHANNEL, DMA_CS, DMA_CS_ACTIVE | DMA_CS_PRIORITY(8) | DMA_CS_PANIC_PRI(15));

    // Wait for completion
    dma_wait(FB_DMA_CHANNEL);

    return 0;
}

// Fast framebuffer copy - copies entire framebuffer
// Optimized for copying backbuffer to frontbuffer
int hal_dma_fb_copy(uint32_t *dst, const uint32_t *src, uint32_t width, uint32_t height) {
    // Use 1D copy for full framebuffer (simpler, same speed for contiguous data)
    return hal_dma_copy(dst, src, width * height * sizeof(uint32_t));
}

// Fill memory with a 32-bit value using DMA
// Much faster than CPU memset for large regions (framebuffer clears, etc.)
// len is in bytes and must be a multiple of 4
int hal_dma_fill(void *dst, uint32_t value, uint32_t len) {
    if (!dma_initialized) return -1;
    if (len == 0) return 0;

    // Fill the source buffer with the value
    // DMA will read from this without incrementing, writing to consecutive dest addresses
    for (int i = 0; i < 16; i++) {
        fill_value_buf[i] = value;
    }

    // Clean cache so DMA sees our fill value
    cache_clean_range(fill_value_buf, sizeof(fill_value_buf));

    // Wait for any previous transfer
    dma_wait(FB_DMA_CHANNEL);

    // Set up control block:
    // - SRC_INC=0: Don't increment source (read same value repeatedly)
    // - DEST_INC=1: Increment destination
    dma_cb.ti = DMA_TI_DEST_INC | DMA_TI_WAIT_RESP;  // Note: NO SRC_INC
    dma_cb.source_ad = phys_to_bus(fill_value_buf);
    dma_cb.dest_ad = phys_to_bus(dst);
    dma_cb.txfr_len = len;
    dma_cb.stride = 0;
    dma_cb.nextconbk = 0;

    // Clean cache for control block
    cache_clean_range(&dma_cb, sizeof(dma_cb));

    // Point DMA to control block
    dma_write(FB_DMA_CHANNEL, DMA_CONBLK_AD, phys_to_bus(&dma_cb));
    dmb();

    // Start transfer
    dma_write(FB_DMA_CHANNEL, DMA_CS, DMA_CS_ACTIVE | DMA_CS_PRIORITY(8) | DMA_CS_PANIC_PRI(15));

    // Wait for completion
    dma_wait(FB_DMA_CHANNEL);

    // Invalidate CPU cache for destination so CPU sees DMA-written data
    cache_invalidate_range(dst, len);

    return 0;
}

// Check if DMA is available
int hal_dma_available(void) {
    return dma_initialized;
}
