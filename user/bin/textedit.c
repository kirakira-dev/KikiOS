/*
 * KikiOS TextEdit
 *
 * Simple text editor in a window. No modes, just type.
 * Usage: textedit [filename]
 */

#include "../lib/kiki.h"
#include "../lib/gfx.h"

static kapi_t *api;
static int window_id = -1;
static uint32_t *win_buffer;
static int win_w, win_h;
static gfx_ctx_t gfx;

// Editor dimensions
#define WINDOW_W 500
#define WINDOW_H 350
#define TITLE_BAR_H 18
#define GUTTER_W 40      // Line number gutter width
#define CONTENT_X (GUTTER_W + 4)
#define CONTENT_Y 4
#define CHAR_W 8
#define CHAR_H 16

// Modern color palette
#define COLOR_BG          0x00FFFFFF  // White background
#define COLOR_GUTTER_BG   0x00F5F5F5
#define COLOR_GUTTER_FG   0x00AAAAAA
#define COLOR_GUTTER_SEP  0x00E0E0E0
#define COLOR_STATUS_BG   0x00F0F0F0
#define COLOR_STATUS_FG   0x00666666
#define COLOR_CURSOR      0x00007AFF  // Blue cursor
#define COLOR_SELECTION   0x00E5F0FF  // Light blue selection bg

// Syntax highlighting (VS Code inspired)
#define COLOR_KEYWORD     0x00AF00DB  // Purple
#define COLOR_COMMENT     0x00008000  // Green
#define COLOR_STRING      0x00A31515  // Red/brown
#define COLOR_NUMBER      0x00098658  // Teal
#define COLOR_TEXT        0x00333333  // Dark gray text

// Text buffer
#define MAX_LINES 256
#define MAX_LINE_LEN 256
#define MAX_TEXT_SIZE (MAX_LINES * MAX_LINE_LEN)

static char text_buffer[MAX_TEXT_SIZE];
static int text_len = 0;
static int cursor_pos = 0;
static int scroll_offset = 0;  // First visible line

// Current file
static char current_file[256];
static int modified = 0;

// Save As modal state
static int save_as_mode = 0;
static char save_as_buf[256];
static int save_as_len = 0;

// Unsaved changes dialog state
static int confirm_close_mode = 0;  // 0=not showing, 1=showing
static int confirm_close_hover = -1;  // Which button is hovered (-1, 0, 1, 2)
static int pending_close = 0;  // Set to 1 when we need to close after save

// Syntax highlighting
static int syntax_c = 0;  // Is this a .c or .h file?

// C keywords
static const char *c_keywords[] = {
    "auto", "break", "case", "char", "const", "continue", "default", "do",
    "double", "else", "enum", "extern", "float", "for", "goto", "if",
    "int", "long", "register", "return", "short", "signed", "sizeof", "static",
    "struct", "switch", "typedef", "union", "unsigned", "void", "volatile", "while",
    "uint8_t", "uint16_t", "uint32_t", "uint64_t", "int8_t", "int16_t", "int32_t", "int64_t",
    "size_t", "NULL", "true", "false",
    0
};

// Visible area
static int visible_cols;
static int visible_rows;

// Check if filename ends with extension
static int ends_with(const char *str, const char *suffix) {
    int str_len = strlen(str);
    int suf_len = strlen(suffix);
    if (suf_len > str_len) return 0;
    for (int i = 0; i < suf_len; i++) {
        if (str[str_len - suf_len + i] != suffix[i]) return 0;
    }
    return 1;
}

static void detect_syntax(const char *filename) {
    syntax_c = 0;
    if (!filename || !filename[0]) return;

    if (ends_with(filename, ".c") || ends_with(filename, ".h") ||
        ends_with(filename, ".C") || ends_with(filename, ".H")) {
        syntax_c = 1;
    }
}

// ============ Drawing Helpers (macros wrapping gfx lib) ============

