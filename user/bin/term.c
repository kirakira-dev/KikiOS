/*
 * term - KikiOS Terminal Emulator
 *
 * A windowed terminal that runs kikish inside a desktop window.
 * Features:
 *   - 500-line scrollback buffer
 *   - Draggable scrollbar (click and drag the thumb)
 *   - Mouse drag scrolling (click and drag text area to scroll)
 *   - Page Up/Page Down keyboard scrolling
 *   - Ctrl+C handling
 *   - Form feed (\f) for clear screen
 */

#include "../lib/kiki.h"
#include "../lib/gfx.h"

// Terminal dimensions (characters)
#define TERM_COLS 80
#define TERM_ROWS 24

// Scrollback buffer size (total lines including visible)
#define SCROLLBACK_LINES 500

// Character size
#define CHAR_WIDTH 8
#define CHAR_HEIGHT 16

// Scrollbar dimensions
#define SCROLLBAR_WIDTH 16
#define SCROLLBAR_MIN_THUMB 20  // Minimum thumb height in pixels

// Window dimensions
// Desktop window chrome: 28px title bar + 1px separator + 10px corner radius = 39px
#define WIN_CHROME_HEIGHT 39
#define WIN_WIDTH  (TERM_COLS * CHAR_WIDTH + SCROLLBAR_WIDTH)
#define WIN_HEIGHT (TERM_ROWS * CHAR_HEIGHT)

// Modern terminal colors
#define TERM_BG         0x00FFFFFF   // Clean white background
#define TERM_FG         0x00333333   // Soft black text
#define SCROLL_BG       0x00F5F5F5   // Scrollbar track
#define SCROLL_THUMB    0x00CCCCCC   // Scrollbar thumb
#define SCROLL_HOVER    0x00AAAAAA   // Scrollbar thumb hover
#define CURSOR_COLOR    0x00007AFF   // Blue cursor

// Global state
static kapi_t *api;
static int window_id = -1;
static uint32_t *win_buffer;
static int win_w, win_h;
static gfx_ctx_t gfx;

// Scrollback buffer - ring buffer of lines
static char scrollback[SCROLLBACK_LINES][TERM_COLS];
static int scroll_head = 0;      // Next line to write to
static int scroll_count = 0;     // Total lines in buffer
static int scroll_offset = 0;    // How many lines scrolled back (0 = at bottom)

// Cursor position (relative to current write position, not view)
static int cursor_row = 0;       // Row within visible area
static int cursor_col = 0;

// Cursor blink state
static int cursor_visible = 1;
static unsigned long last_blink_tick = 0;

// Input buffer (ring buffer for keyboard input) - uses int for special keys
#define INPUT_BUF_SIZE 256
static int input_buffer[INPUT_BUF_SIZE];
static int input_head = 0;
static int input_tail = 0;

// Flag to track if shell is still running
static int shell_running = 1;

// Dirty flag - screen needs redraw
static int screen_dirty = 0;

// Scrollbar state
static int scrollbar_dragging = 0;
static int scrollbar_drag_start_y = 0;
static int scrollbar_drag_start_offset = 0;

// Forward declarations
static void redraw_screen(void);

// ============ Scrollback Buffer Management ============

// Get the line index in scrollback buffer for a given display row
// row 0 is top of display, row TERM_ROWS-1 is bottom
static int get_line_index(int display_row) {
    // The visible area shows lines from (scroll_head - scroll_count + scroll_offset) to end
    // Actually, let's think about this differently:
    // - scroll_head points to where the NEXT line will be written
    // - The most recent line written is at scroll_head - 1
    // - The bottom of the display (when scroll_offset=0) shows the most recent lines

    // Bottom line of display = scroll_head - 1 - scroll_offset
    // Top line of display = bottom - TERM_ROWS + 1

    int bottom_line = scroll_head - 1 - scroll_offset;
    int target_line = bottom_line - (TERM_ROWS - 1 - display_row);

    // Wrap around
    while (target_line < 0) target_line += SCROLLBACK_LINES;
    target_line = target_line % SCROLLBACK_LINES;

    return target_line;
}

// Get pointer to a line in scrollback
static char *get_line(int display_row) {
    int idx = get_line_index(display_row);
    return scrollback[idx];
}

