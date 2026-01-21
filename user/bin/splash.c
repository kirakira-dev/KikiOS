/*
 * KikiOS Boot Splash
 *
 * Minimal boot screen: ASCII logo + progress bar
 * White on black, modern Mac vibes
 */

#include "../lib/kiki.h"
#include "../lib/gfx.h"

static kapi_t *api;
static gfx_ctx_t gfx;

// Bitmap font for "KikiOS" - each letter is 7 pixels wide, 9 pixels tall
// 1 = white pixel, 0 = black/transparent

static const unsigned char letter_V[9][7] = {
    {1,1,0,0,0,1,1},
    {1,1,0,0,0,1,1},
    {1,1,0,0,0,1,1},
    {0,1,1,0,1,1,0},
    {0,1,1,0,1,1,0},
    {0,0,1,1,1,0,0},
    {0,0,1,1,1,0,0},
    {0,0,0,1,0,0,0},
    {0,0,0,1,0,0,0},
};

static const unsigned char letter_i[9][3] = {
    {0,1,0},
    {0,1,0},
    {0,0,0},
    {1,1,0},
    {0,1,0},
    {0,1,0},
    {0,1,0},
    {0,1,0},
    {1,1,1},
};

static const unsigned char letter_b[9][6] = {
    {1,1,0,0,0,0},
    {1,1,0,0,0,0},
    {1,1,0,0,0,0},
    {1,1,1,1,1,0},
    {1,1,0,0,1,1},
    {1,1,0,0,1,1},
    {1,1,0,0,1,1},
    {1,1,0,0,1,1},
    {1,1,1,1,1,0},
};

static const unsigned char letter_e[9][6] = {
    {0,0,0,0,0,0},
    {0,0,0,0,0,0},
    {0,0,0,0,0,0},
    {0,1,1,1,1,0},
    {1,1,0,0,1,1},
    {1,1,1,1,1,1},
    {1,1,0,0,0,0},
    {1,1,0,0,0,1},
    {0,1,1,1,1,0},
};

static const unsigned char letter_O[9][7] = {
    {0,1,1,1,1,1,0},
    {1,1,0,0,0,1,1},
    {1,1,0,0,0,1,1},
    {1,1,0,0,0,1,1},
    {1,1,0,0,0,1,1},
    {1,1,0,0,0,1,1},
    {1,1,0,0,0,1,1},
    {1,1,0,0,0,1,1},
    {0,1,1,1,1,1,0},
};

static const unsigned char letter_S[9][6] = {
    {0,1,1,1,1,0},
    {1,1,0,0,1,1},
    {1,1,0,0,0,0},
    {1,1,0,0,0,0},
    {0,1,1,1,1,0},
    {0,0,0,0,1,1},
    {0,0,0,0,1,1},
    {1,1,0,0,1,1},
    {0,1,1,1,1,0},
};

// Draw a single letter bitmap at position, scaled
static void draw_letter(const unsigned char *bitmap, int rows, int cols,
                        int x, int y, int scale) {
    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            if (bitmap[row * cols + col]) {
                gfx_fill_rect(&gfx, x + col * scale, y + row * scale,
                              scale, scale, COLOR_WHITE);
            }
        }
    }
}

// Draw "KikiOS" logo centered
static void draw_logo(int cx, int cy, int scale) {
    // Letter widths and spacing
    int spacing = 2 * scale;  // gap between letters

    // Calculate total width: V(7) + i(3) + b(6) + e(6) + O(7) + S(6) + 5 gaps
    int total_w = (7 + 3 + 6 + 6 + 7 + 6) * scale + 5 * spacing;
    int letter_h = 9 * scale;

    int x = cx - total_w / 2;
    int y = cy - letter_h / 2;

    // V
    draw_letter(&letter_V[0][0], 9, 7, x, y, scale);
    x += 7 * scale + spacing;

    // i
    draw_letter(&letter_i[0][0], 9, 3, x, y, scale);
    x += 3 * scale + spacing;

    // b
    draw_letter(&letter_b[0][0], 9, 6, x, y, scale);
    x += 6 * scale + spacing;

    // e
    draw_letter(&letter_e[0][0], 9, 6, x, y, scale);
    x += 6 * scale + spacing;

    // O
    draw_letter(&letter_O[0][0], 9, 7, x, y, scale);
    x += 7 * scale + spacing;

    // S
    draw_letter(&letter_S[0][0], 9, 6, x, y, scale);
}

// Draw progress bar
static void draw_progress_bar(int cx, int cy, int width, int height, int progress) {
    int x = cx - width / 2;
    int y = cy - height / 2;

    // Border (1px white outline with rounded corners feel)
    gfx_draw_rect(&gfx, x, y, width, height, COLOR_WHITE);

    // Fill (progress 0-100)
    int fill_w = ((width - 4) * progress) / 100;
    if (fill_w > 0) {
        gfx_fill_rect(&gfx, x + 2, y + 2, fill_w, height - 4, COLOR_WHITE);
    }
}

int main(kapi_t *kapi, int argc, char **argv) {
    api = kapi;

    // Boot target from argv, or default to desktop
    const char *boot_target = "/bin/desktop";
    if (argc > 1 && argv[1]) {
        boot_target = argv[1];
    }

    // Initialize graphics context directly on framebuffer
    gfx_init(&gfx, api->fb_base, api->fb_width, api->fb_height, api->font_data);

    // Fill screen with black
    gfx_fill_rect(&gfx, 0, 0, gfx.width, gfx.height, COLOR_BLACK);

    // Calculate positions
    int center_x = gfx.width / 2;
    int center_y = gfx.height / 2;

    // Scale based on resolution
    int logo_scale = 3;
    if (gfx.height >= 1080) logo_scale = 6;
    if (gfx.height >= 1440) logo_scale = 8;

    // Logo positioned above center
    int logo_y = center_y - 50;
    if (gfx.height >= 1080) logo_y = center_y - 100;

    // Progress bar below logo
    int bar_y = center_y + 70;
    int bar_width = 220;
    int bar_height = 5;
    if (gfx.height >= 1080) {
        bar_y = center_y + 140;
        bar_width = 340;
        bar_height = 8;
    }

    // Draw logo (static)
    draw_logo(center_x, logo_y, logo_scale);

    // Animate progress bar over 2 seconds
    int total_steps = 40;
    int delay_ms = 50;

    for (int step = 0; step <= total_steps; step++) {
        int progress = (step * 100) / total_steps;
        draw_progress_bar(center_x, bar_y, bar_width, bar_height, progress);
        api->sleep_ms(delay_ms);
    }

    // Brief pause at 100%
    api->sleep_ms(100);

    // Launch boot target
    api->exec(boot_target);

    return 0;
}