#define buf_fill_rect(x, y, w, h, c)     gfx_fill_rect(&gfx, x, y, w, h, c)
#define buf_draw_char(x, y, ch, fg, bg)  gfx_draw_char(&gfx, x, y, ch, fg, bg)
#define buf_draw_string(x, y, s, fg, bg) gfx_draw_string(&gfx, x, y, s, fg, bg)
#define buf_fill_rounded(x, y, w, h, r, c) gfx_fill_rounded_rect(&gfx, x, y, w, h, r, c)
#define buf_draw_rounded(x, y, w, h, r, c) gfx_draw_rounded_rect(&gfx, x, y, w, h, r, c)

// ============ Text Buffer Helpers ============

// Get line number and column from cursor position
static void cursor_to_line_col(int pos, int *line, int *col) {
    *line = 0;
    *col = 0;
    for (int i = 0; i < pos && i < text_len; i++) {
        if (text_buffer[i] == '\n') {
            (*line)++;
            *col = 0;
        } else {
            (*col)++;
        }
    }
}

// Get cursor position from line and column
static int line_col_to_cursor(int line, int col) {
    int current_line = 0;
    int current_col = 0;
    int i;

    for (i = 0; i < text_len; i++) {
        if (current_line == line && current_col == col) {
            return i;
        }
        if (text_buffer[i] == '\n') {
            if (current_line == line) {
                // Requested column is past end of line
                return i;
            }
            current_line++;
            current_col = 0;
        } else {
            current_col++;
        }
    }

    // End of buffer
    return i;
}

// Get start of line containing pos
static int line_start(int pos) {
    while (pos > 0 && text_buffer[pos - 1] != '\n') {
        pos--;
    }
    return pos;
}

// Get end of line containing pos
static int line_end(int pos) {
    while (pos < text_len && text_buffer[pos] != '\n') {
        pos++;
    }
    return pos;
}

// Count total lines
static int count_lines(void) {
    int lines = 1;
    for (int i = 0; i < text_len; i++) {
        if (text_buffer[i] == '\n') lines++;
    }
    return lines;
}

// Insert character at cursor
static void insert_char(char c) {
    if (text_len >= MAX_TEXT_SIZE - 1) return;

    // Shift everything after cursor
    for (int i = text_len; i > cursor_pos; i--) {
        text_buffer[i] = text_buffer[i - 1];
    }
    text_buffer[cursor_pos] = c;
    text_len++;
    cursor_pos++;
    modified = 1;
}

// Delete character before cursor (backspace)
static void delete_char_before(void) {
    if (cursor_pos == 0) return;

    // Shift everything after cursor back
    for (int i = cursor_pos - 1; i < text_len - 1; i++) {
        text_buffer[i] = text_buffer[i + 1];
    }
    text_len--;
    cursor_pos--;
    modified = 1;
}

// Delete character at cursor (delete key)
static void delete_char_at(void) {
    if (cursor_pos >= text_len) return;

    for (int i = cursor_pos; i < text_len - 1; i++) {
        text_buffer[i] = text_buffer[i + 1];
    }
    text_len--;
    modified = 1;
}

// ============ File Operations ============

static void load_file(const char *path) {
    detect_syntax(path);

    void *file = api->open(path);
    if (!file) {
        text_len = 0;
        cursor_pos = 0;
        return;
    }

    if (api->is_dir(file)) {
        text_len = 0;
        cursor_pos = 0;
        return;
    }

    int bytes = api->read(file, text_buffer, MAX_TEXT_SIZE - 1, 0);
    if (bytes > 0) {
        text_len = bytes;
        text_buffer[text_len] = '\0';
    } else {
        text_len = 0;
    }
    cursor_pos = 0;
    modified = 0;
}

static int save_failed = 0;  // Show error in status bar

