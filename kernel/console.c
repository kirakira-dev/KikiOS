/*
 * KikiOS Text Console
 *
 * Provides terminal-like text output on the framebuffer.
 * Handles cursor positioning, scrolling, and basic escape sequences.
 *
 * Hardware scroll support:
 * On Pi, uses GPU virtual offset for fast scrolling (no memmove).
 * Falls back to software scroll on QEMU or if hardware scroll unavailable.
 */

#include "console.h"
#include "fb.h"
#include "font.h"
#include "string.h"
#include "printf.h"
#include "hal/hal.h"

// Console state
static int console_initialized = 0;
static int cursor_row = 0;
static int cursor_col = 0;
static int num_rows = 0;
static int num_cols = 0;
static uint32_t fg_color = COLOR_WHITE;
static uint32_t bg_color = COLOR_BLACK;

// Cursor blink state
static int cursor_visible = 0;
static int cursor_enabled = 1;

// Hardware scroll state
static uint32_t scroll_offset = 0;       // Current Y pixel offset in virtual framebuffer
static uint32_t virtual_height = 0;      // Total virtual framebuffer height (pixels)
static int hw_scroll_available = 0;      // Whether hardware scroll is supported

// Text buffer for scrolling (unused with hardware scroll)
static char *text_buffer = NULL;
static uint32_t *fg_buffer = NULL;
static uint32_t *bg_buffer = NULL;

// Line buffer for batched rendering
// Framebuffer is non-cacheable on Pi, so we draw to cached RAM first
#define LINE_BUF_WIDTH 800
static uint32_t line_buffer[LINE_BUF_WIDTH * FONT_HEIGHT] __attribute__((aligned(64)));
static int line_buf_row = -1;      // Which row is buffered (-1 = none)
static int line_buf_min_col = -1;  // Leftmost column drawn
static int line_buf_max_col = -1;  // Rightmost column drawn

void console_init(void) {
    if (fb_base == NULL) return;

    // Calculate dimensions
    num_cols = fb_width / FONT_WIDTH;
    num_rows = fb_height / FONT_HEIGHT;

    // Check for hardware scroll support (Pi has virtual FB 2x height)
    virtual_height = hal_fb_get_virtual_height();
    if (virtual_height > fb_height) {
        // Test if hardware scroll actually works
        if (hal_fb_set_scroll_offset(0) == 0) {
            hw_scroll_available = 1;
        }
    }
    scroll_offset = 0;

    cursor_row = 0;
    cursor_col = 0;

    // Don't clear screen - keep boot messages visible

    console_initialized = 1;
}

// Flush only the drawn portion of line buffer to framebuffer
static void line_buf_flush(void) {
    if (line_buf_row < 0 || line_buf_min_col < 0) return;

    // Calculate pixel region to copy
    uint32_t x_start = line_buf_min_col * FONT_WIDTH;
    uint32_t x_end = (line_buf_max_col + 1) * FONT_WIDTH;
    uint32_t width_bytes = (x_end - x_start) * sizeof(uint32_t);
    uint32_t y_fb = scroll_offset + line_buf_row * FONT_HEIGHT;

    uint32_t *src = &line_buffer[x_start];
    uint32_t *dst = &fb_base[y_fb * fb_width + x_start];

    // Use 2D DMA if available - single operation instead of 16 memcpys
    if (hal_dma_available()) {
        hal_dma_copy_2d(dst, fb_width * sizeof(uint32_t),
                        src, LINE_BUF_WIDTH * sizeof(uint32_t),
                        width_bytes, FONT_HEIGHT);
    } else {
        // Fallback: 16 separate copies
        for (int row = 0; row < FONT_HEIGHT; row++) {
            memcpy(dst, src, width_bytes);
            src += LINE_BUF_WIDTH;
            dst += fb_width;
        }
    }

    line_buf_min_col = -1;
    line_buf_max_col = -1;
}

