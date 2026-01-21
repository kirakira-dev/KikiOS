/*
 * KikiOS Desktop - Window Manager
 *
 * macOS-inspired desktop with dock, menu bar, and draggable windows.
 *
 * Fullscreen apps (snake, tetris) are launched with exec() and take over.
 * Windowed apps use the window API registered in kapi.
 */

#include "kiki.h"
#include "../lib/gfx.h"
#include "../lib/icons.h"

// Screen dimensions (set dynamically from kapi)
static int SCREEN_WIDTH;
static int SCREEN_HEIGHT;

// UI dimensions
#define MENU_BAR_HEIGHT 28
#define DOCK_HEIGHT     70
#define TITLE_BAR_HEIGHT 28
#define CORNER_RADIUS   10
#define SHADOW_BLUR     4    // Subtle shadow
#define SHADOW_OFFSET   2

// Modern color palette - macOS inspired
#define COLOR_BLACK       0x00000000
#define COLOR_WHITE       0x00FFFFFF

// Theme colors (can be changed at runtime)
static uint32_t color_desktop        = 0x00FFFFFF;  // Desktop background
static uint32_t color_win_bg         = 0x00FFFFFF;  // Window background
static uint32_t color_win_border     = 0x00D0D0D0;
static uint32_t color_title_active   = 0x00F0F0F0;
static uint32_t color_title_inactive = 0x00E8E8E8;
static uint32_t color_title_text     = 0x00333333;
static uint32_t color_shadow         = 0x00AAAAAA;
static uint32_t color_menu_bg        = 0x00F5F5F5;
static uint32_t color_menu_text      = 0x00222222;
static uint32_t color_menu_highlight = 0x00007AFF;  // macOS blue
static uint32_t color_dock_bg        = 0x00F0F0F0;
static uint32_t color_dock_border    = 0x00CCCCCC;
static uint32_t color_accent         = 0x00007AFF;  // Blue

// Fixed colors (same in both themes)
#define COLOR_BTN_CLOSE      0x00FF5F57  // Red
#define COLOR_BTN_MINIMIZE   0x00FFBD2E  // Yellow
#define COLOR_BTN_ZOOM       0x0028C840  // Green
#define COLOR_BTN_INACTIVE   0x00CCCCCC

// Compatibility macros for existing code
#define COLOR_DESKTOP        color_desktop
#define COLOR_WIN_BG         color_win_bg
#define COLOR_WIN_BORDER     color_win_border
#define COLOR_TITLE_ACTIVE   color_title_active
#define COLOR_TITLE_INACTIVE color_title_inactive
#define COLOR_TITLE_TEXT     color_title_text
#define COLOR_SHADOW         color_shadow
#define COLOR_MENU_BG        color_menu_bg
#define COLOR_MENU_TEXT      color_menu_text
#define COLOR_MENU_HIGHLIGHT color_menu_highlight
#define COLOR_DOCK_BG        color_dock_bg
#define COLOR_DOCK_BORDER    color_dock_border
#define COLOR_ACCENT         color_accent
#define COLOR_ACCENT_HOVER   0x00339FFF

// Window limits
#define MAX_WINDOWS 16
#define MAX_TITLE_LEN 32

// Event structure
typedef struct {
    int type;
    int data1;
    int data2;
    int data3;
} win_event_t;

// Window structure
typedef struct {
    int active;           // Is this slot in use?
    int x, y, w, h;       // Position and size (including title bar)
    char title[MAX_TITLE_LEN];
    uint32_t *buffer;     // Content buffer (w * (h - TITLE_BAR_HEIGHT))
    int dirty;            // Needs redraw?
    int pid;              // Owner process ID (0 = desktop owns it)

    // Minimize/maximize state
    int minimized;        // Window is minimized to dock
    int maximized;        // Window is maximized
    int restore_x, restore_y, restore_w, restore_h;  // Saved position for restore

    // Event queue (ring buffer)
    win_event_t events[32];
    int event_head;
    int event_tail;
} window_t;

// Dock icon
typedef struct {
    int x, y, w, h;
    const char *label;
    int label_len;        // Cached strlen(label)
    const char *exec_path;
    int is_fullscreen;    // If true, use exec() instead of spawn()
} dock_icon_t;

// Global state
static kapi_t *api;
static uint32_t *backbuffer;
static gfx_ctx_t gfx;  // Graphics context for backbuffer
static window_t windows[MAX_WINDOWS];
static int window_order[MAX_WINDOWS];  // Z-order: window_order[0] is topmost
static int window_count = 0;
static int focused_window = -1;

// Hardware double buffering state
static int use_hw_double_buffer = 0;
static int current_buffer = 0;

// Mouse state
static int mouse_x, mouse_y;
static int mouse_prev_x, mouse_prev_y;
static uint8_t mouse_buttons;
static uint8_t mouse_prev_buttons;

// Dragging state
static int dragging_window = -1;
static int drag_offset_x, drag_offset_y;

// Resizing state
static int resizing_window = -1;
static int resize_start_w, resize_start_h;
static int resize_start_mx, resize_start_my;

// Desktop running flag
static int running = 1;

// Classic mode (flat graphics for Pi - no shadows, alpha, rounded corners)
static int classic_mode = 0;

// Redraw control - skip frames when nothing changed
static int needs_redraw = 1;        // Full redraw needed
static int cursor_moved = 0;        // Just cursor position changed

// Cursor background save (for cursor-only updates)
static uint32_t cursor_save[16 * 16];
static int cursor_save_x = -100, cursor_save_y = -100;
static int cursor_save_valid = 0;

// Dock hover state (to avoid redrawing dock when nothing changed)
static int dock_hover_idx = -1;
static int dock_hover_prev = -1;

// Dock context menu state
static int dock_context_menu_visible = 0;
static int dock_context_menu_x = 0;
static int dock_context_menu_y = 0;
static int dock_context_menu_idx = -1;  // Which dock icon was right-clicked

// Menu system
#define MENU_NONE     -1
#define MENU_APPLE     0
#define MENU_FILE      1
#define MENU_EDIT      2
#define MENU_SETTINGS  3

static int open_menu = MENU_NONE;  // Currently open menu (-1 = none)

// Settings state
static int dark_theme = 0;         // 0 = light, 1 = dark
static int wallpaper_style = 0;    // 0 = solid white, 1 = gradient, 2 = pattern

// Menu item structure
typedef struct {
    const char *label;   // NULL = separator
    int action;          // Action ID
} menu_item_t;

// Action IDs
#define ACTION_NONE           0
#define ACTION_ABOUT          1
#define ACTION_QUIT           2
#define ACTION_NEW_WINDOW     3
#define ACTION_CLOSE_WINDOW   4
#define ACTION_CUT            5
#define ACTION_COPY           6
#define ACTION_PASTE          7
#define ACTION_THEME_LIGHT    8
#define ACTION_THEME_DARK     9
#define ACTION_WALLPAPER_SOLID    10
#define ACTION_WALLPAPER_GRADIENT 11
#define ACTION_WALLPAPER_PATTERN  12

// Apple menu items
static const menu_item_t apple_menu[] = {
    { "About This Computer", ACTION_ABOUT },
    { NULL, 0 },  // separator
    { "Quit Desktop", ACTION_QUIT },
    { NULL, -1 }  // end marker
};

// File menu items
static const menu_item_t file_menu[] = {
    { "New Terminal", ACTION_NEW_WINDOW },
    { "Close Window", ACTION_CLOSE_WINDOW },
    { NULL, -1 }
};

// Edit menu items
static const menu_item_t edit_menu[] = {
    { "Cut", ACTION_CUT },
    { "Copy", ACTION_COPY },
    { "Paste", ACTION_PASTE },
    { NULL, -1 }
};

// Settings menu items
static const menu_item_t settings_menu[] = {
    { "Theme: Light", ACTION_THEME_LIGHT },
    { "Theme: Dark", ACTION_THEME_DARK },
    { NULL, 0 },  // separator
    { "Wallpaper: Solid", ACTION_WALLPAPER_SOLID },
    { "Wallpaper: Gradient", ACTION_WALLPAPER_GRADIENT },
    { "Wallpaper: Pattern", ACTION_WALLPAPER_PATTERN },
    { NULL, -1 }
};

// Forward declarations
static void draw_desktop(void);
static void draw_window(int wid);
static void draw_dock(void);
static void draw_menu_bar(void);
static void flip_buffer(void);
static void draw_about_dialog(void);
static void draw_settings_dialog(void);
static void draw_circle_filled(int cx, int cy, int r, uint32_t color);

// Apply theme colors based on dark_theme setting
static void apply_theme(void) {
    if (dark_theme) {
        // Dark theme
        color_desktop        = 0x001E1E1E;  // Dark gray desktop
        color_win_bg         = 0x002D2D2D;  // Dark window bg
        color_win_border     = 0x00404040;
        color_title_active   = 0x003C3C3C;
        color_title_inactive = 0x00333333;
        color_title_text     = 0x00FFFFFF;
        color_shadow         = 0x00000000;
        color_menu_bg        = 0x002D2D2D;
        color_menu_text      = 0x00FFFFFF;
        color_menu_highlight = 0x00007AFF;
        color_dock_bg        = 0x002D2D2D;
        color_dock_border    = 0x00505050;
        color_accent         = 0x00007AFF;
    } else {
        // Light theme (default)
        color_desktop        = 0x00FFFFFF;
        color_win_bg         = 0x00FFFFFF;
        color_win_border     = 0x00D0D0D0;
        color_title_active   = 0x00F0F0F0;
        color_title_inactive = 0x00E8E8E8;
        color_title_text     = 0x00333333;
        color_shadow         = 0x00AAAAAA;
        color_menu_bg        = 0x00F5F5F5;
        color_menu_text      = 0x00222222;
        color_menu_highlight = 0x00007AFF;
        color_dock_bg        = 0x00F0F0F0;
        color_dock_border    = 0x00CCCCCC;
        color_accent         = 0x00007AFF;
    }
}

// Request a full screen redraw
static inline void request_redraw(void) {
    needs_redraw = 1;
}

// About dialog state (declared here so draw_desktop can see it)
static int show_about_dialog = 0;

// Settings dialog state
static int show_settings_dialog = 0;

// ============ Backbuffer Drawing (wrappers around gfx lib) ============

#define bb_put_pixel(x, y, c)           gfx_put_pixel(&gfx, x, y, c)
#define bb_fill_rect(x, y, w, h, c)     gfx_fill_rect(&gfx, x, y, w, h, c)
#define bb_fill_rect_alpha(x, y, w, h, c, a) gfx_fill_rect_alpha(&gfx, x, y, w, h, c, a)
#define bb_draw_char(x, y, ch, fg, bg)  gfx_draw_char(&gfx, x, y, ch, fg, bg)
#define bb_draw_string(x, y, s, fg, bg) gfx_draw_string(&gfx, x, y, s, fg, bg)
#define bb_draw_hline(x, y, w, c)       gfx_draw_hline(&gfx, x, y, w, c)
#define bb_draw_vline(x, y, h, c)       gfx_draw_vline(&gfx, x, y, h, c)
#define bb_draw_rect(x, y, w, h, c)     gfx_draw_rect(&gfx, x, y, w, h, c)
#define bb_fill_pattern(x, y, w, h)     gfx_fill_pattern(&gfx, x, y, w, h, COLOR_BLACK, COLOR_WHITE)
#define bb_gradient_v(x, y, w, h, t, b) gfx_gradient_v(&gfx, x, y, w, h, t, b)
#define bb_fill_rounded(x, y, w, h, r, c) gfx_fill_rounded_rect(&gfx, x, y, w, h, r, c)
#define bb_fill_rounded_alpha(x, y, w, h, r, c, a) gfx_fill_rounded_rect_alpha(&gfx, x, y, w, h, r, c, a)
#define bb_draw_rounded(x, y, w, h, r, c) gfx_draw_rounded_rect(&gfx, x, y, w, h, r, c)
#define bb_box_shadow(x, y, w, h, blur, ox, oy, c) gfx_box_shadow(&gfx, x, y, w, h, blur, ox, oy, c)
#define bb_box_shadow_rounded(x, y, w, h, r, blur, ox, oy, c) gfx_box_shadow_rounded(&gfx, x, y, w, h, r, blur, ox, oy, c)