// Get the current write line (cursor row)
static char *get_write_line(void) {
    // Current write position is at scroll_head - (TERM_ROWS - cursor_row)
    int idx = scroll_head - (TERM_ROWS - cursor_row);
    while (idx < 0) idx += SCROLLBACK_LINES;
    idx = idx % SCROLLBACK_LINES;
    return scrollback[idx];
}

// Add a new line (scroll the terminal content up)
static void new_line(void) {
    // Clear the new line
    for (int i = 0; i < TERM_COLS; i++) {
        scrollback[scroll_head][i] = ' ';
    }

    scroll_head = (scroll_head + 1) % SCROLLBACK_LINES;
    if (scroll_count < SCROLLBACK_LINES) {
        scroll_count++;
    }

    // If we're NOT scrolled back (scroll_offset == 0), stay at bottom
    // If we ARE scrolled back, keep viewing the same content (offset increases by 1)
    if (scroll_offset > 0) {
        scroll_offset++;
        // Don't let offset exceed the max (can't scroll past the top of buffer)
        int max_offset = scroll_count - TERM_ROWS;
        if (max_offset < 0) max_offset = 0;
        if (scroll_offset > max_offset) {
            scroll_offset = max_offset;
        }
    }
    // else scroll_offset stays 0, we stay at bottom showing newest content
}

// Clear the entire scrollback and screen
static void clear_all(void) {
    for (int i = 0; i < SCROLLBACK_LINES; i++) {
        for (int j = 0; j < TERM_COLS; j++) {
            scrollback[i][j] = ' ';
        }
    }
    scroll_head = TERM_ROWS;  // Leave room for visible area
    scroll_count = TERM_ROWS;
    scroll_offset = 0;
    cursor_row = 0;
    cursor_col = 0;
}

// ============ Drawing Functions ============

static void draw_char_at(int row, int col, char c) {
    if (row < 0 || row >= TERM_ROWS || col < 0 || col >= TERM_COLS) return;

    int px = col * CHAR_WIDTH;
    int py = row * CHAR_HEIGHT;

    const uint8_t *glyph = &api->font_data[(unsigned char)c * 16];

    for (int y = 0; y < CHAR_HEIGHT; y++) {
        for (int x = 0; x < CHAR_WIDTH; x++) {
            uint32_t color = (glyph[y] & (0x80 >> x)) ? TERM_FG : TERM_BG;
            int idx = (py + y) * win_w + (px + x);
            if (idx >= 0 && idx < win_w * win_h) {
                win_buffer[idx] = color;
            }
        }
    }
}

static void draw_cursor(void) {
    // Only draw cursor if we're at the bottom (not scrolled back) and visible
    if (scroll_offset != 0 || !cursor_visible) return;

    // Draw modern blue bar cursor
    int px = cursor_col * CHAR_WIDTH;
    int py = cursor_row * CHAR_HEIGHT;

    // 2-pixel wide vertical bar cursor (modern style)
    for (int y = 0; y < CHAR_HEIGHT; y++) {
        for (int x = 0; x < 2; x++) {
            int idx = (py + y) * win_w + (px + x);
            if (idx >= 0 && idx < win_w * win_h) {
                win_buffer[idx] = CURSOR_COLOR;
            }
        }
    }
}

// Update cursor blink state
static void update_cursor_blink(void) {
    unsigned long now = api->get_uptime_ticks();
    // Blink every 50 ticks (500ms at 100Hz)
    if (now - last_blink_tick >= 50) {
        cursor_visible = !cursor_visible;
        last_blink_tick = now;
        screen_dirty = 1;  // Defer redraw to main loop
    }
}

