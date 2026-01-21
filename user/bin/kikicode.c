/*
 * VibeCode - IDE for KikiOS
 *
 * Left sidebar: file tree
 * Center: code editor with syntax highlighting (C, Python)
 * Bottom: output panel (stdout from running programs)
 * Toolbar: Run button
 */

#include "../lib/kiki.h"
#include "../lib/gfx.h"

static kapi_t *api;
static int window_id = -1;
static uint32_t *win_buffer;
static int win_w, win_h;
static gfx_ctx_t gfx;

// Layout constants
#define WINDOW_W 700
#define WINDOW_H 500
#define TITLE_BAR_H 18
#define TOOLBAR_H 24
#define SIDEBAR_W 160
#define OUTPUT_H 100
#define CHAR_W 8
#define CHAR_H 16

// Colors
#define COLOR_BG        0x00FFFFFF
#define COLOR_FG        0x00000000
#define COLOR_SIDEBAR   0x00EEEEEE
#define COLOR_TOOLBAR   0x00DDDDDD
#define COLOR_OUTPUT_BG 0x00FFFFFF
#define COLOR_OUTPUT_FG 0x00000000
#define COLOR_SELECTED  0x00000000
#define COLOR_SEL_TEXT  0x00FFFFFF
#define COLOR_GUTTER_BG 0x00EEEEEE
#define COLOR_GUTTER_FG 0x00888888
#define COLOR_BTN_BG    0x00CCCCCC
#define COLOR_BTN_HOVER 0x00AAAAAA

// Syntax colors
#define COLOR_KEYWORD   0x000000AA
#define COLOR_COMMENT   0x00008800
#define COLOR_STRING    0x00AA0000
#define COLOR_NUMBER    0x00AA00AA
#define COLOR_FUNCTION  0x00007070
#define COLOR_DECORATOR 0x00AA5500

// File tree
#define MAX_FILES 64
typedef struct {
    char name[64];
    char path[256];
    uint8_t is_dir;
    int depth;      // Indentation level
    int expanded;   // For directories
} file_entry_t;

static file_entry_t files[MAX_FILES];
static int file_count = 0;
static int selected_file = -1;
static int file_scroll = 0;

// Current directory for file tree
static char tree_root[256] = "/";

// Editor state
#define MAX_TEXT_SIZE (64 * 1024)
static char text_buffer[MAX_TEXT_SIZE];
static int text_len = 0;
static int cursor_pos = 0;
static int scroll_line = 0;
static int scroll_col = 0;
static int modified = 0;

// Current file being edited
static char current_file[256] = "";

// Syntax mode
#define SYNTAX_NONE 0
#define SYNTAX_C    1
#define SYNTAX_PY   2
static int syntax_mode = SYNTAX_NONE;

// Output panel
#define MAX_OUTPUT 4096
static char output_buffer[MAX_OUTPUT];
static int output_len = 0;
static int output_scroll = 0;

// UI state
static int hover_run = 0;
static int hover_save = 0;
static int hover_new = 0;
static int hover_help = 0;
static int show_help = 0;
static int help_scroll = 0;

// Welcome screen state
static int show_welcome = 1;            // Show welcome on startup
static int welcome_hover = -1;          // 0=Create, 1=Open
static int welcome_input_mode = 0;      // 0=none, 1=Create project, 2=Open project
static char welcome_input[128] = "";
static int welcome_input_len = 0;

// New file mode (inline editing in sidebar)
static int new_file_mode = 0;           // 1 = typing new filename
static char new_file_name[64] = "";
static int new_file_name_len = 0;

// C keywords
static const char *c_keywords[] = {
    "auto", "break", "case", "char", "const", "continue", "default", "do",
    "double", "else", "enum", "extern", "float", "for", "goto", "if",
    "int", "long", "register", "return", "short", "signed", "sizeof", "static",
    "struct", "switch", "typedef", "union", "unsigned", "void", "volatile", "while",
    "uint8_t", "uint16_t", "uint32_t", "uint64_t", "int8_t", "int16_t", "int32_t", "int64_t",
    "size_t", "NULL", "true", "false", "#include", "#define", "#ifdef", "#ifndef",
    "#endif", "#else", "#pragma",
    0
};

// Python keywords
static const char *py_keywords[] = {
    "and", "as", "assert", "async", "await", "break", "class", "continue",
    "def", "del", "elif", "else", "except", "finally", "for", "from",
    "global", "if", "import", "in", "is", "lambda", "None", "nonlocal",
    "not", "or", "pass", "raise", "return", "try", "while", "with", "yield",
    "True", "False", "print", "len", "range", "str", "int", "float", "list",
    "dict", "set", "tuple", "open", "self",
    0
};

// Forward declarations
static void output_clear(void);
static void output_append(const char *s);
static void refresh_file_tree(void);

// ============ Drawing Helpers ============

#define buf_fill_rect(x, y, w, h, c)     gfx_fill_rect(&gfx, x, y, w, h, c)
#define buf_draw_char(x, y, ch, fg, bg)  gfx_draw_char(&gfx, x, y, ch, fg, bg)
#define buf_draw_string(x, y, s, fg, bg) gfx_draw_string(&gfx, x, y, s, fg, bg)
#define buf_draw_rect(x, y, w, h, c)     gfx_draw_rect(&gfx, x, y, w, h, c)

// ============ Utility Functions ============