static void open_save_as(void) {
    save_as_mode = 1;
    save_as_len = 0;
    save_as_buf[0] = '\0';
    // Pre-fill with current filename if exists
    if (current_file[0]) {
        for (int i = 0; current_file[i] && i < 255; i++) {
            save_as_buf[i] = current_file[i];
            save_as_len++;
        }
        save_as_buf[save_as_len] = '\0';
    }
}

static void do_save(const char *path) {
    void *file = api->open(path);
    if (!file) {
        file = api->create(path);
    }
    if (!file) {
        save_failed = 1;
        return;
    }

    api->write(file, text_buffer, text_len);

    // Update current filename
    int i;
    for (i = 0; path[i] && i < 255; i++) {
        current_file[i] = path[i];
    }
    current_file[i] = '\0';

    // Update syntax highlighting for new filename
    detect_syntax(current_file);

    // Update window title
    api->window_set_title(window_id, current_file);

    modified = 0;
    save_failed = 0;
}

static void save_file(void) {
    if (current_file[0] == '\0') {
        // No filename - open Save As dialog
        open_save_as();
        return;
    }

    do_save(current_file);
}

// ============ Drawing ============

static void draw_save_as_modal(void) {
    // Modal dimensions
    int modal_w = 320;
    int modal_h = 100;
    int modal_x = (win_w - modal_w) / 2;
    int modal_y = (win_h - modal_h) / 2;

    // Draw shadow
    buf_fill_rounded(modal_x + 4, modal_y + 4, modal_w, modal_h, 10, 0x00666666);

    // Draw modal background with rounded corners
    buf_fill_rounded(modal_x, modal_y, modal_w, modal_h, 10, COLOR_BG);
    buf_draw_rounded(modal_x, modal_y, modal_w, modal_h, 10, COLOR_GUTTER_SEP);

    // Draw title
    buf_draw_string(modal_x + 16, modal_y + 14, "Save As:", COLOR_TEXT, COLOR_BG);

    // Draw text input box with rounded corners
    int input_x = modal_x + 16;
    int input_y = modal_y + 36;
    int input_w = modal_w - 32;
    int input_h = 28;

    buf_fill_rounded(input_x, input_y, input_w, input_h, 6, COLOR_BG);
    buf_draw_rounded(input_x, input_y, input_w, input_h, 6, COLOR_GUTTER_SEP);

    // Draw filename text
    buf_draw_string(input_x + 8, input_y + 6, save_as_buf, COLOR_TEXT, COLOR_BG);

    // Draw blue bar cursor
    int cursor_x = input_x + 8 + save_as_len * CHAR_W;
    buf_fill_rect(cursor_x, input_y + 4, 2, CHAR_H, COLOR_CURSOR);

    // Draw hint
    buf_draw_string(modal_x + 16, modal_y + 74, "Enter=Save  Esc=Cancel", COLOR_STATUS_FG, COLOR_BG);
}