static void draw_scrollbar(void) {
    // Calculate scrollbar geometry
    int sb_x = TERM_COLS * CHAR_WIDTH;
    int sb_y = 0;
    int sb_h = WIN_HEIGHT;

    // Draw scrollbar background (subtle light gray)
    gfx_fill_rect(&gfx, sb_x, sb_y, SCROLLBAR_WIDTH, sb_h, SCROLL_BG);

    // Calculate thumb position and size
    int max_offset = scroll_count - TERM_ROWS;
    if (max_offset < 0) max_offset = 0;

    // If there's no scrollable content, draw a subtle disabled thumb
    if (max_offset == 0) {
        // Small rounded thumb at bottom (macOS style when no scroll)
        int thumb_h = 40;
        int thumb_y = sb_h - thumb_h - 4;
        gfx_fill_rounded_rect(&gfx, sb_x + 4, thumb_y, SCROLLBAR_WIDTH - 8, thumb_h, 4, 0x00E0E0E0);
        return;
    }

    // Calculate thumb size proportional to visible content
    int thumb_h = (sb_h * TERM_ROWS) / scroll_count;
    if (thumb_h < SCROLLBAR_MIN_THUMB) thumb_h = SCROLLBAR_MIN_THUMB;
    if (thumb_h > sb_h - 8) thumb_h = sb_h - 8;

    // Calculate thumb position (inverted because higher offset = scrolled up)
    int track_range = sb_h - thumb_h - 8;
    int thumb_y = sb_y + 4 + ((max_offset - scroll_offset) * track_range) / max_offset;

    // Draw rounded pill-style thumb (modern macOS style)
    gfx_fill_rounded_rect(&gfx, sb_x + 4, thumb_y, SCROLLBAR_WIDTH - 8, thumb_h, 4, SCROLL_THUMB);
}

static void redraw_screen(void) {
    // Clear buffer
    for (int i = 0; i < win_w * win_h; i++) {
        win_buffer[i] = TERM_BG;
    }

    // Draw all characters from scrollback
    for (int row = 0; row < TERM_ROWS; row++) {
        char *line = get_line(row);
        for (int col = 0; col < TERM_COLS; col++) {
            char c = line[col];
            if (c && c != ' ') {
                draw_char_at(row, col, c);
            }
        }
    }

    // Draw scrollback indicator if scrolled back
    if (scroll_offset > 0) {
        // Draw a small indicator in top-right of text area
        char indicator[16];
        int lines_back = scroll_offset;
        // Simple integer to string
        int i = 0;
        indicator[i++] = '[';
        if (lines_back >= 100) indicator[i++] = '0' + (lines_back / 100) % 10;
        if (lines_back >= 10) indicator[i++] = '0' + (lines_back / 10) % 10;
        indicator[i++] = '0' + lines_back % 10;
        indicator[i++] = ']';
        indicator[i] = '\0';

        // Draw at top right of text area, inverted
        int start_col = TERM_COLS - i;
        for (int j = 0; j < i && indicator[j]; j++) {
            // Draw inverted
            int px = (start_col + j) * CHAR_WIDTH;
            int py = 0;
            const uint8_t *glyph = &api->font_data[(unsigned char)indicator[j] * 16];
            for (int y = 0; y < CHAR_HEIGHT; y++) {
                for (int x = 0; x < CHAR_WIDTH; x++) {
                    uint32_t color = (glyph[y] & (0x80 >> x)) ? TERM_BG : TERM_FG;
                    int idx = (py + y) * win_w + (px + x);
                    if (idx >= 0 && idx < win_w * win_h) {
                        win_buffer[idx] = color;
                    }
                }
            }
        }
    }

    // Draw cursor
    draw_cursor();

    // Draw scrollbar
    draw_scrollbar();

    // Tell desktop to redraw
    api->window_invalidate(window_id);
}

// ============ Terminal Operations ============

static void term_putc(char c) {
    if (c == '\f') {
        // Form feed - clear screen
        clear_all();
        return;
    }

    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
        if (cursor_row >= TERM_ROWS) {
            cursor_row = TERM_ROWS - 1;
            new_line();
        }
        // Jump to bottom when outputting
        scroll_offset = 0;
        return;
    }

    if (c == '\r') {
        cursor_col = 0;
        return;
    }

    if (c == '\b') {
        if (cursor_col > 0) {
            cursor_col--;
        }
        return;
    }

    if (c == '\t') {
        cursor_col = (cursor_col + 8) & ~7;
        if (cursor_col >= TERM_COLS) {
            cursor_col = 0;
            cursor_row++;
            if (cursor_row >= TERM_ROWS) {
                cursor_row = TERM_ROWS - 1;
                new_line();
            }
        }
        return;
    }

    if (c >= 32 && c < 127) {
        // Printable character - write to current line
        char *line = get_write_line();
        line[cursor_col] = c;
        cursor_col++;
        if (cursor_col >= TERM_COLS) {
            cursor_col = 0;
            cursor_row++;
            if (cursor_row >= TERM_ROWS) {
                cursor_row = TERM_ROWS - 1;
                new_line();
            }
        }
        // Jump to bottom when outputting
        scroll_offset = 0;
    }
}

