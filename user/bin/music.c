/*
 * KikiOS Music Player
 *
 * GUI music player with album browser, MP3/WAV playback.
 */

#include "../lib/kiki.h"
#include "../lib/gfx.h"

// Disable SIMD - freestanding environment has no arm_neon.h
#define MINIMP3_NO_SIMD
#define MINIMP3_IMPLEMENTATION
#include "../../vendor/minimp3.h"

static kapi_t *api;
static int window_id = -1;
static uint32_t *win_buffer;
static int win_w, win_h;
static gfx_ctx_t gfx;

// ============ Modern Color Palette ============
#define BLACK       0x00333333
#define WHITE       0x00FFFFFF
#define GRAY        0x00999999
#define LIGHT_GRAY  0x00F5F5F5
#define BORDER      0x00E0E0E0
#define ACCENT      0x00007AFF  // Blue accent
#define ACCENT_DARK 0x00005FCC
#define RED         0x00FF5F57  // Red for stop
#define GREEN       0x0034C759  // Green for play

// ============ Layout Constants ============
#define SIDEBAR_W       160
#define CONTROLS_H      85
#define ALBUM_ITEM_H    20
#define TRACK_ITEM_H    18

// ============ Player State ============
#define MAX_ALBUMS      32
#define MAX_TRACKS      64
#define MAX_NAME_LEN    64

typedef struct {
    char name[MAX_NAME_LEN];
    char path[128];
} album_t;

typedef struct {
    char name[MAX_NAME_LEN];
    char path[128];
} track_t;

static album_t albums[MAX_ALBUMS];
static int album_count = 0;
static int selected_album = -1;

static track_t tracks[MAX_TRACKS];
static int track_count = 0;
static int selected_track = -1;
static int playing_track = -1;

// Playback state
static int is_playing = 0;
static int volume = 80;  // 0-100

// Decoded audio buffer (kept for async playback)
static int16_t *pcm_buffer = NULL;
static uint32_t pcm_samples = 0;
static uint32_t pcm_sample_rate = 44100;
static uint32_t playback_start_tick = 0;
static uint32_t pause_elapsed_ms = 0;  // Elapsed time when paused

// Scroll positions
static int album_scroll = 0;
static int track_scroll = 0;

// Error message (shown briefly)
static char error_msg[64] = {0};
static uint32_t error_tick = 0;

// Single-file mode (launched with file argument)
static int single_file_mode = 0;
static char single_file_path[256] = {0};
static char single_file_name[MAX_NAME_LEN] = {0};

// Loading state machine
typedef enum {
    LOAD_STATE_IDLE = 0,
    LOAD_STATE_LOADING_FILE,
    LOAD_STATE_COUNTING_SAMPLES,
    LOAD_STATE_DECODING,
    LOAD_STATE_STARTING_PLAYBACK
} load_state_t;

static load_state_t load_state = LOAD_STATE_IDLE;
static int is_loading = 0;

// Loading progress tracking
static int load_progress = 0;  // 0-100 percent
static int load_target_track = -1;

// Dirty rectangle flags - only redraw what changed
static int dirty_sidebar = 1;
static int dirty_tracklist = 1;
static int dirty_controls = 1;
static int dirty_progress = 1;

// Track last displayed time to avoid unnecessary progress redraws
static int last_displayed_second = -1;

// File loading state
static uint8_t *load_mp3_data = NULL;
static int load_file_size = 0;
static int load_file_offset = 0;
static void *load_file_handle = NULL;

// Decode state
static mp3dec_t load_mp3d;
static const uint8_t *load_decode_ptr = NULL;
static int load_decode_remaining = 0;
static uint32_t load_total_samples = 0;
static int load_channels = 0;
static int16_t *load_out_ptr = NULL;
static uint32_t load_decoded_samples = 0;

// Chunk sizes for non-blocking operations
#define FILE_CHUNK_SIZE     (32 * 1024)   // 32KB per file read chunk
#define DECODE_FRAMES_PER_YIELD  20        // Decode 20 MP3 frames per yield

// ============ Drawing Helpers ============

#define fill_rect(x, y, w, h, c)     gfx_fill_rect(&gfx, x, y, w, h, c)
#define draw_char(x, y, ch, fg, bg)  gfx_draw_char(&gfx, x, y, ch, fg, bg)
#define draw_string(x, y, s, fg, bg) gfx_draw_string(&gfx, x, y, s, fg, bg)
#define draw_rect(x, y, w, h, c)     gfx_draw_rect(&gfx, x, y, w, h, c)
#define draw_hline(x, y, w, c)       gfx_draw_hline(&gfx, x, y, w, c)
#define draw_vline(x, y, h, c)       gfx_draw_vline(&gfx, x, y, h, c)

// Draw text clipped to width
static void draw_text_clip(int x, int y, const char *s, uint32_t fg, uint32_t bg, int max_w) {
    int drawn = 0;
    while (*s && drawn + 8 <= max_w) {
        draw_char(x, y, *s, fg, bg);
        x += 8;
        drawn += 8;
        s++;
    }
}

// Show error message for 3 seconds
static void show_error(const char *msg) {
    int i = 0;
    while (msg[i] && i < 63) {
        error_msg[i] = msg[i];
        i++;
    }
    error_msg[i] = 0;
    error_tick = api->get_uptime_ticks();
}

// Draw modern rounded button
static void draw_button(int x, int y, int w, int h, const char *label, int pressed) {
    uint32_t bg = pressed ? ACCENT_DARK : LIGHT_GRAY;
    uint32_t fg = pressed ? WHITE : BLACK;
    uint32_t border_color = pressed ? ACCENT : BORDER;

    gfx_fill_rounded_rect(&gfx, x, y, w, h, 6, bg);
    gfx_draw_rounded_rect(&gfx, x, y, w, h, 6, border_color);
    draw_string(x + (w - strlen(label) * 8) / 2, y + (h - 16) / 2, label, fg, bg);
}