// ============ KikiOS Logo (from icons.h) ============

static void draw_vibeos_logo(int x, int y) {
    for (int py = 0; py < 16; py++) {
        for (int px = 0; px < 16; px++) {
            if (vibeos_logo[py * 16 + px]) {
                bb_put_pixel(x + px, y + py, COLOR_BLACK);
            }
        }
    }
}

// ============ Dock Icons (from icons.h) ============

static void draw_icon_bitmap(int x, int y, const unsigned char *bitmap, uint32_t bg_color) {
    uint32_t fg = COLOR_BLACK;

    // Fast path: icon fully visible (no bounds checking per pixel)
    if (x >= 0 && y >= 0 && x + 32 <= SCREEN_WIDTH && y + 32 <= SCREEN_HEIGHT) {
        for (int py = 0; py < 32; py++) {
            uint32_t *row = &backbuffer[(y + py) * SCREEN_WIDTH + x];
            const unsigned char *src = &bitmap[py * 32];
            for (int px = 0; px < 32; px++) {
                row[px] = src[px] ? fg : bg_color;
            }
        }
    } else {
        // Slow path with bounds checking (rare - icon near edge)
        for (int py = 0; py < 32; py++) {
            for (int px = 0; px < 32; px++) {
                uint32_t color = bitmap[py * 32 + px] ? fg : bg_color;
                bb_put_pixel(x + px, y + py, color);
            }
        }
    }
}

// ============ Window Management ============

static int find_free_window(void) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].active) return i;
    }
    return -1;
}

static void bring_to_front(int wid) {
    if (wid < 0 || !windows[wid].active) return;

    // Find current position in z-order
    int pos = -1;
    for (int i = 0; i < window_count; i++) {
        if (window_order[i] == wid) {
            pos = i;
            break;
        }
    }

    if (pos < 0) return;

    // Shift everything down and put this at front
    for (int i = pos; i > 0; i--) {
        window_order[i] = window_order[i - 1];
    }
    window_order[0] = wid;
    focused_window = wid;
    request_redraw();
}

static int window_at_point(int x, int y) {
    // Check in z-order (front to back)
    for (int i = 0; i < window_count; i++) {
        int wid = window_order[i];
        window_t *w = &windows[wid];
        if (w->active && !w->minimized) {
            if (x >= w->x && x < w->x + w->w &&
                y >= w->y && y < w->y + w->h) {
                return wid;
            }
        }
    }
    return -1;
}

static void push_event(int wid, int event_type, int data1, int data2, int data3) {
    if (wid < 0 || !windows[wid].active) return;
    window_t *w = &windows[wid];

    int next = (w->event_tail + 1) % 32;
    if (next == w->event_head) return;  // Queue full

    w->events[w->event_tail].type = event_type;
    w->events[w->event_tail].data1 = data1;
    w->events[w->event_tail].data2 = data2;
    w->events[w->event_tail].data3 = data3;
    w->event_tail = next;
}

// ============ Window API (registered in kapi) ============

static int wm_window_create(int x, int y, int w, int h, const char *title) {
    int wid = find_free_window();
    if (wid < 0) return -1;

    window_t *win = &windows[wid];
    win->active = 1;
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    win->dirty = 1;
    win->pid = 0;  // TODO: get current process
    win->event_head = 0;
    win->event_tail = 0;
    win->minimized = 0;
    win->maximized = 0;
    win->restore_x = x;
    win->restore_y = y;
    win->restore_w = w;
    win->restore_h = h;

    // Copy title
    int i;
    for (i = 0; i < MAX_TITLE_LEN - 1 && title[i]; i++) {
        win->title[i] = title[i];
    }
    win->title[i] = '\0';

    // Allocate content buffer (excluding title bar)
    int content_h = h - TITLE_BAR_HEIGHT;
    if (content_h < 1) content_h = 1;
    win->buffer = api->malloc(w * content_h * sizeof(uint32_t));
    if (!win->buffer) {
        win->active = 0;
        return -1;
    }

    // Clear to white (use DMA if available for speed)
    if (api->dma_fill) {
        api->dma_fill(win->buffer, COLOR_WIN_BG, w * content_h * sizeof(uint32_t));
    } else {
        for (int j = 0; j < w * content_h; j++) {
            win->buffer[j] = COLOR_WIN_BG;
        }
    }

    // Add to z-order (at front)
    for (int j = window_count; j > 0; j--) {
        window_order[j] = window_order[j - 1];
    }
    window_order[0] = wid;
    window_count++;
    focused_window = wid;
    request_redraw();

    return wid;
}

static void wm_window_destroy(int wid) {
    if (wid < 0 || wid >= MAX_WINDOWS || !windows[wid].active) return;

    window_t *win = &windows[wid];
    if (win->buffer) {
        api->free(win->buffer);
        win->buffer = 0;
    }
    win->active = 0;

    // Remove from z-order
    int pos = -1;
    for (int i = 0; i < window_count; i++) {
        if (window_order[i] == wid) {
            pos = i;
            break;
        }
    }
    if (pos >= 0) {
        for (int i = pos; i < window_count - 1; i++) {
            window_order[i] = window_order[i + 1];
        }
        window_count--;
    }

    // Update focus
    if (focused_window == wid) {
        focused_window = (window_count > 0) ? window_order[0] : -1;
    }
    request_redraw();
}

static uint32_t *wm_window_get_buffer(int wid, int *w, int *h) {
    if (wid < 0 || wid >= MAX_WINDOWS || !windows[wid].active) return 0;
    window_t *win = &windows[wid];
    if (w) *w = win->w;
    if (h) *h = win->h - TITLE_BAR_HEIGHT;
    return win->buffer;
}

static int wm_window_poll_event(int wid, int *event_type, int *data1, int *data2, int *data3) {
    if (wid < 0 || wid >= MAX_WINDOWS || !windows[wid].active) return 0;
    window_t *win = &windows[wid];

    if (win->event_head == win->event_tail) return 0;  // No events

    win_event_t *ev = &win->events[win->event_head];
    *event_type = ev->type;
    *data1 = ev->data1;
    *data2 = ev->data2;
    *data3 = ev->data3;
    win->event_head = (win->event_head + 1) % 32;
    return 1;
}

static void wm_window_invalidate(int wid) {
    if (wid < 0 || wid >= MAX_WINDOWS || !windows[wid].active) return;
    windows[wid].dirty = 1;
    request_redraw();
}

static void wm_window_set_title(int wid, const char *title) {
    if (wid < 0 || wid >= MAX_WINDOWS || !windows[wid].active) return;
    window_t *win = &windows[wid];
    int i;
    for (i = 0; i < MAX_TITLE_LEN - 1 && title[i]; i++) {
        win->title[i] = title[i];
    }
    win->title[i] = '\0';
    win->dirty = 1;
    request_redraw();
}

// ============ Dock ============

#define DOCK_ICON_SIZE 32
#define DOCK_PADDING 32   // Spacing between icons (enough for labels)
#define DOCK_LABEL_HEIGHT 14

// Dock icons with bitmap indices (label_len computed at init)
static dock_icon_t dock_icons[] = {
    { 0, 0, DOCK_ICON_SIZE, DOCK_ICON_SIZE, "Snake",    0, "/bin/snake",    1 },
    { 0, 0, DOCK_ICON_SIZE, DOCK_ICON_SIZE, "Tetris",   0, "/bin/tetris",   1 },
    { 0, 0, DOCK_ICON_SIZE, DOCK_ICON_SIZE, "DOOM",     0, "/bin/doom",     1 },
    { 0, 0, DOCK_ICON_SIZE, DOCK_ICON_SIZE, "Calc",     0, "/bin/calc",     0 },
    { 0, 0, DOCK_ICON_SIZE, DOCK_ICON_SIZE, "Files",    0, "/bin/files",    0 },
    { 0, 0, DOCK_ICON_SIZE, DOCK_ICON_SIZE, "Music",    0, "/bin/music",    0 },
    { 0, 0, DOCK_ICON_SIZE, DOCK_ICON_SIZE, "Kivi",     0, "/bin/browser",  0 },
    { 0, 0, DOCK_ICON_SIZE, DOCK_ICON_SIZE, "Term",     0, "/bin/term",     0 },
    { 0, 0, DOCK_ICON_SIZE, DOCK_ICON_SIZE, "SysMon",   0, "/bin/sysmon",   0 },
    { 0, 0, DOCK_ICON_SIZE, DOCK_ICON_SIZE, "KikiCode", 0, "/bin/kikicode", 0 },
    { 0, 0, DOCK_ICON_SIZE, DOCK_ICON_SIZE, "Settings", 0, "__SETTINGS__",  0 },
};
#define NUM_DOCK_ICONS (sizeof(dock_icons) / sizeof(dock_icons[0]))

// Cached dock dimensions (computed once at init)
static int dock_content_w, dock_pill_x, dock_pill_y, dock_pill_h;
static const int dock_pill_r = 16;  // Corner radius

static void init_dock_positions(void) {
    // Cache dock pill dimensions
    dock_content_w = NUM_DOCK_ICONS * (DOCK_ICON_SIZE + DOCK_PADDING) - DOCK_PADDING + 40;
    dock_pill_x = (SCREEN_WIDTH - dock_content_w) / 2;
    dock_pill_y = SCREEN_HEIGHT - DOCK_HEIGHT + 6;
    dock_pill_h = DOCK_HEIGHT - 12;

    // Icon positions
    int total_width = NUM_DOCK_ICONS * (DOCK_ICON_SIZE + DOCK_PADDING) - DOCK_PADDING + 32;
    int start_x = (SCREEN_WIDTH - total_width) / 2 + 16;
    int y = dock_pill_y + 4;  // Minimal top padding

    for (int i = 0; i < (int)NUM_DOCK_ICONS; i++) {
        dock_icons[i].x = start_x + i * (DOCK_ICON_SIZE + DOCK_PADDING);
        dock_icons[i].y = y;
        // Cache label length
        dock_icons[i].label_len = strlen(dock_icons[i].label);
    }
}

static void draw_dock_icon(dock_icon_t *icon, int icon_idx, int highlight) {
    // Draw highlight background if hovered
    if (highlight) {
        if (classic_mode) {
            bb_fill_rect(icon->x - 6, icon->y - 4, DOCK_ICON_SIZE + 12, DOCK_ICON_SIZE + DOCK_LABEL_HEIGHT + 8,
                         0x00CCCCCC);
        } else {
            bb_fill_rounded_alpha(icon->x - 6, icon->y - 4, DOCK_ICON_SIZE + 12, DOCK_ICON_SIZE + DOCK_LABEL_HEIGHT + 8,
                                  8, COLOR_ACCENT, 40);
        }
    }

    // Draw the bitmap icon with dock background color
    draw_icon_bitmap(icon->x, icon->y, icon_bitmaps[icon_idx], COLOR_DOCK_BG);

    // Draw label below icon (using cached label_len)
    int label_x = icon->x + (DOCK_ICON_SIZE - icon->label_len * 8) / 2;
    int label_y = icon->y + DOCK_ICON_SIZE + 4;

    for (int i = 0; icon->label[i]; i++) {
        gfx_draw_char(&gfx, label_x + i * 8, label_y, icon->label[i],
                      highlight ? COLOR_WHITE : COLOR_MENU_TEXT, COLOR_DOCK_BG);
    }
}

