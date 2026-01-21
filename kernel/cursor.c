/*
 * KikiOS Mouse Cursor
 *
 * Classic Mac-style arrow cursor with save/restore of background
 */

#include "cursor.h"
#include "mouse.h"
#include "fb.h"

// Cursor dimensions
#define CURSOR_WIDTH 12
#define CURSOR_HEIGHT 19

// Classic Mac arrow cursor (1 = black, 2 = white, 0 = transparent)
static const uint8_t cursor_data[CURSOR_HEIGHT][CURSOR_WIDTH] = {
    {1,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0,0},
    {1,2,2,2,2,2,2,2,2,1,0,0},
    {1,2,2,2,2,2,2,2,2,2,1,0},
    {1,2,2,2,2,2,2,1,1,1,1,1},
    {1,2,2,2,1,2,2,1,0,0,0,0},
    {1,2,2,1,0,1,2,2,1,0,0,0},
    {1,2,1,0,0,1,2,2,1,0,0,0},
    {1,1,0,0,0,0,1,2,2,1,0,0},
    {1,0,0,0,0,0,1,2,2,1,0,0},
    {0,0,0,0,0,0,0,1,2,1,0,0},
    {0,0,0,0,0,0,0,1,1,0,0,0},
};

// Saved background under cursor
static uint32_t saved_bg[CURSOR_HEIGHT][CURSOR_WIDTH];
static int saved_x = -1;
static int saved_y = -1;
static int cursor_visible = 0;

// Current cursor position
static int cursor_x = 0;
static int cursor_y = 0;

// Access framebuffer
extern uint32_t fb_width, fb_height;
extern uint32_t *fb_base;

// Save the background pixels under cursor
static void save_background(int x, int y) {
    for (int row = 0; row < CURSOR_HEIGHT; row++) {
        for (int col = 0; col < CURSOR_WIDTH; col++) {
            int px = x + col;
            int py = y + row;
            if (px >= 0 && px < (int)fb_width && py >= 0 && py < (int)fb_height) {
                saved_bg[row][col] = fb_base[py * fb_width + px];
            }
        }
    }
    saved_x = x;
    saved_y = y;
}

// Restore the background pixels
static void restore_background(void) {
    if (saved_x < 0) return;

    for (int row = 0; row < CURSOR_HEIGHT; row++) {
        for (int col = 0; col < CURSOR_WIDTH; col++) {
            int px = saved_x + col;
            int py = saved_y + row;
            if (px >= 0 && px < (int)fb_width && py >= 0 && py < (int)fb_height) {
                fb_base[py * fb_width + px] = saved_bg[row][col];
            }
        }
    }
    saved_x = -1;
}

// Draw the cursor at position
static void draw_cursor_at(int x, int y) {
    for (int row = 0; row < CURSOR_HEIGHT; row++) {
        for (int col = 0; col < CURSOR_WIDTH; col++) {
            uint8_t pixel = cursor_data[row][col];
            if (pixel == 0) continue;  // Transparent

            int px = x + col;
            int py = y + row;
            if (px >= 0 && px < (int)fb_width && py >= 0 && py < (int)fb_height) {
                uint32_t color = (pixel == 1) ? 0x00000000 : 0x00FFFFFF;  // Black or white
                fb_base[py * fb_width + px] = color;
            }
        }
    }
}

void cursor_init(void) {
    cursor_x = fb_width / 2;
    cursor_y = fb_height / 2;
    cursor_visible = 0;
    saved_x = -1;
}

void cursor_show(void) {
    if (!cursor_visible) {
        save_background(cursor_x, cursor_y);
        draw_cursor_at(cursor_x, cursor_y);
        cursor_visible = 1;
    }
}

void cursor_hide(void) {
    if (cursor_visible) {
        restore_background();
        cursor_visible = 0;
    }
}

void cursor_update(void) {
    int new_x, new_y;
    mouse_get_screen_pos(&new_x, &new_y);

    // Only update if position changed
    if (new_x == cursor_x && new_y == cursor_y) {
        return;
    }

    // Hide old cursor (restore background)
    if (cursor_visible) {
        restore_background();
    }

    // Update position
    cursor_x = new_x;
    cursor_y = new_y;

    // Draw new cursor
    if (cursor_visible) {
        save_background(cursor_x, cursor_y);
        draw_cursor_at(cursor_x, cursor_y);
    }
}

void cursor_draw(int x, int y) {
    cursor_x = x;
    cursor_y = y;
    if (cursor_visible) {
        restore_background();
    }
    save_background(x, y);
    draw_cursor_at(x, y);
    cursor_visible = 1;
}

void cursor_get_pos(int *x, int *y) {
    if (x) *x = cursor_x;
    if (y) *y = cursor_y;
}
