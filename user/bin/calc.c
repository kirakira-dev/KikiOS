/*
 * KikiOS Calculator
 *
 * Floating-point calculator that runs in a desktop window.
 * Uses the window API to create and manage its window.
 */

#include "kiki.h"
#include "../lib/gfx.h"

static kapi_t *api;
static int window_id = -1;
static uint32_t *win_buffer;
static int win_w, win_h;
static gfx_ctx_t gfx;

// Calculator state - now using doubles!
static double display_value = 0.0;
static double pending_value = 0.0;
static char pending_op = 0;
static int clear_on_digit = 0;
static int has_decimal = 0;      // Are we entering decimal digits?
static double decimal_place = 0.1;  // Current decimal place (0.1, 0.01, etc.)

// Button layout
#define BTN_W 40
#define BTN_H 30
#define BTN_PAD 4
#define DISPLAY_H 30

static const char button_labels[4][4][3] = {
    { "7", "8", "9", "/" },
    { "4", "5", "6", "*" },
    { "1", "2", "3", "-" },
    { ".", "0", "=", "+" }
};

// Modern color palette
#define COLOR_BG         0x00F5F5F5
#define COLOR_DISPLAY_BG 0x00FFFFFF
#define COLOR_DISPLAY_FG 0x00222222
#define COLOR_BTN_NUM    0x00FFFFFF
#define COLOR_BTN_OP     0x00FF9500  // Orange for operators
#define COLOR_BTN_EQ     0x00E0E0E0  // Gray for equals (like number buttons)
#define COLOR_BTN_TEXT   0x00333333
#define COLOR_BTN_OP_TXT 0x00FFFFFF
#define COLOR_BTN_BORDER 0x00CCCCCC

// ============ Drawing Helpers (macros wrapping gfx lib) ============

#define buf_fill_rect(x, y, w, h, c)     gfx_fill_rect(&gfx, x, y, w, h, c)
#define buf_draw_char(x, y, ch, fg, bg)  gfx_draw_char(&gfx, x, y, ch, fg, bg)
#define buf_draw_string(x, y, s, fg, bg) gfx_draw_string(&gfx, x, y, s, fg, bg)
#define buf_draw_rect(x, y, w, h, c)     gfx_draw_rect(&gfx, x, y, w, h, c)
#define buf_fill_rounded(x, y, w, h, r, c) gfx_fill_rounded_rect(&gfx, x, y, w, h, r, c)
#define buf_draw_rounded(x, y, w, h, r, c) gfx_draw_rounded_rect(&gfx, x, y, w, h, r, c)

// ============ Float to String ============

// Simple float to string conversion (no sprintf with %f available)
static void float_to_str(double val, char *buf, int buf_size) {
    int idx = 0;

    // Handle negative
    if (val < 0) {
        buf[idx++] = '-';
        val = -val;
    }

    // Get integer part
    long long int_part = (long long)val;
    double frac_part = val - (double)int_part;

    // Convert integer part
    char int_buf[20];
    int int_idx = 0;
    if (int_part == 0) {
        int_buf[int_idx++] = '0';
    } else {
        while (int_part > 0 && int_idx < 19) {
            int_buf[int_idx++] = '0' + (int_part % 10);
            int_part /= 10;
        }
    }

    // Reverse integer part into buffer
    while (int_idx > 0 && idx < buf_size - 1) {
        buf[idx++] = int_buf[--int_idx];
    }

    // Add decimal point and fractional part (up to 6 digits)
    if (frac_part > 0.0000001 && idx < buf_size - 8) {
        buf[idx++] = '.';

        int frac_digits = 0;
        while (frac_part > 0.0000001 && frac_digits < 6 && idx < buf_size - 1) {
            frac_part *= 10;
            int digit = (int)frac_part;
            buf[idx++] = '0' + digit;
            frac_part -= digit;
            frac_digits++;
        }

        // Remove trailing zeros
        while (idx > 0 && buf[idx-1] == '0') {
            idx--;
        }
        // Remove decimal point if no fractional digits
        if (idx > 0 && buf[idx-1] == '.') {
            idx--;
        }
    }

    buf[idx] = '\0';
}

// ============ Drawing ============