static int ends_with(const char *str, const char *suffix) {
    int str_len = strlen(str);
    int suf_len = strlen(suffix);
    if (suf_len > str_len) return 0;
    for (int i = 0; i < suf_len; i++) {
        char a = str[str_len - suf_len + i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return 0;
    }
    return 1;
}

static void detect_syntax(const char *filename) {
    syntax_mode = SYNTAX_NONE;
    if (!filename || !filename[0]) return;

    if (ends_with(filename, ".c") || ends_with(filename, ".h")) {
        syntax_mode = SYNTAX_C;
    } else if (ends_with(filename, ".py")) {
        syntax_mode = SYNTAX_PY;
    }
}

static int is_keyword(const char *word, const char **keywords) {
    for (int i = 0; keywords[i]; i++) {
        if (strcmp(word, keywords[i]) == 0) return 1;
    }
    return 0;
}

static int is_word_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

// ============ File Tree ============

static void load_directory(const char *path, int depth) {
    if (file_count >= MAX_FILES) return;

    void *dir = api->open(path);
    if (!dir || !api->is_dir(dir)) return;

    char name[64];
    uint8_t type;
    int idx = 0;

    while (file_count < MAX_FILES && api->readdir(dir, idx, name, sizeof(name), &type) == 0) {
        idx++;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

        file_entry_t *f = &files[file_count];
        strncpy_safe(f->name, name, sizeof(f->name));

        // Build full path
        if (strcmp(path, "/") == 0) {
            f->path[0] = '/';
            strncpy_safe(f->path + 1, name, sizeof(f->path) - 1);
        } else {
            strncpy_safe(f->path, path, sizeof(f->path));
            int plen = strlen(f->path);
            if (plen < (int)sizeof(f->path) - 1) {
                f->path[plen] = '/';
                strncpy_safe(f->path + plen + 1, name, sizeof(f->path) - plen - 1);
            }
        }

        f->is_dir = (type == 2);
        f->depth = depth;
        f->expanded = 0;
        file_count++;
    }
}

static void refresh_file_tree(void) {
    file_count = 0;
    load_directory(tree_root, 0);
}

// ============ Editor ============

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

static int line_col_to_cursor(int line, int col) {
    int current_line = 0;
    int current_col = 0;

    for (int i = 0; i < text_len; i++) {
        if (current_line == line && current_col == col) return i;
        if (text_buffer[i] == '\n') {
            if (current_line == line) return i;
            current_line++;
            current_col = 0;
        } else {
            current_col++;
        }
    }
    return text_len;
}

static int line_start(int pos) {
    while (pos > 0 && text_buffer[pos - 1] != '\n') pos--;
    return pos;
}

static int line_end(int pos) {
    while (pos < text_len && text_buffer[pos] != '\n') pos++;
    return pos;
}

static int count_lines(void) {
    int lines = 1;
    for (int i = 0; i < text_len; i++) {
        if (text_buffer[i] == '\n') lines++;
    }
    return lines;
}

static void insert_char(char c) {
    if (text_len >= MAX_TEXT_SIZE - 1) return;
    for (int i = text_len; i > cursor_pos; i--) {
        text_buffer[i] = text_buffer[i - 1];
    }
    text_buffer[cursor_pos] = c;
    text_len++;
    cursor_pos++;
    modified = 1;
}

static void delete_char_before(void) {
    if (cursor_pos == 0) return;
    for (int i = cursor_pos - 1; i < text_len - 1; i++) {
        text_buffer[i] = text_buffer[i + 1];
    }
    text_len--;
    cursor_pos--;
    modified = 1;
}

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
    void *file = api->open(path);
    if (!file || api->is_dir(file)) {
        text_len = 0;
        cursor_pos = 0;
        return;
    }

    int size = api->file_size(file);
    if (size > MAX_TEXT_SIZE - 1) size = MAX_TEXT_SIZE - 1;

    int bytes = api->read(file, text_buffer, size, 0);
    text_len = (bytes > 0) ? bytes : 0;
    text_buffer[text_len] = '\0';
    cursor_pos = 0;
    scroll_line = 0;
    modified = 0;

    strncpy_safe(current_file, path, sizeof(current_file));
    detect_syntax(current_file);
}

static void save_file(void) {
    // If no current file, trigger save-as mode to get a name first
    if (!current_file[0]) {
        if (text_len > 0) {
            // Content exists but no filename - save as mode (keeps buffer)
            new_file_mode = 2;
            new_file_name[0] = '\0';
            new_file_name_len = 0;
            output_clear();
            output_append("Enter filename in sidebar\n");
        }
        return;
    }

    // Always use create for writing (create overwrites or makes new)
    void *file = api->create(current_file);
    if (!file) {
        output_clear();
        output_append("Error: Could not save file\n");
        return;
    }

    api->write(file, text_buffer, text_len);
    modified = 0;

    // Feedback
    output_clear();
    output_append("Saved: ");
    output_append(current_file);
    output_append("\n");
}

// new_file_mode: 1 = new file (clear buffer), 2 = save as (keep buffer)
static void new_file(void) {
    show_welcome = 0;  // Dismiss welcome screen
    // Clear buffer for truly new file
    text_len = 0;
    text_buffer[0] = '\0';
    cursor_pos = 0;
    scroll_line = 0;
    scroll_col = 0;
    current_file[0] = '\0';
    modified = 0;
    syntax_mode = SYNTAX_NONE;
    // Now enter naming mode
    new_file_mode = 1;
    new_file_name[0] = '\0';
    new_file_name_len = 0;
}

static void create_new_file(void) {
    if (new_file_name_len == 0) {
        new_file_mode = 0;
        return;
    }

    // Build full path
    char path[256];
    int pi = 0;
    for (int i = 0; tree_root[i] && pi < 250; i++) {
        path[pi++] = tree_root[i];
    }
    if (pi > 0 && path[pi-1] != '/') path[pi++] = '/';
    for (int i = 0; new_file_name[i] && pi < 254; i++) {
        path[pi++] = new_file_name[i];
    }
    path[pi] = '\0';

    // Set as current file first
    strncpy_safe(current_file, path, sizeof(current_file));

    // Create the file with current editor content
    void *f = api->create(path);
    if (f) {
        // Write current content (may be empty or may have existing code)
        api->write(f, text_buffer, text_len);
        modified = 0;
        output_clear();
        output_append("Created: ");
        output_append(path);
        output_append("\n");
    } else {
        output_clear();
        output_append("Error: Could not create file\n");
    }

    // Detect syntax
    detect_syntax(path);

    // Refresh tree and exit new file mode
    refresh_file_tree();
    new_file_mode = 0;

    // Select the new file in the tree
    for (int i = 0; i < file_count; i++) {
        if (strcmp(files[i].path, path) == 0) {
            selected_file = i;
            break;
        }
    }
}

// ============ Output Panel ============

static void output_clear(void) {
    output_len = 0;
    output_scroll = 0;
}

static void output_append(const char *s) {
    while (*s && output_len < MAX_OUTPUT - 1) {
        output_buffer[output_len++] = *s++;
    }
    output_buffer[output_len] = '\0';
    output_scroll = 9999;  // Auto-scroll to bottom (clamped in draw)
}

static void output_append_char(char c) {
    if (output_len < MAX_OUTPUT - 1) {
        output_buffer[output_len++] = c;
        output_buffer[output_len] = '\0';
        output_scroll = 9999;  // Auto-scroll to bottom
    }
}

// Stdio hooks for capturing output
static void capture_putc(char c) {
    output_append_char(c);
}

static void capture_puts(const char *s) {
    output_append(s);
}

// ============ Run Code ============