// Track minimized window count (avoids counting every frame)
static int minimized_count = 0;

static void draw_dock(void) {
    // Use cached dock pill dimensions
    if (classic_mode) {
        // Classic flat mode: simple rectangle, no shadow
        bb_fill_rect(dock_pill_x, dock_pill_y, dock_content_w, dock_pill_h, COLOR_DOCK_BG);
        bb_draw_rect(dock_pill_x, dock_pill_y, dock_content_w, dock_pill_h, COLOR_DOCK_BORDER);
    } else {
        // Fancy mode: shadow + rounded corners
        bb_box_shadow_rounded(dock_pill_x, dock_pill_y, dock_content_w, dock_pill_h, dock_pill_r,
                              4, 0, 2, 0x00999999);
        bb_fill_rounded(dock_pill_x, dock_pill_y, dock_content_w, dock_pill_h, dock_pill_r, COLOR_DOCK_BG);
        bb_draw_rounded(dock_pill_x, dock_pill_y, dock_content_w, dock_pill_h, dock_pill_r, COLOR_DOCK_BORDER);
    }

    // Icons
    for (int i = 0; i < (int)NUM_DOCK_ICONS; i++) {
        int highlight = (mouse_y >= dock_icons[i].y &&
                        mouse_y < dock_icons[i].y + DOCK_ICON_SIZE + DOCK_LABEL_HEIGHT &&
                        mouse_x >= dock_icons[i].x &&
                        mouse_x < dock_icons[i].x + DOCK_ICON_SIZE);
        draw_dock_icon(&dock_icons[i], i, highlight);
    }

    // Draw separator if there are minimized windows (using cached count)
    if (minimized_count > 0) {
        // Separator line
        int sep_x = dock_pill_x + dock_content_w - 4;
        bb_draw_vline(sep_x, dock_pill_y + 10, dock_pill_h - 20, COLOR_DOCK_BORDER);

        // Draw minimized windows to the right
        int min_x = sep_x + 8;
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (windows[i].active && windows[i].minimized) {
                // Small window preview
                int min_w = 40;
                int min_h = 30;
                int min_y = dock_pill_y + (dock_pill_h - min_h) / 2;

                // Check hover
                int hovering = (mouse_x >= min_x && mouse_x < min_x + min_w &&
                               mouse_y >= min_y && mouse_y < min_y + min_h);

                if (hovering) {
                    if (classic_mode) {
                        bb_fill_rect(min_x - 2, min_y - 2, min_w + 4, min_h + 4, 0x00CCCCCC);
                    } else {
                        bb_fill_rounded_alpha(min_x - 2, min_y - 2, min_w + 4, min_h + 4, 4, COLOR_ACCENT, 60);
                    }
                }

                // Mini window icon
                if (classic_mode) {
                    bb_fill_rect(min_x, min_y, min_w, min_h, COLOR_WIN_BG);
                    bb_draw_rect(min_x, min_y, min_w, min_h, COLOR_WIN_BORDER);
                } else {
                    bb_fill_rounded(min_x, min_y, min_w, min_h, 4, COLOR_WIN_BG);
                    bb_draw_rounded(min_x, min_y, min_w, min_h, 4, COLOR_WIN_BORDER);
                }
                // Title bar
                bb_fill_rect(min_x + 1, min_y + 1, min_w - 2, 8, COLOR_TITLE_ACTIVE);
                // Mini traffic lights
                draw_circle_filled(min_x + 5, min_y + 5, 2, COLOR_BTN_CLOSE);
                draw_circle_filled(min_x + 11, min_y + 5, 2, COLOR_BTN_MINIMIZE);
                draw_circle_filled(min_x + 17, min_y + 5, 2, COLOR_BTN_ZOOM);

                min_x += min_w + 8;
            }
        }
    }
}

static int dock_icon_at_point(int x, int y) {
    for (int i = 0; i < (int)NUM_DOCK_ICONS; i++) {
        if (x >= dock_icons[i].x && x < dock_icons[i].x + DOCK_ICON_SIZE &&
            y >= dock_icons[i].y && y < dock_icons[i].y + DOCK_ICON_SIZE) {
            return i;
        }
    }
    return -1;
}

// Draw dock context menu
static void draw_dock_context_menu(void) {
    if (!dock_context_menu_visible || dock_context_menu_idx < 0) return;

    int menu_w = 120;
    int menu_h = 32;
    int menu_x = dock_context_menu_x;
    int menu_y = dock_context_menu_y - menu_h - 8;  // Above the icon

    // Clamp to screen
    if (menu_x + menu_w > SCREEN_WIDTH) menu_x = SCREEN_WIDTH - menu_w;
    if (menu_y < MENU_BAR_HEIGHT) menu_y = dock_context_menu_y + 8;

    if (classic_mode) {
        // Classic flat mode
        bb_fill_rect(menu_x, menu_y, menu_w, menu_h, 0x00F8F8F8);
        bb_draw_rect(menu_x, menu_y, menu_w, menu_h, COLOR_DOCK_BORDER);
    } else {
        // Fancy mode with shadow and rounded corners
        int menu_r = 8;
        bb_box_shadow_rounded(menu_x, menu_y, menu_w, menu_h, menu_r, 6, 2, 3, 0x00888888);
        bb_fill_rounded_alpha(menu_x, menu_y, menu_w, menu_h, menu_r, 0x00F8F8F8, 250);
        bb_draw_rounded(menu_x, menu_y, menu_w, menu_h, menu_r, COLOR_DOCK_BORDER);
    }

    // "New Window" item
    int item_y = menu_y + 4;
    int hovering = (mouse_x >= menu_x + 4 && mouse_x < menu_x + menu_w - 4 &&
                   mouse_y >= item_y && mouse_y < item_y + 24);

    if (hovering) {
        if (classic_mode) {
            bb_fill_rect(menu_x + 4, item_y, menu_w - 8, 24, COLOR_MENU_HIGHLIGHT);
        } else {
            bb_fill_rounded(menu_x + 4, item_y, menu_w - 8, 24, 4, COLOR_MENU_HIGHLIGHT);
        }
        bb_draw_string(menu_x + 12, item_y + 4, "New Window", COLOR_WHITE, COLOR_MENU_HIGHLIGHT);
    } else {
        bb_draw_string(menu_x + 12, item_y + 4, "New Window", COLOR_MENU_TEXT, 0x00F8F8F8);
    }
}

// Check if click is on a minimized window in dock, return window id or -1
static int minimized_window_at_point(int x, int y) {
    // Use cached minimized_count
    if (minimized_count == 0) return -1;

    // Use cached dock dimensions
    int sep_x = dock_pill_x + dock_content_w - 4;
    int min_x = sep_x + 8;
    int min_w = 40;
    int min_h = 30;
    int min_y = dock_pill_y + (dock_pill_h - min_h) / 2;

    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].active && windows[i].minimized) {
            if (x >= min_x && x < min_x + min_w &&
                y >= min_y && y < min_y + min_h) {
                return i;
            }
            min_x += min_w + 8;
        }
    }
    return -1;
}

// ============ Menu Bar ============

// Cached date/time strings (updated every second)
static char cached_date[16];
static char cached_time[8];
static unsigned long last_datetime_update = 0;

