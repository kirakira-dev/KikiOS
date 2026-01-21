/*
 * KikiOS VIM - Modal Text Editor
 *
 * A proper vim implementation for KikiOS with:
 * - Modal editing (NORMAL, INSERT, COMMAND, OPERATOR_PENDING)
 * - Core motions: hjkl, w/b/e, 0/$, gg/G
 * - Operators: d, y, c with motions
 * - Counts: 5j, 3dd, etc.
 * - Single register for yank/paste
 */

#include "kiki.h"

// =============================================================================
// Configuration
// =============================================================================

#define INITIAL_BUFFER_SIZE 4096
#define GAP_SIZE 256
#define MAX_CMD_LEN 256
#define MAX_FILENAME 256
#define MAX_STATUS_MSG 80
#define MAX_YANK 8192

// =============================================================================
// Types
// =============================================================================

// Editor modes
typedef enum {
    MODE_NORMAL,
    MODE_INSERT,
    MODE_COMMAND,
    MODE_OPERATOR_PENDING
} editor_mode_t;

// Operator types
typedef enum {
    OP_NONE,
    OP_DELETE,
    OP_YANK,
    OP_CHANGE
} operator_t;

// Gap buffer
typedef struct {
    char *data;
    size_t size;        // Total allocated size
    size_t gap_start;   // Start of gap (cursor position in buffer)
    size_t gap_end;     // End of gap
} gap_buffer_t;

// Redraw modes
typedef enum {
    REDRAW_NONE,    // Just reposition cursor
    REDRAW_LINE,    // Redraw current line only
    REDRAW_STATUS,  // Redraw status line only
    REDRAW_ALL      // Full screen redraw
} redraw_t;

// Editor state
typedef struct {
    kapi_t *api;

    // Text buffer
    gap_buffer_t buf;

    // Cursor position (logical, in text)
    size_t cursor;

    // Preferred column for vertical movement
    int preferred_col;

    // Mode
    editor_mode_t mode;

    // Pending operator
    operator_t pending_op;

    // Count prefix (0 = none)
    int count;

    // Pending 'g' for gg command
    int pending_g;

    // Yank register
    char yank_buffer[MAX_YANK];
    size_t yank_len;
    int yank_linewise;

    // Command line buffer
    char cmd_buf[MAX_CMD_LEN];
    int cmd_len;

    // File info
    char filename[MAX_FILENAME];
    int modified;

    // Screen dimensions
    int screen_rows;
    int screen_cols;

    // Viewport
    int top_line;

    // Status message
    char status_msg[MAX_STATUS_MSG];

    // Running flag
    int running;

    // Redraw flag
    redraw_t needs_redraw;
} editor_t;

// Global editor state
static editor_t ed;

// =============================================================================
// Gap Buffer Implementation
// =============================================================================

static int gap_init(gap_buffer_t *gb, size_t initial_size) {
    gb->data = ed.api->malloc(initial_size);
    if (!gb->data) return -1;
    gb->size = initial_size;
    gb->gap_start = 0;
    gb->gap_end = initial_size;
    return 0;
}

static void gap_free(gap_buffer_t *gb) {
    if (gb->data) {
        ed.api->free(gb->data);
        gb->data = NULL;
    }
}

static size_t gap_length(gap_buffer_t *gb) {
    return gb->size - (gb->gap_end - gb->gap_start);
}

static char gap_get_char(gap_buffer_t *gb, size_t pos) {
    if (pos >= gap_length(gb)) return '\0';
    if (pos < gb->gap_start) {
        return gb->data[pos];
    } else {
        return gb->data[pos + (gb->gap_end - gb->gap_start)];
    }
}

static void gap_move_to(gap_buffer_t *gb, size_t pos) {
    if (pos == gb->gap_start) return;

    if (pos < gb->gap_start) {
        // Move gap left
        size_t count = gb->gap_start - pos;
        size_t gap_size = gb->gap_end - gb->gap_start;
        // Move chars from before gap to after gap
        for (size_t i = 0; i < count; i++) {
            gb->data[gb->gap_end - 1 - i] = gb->data[gb->gap_start - 1 - i];
        }
        gb->gap_start = pos;
        gb->gap_end = pos + gap_size;
    } else {
        // Move gap right
        size_t count = pos - gb->gap_start;
        // Move chars from after gap to before gap
        for (size_t i = 0; i < count; i++) {
            gb->data[gb->gap_start + i] = gb->data[gb->gap_end + i];
        }
        size_t gap_size = gb->gap_end - gb->gap_start;
        gb->gap_start = pos;
        gb->gap_end = pos + gap_size;
    }
}

static int gap_grow(gap_buffer_t *gb) {
    size_t new_size = gb->size * 2;
    char *new_data = ed.api->malloc(new_size);
    if (!new_data) return -1;

    // Copy before gap
    memcpy(new_data, gb->data, gb->gap_start);
    // Copy after gap
    size_t after_gap = gb->size - gb->gap_end;
    memcpy(new_data + new_size - after_gap, gb->data + gb->gap_end, after_gap);

    ed.api->free(gb->data);
    gb->data = new_data;
    gb->gap_end = new_size - after_gap;
    gb->size = new_size;
    return 0;
}

static int gap_insert_char(gap_buffer_t *gb, char c) {
    if (gb->gap_start >= gb->gap_end) {
        if (gap_grow(gb) < 0) return -1;
    }
    gb->data[gb->gap_start++] = c;
    return 0;
}

