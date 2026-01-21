/*
 * KikiOS System Monitor
 *
 * Classic Mac-style system monitor showing system stats.
 * Shows: uptime, date/time, memory, disk, processes, sound status.
 */

#include "../lib/kiki.h"
#include "../lib/gfx.h"

static kapi_t *api;
static int window_id = -1;
static uint32_t *win_buffer;
static int win_w, win_h;
static gfx_ctx_t gfx;

// Window content dimensions
#define CONTENT_W 320
#define CONTENT_H 520

// Process states (must match kernel)
#define PROC_STATE_FREE    0
#define PROC_STATE_READY   1
#define PROC_STATE_RUNNING 2
#define PROC_STATE_BLOCKED 3
#define PROC_STATE_ZOMBIE  4

#define MAX_PROCESSES 16

// State tracking for dirty-rectangle optimization
// Only redraw when values actually change
static unsigned long last_uptime_sec = 0;
static size_t last_mem_used = 0;
static int last_proc_count = 0;
static int last_sound_state = 0;  // 0=idle, 1=playing, 2=paused
static int needs_redraw = 1;

// Cached stats - fetched once in check_for_changes, reused in draw_all
static unsigned long cached_ticks = 0;
static size_t cached_mem_used = 0;
static size_t cached_mem_free = 0;
static int cached_alloc_count = 0;
static int cached_proc_count = 0;

// Modern colors
#define COLOR_BG         0x00F5F5F5
#define COLOR_SECTION_BG 0x00FFFFFF
#define COLOR_TEXT       0x00333333
#define COLOR_LABEL      0x00666666
#define COLOR_VALUE      0x00222222
#define COLOR_BORDER     0x00E0E0E0
#define COLOR_ACCENT     0x00007AFF
#define COLOR_BAR_BG     0x00E8E8E8
#define COLOR_BAR_FILL   0x0034C759  // Green
#define COLOR_BAR_WARN   0x00FF9500  // Orange when high

// Drawing macros
#define buf_fill_rect(x, y, w, h, c)     gfx_fill_rect(&gfx, x, y, w, h, c)
#define buf_draw_char(x, y, ch, fg, bg)  gfx_draw_char(&gfx, x, y, ch, fg, bg)
#define buf_draw_string(x, y, s, fg, bg) gfx_draw_string(&gfx, x, y, s, fg, bg)
#define buf_draw_rect(x, y, w, h, c)     gfx_draw_rect(&gfx, x, y, w, h, c)
#define buf_fill_rounded(x, y, w, h, r, c) gfx_fill_rounded_rect(&gfx, x, y, w, h, r, c)
#define buf_draw_rounded(x, y, w, h, r, c) gfx_draw_rounded_rect(&gfx, x, y, w, h, r, c)

// ============ Formatting Helpers ============

static void format_num(char *buf, unsigned long n) {
    if (n == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    char tmp[20];
    int i = 0;
    while (n > 0) {
        tmp[i++] = '0' + (n % 10);
        n /= 10;
    }
    int j = 0;
    while (i > 0) {
        buf[j++] = tmp[--i];
    }
    buf[j] = '\0';
}

static void format_hex(char *buf, uint64_t n) {
    const char *hex = "0123456789ABCDEF";
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 8; i++) {
        buf[2 + i] = hex[(n >> (28 - i * 4)) & 0xF];
    }
    buf[10] = '\0';
}

static void format_size_mb(char *buf, unsigned long bytes) {
    unsigned long mb = bytes / (1024 * 1024);
    unsigned long remainder = (bytes % (1024 * 1024)) * 10 / (1024 * 1024);

    format_num(buf, mb);
    int len = strlen(buf);
    buf[len] = '.';
    buf[len+1] = '0' + remainder;
    buf[len+2] = ' ';
    buf[len+3] = 'M';
    buf[len+4] = 'B';
    buf[len+5] = '\0';
}

static void format_size_kb(char *buf, int kb) {
    if (kb >= 1024) {
        // Show as MB
        int mb = kb / 1024;
        int remainder = ((kb % 1024) * 10) / 1024;
        format_num(buf, mb);
        int len = strlen(buf);
        buf[len] = '.';
        buf[len+1] = '0' + remainder;
        buf[len+2] = ' ';
        buf[len+3] = 'M';
        buf[len+4] = 'B';
        buf[len+5] = '\0';
    } else {
        format_num(buf, kb);
        int len = strlen(buf);
        buf[len] = ' ';
        buf[len+1] = 'K';
        buf[len+2] = 'B';
        buf[len+3] = '\0';
    }
}