// Update cached date/time strings (call once per second)
static void update_datetime_cache(void) {
    static const char day_names[7][4] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char month_names[12][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                             "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    int year, month, day, hour, minute, second, weekday;
    (void)year;
    api->get_datetime(&year, &month, &day, &hour, &minute, &second, &weekday);

    // Format time: "HH:MM"
    cached_time[0] = '0' + (hour / 10);
    cached_time[1] = '0' + (hour % 10);
    cached_time[2] = ':';
    cached_time[3] = '0' + (minute / 10);
    cached_time[4] = '0' + (minute % 10);
    cached_time[5] = '\0';

    // Format date: "Mon Dec 8"
    cached_date[0] = day_names[weekday][0];
    cached_date[1] = day_names[weekday][1];
    cached_date[2] = day_names[weekday][2];
    cached_date[3] = ' ';
    cached_date[4] = month_names[month - 1][0];
    cached_date[5] = month_names[month - 1][1];
    cached_date[6] = month_names[month - 1][2];
    cached_date[7] = ' ';
    if (day >= 10) {
        cached_date[8] = '0' + (day / 10);
        cached_date[9] = '0' + (day % 10);
        cached_date[10] = '\0';
    } else {
        cached_date[8] = '0' + day;
        cached_date[9] = '\0';
    }
}

// Menu bar item positions (x, width)
#define APPLE_MENU_X     4
#define APPLE_MENU_W     20
#define FILE_MENU_X      28
#define FILE_MENU_W      32
#define EDIT_MENU_X      68
#define EDIT_MENU_W      32
#define SETTINGS_MENU_X  108
#define SETTINGS_MENU_W  64

static void draw_dropdown_menu(int menu_x, const menu_item_t *items) {
    // Calculate menu dimensions
    int max_width = 0;
    int item_count = 0;
    for (int i = 0; items[i].action != -1; i++) {
        if (items[i].label) {
            int len = strlen(items[i].label);
            if (len > max_width) max_width = len;
        }
        item_count++;
    }

    int menu_w = max_width * 8 + 32;  // Padding on sides
    int menu_h = item_count * 24 + 8; // 24px per item + padding
    int menu_y = MENU_BAR_HEIGHT + 4;

    if (classic_mode) {
        // Classic flat mode
        bb_fill_rect(menu_x, menu_y, menu_w, menu_h, 0x00F8F8F8);
        bb_draw_rect(menu_x, menu_y, menu_w, menu_h, COLOR_WIN_BORDER);
    } else {
        // Fancy mode with shadow and rounded corners
        int menu_r = 8;
        bb_box_shadow_rounded(menu_x, menu_y, menu_w, menu_h, menu_r, 8, 2, 4, COLOR_BLACK);
        bb_fill_rounded_alpha(menu_x, menu_y, menu_w, menu_h, menu_r, 0x00F8F8F8, 245);
    }

    // Draw items
    int y = menu_y + 4;
    for (int i = 0; items[i].action != -1; i++) {
        if (items[i].label == NULL) {
            // Separator
            bb_draw_hline(menu_x + 8, y + 11, menu_w - 16, 0x00DDDDDD);
        } else {
            // Check if mouse is over this item
            int item_y = y;
            int hovering = (mouse_y >= item_y && mouse_y < item_y + 24 &&
                           mouse_x >= menu_x + 4 && mouse_x < menu_x + menu_w - 4);

            if (hovering) {
                if (classic_mode) {
                    bb_fill_rect(menu_x + 4, item_y, menu_w - 8, 22, COLOR_MENU_HIGHLIGHT);
                } else {
                    bb_fill_rounded(menu_x + 4, item_y, menu_w - 8, 22, 4, COLOR_MENU_HIGHLIGHT);
                }
                bb_draw_string(menu_x + 16, item_y + 4, items[i].label, COLOR_WHITE, COLOR_MENU_HIGHLIGHT);
            } else {
                bb_draw_string(menu_x + 16, item_y + 4, items[i].label, COLOR_MENU_TEXT, 0x00F8F8F8);
            }
        }
        y += 24;
    }
}

static void draw_menu_bar(void) {
    // Background
    if (classic_mode) {
        bb_fill_rect(0, 0, SCREEN_WIDTH, MENU_BAR_HEIGHT, COLOR_MENU_BG);
    } else {
        bb_fill_rect_alpha(0, 0, SCREEN_WIDTH, MENU_BAR_HEIGHT, COLOR_MENU_BG, 220);
    }

    // Subtle bottom shadow line
    bb_draw_hline(0, MENU_BAR_HEIGHT - 1, SCREEN_WIDTH, 0x00CCCCCC);

    int text_y = (MENU_BAR_HEIGHT - 16) / 2;  // Center text vertically

    // KikiOS logo in menu bar (highlighted if menu open)
    if (open_menu == MENU_APPLE) {
        if (classic_mode) {
            bb_fill_rect(APPLE_MENU_X - 4, 4, APPLE_MENU_W + 8, MENU_BAR_HEIGHT - 8, COLOR_MENU_HIGHLIGHT);
        } else {
            bb_fill_rounded(APPLE_MENU_X - 4, 4, APPLE_MENU_W + 8, MENU_BAR_HEIGHT - 8, 4, COLOR_MENU_HIGHLIGHT);
        }
        // Draw inverted logo
        for (int py = 0; py < 16; py++) {
            for (int px = 0; px < 16; px++) {
                if (vibeos_logo[py * 16 + px]) {
                    bb_put_pixel(APPLE_MENU_X + px, text_y + py, COLOR_WHITE);
                }
            }
        }
    } else {
        for (int py = 0; py < 16; py++) {
            for (int px = 0; px < 16; px++) {
                if (vibeos_logo[py * 16 + px]) {
                    bb_put_pixel(APPLE_MENU_X + px, text_y + py, COLOR_MENU_TEXT);
                }
            }
        }
    }

    // File menu
    if (open_menu == MENU_FILE) {
        if (classic_mode) {
            bb_fill_rect(FILE_MENU_X - 6, 4, FILE_MENU_W + 12, MENU_BAR_HEIGHT - 8, COLOR_MENU_HIGHLIGHT);
        } else {
            bb_fill_rounded(FILE_MENU_X - 6, 4, FILE_MENU_W + 12, MENU_BAR_HEIGHT - 8, 4, COLOR_MENU_HIGHLIGHT);
        }
        bb_draw_string(FILE_MENU_X, text_y, "File", COLOR_WHITE, COLOR_MENU_HIGHLIGHT);
    } else {
        bb_draw_string(FILE_MENU_X, text_y, "File", COLOR_MENU_TEXT, COLOR_MENU_BG);
    }

    // Edit menu
    if (open_menu == MENU_EDIT) {
        if (classic_mode) {
            bb_fill_rect(EDIT_MENU_X - 6, 4, EDIT_MENU_W + 12, MENU_BAR_HEIGHT - 8, COLOR_MENU_HIGHLIGHT);
        } else {
            bb_fill_rounded(EDIT_MENU_X - 6, 4, EDIT_MENU_W + 12, MENU_BAR_HEIGHT - 8, 4, COLOR_MENU_HIGHLIGHT);
        }
        bb_draw_string(EDIT_MENU_X, text_y, "Edit", COLOR_WHITE, COLOR_MENU_HIGHLIGHT);
    } else {
        bb_draw_string(EDIT_MENU_X, text_y, "Edit", COLOR_MENU_TEXT, COLOR_MENU_BG);
    }

    // Settings menu
    if (open_menu == MENU_SETTINGS) {
        if (classic_mode) {
            bb_fill_rect(SETTINGS_MENU_X - 6, 4, SETTINGS_MENU_W + 12, MENU_BAR_HEIGHT - 8, COLOR_MENU_HIGHLIGHT);
        } else {
            bb_fill_rounded(SETTINGS_MENU_X - 6, 4, SETTINGS_MENU_W + 12, MENU_BAR_HEIGHT - 8, 4, COLOR_MENU_HIGHLIGHT);
        }
        bb_draw_string(SETTINGS_MENU_X, text_y, "Settings", COLOR_WHITE, COLOR_MENU_HIGHLIGHT);
    } else {
        bb_draw_string(SETTINGS_MENU_X, text_y, "Settings", COLOR_MENU_TEXT, COLOR_MENU_BG);
    }

    // Date and time on right side (using cached strings, update every 100 ticks = 1 sec)
    unsigned long now = api->get_uptime_ticks();
    if (now - last_datetime_update >= 100 || last_datetime_update == 0) {
        update_datetime_cache();
        last_datetime_update = now;
    }

    // Draw date then time: "Mon Dec 8  12:00"
    int date_len = strlen(cached_date);
    int time_x = SCREEN_WIDTH - 56;  // Time on far right
    int date_x = time_x - (date_len * 8) - 16;  // Date with gap

    bb_draw_string(date_x, text_y, cached_date, COLOR_MENU_TEXT, COLOR_MENU_BG);
    bb_draw_string(time_x, text_y, cached_time, COLOR_MENU_TEXT, COLOR_MENU_BG);
}

static void draw_open_menu(void) {
    if (open_menu == MENU_APPLE) {
        draw_dropdown_menu(APPLE_MENU_X - 2, apple_menu);
    } else if (open_menu == MENU_FILE) {
        draw_dropdown_menu(FILE_MENU_X - 4, file_menu);
    } else if (open_menu == MENU_EDIT) {
        draw_dropdown_menu(EDIT_MENU_X - 4, edit_menu);
    } else if (open_menu == MENU_SETTINGS) {
        draw_dropdown_menu(SETTINGS_MENU_X - 4, settings_menu);
    }
}

// ============ Window Drawing ============

// Precomputed corner insets for CORNER_RADIUS (10)
// corner_insets[py] = how many pixels to inset from edge at row py
static const int corner_insets[CORNER_RADIUS] = {5, 3, 2, 2, 1, 1, 1, 0, 0, 0};

// Precomputed half-widths for traffic light button circles (r=6)
// For each row from -6 to +6, the half-width of the circle
static const int circle_r6_half[13] = {0, 3, 5, 5, 6, 6, 6, 6, 6, 5, 5, 3, 0};

// Draw a filled circle using horizontal spans (optimized)
static void draw_circle_filled(int cx, int cy, int r, uint32_t color) {
    if (r == 6) {
        // Fast path for traffic light buttons (most common)
        for (int dy = -6; dy <= 6; dy++) {
            int half = circle_r6_half[dy + 6];
            if (half > 0) {
                bb_draw_hline(cx - half, cy + dy, half * 2 + 1, color);
            }
        }
    } else {
        // General case with horizontal spans
        int r2 = r * r;
        for (int dy = -r; dy <= r; dy++) {
            int dy2 = dy * dy;
            // Find half-width using integer math
            int half = 0;
            while ((half + 1) * (half + 1) + dy2 <= r2) half++;
            if (half >= 0) {
                bb_draw_hline(cx - half, cy + dy, half * 2 + 1, color);
            }
        }
    }
}

static void draw_window(int wid) {
    if (wid < 0 || !windows[wid].active) return;
    window_t *w = &windows[wid];

    // Don't draw minimized windows
    if (w->minimized) return;

    int is_focused = (wid == focused_window);

    if (classic_mode) {
        // Classic flat mode: simple rectangles, no shadow
        bb_fill_rect(w->x, w->y, w->w, w->h, COLOR_WIN_BG);
        bb_draw_rect(w->x, w->y, w->w, w->h, COLOR_WIN_BORDER);

        // Solid title bar (no gradient)
        uint32_t title_color = is_focused ? 0x00DDDDDD : 0x00E8E8E8;
        bb_fill_rect(w->x + 1, w->y + 1, w->w - 2, TITLE_BAR_HEIGHT - 1, title_color);
    } else {
        // Fancy mode: shadow + rounded corners + gradient title bar
        bb_box_shadow_rounded(w->x, w->y, w->w, w->h, CORNER_RADIUS,
                              SHADOW_BLUR, SHADOW_OFFSET, SHADOW_OFFSET, COLOR_SHADOW);
        bb_fill_rounded(w->x, w->y, w->w, w->h, CORNER_RADIUS, COLOR_WIN_BG);
        bb_draw_rounded(w->x, w->y, w->w, w->h, CORNER_RADIUS, COLOR_WIN_BORDER);

        // Title bar gradient (using precomputed corner insets)
        uint32_t title_top = is_focused ? 0x00E8E8E8 : 0x00F5F5F5;
        uint32_t title_bot = is_focused ? 0x00D0D0D0 : 0x00E8E8E8;

        for (int py = 0; py < TITLE_BAR_HEIGHT; py++) {
            uint8_t t = (py * 255) / (TITLE_BAR_HEIGHT > 1 ? TITLE_BAR_HEIGHT - 1 : 1);
            uint32_t color = gfx_lerp_color(title_top, title_bot, t);

            int start_x = w->x;
            int end_x = w->x + w->w;

            if (py < CORNER_RADIUS) {
                int inset = corner_insets[py];
                start_x += inset;
                end_x -= inset;
            }

            bb_draw_hline(start_x, w->y + py, end_x - start_x, color);
        }
    }

    // Title bar solid color for text background (used in classic mode)
    uint32_t title_bg = is_focused ? 0x00DDDDDD : 0x00E8E8E8;

    // Separator line below title bar
    bb_draw_hline(w->x, w->y + TITLE_BAR_HEIGHT, w->w, 0x00BBBBBB);

    // Traffic light buttons (close, minimize, zoom)
    int btn_y = w->y + TITLE_BAR_HEIGHT / 2;
    int btn_r = 6;
    int btn_spacing = 20;
    int btn_start_x = w->x + 14;

    if (is_focused) {
        // Red close button
        draw_circle_filled(btn_start_x, btn_y, btn_r, COLOR_BTN_CLOSE);
        // Yellow minimize button
        draw_circle_filled(btn_start_x + btn_spacing, btn_y, btn_r, COLOR_BTN_MINIMIZE);
        // Green zoom button
        draw_circle_filled(btn_start_x + btn_spacing * 2, btn_y, btn_r, COLOR_BTN_ZOOM);
    } else {
        // Gray inactive buttons
        draw_circle_filled(btn_start_x, btn_y, btn_r, COLOR_BTN_INACTIVE);
        draw_circle_filled(btn_start_x + btn_spacing, btn_y, btn_r, COLOR_BTN_INACTIVE);
        draw_circle_filled(btn_start_x + btn_spacing * 2, btn_y, btn_r, COLOR_BTN_INACTIVE);
    }

    // Title text (centered)
    int title_len = strlen(w->title);
    int title_x = w->x + (w->w - title_len * 8) / 2;
    int title_y = w->y + (TITLE_BAR_HEIGHT - 16) / 2;
    bb_draw_string(title_x, title_y, w->title, COLOR_TITLE_TEXT, title_bg);

    // Content area - copy from window buffer
    int content_y = w->y + TITLE_BAR_HEIGHT + 1;
    int content_h = w->h - TITLE_BAR_HEIGHT - (classic_mode ? 1 : CORNER_RADIUS) - 1;
    int content_w = w->w - 2;
    int content_x = w->x + 1;

    if (content_h < 1) content_h = 1;
    if (content_w < 1) content_w = 1;

    // Check if window is fully on screen (no clipping needed)
    int fully_visible = (content_x >= 0) &&
                        (content_x + content_w <= SCREEN_WIDTH) &&
                        (content_y >= 0) &&
                        (content_y + content_h <= SCREEN_HEIGHT);

    if (fully_visible && api->dma_available && api->dma_available() && content_w > 0 && content_h > 0) {
        // Use DMA 2D copy for fast rectangular blit
        uint32_t *dst = &backbuffer[content_y * SCREEN_WIDTH + content_x];
        uint32_t dst_pitch = SCREEN_WIDTH * sizeof(uint32_t);
        uint32_t *src = w->buffer;
        uint32_t src_pitch = w->w * sizeof(uint32_t);
        uint32_t copy_bytes = content_w * sizeof(uint32_t);

        api->dma_copy_2d(dst, dst_pitch, src, src_pitch, copy_bytes, content_h);
    } else {
        // Fallback: row-wise copy with clipping
        for (int py = 0; py < content_h; py++) {
            int screen_y = content_y + py;
            if (screen_y >= SCREEN_HEIGHT) break;

            int dst_offset = screen_y * SCREEN_WIDTH + content_x;
            int src_offset = py * w->w;

            // Clip width to screen bounds
            int copy_w = content_w;
            if (content_x + copy_w > SCREEN_WIDTH) {
                copy_w = SCREEN_WIDTH - content_x;
            }
            if (copy_w > 0) {
                // Use 64-bit copy for entire row
                memcpy64(&backbuffer[dst_offset], &w->buffer[src_offset], copy_w * sizeof(uint32_t));
            }
        }
    }

    // Resize handle (bottom-right corner) - subtle dots (optimized with fill_rect)
    int rh_x = w->x + w->w - 14;
    int rh_y = w->y + w->h - 14;
    uint32_t dot_color = 0x00999999;
    for (int row = 0; row < 3; row++) {
        for (int col = row; col < 3; col++) {
            int dx = (2 - col) * 4 + 2;
            int dy = row * 4 + 2;
            bb_fill_rect(rh_x + dx, rh_y + dy, 2, 2, dot_color);
        }
    }
}

// ============ Cursor ============

// Shared cursor bitmap (1 = black outline, 2 = white fill, 0 = transparent)
static const uint8_t cursor_bits[16 * 16] = {
    1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,2,2,1,0,0,0,0,0,0,0,0,0,0,0,0,
    1,2,2,2,1,0,0,0,0,0,0,0,0,0,0,0,
    1,2,2,2,2,1,0,0,0,0,0,0,0,0,0,0,
    1,2,2,2,2,2,1,0,0,0,0,0,0,0,0,0,
    1,2,2,2,2,2,2,1,0,0,0,0,0,0,0,0,
    1,2,2,2,2,2,2,2,1,0,0,0,0,0,0,0,
    1,2,2,2,2,2,2,2,2,1,0,0,0,0,0,0,
    1,2,2,2,2,2,1,1,1,1,1,0,0,0,0,0,
    1,2,2,1,2,2,1,0,0,0,0,0,0,0,0,0,
    1,2,1,1,2,2,1,0,0,0,0,0,0,0,0,0,
    1,1,0,0,1,2,2,1,0,0,0,0,0,0,0,0,
    1,0,0,0,0,1,2,2,1,0,0,0,0,0,0,0,
    0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,
};

// Save the background under cursor position from a buffer
static void save_cursor_bg(uint32_t *buffer, int x, int y) {
    for (int py = 0; py < 16; py++) {
        for (int px = 0; px < 16; px++) {
            int sx = x + px, sy = y + py;
            if (sx >= 0 && sx < SCREEN_WIDTH && sy >= 0 && sy < SCREEN_HEIGHT) {
                cursor_save[py * 16 + px] = buffer[sy * SCREEN_WIDTH + sx];
            } else {
                cursor_save[py * 16 + px] = COLOR_BLACK;
            }
        }
    }
    cursor_save_x = x;
    cursor_save_y = y;
    cursor_save_valid = 1;
}

// Restore cursor background to a buffer
static void restore_cursor_bg(uint32_t *buffer) {
    if (!cursor_save_valid) return;
    for (int py = 0; py < 16; py++) {
        for (int px = 0; px < 16; px++) {
            int sx = cursor_save_x + px, sy = cursor_save_y + py;
            if (sx >= 0 && sx < SCREEN_WIDTH && sy >= 0 && sy < SCREEN_HEIGHT) {
                buffer[sy * SCREEN_WIDTH + sx] = cursor_save[py * 16 + px];
            }
        }
    }
}

// Draw cursor to a specific buffer
static void draw_cursor_to_buffer(uint32_t *buffer, int x, int y) {
    for (int py = 0; py < 16; py++) {
        for (int px = 0; px < 16; px++) {
            uint8_t c = cursor_bits[py * 16 + px];
            if (c != 0) {
                int sx = x + px;
                int sy = y + py;
                if (sx >= 0 && sx < SCREEN_WIDTH && sy >= 0 && sy < SCREEN_HEIGHT) {
                    uint32_t color = (c == 1) ? COLOR_BLACK : COLOR_WHITE;
                    buffer[sy * SCREEN_WIDTH + sx] = color;
                }
            }
        }
    }
}

// Get pointer to the currently visible buffer
static uint32_t *get_visible_buffer(void) {
    if (use_hw_double_buffer) {
        // After flip_buffer(), current_buffer was toggled and now points to the BACKBUFFER
        // So the VISIBLE buffer is the opposite of current_buffer
        // If current_buffer == 0, visible = buffer 1 (bottom half)
        // If current_buffer == 1, visible = buffer 0 (top half)
        return api->fb_base + (current_buffer ? 0 : SCREEN_WIDTH * SCREEN_HEIGHT);
    } else {
        return api->fb_base;
    }
}

// Update cursor position on the visible buffer (for cursor-only updates)
static void update_cursor_only(int old_x, int old_y, int new_x, int new_y) {
    uint32_t *visible = get_visible_buffer();
    (void)old_x; (void)old_y;  // We use cursor_save_x/y instead

    // Restore old cursor background
    restore_cursor_bg(visible);

    // Save new cursor background
    save_cursor_bg(visible, new_x, new_y);

    // Draw cursor at new position
    draw_cursor_to_buffer(visible, new_x, new_y);
}

static void draw_cursor(int x, int y) {
    for (int py = 0; py < 16; py++) {
        for (int px = 0; px < 16; px++) {
            uint8_t c = cursor_bits[py * 16 + px];
            if (c != 0) {
                int sx = x + px;
                int sy = y + py;
                if (sx >= 0 && sx < SCREEN_WIDTH && sy >= 0 && sy < SCREEN_HEIGHT) {
                    uint32_t color = (c == 1) ? COLOR_BLACK : COLOR_WHITE;
                    backbuffer[sy * SCREEN_WIDTH + sx] = color;
                }
            }
        }
    }
}

// ============ Main Drawing ============

static void draw_desktop(void) {
    // Desktop background based on wallpaper_style
    switch (wallpaper_style) {
        case 0:  // Solid color
            bb_fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_DESKTOP);
            break;
        case 1:  // Gradient
            if (dark_theme) {
                bb_gradient_v(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0x00303050, 0x00101020);
            } else {
                bb_gradient_v(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0x00B0D4F1, 0x00E8F4FC);
            }
            break;
        case 2:  // Pattern
            bb_fill_pattern(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
            break;
        default:
            bb_fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_DESKTOP);
    }

    // Menu bar (drawn on top of gradient for translucency effect)
    draw_menu_bar();

    // Windows (back to front)
    for (int i = window_count - 1; i >= 0; i--) {
        draw_window(window_order[i]);
    }

    // Dock
    draw_dock();

    // Dock context menu
    if (dock_context_menu_visible) {
        draw_dock_context_menu();
    }

    // Open menu dropdown (draw last, on top of everything)
    if (open_menu != MENU_NONE) {
        draw_open_menu();
    }

    // About dialog (on top of everything)
    if (show_about_dialog) {
        draw_about_dialog();
    }

    // Settings dialog (on top of everything)
    if (show_settings_dialog) {
        draw_settings_dialog();
    }
}