static int gap_insert_string(gap_buffer_t *gb, const char *s, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (gap_insert_char(gb, s[i]) < 0) return -1;
    }
    return 0;
}

static void gap_delete_backward(gap_buffer_t *gb) {
    if (gb->gap_start > 0) {
        gb->gap_start--;
    }
}

static void gap_delete_forward(gap_buffer_t *gb) {
    if (gb->gap_end < gb->size) {
        gb->gap_end++;
    }
}

// =============================================================================
// Position Utilities
// =============================================================================

// Get line number (0-indexed) for a position
static int pos_to_line(size_t pos) {
    int line = 0;
    for (size_t i = 0; i < pos && i < gap_length(&ed.buf); i++) {
        if (gap_get_char(&ed.buf, i) == '\n') line++;
    }
    return line;
}

// Get column (0-indexed) for a position
static int pos_to_col(size_t pos) {
    int col = 0;
    for (size_t i = pos; i > 0; i--) {
        char c = gap_get_char(&ed.buf, i - 1);
        if (c == '\n') break;
        col++;
    }
    return col;
}

// Get start of line containing pos
static size_t line_start(size_t pos) {
    while (pos > 0 && gap_get_char(&ed.buf, pos - 1) != '\n') {
        pos--;
    }
    return pos;
}

// Get end of line containing pos (position of \n or EOF)
static size_t line_end(size_t pos) {
    size_t len = gap_length(&ed.buf);
    while (pos < len && gap_get_char(&ed.buf, pos) != '\n') {
        pos++;
    }
    return pos;
}

// Get line length (excluding newline)
static int line_length(size_t pos) {
    return (int)(line_end(pos) - line_start(pos));
}

// Get total line count
static int line_count(void) {
    size_t len = gap_length(&ed.buf);
    int count = 1;
    for (size_t i = 0; i < len; i++) {
        if (gap_get_char(&ed.buf, i) == '\n') count++;
    }
    return count;
}

// Get position of start of line N (0-indexed)
static size_t line_n_start(int n) {
    size_t pos = 0;
    size_t len = gap_length(&ed.buf);
    int line = 0;
    while (line < n && pos < len) {
        if (gap_get_char(&ed.buf, pos) == '\n') line++;
        pos++;
    }
    return pos;
}

// =============================================================================
// Character Classification
// =============================================================================

static int is_word_char(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') ||
           c == '_';
}

static int is_whitespace(char c) {
    return c == ' ' || c == '\t';
}

// =============================================================================
// Word Motion Helpers
// =============================================================================

// Move forward to next word start (w)
static size_t word_forward(size_t pos) {
    size_t len = gap_length(&ed.buf);
    if (pos >= len) return pos;

    char c = gap_get_char(&ed.buf, pos);

    // Skip current word or punctuation
    if (is_word_char(c)) {
        while (pos < len && is_word_char(gap_get_char(&ed.buf, pos))) pos++;
    } else if (!is_whitespace(c) && c != '\n') {
        while (pos < len) {
            c = gap_get_char(&ed.buf, pos);
            if (is_word_char(c) || is_whitespace(c) || c == '\n') break;
            pos++;
        }
    }

    // Skip whitespace (but stop at newline)
    while (pos < len) {
        c = gap_get_char(&ed.buf, pos);
        if (c == '\n') {
            pos++;  // Move past newline
            break;
        }
        if (!is_whitespace(c)) break;
        pos++;
    }

    return pos;
}

// Move backward to previous word start (b)
static size_t word_backward(size_t pos) {
    if (pos == 0) return 0;
    pos--;

    // Skip whitespace and newlines
    while (pos > 0) {
        char c = gap_get_char(&ed.buf, pos);
        if (!is_whitespace(c) && c != '\n') break;
        pos--;
    }

    // Skip current word or punctuation
    char c = gap_get_char(&ed.buf, pos);
    if (is_word_char(c)) {
        while (pos > 0 && is_word_char(gap_get_char(&ed.buf, pos - 1))) pos--;
    } else if (!is_whitespace(c) && c != '\n') {
        while (pos > 0) {
            c = gap_get_char(&ed.buf, pos - 1);
            if (is_word_char(c) || is_whitespace(c) || c == '\n') break;
            pos--;
        }
    }

    return pos;
}

// Move forward to end of word (e)
static size_t word_end(size_t pos) {
    size_t len = gap_length(&ed.buf);
    if (pos >= len) return pos;

    pos++;  // Move off current char

    // Skip whitespace
    while (pos < len) {
        char c = gap_get_char(&ed.buf, pos);
        if (!is_whitespace(c) && c != '\n') break;
        pos++;
    }

    if (pos >= len) return len > 0 ? len - 1 : 0;

    // Move to end of word
    char c = gap_get_char(&ed.buf, pos);
    if (is_word_char(c)) {
        while (pos < len - 1 && is_word_char(gap_get_char(&ed.buf, pos + 1))) pos++;
    } else if (!is_whitespace(c) && c != '\n') {
        while (pos < len - 1) {
            c = gap_get_char(&ed.buf, pos + 1);
            if (is_word_char(c) || is_whitespace(c) || c == '\n') break;
            pos++;
        }
    }

    return pos;
}

// =============================================================================
// Screen Rendering
// =============================================================================

static void set_status(const char *msg) {
    strncpy_safe(ed.status_msg, msg, MAX_STATUS_MSG);
}

