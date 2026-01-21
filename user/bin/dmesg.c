/*
 * dmesg - print kernel ring buffer
 *
 * Usage: dmesg [-n]
 *   -n  Non-interactive (dump all output)
 *
 * Without arguments, shows scrollable interface.
 * Controls: j/k or arrows to scroll, q to quit,
 *           g/G for start/end, u/d for page up/down
 */

#include "../lib/kiki.h"

static kapi_t *api;

static void out_puts(const char *s) {
    if (api->stdio_puts) api->stdio_puts(s);
    else api->puts(s);
}

static void out_putc(char c) {
    if (api->stdio_putc) api->stdio_putc(c);
    else api->putc(c);
}

static int in_getc(void) {
    if (api->stdio_getc) return api->stdio_getc();
    return api->getc();
}

static int in_has_key(void) {
    if (api->stdio_has_key) return api->stdio_has_key();
    return api->has_key();
}

// Buffer to hold the log data
static char *log_buf = NULL;
static size_t log_size = 0;

// Line index (start offset of each line)
#define MAX_LINES 8192
static size_t line_offsets[MAX_LINES];
static int line_count = 0;

// Build line index from log buffer
static void build_line_index(void) {
    line_count = 0;
    if (log_size == 0) return;

    line_offsets[line_count++] = 0;

    for (size_t i = 0; i < log_size && line_count < MAX_LINES; i++) {
        if (log_buf[i] == '\n' && i + 1 < log_size) {
            line_offsets[line_count++] = i + 1;
        }
    }
}

// Print integer helper
static void print_int(int n) {
    if (n == 0) {
        out_putc('0');
        return;
    }
    char buf[12];
    int i = 0;
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i > 0) {
        out_putc(buf[--i]);
    }
}

// Print a single line (no newline at end)
static void print_line(int line_idx, int max_cols) {
    if (line_idx < 0 || line_idx >= line_count) return;

    size_t start = line_offsets[line_idx];
    size_t end;
    if (line_idx + 1 < line_count) {
        end = line_offsets[line_idx + 1];
    } else {
        end = log_size;
    }

    // Skip trailing newline if present
    if (end > start && log_buf[end - 1] == '\n') {
        end--;
    }

    // Print up to max_cols characters
    int col = 0;
    for (size_t i = start; i < end && col < max_cols; i++) {
        char c = log_buf[i];
        if (c == '\t') {
            // Tab: print spaces to next 8-column boundary
            int spaces = 8 - (col % 8);
            for (int s = 0; s < spaces && col < max_cols; s++) {
                out_putc(' ');
                col++;
            }
        } else if (c >= 32 && c < 127) {
            out_putc(c);
            col++;
        } else if (c == '\r') {
            // Skip CR
        }
    }
}

// Interactive scrollable viewer
static void interactive_view(void) {
    int rows = api->console_rows ? api->console_rows() : 24;
    int cols = api->console_cols ? api->console_cols() : 80;
    int view_rows = rows - 1;  // Reserve bottom row for status

    int top_line = 0;  // First visible line
    int running = 1;

    // If fewer lines than screen, no scrolling needed
    if (line_count <= view_rows) {
        top_line = 0;
    } else {
        // Start at end (show most recent)
        top_line = line_count - view_rows;
    }

    api->clear();
    api->set_cursor_enabled(0);

    while (running) {
        // Draw visible lines
        for (int i = 0; i < view_rows; i++) {
            api->set_cursor(i, 0);
            api->clear_to_eol();

            int line_idx = top_line + i;
            if (line_idx < line_count) {
                print_line(line_idx, cols);
            }
        }

        // Draw status bar
        api->set_cursor(rows - 1, 0);
        api->set_color(COLOR_BLACK, COLOR_WHITE);
        out_puts(" dmesg: ");

        // Show position info
        int shown_start = top_line + 1;
        int shown_end = top_line + view_rows;
        if (shown_end > line_count) shown_end = line_count;

        print_int(shown_start);
        out_putc('-');
        print_int(shown_end);
        out_putc('/');
        print_int(line_count);

        out_puts("  q:quit j/k:scroll g/G:start/end u/d:page ");
        api->set_color(COLOR_WHITE, COLOR_BLACK);

        // Wait for key
        while (!in_has_key()) {
            api->yield();
        }

        int key = in_getc();

        switch (key) {
            case 'q':
            case 'Q':
            case 27:  // Escape
                running = 0;
                break;

            case KEY_UP:
            case 'k':
                if (top_line > 0) top_line--;
                break;

            case KEY_DOWN:
            case 'j':
                if (line_count > view_rows && top_line < line_count - view_rows) {
                    top_line++;
                }
                break;

            case KEY_HOME:
            case 'g':
                top_line = 0;
                break;

            case KEY_END:
            case 'G':
                if (line_count > view_rows) {
                    top_line = line_count - view_rows;
                }
                break;

            // Page up
            case 'u':
                top_line -= view_rows;
                if (top_line < 0) top_line = 0;
                break;

            // Page down
            case 'd':
                top_line += view_rows;
                if (line_count > view_rows && top_line > line_count - view_rows) {
                    top_line = line_count - view_rows;
                }
                if (top_line < 0) top_line = 0;
                break;

            default:
                break;
        }
    }

    api->set_cursor_enabled(1);
    api->clear();
}

// Simple dump mode (non-interactive)
static void dump_log(void) {
    for (size_t i = 0; i < log_size; i++) {
        out_putc(log_buf[i]);
    }
    // Ensure output ends with newline
    if (log_size > 0 && log_buf[log_size - 1] != '\n') {
        out_putc('\n');
    }
}

int main(kapi_t *k, int argc, char **argv) {
    api = k;

    // Check for -n flag (non-interactive dump)
    // Also auto-detect terminal: if stdio hooks are set, we're in a terminal
    // and interactive mode won't work (it uses console functions directly)
    int in_terminal = (k->stdio_putc != 0);
    int interactive = !in_terminal;  // Default to interactive unless in terminal
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0) {
            interactive = 0;
        }
    }

    // Get log size
    log_size = k->klog_size();
    if (log_size == 0) {
        out_puts("(kernel log empty)\n");
        return 0;
    }

    // Allocate buffer
    log_buf = k->malloc(log_size + 1);
    if (!log_buf) {
        out_puts("dmesg: out of memory\n");
        return 1;
    }

    // Read log
    size_t bytes_read = k->klog_read(log_buf, 0, log_size);
    log_buf[bytes_read] = '\0';
    log_size = bytes_read;

    if (interactive) {
        build_line_index();
        interactive_view();
    } else {
        dump_log();
    }

    k->free(log_buf);
    return 0;
}