static void flip_buffer(void) {
    if (use_hw_double_buffer) {
        // Hardware flip - instant, zero-copy!
        // We've been drawing to the backbuffer (non-visible half)
        // Now make it visible by switching scroll offset
        api->fb_flip(current_buffer);
        current_buffer = !current_buffer;
        // Update backbuffer pointer to the now-hidden buffer
        backbuffer = api->fb_get_backbuffer();
        gfx.buffer = backbuffer;
    } else if (api->dma_available && api->dma_available()) {
        // DMA copy - hardware accelerated, frees CPU (Pi)
        api->dma_fb_copy(api->fb_base, backbuffer, SCREEN_WIDTH, SCREEN_HEIGHT);
    } else {
        // Software copy (QEMU fallback) - use fast 64-bit copy
        memcpy64(api->fb_base, backbuffer, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32_t));
    }
}

// ============ Input Handling ============

#define ABOUT_W 320
#define ABOUT_H 220
#define ABOUT_X ((SCREEN_WIDTH - ABOUT_W) / 2)
#define ABOUT_Y ((SCREEN_HEIGHT - ABOUT_H) / 2 - 40)

static void draw_about_dialog(void) {
    int x = ABOUT_X;
    int y = ABOUT_Y;

    if (classic_mode) {
        // Classic flat mode
        bb_fill_rect(x, y, ABOUT_W, ABOUT_H, 0x00FAFAFA);
        bb_draw_rect(x, y, ABOUT_W, ABOUT_H, COLOR_WIN_BORDER);
    } else {
        // Fancy mode
        int r = 12;
        bb_box_shadow_rounded(x, y, ABOUT_W, ABOUT_H, r, 4, 2, 2, COLOR_SHADOW);
        bb_fill_rounded_alpha(x, y, ABOUT_W, ABOUT_H, r, 0x00FAFAFA, 250);
    }

    // Draw a big KikiOS logo in the dialog (3x size) - optimized with fill_rect
    int logo_x = x + (ABOUT_W - 48) / 2;  // 16*3 = 48
    int logo_y = y + 20;
    for (int py = 0; py < 16; py++) {
        for (int px = 0; px < 16; px++) {
            if (vibeos_logo[py * 16 + px]) {
                bb_fill_rect(logo_x + px*3, logo_y + py*3, 3, 3, COLOR_MENU_TEXT);
            }
        }
    }

    // Title
    const char *title = "KikiOS";
    int title_x = x + (ABOUT_W - strlen(title) * 8) / 2;
    bb_draw_string(title_x, y + 74, title, COLOR_MENU_TEXT, 0x00FAFAFA);

    // Version
    const char *version = "Version 1.0";
    int ver_x = x + (ABOUT_W - strlen(version) * 8) / 2;
    bb_draw_string(ver_x, y + 94, version, 0x00666666, 0x00FAFAFA);

    // Separator line
    bb_draw_hline(x + 30, y + 116, ABOUT_W - 60, 0x00DDDDDD);

    // System info
    // Memory
    unsigned long mem_used = api->get_mem_used() / 1024;  // KB
    unsigned long mem_free = api->get_mem_free() / 1024;  // KB
    unsigned long mem_total = mem_used + mem_free;

    char mem_str[40];
    char *p = mem_str;
    const char *m1 = "Memory: ";
    while (*m1) *p++ = *m1++;

    char num[12];
    int ni = 0;
    unsigned long n = mem_used;
    if (n == 0) num[ni++] = '0';
    else { while (n > 0) { num[ni++] = '0' + (n % 10); n /= 10; } }
    while (ni > 0) *p++ = num[--ni];

    const char *m2 = " / ";
    while (*m2) *p++ = *m2++;

    n = mem_total;
    ni = 0;
    if (n == 0) num[ni++] = '0';
    else { while (n > 0) { num[ni++] = '0' + (n % 10); n /= 10; } }
    while (ni > 0) *p++ = num[--ni];

    const char *m3 = " KB";
    while (*m3) *p++ = *m3++;
    *p = '\0';

    int mem_x = x + (ABOUT_W - strlen(mem_str) * 8) / 2;
    bb_draw_string(mem_x, y + 130, mem_str, 0x00555555, 0x00FAFAFA);

    // Uptime
    unsigned long ticks = api->get_uptime_ticks();
    unsigned long secs = ticks / 100;
    unsigned long mins = secs / 60;
    unsigned long hours = mins / 60;
    mins = mins % 60;
    secs = secs % 60;

    char up_str[32];
    p = up_str;
    const char *u1 = "Uptime: ";
    while (*u1) *p++ = *u1++;

    n = hours;
    ni = 0;
    if (n == 0) num[ni++] = '0';
    else { while (n > 0) { num[ni++] = '0' + (n % 10); n /= 10; } }
    while (ni > 0) *p++ = num[--ni];
    *p++ = ':';

    *p++ = '0' + (mins / 10);
    *p++ = '0' + (mins % 10);
    *p++ = ':';

    *p++ = '0' + (secs / 10);
    *p++ = '0' + (secs % 10);
    *p = '\0';

    int up_x = x + (ABOUT_W - strlen(up_str) * 8) / 2;
    bb_draw_string(up_x, y + 150, up_str, 0x00555555, 0x00FAFAFA);

    // OK button
    int btn_w = 80;
    int btn_h = 28;
    int btn_x = x + (ABOUT_W - btn_w) / 2;
    int btn_y = y + ABOUT_H - 45;

    int hovering = (mouse_x >= btn_x && mouse_x < btn_x + btn_w &&
                   mouse_y >= btn_y && mouse_y < btn_y + btn_h);

    uint32_t btn_bg = hovering ? 0x00CCCCCC : 0x00E0E0E0;
    if (classic_mode) {
        bb_fill_rect(btn_x, btn_y, btn_w, btn_h, btn_bg);
        bb_draw_rect(btn_x, btn_y, btn_w, btn_h, 0x00AAAAAA);
    } else {
        int btn_r = 6;
        bb_fill_rounded(btn_x, btn_y, btn_w, btn_h, btn_r, btn_bg);
        bb_draw_rounded(btn_x, btn_y, btn_w, btn_h, btn_r, 0x00AAAAAA);
    }
    int ok_x = btn_x + (btn_w - 16) / 2;
    bb_draw_string(ok_x, btn_y + 6, "OK", COLOR_MENU_TEXT, btn_bg);
}

