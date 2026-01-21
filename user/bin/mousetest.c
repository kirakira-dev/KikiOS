/*
 * Mouse Test - Simple USB mouse test program
 * Shows mouse position, cursor, and button clicks
 */

#include "kiki.h"

// Simple integer to string
static void itoa(int n, char *buf) {
    if (n < 0) {
        *buf++ = '-';
        n = -n;
    }
    char tmp[12];
    int i = 0;
    if (n == 0) {
        tmp[i++] = '0';
    } else {
        while (n > 0) {
            tmp[i++] = '0' + (n % 10);
            n /= 10;
        }
    }
    while (i > 0) {
        *buf++ = tmp[--i];
    }
    *buf = '\0';
}

// Draw a simple cursor (crosshair)
static void draw_cursor(kapi_t *k, int x, int y, uint32_t color) {
    // Horizontal line
    for (int i = -8; i <= 8; i++) {
        int px = x + i;
        if (px >= 0 && px < (int)k->fb_width) {
            k->fb_put_pixel(px, y, color);
        }
    }
    // Vertical line
    for (int i = -8; i <= 8; i++) {
        int py = y + i;
        if (py >= 0 && py < (int)k->fb_height) {
            k->fb_put_pixel(x, py, color);
        }
    }
}

// Draw filled circle for click indicator
static void draw_circle(kapi_t *k, int cx, int cy, int r, uint32_t color) {
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if (x*x + y*y <= r*r) {
                int px = cx + x;
                int py = cy + y;
                if (px >= 0 && px < (int)k->fb_width &&
                    py >= 0 && py < (int)k->fb_height) {
                    k->fb_put_pixel(px, py, color);
                }
            }
        }
    }
}

int main(kapi_t *k) {
    // Clear screen to dark gray
    k->fb_fill_rect(0, 0, k->fb_width, k->fb_height, 0x00202020);

    // Title
    k->fb_draw_string(10, 10, "Mouse Test - Press any key to exit", COLOR_WHITE, 0x00202020);
    k->fb_draw_string(10, 30, "Move mouse, click left/right buttons", COLOR_CYAN, 0x00202020);

    // Status area
    k->fb_draw_string(10, 60, "Position:", COLOR_WHITE, 0x00202020);
    k->fb_draw_string(10, 80, "Buttons:", COLOR_WHITE, 0x00202020);

    int prev_x = -1, prev_y = -1;
    uint8_t prev_buttons = 0;
    int poll_count = 0;

    // Click indicator positions
    int left_x = k->fb_width / 2 - 80;
    int right_x = k->fb_width / 2 + 80;
    int btn_y = k->fb_height - 60;

    // Draw button labels
    k->fb_draw_string(left_x - 40, btn_y + 30, "LEFT", COLOR_WHITE, 0x00202020);
    k->fb_draw_string(right_x - 44, btn_y + 30, "RIGHT", COLOR_WHITE, 0x00202020);

    while (1) {
        // Check for key to exit
        if (k->has_key()) {
            k->getc();  // consume the key
            break;
        }

        // Poll mouse
        k->mouse_poll();

        // Get mouse state
        int mx, my;
        k->mouse_get_pos(&mx, &my);
        uint8_t buttons = k->mouse_get_buttons();

        // Only update if something changed
        if (mx != prev_x || my != prev_y || buttons != prev_buttons) {
            // Erase old cursor
            if (prev_x >= 0 && prev_y >= 0) {
                draw_cursor(k, prev_x, prev_y, 0x00202020);
            }

            // Draw new cursor
            draw_cursor(k, mx, my, COLOR_WHITE);

            // Update position display
            char buf[64];
            k->fb_fill_rect(100, 60, 150, 16, 0x00202020);
            buf[0] = 'X';
            buf[1] = ':';
            buf[2] = ' ';
            itoa(mx, buf + 3);
            int len = strlen(buf);
            buf[len] = ' ';
            buf[len+1] = ' ';
            buf[len+2] = 'Y';
            buf[len+3] = ':';
            buf[len+4] = ' ';
            itoa(my, buf + len + 5);
            k->fb_draw_string(100, 60, buf, COLOR_GREEN, 0x00202020);

            // Update button display
            k->fb_fill_rect(100, 80, 200, 16, 0x00202020);
            char btn_str[32];
            int idx = 0;
            if (buttons & MOUSE_BTN_LEFT) {
                btn_str[idx++] = 'L';
                btn_str[idx++] = 'E';
                btn_str[idx++] = 'F';
                btn_str[idx++] = 'T';
                btn_str[idx++] = ' ';
            }
            if (buttons & MOUSE_BTN_RIGHT) {
                btn_str[idx++] = 'R';
                btn_str[idx++] = 'I';
                btn_str[idx++] = 'G';
                btn_str[idx++] = 'H';
                btn_str[idx++] = 'T';
                btn_str[idx++] = ' ';
            }
            if (buttons & MOUSE_BTN_MIDDLE) {
                btn_str[idx++] = 'M';
                btn_str[idx++] = 'I';
                btn_str[idx++] = 'D';
            }
            if (idx == 0) {
                btn_str[idx++] = '-';
            }
            btn_str[idx] = '\0';
            k->fb_draw_string(100, 80, btn_str, COLOR_YELLOW, 0x00202020);

            // Draw button indicators
            uint32_t left_color = (buttons & MOUSE_BTN_LEFT) ? COLOR_GREEN : 0x00404040;
            uint32_t right_color = (buttons & MOUSE_BTN_RIGHT) ? COLOR_RED : 0x00404040;
            draw_circle(k, left_x, btn_y, 25, left_color);
            draw_circle(k, right_x, btn_y, 25, right_color);

            prev_x = mx;
            prev_y = my;
            prev_buttons = buttons;
        }

        // Small delay to avoid busy spinning
        k->yield();
    }

    // Clear screen before exit
    k->clear();
    k->puts("Mouse test ended.\n");

    return 0;
}