static void run_current_file(void) {
    if (!current_file[0]) {
        output_clear();
        output_append("No file to run.\n");
        return;
    }

    // Save first
    if (modified) save_file();

    output_clear();

    if (syntax_mode == SYNTAX_C) {
        // Compile with TCC then run
        output_append("Compiling with TCC...\n");

        // Build output path: /tmp/out
        char out_path[64] = "/tmp/kikicode_out";

        // Set up stdio hooks to capture output
        api->stdio_putc = capture_putc;
        api->stdio_puts = capture_puts;

        // Run TCC
        char *tcc_argv[] = {"/bin/tcc", "-o", out_path, (char *)current_file};
        int result = api->exec_args("/bin/tcc", 4, tcc_argv);

        if (result == 0) {
            output_append("Running...\n\n");

            // Run the compiled program
            char *run_argv[] = {out_path};
            api->exec_args(out_path, 1, run_argv);

            output_append("\n\n[Program finished]");
        } else {
            output_append("\n[Compilation failed]");
        }

        // Clear hooks
        api->stdio_putc = 0;
        api->stdio_puts = 0;

        // Clean up
        api->delete(out_path);

    } else if (syntax_mode == SYNTAX_PY) {
        // Run with MicroPython
        output_append("Running with MicroPython...\n\n");

        // Set up stdio hooks
        api->stdio_putc = capture_putc;
        api->stdio_puts = capture_puts;

        char *py_argv[] = {"/bin/micropython", (char *)current_file};
        api->exec_args("/bin/micropython", 2, py_argv);

        api->stdio_putc = 0;
        api->stdio_puts = 0;

        output_append("\n\n[Program finished]");
    } else {
        output_append("Unknown file type. Save as .c or .py\n");
    }
}

// ============ Drawing ============

static void draw_toolbar(void) {
    int y = 0;

    // Background
    buf_fill_rect(0, y, win_w, TOOLBAR_H, COLOR_TOOLBAR);
    buf_fill_rect(0, y + TOOLBAR_H - 1, win_w, 1, COLOR_FG);

    // New button
    int btn_x = 4;
    uint32_t new_bg = hover_new ? COLOR_BTN_HOVER : COLOR_BTN_BG;
    buf_fill_rect(btn_x, y + 2, 48, 20, new_bg);
    buf_draw_rect(btn_x, y + 2, 48, 20, COLOR_FG);
    buf_draw_string(btn_x + 8, y + 4, "New", COLOR_FG, new_bg);

    // Save button
    btn_x = 56;
    uint32_t save_bg = hover_save ? COLOR_BTN_HOVER : COLOR_BTN_BG;
    buf_fill_rect(btn_x, y + 2, 48, 20, save_bg);
    buf_draw_rect(btn_x, y + 2, 48, 20, COLOR_FG);
    buf_draw_string(btn_x + 8, y + 4, "Save", COLOR_FG, save_bg);

    // Run button
    btn_x = 108;
    uint32_t run_bg = hover_run ? COLOR_BTN_HOVER : COLOR_BTN_BG;
    buf_fill_rect(btn_x, y + 2, 48, 20, run_bg);
    buf_draw_rect(btn_x, y + 2, 48, 20, COLOR_FG);
    buf_draw_string(btn_x + 12, y + 4, "Run", COLOR_FG, run_bg);

    // Help button (right side)
    int help_x = win_w - 52;
    uint32_t help_bg = hover_help ? COLOR_BTN_HOVER : COLOR_BTN_BG;
    buf_fill_rect(help_x, y + 2, 48, 20, help_bg);
    buf_draw_rect(help_x, y + 2, 48, 20, COLOR_FG);
    buf_draw_string(help_x + 8, y + 4, "Help", COLOR_FG, help_bg);

    // Current file name
    if (current_file[0]) {
        // Find basename
        const char *name = current_file;
        for (const char *p = current_file; *p; p++) {
            if (*p == '/') name = p + 1;
        }
        char title[64];
        int ti = 0;
        if (modified) title[ti++] = '*';
        while (*name && ti < 60) title[ti++] = *name++;
        title[ti] = '\0';

        buf_draw_string(170, y + 4, title, COLOR_FG, COLOR_TOOLBAR);
    }
}

static void draw_sidebar(void) {
    int x = 0;
    int y = TOOLBAR_H;
    int h = win_h - TOOLBAR_H - OUTPUT_H;

    // Background
    buf_fill_rect(x, y, SIDEBAR_W, h, COLOR_SIDEBAR);
    buf_fill_rect(x + SIDEBAR_W - 1, y, 1, h, COLOR_FG);

    // Header
    buf_draw_string(4, y + 2, "Files", COLOR_FG, COLOR_SIDEBAR);
    buf_fill_rect(0, y + 18, SIDEBAR_W - 1, 1, COLOR_FG);

    // File list
    int list_y = y + 20;
    int visible = (h - 20) / CHAR_H;

    // New file input at top of list
    if (new_file_mode) {
        buf_fill_rect(0, list_y, SIDEBAR_W - 1, CHAR_H, COLOR_WHITE);
        buf_draw_rect(0, list_y, SIDEBAR_W - 1, CHAR_H, COLOR_FG);
        buf_draw_string(4, list_y, new_file_name, COLOR_FG, COLOR_WHITE);
        // Cursor
        int cursor_x = 4 + new_file_name_len * CHAR_W;
        buf_fill_rect(cursor_x, list_y + 1, 2, CHAR_H - 2, COLOR_FG);
        list_y += CHAR_H;
        visible--;
    }

    for (int i = 0; i < visible && (file_scroll + i) < file_count; i++) {
        file_entry_t *f = &files[file_scroll + i];
        int idx = file_scroll + i;
        int item_y = list_y + i * CHAR_H;

        uint32_t bg = (idx == selected_file) ? COLOR_SELECTED : COLOR_SIDEBAR;
        uint32_t fg = (idx == selected_file) ? COLOR_SEL_TEXT : COLOR_FG;

        buf_fill_rect(0, item_y, SIDEBAR_W - 1, CHAR_H, bg);

        // Icon and name
        int text_x = 4 + f->depth * 12;
        char prefix = f->is_dir ? '+' : ' ';
        if (text_x < SIDEBAR_W - 16) {
            buf_draw_char(text_x, item_y, prefix, fg, bg);
            buf_draw_string(text_x + 10, item_y, f->name, fg, bg);
        }
    }
}