// Draw checkerboard pattern (System 7 style)
static void draw_pattern(int x, int y, int w, int h) {
    for (int py = y; py < y + h && py < gfx.height; py++) {
        for (int px = x; px < x + w && px < gfx.width; px++) {
            if ((px + py) % 2 == 0) {
                gfx.buffer[py * gfx.width + px] = GRAY;
            } else {
                gfx.buffer[py * gfx.width + px] = WHITE;
            }
        }
    }
}

// ============ UI Drawing ============

static void draw_sidebar(void) {
    // Sidebar background
    fill_rect(0, 0, SIDEBAR_W, win_h - CONTROLS_H, LIGHT_GRAY);
    draw_vline(SIDEBAR_W - 1, 0, win_h - CONTROLS_H, BORDER);

    // Title
    draw_string(8, 8, "Albums", GRAY, LIGHT_GRAY);
    draw_hline(4, 26, SIDEBAR_W - 8, BORDER);

    // Album list
    int y = 30;
    int visible_albums = (win_h - CONTROLS_H - 34) / ALBUM_ITEM_H;

    for (int i = album_scroll; i < album_count && i < album_scroll + visible_albums; i++) {
        int item_y = y + (i - album_scroll) * ALBUM_ITEM_H;

        // Highlight selected with rounded blue selection
        if (i == selected_album) {
            gfx_fill_rounded_rect(&gfx, 4, item_y, SIDEBAR_W - 8, ALBUM_ITEM_H - 2, 4, ACCENT);
            draw_text_clip(8, item_y + 2, albums[i].name, WHITE, ACCENT, SIDEBAR_W - 16);
        } else {
            draw_text_clip(8, item_y + 2, albums[i].name, BLACK, LIGHT_GRAY, SIDEBAR_W - 16);
        }
    }

    // Scroll arrows if needed
    if (album_count > visible_albums) {
        if (album_scroll > 0) {
            draw_string(SIDEBAR_W - 16, 30, "^", GRAY, LIGHT_GRAY);
        }
        if (album_scroll + visible_albums < album_count) {
            draw_string(SIDEBAR_W - 16, win_h - CONTROLS_H - 20, "v", GRAY, LIGHT_GRAY);
        }
    }
}

static void draw_track_list(void) {
    int x = SIDEBAR_W;
    int w = win_w - SIDEBAR_W;
    int h = win_h - CONTROLS_H;

    // Background
    fill_rect(x, 0, w, h, WHITE);

    // Show error message for 3 seconds
    if (error_msg[0] && api->get_uptime_ticks() - error_tick < 300) {
        int err_len = strlen(error_msg) * 8;
        int err_x = x + (w - err_len) / 2;
        fill_rect(err_x - 4, h - 30, err_len + 8, 20, BLACK);
        draw_string(err_x, h - 28, error_msg, WHITE, BLACK);
    } else {
        error_msg[0] = 0;  // Clear expired error
    }

    if (selected_album < 0 || selected_album >= album_count) {
        // No album selected
        draw_string(x + w/2 - 64, h/2 - 8, "Select album", BLACK, WHITE);
        return;
    }

    // Album header
    draw_string(x + 8, 6, albums[selected_album].name, BLACK, WHITE);

    // Track count
    char info[32];
    int tc = track_count;
    int len = 0;
    info[len++] = '(';
    if (tc >= 10) info[len++] = '0' + (tc / 10);
    info[len++] = '0' + (tc % 10);
    info[len++] = ' ';
    info[len++] = 't';
    info[len++] = 'r';
    info[len++] = 'a';
    info[len++] = 'c';
    info[len++] = 'k';
    if (tc != 1) info[len++] = 's';
    info[len++] = ')';
    info[len] = 0;
    draw_string(x + 8 + strlen(albums[selected_album].name) * 8 + 8, 6, info, GRAY, WHITE);

    draw_hline(x + 4, 24, w - 8, BLACK);

    // Track list
    int list_y = 28;
    int visible_tracks = (h - 32) / TRACK_ITEM_H;

    for (int i = track_scroll; i < track_count && i < track_scroll + visible_tracks; i++) {
        int item_y = list_y + (i - track_scroll) * TRACK_ITEM_H;

        // Track number
        char num[4];
        num[0] = '0' + ((i + 1) / 10);
        num[1] = '0' + ((i + 1) % 10);
        num[2] = '.';
        num[3] = 0;
        if (num[0] == '0') num[0] = ' ';

        // Highlight selected/playing
        if (i == selected_track) {
            fill_rect(x + 2, item_y, w - 4, TRACK_ITEM_H - 2, BLACK);
            draw_string(x + 6, item_y + 1, num, WHITE, BLACK);
        } else {
            draw_string(x + 6, item_y + 1, num, GRAY, WHITE);
        }

        // Playing indicator
        if (i == playing_track && is_playing) {
            if (i == selected_track) {
                draw_char(x + 6, item_y + 1, '>', WHITE, BLACK);
            } else {
                draw_char(x + 6, item_y + 1, '>', BLACK, WHITE);
            }
        }

        // Track name (strip .mp3)
        char display_name[MAX_NAME_LEN];
        int j = 0;
        for (; tracks[i].name[j] && j < MAX_NAME_LEN - 1; j++) {
            display_name[j] = tracks[i].name[j];
        }
        display_name[j] = 0;
        if (j > 4 && display_name[j-4] == '.' && display_name[j-3] == 'm') {
            display_name[j-4] = 0;
        }

        if (i == selected_track) {
            draw_text_clip(x + 32, item_y + 1, display_name, WHITE, BLACK, w - 40);
        } else {
            draw_text_clip(x + 32, item_y + 1, display_name, BLACK, WHITE, w - 40);
        }
    }
}