static void term_puts(const char *s) {
    while (*s) {
        term_putc(*s++);
    }
}

// ============ Stdio Hooks ============

static void stdio_hook_putc(char c) {
    term_putc(c);
    screen_dirty = 1;  // Defer redraw to main loop
}

static void stdio_hook_puts(const char *s) {
    term_puts(s);
    screen_dirty = 1;  // Defer redraw to main loop
}

static int stdio_hook_getc(void) {
    if (input_head == input_tail) {
        return -1;  // No input available
    }
    int c = input_buffer[input_head];
    input_head = (input_head + 1) % INPUT_BUF_SIZE;
    return c;
}

static int stdio_hook_has_key(void) {
    return input_head != input_tail;
}

// Add a key to input buffer
static void input_push(int c) {
    int next = (input_tail + 1) % INPUT_BUF_SIZE;
    if (next != input_head) {  // Not full
        input_buffer[input_tail] = c;
        input_tail = next;
    }
}

// ============ Scrolling ============

static void scroll_up(int lines) {
    // Scroll view back (show older content)
    int max_offset = scroll_count - TERM_ROWS;
    if (max_offset < 0) max_offset = 0;

    scroll_offset += lines;
    if (scroll_offset > max_offset) {
        scroll_offset = max_offset;
    }

    redraw_screen();
}

static void scroll_down(int lines) {
    // Scroll view forward (show newer content)
    scroll_offset -= lines;
    if (scroll_offset < 0) {
        scroll_offset = 0;
    }

    redraw_screen();
}

static void scroll_to_bottom(void) {
    scroll_offset = 0;
    redraw_screen();
}

// ============ Main ============