static void draw_editor(void) {
    int x = SIDEBAR_W;
    int y = TOOLBAR_H;
    int w = win_w - SIDEBAR_W;
    int h = win_h - TOOLBAR_H - OUTPUT_H;

    int gutter_w = 40;
    int content_x = x + gutter_w + 4;
    int content_y = y + 4;
    int visible_rows = (h - 8) / CHAR_H;
    int visible_cols = (w - gutter_w - 8) / CHAR_W;

    // Background
    buf_fill_rect(x, y, w, h, COLOR_BG);

    // Gutter
    buf_fill_rect(x, y, gutter_w, h, COLOR_GUTTER_BG);
    buf_fill_rect(x + gutter_w - 1, y, 1, h, 0x00CCCCCC);

    // Border
    buf_fill_rect(x, y + h - 1, w, 1, COLOR_FG);

    // Get cursor position
    int cursor_line, cursor_col;
    cursor_to_line_col(cursor_pos, &cursor_line, &cursor_col);

    // Adjust vertical scroll
    if (cursor_line < scroll_line) scroll_line = cursor_line;
    if (cursor_line >= scroll_line + visible_rows) scroll_line = cursor_line - visible_rows + 1;

    // Adjust horizontal scroll
    if (cursor_col < scroll_col) scroll_col = cursor_col;
    if (cursor_col >= scroll_col + visible_cols - 1) scroll_col = cursor_col - visible_cols + 2;
    if (scroll_col < 0) scroll_col = 0;

    // Draw line numbers
    int total_lines = count_lines();
    for (int row = 0; row < visible_rows; row++) {
        int line_num = scroll_line + row + 1;
        if (line_num <= total_lines) {
            char num[8];
            int n = line_num;
            int ni = 0;
            if (n == 0) {
                num[ni++] = '0';
            } else {
                char tmp[8];
                int ti = 0;
                while (n > 0) { tmp[ti++] = '0' + (n % 10); n /= 10; }
                while (ti > 0) num[ni++] = tmp[--ti];
            }
            num[ni] = '\0';

            int num_x = x + gutter_w - 8 - ni * CHAR_W;
            buf_draw_string(num_x, content_y + row * CHAR_H, num, COLOR_GUTTER_FG, COLOR_GUTTER_BG);
        }
    }

    // Draw text with syntax highlighting
    int current_line = 0;
    int col = 0;
    int in_comment = 0;      // For // comments
    int in_block = 0;        // For /* */ comments
    int in_string = 0;
    char string_char = 0;
    int token_color = 0;     // Current token color (keyword/number/function)
    int token_remain = 0;    // Characters remaining in current token

    const char **keywords = (syntax_mode == SYNTAX_C) ? c_keywords :
                            (syntax_mode == SYNTAX_PY) ? py_keywords : 0;

    for (int i = 0; i <= text_len; i++) {
        // Draw cursor
        if (i == cursor_pos && current_line >= scroll_line && current_line < scroll_line + visible_rows) {
            int cy = content_y + (current_line - scroll_line) * CHAR_H;
            int cx = content_x + (col - scroll_col) * CHAR_W;
            if (col >= scroll_col && col < scroll_col + visible_cols) {
                buf_fill_rect(cx, cy, CHAR_W, CHAR_H, COLOR_FG);
                if (i < text_len && text_buffer[i] != '\n') {
                    buf_draw_char(cx, cy, text_buffer[i], COLOR_BG, COLOR_FG);
                }
            }
        }

        if (i >= text_len) break;

        char c = text_buffer[i];

        if (c == '\n') {
            in_comment = 0;
            token_remain = 0;
            current_line++;
            col = 0;
            continue;
        }

        // Determine color
        uint32_t fg_color = COLOR_FG;

        if (keywords) {
            // Check for Python decorator
            if (syntax_mode == SYNTAX_PY && c == '@' && col == 0) {
                fg_color = COLOR_DECORATOR;
            }
            // Check for comment start
            else if (!in_string && !in_comment && !in_block) {
                if (syntax_mode == SYNTAX_C && c == '/' && i + 1 < text_len) {
                    if (text_buffer[i + 1] == '/') in_comment = 1;
                    else if (text_buffer[i + 1] == '*') in_block = 1;
                }
                if (syntax_mode == SYNTAX_PY && c == '#') in_comment = 1;
            }

            // Check for string
            if (!in_comment && !in_block && !in_string && (c == '"' || c == '\'')) {
                in_string = 1;
                string_char = c;
                token_remain = 0;
            } else if (in_string && c == string_char && (i == 0 || text_buffer[i-1] != '\\')) {
                // End of string - draw this char as string then reset
                fg_color = COLOR_STRING;
                if (current_line >= scroll_line && current_line < scroll_line + visible_rows) {
                    if (i != cursor_pos && col >= scroll_col && col < scroll_col + visible_cols) {
                        int cy = content_y + (current_line - scroll_line) * CHAR_H;
                        int cx = content_x + (col - scroll_col) * CHAR_W;
                        buf_draw_char(cx, cy, c, fg_color, COLOR_BG);
                    }
                }
                col++;
                in_string = 0;
                continue;
            }

            // End block comment
            if (in_block && c == '/' && i > 0 && text_buffer[i-1] == '*') {
                in_block = 0;
            }

            // Set color
            if (in_comment || in_block) {
                fg_color = COLOR_COMMENT;
            } else if (in_string) {
                fg_color = COLOR_STRING;
            } else if (token_remain > 0) {
                // Continue current token color
                fg_color = token_color;
                token_remain--;
            } else {
                // Check keyword at word start
                int is_start = (i == 0 || !is_word_char(text_buffer[i-1]));
                if (is_start && is_word_char(c)) {
                    char word[32];
                    int wi = 0;
                    for (int j = i; j < text_len && wi < 31 && is_word_char(text_buffer[j]); j++) {
                        word[wi++] = text_buffer[j];
                    }
                    word[wi] = '\0';

                    if (is_keyword(word, keywords)) {
                        fg_color = COLOR_KEYWORD;
                        token_color = COLOR_KEYWORD;
                        token_remain = wi - 1;  // -1 because current char counts
                    }
                    // Check if it's a function call (followed by '(')
                    else {
                        int end = i + wi;
                        while (end < text_len && text_buffer[end] == ' ') end++;
                        if (end < text_len && text_buffer[end] == '(') {
                            fg_color = COLOR_FUNCTION;
                            token_color = COLOR_FUNCTION;
                            token_remain = wi - 1;
                        }
                    }
                }

                // Numbers (check if start of number)
                if (c >= '0' && c <= '9') {
                    int is_num_start = (i == 0 || (!is_word_char(text_buffer[i-1]) || text_buffer[i-1] == '_'));
                    if (is_num_start) {
                        // Count number length (including hex 0x, floats, etc)
                        int num_len = 0;
                        for (int j = i; j < text_len; j++) {
                            char nc = text_buffer[j];
                            if ((nc >= '0' && nc <= '9') || (nc >= 'a' && nc <= 'f') ||
                                (nc >= 'A' && nc <= 'F') || nc == 'x' || nc == 'X' || nc == '.') {
                                num_len++;
                            } else {
                                break;
                            }
                        }
                        fg_color = COLOR_NUMBER;
                        token_color = COLOR_NUMBER;
                        token_remain = num_len - 1;
                    }
                }
            }
        }

        // Draw if visible
        if (current_line >= scroll_line && current_line < scroll_line + visible_rows) {
            if (i != cursor_pos && col >= scroll_col && col < scroll_col + visible_cols) {
                int cy = content_y + (current_line - scroll_line) * CHAR_H;
                int cx = content_x + (col - scroll_col) * CHAR_W;
                buf_draw_char(cx, cy, c, fg_color, COLOR_BG);
            }
        }
        col++;
    }

    // Scrollbar
    if (total_lines > visible_rows) {
        int sb_x = x + w - 12;
        int sb_y = y + 2;
        int sb_h = h - 4;

        // Track
        buf_fill_rect(sb_x, sb_y, 10, sb_h, 0x00DDDDDD);

        // Thumb
        int thumb_h = (visible_rows * sb_h) / total_lines;
        if (thumb_h < 20) thumb_h = 20;
        int thumb_y = sb_y + (scroll_line * (sb_h - thumb_h)) / (total_lines - visible_rows);
        buf_fill_rect(sb_x + 1, thumb_y, 8, thumb_h, 0x00888888);
    }
}