static void draw_confirm_close_modal(void) {
    // Modal dimensions
    int modal_w = 340;
    int modal_h = 120;
    int modal_x = (win_w - modal_w) / 2;
    int modal_y = (win_h - modal_h) / 2;

    // Draw shadow
    buf_fill_rounded(modal_x + 4, modal_y + 4, modal_w, modal_h, 10, 0x00666666);

    // Draw modal background with rounded corners
    buf_fill_rounded(modal_x, modal_y, modal_w, modal_h, 10, COLOR_BG);
    buf_draw_rounded(modal_x, modal_y, modal_w, modal_h, 10, COLOR_GUTTER_SEP);

    // Draw message
    buf_draw_string(modal_x + 20, modal_y + 16, "You have unsaved changes.", COLOR_TEXT, COLOR_BG);
    buf_draw_string(modal_x + 20, modal_y + 36, "Save before closing?", COLOR_STATUS_FG, COLOR_BG);

    // Button dimensions
    int btn_w = 90;
    int btn_h = 28;
    int btn_y = modal_y + 70;
    int btn_spacing = 10;
    int total_btn_w = 3 * btn_w + 2 * btn_spacing;
    int btn_start_x = modal_x + (modal_w - total_btn_w) / 2;

    // Draw buttons: [Save] [Don't Save] [Cancel]
    const char *labels[] = {"Save", "Don't Save", "Cancel"};
    for (int i = 0; i < 3; i++) {
        int bx = btn_start_x + i * (btn_w + btn_spacing);

        // Button colors - save button is blue, others are gray
        uint32_t bg, fg;
        if (i == 0) {
            // Save button - blue (primary action)
            bg = (i == confirm_close_hover) ? 0x00005FCC : COLOR_CURSOR;
            fg = COLOR_BG;
        } else if (i == confirm_close_hover) {
            bg = COLOR_GUTTER_BG;
            fg = COLOR_TEXT;
        } else {
            bg = COLOR_BG;
            fg = COLOR_TEXT;
        }

        buf_fill_rounded(bx, btn_y, btn_w, btn_h, 6, bg);
        buf_draw_rounded(bx, btn_y, btn_w, btn_h, 6, i == 0 ? COLOR_CURSOR : COLOR_GUTTER_SEP);

        // Center label
        int label_len = strlen(labels[i]);
        int label_x = bx + (btn_w - label_len * CHAR_W) / 2;
        int label_y = btn_y + (btn_h - CHAR_H) / 2;
        buf_draw_string(label_x, label_y, labels[i], fg, bg);
    }
}

// Draw a line number right-aligned in the gutter
static void draw_line_number(int screen_row, int line_num) {
    char num_str[8];
    int n = line_num;
    int len = 0;

    // Convert number to string
    if (n == 0) {
        num_str[len++] = '0';
    } else {
        char tmp[8];
        int ti = 0;
        while (n > 0) {
            tmp[ti++] = '0' + (n % 10);
            n /= 10;
        }
        while (ti > 0) {
            num_str[len++] = tmp[--ti];
        }
    }
    num_str[len] = '\0';

    // Right-align: draw at (GUTTER_W - 8 - len * CHAR_W)
    int x = GUTTER_W - 8 - len * CHAR_W;
    int y = CONTENT_Y + screen_row * CHAR_H;
    buf_draw_string(x, y, num_str, COLOR_GUTTER_FG, COLOR_GUTTER_BG);
}