// Draw character to line buffer (cached RAM)
static void draw_char_at(int row, int col, char c) {
    // If switching rows, flush old row first
    if (row != line_buf_row && line_buf_row >= 0) {
        line_buf_flush();
    }
    line_buf_row = row;

    // Draw character into line buffer
    uint32_t x = col * FONT_WIDTH;
    if (x + FONT_WIDTH > LINE_BUF_WIDTH) return;

    const uint8_t *glyph = font_data[(uint8_t)c];

    for (int r = 0; r < FONT_HEIGHT; r++) {
        uint32_t *row_ptr = &line_buffer[r * LINE_BUF_WIDTH + x];
        uint8_t bits = glyph[r];
        row_ptr[0] = (bits & 0x80) ? fg_color : bg_color;
        row_ptr[1] = (bits & 0x40) ? fg_color : bg_color;
        row_ptr[2] = (bits & 0x20) ? fg_color : bg_color;
        row_ptr[3] = (bits & 0x10) ? fg_color : bg_color;
        row_ptr[4] = (bits & 0x08) ? fg_color : bg_color;
        row_ptr[5] = (bits & 0x04) ? fg_color : bg_color;
        row_ptr[6] = (bits & 0x02) ? fg_color : bg_color;
        row_ptr[7] = (bits & 0x01) ? fg_color : bg_color;
    }

    // Track drawn region
    if (line_buf_min_col < 0 || col < line_buf_min_col) line_buf_min_col = col;
    if (col > line_buf_max_col) line_buf_max_col = col;
}

static void scroll_up(void) {
    // Flush line buffer before scrolling
    line_buf_flush();
    line_buf_row = -1;

    uint32_t line_pixels = fb_width * FONT_HEIGHT;

    if (!hw_scroll_available) {
        // Software scroll fallback (QEMU)
        uint32_t total_pixels = fb_width * fb_height;
        memmove(fb_base, fb_base + line_pixels, (total_pixels - line_pixels) * sizeof(uint32_t));
        memset32(fb_base + (total_pixels - line_pixels), bg_color, line_pixels);
        return;
    }

    // Hardware scroll (Pi) - circular buffer approach
    // With 2x virtual height, we can scroll ~37 lines before needing to wrap
    uint32_t max_offset = virtual_height - fb_height;

    // Check if we need to wrap around
    if (scroll_offset + FONT_HEIGHT > max_offset) {
        // Copy visible portion back to top of buffer, then reset offset
        memmove(fb_base, fb_base + scroll_offset * fb_width, fb_height * fb_width * sizeof(uint32_t));
        scroll_offset = 0;
    }

    // Scroll by one line
    scroll_offset += FONT_HEIGHT;

    // Clear the new bottom line
    uint32_t new_bottom_y = scroll_offset + fb_height - FONT_HEIGHT;
    memset32(fb_base + new_bottom_y * fb_width, bg_color, line_pixels);

    // Update GPU display offset
    hal_fb_set_scroll_offset(scroll_offset);
}

static void newline(void) {
    cursor_col = 0;
    cursor_row++;

    if (cursor_row >= num_rows) {
        scroll_up();
        cursor_row = num_rows - 1;
    }
}

// Forward declaration
static void draw_cursor(int show);

void console_putc(char c) {
    // If console not initialized, fall back to UART
    if (!console_initialized) {
        extern void uart_putc(char c);
        if (c == '\n') uart_putc('\r');
        uart_putc(c);
        return;
    }

    // Hide cursor before any operation that might move it
    if (cursor_visible) {
        draw_cursor(0);
    }

    switch (c) {
        case '\n':
            newline();
            break;

        case '\r':
            cursor_col = 0;
            break;

        case '\t':
            // Tab to next 8-column boundary
            cursor_col = (cursor_col + 8) & ~7;
            if (cursor_col >= num_cols) {
                newline();
            }
            break;

        case '\b':
            // Backspace - only move cursor, don't erase
            if (cursor_col > 0) {
                cursor_col--;
            }
            break;

        default:
            if (c >= 32 && c < 127) {
                draw_char_at(cursor_row, cursor_col, c);
                cursor_col++;

                if (cursor_col >= num_cols) {
                    newline();
                }
            }
            break;
    }

    // Show cursor at new position (static cursor, always visible)
    if (cursor_enabled && !cursor_visible) {
        draw_cursor(1);
    } else {
        // Flush line buffer even when cursor is disabled (for games like snake)
        line_buf_flush();
    }
}

void console_puts(const char *s) {
    // If no framebuffer, fall back to UART
    if (fb_base == NULL) {
        printf("%s", s);
        return;
    }
    while (*s) {
        console_putc(*s++);
    }
    // Flush for immediate display
    line_buf_flush();
}

void console_clear(void) {
    // Discard any pending line buffer
    line_buf_row = -1;
    line_buf_min_col = -1;
    line_buf_max_col = -1;

    // Reset scroll offset when clearing
    if (hw_scroll_available) {
        scroll_offset = 0;
        hal_fb_set_scroll_offset(0);
    }
    fb_clear(bg_color);
    cursor_row = 0;
    cursor_col = 0;
}