static void draw_controls(void) {
    int y = win_h - CONTROLS_H;

    // Background with border
    fill_rect(0, y, win_w, CONTROLS_H, WHITE);
    draw_hline(0, y, win_w, BLACK);

    // Now playing info (left side) - only in album mode
    if (!single_file_mode) {
        if (playing_track >= 0 && playing_track < track_count) {
            char display_name[MAX_NAME_LEN];
            int j = 0;
            for (; tracks[playing_track].name[j] && j < MAX_NAME_LEN - 1; j++) {
                display_name[j] = tracks[playing_track].name[j];
            }
            display_name[j] = 0;
            if (j > 4 && display_name[j-4] == '.' && display_name[j-3] == 'm') {
                display_name[j-4] = 0;
            }

            draw_text_clip(8, y + 8, display_name, BLACK, WHITE, 180);
            draw_text_clip(8, y + 26, albums[selected_album].name, GRAY, WHITE, 180);
        } else if (is_loading) {
            const char *status = "Loading...";
            if (load_state == LOAD_STATE_LOADING_FILE) status = "Reading file...";
            else if (load_state == LOAD_STATE_DECODING) status = "Decoding...";
            draw_string(8, y + 16, status, BLACK, WHITE);
        } else {
            draw_string(8, y + 16, "No track", GRAY, WHITE);
        }
    }

    // Center controls
    int cx = win_w / 2;
    int btn_y = y + 8;

    // In single file mode, only show Play/Pause (centered)
    if (single_file_mode) {
        if (is_playing) {
            draw_button(cx - 40, btn_y, 80, 24, "Pause", 0);
        } else {
            draw_button(cx - 40, btn_y, 80, 24, "Play", 0);
        }
    } else {
        // |< Back
        draw_button(cx - 90, btn_y, 30, 24, "|<", 0);

        // Play/Pause
        if (is_playing) {
            draw_button(cx - 40, btn_y, 80, 24, "Pause", 0);
        } else {
            draw_button(cx - 40, btn_y, 80, 24, "Play", 0);
        }

        // >| Next
        draw_button(cx + 60, btn_y, 30, 24, ">|", 0);
    }

    // Progress bar
    int prog_y = y + 42;
    int prog_x = 8;
    int prog_w = win_w - 100;

    // Time labels
    draw_string(prog_x, prog_y, "0:00", GRAY, WHITE);

    // Bar background
    fill_rect(prog_x + 40, prog_y + 4, prog_w - 80, 8, WHITE);
    draw_rect(prog_x + 40, prog_y + 4, prog_w - 80, 8, BLACK);

    // Progress fill - show for both playing and paused states
    if ((is_playing || (playing_track >= 0 && api->sound_is_paused && api->sound_is_paused()))
        && pcm_samples > 0 && pcm_sample_rate > 0) {
        uint32_t elapsed_ms;
        if (is_playing) {
            uint32_t now = api->get_uptime_ticks ? api->get_uptime_ticks() : 0;
            uint32_t elapsed_ticks = now - playback_start_tick;
            elapsed_ms = elapsed_ticks * 10;  // 100Hz timer
        } else {
            // Paused - use saved position
            elapsed_ms = pause_elapsed_ms;
        }
        uint32_t total_ms = ((uint64_t)pcm_samples * 1000) / pcm_sample_rate;

        if (elapsed_ms > total_ms) elapsed_ms = total_ms;

        int fill_w = ((prog_w - 84) * elapsed_ms) / (total_ms > 0 ? total_ms : 1);
        if (fill_w > 0) {
            // Dithered fill
            for (int py = prog_y + 5; py < prog_y + 11; py++) {
                for (int px = prog_x + 41; px < prog_x + 41 + fill_w; px++) {
                    if ((px + py) % 2 == 0) {
                        gfx.buffer[py * gfx.width + px] = BLACK;
                    }
                }
            }
        }

        // Time display
        int secs = elapsed_ms / 1000;
        int mins = secs / 60;
        secs = secs % 60;
        char time_str[8];
        time_str[0] = '0' + mins;
        time_str[1] = ':';
        time_str[2] = '0' + (secs / 10);
        time_str[3] = '0' + (secs % 10);
        time_str[4] = 0;
        draw_string(prog_x, prog_y, time_str, BLACK, WHITE);

        // Total time
        int total_secs = total_ms / 1000;
        int total_mins = total_secs / 60;
        total_secs = total_secs % 60;
        time_str[0] = '0' + total_mins;
        time_str[1] = ':';
        time_str[2] = '0' + (total_secs / 10);
        time_str[3] = '0' + (total_secs % 10);
        time_str[4] = 0;
        draw_string(prog_x + prog_w - 32, prog_y, time_str, GRAY, WHITE);
    } else {
        draw_string(prog_x + prog_w - 32, prog_y, "0:00", GRAY, WHITE);
    }

    // Volume (right side)
    int vol_x = win_w - 80;
    draw_string(vol_x, y + 8, "Vol:", BLACK, WHITE);

    // Volume bar
    fill_rect(vol_x, y + 28, 70, 10, WHITE);
    draw_rect(vol_x, y + 28, 70, 10, BLACK);
    int vol_fill = (volume * 66) / 100;
    fill_rect(vol_x + 2, y + 30, vol_fill, 6, BLACK);
}