static void draw_all(void) {
    // Clear background (white)
    buf_fill_rect(0, 0, win_w, win_h, COLOR_BG);

    // Draw gutter background
    buf_fill_rect(0, 0, GUTTER_W, win_h, COLOR_GUTTER_BG);
    // Gutter separator line (subtle)
    buf_fill_rect(GUTTER_W - 1, 0, 1, win_h, COLOR_GUTTER_SEP);

    // Get cursor line/col for scroll adjustment
    int cursor_line, cursor_col;
    cursor_to_line_col(cursor_pos, &cursor_line, &cursor_col);

    // Adjust scroll to keep cursor visible
    if (cursor_line < scroll_offset) {
        scroll_offset = cursor_line;
    } else if (cursor_line >= scroll_offset + visible_rows) {
        scroll_offset = cursor_line - visible_rows + 1;
    }

    // Draw line numbers for visible rows
    int total_lines = count_lines();
    for (int row = 0; row < visible_rows; row++) {
        int line_num = scroll_offset + row + 1;  // 1-indexed
        if (line_num <= total_lines) {
            draw_line_number(row, line_num);
        }
    }

    // Draw text with syntax highlighting
    int current_line = 0;
    int col = 0;

    // Syntax highlighting state
    int in_line_comment = 0;
    int in_block_comment = 0;
    int in_string = 0;
    char string_char = 0;

    for (int i = 0; i <= text_len; i++) {
        // Draw cursor (modern blue bar)
        if (i == cursor_pos && current_line >= scroll_offset && current_line < scroll_offset + visible_rows) {
            int cy = CONTENT_Y + (current_line - scroll_offset) * CHAR_H;
            int cx = CONTENT_X + col * CHAR_W;
            // 2-pixel wide blue bar cursor
            buf_fill_rect(cx, cy, 2, CHAR_H, COLOR_CURSOR);
        }

        if (i >= text_len) break;

        char c = text_buffer[i];

        if (c == '\n') {
            in_line_comment = 0;
            current_line++;
            col = 0;
        } else {
            // Determine color for this character
            uint32_t fg_color = COLOR_TEXT;

            if (syntax_c) {
                // Check for comment start/end
                if (!in_string && !in_line_comment && !in_block_comment) {
                    if (c == '/' && i + 1 < text_len && text_buffer[i + 1] == '/') {
                        in_line_comment = 1;
                    } else if (c == '/' && i + 1 < text_len && text_buffer[i + 1] == '*') {
                        in_block_comment = 1;
                    }
                }

                // Check for string start
                if (!in_line_comment && !in_block_comment && !in_string) {
                    if (c == '"' || c == '\'') {
                        in_string = 1;
                        string_char = c;
                    }
                } else if (in_string && c == string_char) {
                    // Check for escape
                    if (i == 0 || text_buffer[i - 1] != '\\') {
                        // String ends after this char, color it as string
                        fg_color = COLOR_STRING;
                        // Draw this char, then reset
                        if (current_line >= scroll_offset && current_line < scroll_offset + visible_rows) {
                            if (i != cursor_pos) {
                                int cy = CONTENT_Y + (current_line - scroll_offset) * CHAR_H;
                                int cx = CONTENT_X + col * CHAR_W;
                                if (cx + CHAR_W <= win_w - CONTENT_X) {
                                    buf_draw_char(cx, cy, c, fg_color, COLOR_BG);
                                }
                            }
                        }
                        col++;
                        in_string = 0;
                        continue;
                    }
                }

                // Check for block comment end
                if (in_block_comment && c == '*' && i + 1 < text_len && text_buffer[i + 1] == '/') {
                    // Will end after next char
                }
                if (in_block_comment && i > 0 && text_buffer[i - 1] == '*' && c == '/') {
                    in_block_comment = 0;
                }

                // Set color based on state
                if (in_line_comment || in_block_comment) {
                    fg_color = COLOR_COMMENT;
                } else if (in_string) {
                    fg_color = COLOR_STRING;
                } else {
                    // Check for keywords
                    int is_word_start = (i == 0 || !((text_buffer[i-1] >= 'a' && text_buffer[i-1] <= 'z') ||
                                                     (text_buffer[i-1] >= 'A' && text_buffer[i-1] <= 'Z') ||
                                                     (text_buffer[i-1] >= '0' && text_buffer[i-1] <= '9') ||
                                                     text_buffer[i-1] == '_'));
                    if (is_word_start && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')) {
                        // Extract word
                        char word[32];
                        int wi = 0;
                        for (int j = i; j < text_len && wi < 31; j++) {
                            char wc = text_buffer[j];
                            if ((wc >= 'a' && wc <= 'z') || (wc >= 'A' && wc <= 'Z') ||
                                (wc >= '0' && wc <= '9') || wc == '_') {
                                word[wi++] = wc;
                            } else {
                                break;
                            }
                        }
                        word[wi] = '\0';

                        // Check if it's a keyword
                        for (int k = 0; c_keywords[k]; k++) {
                            if (strcmp(word, c_keywords[k]) == 0) {
                                fg_color = COLOR_KEYWORD;
                                break;
                            }
                        }
                    }

                    // Check for numbers
                    if (c >= '0' && c <= '9') {
                        int is_num_start = (i == 0 || !((text_buffer[i-1] >= 'a' && text_buffer[i-1] <= 'z') ||
                                                        (text_buffer[i-1] >= 'A' && text_buffer[i-1] <= 'Z') ||
                                                        text_buffer[i-1] == '_'));
                        if (is_num_start) {
                            fg_color = COLOR_NUMBER;
                        }
                    }
                }
            }

            // Only draw if in visible area and not at cursor (cursor already drawn)
            if (current_line >= scroll_offset && current_line < scroll_offset + visible_rows) {
                if (i != cursor_pos) {
                    int cy = CONTENT_Y + (current_line - scroll_offset) * CHAR_H;
                    int cx = CONTENT_X + col * CHAR_W;
                    if (cx + CHAR_W <= win_w - CONTENT_X) {
                        buf_draw_char(cx, cy, c, fg_color, COLOR_BG);
                    }
                }
            }
            col++;
        }
    }

    // Draw modern status bar at bottom
    int status_y = win_h - CHAR_H - 4;
    buf_fill_rect(0, status_y - 2, win_w, CHAR_H + 6, COLOR_STATUS_BG);

    // Status text: filename and position
    char status[64];
    int si = 0;

    // Save failed indicator
    if (save_failed) {
        const char *err = "[No filename] ";
        for (int i = 0; err[i]; i++) status[si++] = err[i];
    }

    // Modified indicator
    if (modified) {
        status[si++] = '*';
    }

    // Filename
    const char *fname = current_file[0] ? current_file : "untitled";
    for (int i = 0; fname[i] && si < 40; i++) {
        status[si++] = fname[i];
    }

    status[si++] = ' ';
    status[si++] = '-';
    status[si++] = ' ';
    status[si++] = 'L';

    // Line number
    char num[8];
    int n = cursor_line + 1;
    int ni = 0;
    if (n == 0) {
        num[ni++] = '0';
    } else {
        char tmp[8];
        int ti = 0;
        while (n > 0) {
            tmp[ti++] = '0' + (n % 10);
            n /= 10;
        }
        while (ti > 0) {
            num[ni++] = tmp[--ti];
        }
    }
    for (int i = 0; i < ni && si < 60; i++) {
        status[si++] = num[i];
    }

    status[si++] = ':';

    // Column number
    n = cursor_col + 1;
    ni = 0;
    if (n == 0) {
        num[ni++] = '0';
    } else {
        char tmp[8];
        int ti = 0;
        while (n > 0) {
            tmp[ti++] = '0' + (n % 10);
            n /= 10;
        }
        while (ti > 0) {
            num[ni++] = tmp[--ti];
        }
    }
    for (int i = 0; i < ni && si < 63; i++) {
        status[si++] = num[i];
    }

    status[si] = '\0';

    buf_draw_string(8, status_y, status, COLOR_STATUS_FG, COLOR_STATUS_BG);

    // Draw Save As modal if active
    if (save_as_mode) {
        draw_save_as_modal();
    }

    // Draw confirm close modal if active
    if (confirm_close_mode) {
        draw_confirm_close_modal();
    }

    api->window_invalidate(window_id);
}