static void format_uptime(char *buf, unsigned long ticks) {
    unsigned long total_seconds = ticks / 100;
    unsigned long hours = total_seconds / 3600;
    unsigned long minutes = (total_seconds % 3600) / 60;
    unsigned long seconds = total_seconds % 60;

    char tmp[8];
    int pos = 0;

    if (hours > 0) {
        format_num(tmp, hours);
        for (int i = 0; tmp[i]; i++) buf[pos++] = tmp[i];
        buf[pos++] = 'h';
        buf[pos++] = ' ';
    }

    format_num(tmp, minutes);
    for (int i = 0; tmp[i]; i++) buf[pos++] = tmp[i];
    buf[pos++] = 'm';
    buf[pos++] = ' ';

    format_num(tmp, seconds);
    for (int i = 0; tmp[i]; i++) buf[pos++] = tmp[i];
    buf[pos++] = 's';
    buf[pos] = '\0';
}

static void format_datetime(char *buf, int year, int month, int day,
                            int hour, int minute, int second) {
    // Format: YYYY-MM-DD HH:MM:SS
    int pos = 0;

    // Year
    buf[pos++] = '0' + (year / 1000) % 10;
    buf[pos++] = '0' + (year / 100) % 10;
    buf[pos++] = '0' + (year / 10) % 10;
    buf[pos++] = '0' + year % 10;
    buf[pos++] = '-';

    // Month
    buf[pos++] = '0' + (month / 10) % 10;
    buf[pos++] = '0' + month % 10;
    buf[pos++] = '-';

    // Day
    buf[pos++] = '0' + (day / 10) % 10;
    buf[pos++] = '0' + day % 10;
    buf[pos++] = ' ';

    // Hour
    buf[pos++] = '0' + (hour / 10) % 10;
    buf[pos++] = '0' + hour % 10;
    buf[pos++] = ':';

    // Minute
    buf[pos++] = '0' + (minute / 10) % 10;
    buf[pos++] = '0' + minute % 10;
    buf[pos++] = ':';

    // Second
    buf[pos++] = '0' + (second / 10) % 10;
    buf[pos++] = '0' + second % 10;

    buf[pos] = '\0';
}

// ============ Drawing ============

static void draw_progress_bar(int x, int y, int w, int h, int percent) {
    // Rounded background
    buf_fill_rounded(x, y, w, h, h/2, COLOR_BAR_BG);

    // Fill - green normally, orange when > 80%
    int fill_w = (w - 2) * percent / 100;
    if (fill_w > 0) {
        uint32_t fill_color = (percent > 80) ? COLOR_BAR_WARN : COLOR_BAR_FILL;
        buf_fill_rounded(x + 1, y + 1, fill_w, h - 2, (h-2)/2, fill_color);
    }
}

static void draw_section_header(int y, const char *title) {
    // Modern section header - just bold text with accent underline
    buf_draw_string(12, y, title, COLOR_ACCENT, COLOR_BG);
    buf_fill_rect(12, y + 17, strlen(title) * 8, 2, COLOR_ACCENT);
}

static void draw_label_value(int y, const char *label, const char *value) {
    buf_draw_string(16, y, label, COLOR_LABEL, COLOR_BG);
    buf_draw_string(120, y, value, COLOR_VALUE, COLOR_BG);
}