// Draw only the progress bar area (dirty rectangle optimization)
static void draw_progress_only(void) {
    int y = win_h - CONTROLS_H;
    int prog_y = y + 42;
    int prog_x = 8;
    int prog_w = win_w - 100;

    // Clear progress bar area (time labels + bar)
    fill_rect(prog_x, prog_y, prog_w, 16, WHITE);

    // Time label placeholder
    draw_string(prog_x, prog_y, "0:00", GRAY, WHITE);

    // Bar background
    fill_rect(prog_x + 40, prog_y + 4, prog_w - 80, 8, WHITE);
    draw_rect(prog_x + 40, prog_y + 4, prog_w - 80, 8, BLACK);

    // Progress fill
    if ((is_playing || (playing_track >= 0 && api->sound_is_paused && api->sound_is_paused()))
        && pcm_samples > 0 && pcm_sample_rate > 0) {
        uint32_t elapsed_ms;
        if (is_playing) {
            uint32_t now = api->get_uptime_ticks ? api->get_uptime_ticks() : 0;
            uint32_t elapsed_ticks = now - playback_start_tick;
            elapsed_ms = elapsed_ticks * 10;
        } else {
            elapsed_ms = pause_elapsed_ms;
        }
        uint32_t total_ms = ((uint64_t)pcm_samples * 1000) / pcm_sample_rate;
        if (elapsed_ms > total_ms) elapsed_ms = total_ms;

        int fill_w = ((prog_w - 84) * elapsed_ms) / (total_ms > 0 ? total_ms : 1);
        if (fill_w > 0) {
            for (int py = prog_y + 5; py < prog_y + 11; py++) {
                for (int px = prog_x + 41; px < prog_x + 41 + fill_w; px++) {
                    if ((px + py) % 2 == 0) {
                        gfx.buffer[py * gfx.width + px] = BLACK;
                    }
                }
            }
        }

        // Time display
        int secs = elapsed_ms / 1000;
        int mins = secs / 60;
        secs = secs % 60;
        char time_str[8];
        time_str[0] = '0' + mins;
        time_str[1] = ':';
        time_str[2] = '0' + (secs / 10);
        time_str[3] = '0' + (secs % 10);
        time_str[4] = 0;
        draw_string(prog_x, prog_y, time_str, BLACK, WHITE);

        // Total time
        int total_secs = total_ms / 1000;
        int total_mins = total_secs / 60;
        total_secs = total_secs % 60;
        time_str[0] = '0' + total_mins;
        time_str[1] = ':';
        time_str[2] = '0' + (total_secs / 10);
        time_str[3] = '0' + (total_secs % 10);
        time_str[4] = 0;
        draw_string(prog_x + prog_w - 32, prog_y, time_str, GRAY, WHITE);
    } else {
        draw_string(prog_x + prog_w - 32, prog_y, "0:00", GRAY, WHITE);
    }

    api->window_invalidate(window_id);
}

// Check if progress bar second changed (returns current second, or -1 if not playing)
static int get_current_playback_second(void) {
    if (!is_playing || pcm_samples == 0 || pcm_sample_rate == 0) return -1;
    uint32_t now = api->get_uptime_ticks ? api->get_uptime_ticks() : 0;
    uint32_t elapsed_ticks = now - playback_start_tick;
    uint32_t elapsed_ms = elapsed_ticks * 10;
    return elapsed_ms / 1000;
}

// Now Playing view for single-file mode
static void draw_now_playing(void) {
    // Full window background
    fill_rect(0, 0, win_w, win_h - CONTROLS_H, WHITE);

    // Center content vertically
    int cy = (win_h - CONTROLS_H) / 2;

    // Large "Now Playing" title
    const char *title = "Now Playing";
    int title_w = 11 * 8;  // strlen("Now Playing") * 8
    draw_string((win_w - title_w) / 2, cy - 50, title, GRAY, WHITE);

    // File name (centered, large-ish)
    int name_len = 0;
    while (single_file_name[name_len]) name_len++;
    int name_w = name_len * 8;
    if (name_w > win_w - 40) name_w = win_w - 40;  // Clamp

    // Strip extension for display
    char display_name[MAX_NAME_LEN];
    int j = 0;
    for (; single_file_name[j] && j < MAX_NAME_LEN - 1; j++) {
        display_name[j] = single_file_name[j];
    }
    display_name[j] = 0;
    // Remove .mp3 or .wav
    if (j > 4 && display_name[j-4] == '.') {
        display_name[j-4] = 0;
        j -= 4;
    }

    name_w = j * 8;
    draw_text_clip((win_w - name_w) / 2, cy - 10, display_name, BLACK, WHITE, win_w - 40);

    // Show error if any
    if (error_msg[0] && api->get_uptime_ticks() - error_tick < 300) {
        int err_len = strlen(error_msg) * 8;
        int err_x = (win_w - err_len) / 2;
        fill_rect(err_x - 4, cy + 30, err_len + 8, 20, BLACK);
        draw_string(err_x, cy + 32, error_msg, WHITE, BLACK);
    }
}

// Draw only dirty regions
static void draw_dirty(void) {
    int did_draw = 0;

    if (single_file_mode) {
        // In single file mode, sidebar/tracklist flags mean "now playing" area
        if (dirty_sidebar || dirty_tracklist) {
            draw_now_playing();
            dirty_sidebar = 0;
            dirty_tracklist = 0;
            did_draw = 1;
        }
    } else {
        if (dirty_sidebar) {
            draw_sidebar();
            dirty_sidebar = 0;
            did_draw = 1;
        }
        if (dirty_tracklist) {
            draw_track_list();
            dirty_tracklist = 0;
            did_draw = 1;
        }
    }

    if (dirty_controls) {
        draw_controls();
        dirty_controls = 0;
        did_draw = 1;
    } else if (dirty_progress) {
        // Only progress bar changed, not full controls
        draw_progress_only();
        dirty_progress = 0;
        did_draw = 1;
    }

    if (did_draw) {
        api->window_invalidate(window_id);
    }
}

// Full redraw (sets all dirty flags and draws)
static void draw_all(void) {
    dirty_sidebar = 1;
    dirty_tracklist = 1;
    dirty_controls = 1;
    dirty_progress = 0;  // Controls includes progress
    draw_dirty();
}