int main(kapi_t *kapi, int argc, char **argv) {
    (void)argc;
    (void)argv;

    api = kapi;

    // Check if window API is available
    if (!api->window_create) {
        api->puts("term: no window manager available\n");
        return 1;
    }

    // Create window (add chrome height for title bar, separator, and corner radius)
    window_id = api->window_create(50, 50, WIN_WIDTH, WIN_HEIGHT + WIN_CHROME_HEIGHT, "Terminal");
    if (window_id < 0) {
        api->puts("term: failed to create window\n");
        return 1;
    }

    // Get window buffer
    win_buffer = api->window_get_buffer(window_id, &win_w, &win_h);
    if (!win_buffer) {
        api->puts("term: failed to get window buffer\n");
        api->window_destroy(window_id);
        return 1;
    }

    // Initialize graphics context
    gfx_init(&gfx, win_buffer, win_w, win_h, api->font_data);

    // Initialize scrollback buffer
    clear_all();

    // Clear window to background color
    for (int i = 0; i < win_w * win_h; i++) {
        win_buffer[i] = TERM_BG;
    }

    // Register stdio hooks
    api->stdio_putc = stdio_hook_putc;
    api->stdio_puts = stdio_hook_puts;
    api->stdio_getc = stdio_hook_getc;
    api->stdio_has_key = stdio_hook_has_key;

    // Initial draw
    redraw_screen();

    // Spawn kikish - it will use our stdio hooks
    int shell_pid = api->spawn("/bin/kikish");
    if (shell_pid < 0) {
        term_puts("Failed to start shell!\n");
        redraw_screen();
    }

    // Track last mouse Y for scroll detection
    int last_mouse_y = -1;
    int mouse_scrolling = 0;

    // Main event loop
    while (shell_running) {
        // Poll window events
        int event_type, data1, data2, data3;
        while (api->window_poll_event(window_id, &event_type, &data1, &data2, &data3)) {
            if (event_type == WIN_EVENT_CLOSE) {
                shell_running = 0;
                break;
            }

            if (event_type == WIN_EVENT_KEY) {
                int key = data1;

                // Check for scroll keys (Page Up/Page Down)
                if (key == KEY_PGUP) {  // Page Up
                    scroll_up(TERM_ROWS / 2);  // Scroll up half a screen
                    continue;  // Don't pass to shell
                }
                if (key == KEY_PGDN) {  // Page Down
                    scroll_down(TERM_ROWS / 2);  // Scroll down half a screen
                    continue;  // Don't pass to shell
                }

                // Key pressed - add to input buffer (data1 is int, preserves special keys)
                input_push(key);

                // Reset cursor blink to visible on keypress
                cursor_visible = 1;
                last_blink_tick = api->get_uptime_ticks();

                // If we're scrolled back and user types, jump to bottom
                if (scroll_offset > 0) {
                    scroll_to_bottom();
                }
            }

            if (event_type == WIN_EVENT_MOUSE_DOWN) {
                int mouse_x = data1;
                int mouse_y = data2;

                // Check if click is in scrollbar area
                int sb_x = TERM_COLS * CHAR_WIDTH;
                if (mouse_x >= sb_x && mouse_x < sb_x + SCROLLBAR_WIDTH) {
                    // Click in scrollbar
                    scrollbar_dragging = 1;
                    scrollbar_drag_start_y = mouse_y;
                    scrollbar_drag_start_offset = scroll_offset;
                } else {
                    // Click in text area - start content drag scrolling
                    last_mouse_y = mouse_y;
                    mouse_scrolling = 1;
                }
            }

            if (event_type == WIN_EVENT_MOUSE_UP) {
                mouse_scrolling = 0;
                scrollbar_dragging = 0;
            }

            if (event_type == WIN_EVENT_MOUSE_MOVE) {
                int mouse_y = data2;

                // Handle scrollbar dragging
                if (scrollbar_dragging && (data3 & MOUSE_BTN_LEFT)) {
                    int dy = mouse_y - scrollbar_drag_start_y;

                    // Calculate how much to scroll based on drag distance
                    int max_offset = scroll_count - TERM_ROWS;
                    if (max_offset < 0) max_offset = 0;

                    if (max_offset > 0) {
                        // Calculate thumb size and track range
                        int sb_h = WIN_HEIGHT;
                        int thumb_h = (sb_h * TERM_ROWS) / scroll_count;
                        if (thumb_h < SCROLLBAR_MIN_THUMB) thumb_h = SCROLLBAR_MIN_THUMB;
                        if (thumb_h > sb_h - 4) thumb_h = sb_h - 4;
                        int track_range = sb_h - thumb_h - 4;

                        // Convert pixel movement to scroll offset (inverted)
                        int offset_delta = -(dy * max_offset) / track_range;
                        int new_offset = scrollbar_drag_start_offset + offset_delta;

                        // Clamp
                        if (new_offset < 0) new_offset = 0;
                        if (new_offset > max_offset) new_offset = max_offset;

                        if (new_offset != scroll_offset) {
                            scroll_offset = new_offset;
                            redraw_screen();
                        }
                    }
                }
                // Handle content drag scrolling
                else if (mouse_scrolling && (data3 & MOUSE_BTN_LEFT)) {
                    int dy = mouse_y - last_mouse_y;
                    if (dy < -CHAR_HEIGHT) {
                        // Dragged up (mouse Y decreased) = scroll down (show newer)
                        scroll_down(1);
                        last_mouse_y = mouse_y;
                    } else if (dy > CHAR_HEIGHT) {
                        // Dragged down (mouse Y increased) = scroll up (show older)
                        scroll_up(1);
                        last_mouse_y = mouse_y;
                    }
                }
            }

            if (event_type == WIN_EVENT_RESIZE) {
                // Re-fetch buffer with new dimensions
                win_buffer = api->window_get_buffer(window_id, &win_w, &win_h);
                gfx_init(&gfx, win_buffer, win_w, win_h, api->font_data);
                redraw_screen();
            }
        }

        // Update cursor blink
        update_cursor_blink();

        // Redraw if dirty (once per frame, not per character)
        if (screen_dirty) {
            redraw_screen();
            screen_dirty = 0;
        }

        // Yield to other processes
        api->yield();
    }

    // Kill the shell process if it's still running
    if (shell_pid > 0 && api->kill_process) {
        api->kill_process(shell_pid);
    }

    // Clean up stdio hooks
    api->stdio_putc = 0;
    api->stdio_puts = 0;
    api->stdio_getc = 0;
    api->stdio_has_key = 0;

    // Destroy window
    api->window_destroy(window_id);

    return 0;
}