// Forward declaration for fast line clearing
static void clear_line(int row);

static void draw_status_line(void) {
    kapi_t *api = ed.api;
    int row = ed.screen_rows - 1;

    // Fast clear the status line first
    clear_line(row);
    api->set_cursor(row, 0);
    api->set_color(COLOR_BLACK, COLOR_WHITE);

    // Mode indicator
    const char *mode_str = "NORMAL";
    switch (ed.mode) {
        case MODE_INSERT: mode_str = "INSERT"; break;
        case MODE_COMMAND: mode_str = "COMMAND"; break;
        case MODE_OPERATOR_PENDING: mode_str = "OP-PEND"; break;
        default: break;
    }
    api->puts(" ");
    api->puts(mode_str);
    api->puts(" ");

    api->set_color(COLOR_WHITE, COLOR_BLACK);
    api->puts(" ");

    // Filename
    if (ed.filename[0]) {
        api->puts(ed.filename);
    } else {
        api->puts("[No Name]");
    }

    // Modified indicator
    if (ed.modified) {
        api->puts(" [+]");
    }

    // Fill middle with spaces
    int cur_col = strlen(mode_str) + 3 + strlen(ed.filename[0] ? ed.filename : "[No Name]") + (ed.modified ? 4 : 0);

    // Right side: line:col
    char pos_buf[32];
    int line = pos_to_line(ed.cursor) + 1;
    int col = pos_to_col(ed.cursor) + 1;

    // Build position string manually
    char *p = pos_buf;
    // Line number
    int tmp = line;
    char digits[16];
    int ndigits = 0;
    do {
        digits[ndigits++] = '0' + (tmp % 10);
        tmp /= 10;
    } while (tmp > 0);
    for (int i = ndigits - 1; i >= 0; i--) *p++ = digits[i];
    *p++ = ':';
    // Col number
    tmp = col;
    ndigits = 0;
    do {
        digits[ndigits++] = '0' + (tmp % 10);
        tmp /= 10;
    } while (tmp > 0);
    for (int i = ndigits - 1; i >= 0; i--) *p++ = digits[i];
    *p = '\0';

    int pos_len = strlen(pos_buf);
    // Move cursor to right side for position display (line already cleared)
    // Leave 1 char margin to avoid triggering scroll on last column
    int pos_col = ed.screen_cols - pos_len - 1;
    if (pos_col > cur_col) {
        api->set_cursor(row, pos_col);
        api->puts(pos_buf);
        // Don't write trailing space - it would push cursor past last column
        // and trigger a scroll on the status line row
    }
}

static void draw_command_line(void) {
    kapi_t *api = ed.api;
    int row = ed.screen_rows - 1;
    if (row < 0) row = 0;

    // Fast clear the line first
    clear_line(row);
    api->set_cursor(row, 0);
    api->set_color(COLOR_WHITE, COLOR_BLACK);

    api->putc(':');
    // Limit command length to avoid scrolling
    int max_cmd = ed.screen_cols - 2;  // Leave room for ':' and margin
    int draw_len = ed.cmd_len < max_cmd ? ed.cmd_len : max_cmd;
    for (int i = 0; i < draw_len; i++) {
        api->putc(ed.cmd_buf[i]);
    }
    // No need to clear rest of line - already cleared
}

// Fast clear from current position to end of line
// Uses hardware-accelerated clear when available
static void clear_to_eol(int col) {
    kapi_t *api = ed.api;
    // Use fast clear_to_eol which clears from current cursor position
    if (api->clear_to_eol) {
        api->clear_to_eol();
    } else {
        // Fallback to slow putc loop
        while (col < ed.screen_cols) {
            api->putc(' ');
            col++;
        }
    }
}

// Fast clear entire line (call this before drawing)
static void clear_line(int row) {
    kapi_t *api = ed.api;
    if (api->clear_region) {
        api->clear_region(row, 0, ed.screen_cols, 1);
    } else {
        // Fallback: clear with spaces (stop 1 early to avoid scroll on last row)
        api->set_cursor(row, 0);
        for (int i = 0; i < ed.screen_cols - 1; i++) {
            api->putc(' ');
        }
    }
}

// Draw a single line at screen row
static void draw_line(int screen_row, size_t line_pos, int is_eof) {
    kapi_t *api = ed.api;
    size_t len = gap_length(&ed.buf);

    // Fast clear entire line first (much faster than putc(' ') for each empty cell)
    clear_line(screen_row);
    api->set_cursor(screen_row, 0);

    if (!is_eof && line_pos <= len) {
        api->set_color(COLOR_WHITE, COLOR_BLACK);
        int col = 0;
        size_t pos = line_pos;

        while (pos < len && col < ed.screen_cols) {
            char c = gap_get_char(&ed.buf, pos);
            if (c == '\n') break;
            if (c == '\t') {
                int spaces = 4 - (col % 4);
                for (int s = 0; s < spaces && col < ed.screen_cols; s++) {
                    api->putc(' ');
                    col++;
                }
            } else {
                api->putc(c);
                col++;
            }
            pos++;
        }
        // No need to clear_to_eol - we already cleared the whole line
    } else {
        api->set_color(COLOR_BLUE, COLOR_BLACK);
        api->putc('~');
        api->set_color(COLOR_WHITE, COLOR_BLACK);
        // No need to clear_to_eol - we already cleared the whole line
    }
}