// ============ Album/Track Loading ============

static void scan_albums(void) {
    album_count = 0;

    void *dir = api->open("/home/user/Music");
    if (!dir || !api->is_dir(dir)) {
        return;
    }

    char name[MAX_NAME_LEN];
    uint8_t type;
    int idx = 0;

    while (album_count < MAX_ALBUMS && api->readdir(dir, idx, name, sizeof(name), &type) == 0) {
        idx++;
        if (name[0] == '.') continue;
        if (type != 2) continue;  // 2 = directory

        int i = 0;
        while (name[i] && i < MAX_NAME_LEN - 1) {
            albums[album_count].name[i] = name[i];
            i++;
        }
        albums[album_count].name[i] = 0;

        char *p = albums[album_count].path;
        const char *base = "/home/user/Music/";
        while (*base) *p++ = *base++;
        i = 0;
        while (name[i]) *p++ = name[i++];
        *p = 0;

        album_count++;
    }
}

static void load_tracks(int album_idx) {
    track_count = 0;
    selected_track = -1;
    track_scroll = 0;

    if (album_idx < 0 || album_idx >= album_count) return;

    void *dir = api->open(albums[album_idx].path);
    if (!dir || !api->is_dir(dir)) return;

    char name[MAX_NAME_LEN];
    uint8_t type;
    int idx = 0;

    while (track_count < MAX_TRACKS && api->readdir(dir, idx, name, sizeof(name), &type) == 0) {
        idx++;
        if (name[0] == '.') continue;
        if (type == 2) continue;

        int len = 0;
        while (name[len]) len++;
        if (len < 4) continue;
        if (name[len-4] != '.' || name[len-3] != 'm' || name[len-2] != 'p' || name[len-1] != '3') {
            continue;
        }

        int i = 0;
        while (name[i] && i < MAX_NAME_LEN - 1) {
            tracks[track_count].name[i] = name[i];
            i++;
        }
        tracks[track_count].name[i] = 0;

        char *p = tracks[track_count].path;
        const char *base = albums[album_idx].path;
        while (*base) *p++ = *base++;
        *p++ = '/';
        i = 0;
        while (name[i]) *p++ = name[i++];
        *p = 0;

        track_count++;
    }
}

// ============ Playback ============

static int play_track(int track_idx) {
    if (track_idx < 0 || track_idx >= track_count) return -1;

    // Stop current playback
    if (is_playing) {
        api->sound_stop();
        is_playing = 0;
    }

    // Free old buffer
    if (pcm_buffer) {
        api->free(pcm_buffer);
        pcm_buffer = NULL;
    }

    is_loading = 1;
    load_state = LOAD_STATE_LOADING_FILE;
    draw_all();
    api->yield();

    // Load file
    void *file = api->open(tracks[track_idx].path);
    if (!file) {
        is_loading = 0;
        load_state = LOAD_STATE_IDLE;
        show_error("Cannot open file");
        return -1;
    }

    int size = api->file_size(file);
    if (size <= 0) {
        is_loading = 0;
        load_state = LOAD_STATE_IDLE;
        show_error("Empty file");
        return -1;
    }

    uint8_t *mp3_data = api->malloc(size);
    if (!mp3_data) {
        is_loading = 0;
        load_state = LOAD_STATE_IDLE;
        show_error("Out of memory (file too large)");
        return -1;
    }

    int offset = 0;
    while (offset < size) {
        int n = api->read(file, (char *)mp3_data + offset, size - offset, offset);
        if (n <= 0) break;
        offset += n;
    }

    // Switch to decoding state
    load_state = LOAD_STATE_DECODING;
    draw_all();
    api->yield();

    // Single-pass decode with pre-allocated buffer
    // For 10MB MP3 @ 128kbps stereo: ~50 min = ~530MB PCM
    // Ratio ~53:1, but 320kbps would be ~21:1. Use 15x for safety.
    uint32_t max_pcm_bytes = (uint32_t)size * 15;
    pcm_buffer = api->malloc(max_pcm_bytes);
    if (!pcm_buffer) {
        api->free(mp3_data);
        is_loading = 0;
        load_state = LOAD_STATE_IDLE;
        show_error("Out of memory (song too long)");
        return -1;
    }

    mp3dec_t mp3d;
    mp3dec_init(&mp3d);
    mp3dec_frame_info_t info;
    int16_t temp_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];

    const uint8_t *ptr = mp3_data;
    int remaining = size;
    int16_t *out_ptr = pcm_buffer;
    uint32_t decoded_samples = 0;
    int channels = 0;

    while (remaining > 0) {
        int samples = mp3dec_decode_frame(&mp3d, ptr, remaining, temp_pcm, &info);
        if (info.frame_bytes == 0) break;
        if (samples > 0) {
            if (channels == 0) {
                channels = info.channels;
                pcm_sample_rate = info.hz;
            }
            decoded_samples += samples;
            if (channels == 1) {
                for (int i = 0; i < samples; i++) {
                    *out_ptr++ = temp_pcm[i];
                    *out_ptr++ = temp_pcm[i];
                }
            } else {
                for (int i = 0; i < samples * 2; i++) {
                    *out_ptr++ = temp_pcm[i];
                }
            }
        }
        ptr += info.frame_bytes;
        remaining -= info.frame_bytes;
    }

    api->free(mp3_data);

    if (decoded_samples == 0 || channels == 0) {
        api->free(pcm_buffer);
        pcm_buffer = NULL;
        is_loading = 0;
        load_state = LOAD_STATE_IDLE;
        show_error("Invalid MP3 format");
        return -1;
    }

    // Use actual decoded sample count for accurate duration
    pcm_samples = decoded_samples;
    playing_track = track_idx;
    is_playing = 1;
    is_loading = 0;
    load_state = LOAD_STATE_IDLE;
    playback_start_tick = api->get_uptime_ticks ? api->get_uptime_ticks() : 0;
    pause_elapsed_ms = 0;

    api->sound_play_pcm_async(pcm_buffer, pcm_samples, 2, pcm_sample_rate);

    return 0;
}