static int count_output_lines(void) {
    int lines = 1;
    for (int i = 0; i < output_len; i++) {
        if (output_buffer[i] == '\n') lines++;
    }
    return lines;
}

static void draw_output(void) {
    int y = win_h - OUTPUT_H;

    // Background
    buf_fill_rect(0, y, win_w, OUTPUT_H, COLOR_OUTPUT_BG);
    buf_fill_rect(0, y, win_w, 1, COLOR_FG);

    // Title
    buf_draw_string(4, y + 2, "Output", COLOR_OUTPUT_FG, COLOR_OUTPUT_BG);
    buf_fill_rect(0, y + 18, win_w, 1, COLOR_FG);

    // Content
    int text_y = y + 20;
    int visible_rows = (OUTPUT_H - 22) / CHAR_H;
    int visible_cols = (win_w - 20) / CHAR_W;  // Leave room for scrollbar
    int total_lines = count_output_lines();

    // Clamp scroll
    int max_scroll = total_lines - visible_rows;
    if (max_scroll < 0) max_scroll = 0;
    if (output_scroll > max_scroll) output_scroll = max_scroll;
    if (output_scroll < 0) output_scroll = 0;

    // Draw lines
    int line = 0;
    int col = 0;

    for (int i = 0; i < output_len; i++) {
        char c = output_buffer[i];

        if (c == '\n') {
            line++;
            col = 0;
            continue;
        }

        if (line >= output_scroll && line - output_scroll < visible_rows) {
            if (col < visible_cols) {
                int px = 4 + col * CHAR_W;
                int py = text_y + (line - output_scroll) * CHAR_H;
                buf_draw_char(px, py, c, COLOR_OUTPUT_FG, COLOR_OUTPUT_BG);
            }
        }
        col++;
    }

    // Scrollbar
    if (total_lines > visible_rows) {
        int sb_x = win_w - 12;
        int sb_y = y + 20;
        int sb_h = OUTPUT_H - 22;

        // Track
        buf_fill_rect(sb_x, sb_y, 10, sb_h, 0x00DDDDDD);

        // Thumb
        int thumb_h = (visible_rows * sb_h) / total_lines;
        if (thumb_h < 10) thumb_h = 10;
        int max_scroll = total_lines - visible_rows;
        int thumb_y = sb_y;
        if (max_scroll > 0) {
            thumb_y = sb_y + (output_scroll * (sb_h - thumb_h)) / max_scroll;
        }
        buf_fill_rect(sb_x + 1, thumb_y, 8, thumb_h, 0x00888888);
    }
}

// Help content - quick API reference
static const char *help_lines[] = {
    "=== VibeCode Quick Reference ===",
    "",
    "KEYBOARD SHORTCUTS:",
    "  Ctrl+S    Save file",
    "  Ctrl+R    Run program",
    "  Ctrl+N    New file",
    "  Esc       Toggle help",
    "",
    "=== Vibe API Basics ===",
    "",
    "PROGRAM STRUCTURE:",
    "  #include \"../lib/kiki.h\"",
    "  int main(kapi_t *k, int argc, char **argv) {",
    "      vibe_puts(k, \"Hello!\\n\");",
    "      return 0;",
    "  }",
    "",
    "CONSOLE I/O:",
    "  vibe_puts(k, \"text\")   Print string",
    "  vibe_putc(k, 'c')      Print char",
    "  vibe_getc(k)           Read char",
    "  vibe_has_key(k)        Check key available",
    "  vibe_print_int(k, n)   Print integer",
    "",
    "MEMORY:",
    "  k->malloc(size)        Allocate",
    "  k->free(ptr)           Free",
    "",
    "FILES:",
    "  k->open(path)          Open file/dir",
    "  k->read(f, buf, sz, 0) Read from file",
    "  k->write(f, buf, sz)   Write to file",
    "  k->create(path)        Create file",
    "  k->mkdir(path)         Create directory",
    "",
    "WINDOW (GUI):",
    "  k->window_create(x,y,w,h,title)",
    "  k->window_get_buffer(wid, &w, &h)",
    "  k->window_poll_event(wid, &t, &d1, &d2, &d3)",
    "  k->window_invalidate(wid)",
    "  k->window_destroy(wid)",
    "",
    "EVENTS:",
    "  WIN_EVENT_CLOSE        Window closed",
    "  WIN_EVENT_KEY          Key pressed (d1=key)",
    "  WIN_EVENT_MOUSE_DOWN   Click (d1=x, d2=y)",
    "  WIN_EVENT_MOUSE_MOVE   Move (d1=x, d2=y)",
    "",
    "DRAWING (gfx.h):",
    "  gfx_fill_rect(&g, x,y,w,h, color)",
    "  gfx_draw_string(&g, x,y, str, fg, bg)",
    "",
    "COLORS: 0x00RRGGBB",
    "  COLOR_BLACK  0x00000000",
    "  COLOR_WHITE  0x00FFFFFF",
    "",
    "Full docs: /docs/api.md",
    "",
    "[Press Esc to close]",
    0
};