// ============ Settings Dialog ============

#define SETTINGS_W 360
#define SETTINGS_H 280
#define SETTINGS_X ((SCREEN_WIDTH - SETTINGS_W) / 2)
#define SETTINGS_Y ((SCREEN_HEIGHT - SETTINGS_H) / 2 - 20)

static void draw_settings_dialog(void) {
    int x = SETTINGS_X;
    int y = SETTINGS_Y;
    uint32_t dialog_bg = dark_theme ? 0x002D2D2D : 0x00FAFAFA;
    uint32_t text_color = dark_theme ? 0x00FFFFFF : 0x00333333;
    uint32_t section_color = dark_theme ? 0x00888888 : 0x00666666;

    if (classic_mode) {
        bb_fill_rect(x, y, SETTINGS_W, SETTINGS_H, dialog_bg);
        bb_draw_rect(x, y, SETTINGS_W, SETTINGS_H, COLOR_WIN_BORDER);
    } else {
        int r = 12;
        bb_box_shadow_rounded(x, y, SETTINGS_W, SETTINGS_H, r, 4, 2, 2, COLOR_SHADOW);
        bb_fill_rounded_alpha(x, y, SETTINGS_W, SETTINGS_H, r, dialog_bg, 250);
    }

    // Title
    const char *title = "Settings";
    int title_x = x + (SETTINGS_W - strlen(title) * 8) / 2;
    bb_draw_string(title_x, y + 16, title, text_color, dialog_bg);

    // Separator
    bb_draw_hline(x + 20, y + 40, SETTINGS_W - 40, dark_theme ? 0x00505050 : 0x00DDDDDD);

    // Theme section
    bb_draw_string(x + 20, y + 55, "Theme", section_color, dialog_bg);

    int btn_w = 100;
    int btn_h = 30;
    int btn_y_theme = y + 75;

    // Light theme button
    int light_x = x + 40;
    int light_hover = (mouse_x >= light_x && mouse_x < light_x + btn_w &&
                      mouse_y >= btn_y_theme && mouse_y < btn_y_theme + btn_h);
    uint32_t light_bg = dark_theme == 0 ? color_accent : (light_hover ? 0x00DDDDDD : 0x00E8E8E8);
    uint32_t light_fg = dark_theme == 0 ? COLOR_WHITE : text_color;
    if (classic_mode) {
        bb_fill_rect(light_x, btn_y_theme, btn_w, btn_h, light_bg);
        bb_draw_rect(light_x, btn_y_theme, btn_w, btn_h, COLOR_WIN_BORDER);
    } else {
        bb_fill_rounded(light_x, btn_y_theme, btn_w, btn_h, 6, light_bg);
    }
    bb_draw_string(light_x + 30, btn_y_theme + 7, "Light", light_fg, light_bg);

    // Dark theme button
    int dark_x = x + 160;
    int dark_hover = (mouse_x >= dark_x && mouse_x < dark_x + btn_w &&
                     mouse_y >= btn_y_theme && mouse_y < btn_y_theme + btn_h);
    uint32_t dark_bg = dark_theme == 1 ? color_accent : (dark_hover ? 0x00DDDDDD : 0x00E8E8E8);
    uint32_t dark_fg = dark_theme == 1 ? COLOR_WHITE : text_color;
    if (classic_mode) {
        bb_fill_rect(dark_x, btn_y_theme, btn_w, btn_h, dark_bg);
        bb_draw_rect(dark_x, btn_y_theme, btn_w, btn_h, COLOR_WIN_BORDER);
    } else {
        bb_fill_rounded(dark_x, btn_y_theme, btn_w, btn_h, 6, dark_bg);
    }
    bb_draw_string(dark_x + 34, btn_y_theme + 7, "Dark", dark_fg, dark_bg);

    // Separator
    bb_draw_hline(x + 20, y + 120, SETTINGS_W - 40, dark_theme ? 0x00505050 : 0x00DDDDDD);

    // Wallpaper section
    bb_draw_string(x + 20, y + 135, "Wallpaper", section_color, dialog_bg);

    int btn_y_wall = y + 155;

    // Solid button
    int solid_x = x + 30;
    int solid_hover = (mouse_x >= solid_x && mouse_x < solid_x + btn_w &&
                      mouse_y >= btn_y_wall && mouse_y < btn_y_wall + btn_h);
    uint32_t solid_bg = wallpaper_style == 0 ? color_accent : (solid_hover ? 0x00DDDDDD : 0x00E8E8E8);
    uint32_t solid_fg = wallpaper_style == 0 ? COLOR_WHITE : text_color;
    if (classic_mode) {
        bb_fill_rect(solid_x, btn_y_wall, btn_w, btn_h, solid_bg);
        bb_draw_rect(solid_x, btn_y_wall, btn_w, btn_h, COLOR_WIN_BORDER);
    } else {
        bb_fill_rounded(solid_x, btn_y_wall, btn_w, btn_h, 6, solid_bg);
    }
    bb_draw_string(solid_x + 30, btn_y_wall + 7, "Solid", solid_fg, solid_bg);

    // Gradient button
    int grad_x = x + 135;
    int grad_hover = (mouse_x >= grad_x && mouse_x < grad_x + btn_w &&
                     mouse_y >= btn_y_wall && mouse_y < btn_y_wall + btn_h);
    uint32_t grad_bg = wallpaper_style == 1 ? color_accent : (grad_hover ? 0x00DDDDDD : 0x00E8E8E8);
    uint32_t grad_fg = wallpaper_style == 1 ? COLOR_WHITE : text_color;
    if (classic_mode) {
        bb_fill_rect(grad_x, btn_y_wall, btn_w, btn_h, grad_bg);
        bb_draw_rect(grad_x, btn_y_wall, btn_w, btn_h, COLOR_WIN_BORDER);
    } else {
        bb_fill_rounded(grad_x, btn_y_wall, btn_w, btn_h, 6, grad_bg);
    }
    bb_draw_string(grad_x + 18, btn_y_wall + 7, "Gradient", grad_fg, grad_bg);

    // Pattern button
    int patt_x = x + 240;
    int patt_hover = (mouse_x >= patt_x && mouse_x < patt_x + btn_w &&
                     mouse_y >= btn_y_wall && mouse_y < btn_y_wall + btn_h);
    uint32_t patt_bg = wallpaper_style == 2 ? color_accent : (patt_hover ? 0x00DDDDDD : 0x00E8E8E8);
    uint32_t patt_fg = wallpaper_style == 2 ? COLOR_WHITE : text_color;
    if (classic_mode) {
        bb_fill_rect(patt_x, btn_y_wall, btn_w, btn_h, patt_bg);
        bb_draw_rect(patt_x, btn_y_wall, btn_w, btn_h, COLOR_WIN_BORDER);
    } else {
        bb_fill_rounded(patt_x, btn_y_wall, btn_w, btn_h, 6, patt_bg);
    }
    bb_draw_string(patt_x + 22, btn_y_wall + 7, "Pattern", patt_fg, patt_bg);

    // Separator
    bb_draw_hline(x + 20, y + 200, SETTINGS_W - 40, dark_theme ? 0x00505050 : 0x00DDDDDD);

    // Close button
    int close_w = 80;
    int close_h = 30;
    int close_x = x + (SETTINGS_W - close_w) / 2;
    int close_y = y + SETTINGS_H - 50;

    int close_hover = (mouse_x >= close_x && mouse_x < close_x + close_w &&
                      mouse_y >= close_y && mouse_y < close_y + close_h);
    uint32_t close_bg = close_hover ? color_accent : (dark_theme ? 0x00404040 : 0x00E0E0E0);
    uint32_t close_fg = close_hover ? COLOR_WHITE : text_color;
    if (classic_mode) {
        bb_fill_rect(close_x, close_y, close_w, close_h, close_bg);
        bb_draw_rect(close_x, close_y, close_w, close_h, COLOR_WIN_BORDER);
    } else {
        bb_fill_rounded(close_x, close_y, close_w, close_h, 6, close_bg);
    }
    bb_draw_string(close_x + 22, close_y + 7, "Close", close_fg, close_bg);
}

// Apply theme colors based on dark_theme setting
static void apply_theme(void);

// Execute a menu action
static void do_menu_action(int action) {
    switch (action) {
        case ACTION_ABOUT:
            show_about_dialog = 1;
            request_redraw();
            break;
        case ACTION_QUIT:
            running = 0;
            break;
        case ACTION_NEW_WINDOW:
            api->spawn("/bin/term");
            break;
        case ACTION_CLOSE_WINDOW:
            if (focused_window >= 0) {
                push_event(focused_window, WIN_EVENT_CLOSE, 0, 0, 0);
            }
            break;
        case ACTION_CUT:
        case ACTION_COPY:
        case ACTION_PASTE:
            // TODO: Clipboard operations
            break;
        case ACTION_THEME_LIGHT:
            dark_theme = 0;
            apply_theme();
            request_redraw();
            break;
        case ACTION_THEME_DARK:
            dark_theme = 1;
            apply_theme();
            request_redraw();
            break;
        case ACTION_WALLPAPER_SOLID:
            wallpaper_style = 0;
            request_redraw();
            break;
        case ACTION_WALLPAPER_GRADIENT:
            wallpaper_style = 1;
            request_redraw();
            break;
        case ACTION_WALLPAPER_PATTERN:
            wallpaper_style = 2;
            request_redraw();
            break;
    }
}

