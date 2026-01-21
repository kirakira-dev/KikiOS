/*
 * doomgeneric for KikiOS
 * Platform-specific implementation for doomgeneric port
 *
 * Copyright (C) 2024-2025 Kaan Senol
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2.
 */

#include "doom_libc.h"
#include "doomgeneric.h"
#include "doomkeys.h"
#include "d_event.h"

/* External function to post events to DOOM */
extern void D_PostEvent(event_t *ev);

/* Global kapi pointer - also used by doom_libc */
kapi_t *doom_kapi = 0;

/* Start time for DG_GetTicksMs */
static uint64_t start_ticks = 0;

/* Screen positioning - calculated at runtime to center on any resolution */
static int screen_offset_x = 0;
static int screen_offset_y = 0;
static int scale_factor = 1;

/* Key queue for input */
#define KEYQUEUE_SIZE 64
static struct {
    unsigned char key;
    int pressed;
} key_queue[KEYQUEUE_SIZE];
static int key_queue_read = 0;
static int key_queue_write = 0;

/* Track which keys are currently held (for release events) */
static unsigned char keys_held[256];

/* Add a key event to the queue */
static void add_key_event(unsigned char doom_key, int pressed) {
    int next = (key_queue_write + 1) % KEYQUEUE_SIZE;
    if (next != key_queue_read) {
        key_queue[key_queue_write].key = doom_key;
        key_queue[key_queue_write].pressed = pressed;
        key_queue_write = next;
    }
}

/* Map KikiOS key to DOOM key */
static unsigned char translate_key(int vibe_key) {
    /* Arrow keys */
    if (vibe_key == 0x100) return KEY_UPARROW;     /* KEY_UP */
    if (vibe_key == 0x101) return KEY_DOWNARROW;   /* KEY_DOWN */
    if (vibe_key == 0x102) return KEY_LEFTARROW;   /* KEY_LEFT */
    if (vibe_key == 0x103) return KEY_RIGHTARROW;  /* KEY_RIGHT */

    /* Modifier keys */
    if (vibe_key == 0x109) return KEY_RCTRL;       /* SPECIAL_KEY_CTRL = fire */
    if (vibe_key == 0x10A) return KEY_RSHIFT;      /* SPECIAL_KEY_SHIFT = run */

    /* Special keys */
    if (vibe_key == 27) return KEY_ESCAPE;
    if (vibe_key == '\n' || vibe_key == '\r') return KEY_ENTER;
    if (vibe_key == '\t') return KEY_TAB;
    if (vibe_key == ' ') return KEY_USE;           /* Space = use/open doors */
    if (vibe_key == 127 || vibe_key == 8) return KEY_BACKSPACE;

    /* Control key (ASCII 1-26 are Ctrl+letter) - also fire */
    if (vibe_key >= 1 && vibe_key <= 26) {
        return KEY_RCTRL;  /* Ctrl+letter = fire */
    }

    /* Function keys (if supported) */
    if (vibe_key >= 0x110 && vibe_key <= 0x11B) {
        return KEY_F1 + (vibe_key - 0x110);
    }

    /* WASD movement + E for use */
    if (vibe_key == 'w' || vibe_key == 'W') return KEY_UPARROW;
    if (vibe_key == 's' || vibe_key == 'S') return KEY_DOWNARROW;
    if (vibe_key == 'a' || vibe_key == 'A') return KEY_STRAFE_L;
    if (vibe_key == 'd' || vibe_key == 'D') return KEY_STRAFE_R;
    if (vibe_key == 'e' || vibe_key == 'E') return KEY_USE;

    /* Letters - lowercase them for DOOM */
    if (vibe_key >= 'A' && vibe_key <= 'Z') {
        return vibe_key + 32;  /* lowercase */
    }
    if (vibe_key >= 'a' && vibe_key <= 'z') {
        return vibe_key;
    }

    /* Numbers and common symbols */
    if (vibe_key >= '0' && vibe_key <= '9') return vibe_key;
    if (vibe_key == '-') return KEY_MINUS;
    if (vibe_key == '=') return KEY_EQUALS;
    if (vibe_key == '+') return '+';
    if (vibe_key == ',') return ',';
    if (vibe_key == '.') return '.';
    if (vibe_key == '/') return '/';

    /* Y/N for prompts */
    if (vibe_key == 'y' || vibe_key == 'Y') return 'y';
    if (vibe_key == 'n' || vibe_key == 'N') return 'n';

    return 0;  /* Unknown key */
}

/* Poll keyboard and queue events */
static void poll_keys(void) {
    while (doom_kapi->has_key()) {
        int c = doom_kapi->getc();
        if (c < 0) break;

        unsigned char doom_key = translate_key(c);
        if (doom_key) {
            /* Key press */
            add_key_event(doom_key, 1);
            keys_held[doom_key] = 1;
        }
    }

    /* Generate release events for held keys after a delay
     * (KikiOS doesn't have key-up events, so we fake them) */
    static uint64_t last_release_check = 0;
    uint64_t now = doom_kapi->get_uptime_ticks();
    if (now - last_release_check > 10) {  /* Every 100ms */
        last_release_check = now;
        for (int i = 0; i < 256; i++) {
            if (keys_held[i]) {
                add_key_event(i, 0);  /* Release */
                keys_held[i] = 0;
            }
        }
    }
}