static void draw_help_panel(void) {
    // Semi-transparent overlay effect (darken background)
    int panel_w = win_w - 100;
    int panel_h = win_h - 60;
    int panel_x = 50;
    int panel_y = 30;

    // Shadow
    buf_fill_rect(panel_x + 4, panel_y + 4, panel_w, panel_h, 0x00888888);

    // Panel background
    buf_fill_rect(panel_x, panel_y, panel_w, panel_h, COLOR_WHITE);

    // Border
    buf_draw_rect(panel_x, panel_y, panel_w, panel_h, COLOR_FG);
    buf_draw_rect(panel_x + 1, panel_y + 1, panel_w - 2, panel_h - 2, COLOR_FG);

    // Title bar (classic Mac style - striped pattern)
    buf_fill_rect(panel_x + 2, panel_y + 2, panel_w - 4, 20, COLOR_WHITE);
    for (int i = 0; i < 9; i++) {
        buf_fill_rect(panel_x + 4, panel_y + 4 + i * 2, panel_w - 8, 1, COLOR_FG);
    }
    buf_fill_rect(panel_x + 8, panel_y + 4, 25 * 8 + 4, 16, COLOR_WHITE);
    buf_draw_string(panel_x + 10, panel_y + 4, "Help - Vibe API Reference", COLOR_FG, COLOR_WHITE);

    // Close X
    buf_draw_string(panel_x + panel_w - 20, panel_y + 4, "X", COLOR_FG, COLOR_WHITE);

    // Content area
    int content_x = panel_x + 8;
    int content_y = panel_y + 26;
    int content_h = panel_h - 30;
    int visible_rows = content_h / CHAR_H;

    // Count total lines
    int total_lines = 0;
    while (help_lines[total_lines]) total_lines++;

    // Draw help text
    for (int i = 0; i < visible_rows && (help_scroll + i) < total_lines; i++) {
        const char *line = help_lines[help_scroll + i];
        int y = content_y + i * CHAR_H;
        buf_draw_string(content_x, y, line, COLOR_FG, COLOR_WHITE);
    }

    // Scroll indicator
    if (total_lines > visible_rows) {
        int track_h = content_h - 4;
        int thumb_h = (visible_rows * track_h) / total_lines;
        if (thumb_h < 20) thumb_h = 20;
        int thumb_y = content_y + (help_scroll * track_h) / total_lines;

        buf_fill_rect(panel_x + panel_w - 14, content_y, 10, content_h, 0x00DDDDDD);
        buf_fill_rect(panel_x + panel_w - 12, thumb_y, 6, thumb_h, 0x00888888);
    }
}

static void draw_welcome_screen(void) {
    // Full window background - light gray
    buf_fill_rect(0, 0, win_w, win_h, 0x00F0F0F0);

    // Title
    int title_y = 60;
    buf_draw_string((win_w - 8 * 8) / 2, title_y, "VibeCode", COLOR_FG, 0x00F0F0F0);

    // Subtitle
    buf_draw_string((win_w - 20 * 8) / 2, title_y + 24, "IDE for KikiOS", 0x00666666, 0x00F0F0F0);

    // Buttons
    int btn_w = 200;
    int btn_h = 40;
    int btn_x = (win_w - btn_w) / 2;
    int btn_y = 140;
    int btn_spacing = 20;

    // Create Project button (Mac-style: inverted when hovered)
    uint32_t create_bg = (welcome_hover == 0) ? COLOR_FG : COLOR_WHITE;
    uint32_t create_fg = (welcome_hover == 0) ? COLOR_WHITE : COLOR_FG;
    buf_fill_rect(btn_x, btn_y, btn_w, btn_h, create_bg);
    buf_draw_rect(btn_x, btn_y, btn_w, btn_h, COLOR_FG);
    buf_draw_string(btn_x + (btn_w - 14 * 8) / 2, btn_y + 12, "Create Project", create_fg, create_bg);

    // Open Project button
    int open_y = btn_y + btn_h + btn_spacing;
    uint32_t open_bg = (welcome_hover == 1) ? COLOR_FG : COLOR_WHITE;
    uint32_t open_fg = (welcome_hover == 1) ? COLOR_WHITE : COLOR_FG;
    buf_fill_rect(btn_x, open_y, btn_w, btn_h, open_bg);
    buf_draw_rect(btn_x, open_y, btn_w, btn_h, COLOR_FG);
    buf_draw_string(btn_x + (btn_w - 12 * 8) / 2, open_y + 12, "Open Project", open_fg, open_bg);

    // Recent hint
    buf_draw_string((win_w - 28 * 8) / 2, open_y + btn_h + 40,
                    "Or drop a file on the dock", 0x00888888, 0x00F0F0F0);

    // If input mode, draw input dialog
    if (welcome_input_mode) {
        int dlg_w = 300;
        int dlg_h = 100;
        int dlg_x = (win_w - dlg_w) / 2;
        int dlg_y = (win_h - dlg_h) / 2;

        // Shadow and background
        buf_fill_rect(dlg_x + 4, dlg_y + 4, dlg_w, dlg_h, 0x00888888);
        buf_fill_rect(dlg_x, dlg_y, dlg_w, dlg_h, COLOR_WHITE);
        buf_draw_rect(dlg_x, dlg_y, dlg_w, dlg_h, COLOR_FG);
        buf_draw_rect(dlg_x + 1, dlg_y + 1, dlg_w - 2, dlg_h - 2, COLOR_FG);

        // Title - different for Create vs Open
        const char *title = (welcome_input_mode == 1) ? "Project Name:" : "Project Path:";
        buf_draw_string(dlg_x + 12, dlg_y + 12, title, COLOR_FG, COLOR_WHITE);

        // Input box
        int inp_x = dlg_x + 12;
        int inp_y = dlg_y + 36;
        int inp_w = dlg_w - 24;
        int inp_h = 24;
        buf_fill_rect(inp_x, inp_y, inp_w, inp_h, COLOR_WHITE);
        buf_draw_rect(inp_x, inp_y, inp_w, inp_h, COLOR_FG);

        // Input text (scroll if too long)
        int max_visible = (inp_w - 8) / 8;
        int text_offset = 0;
        if (welcome_input_len > max_visible) {
            text_offset = welcome_input_len - max_visible;
        }
        buf_draw_string(inp_x + 4, inp_y + 4, welcome_input + text_offset, COLOR_FG, COLOR_WHITE);

        // Cursor
        int cursor_x = inp_x + 4 + (welcome_input_len - text_offset) * 8;
        if (cursor_x < inp_x + inp_w - 8) {
            buf_fill_rect(cursor_x, inp_y + 4, 8, 16, COLOR_FG);
        }

        // Instructions
        const char *instr = (welcome_input_mode == 1) ? "Enter=Create  Esc=Cancel" : "Enter=Open  Esc=Cancel";
        buf_draw_string(dlg_x + 12, dlg_y + 70, instr, 0x00666666, COLOR_WHITE);
    }
}

static void draw_all(void) {
    // Welcome screen takes over everything
    if (show_welcome) {
        draw_welcome_screen();
        api->window_invalidate(window_id);
        return;
    }

    draw_toolbar();
    draw_sidebar();
    draw_editor();
    draw_output();

    // Help panel overlay
    if (show_help) {
        draw_help_panel();
    }

    api->window_invalidate(window_id);
}

// ============ Input Handling ============

// Count help lines
static int help_line_count(void) {
    int count = 0;
    while (help_lines[count]) count++;
    return count;
}