// Check if click is on a menu item and return its action
static int get_menu_item_action(int menu_x, const menu_item_t *items, int click_x, int click_y) {
    // Calculate menu dimensions
    int max_width = 0;
    int item_count = 0;
    for (int i = 0; items[i].action != -1; i++) {
        if (items[i].label) {
            int len = strlen(items[i].label);
            if (len > max_width) max_width = len;
        }
        item_count++;
    }

    int menu_w = max_width * 8 + 32;
    int menu_y = MENU_BAR_HEIGHT + 4;

    // Check if click is within menu bounds
    if (click_x < menu_x || click_x >= menu_x + menu_w) return ACTION_NONE;
    if (click_y < menu_y) return ACTION_NONE;

    // Find which item was clicked (24px per item)
    int y = menu_y + 4;
    for (int i = 0; items[i].action != -1; i++) {
        if (click_y >= y && click_y < y + 24) {
            if (items[i].label != NULL) {
                return items[i].action;
            }
            return ACTION_NONE;  // Clicked on separator
        }
        y += 24;
    }

    return ACTION_NONE;
}

static void handle_mouse_click(int x, int y, uint8_t buttons) {
    // Handle About dialog (modal - blocks everything else)
    if (show_about_dialog && (buttons & MOUSE_BTN_LEFT)) {
        // Check OK button
        int btn_w = 80;
        int btn_h = 28;
        int btn_x = ABOUT_X + (ABOUT_W - btn_w) / 2;
        int btn_y = ABOUT_Y + ABOUT_H - 45;

        if (x >= btn_x && x < btn_x + btn_w &&
            y >= btn_y && y < btn_y + btn_h) {
            show_about_dialog = 0;
            request_redraw();
            return;
        }
        // Click outside dialog dismisses it
        if (x < ABOUT_X || x >= ABOUT_X + ABOUT_W ||
            y < ABOUT_Y || y >= ABOUT_Y + ABOUT_H) {
            show_about_dialog = 0;
            request_redraw();
        }
        return;  // Modal - don't process other clicks
    }

    // Handle Settings dialog (modal)
    if (show_settings_dialog && (buttons & MOUSE_BTN_LEFT)) {
        int sx = SETTINGS_X;
        int sy = SETTINGS_Y;
        int btn_w = 100;
        int btn_h = 30;

        // Theme buttons
        int btn_y_theme = sy + 75;
        int light_x = sx + 40;
        int dark_x = sx + 160;

        // Light theme button
        if (x >= light_x && x < light_x + btn_w &&
            y >= btn_y_theme && y < btn_y_theme + btn_h) {
            dark_theme = 0;
            apply_theme();
            request_redraw();
            return;
        }
        // Dark theme button
        if (x >= dark_x && x < dark_x + btn_w &&
            y >= btn_y_theme && y < btn_y_theme + btn_h) {
            dark_theme = 1;
            apply_theme();
            request_redraw();
            return;
        }

        // Wallpaper buttons
        int btn_y_wall = sy + 155;
        int solid_x = sx + 30;
        int grad_x = sx + 135;
        int patt_x = sx + 240;

        if (x >= solid_x && x < solid_x + btn_w &&
            y >= btn_y_wall && y < btn_y_wall + btn_h) {
            wallpaper_style = 0;
            request_redraw();
            return;
        }
        if (x >= grad_x && x < grad_x + btn_w &&
            y >= btn_y_wall && y < btn_y_wall + btn_h) {
            wallpaper_style = 1;
            request_redraw();
            return;
        }
        if (x >= patt_x && x < patt_x + btn_w &&
            y >= btn_y_wall && y < btn_y_wall + btn_h) {
            wallpaper_style = 2;
            request_redraw();
            return;
        }

        // Close button
        int close_w = 80;
        int close_h = 30;
        int close_x = sx + (SETTINGS_W - close_w) / 2;
        int close_y = sy + SETTINGS_H - 50;

        if (x >= close_x && x < close_x + close_w &&
            y >= close_y && y < close_y + close_h) {
            show_settings_dialog = 0;
            request_redraw();
            return;
        }

        // Click outside dialog dismisses it
        if (x < SETTINGS_X || x >= SETTINGS_X + SETTINGS_W ||
            y < SETTINGS_Y || y >= SETTINGS_Y + SETTINGS_H) {
            show_settings_dialog = 0;
            request_redraw();
        }
        return;  // Modal - don't process other clicks
    }

    // Handle menu bar clicks (left click only)
    if ((buttons & MOUSE_BTN_LEFT) && y < MENU_BAR_HEIGHT) {
        // Check which menu was clicked
        if (x >= APPLE_MENU_X && x < APPLE_MENU_X + APPLE_MENU_W) {
            open_menu = (open_menu == MENU_APPLE) ? MENU_NONE : MENU_APPLE;
        } else if (x >= FILE_MENU_X && x < FILE_MENU_X + FILE_MENU_W) {
            open_menu = (open_menu == MENU_FILE) ? MENU_NONE : MENU_FILE;
        } else if (x >= EDIT_MENU_X && x < EDIT_MENU_X + EDIT_MENU_W) {
            open_menu = (open_menu == MENU_EDIT) ? MENU_NONE : MENU_EDIT;
        } else if (x >= SETTINGS_MENU_X && x < SETTINGS_MENU_X + SETTINGS_MENU_W) {
            open_menu = (open_menu == MENU_SETTINGS) ? MENU_NONE : MENU_SETTINGS;
        } else {
            open_menu = MENU_NONE;
        }
        request_redraw();
        return;
    }

    // Handle clicks on open menu dropdown
    if ((buttons & MOUSE_BTN_LEFT) && open_menu != MENU_NONE) {
        int action = ACTION_NONE;

        if (open_menu == MENU_APPLE) {
            action = get_menu_item_action(APPLE_MENU_X - 2, apple_menu, x, y);
        } else if (open_menu == MENU_FILE) {
            action = get_menu_item_action(FILE_MENU_X - 4, file_menu, x, y);
        } else if (open_menu == MENU_EDIT) {
            action = get_menu_item_action(EDIT_MENU_X - 4, edit_menu, x, y);
        } else if (open_menu == MENU_SETTINGS) {
            action = get_menu_item_action(SETTINGS_MENU_X - 4, settings_menu, x, y);
        }

        if (action != ACTION_NONE) {
            do_menu_action(action);
        }

        // Close menu after any click outside menu bar
        open_menu = MENU_NONE;
        request_redraw();
        return;
    }

    // Handle dock context menu clicks
    if (dock_context_menu_visible) {
        if (buttons & MOUSE_BTN_LEFT) {
            // Check if clicking on "New Window" item
            int menu_w = 120;
            int menu_h = 32;
            int menu_x = dock_context_menu_x;
            int menu_y = dock_context_menu_y - menu_h - 8;

            // Clamp to screen (same as draw_dock_context_menu)
            if (menu_x + menu_w > SCREEN_WIDTH) menu_x = SCREEN_WIDTH - menu_w;
            if (menu_y < MENU_BAR_HEIGHT) menu_y = dock_context_menu_y + 8;

            int item_y = menu_y + 4;
            if (x >= menu_x + 4 && x < menu_x + menu_w - 4 &&
                y >= item_y && y < item_y + 24 &&
                dock_context_menu_idx >= 0) {
                // Clicked on "New Window" - spawn the app
                dock_icon_t *icon = &dock_icons[dock_context_menu_idx];
                if (icon->is_fullscreen) {
                    api->exec(icon->exec_path);
                } else {
                    api->spawn(icon->exec_path);
                }
            }
            // Dismiss menu on any left click
            dock_context_menu_visible = 0;
            dock_context_menu_idx = -1;
            request_redraw();
            return;
        }
    }

    // Handle right-click on dock icons to show context menu
    if (buttons & MOUSE_BTN_RIGHT) {
        int dock_idx = dock_icon_at_point(x, y);
        if (dock_idx >= 0) {
            dock_context_menu_visible = 1;
            dock_context_menu_x = dock_icons[dock_idx].x;
            dock_context_menu_y = dock_icons[dock_idx].y;
            dock_context_menu_idx = dock_idx;
            request_redraw();
            return;
        }
    }

    // Check dock first (left click only)
    if (buttons & MOUSE_BTN_LEFT) {
        // Check if clicking a minimized window to restore it
        int min_wid = minimized_window_at_point(x, y);
        if (min_wid >= 0) {
            windows[min_wid].minimized = 0;
            minimized_count--;
            bring_to_front(min_wid);
            request_redraw();
            return;
        }

        int dock_idx = dock_icon_at_point(x, y);
        if (dock_idx >= 0) {
            dock_icon_t *icon = &dock_icons[dock_idx];
            // Check for special Settings icon
            if (strcmp(icon->exec_path, "__SETTINGS__") == 0) {
                show_settings_dialog = 1;
                request_redraw();
                return;
            }
            if (icon->is_fullscreen) {
                // Fullscreen app - exec and wait
                api->exec(icon->exec_path);
                // When we return, redraw everything
                request_redraw();
            } else {
                // Windowed app - spawn
                api->spawn(icon->exec_path);
            }
            return;
        }
    }

    // Check windows
    int wid = window_at_point(x, y);
    if (wid >= 0) {
        window_t *w = &windows[wid];
        bring_to_front(wid);

        // Check if click is on title bar (left click only for dragging/close)
        if ((buttons & MOUSE_BTN_LEFT) && y >= w->y && y < w->y + TITLE_BAR_HEIGHT) {
            // Traffic light buttons
            int btn_y = w->y + TITLE_BAR_HEIGHT / 2;
            int btn_r = 6;
            int btn_spacing = 20;
            int btn_start_x = w->x + 14;

            // Check close button (red)
            int dx = x - btn_start_x;
            int dy = y - btn_y;
            if (dx * dx + dy * dy <= btn_r * btn_r) {
                push_event(wid, WIN_EVENT_CLOSE, 0, 0, 0);
                return;
            }

            // Check minimize button (yellow)
            dx = x - (btn_start_x + btn_spacing);
            if (dx * dx + dy * dy <= btn_r * btn_r) {
                // Minimize window
                w->minimized = 1;
                minimized_count++;
                // Update focus to next window
                focused_window = -1;
                for (int i = 0; i < window_count; i++) {
                    int next_wid = window_order[i];
                    if (windows[next_wid].active && !windows[next_wid].minimized) {
                        focused_window = next_wid;
                        break;
                    }
                }
                request_redraw();
                return;
            }

            // Check zoom/maximize button (green)
            dx = x - (btn_start_x + btn_spacing * 2);
            if (dx * dx + dy * dy <= btn_r * btn_r) {
                if (w->maximized) {
                    // Restore to saved position
                    w->x = w->restore_x;
                    w->y = w->restore_y;
                    w->w = w->restore_w;
                    w->h = w->restore_h;
                    w->maximized = 0;
                } else {
                    // Save current position and maximize
                    w->restore_x = w->x;
                    w->restore_y = w->y;
                    w->restore_w = w->w;
                    w->restore_h = w->h;

                    // Maximize to usable area (between menu bar and dock)
                    w->x = 0;
                    w->y = MENU_BAR_HEIGHT;
                    w->w = SCREEN_WIDTH;
                    w->h = SCREEN_HEIGHT - MENU_BAR_HEIGHT - DOCK_HEIGHT;
                    w->maximized = 1;
                }

                // Reallocate buffer for new size
                int content_h = w->h - TITLE_BAR_HEIGHT;
                if (content_h < 1) content_h = 1;
                uint32_t *new_buffer = api->malloc(w->w * content_h * sizeof(uint32_t));
                if (new_buffer) {
                    api->free(w->buffer);
                    w->buffer = new_buffer;
                    if (api->dma_fill) {
                        api->dma_fill(w->buffer, COLOR_WHITE, w->w * content_h * sizeof(uint32_t));
                    } else {
                        for (int i = 0; i < w->w * content_h; i++) {
                            w->buffer[i] = COLOR_WHITE;
                        }
                    }
                    push_event(wid, WIN_EVENT_RESIZE, w->w, w->h, 0);
                }
                request_redraw();
                return;
            }

            // Start dragging (if not on buttons)
            dragging_window = wid;
            drag_offset_x = x - w->x;
            drag_offset_y = y - w->y;
        } else if (y >= w->y + TITLE_BAR_HEIGHT) {
            // Check for resize handle (bottom-right corner, 15x15 area)
            int rh_x = w->x + w->w - 15;
            int rh_y = w->y + w->h - 15;
            if ((buttons & MOUSE_BTN_LEFT) &&
                x >= rh_x && x < w->x + w->w &&
                y >= rh_y && y < w->y + w->h) {
                // Start resizing
                resizing_window = wid;
                resize_start_w = w->w;
                resize_start_h = w->h;
                resize_start_mx = x;
                resize_start_my = y;
                return;
            }
            // Click in content area - send event to app with button info
            int local_x = x - w->x - 1;
            int local_y = y - w->y - TITLE_BAR_HEIGHT - 1;
            push_event(wid, WIN_EVENT_MOUSE_DOWN, local_x, local_y, buttons);
        }
    }
}