// Redraw just the current line (fast path for insert mode)
static void redraw_current_line(void) {
    int cursor_line = pos_to_line(ed.cursor);
    int screen_row = cursor_line - ed.top_line;

    if (screen_row >= 0 && screen_row < ed.screen_rows - 1) {
        size_t line_pos = line_start(ed.cursor);
        draw_line(screen_row, line_pos, 0);
    }

    // Position cursor
    int cursor_col = pos_to_col(ed.cursor);
    ed.api->set_cursor(screen_row, cursor_col);
}

// Position cursor only (fastest path)
static void position_cursor(void) {
    int cursor_line = pos_to_line(ed.cursor);
    int cursor_col = pos_to_col(ed.cursor);
    int screen_row = cursor_line - ed.top_line;
    ed.api->set_cursor(screen_row, cursor_col);
}

static void redraw_screen(void) {
    kapi_t *api = ed.api;

    // Calculate cursor position
    int cursor_line = pos_to_line(ed.cursor);
    int cursor_col = pos_to_col(ed.cursor);

    // Adjust viewport
    int text_rows = ed.screen_rows - 1;  // Leave room for status line
    if (cursor_line < ed.top_line) {
        ed.top_line = cursor_line;
    } else if (cursor_line >= ed.top_line + text_rows) {
        ed.top_line = cursor_line - text_rows + 1;
    }

    size_t len = gap_length(&ed.buf);
    size_t pos = line_n_start(ed.top_line);
    int past_eof = (pos >= len && len == 0) ? 1 : 0;

    api->set_color(COLOR_WHITE, COLOR_BLACK);

    // Fast clear all text rows at once if possible
    if (api->clear_region) {
        api->clear_region(0, 0, ed.screen_cols, text_rows);
    }

    for (int row = 0; row < text_rows; row++) {
        api->set_cursor(row, 0);

        if (!past_eof && pos <= len) {
            // Draw line content
            int col = 0;
            while (pos < len && col < ed.screen_cols) {
                char c = gap_get_char(&ed.buf, pos);
                if (c == '\n') {
                    pos++;
                    break;
                }
                if (c == '\t') {
                    int spaces = 4 - (col % 4);
                    for (int s = 0; s < spaces && col < ed.screen_cols; s++) {
                        api->putc(' ');
                        col++;
                    }
                } else {
                    api->putc(c);
                    col++;
                }
                pos++;
            }

            // No clear_to_eol needed - we already cleared all rows

            // If we hit end of buffer without newline, mark past_eof
            if (pos >= len) {
                past_eof = 1;
            }
        } else {
            // Empty line indicator
            api->set_color(COLOR_BLUE, COLOR_BLACK);
            api->putc('~');
            api->set_color(COLOR_WHITE, COLOR_BLACK);
            // No clear_to_eol needed - we already cleared all rows
        }
    }

    // Draw status or command line
    if (ed.mode == MODE_COMMAND) {
        draw_command_line();
    } else {
        draw_status_line();
    }

    // Position cursor
    int screen_row = cursor_line - ed.top_line;
    api->set_cursor(screen_row, cursor_col);
}

// =============================================================================
// File I/O
// =============================================================================

static int load_file(const char *filename) {
    void *file = ed.api->open(filename);
    if (!file) {
        // New file
        set_status("New file");
        return 0;
    }

    int size = ed.api->file_size(file);
    if (size < 0) {
        set_status("Error: could not get file size");
        return -1;
    }

    // Ensure buffer is big enough
    while (ed.buf.size < (size_t)size + GAP_SIZE) {
        if (gap_grow(&ed.buf) < 0) {
            set_status("Error: out of memory");
            return -1;
        }
    }

    // Read file content
    char *temp = ed.api->malloc(size + 1);
    if (!temp) {
        set_status("Error: out of memory");
        return -1;
    }

    int bytes_read = ed.api->read(file, temp, size, 0);
    if (bytes_read < 0) {
        ed.api->free(temp);
        set_status("Error: read failed");
        return -1;
    }

    // Insert into buffer
    for (int i = 0; i < bytes_read; i++) {
        gap_insert_char(&ed.buf, temp[i]);
    }

    ed.api->free(temp);

    // Reset cursor to start
    ed.cursor = 0;
    gap_move_to(&ed.buf, 0);
    ed.modified = 0;

    set_status("File loaded");
    return 0;
}

static int save_file(void) {
    if (!ed.filename[0]) {
        set_status("No filename");
        return -1;
    }

    size_t len = gap_length(&ed.buf);

    // Create/truncate file
    void *file = ed.api->create(ed.filename);
    if (!file) {
        set_status("Error: could not create file");
        return -1;
    }

    // Write content
    char *temp = ed.api->malloc(len + 1);
    if (!temp) {
        set_status("Error: out of memory");
        return -1;
    }

    for (size_t i = 0; i < len; i++) {
        temp[i] = gap_get_char(&ed.buf, i);
    }

    int written = ed.api->write(file, temp, len);
    ed.api->free(temp);

    if (written < 0 || (size_t)written != len) {
        set_status("Error: write failed");
        return -1;
    }

    ed.modified = 0;
    set_status("Written");
    return 0;
}

// =============================================================================
// Operators
// =============================================================================

static void yank_range(size_t from, size_t to, int linewise) {
    if (from > to) {
        size_t tmp = from;
        from = to;
        to = tmp;
    }

    size_t len = to - from;
    if (len > MAX_YANK - 1) len = MAX_YANK - 1;

    for (size_t i = 0; i < len; i++) {
        ed.yank_buffer[i] = gap_get_char(&ed.buf, from + i);
    }
    ed.yank_buffer[len] = '\0';
    ed.yank_len = len;
    ed.yank_linewise = linewise;
}