static void handle_key(int key) {
    // New file mode takes priority for Escape
    if (new_file_mode && key == 0x1B) {
        new_file_mode = 0;
        return;
    }

    // Escape closes help if open, or toggles if closed
    if (key == 0x1B) {  // Escape
        show_help = !show_help;
        help_scroll = 0;
        return;
    }

    // If help is showing, handle help navigation
    if (show_help) {
        int total = help_line_count();
        int visible = (win_h - 60 - 30) / CHAR_H;

        switch (key) {
            case KEY_UP:
                if (help_scroll > 0) help_scroll--;
                break;
            case KEY_DOWN:
                if (help_scroll < total - visible) help_scroll++;
                break;
            case KEY_PGUP:
                help_scroll -= visible;
                if (help_scroll < 0) help_scroll = 0;
                break;
            case KEY_PGDN:
                help_scroll += visible;
                if (help_scroll > total - visible) help_scroll = total - visible;
                if (help_scroll < 0) help_scroll = 0;
                break;
            case KEY_HOME:
                help_scroll = 0;
                break;
            case KEY_END:
                help_scroll = total - visible;
                if (help_scroll < 0) help_scroll = 0;
                break;
        }
        return;
    }

    // New file input mode
    if (new_file_mode) {
        if (key == '\r' || key == '\n') {  // Enter - create
            create_new_file();
        } else if (key == 8) {  // Backspace
            if (new_file_name_len > 0) {
                new_file_name_len--;
                new_file_name[new_file_name_len] = '\0';
            }
        } else if (key >= 32 && key < 127 && new_file_name_len < 60) {
            // Valid filename char (avoid / and special chars)
            if (key != '/' && key != '\\' && key != ':' && key != '*' && key != '?') {
                new_file_name[new_file_name_len++] = (char)key;
                new_file_name[new_file_name_len] = '\0';
            }
        }
        return;
    }

    // Normal editor key handling
    int line, col;

    switch (key) {
        case '\r':
        case '\n':
            insert_char('\n');
            // Auto-indent
            {
                int ls = line_start(cursor_pos - 1);
                while (ls < cursor_pos - 1 && (text_buffer[ls] == ' ' || text_buffer[ls] == '\t')) {
                    insert_char(text_buffer[ls]);
                    ls++;
                }
            }
            break;

        case 8:  // Backspace
            delete_char_before();
            break;

        case KEY_DELETE:
            delete_char_at();
            break;

        case '\t':
            insert_char(' ');
            insert_char(' ');
            insert_char(' ');
            insert_char(' ');
            break;

        case KEY_UP:
            cursor_to_line_col(cursor_pos, &line, &col);
            if (line > 0) cursor_pos = line_col_to_cursor(line - 1, col);
            break;

        case KEY_DOWN:
            cursor_to_line_col(cursor_pos, &line, &col);
            cursor_pos = line_col_to_cursor(line + 1, col);
            if (cursor_pos > text_len) cursor_pos = text_len;
            break;

        case KEY_LEFT:
            if (cursor_pos > 0) cursor_pos--;
            break;

        case KEY_RIGHT:
            if (cursor_pos < text_len) cursor_pos++;
            break;

        case KEY_HOME:
            cursor_pos = line_start(cursor_pos);
            break;

        case KEY_END:
            cursor_pos = line_end(cursor_pos);
            break;

        case 19:  // Ctrl+S
            save_file();
            break;

        case 18:  // Ctrl+R
            run_current_file();
            break;

        case 14:  // Ctrl+N
            new_file();
            break;

        default:
            if (key >= 32 && key < 127) {
                insert_char((char)key);
                // Auto-close brackets and quotes
                char close = 0;
                if (key == '(') close = ')';
                else if (key == '[') close = ']';
                else if (key == '{') close = '}';
                else if (key == '"') close = '"';
                else if (key == '\'') close = '\'';
                if (close) {
                    insert_char(close);
                    cursor_pos--;  // Move cursor between the pair
                }
            }
            break;
    }
}

static int file_at_point(int x, int y) {
    if (x >= SIDEBAR_W) return -1;

    int list_y = TOOLBAR_H + 20;
    if (y < list_y) return -1;

    int row = (y - list_y) / CHAR_H;
    int idx = file_scroll + row;

    if (idx >= 0 && idx < file_count) return idx;
    return -1;
}

static int is_button_hit(int x, int y, int btn_x, int btn_y, int btn_w, int btn_h) {
    return x >= btn_x && x < btn_x + btn_w && y >= btn_y && y < btn_y + btn_h;
}

// ============ Main ============