static void handle_mouse_release(int x, int y) {
    dragging_window = -1;

    // Handle resize completion
    if (resizing_window >= 0) {
        window_t *w = &windows[resizing_window];

        // Allocate new buffer before freeing old one (in case malloc fails)
        int content_h = w->h - TITLE_BAR_HEIGHT;
        if (content_h < 1) content_h = 1;
        uint32_t *new_buffer = api->malloc(w->w * content_h * sizeof(uint32_t));

        if (new_buffer) {
            // Success - free old buffer and use new one
            api->free(w->buffer);
            w->buffer = new_buffer;

            // Clear new buffer to white (use DMA if available)
            if (api->dma_fill) {
                api->dma_fill(w->buffer, COLOR_WHITE, w->w * content_h * sizeof(uint32_t));
            } else {
                for (int i = 0; i < w->w * content_h; i++) {
                    w->buffer[i] = COLOR_WHITE;
                }
            }

            // Send resize event to app (data1=new width, data2=new height)
            push_event(resizing_window, WIN_EVENT_RESIZE, w->w, w->h, 0);
        }
        // If malloc failed, keep old buffer and size (resize is cancelled)

        resizing_window = -1;
        request_redraw();
        return;
    }

    int wid = window_at_point(x, y);
    if (wid >= 0) {
        window_t *w = &windows[wid];
        if (y >= w->y + TITLE_BAR_HEIGHT) {
            int local_x = x - w->x - 1;
            int local_y = y - w->y - TITLE_BAR_HEIGHT - 1;
            push_event(wid, WIN_EVENT_MOUSE_UP, local_x, local_y, 0);
        }
    }
}

static void handle_mouse_move(int x, int y) {
    if (dragging_window >= 0) {
        window_t *w = &windows[dragging_window];
        w->x = x - drag_offset_x;
        w->y = y - drag_offset_y;

        // Clamp to screen
        if (w->x < 0) w->x = 0;
        if (w->y < MENU_BAR_HEIGHT) w->y = MENU_BAR_HEIGHT;
        if (w->x + w->w > SCREEN_WIDTH) w->x = SCREEN_WIDTH - w->w;
        if (w->y + w->h > SCREEN_HEIGHT - DOCK_HEIGHT)
            w->y = SCREEN_HEIGHT - DOCK_HEIGHT - w->h;
        request_redraw();
        return;  // Don't send move events while dragging
    }

    if (resizing_window >= 0) {
        window_t *w = &windows[resizing_window];

        // Calculate new size based on mouse delta
        int new_w = resize_start_w + (x - resize_start_mx);
        int new_h = resize_start_h + (y - resize_start_my);

        // Enforce minimum size
        if (new_w < 100) new_w = 100;
        if (new_h < 60) new_h = 60;

        // Enforce maximum (screen bounds)
        if (w->x + new_w > SCREEN_WIDTH) new_w = SCREEN_WIDTH - w->x;
        if (w->y + new_h > SCREEN_HEIGHT - DOCK_HEIGHT)
            new_h = SCREEN_HEIGHT - DOCK_HEIGHT - w->y;

        w->w = new_w;
        w->h = new_h;
        request_redraw();
        return;  // Don't send move events while resizing
    }

    // Send mouse move event to window under cursor (if in content area)
    int wid = window_at_point(x, y);
    if (wid >= 0) {
        window_t *w = &windows[wid];
        // Only send if in content area (below title bar)
        if (y >= w->y + TITLE_BAR_HEIGHT) {
            int local_x = x - w->x - 1;
            int local_y = y - w->y - TITLE_BAR_HEIGHT - 1;
            uint8_t buttons = api->mouse_get_buttons();
            push_event(wid, WIN_EVENT_MOUSE_MOVE, local_x, local_y, buttons);
        }
    }
}

static void handle_keyboard(void) {
    while (api->has_key()) {
        int c = api->getc();

        // Send to focused window
        if (focused_window >= 0) {
            push_event(focused_window, WIN_EVENT_KEY, c, 0, 0);
        }

        // Global shortcuts
        if (c == 'q' || c == 'Q') {
            // For debugging - quit desktop
            // running = 0;
        }
    }
}

// ============ Main ============

static void register_window_api(void) {
    // Register our window functions in kapi
    // This is a bit of a hack - we're modifying kapi from userspace
    // But since we're all in the same address space, it works
    api->window_create = wm_window_create;
    api->window_destroy = wm_window_destroy;
    api->window_get_buffer = wm_window_get_buffer;
    api->window_poll_event = wm_window_poll_event;
    api->window_invalidate = wm_window_invalidate;
    api->window_set_title = wm_window_set_title;
}

int main(kapi_t *kapi, int argc, char **argv) {
    (void)argc;
    (void)argv;

    api = kapi;

    // Get screen dimensions from kapi
    SCREEN_WIDTH = api->fb_width;
    SCREEN_HEIGHT = api->fb_height;

    // Check for hardware double buffering (Pi only)
    if (api->fb_has_hw_double_buffer && api->fb_has_hw_double_buffer()) {
        use_hw_double_buffer = 1;
        // Use the kernel-provided backbuffer (part of the 2x height framebuffer)
        backbuffer = api->fb_get_backbuffer();
        // Determine which buffer we're drawing to based on backbuffer address
        // If backbuffer is the bottom half, we'll flip to show buffer 1
        // If backbuffer is the top half, we'll flip to show buffer 0
        if (backbuffer == api->fb_base) {
            current_buffer = 0;  // Drawing to top, will show top
        } else {
            current_buffer = 1;  // Drawing to bottom, will show bottom
        }
        api->puts("Desktop: using hardware double buffering\n");
    } else {
        use_hw_double_buffer = 0;
        // Allocate our own backbuffer
        backbuffer = api->malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32_t));
        if (!backbuffer) {
            api->puts("Desktop: failed to allocate backbuffer\n");
            return 1;
        }
    }

    // Initialize graphics context
    gfx_init(&gfx, backbuffer, SCREEN_WIDTH, SCREEN_HEIGHT, api->font_data);

    // Detect Pi (has DMA) and enable classic flat mode for performance
    if (api->dma_available && api->dma_available()) {
        classic_mode = 1;
        api->puts("Desktop: classic mode (Pi detected)\n");
    }

    // Initialize
    init_dock_positions();
    register_window_api();

    mouse_x = 0;
    mouse_y = 0;
    mouse_prev_x = 0;
    mouse_prev_y = 0;

    // Main loop
    while (running) {
        // Poll mouse
        api->mouse_poll();
        api->mouse_get_pos(&mouse_x, &mouse_y);
        mouse_buttons = api->mouse_get_buttons();

        // Track if cursor moved (for cursor-only updates)
        cursor_moved = (mouse_x != mouse_prev_x || mouse_y != mouse_prev_y);

        // Track dock hover state
        dock_hover_prev = dock_hover_idx;
        dock_hover_idx = dock_icon_at_point(mouse_x, mouse_y);
        int dock_hover_changed = (dock_hover_idx != dock_hover_prev);

        // Handle mouse events
        int left_pressed = (mouse_buttons & MOUSE_BTN_LEFT) && !(mouse_prev_buttons & MOUSE_BTN_LEFT);
        int left_released = !(mouse_buttons & MOUSE_BTN_LEFT) && (mouse_prev_buttons & MOUSE_BTN_LEFT);
        int right_pressed = (mouse_buttons & MOUSE_BTN_RIGHT) && !(mouse_prev_buttons & MOUSE_BTN_RIGHT);

        if (left_pressed || right_pressed) {
            uint8_t pressed_btns = 0;
            if (left_pressed) pressed_btns |= MOUSE_BTN_LEFT;
            if (right_pressed) pressed_btns |= MOUSE_BTN_RIGHT;
            handle_mouse_click(mouse_x, mouse_y, pressed_btns);
        }
        if (left_released) {
            handle_mouse_release(mouse_x, mouse_y);
        }
        if (cursor_moved) {
            handle_mouse_move(mouse_x, mouse_y);
        }

        // Handle keyboard
        handle_keyboard();

        // Dock hover change requires full redraw (icon highlight changes)
        if (dock_hover_changed) {
            needs_redraw = 1;
        }

        // Menu open requires full redraw on cursor move (hover highlighting)
        if (open_menu != MENU_NONE && cursor_moved) {
            needs_redraw = 1;
        }

        // About dialog hover requires full redraw (button highlighting)
        if (show_about_dialog && cursor_moved) {
            needs_redraw = 1;
        }

        // Settings dialog hover requires full redraw (button highlighting)
        if (show_settings_dialog && cursor_moved) {
            needs_redraw = 1;
        }

        // Dock context menu hover requires full redraw
        if (dock_context_menu_visible && cursor_moved) {
            needs_redraw = 1;
        }

        // Decide what to redraw
        if (needs_redraw) {
            // Full redraw needed
            draw_desktop();
            // Save cursor background BEFORE drawing cursor (so we save the clean background)
            save_cursor_bg(backbuffer, mouse_x, mouse_y);
            draw_cursor(mouse_x, mouse_y);
            flip_buffer();
            needs_redraw = 0;
        } else if (cursor_moved) {
            // Only cursor moved - update cursor directly on visible buffer
            // This is MUCH faster than a full redraw
            update_cursor_only(mouse_prev_x, mouse_prev_y, mouse_x, mouse_y);
        }

        mouse_prev_x = mouse_x;
        mouse_prev_y = mouse_y;
        mouse_prev_buttons = mouse_buttons;

        // Yield to other processes (kernel WFIs if nothing else to run)
        api->yield();
    }

    // Cleanup - clear screen to black and restore console (use DMA if available)
    if (api->dma_fill) {
        api->dma_fill(api->fb_base, COLOR_BLACK, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32_t));
    } else {
        for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
            api->fb_base[i] = COLOR_BLACK;
        }
    }

    // Reset scroll offset if using hardware double buffering
    if (use_hw_double_buffer) {
        api->fb_flip(0);  // Show top buffer
    }

    // Clear console and show exit message
    api->clear();
    api->puts("Desktop exited.\n");

    // Only free if we allocated it ourselves
    if (!use_hw_double_buffer) {
        api->free(backbuffer);
    }

    return 0;
}