// Check file extension (case insensitive)
static int ends_with(const char *str, const char *suffix) {
    int str_len = 0, suf_len = 0;
    while (str[str_len]) str_len++;
    while (suffix[suf_len]) suf_len++;
    if (suf_len > str_len) return 0;
    for (int i = 0; i < suf_len; i++) {
        char a = str[str_len - suf_len + i];
        char b = suffix[i];
        // Lowercase
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return 0;
    }
    return 1;
}

// Play a file directly by path (MP3 or WAV)
static int play_file(const char *path) {
    // Stop current playback
    if (is_playing) {
        api->sound_stop();
        is_playing = 0;
    }

    // Free old buffer
    if (pcm_buffer) {
        api->free(pcm_buffer);
        pcm_buffer = NULL;
    }

    is_loading = 1;
    load_state = LOAD_STATE_LOADING_FILE;
    draw_all();
    api->yield();

    // Load file
    void *file = api->open(path);
    if (!file) {
        is_loading = 0;
        load_state = LOAD_STATE_IDLE;
        show_error("Cannot open file");
        return -1;
    }

    int size = api->file_size(file);
    if (size <= 0) {
        is_loading = 0;
        load_state = LOAD_STATE_IDLE;
        show_error("Empty file");
        return -1;
    }

    uint8_t *file_data = api->malloc(size);
    if (!file_data) {
        is_loading = 0;
        load_state = LOAD_STATE_IDLE;
        show_error("Out of memory");
        return -1;
    }

    int offset = 0;
    while (offset < size) {
        int n = api->read(file, (char *)file_data + offset, size - offset, offset);
        if (n <= 0) break;
        offset += n;
    }

    // Check file type and decode
    if (ends_with(path, ".wav")) {
        // WAV file - parse header and extract PCM
        load_state = LOAD_STATE_DECODING;
        draw_all();
        api->yield();

        // Basic WAV header parsing
        if (size < 44) {
            api->free(file_data);
            is_loading = 0;
            load_state = LOAD_STATE_IDLE;
            show_error("Invalid WAV file");
            return -1;
        }

        // Check RIFF header
        if (file_data[0] != 'R' || file_data[1] != 'I' ||
            file_data[2] != 'F' || file_data[3] != 'F') {
            api->free(file_data);
            is_loading = 0;
            load_state = LOAD_STATE_IDLE;
            show_error("Not a WAV file");
            return -1;
        }

        // Find fmt chunk
        int channels = file_data[22] | (file_data[23] << 8);
        int sample_rate = file_data[24] | (file_data[25] << 8) |
                         (file_data[26] << 16) | (file_data[27] << 24);
        int bits_per_sample = file_data[34] | (file_data[35] << 8);

        if (bits_per_sample != 16) {
            api->free(file_data);
            is_loading = 0;
            load_state = LOAD_STATE_IDLE;
            show_error("Only 16-bit WAV supported");
            return -1;
        }

        // Find data chunk (starts at offset 44 for standard WAV)
        int data_offset = 44;
        int data_size = size - 44;

        // Search for "data" marker if needed
        for (int i = 36; i < size - 8; i++) {
            if (file_data[i] == 'd' && file_data[i+1] == 'a' &&
                file_data[i+2] == 't' && file_data[i+3] == 'a') {
                data_size = file_data[i+4] | (file_data[i+5] << 8) |
                           (file_data[i+6] << 16) | (file_data[i+7] << 24);
                data_offset = i + 8;
                break;
            }
        }

        int num_samples = data_size / (channels * 2);  // 16-bit = 2 bytes

        // Convert to stereo if mono
        if (channels == 1) {
            pcm_buffer = api->malloc(num_samples * 4);  // 2 channels * 2 bytes
            if (!pcm_buffer) {
                api->free(file_data);
                is_loading = 0;
                load_state = LOAD_STATE_IDLE;
                show_error("Out of memory");
                return -1;
            }
            int16_t *src = (int16_t *)(file_data + data_offset);
            int16_t *dst = pcm_buffer;
            for (int i = 0; i < num_samples; i++) {
                dst[i * 2] = src[i];
                dst[i * 2 + 1] = src[i];
            }
        } else {
            // Already stereo, just copy
            pcm_buffer = api->malloc(data_size);
            if (!pcm_buffer) {
                api->free(file_data);
                is_loading = 0;
                load_state = LOAD_STATE_IDLE;
                show_error("Out of memory");
                return -1;
            }
            // Manual copy
            int16_t *src = (int16_t *)(file_data + data_offset);
            int16_t *dst = pcm_buffer;
            for (int i = 0; i < num_samples * 2; i++) {
                dst[i] = src[i];
            }
        }

        api->free(file_data);
        pcm_samples = num_samples;
        pcm_sample_rate = sample_rate;

    } else {
        // Assume MP3
        load_state = LOAD_STATE_DECODING;
        draw_all();
        api->yield();

        uint32_t max_pcm_bytes = (uint32_t)size * 15;
        pcm_buffer = api->malloc(max_pcm_bytes);
        if (!pcm_buffer) {
            api->free(file_data);
            is_loading = 0;
            load_state = LOAD_STATE_IDLE;
            show_error("Out of memory");
            return -1;
        }

        mp3dec_t mp3d;
        mp3dec_init(&mp3d);
        mp3dec_frame_info_t info;
        int16_t temp_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];

        const uint8_t *ptr = file_data;
        int remaining = size;
        int16_t *out_ptr = pcm_buffer;
        uint32_t decoded_samples = 0;
        int channels = 0;

        while (remaining > 0) {
            int samples = mp3dec_decode_frame(&mp3d, ptr, remaining, temp_pcm, &info);
            if (info.frame_bytes == 0) break;
            if (samples > 0) {
                if (channels == 0) {
                    channels = info.channels;
                    pcm_sample_rate = info.hz;
                }
                decoded_samples += samples;
                if (channels == 1) {
                    for (int i = 0; i < samples; i++) {
                        *out_ptr++ = temp_pcm[i];
                        *out_ptr++ = temp_pcm[i];
                    }
                } else {
                    for (int i = 0; i < samples * 2; i++) {
                        *out_ptr++ = temp_pcm[i];
                    }
                }
            }
            ptr += info.frame_bytes;
            remaining -= info.frame_bytes;
        }

        api->free(file_data);

        if (decoded_samples == 0 || channels == 0) {
            api->free(pcm_buffer);
            pcm_buffer = NULL;
            is_loading = 0;
            load_state = LOAD_STATE_IDLE;
            show_error("Invalid audio format");
            return -1;
        }

        pcm_samples = decoded_samples;
    }

    // Start playback
    playing_track = 0;  // Use 0 to indicate "something is playing"
    is_playing = 1;
    is_loading = 0;
    load_state = LOAD_STATE_IDLE;
    playback_start_tick = api->get_uptime_ticks ? api->get_uptime_ticks() : 0;
    pause_elapsed_ms = 0;

    api->sound_play_pcm_async(pcm_buffer, pcm_samples, 2, pcm_sample_rate);

    return 0;
}