// ============ Input Handling ============

static void handle_save_as_key(int key) {
    switch (key) {
        case '\r':
        case '\n':
            // Confirm save
            if (save_as_len > 0) {
                save_as_buf[save_as_len] = '\0';
                do_save(save_as_buf);
                save_as_mode = 0;
            }
            break;

        case 0x1B: // Escape - cancel
            save_as_mode = 0;
            break;

        case 8: // Backspace
            if (save_as_len > 0) {
                save_as_len--;
                save_as_buf[save_as_len] = '\0';
            }
            break;

        default:
            // Add printable characters
            if (key >= 32 && key < 127 && save_as_len < 250) {
                save_as_buf[save_as_len++] = (char)key;
                save_as_buf[save_as_len] = '\0';
            }
            break;
    }
}

static void handle_key(int key) {
    // If Save As modal is open, handle those keys
    if (save_as_mode) {
        handle_save_as_key(key);
        return;
    }

    int line, col;

    switch (key) {
        case '\r':
        case '\n':
            insert_char('\n');
            break;

        case 8:   // Backspace
            delete_char_before();
            break;

        case 0x106: // Delete key
            delete_char_at();
            break;

        case 0x1B: // Escape - could use for menu later
            break;

        case '\t': // Tab - insert 4 spaces
            insert_char(' ');
            insert_char(' ');
            insert_char(' ');
            insert_char(' ');
            break;

        // Arrow keys (special codes from keyboard driver)
        case 0x100: // Up
            cursor_to_line_col(cursor_pos, &line, &col);
            if (line > 0) {
                cursor_pos = line_col_to_cursor(line - 1, col);
            }
            break;

        case 0x101: // Down
            cursor_to_line_col(cursor_pos, &line, &col);
            cursor_pos = line_col_to_cursor(line + 1, col);
            if (cursor_pos > text_len) cursor_pos = text_len;
            break;

        case 0x102: // Left
            if (cursor_pos > 0) cursor_pos--;
            break;

        case 0x103: // Right
            if (cursor_pos < text_len) cursor_pos++;
            break;

        case 0x104: // Home
            cursor_pos = line_start(cursor_pos);
            break;

        case 0x105: // End
            cursor_pos = line_end(cursor_pos);
            break;

        case 19: // Ctrl+S
            save_file();
            break;

        default:
            if (key >= 32 && key < 127) {
                char c = (char)key;

                // Auto-close brackets and quotes
                char close_char = 0;
                switch (c) {
                    case '(': close_char = ')'; break;
                    case '[': close_char = ']'; break;
                    case '{': close_char = '}'; break;
                    case '"': close_char = '"'; break;
                    case '\'': close_char = '\''; break;
                }

                if (close_char) {
                    insert_char(c);
                    insert_char(close_char);
                    cursor_pos--;  // Move cursor between the pair
                } else {
                    insert_char(c);
                }
            }
            break;
    }
}