static void delete_range(size_t from, size_t to) {
    if (from > to) {
        size_t tmp = from;
        from = to;
        to = tmp;
    }

    size_t len = to - from;

    // Move gap to end of range and delete backward
    gap_move_to(&ed.buf, to);
    for (size_t i = 0; i < len; i++) {
        gap_delete_backward(&ed.buf);
    }

    ed.cursor = from;
    ed.modified = 1;
}

static void apply_operator(operator_t op, size_t from, size_t to, int linewise) {
    // Yank first (delete and change also yank)
    yank_range(from, to, linewise);

    switch (op) {
        case OP_DELETE:
        case OP_CHANGE:
            delete_range(from, to);
            if (op == OP_CHANGE) {
                ed.mode = MODE_INSERT;
                gap_move_to(&ed.buf, ed.cursor);
            }
            break;
        case OP_YANK:
            // Already yanked above
            set_status("Yanked");
            break;
        default:
            break;
    }
}

// =============================================================================
// Insert Mode
// =============================================================================

static void handle_insert_mode(int key) {
    gap_move_to(&ed.buf, ed.cursor);

    if (key == 27) {  // Escape
        ed.mode = MODE_NORMAL;
        // Move cursor back one if possible (vim behavior)
        if (ed.cursor > 0) {
            size_t ls = line_start(ed.cursor);
            if (ed.cursor > ls) ed.cursor--;
        }
        ed.needs_redraw = REDRAW_STATUS;  // Update mode display
        return;
    }

    if (key == '\r' || key == '\n') {
        gap_insert_char(&ed.buf, '\n');
        ed.cursor++;
        ed.modified = 1;
        ed.needs_redraw = REDRAW_ALL;  // New line needs full redraw
        return;
    }

    if (key == 127 || key == '\b') {  // Backspace
        if (ed.cursor > 0) {
            char deleted = gap_get_char(&ed.buf, ed.cursor - 1);
            gap_delete_backward(&ed.buf);
            ed.cursor--;
            ed.modified = 1;
            // If we deleted a newline, need full redraw
            ed.needs_redraw = (deleted == '\n') ? REDRAW_ALL : REDRAW_LINE;
        }
        return;
    }

    if (key == KEY_DELETE) {
        if (ed.cursor < gap_length(&ed.buf)) {
            char deleted = gap_get_char(&ed.buf, ed.cursor);
            gap_delete_forward(&ed.buf);
            ed.modified = 1;
            ed.needs_redraw = (deleted == '\n') ? REDRAW_ALL : REDRAW_LINE;
        }
        return;
    }

    if (key == KEY_LEFT) {
        if (ed.cursor > 0) ed.cursor--;
        ed.needs_redraw = REDRAW_NONE;
        return;
    }
    if (key == KEY_RIGHT) {
        if (ed.cursor < gap_length(&ed.buf)) ed.cursor++;
        ed.needs_redraw = REDRAW_NONE;
        return;
    }
    if (key == KEY_UP) {
        int col = pos_to_col(ed.cursor);
        size_t ls = line_start(ed.cursor);
        if (ls > 0) {
            size_t prev_ls = line_start(ls - 1);
            int prev_len = line_length(prev_ls);
            ed.cursor = prev_ls + (col < prev_len ? col : prev_len);
        }
        ed.needs_redraw = REDRAW_STATUS;
        return;
    }
    if (key == KEY_DOWN) {
        int col = pos_to_col(ed.cursor);
        size_t le = line_end(ed.cursor);
        if (le < gap_length(&ed.buf)) {
            size_t next_ls = le + 1;
            int next_len = line_length(next_ls);
            ed.cursor = next_ls + (col < next_len ? col : next_len);
        }
        ed.needs_redraw = REDRAW_STATUS;
        return;
    }

    // Regular character
    if (key >= 32 && key < 127) {
        gap_insert_char(&ed.buf, (char)key);
        ed.cursor++;
        ed.modified = 1;
        ed.needs_redraw = REDRAW_LINE;
    }
}

// =============================================================================
// Command Mode
// =============================================================================

static void process_command(void) {
    ed.cmd_buf[ed.cmd_len] = '\0';

    if (strcmp(ed.cmd_buf, "q") == 0) {
        if (ed.modified) {
            set_status("No write since last change (use :q! to override)");
        } else {
            ed.running = 0;
        }
    } else if (strcmp(ed.cmd_buf, "q!") == 0) {
        ed.running = 0;
    } else if (strcmp(ed.cmd_buf, "w") == 0) {
        save_file();
    } else if (strcmp(ed.cmd_buf, "wq") == 0 || strcmp(ed.cmd_buf, "x") == 0) {
        if (save_file() == 0) {
            ed.running = 0;
        }
    } else if (strncmp(ed.cmd_buf, "w ", 2) == 0) {
        // :w filename
        strncpy_safe(ed.filename, ed.cmd_buf + 2, MAX_FILENAME);
        save_file();
    } else if (strncmp(ed.cmd_buf, "e ", 2) == 0) {
        // :e filename - TODO: check modified
        strncpy_safe(ed.filename, ed.cmd_buf + 2, MAX_FILENAME);
        // Clear buffer
        ed.buf.gap_start = 0;
        ed.buf.gap_end = ed.buf.size;
        ed.cursor = 0;
        load_file(ed.filename);
    } else {
        set_status("Unknown command");
    }

    ed.mode = MODE_NORMAL;
    ed.cmd_len = 0;
}