static void toggle_play_pause(void) {
    if (playing_track < 0) {
        // Nothing loaded - play selected or first track
        if (single_file_mode) {
            play_file(single_file_path);
        } else if (selected_track >= 0) {
            play_track(selected_track);
        } else if (track_count > 0) {
            play_track(0);
        }
    } else if (is_playing) {
        // Currently playing - pause it
        if (api->sound_pause) {
            // Save current elapsed time before pausing
            uint32_t now = api->get_uptime_ticks ? api->get_uptime_ticks() : 0;
            uint32_t elapsed_ticks = now - playback_start_tick;
            pause_elapsed_ms = elapsed_ticks * 10;  // 100Hz timer
            api->sound_pause();
            is_playing = 0;
        }
    } else {
        // Currently paused - resume
        if (api->sound_resume && api->sound_is_paused && api->sound_is_paused()) {
            // Adjust start tick so progress bar continues from paused position
            uint32_t now = api->get_uptime_ticks ? api->get_uptime_ticks() : 0;
            playback_start_tick = now - (pause_elapsed_ms / 10);
            api->sound_resume();
            is_playing = 1;
        } else {
            // Fallback: restart from beginning
            playback_start_tick = api->get_uptime_ticks ? api->get_uptime_ticks() : 0;
            pause_elapsed_ms = 0;
            api->sound_play_pcm_async(pcm_buffer, pcm_samples, 2, pcm_sample_rate);
            is_playing = 1;
        }
    }
    dirty_controls = 1;  // Play/pause button label changed
}

static void next_track(void) {
    if (track_count == 0) return;
    int next = (playing_track >= 0 ? playing_track : selected_track) + 1;
    if (next >= track_count) next = 0;
    play_track(next);
}

static void prev_track(void) {
    if (track_count == 0) return;
    int prev = (playing_track >= 0 ? playing_track : selected_track) - 1;
    if (prev < 0) prev = track_count - 1;
    play_track(prev);
}

// ============ Event Handling ============

static void handle_click(int mx, int my) {
    int ctrl_y = win_h - CONTROLS_H;

    // Click in controls area
    if (my >= ctrl_y) {
        int cx = win_w / 2;
        int btn_y = ctrl_y + 8;

        // Back button (not in single file mode)
        if (!single_file_mode && mx >= cx - 90 && mx < cx - 60 && my >= btn_y && my < btn_y + 24) {
            prev_track();
            return;
        }

        // Play/Pause
        if (mx >= cx - 40 && mx < cx + 40 && my >= btn_y && my < btn_y + 24) {
            toggle_play_pause();
            return;
        }

        // Next button (not in single file mode)
        if (!single_file_mode && mx >= cx + 60 && mx < cx + 90 && my >= btn_y && my < btn_y + 24) {
            next_track();
            return;
        }

        // Volume bar (not shown in single file mode compact view)
        if (!single_file_mode) {
            int vol_x = win_w - 80;
            if (mx >= vol_x && mx < vol_x + 70 && my >= ctrl_y + 28 && my < ctrl_y + 38) {
                volume = ((mx - vol_x) * 100) / 70;
                if (volume < 0) volume = 0;
                if (volume > 100) volume = 100;
                dirty_controls = 1;  // Volume bar changed
                return;
            }
        }

        return;
    }

    // Skip album/track clicks in single file mode
    if (single_file_mode) return;

    // Click in sidebar (albums)
    if (mx < SIDEBAR_W) {
        int y = 28;
        int visible_albums = (win_h - CONTROLS_H - 32) / ALBUM_ITEM_H;

        for (int i = album_scroll; i < album_count && i < album_scroll + visible_albums; i++) {
            int item_y = y + (i - album_scroll) * ALBUM_ITEM_H;
            if (my >= item_y && my < item_y + ALBUM_ITEM_H) {
                selected_album = i;
                load_tracks(i);
                dirty_sidebar = 1;    // Album selection changed
                dirty_tracklist = 1;  // Track list now showing different album
                return;
            }
        }
        return;
    }

    // Click in track list
    if (selected_album >= 0) {
        int list_y = 28;
        int visible_tracks = (win_h - CONTROLS_H - 32) / TRACK_ITEM_H;

        for (int i = track_scroll; i < track_count && i < track_scroll + visible_tracks; i++) {
            int item_y = list_y + (i - track_scroll) * TRACK_ITEM_H;
            if (my >= item_y && my < item_y + TRACK_ITEM_H) {
                selected_track = i;
                dirty_tracklist = 1;  // Track selection changed
                return;
            }
        }
    }
}