// ============ Main ============

int main(kapi_t *kapi, int argc, char **argv) {
    api = kapi;

    // Initialize
    text_len = 0;
    cursor_pos = 0;
    scroll_offset = 0;
    modified = 0;
    current_file[0] = '\0';

    // Load file if specified
    if (argc > 1) {
        // Copy filename
        int i;
        for (i = 0; argv[1][i] && i < 255; i++) {
            current_file[i] = argv[1][i];
        }
        current_file[i] = '\0';
        load_file(current_file);
    }

    // Check for window API
    if (!api->window_create) {
        api->puts("textedit: window API not available (run from desktop)\n");
        return 1;
    }

    // Create window
    const char *title = current_file[0] ? current_file : "TextEdit";
    window_id = api->window_create(50, 50, WINDOW_W, WINDOW_H + TITLE_BAR_H, title);
    if (window_id < 0) {
        api->puts("textedit: failed to create window\n");
        return 1;
    }

    // Get buffer
    win_buffer = api->window_get_buffer(window_id, &win_w, &win_h);
    if (!win_buffer) {
        api->puts("textedit: failed to get window buffer\n");
        api->window_destroy(window_id);
        return 1;
    }

    // Initialize graphics context
    gfx_init(&gfx, win_buffer, win_w, win_h, api->font_data);

    // Calculate visible area
    visible_cols = (win_w - CONTENT_X * 2) / CHAR_W;
    visible_rows = (win_h - CONTENT_Y * 2 - CHAR_H - 4) / CHAR_H;  // Account for status bar

    // Initial draw
    draw_all();

    // Event loop
    int running = 1;
    while (running) {
        int event_type, data1, data2, data3;
        while (api->window_poll_event(window_id, &event_type, &data1, &data2, &data3)) {
            switch (event_type) {
                case WIN_EVENT_CLOSE:
                    if (modified) {
                        // Show confirm dialog instead of closing
                        confirm_close_mode = 1;
                        confirm_close_hover = -1;
                        draw_all();
                    } else {
                        running = 0;
                    }
                    break;

                case WIN_EVENT_KEY:
                    if (confirm_close_mode) {
                        // Handle confirm dialog keys
                        if (data1 == 0x1B) {  // Escape = Cancel
                            confirm_close_mode = 0;
                        } else if (data1 == 's' || data1 == 'S' || data1 == '\r' || data1 == '\n') {
                            // Save
                            confirm_close_mode = 0;
                            pending_close = 1;
                            save_file();
                            if (!save_as_mode && !modified) {
                                running = 0;
                            }
                        } else if (data1 == 'd' || data1 == 'D' || data1 == 'n' || data1 == 'N') {
                            // Don't Save
                            confirm_close_mode = 0;
                            running = 0;
                        }
                        draw_all();
                    } else {
                        handle_key(data1);
                        // Check if we were waiting to close after save
                        if (pending_close && !save_as_mode && !modified) {
                            running = 0;
                        }
                        draw_all();
                    }
                    break;

                case WIN_EVENT_MOUSE_DOWN:
                    if (confirm_close_mode) {
                        int mx = data1, my = data2;
                        // Check button clicks
                        int modal_w = 320;
                        int modal_h = 100;
                        int modal_x = (win_w - modal_w) / 2;
                        int modal_y = (win_h - modal_h) / 2;
                        int btn_w = 85;
                        int btn_h = 24;
                        int btn_y = modal_y + 60;
                        int btn_spacing = 10;
                        int total_btn_w = 3 * btn_w + 2 * btn_spacing;
                        int btn_start_x = modal_x + (modal_w - total_btn_w) / 2;

                        for (int i = 0; i < 3; i++) {
                            int bx = btn_start_x + i * (btn_w + btn_spacing);
                            if (mx >= bx && mx < bx + btn_w && my >= btn_y && my < btn_y + btn_h) {
                                if (i == 0) {  // Save
                                    confirm_close_mode = 0;
                                    pending_close = 1;
                                    save_file();
                                    if (!save_as_mode && !modified) {
                                        running = 0;
                                    }
                                } else if (i == 1) {  // Don't Save
                                    confirm_close_mode = 0;
                                    running = 0;
                                } else {  // Cancel
                                    confirm_close_mode = 0;
                                }
                                break;
                            }
                        }
                        draw_all();
                    }
                    break;

                case WIN_EVENT_MOUSE_MOVE:
                    if (confirm_close_mode) {
                        int mx = data1, my = data2;
                        // Check button hover
                        int modal_w = 320;
                        int modal_h = 100;
                        int modal_x = (win_w - modal_w) / 2;
                        int modal_y = (win_h - modal_h) / 2;
                        int btn_w = 85;
                        int btn_h = 24;
                        int btn_y = modal_y + 60;
                        int btn_spacing = 10;
                        int total_btn_w = 3 * btn_w + 2 * btn_spacing;
                        int btn_start_x = modal_x + (modal_w - total_btn_w) / 2;

                        int new_hover = -1;
                        for (int i = 0; i < 3; i++) {
                            int bx = btn_start_x + i * (btn_w + btn_spacing);
                            if (mx >= bx && mx < bx + btn_w && my >= btn_y && my < btn_y + btn_h) {
                                new_hover = i;
                                break;
                            }
                        }
                        if (new_hover != confirm_close_hover) {
                            confirm_close_hover = new_hover;
                            draw_all();
                        }
                    }
                    break;

                case WIN_EVENT_RESIZE:
                    // Re-fetch buffer with new dimensions
                    win_buffer = api->window_get_buffer(window_id, &win_w, &win_h);
                    gfx_init(&gfx, win_buffer, win_w, win_h, api->font_data);
                    // Recalculate visible area
                    visible_cols = (win_w - CONTENT_X * 2) / CHAR_W;
                    visible_rows = (win_h - CONTENT_Y * 2 - CHAR_H - 4) / CHAR_H;
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