static void draw_display(void) {
    // Display background - rounded white box
    buf_fill_rounded(BTN_PAD, BTN_PAD, win_w - BTN_PAD * 2, DISPLAY_H, 6, COLOR_DISPLAY_BG);
    buf_draw_rounded(BTN_PAD, BTN_PAD, win_w - BTN_PAD * 2, DISPLAY_H, 6, COLOR_BTN_BORDER);

    // Format number
    char buf[24];
    float_to_str(display_value, buf, sizeof(buf));

    // Right-align in display
    int text_len = strlen(buf);
    int text_x = win_w - BTN_PAD * 2 - text_len * 8 - 8;
    buf_draw_string(text_x, BTN_PAD + 8, buf, COLOR_DISPLAY_FG, COLOR_DISPLAY_BG);
}

static void draw_button(int row, int col, int pressed) {
    int x = BTN_PAD + col * (BTN_W + BTN_PAD);
    int y = DISPLAY_H + BTN_PAD * 2 + row * (BTN_H + BTN_PAD);

    const char *label = button_labels[row][col];
    char c = label[0];

    // Determine button colors based on type
    uint32_t bg, fg;
    int is_op = (c == '+' || c == '-' || c == '*' || c == '/');
    int is_eq = (c == '=');

    if (is_eq) {
        bg = pressed ? 0x00CCCCCC : COLOR_BTN_EQ;
        fg = COLOR_BTN_TEXT;  // Dark text on gray button
    } else if (is_op) {
        bg = pressed ? 0x00CC7700 : COLOR_BTN_OP;
        fg = COLOR_BTN_OP_TXT;
    } else {
        bg = pressed ? 0x00E0E0E0 : COLOR_BTN_NUM;
        fg = COLOR_BTN_TEXT;
    }

    // Button face - rounded
    buf_fill_rounded(x, y, BTN_W, BTN_H, 6, bg);
    buf_draw_rounded(x, y, BTN_W, BTN_H, 6, COLOR_BTN_BORDER);

    // Label
    int label_len = strlen(label);
    int lx = x + (BTN_W - label_len * 8) / 2;
    int ly = y + (BTN_H - 16) / 2;
    buf_draw_string(lx, ly, label, fg, bg);
}

static void draw_all(void) {
    // Clear background
    buf_fill_rect(0, 0, win_w, win_h, COLOR_BG);

    // Display
    draw_display();

    // Buttons
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            draw_button(row, col, 0);
        }
    }

    api->window_invalidate(window_id);
}

// ============ Calculator Logic ============

static void do_op(void) {
    switch (pending_op) {
        case '+': display_value = pending_value + display_value; break;
        case '-': display_value = pending_value - display_value; break;
        case '*': display_value = pending_value * display_value; break;
        case '/':
            if (display_value != 0.0)
                display_value = pending_value / display_value;
            break;
    }
    pending_op = 0;
}

static void button_click(int row, int col) {
    const char *label = button_labels[row][col];
    char c = label[0];

    if (c >= '0' && c <= '9') {
        int digit = c - '0';
        if (clear_on_digit) {
            display_value = digit;
            clear_on_digit = 0;
            has_decimal = 0;
            decimal_place = 0.1;
        } else if (has_decimal) {
            display_value = display_value + digit * decimal_place;
            decimal_place *= 0.1;
        } else {
            display_value = display_value * 10.0 + digit;
        }
    } else if (c == '.') {
        if (!has_decimal) {
            has_decimal = 1;
            decimal_place = 0.1;
            if (clear_on_digit) {
                display_value = 0.0;
                clear_on_digit = 0;
            }
        }
    } else if (c == '=') {
        if (pending_op) {
            do_op();
        }
        clear_on_digit = 1;
        has_decimal = 0;
    } else if (c == '+' || c == '-' || c == '*' || c == '/') {
        if (pending_op) {
            do_op();
        }
        pending_value = display_value;
        pending_op = c;
        clear_on_digit = 1;
        has_decimal = 0;
    }
}

// Clear function (C button replaced with .)
static void clear_calc(void) {
    display_value = 0.0;
    pending_value = 0.0;
    pending_op = 0;
    clear_on_digit = 0;
    has_decimal = 0;
    decimal_place = 0.1;
}

// ============ Input Handling ============