static void handle_double_click(int mx, int my) {
    if (single_file_mode) return;
    if (mx >= SIDEBAR_W && selected_album >= 0) {
        int list_y = 28;
        int visible_tracks = (win_h - CONTROLS_H - 32) / TRACK_ITEM_H;

        for (int i = track_scroll; i < track_count && i < track_scroll + visible_tracks; i++) {
            int item_y = list_y + (i - track_scroll) * TRACK_ITEM_H;
            if (my >= item_y && my < item_y + TRACK_ITEM_H) {
                play_track(i);
                return;
            }
        }
    }
}

// ============ Main ============

// Extract filename from path
static void extract_filename(const char *path, char *name, int max_len) {
    // Find last '/'
    const char *last_slash = path;
    const char *p = path;
    while (*p) {
        if (*p == '/') last_slash = p + 1;
        p++;
    }
    // Copy filename
    int i = 0;
    while (last_slash[i] && i < max_len - 1) {
        name[i] = last_slash[i];
        i++;
    }
    name[i] = 0;
}

int main(kapi_t *k, int argc, char **argv) {
    api = k;

    // Check for file argument
    if (argc > 1 && argv[1]) {
        single_file_mode = 1;
        // Copy path
        int i = 0;
        while (argv[1][i] && i < 255) {
            single_file_path[i] = argv[1][i];
            i++;
        }
        single_file_path[i] = 0;
        // Extract filename for display
        extract_filename(single_file_path, single_file_name, MAX_NAME_LEN);
    }

    // Create window - smaller for single file mode
    if (single_file_mode) {
        win_w = 350;
        win_h = 200;
    } else {
        win_w = 500;
        win_h = 400;
    }

    // Window title: "Music" or filename
    const char *title = single_file_mode ? single_file_name : "Music";
    window_id = api->window_create(150, 80, win_w, win_h, title);
    if (window_id < 0) {
        return 1;
    }

    int bw, bh;
    win_buffer = api->window_get_buffer(window_id, &bw, &bh);
    if (!win_buffer) {
        api->window_destroy(window_id);
        return 1;
    }

    gfx_init(&gfx, win_buffer, bw, bh, api->font_data);

    if (single_file_mode) {
        // Start playing immediately
        draw_all();
        api->yield();
        play_file(single_file_path);
    } else {
        scan_albums();
    }
    draw_all();

    int running = 1;
    int last_click_tick = 0;
    int last_click_x = -1, last_click_y = -1;

    while (running) {
        int event_type, data1, data2, data3;

        while (api->window_poll_event(window_id, &event_type, &data1, &data2, &data3)) {
            switch (event_type) {
                case WIN_EVENT_CLOSE:
                    running = 0;
                    break;

                case WIN_EVENT_MOUSE_DOWN: {
                    int mx = data1;
                    int my = data2;

                    uint32_t now = api->get_uptime_ticks ? api->get_uptime_ticks() : 0;
                    if (now - last_click_tick < 30 &&
                        mx >= last_click_x - 5 && mx <= last_click_x + 5 &&
                        my >= last_click_y - 5 && my <= last_click_y + 5) {
                        handle_double_click(mx, my);
                    } else {
                        handle_click(mx, my);
                    }
                    last_click_tick = now;
                    last_click_x = mx;
                    last_click_y = my;
                    break;
                }

                case WIN_EVENT_KEY: {
                    int key = data1;
                    if (key == ' ') {
                        toggle_play_pause();
                    } else if (!single_file_mode && (key == 'n' || key == 'N' || key == 0x103)) {
                        next_track();
                    } else if (!single_file_mode && (key == 'p' || key == 'P' || key == 0x102)) {
                        prev_track();
                    } else if (!single_file_mode && key == 0x101) {
                        if (selected_track < track_count - 1) {
                            selected_track++;
                            dirty_tracklist = 1;
                        }
                    } else if (!single_file_mode && key == 0x100) {
                        if (selected_track > 0) {
                            selected_track--;
                            dirty_tracklist = 1;
                        }
                    } else if (!single_file_mode && (key == '\n' || key == '\r')) {
                        if (selected_track >= 0) play_track(selected_track);
                    } else if (key == 'q' || key == 'Q') {
                        running = 0;
                    }
                    break;
                }

                case WIN_EVENT_RESIZE: {
                    // Re-fetch buffer with new dimensions
                    int bw, bh;
                    win_buffer = api->window_get_buffer(window_id, &bw, &bh);
                    win_w = bw;
                    win_h = bh;
                    gfx_init(&gfx, win_buffer, bw, bh, api->font_data);
                    dirty_sidebar = 1;
                    dirty_tracklist = 1;
                    dirty_controls = 1;
                    break;
                }
            }
        }

        // Check if playback finished
        if (is_playing && api->sound_is_playing && !api->sound_is_playing()) {
            if (single_file_mode) {
                // In single file mode, just stop (don't advance)
                is_playing = 0;
                playing_track = -1;
                dirty_controls = 1;
            } else {
                next_track();
            }
        }

        // Check if progress bar second changed (only updates once per second during playback)
        if (is_playing) {
            int current_second = get_current_playback_second();
            if (current_second != last_displayed_second) {
                last_displayed_second = current_second;
                dirty_progress = 1;
            }
        }

        // Only redraw what's dirty
        draw_dirty();
        api->yield();
    }

    if (is_playing) {
        api->sound_stop();
    }
    if (pcm_buffer) {
        api->free(pcm_buffer);
    }
    api->window_destroy(window_id);

    return 0;
}