static void handle_command_mode(int key) {
    if (key == 27) {  // Escape
        ed.mode = MODE_NORMAL;
        ed.cmd_len = 0;
        ed.needs_redraw = REDRAW_ALL;
        return;
    }

    if (key == '\r' || key == '\n') {
        process_command();
        ed.needs_redraw = REDRAW_ALL;
        return;
    }

    if (key == 127 || key == '\b') {  // Backspace
        if (ed.cmd_len > 0) {
            ed.cmd_len--;
        } else {
            ed.mode = MODE_NORMAL;
        }
        ed.needs_redraw = REDRAW_STATUS;
        return;
    }

    // Add character to command
    if (key >= 32 && key < 127 && ed.cmd_len < MAX_CMD_LEN - 1) {
        ed.cmd_buf[ed.cmd_len++] = (char)key;
        ed.needs_redraw = REDRAW_STATUS;
    }
}

// =============================================================================
// Normal Mode
// =============================================================================

static void do_motion(int key, int count) {
    size_t len = gap_length(&ed.buf);

    switch (key) {
        case 'h':
        case KEY_LEFT: {
            size_t ls = line_start(ed.cursor);
            for (int i = 0; i < count && ed.cursor > ls; i++) {
                ed.cursor--;
            }
            break;
        }

        case 'l':
        case KEY_RIGHT: {
            size_t le = line_end(ed.cursor);
            for (int i = 0; i < count && ed.cursor < le; i++) {
                // Don't go past last char on line (vim behavior in normal mode)
                if (ed.cursor + 1 < le || ed.mode == MODE_INSERT) {
                    ed.cursor++;
                }
            }
            break;
        }

        case 'j':
        case KEY_DOWN: {
            int col = ed.preferred_col >= 0 ? ed.preferred_col : pos_to_col(ed.cursor);
            for (int i = 0; i < count; i++) {
                size_t le = line_end(ed.cursor);
                if (le >= len) break;
                size_t next_ls = le + 1;
                int next_len = line_length(next_ls);
                ed.cursor = next_ls + (col < next_len ? col : (next_len > 0 ? next_len - 1 : 0));
            }
            ed.preferred_col = col;
            return;  // Don't reset preferred_col
        }

        case 'k':
        case KEY_UP: {
            int col = ed.preferred_col >= 0 ? ed.preferred_col : pos_to_col(ed.cursor);
            for (int i = 0; i < count; i++) {
                size_t ls = line_start(ed.cursor);
                if (ls == 0) break;
                size_t prev_ls = line_start(ls - 1);
                int prev_len = line_length(prev_ls);
                ed.cursor = prev_ls + (col < prev_len ? col : (prev_len > 0 ? prev_len - 1 : 0));
            }
            ed.preferred_col = col;
            return;  // Don't reset preferred_col
        }

        case 'w': {
            for (int i = 0; i < count; i++) {
                ed.cursor = word_forward(ed.cursor);
            }
            break;
        }

        case 'b': {
            for (int i = 0; i < count; i++) {
                ed.cursor = word_backward(ed.cursor);
            }
            break;
        }

        case 'e': {
            for (int i = 0; i < count; i++) {
                ed.cursor = word_end(ed.cursor);
            }
            break;
        }

        case '0':
        case KEY_HOME:
            ed.cursor = line_start(ed.cursor);
            break;

        case '$':
        case KEY_END: {
            size_t le = line_end(ed.cursor);
            // In normal mode, stop before newline
            if (le > line_start(ed.cursor)) {
                ed.cursor = le - 1;
            } else {
                ed.cursor = le;
            }
            break;
        }

        case '^': {
            // First non-blank
            size_t ls = line_start(ed.cursor);
            size_t le = line_end(ed.cursor);
            ed.cursor = ls;
            while (ed.cursor < le && is_whitespace(gap_get_char(&ed.buf, ed.cursor))) {
                ed.cursor++;
            }
            break;
        }

        case 'G': {
            if (ed.count > 0) {
                // Go to line N
                int target = ed.count - 1;
                int total = line_count();
                if (target >= total) target = total - 1;
                ed.cursor = line_n_start(target);
            } else {
                // Go to last line
                int total = line_count();
                ed.cursor = line_n_start(total - 1);
            }
            break;
        }
    }

    ed.preferred_col = -1;
}

// Returns the range end for a motion (cursor doesn't move yet)
static size_t motion_end(int key, int count) {
    size_t saved_cursor = ed.cursor;
    do_motion(key, count);
    size_t end = ed.cursor;
    ed.cursor = saved_cursor;
    return end;
}