/* Poll mouse and post events to DOOM */
static void poll_mouse(void) {
    if (!doom_kapi->mouse_get_delta) return;

    /* Get accumulated delta (this also polls and clears) */
    int dx, dy;
    doom_kapi->mouse_get_delta(&dx, &dy);

    /* Get button state */
    uint8_t buttons = doom_kapi->mouse_get_buttons();
    int doom_buttons = 0;
    if (buttons & 0x01) doom_buttons |= 1;  /* Left = fire */
    if (buttons & 0x02) doom_buttons |= 2;  /* Right */
    if (buttons & 0x04) doom_buttons |= 4;  /* Middle */

    /* Post event if there's movement or buttons pressed */
    if (dx != 0 || doom_buttons) {
        event_t ev;
        ev.type = ev_mouse;
        ev.data1 = doom_buttons;
        ev.data2 = dx * 2;   /* Scale up for better sensitivity */
        ev.data3 = 0;        /* Ignore Y - mouse for turning only */
        ev.data4 = 0;
        D_PostEvent(&ev);
    }
}

/* ============ DoomGeneric Platform Functions ============ */

void DG_Init(void) {
    /* Record start time */
    start_ticks = doom_kapi->get_uptime_ticks();

    /* Calculate scale factor - largest integer scale that fits */
    int fb_w = doom_kapi->fb_width;
    int fb_h = doom_kapi->fb_height;
    int scale_x = fb_w / DOOMGENERIC_RESX;
    int scale_y = fb_h / DOOMGENERIC_RESY;
    scale_factor = (scale_x < scale_y) ? scale_x : scale_y;
    if (scale_factor < 1) scale_factor = 1;

    /* Calculate centering offsets */
    int scaled_w = DOOMGENERIC_RESX * scale_factor;
    int scaled_h = DOOMGENERIC_RESY * scale_factor;
    screen_offset_x = (fb_w - scaled_w) / 2;
    screen_offset_y = (fb_h - scaled_h) / 2;

    /* Clear screen to black */
    if (doom_kapi->fb_base) {
        uint32_t *fb = doom_kapi->fb_base;
        int total = fb_w * fb_h;
        for (int i = 0; i < total; i++) {
            fb[i] = 0;
        }
    }

    /* Initialize key state */
    for (int i = 0; i < 256; i++) {
        keys_held[i] = 0;
    }

    printf("DG_Init: KikiOS DOOM initialized\n");
    printf("  DOOM res: %dx%d, scale: %dx, screen: %dx%d\n",
           DOOMGENERIC_RESX, DOOMGENERIC_RESY, scale_factor, fb_w, fb_h);
    printf("  Centered at (%d,%d), output: %dx%d\n",
           screen_offset_x, screen_offset_y, scaled_w, scaled_h);
}

void DG_DrawFrame(void) {
    if (!doom_kapi->fb_base || !DG_ScreenBuffer) return;

    uint32_t *fb = doom_kapi->fb_base;
    int fb_width = doom_kapi->fb_width;

    if (scale_factor == 1) {
        /* No scaling - direct copy */
        pixel_t *src = DG_ScreenBuffer;
        for (int y = 0; y < DOOMGENERIC_RESY; y++) {
            uint32_t *dst = fb + (y + screen_offset_y) * fb_width + screen_offset_x;
            for (int x = 0; x < DOOMGENERIC_RESX; x++) {
                *dst++ = *src++;
            }
        }
    } else {
        /* Integer scaling - duplicate pixels */
        for (int y = 0; y < DOOMGENERIC_RESY; y++) {
            pixel_t *src_row = DG_ScreenBuffer + y * DOOMGENERIC_RESX;
            for (int sy = 0; sy < scale_factor; sy++) {
                uint32_t *dst = fb + (y * scale_factor + sy + screen_offset_y) * fb_width + screen_offset_x;
                for (int x = 0; x < DOOMGENERIC_RESX; x++) {
                    uint32_t pixel = src_row[x];
                    for (int sx = 0; sx < scale_factor; sx++) {
                        *dst++ = pixel;
                    }
                }
            }
        }
    }
}

void DG_SleepMs(uint32_t ms) {
    doom_kapi->sleep_ms(ms);
}

uint32_t DG_GetTicksMs(void) {
    /* KikiOS ticks are 10ms each (100Hz timer) */
    uint64_t now = doom_kapi->get_uptime_ticks();
    return (uint32_t)((now - start_ticks) * 10);
}

int DG_GetKey(int *pressed, unsigned char *doomKey) {
    /* Poll for new input */
    poll_keys();
    poll_mouse();

    /* Return key from queue if available */
    if (key_queue_read != key_queue_write) {
        *pressed = key_queue[key_queue_read].pressed;
        *doomKey = key_queue[key_queue_read].key;
        key_queue_read = (key_queue_read + 1) % KEYQUEUE_SIZE;
        return 1;
    }

    return 0;  /* No key available */
}

void DG_SetWindowTitle(const char *title) {
    /* No window title in KikiOS - just print to console */
    (void)title;
}

/* ============ Main Entry Point ============ */

int main(kapi_t *api, int argc, char **argv) {
    /* Save kapi pointer globally */
    doom_kapi = api;

    /* Initialize libc with kapi */
    doom_libc_init(api);

    /* Clear screen */
    api->clear();

    printf("DOOM for KikiOS\n");
    printf("===============\n\n");

    /* Default arguments if none provided */
    static char *default_argv[] = {
        "doom",
        "-iwad", "/games/doom1.wad",
        NULL
    };

    if (argc < 2) {
        printf("No WAD specified, using default: /games/doom1.wad\n");
        argc = 3;
        argv = default_argv;
    }

    printf("Starting DOOM with %d args:\n", argc);
    for (int i = 0; i < argc; i++) {
        printf("  argv[%d] = %s\n", i, argv[i]);
    }
    printf("\n");

    /* Initialize DOOM */
    printf("Calling doomgeneric_Create...\n");
    doomgeneric_Create(argc, argv);

    printf("Entering main loop...\n");

    /* Main game loop */
    while (1) {
        doomgeneric_Tick();
    }

    return 0;
}
