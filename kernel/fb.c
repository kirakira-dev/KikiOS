/*
 * KikiOS Framebuffer Driver
 *
 * Generic framebuffer operations.
 * Platform-specific initialization is in hal/<platform>/fb.c
 */

#include "fb.h"
#include "printf.h"
#include "string.h"
#include "hal/hal.h"

// Framebuffer state - these are exported for backward compatibility
uint32_t fb_width = 0;
uint32_t fb_height = 0;        // Visible display height
uint32_t fb_pitch = 0;
uint32_t *fb_base = NULL;
static uint32_t fb_buffer_height = 0;  // Actual buffer height (may be > fb_height for hw scroll)

// Hardware double buffering state
static int current_buffer = 0;  // 0 = top half visible, 1 = bottom half visible

int fb_init(void) {
    // Note: Don't use printf here - console isn't initialized yet!

    // Call platform-specific init with 1280x800 resolution
    // Pi overrides to 800x600 anyway for performance
    if (hal_fb_init(1280, 800) < 0) {
        return -1;
    }

    // Get info from HAL
    hal_fb_info_t *info = hal_fb_get_info();
    if (!info || !info->base) {
        return -1;
    }

    // Copy to our global vars for backward compatibility
    fb_base = info->base;
    fb_width = info->width;
    fb_height = info->height;
    fb_pitch = info->pitch;

    // Get actual buffer height (for hardware scroll support)
    fb_buffer_height = hal_fb_get_virtual_height();
    if (fb_buffer_height < fb_height) {
        fb_buffer_height = fb_height;  // Fallback
    }

    // Clear entire buffer to black (including virtual scroll area)
    // Use DMA if available for speed
    if (hal_dma_available()) {
        hal_dma_fill(fb_base, COLOR_BLACK, fb_width * fb_buffer_height * sizeof(uint32_t));
    } else {
        memset32(fb_base, COLOR_BLACK, fb_width * fb_buffer_height);
    }

    return 0;
}

void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= fb_width || y >= fb_buffer_height) return;
    fb_base[y * fb_width + x] = color;
}

void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    // Clip to buffer bounds (not just visible height - needed for hw scroll)
    if (x >= fb_width || y >= fb_buffer_height) return;
    if (x + w > fb_width) w = fb_width - x;
    if (y + h > fb_buffer_height) h = fb_buffer_height - y;

    // Fill row by row using memset32
    for (uint32_t row = y; row < y + h; row++) {
        memset32(&fb_base[row * fb_width + x], color, w);
    }
}

void fb_clear(uint32_t color) {
    // Clear entire buffer including virtual scroll area
    memset32(fb_base, color, fb_width * fb_buffer_height);
}

// Include font data
#include "font.h"

void fb_draw_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg) {
    // Quick bounds check for entire character (use buffer height for hw scroll)
    if (x + FONT_WIDTH > fb_width || y + FONT_HEIGHT > fb_buffer_height) return;

    const uint8_t *glyph = font_data[(uint8_t)c];
    uint32_t *row_ptr = &fb_base[y * fb_width + x];

    for (int row = 0; row < FONT_HEIGHT; row++) {
        uint8_t bits = glyph[row];
        // Unroll the 8-pixel row for speed
        row_ptr[0] = (bits & 0x80) ? fg : bg;
        row_ptr[1] = (bits & 0x40) ? fg : bg;
        row_ptr[2] = (bits & 0x20) ? fg : bg;
        row_ptr[3] = (bits & 0x10) ? fg : bg;
        row_ptr[4] = (bits & 0x08) ? fg : bg;
        row_ptr[5] = (bits & 0x04) ? fg : bg;
        row_ptr[6] = (bits & 0x02) ? fg : bg;
        row_ptr[7] = (bits & 0x01) ? fg : bg;
        row_ptr += fb_width;
    }
}

void fb_draw_string(uint32_t x, uint32_t y, const char *s, uint32_t fg, uint32_t bg) {
    uint32_t orig_x = x;
    while (*s) {
        if (*s == '\n') {
            x = orig_x;
            y += FONT_HEIGHT;
        } else {
            fb_draw_char(x, y, *s, fg, bg);
            x += FONT_WIDTH;
        }
        s++;
    }
}

// Hardware double buffering functions

int fb_has_hw_double_buffer(void) {
    // Hardware double buffering is available if virtual height is at least 2x physical
    return (fb_buffer_height >= fb_height * 2);
}

// Clean CPU cache for a memory range (ARM64)
// Required before GPU reads data written by CPU through cache
#ifdef __aarch64__
static void fb_cache_clean(void *start, uint32_t len) {
    uintptr_t addr = (uintptr_t)start & ~63UL;  // Align to 64-byte cache line
    uintptr_t end = (uintptr_t)start + len;
    while (addr < end) {
        asm volatile("dc cvac, %0" : : "r"(addr) : "memory");
        addr += 64;
    }
    asm volatile("dsb sy" ::: "memory");
}
#else
static void fb_cache_clean(void *start, uint32_t len) {
    (void)start; (void)len;  // No-op on other architectures
}
#endif

int fb_flip(int buffer) {
    if (!fb_has_hw_double_buffer()) return -1;
    if (buffer < 0 || buffer > 1) return -1;

    // Clean cache for the buffer we're about to display
    // The buffer being flipped TO is the one we've been drawing on (backbuffer)
    uint32_t *flip_buffer = fb_base + (buffer ? fb_width * fb_height : 0);
    fb_cache_clean(flip_buffer, fb_width * fb_height * sizeof(uint32_t));

    // Set the scroll offset to show the requested buffer
    uint32_t y_offset = buffer ? fb_height : 0;
    if (hal_fb_set_scroll_offset(y_offset) == 0) {
        current_buffer = buffer;
        return 0;
    }
    return -1;
}

uint32_t *fb_get_backbuffer(void) {
    if (!fb_has_hw_double_buffer()) {
        return fb_base;  // No hardware double buffering, use same buffer
    }
    // Return pointer to the non-visible buffer
    // If buffer 0 is visible (top), return bottom; if buffer 1 is visible (bottom), return top
    return fb_base + (current_buffer ? 0 : fb_width * fb_height);
}