static void handle_normal_mode(int key) {
    // Handle count prefix
    if (key >= '1' && key <= '9' && ed.count == 0 && !ed.pending_g) {
        ed.count = key - '0';
        return;
    }
    if (key >= '0' && key <= '9' && ed.count > 0) {
        ed.count = ed.count * 10 + (key - '0');
        return;
    }

    int count = ed.count > 0 ? ed.count : 1;

    // Handle pending g command
    if (ed.pending_g) {
        ed.pending_g = 0;
        if (key == 'g') {
            // gg - go to first line
            ed.cursor = 0;
            ed.needs_redraw = REDRAW_ALL;  // May scroll
        }
        ed.count = 0;
        ed.pending_op = OP_NONE;
        return;
    }

    // Handle operator-pending mode
    if (ed.mode == MODE_OPERATOR_PENDING) {
        size_t from = ed.cursor;
        size_t to = from;
        int linewise = 0;

        // Same key = line operation
        if ((ed.pending_op == OP_DELETE && key == 'd') ||
            (ed.pending_op == OP_YANK && key == 'y') ||
            (ed.pending_op == OP_CHANGE && key == 'c')) {
            // Line-wise operation
            linewise = 1;
            for (int i = 0; i < count; i++) {
                to = line_end(to);
                if (to < gap_length(&ed.buf)) to++;  // Include newline
            }
            from = line_start(from);
        } else if (key == 'w' || key == 'b' || key == 'e' || key == '$' || key == '0' || key == '^') {
            to = motion_end(key, count);
            if (key == 'w' || key == 'e') {
                // For dw, include up to next word start
                if (ed.pending_op == OP_DELETE || ed.pending_op == OP_CHANGE) {
                    // dw deletes to start of next word including whitespace
                }
            }
        } else if (key == 'g') {
            ed.pending_g = 1;
            return;
        } else if (key == 'G') {
            to = gap_length(&ed.buf);
            from = line_start(from);
            linewise = 1;
        } else if (key == 27) {  // Escape
            ed.mode = MODE_NORMAL;
            ed.pending_op = OP_NONE;
            ed.count = 0;
            return;
        } else {
            // Unknown motion - cancel
            ed.mode = MODE_NORMAL;
            ed.pending_op = OP_NONE;
            ed.count = 0;
            return;
        }

        apply_operator(ed.pending_op, from, to, linewise);
        ed.mode = (ed.pending_op == OP_CHANGE) ? MODE_INSERT : MODE_NORMAL;
        ed.pending_op = OP_NONE;
        ed.count = 0;
        ed.needs_redraw = REDRAW_ALL;
        return;
    }

    // Normal mode commands
    switch (key) {
        // Mode switches
        case 'i':
            ed.mode = MODE_INSERT;
            gap_move_to(&ed.buf, ed.cursor);
            ed.needs_redraw = REDRAW_STATUS;
            break;

        case 'a':
            if (ed.cursor < line_end(ed.cursor)) {
                ed.cursor++;
            }
            ed.mode = MODE_INSERT;
            gap_move_to(&ed.buf, ed.cursor);
            ed.needs_redraw = REDRAW_STATUS;
            break;

        case 'I':
            // Insert at first non-blank
            ed.cursor = line_start(ed.cursor);
            while (ed.cursor < line_end(ed.cursor) && is_whitespace(gap_get_char(&ed.buf, ed.cursor))) {
                ed.cursor++;
            }
            ed.mode = MODE_INSERT;
            gap_move_to(&ed.buf, ed.cursor);
            ed.needs_redraw = REDRAW_STATUS;
            break;

        case 'A':
            ed.cursor = line_end(ed.cursor);
            ed.mode = MODE_INSERT;
            gap_move_to(&ed.buf, ed.cursor);
            ed.needs_redraw = REDRAW_STATUS;
            break;

        case 'o': {
            // Open line below
            ed.cursor = line_end(ed.cursor);
            gap_move_to(&ed.buf, ed.cursor);
            gap_insert_char(&ed.buf, '\n');
            ed.cursor++;
            ed.mode = MODE_INSERT;
            ed.modified = 1;
            ed.needs_redraw = REDRAW_ALL;
            break;
        }

        case 'O': {
            // Open line above
            ed.cursor = line_start(ed.cursor);
            gap_move_to(&ed.buf, ed.cursor);
            gap_insert_char(&ed.buf, '\n');
            // cursor stays at new empty line
            ed.mode = MODE_INSERT;
            ed.modified = 1;
            ed.needs_redraw = REDRAW_ALL;
            break;
        }

        case ':':
            ed.mode = MODE_COMMAND;
            ed.cmd_len = 0;
            ed.needs_redraw = REDRAW_STATUS;
            break;

        // Operators
        case 'd':
            ed.mode = MODE_OPERATOR_PENDING;
            ed.pending_op = OP_DELETE;
            ed.needs_redraw = REDRAW_STATUS;
            break;

        case 'y':
            ed.mode = MODE_OPERATOR_PENDING;
            ed.pending_op = OP_YANK;
            ed.needs_redraw = REDRAW_STATUS;
            break;

        case 'c':
            ed.mode = MODE_OPERATOR_PENDING;
            ed.pending_op = OP_CHANGE;
            ed.needs_redraw = REDRAW_STATUS;
            break;

        // Single-char operations
        case 'x': {
            // Delete char under cursor
            size_t le = line_end(ed.cursor);
            if (ed.cursor < le) {
                yank_range(ed.cursor, ed.cursor + 1, 0);
                gap_move_to(&ed.buf, ed.cursor + 1);
                gap_delete_backward(&ed.buf);
                ed.modified = 1;
                ed.needs_redraw = REDRAW_LINE;
            }
            break;
        }

        case 'D': {
            // Delete to end of line
            size_t le = line_end(ed.cursor);
            if (ed.cursor < le) {
                yank_range(ed.cursor, le, 0);
                delete_range(ed.cursor, le);
                ed.needs_redraw = REDRAW_LINE;
            }
            break;
        }

        case 'C': {
            // Change to end of line
            size_t le = line_end(ed.cursor);
            yank_range(ed.cursor, le, 0);
            delete_range(ed.cursor, le);
            ed.mode = MODE_INSERT;
            gap_move_to(&ed.buf, ed.cursor);
            ed.needs_redraw = REDRAW_ALL;
            break;
        }

        // Put
        case 'p': {
            if (ed.yank_len == 0) break;
            if (ed.yank_linewise) {
                // Put after current line
                size_t le = line_end(ed.cursor);
                if (le < gap_length(&ed.buf)) le++;  // After newline
                gap_move_to(&ed.buf, le);
                gap_insert_string(&ed.buf, ed.yank_buffer, ed.yank_len);
                ed.cursor = le;
            } else {
                // Put after cursor
                if (ed.cursor < gap_length(&ed.buf)) ed.cursor++;
                gap_move_to(&ed.buf, ed.cursor);
                gap_insert_string(&ed.buf, ed.yank_buffer, ed.yank_len);
            }
            ed.modified = 1;
            ed.needs_redraw = REDRAW_ALL;
            break;
        }

        case 'P': {
            if (ed.yank_len == 0) break;
            if (ed.yank_linewise) {
                // Put before current line
                size_t ls = line_start(ed.cursor);
                gap_move_to(&ed.buf, ls);
                gap_insert_string(&ed.buf, ed.yank_buffer, ed.yank_len);
                ed.cursor = ls;
            } else {
                // Put before cursor
                gap_move_to(&ed.buf, ed.cursor);
                gap_insert_string(&ed.buf, ed.yank_buffer, ed.yank_len);
            }
            ed.modified = 1;
            ed.needs_redraw = REDRAW_ALL;
            break;
        }

        // g prefix
        case 'g':
            ed.pending_g = 1;
            break;

        // Motions (these move cursor)
        case 'h':
        case 'j':
        case 'k':
        case 'l':
        case 'w':
        case 'b':
        case 'e':
        case '0':
        case '$':
        case '^':
        case 'G':
        case KEY_LEFT:
        case KEY_RIGHT:
        case KEY_UP:
        case KEY_DOWN:
        case KEY_HOME:
        case KEY_END:
            do_motion(key, count);
            break;

        // Other
        case 'u':
            set_status("Undo not yet implemented");
            break;

        case 'r': {
            // Replace single char - wait for next key
            // For simplicity, just read next key here
            while (!ed.api->has_key()) ed.api->yield();
            int ch = ed.api->getc();
            if (ch >= 32 && ch < 127) {
                size_t le = line_end(ed.cursor);
                if (ed.cursor < le) {
                    gap_move_to(&ed.buf, ed.cursor + 1);
                    gap_delete_backward(&ed.buf);
                    gap_insert_char(&ed.buf, (char)ch);
                    ed.modified = 1;
                    ed.needs_redraw = REDRAW_LINE;
                }
            }
            break;
        }

        default:
            break;
    }

    ed.count = 0;

    // If no specific redraw was set, default to updating status line
    if (ed.needs_redraw == REDRAW_NONE) {
        ed.needs_redraw = REDRAW_STATUS;
    }
}