static void draw_all(void) {
    // Clear background
    buf_fill_rect(0, 0, win_w, win_h, COLOR_BG);

    char buf[64];
    int y = 8;

    // ============ Overview Section ============
    draw_section_header(y, "Overview");
    y += 24;

    // Uptime (use cached value)
    format_uptime(buf, cached_ticks);
    draw_label_value(y, "Uptime:", buf);
    y += 18;

    // Date/Time
    int year, month, day, hour, minute, second, weekday;
    api->get_datetime(&year, &month, &day, &hour, &minute, &second, &weekday);
    format_datetime(buf, year, month, day, hour, minute, second);
    draw_label_value(y, "Time:", buf);
    y += 24;

    // ============ Memory Section ============
    draw_section_header(y, "Memory");
    y += 24;

    // RAM total
    size_t ram_total = api->get_ram_total();
    format_size_mb(buf, ram_total);
    draw_label_value(y, "RAM Total:", buf);
    y += 18;

    // Heap used/free (use cached values)
    size_t mem_total = cached_mem_used + cached_mem_free;
    int mem_percent = mem_total ? (int)((cached_mem_used * 100) / mem_total) : 0;

    format_size_mb(buf, cached_mem_used);
    draw_label_value(y, "Heap Used:", buf);
    y += 18;

    format_size_mb(buf, cached_mem_free);
    draw_label_value(y, "Heap Free:", buf);
    y += 18;

    // Memory progress bar
    draw_progress_bar(16, y, CONTENT_W - 80, 14, mem_percent);

    // Show percentage
    format_num(buf, mem_percent);
    int blen = strlen(buf);
    buf[blen] = '%';
    buf[blen+1] = '\0';
    buf_draw_string(CONTENT_W - 48, y, buf, COLOR_VALUE, COLOR_BG);
    y += 24;

    // ============ Debug Memory Section ============
    draw_section_header(y, "Memory Debug");
    y += 24;

    // Heap bounds
    uint64_t heap_start = api->get_heap_start();
    uint64_t heap_end = api->get_heap_end();
    format_hex(buf, heap_start);
    draw_label_value(y, "Heap Start:", buf);
    y += 18;

    format_hex(buf, heap_end);
    draw_label_value(y, "Heap End:", buf);
    y += 18;

    // Heap size
    uint64_t heap_size = heap_end - heap_start;
    format_size_mb(buf, heap_size);
    draw_label_value(y, "Heap Size:", buf);
    y += 18;

    // Stack pointer
    uint64_t sp = api->get_stack_ptr();
    format_hex(buf, sp);
    draw_label_value(y, "Stack Ptr:", buf);
    y += 18;

    // Allocation count (use cached value)
    format_num(buf, cached_alloc_count);
    draw_label_value(y, "Allocs:", buf);
    y += 24;

    // ============ Disk Section ============
    draw_section_header(y, "Disk");
    y += 24;

    int disk_total = api->get_disk_total();
    format_size_kb(buf, disk_total);
    draw_label_value(y, "Size:", buf);
    y += 24;

    // ============ Processes Section ============
    draw_section_header(y, "Processes");
    y += 24;

    // Use cached process count
    format_num(buf, cached_proc_count);
    strcat(buf, " active");
    draw_label_value(y, "Count:", buf);
    y += 18;

    // List active processes (no limit)
    const char *state_names[] = { "-", "Ready", "Run", "Block", "Zombie" };
    int shown = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        char name[32];
        int state;
        if (api->get_process_info(i, name, sizeof(name), &state)) {
            // Truncate long names
            if (strlen(name) > 12) {
                name[12] = '\0';
            }
            buf_draw_string(24, y, name, COLOR_TEXT, COLOR_BG);

            const char *state_str = (state >= 0 && state <= 4) ? state_names[state] : "?";
            uint32_t state_color = (state == 2) ? COLOR_BAR_FILL : COLOR_LABEL;  // Green if running
            buf_draw_string(140, y, state_str, state_color, COLOR_BG);
            y += 16;
            shown++;
        }
    }
    if (shown == 0) {
        buf_draw_string(24, y, "(none)", COLOR_LABEL, COLOR_BG);
        y += 16;
    }
    y += 8;

    // ============ Sound Section ============
    draw_section_header(y, "Sound");
    y += 24;

    const char *sound_status;
    uint32_t sound_color;
    if (api->sound_is_playing()) {
        sound_status = "Playing";
        sound_color = COLOR_BAR_FILL;
    } else if (api->sound_is_paused()) {
        sound_status = "Paused";
        sound_color = COLOR_BAR_WARN;
    } else {
        sound_status = "Idle";
        sound_color = COLOR_LABEL;
    }
    buf_draw_string(16, y, "Status:", COLOR_LABEL, COLOR_BG);
    buf_draw_string(120, y, sound_status, sound_color, COLOR_BG);

    api->window_invalidate(window_id);
}

// Check if any displayed values changed - also populates cache for draw_all
static void check_for_changes(void) {
    // Fetch all stats ONCE and cache them
    cached_ticks = api->get_uptime_ticks();
    cached_mem_used = api->get_mem_used();
    cached_mem_free = api->get_mem_free();
    cached_alloc_count = api->get_alloc_count();
    cached_proc_count = api->get_process_count();

    unsigned long current_sec = cached_ticks / 100;

    int sound_state = 0;  // idle
    if (api->sound_is_playing && api->sound_is_playing()) {
        sound_state = 1;  // playing
    } else if (api->sound_is_paused && api->sound_is_paused()) {
        sound_state = 2;  // paused
    }

    // Check if anything changed
    if (current_sec != last_uptime_sec ||
        cached_mem_used != last_mem_used ||
        cached_proc_count != last_proc_count ||
        sound_state != last_sound_state) {
        needs_redraw = 1;
        last_uptime_sec = current_sec;
        last_mem_used = cached_mem_used;
        last_proc_count = cached_proc_count;
        last_sound_state = sound_state;
    }
}