static int button_at_point(int x, int y) {
    // Returns button index (row * 4 + col) or -1
    int bx = x - BTN_PAD;
    int by = y - (DISPLAY_H + BTN_PAD * 2);

    if (bx < 0 || by < 0) return -1;

    int col = bx / (BTN_W + BTN_PAD);
    int row = by / (BTN_H + BTN_PAD);

    if (row < 0 || row >= 4 || col < 0 || col >= 4) return -1;

    // Check if actually within button bounds (not in padding)
    int btn_x = BTN_PAD + col * (BTN_W + BTN_PAD);
    int btn_y = DISPLAY_H + BTN_PAD * 2 + row * (BTN_H + BTN_PAD);
    if (x < btn_x || x >= btn_x + BTN_W) return -1;
    if (y < btn_y || y >= btn_y + BTN_H) return -1;

    return row * 4 + col;
}

// ============ Main ============

int main(kapi_t *kapi, int argc, char **argv) {
    (void)argc;
    (void)argv;

    api = kapi;

    // Wait for window API to be available (desktop must be running)
    if (!api->window_create) {
        api->puts("calc: window API not available (desktop not running?)\n");
        return 1;
    }

    // Calculate window size
    int content_w = BTN_PAD * 2 + 4 * BTN_W + 3 * BTN_PAD;
    int content_h = DISPLAY_H + BTN_PAD * 3 + 4 * BTN_H + 3 * BTN_PAD;

    // Create window (add title bar height - desktop uses 28px title bar)
    window_id = api->window_create(200, 100, content_w, content_h + 28, "Calculator");
    if (window_id < 0) {
        api->puts("calc: failed to create window\n");
        return 1;
    }

    // Get buffer
    win_buffer = api->window_get_buffer(window_id, &win_w, &win_h);
    if (!win_buffer) {
        api->puts("calc: failed to get window buffer\n");
        api->window_destroy(window_id);
        return 1;
    }

    // Initialize graphics context
    gfx_init(&gfx, win_buffer, win_w, win_h, api->font_data);

    // Initial draw
    draw_all();

    // Event loop
    int running = 1;
    while (running) {
        int event_type, data1, data2, data3;
        while (api->window_poll_event(window_id, &event_type, &data1, &data2, &data3)) {
            switch (event_type) {
                case WIN_EVENT_CLOSE:
                    running = 0;
                    break;

                case WIN_EVENT_MOUSE_DOWN: {
                    int btn = button_at_point(data1, data2);
                    if (btn >= 0) {
                        int row = btn / 4;
                        int col = btn % 4;
                        draw_button(row, col, 1);  // Pressed
                        api->window_invalidate(window_id);
                    }
                    break;
                }

                case WIN_EVENT_MOUSE_UP: {
                    int btn = button_at_point(data1, data2);
                    if (btn >= 0) {
                        int row = btn / 4;
                        int col = btn % 4;
                        button_click(row, col);
                    }
                    draw_all();  // Redraw with button unpressed
                    break;
                }

                case WIN_EVENT_KEY: {
                    char c = (char)data1;
                    // Map keys to buttons
                    if (c >= '0' && c <= '9') {
                        int digit = c - '0';
                        int row = (9 - digit) / 3;
                        int col = (digit - 1) % 3;
                        if (digit == 0) { row = 3; col = 1; }
                        button_click(row, col);
                        draw_all();
                    } else if (c == '.') { button_click(3, 0); draw_all(); }
                    else if (c == '+') { button_click(3, 3); draw_all(); }
                    else if (c == '-') { button_click(2, 3); draw_all(); }
                    else if (c == '*') { button_click(1, 3); draw_all(); }
                    else if (c == '/') { button_click(0, 3); draw_all(); }
                    else if (c == '=' || c == '\r' || c == '\n') { button_click(3, 2); draw_all(); }
                    else if (c == 'c' || c == 'C') { clear_calc(); draw_all(); }
                    else if (c == 'q' || c == 'Q') { running = 0; }
                    break;
                }

                case WIN_EVENT_RESIZE:
                    // Re-fetch buffer with new dimensions
                    win_buffer = api->window_get_buffer(window_id, &win_w, &win_h);
                    gfx_init(&gfx, win_buffer, win_w, win_h, api->font_data);
                    draw_all();
                    break;
            }
        }

        // Yield to other processes
        api->yield();
    }

    api->window_destroy(window_id);
    return 0;
}