// =============================================================================
// Main
// =============================================================================

int main(kapi_t *api, int argc, char **argv) {
    // Initialize
    memset(&ed, 0, sizeof(ed));
    ed.api = api;
    ed.running = 1;
    ed.mode = MODE_NORMAL;
    ed.preferred_col = -1;

    // Get screen size
    ed.screen_rows = api->console_rows ? api->console_rows() : 24;
    ed.screen_cols = api->console_cols ? api->console_cols() : 80;

    // Initialize buffer
    if (gap_init(&ed.buf, INITIAL_BUFFER_SIZE) < 0) {
        api->puts("Error: could not allocate buffer\n");
        return 1;
    }

    // Load file if specified
    if (argc > 1) {
        strncpy_safe(ed.filename, argv[1], MAX_FILENAME);
        load_file(ed.filename);
    }

    // Initial draw
    ed.needs_redraw = REDRAW_ALL;

    // Main loop
    while (ed.running) {
        // Check if cursor is outside visible area - force full redraw if so
        int cursor_line = pos_to_line(ed.cursor);
        int text_rows = ed.screen_rows - 1;
        if (cursor_line < ed.top_line || cursor_line >= ed.top_line + text_rows) {
            ed.needs_redraw = REDRAW_ALL;
        }

        // Smart redraw based on what changed
        switch (ed.needs_redraw) {
            case REDRAW_ALL:
                redraw_screen();
                break;
            case REDRAW_LINE:
                redraw_current_line();
                draw_status_line();
                position_cursor();
                break;
            case REDRAW_STATUS:
                draw_status_line();
                position_cursor();
                break;
            case REDRAW_NONE:
                position_cursor();
                break;
        }
        ed.needs_redraw = REDRAW_NONE;

        // Wait for key
        while (!api->has_key()) {
            api->yield();
        }

        int key = api->getc();

        switch (ed.mode) {
            case MODE_NORMAL:
            case MODE_OPERATOR_PENDING:
                handle_normal_mode(key);
                break;
            case MODE_INSERT:
                handle_insert_mode(key);
                break;
            case MODE_COMMAND:
                handle_command_mode(key);
                break;
        }
    }

    // Cleanup
    gap_free(&ed.buf);
    api->clear();

    return 0;
}