// ============ CLI Output ============

static void print_cli(void) {
    char buf[64];

    // Helper to print output
    void (*out)(const char *) = api->stdio_puts ? api->stdio_puts : api->puts;

    out("=== System Monitor ===\n\n");

    // Uptime
    unsigned long ticks = api->get_uptime_ticks();
    format_uptime(buf, ticks);
    out("Uptime:     ");
    out(buf);
    out("\n");

    // Date/Time
    int year, month, day, hour, minute, second, weekday;
    api->get_datetime(&year, &month, &day, &hour, &minute, &second, &weekday);
    format_datetime(buf, year, month, day, hour, minute, second);
    out("Time:       ");
    out(buf);
    out("\n\n");

    // RAM
    size_t ram_total = api->get_ram_total();
    format_size_mb(buf, ram_total);
    out("RAM Total:  ");
    out(buf);
    out("\n");

    // Heap
    size_t mem_used = api->get_mem_used();
    size_t mem_free = api->get_mem_free();
    size_t mem_total = mem_used + mem_free;
    int mem_percent = (int)((mem_used * 100) / mem_total);

    format_size_mb(buf, mem_used);
    out("Heap Used:  ");
    out(buf);
    out("\n");

    format_size_mb(buf, mem_free);
    out("Heap Free:  ");
    out(buf);
    out(" (");
    format_num(buf, mem_percent);
    out(buf);
    out("% used)\n\n");

    // Disk
    int disk_total = api->get_disk_total();
    format_size_kb(buf, disk_total);
    out("Disk Size:  ");
    out(buf);
    out("\n\n");

    // Processes
    int proc_count = api->get_process_count();
    out("Processes:  ");
    format_num(buf, proc_count);
    out(buf);
    out(" active\n");

    const char *state_names[] = { "-", "Ready", "Run", "Block", "Zombie" };
    for (int i = 0; i < MAX_PROCESSES; i++) {
        char name[32];
        int state;
        if (api->get_process_info(i, name, sizeof(name), &state)) {
            out("  ");
            out(name);
            // Pad to 16 chars
            int pad = 16 - strlen(name);
            while (pad-- > 0) out(" ");
            const char *state_str = (state >= 0 && state <= 4) ? state_names[state] : "?";
            out(state_str);
            out("\n");
        }
    }
    out("\n");

    // Sound
    out("Sound:      ");
    if (api->sound_is_playing()) {
        out("Playing\n");
    } else if (api->sound_is_paused()) {
        out("Paused\n");
    } else {
        out("Idle\n");
    }
}

// ============ Main ============

int main(kapi_t *kapi, int argc, char **argv) {
    (void)argc;
    (void)argv;

    api = kapi;

    // If no window API, run as CLI tool
    if (!api->window_create) {
        print_cli();
        return 0;
    }

    // Create window
    window_id = api->window_create(250, 100, CONTENT_W, CONTENT_H + 18, "System Monitor");
    if (window_id < 0) {
        api->puts("sysmon: failed to create window\n");
        return 1;
    }

    // Get buffer
    win_buffer = api->window_get_buffer(window_id, &win_w, &win_h);
    if (!win_buffer) {
        api->puts("sysmon: failed to get window buffer\n");
        api->window_destroy(window_id);
        return 1;
    }

    // Initialize graphics context
    gfx_init(&gfx, win_buffer, win_w, win_h, api->font_data);

    // Populate cache before initial draw
    check_for_changes();

    // Initial draw
    draw_all();
    needs_redraw = 0;

    // Event loop - only redraw when data changes
    int running = 1;

    while (running) {
        int event_type, data1, data2, data3;
        while (api->window_poll_event(window_id, &event_type, &data1, &data2, &data3)) {
            switch (event_type) {
                case WIN_EVENT_CLOSE:
                    running = 0;
                    break;
                case WIN_EVENT_KEY:
                    if (data1 == 'q' || data1 == 'Q' || data1 == 27) {
                        running = 0;
                    }
                    break;
                case WIN_EVENT_RESIZE:
                    win_buffer = api->window_get_buffer(window_id, &win_w, &win_h);
                    gfx_init(&gfx, win_buffer, win_w, win_h, api->font_data);
                    needs_redraw = 1;
                    break;
            }
        }

        // Check if displayed values changed
        check_for_changes();

        // Only redraw if something changed
        if (needs_redraw) {
            draw_all();
            needs_redraw = 0;
        }

        api->yield();
    }

    api->window_destroy(window_id);
    return 0;
}