int main(kapi_t *kapi, int argc, char **argv) {
    api = kapi;

    if (!api->window_create) {
        api->puts("kikicode: window API not available\n");
        return 1;
    }

    // Create window
    window_id = api->window_create(20, 20, WINDOW_W, WINDOW_H + TITLE_BAR_H, "VibeCode");
    if (window_id < 0) {
        api->puts("kikicode: failed to create window\n");
        return 1;
    }

    win_buffer = api->window_get_buffer(window_id, &win_w, &win_h);
    if (!win_buffer) {
        api->puts("kikicode: failed to get window buffer\n");
        api->window_destroy(window_id);
        return 1;
    }

    gfx_init(&gfx, win_buffer, win_w, win_h, api->font_data);

    // Get starting directory
    api->get_cwd(tree_root, sizeof(tree_root));
    refresh_file_tree();

    // Load file if specified - skip welcome screen
    if (argc > 1) {
        load_file(argv[1]);
        show_welcome = 0;
    }

    // Initial draw
    draw_all();

    // Event loop
    int running = 1;
    while (running) {
        int event_type, data1, data2, data3;

        while (api->window_poll_event(window_id, &event_type, &data1, &data2, &data3)) {
            switch (event_type) {
                case WIN_EVENT_CLOSE:
                    running = 0;
                    break;

                case WIN_EVENT_KEY:
                    // Welcome screen input handling
                    if (show_welcome) {
                        if (welcome_input_mode) {
                            if (data1 == 0x1B) {  // Escape
                                welcome_input_mode = 0;
                            } else if (data1 == '\r' || data1 == '\n') {
                                if (welcome_input_len > 0) {
                                    if (welcome_input_mode == 1) {
                                        // Create project - make dir under /home/user
                                        char path[128] = "/home/user/";
                                        strcat(path, welcome_input);
                                        api->mkdir(path);
                                        strncpy_safe(tree_root, path, sizeof(tree_root));
                                    } else {
                                        // Open project - use path directly
                                        strncpy_safe(tree_root, welcome_input, sizeof(tree_root));
                                    }
                                    refresh_file_tree();
                                    show_welcome = 0;
                                    welcome_input_mode = 0;
                                }
                            } else if (data1 == 8 || data1 == 127) {  // Backspace
                                if (welcome_input_len > 0) {
                                    welcome_input[--welcome_input_len] = '\0';
                                }
                            } else if (data1 >= 32 && data1 < 127 && welcome_input_len < 120) {
                                welcome_input[welcome_input_len++] = (char)data1;
                                welcome_input[welcome_input_len] = '\0';
                            }
                        }
                        draw_all();
                        break;
                    }
                    handle_key(data1);
                    draw_all();
                    break;

                case WIN_EVENT_MOUSE_DOWN: {
                    int x = data1, y = data2;

                    // Welcome screen click handling
                    if (show_welcome) {
                        int btn_w = 200;
                        int btn_h = 40;
                        int btn_x = (win_w - btn_w) / 2;
                        int btn_y = 140;
                        int btn_spacing = 20;

                        if (!welcome_input_mode) {
                            // Check Create button
                            if (x >= btn_x && x < btn_x + btn_w &&
                                y >= btn_y && y < btn_y + btn_h) {
                                welcome_input_mode = 1;
                                welcome_input[0] = '\0';
                                welcome_input_len = 0;
                            }
                            // Check Open button
                            int open_y = btn_y + btn_h + btn_spacing;
                            if (x >= btn_x && x < btn_x + btn_w &&
                                y >= open_y && y < open_y + btn_h) {
                                welcome_input_mode = 2;
                                // Pre-fill with /home/user
                                strncpy_safe(welcome_input, "/home/user", sizeof(welcome_input));
                                welcome_input_len = 10;
                            }
                        } else {
                            // Click outside dialog cancels
                            int dlg_w = 300;
                            int dlg_h = 100;
                            int dlg_x = (win_w - dlg_w) / 2;
                            int dlg_y = (win_h - dlg_h) / 2;
                            if (x < dlg_x || x > dlg_x + dlg_w ||
                                y < dlg_y || y > dlg_y + dlg_h) {
                                welcome_input_mode = 0;
                            }
                        }
                        draw_all();
                        break;
                    }

                    // If help is showing, check for close click
                    if (show_help) {
                        int panel_w = win_w - 100;
                        int panel_x = 50;
                        int panel_y = 30;
                        // Close X button area
                        if (x >= panel_x + panel_w - 24 && x < panel_x + panel_w - 4 &&
                            y >= panel_y + 2 && y < panel_y + 22) {
                            show_help = 0;
                            draw_all();
                            break;
                        }
                        // Click outside panel closes it
                        if (x < panel_x || x > panel_x + panel_w ||
                            y < panel_y || y > panel_y + (win_h - 60)) {
                            show_help = 0;
                            draw_all();
                            break;
                        }
                        // Otherwise ignore clicks inside help panel
                        break;
                    }

                    // Check toolbar buttons
                    if (y < TOOLBAR_H) {
                        if (is_button_hit(x, y, 4, 2, 48, 20)) {
                            new_file();
                        } else if (is_button_hit(x, y, 56, 2, 48, 20)) {
                            save_file();
                        } else if (is_button_hit(x, y, 108, 2, 48, 20)) {
                            run_current_file();
                        } else if (is_button_hit(x, y, win_w - 52, 2, 48, 20)) {
                            show_help = !show_help;
                            help_scroll = 0;
                        }
                        draw_all();
                        break;
                    }

                    // Check file list click
                    int fidx = file_at_point(x, y);
                    if (fidx >= 0) {
                        selected_file = fidx;

                        // Double-click handling via single click on file
                        file_entry_t *f = &files[fidx];
                        if (!f->is_dir) {
                            load_file(f->path);
                        }
                        draw_all();
                        break;
                    }

                    // Check output scrollbar click
                    int output_y = win_h - OUTPUT_H;
                    if (y > output_y + 20 && x > win_w - 14) {
                        int total_lines = count_output_lines();
                        int visible_rows = (OUTPUT_H - 22) / CHAR_H;
                        if (total_lines > visible_rows) {
                            // Calculate scroll position from click
                            int sb_y = output_y + 20;
                            int sb_h = OUTPUT_H - 22;
                            int click_pos = y - sb_y;
                            int max_scroll = total_lines - visible_rows;
                            output_scroll = (click_pos * max_scroll) / sb_h;
                            if (output_scroll < 0) output_scroll = 0;
                            if (output_scroll > max_scroll) output_scroll = max_scroll;
                            draw_all();
                        }
                        break;
                    }

                    // Check editor scrollbar click
                    int editor_x = SIDEBAR_W;
                    int editor_y = TOOLBAR_H;
                    int editor_w = win_w - SIDEBAR_W;
                    int editor_h = win_h - TOOLBAR_H - OUTPUT_H;
                    if (x > editor_x + editor_w - 14 && x < editor_x + editor_w &&
                        y > editor_y && y < editor_y + editor_h) {
                        int total_lines = count_lines();
                        int visible_rows = (editor_h - 8) / CHAR_H;
                        if (total_lines > visible_rows) {
                            int sb_y = editor_y + 2;
                            int sb_h = editor_h - 4;
                            int click_pos = y - sb_y;
                            int max_scroll = total_lines - visible_rows;
                            scroll_line = (click_pos * max_scroll) / sb_h;
                            if (scroll_line < 0) scroll_line = 0;
                            if (scroll_line > max_scroll) scroll_line = max_scroll;
                            draw_all();
                        }
                        break;
                    }
                    break;
                }

                case WIN_EVENT_MOUSE_MOVE: {
                    int x = data1, y = data2;

                    // Welcome screen hover
                    if (show_welcome && !welcome_input_mode) {
                        int btn_w = 200;
                        int btn_h = 40;
                        int btn_x = (win_w - btn_w) / 2;
                        int btn_y = 140;
                        int btn_spacing = 20;
                        int old_hover = welcome_hover;

                        welcome_hover = -1;
                        if (x >= btn_x && x < btn_x + btn_w) {
                            if (y >= btn_y && y < btn_y + btn_h) {
                                welcome_hover = 0;  // Create
                            }
                            int open_y = btn_y + btn_h + btn_spacing;
                            if (y >= open_y && y < open_y + btn_h) {
                                welcome_hover = 1;  // Open
                            }
                        }

                        if (welcome_hover != old_hover) {
                            draw_all();
                        }
                        break;
                    }

                    int old_run = hover_run;
                    int old_save = hover_save;
                    int old_new = hover_new;
                    int old_help = hover_help;

                    hover_new = is_button_hit(x, y, 4, 2, 48, 20);
                    hover_save = is_button_hit(x, y, 56, 2, 48, 20);
                    hover_run = is_button_hit(x, y, 108, 2, 48, 20);
                    hover_help = is_button_hit(x, y, win_w - 52, 2, 48, 20);

                    if (hover_run != old_run || hover_save != old_save ||
                        hover_new != old_new || hover_help != old_help) {
                        draw_all();
                    }
                    break;
                }

                case WIN_EVENT_RESIZE:
                    win_buffer = api->window_get_buffer(window_id, &win_w, &win_h);
                    gfx_init(&gfx, win_buffer, win_w, win_h, api->font_data);
                    draw_all();
                    break;
            }
        }

        api->yield();
    }

    api->window_destroy(window_id);
    return 0;
}