// Fast clear from cursor position to end of line using fb_fill_rect
void console_clear_to_eol(void) {
    if (!console_initialized || fb_base == NULL) return;

    // Flush line buffer if on this row
    if (line_buf_row == cursor_row) {
        line_buf_flush();
        line_buf_row = -1;
    }

    // Hide cursor before clearing
    if (cursor_visible) {
        draw_cursor(0);
    }

    // Calculate pixel coordinates (account for hardware scroll offset)
    uint32_t x = cursor_col * FONT_WIDTH;
    uint32_t y = scroll_offset + cursor_row * FONT_HEIGHT;
    uint32_t w = fb_width - x;  // Width to end of screen
    uint32_t h = FONT_HEIGHT;

    // Use fast fb_fill_rect instead of putc(' ') loop
    fb_fill_rect(x, y, w, h, bg_color);

    // Show cursor at current position
    if (cursor_enabled && !cursor_visible) {
        draw_cursor(1);
    }
}

// Fast rectangular clear using fb_fill_rect
void console_clear_region(int row, int col, int width, int height) {
    if (!console_initialized || fb_base == NULL) return;

    // Clip to console bounds
    if (row < 0) row = 0;
    if (col < 0) col = 0;
    if (row + height > num_rows) height = num_rows - row;
    if (col + width > num_cols) width = num_cols - col;
    if (width <= 0 || height <= 0) return;

    // Flush line buffer if it overlaps
    if (line_buf_row >= row && line_buf_row < row + height) {
        line_buf_flush();
        line_buf_row = -1;
    }

    // Hide cursor if it's in the region
    if (cursor_visible) {
        draw_cursor(0);
    }

    // Calculate pixel coordinates (account for hardware scroll offset)
    uint32_t px = col * FONT_WIDTH;
    uint32_t py = scroll_offset + row * FONT_HEIGHT;
    uint32_t pw = width * FONT_WIDTH;
    uint32_t ph = height * FONT_HEIGHT;

    // Use fast fb_fill_rect
    fb_fill_rect(px, py, pw, ph, bg_color);

    // Restore cursor
    if (cursor_enabled && !cursor_visible) {
        draw_cursor(1);
    }
}

void console_set_cursor(int row, int col) {
    // Hide cursor before moving
    if (cursor_visible) {
        draw_cursor(0);
    }
    if (row >= 0 && row < num_rows) cursor_row = row;
    if (col >= 0 && col < num_cols) cursor_col = col;
    // Show cursor at new position
    if (cursor_enabled && !cursor_visible) {
        draw_cursor(1);
    }
}

void console_get_cursor(int *row, int *col) {
    if (row) *row = cursor_row;
    if (col) *col = cursor_col;
}

void console_set_color(uint32_t fg, uint32_t bg) {
    fg_color = fg;
    bg_color = bg;
}

int console_rows(void) {
    return num_rows;
}

int console_cols(void) {
    return num_cols;
}

// Draw/undraw cursor at current position by inverting pixels
static void draw_cursor(int show) {
    if (!console_initialized || fb_base == NULL) return;
    if (show == cursor_visible) return;  // Already in desired state

    // Flush line buffer if cursor is on buffered row
    if (line_buf_row == cursor_row) {
        line_buf_flush();
    }

    uint32_t x = cursor_col * FONT_WIDTH;
    // Account for hardware scroll offset
    uint32_t y = scroll_offset + cursor_row * FONT_HEIGHT;

    // Get the actual buffer height limit
    uint32_t buf_height = hw_scroll_available ? virtual_height : fb_height;

    // Toggle pixels (XOR-style invert)
    for (int dy = 0; dy < FONT_HEIGHT; dy++) {
        for (int dx = 0; dx < FONT_WIDTH; dx++) {
            uint32_t px = x + dx;
            uint32_t py = y + dy;
            if (px < fb_width && py < buf_height) {
                uint32_t *pixel = fb_base + py * fb_width + px;
                // Invert: swap fg and bg
                *pixel = (*pixel == bg_color) ? fg_color : bg_color;
            }
        }
    }
    cursor_visible = show;
}

// Toggle cursor visibility (called by timer)
void console_blink_cursor(void) {
    if (!cursor_enabled) return;
    draw_cursor(!cursor_visible);
}

// Enable/disable cursor
void console_set_cursor_enabled(int enabled) {
    if (!enabled && cursor_visible) {
        draw_cursor(0);  // Hide cursor
    }
    cursor_enabled = enabled;
}

// Force redraw cursor (call after moving cursor)
void console_show_cursor(void) {
    if (cursor_enabled && !cursor_visible) {
        draw_cursor(1);
    }
}
