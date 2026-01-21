/*
 * KikiOS File Explorer
 *
 * A windowed file browser with right-click context menu.
 * Features: navigate dirs, create/rename/delete files and folders,
 * open with TextEdit, open terminal here.
 */

#include "../lib/kiki.h"
#include "../lib/gfx.h"

// Window dimensions
#define WIN_WIDTH  400
#define WIN_HEIGHT 350

// UI constants
#define PATH_BAR_HEIGHT 24
#define ITEM_HEIGHT     18
#define SCROLL_WIDTH    16
#define MAX_ITEMS       64
#define MAX_VISIBLE     ((WIN_HEIGHT - PATH_BAR_HEIGHT - 4) / ITEM_HEIGHT)

// Modern colors
#define COLOR_BG        0x00F5F5F5
#define COLOR_FG        0x00333333
#define COLOR_SELECTED  0x00007AFF  // Blue selection
#define COLOR_SEL_TEXT  0x00FFFFFF
#define COLOR_PATH_BG   0x00FFFFFF
#define COLOR_MENU_BG   0x00FFFFFF
#define COLOR_MENU_HL   0x00007AFF
#define COLOR_FOLDER    0x0066B3FF  // Lighter blue folders
#define COLOR_FILE      0x00888888
#define COLOR_BORDER    0x00E0E0E0
#define COLOR_HOVER     0x00E8F4FF  // Light blue hover

// Context menu
#define MENU_ITEM_HEIGHT 24
#define MENU_WIDTH       160

typedef struct {
    char name[64];
    uint8_t is_dir;
} file_entry_t;

// Global state
static kapi_t *api;
static int window_id = -1;
static uint32_t *win_buffer;
static int win_w, win_h;
static gfx_ctx_t gfx;

// File list
static file_entry_t items[MAX_ITEMS];
static int item_count = 0;
static int selected_idx = -1;
static int scroll_offset = 0;

// Current path
static char current_path[256];

// Context menu state
static int menu_visible = 0;
static int menu_x, menu_y;
static int menu_hover = -1;

// Rename/create state
static int renaming = 0;
static char rename_buf[64];
static int rename_cursor = 0;
static int creating_new = 0;      // 0 = renaming, 1 = new file, 2 = new folder
static int creating_idx = -1;     // Index of the fake "new" item in list

// Context menu items
static const char *menu_items[] = {
    "New File",
    "New Folder",
    "Rename",
    "Delete",
    "Open with TextEdit",
    "Open Terminal Here"
};
#define MENU_ITEM_COUNT 6

// ============ Drawing Helpers (macros wrapping gfx lib) ============

#define buf_fill_rect(x, y, w, h, c)          gfx_fill_rect(&gfx, x, y, w, h, c)
#define buf_draw_char(x, y, ch, fg, bg)       gfx_draw_char(&gfx, x, y, ch, fg, bg)
#define buf_draw_string(x, y, s, fg, bg)      gfx_draw_string(&gfx, x, y, s, fg, bg)
#define buf_draw_string_clip(x, y, s, fg, bg, max_w) gfx_draw_string_clip(&gfx, x, y, s, fg, bg, max_w)
#define buf_draw_rect(x, y, w, h, c)          gfx_draw_rect(&gfx, x, y, w, h, c)
#define buf_fill_rounded(x, y, w, h, r, c)    gfx_fill_rounded_rect(&gfx, x, y, w, h, r, c)
#define buf_draw_rounded(x, y, w, h, r, c)    gfx_draw_rounded_rect(&gfx, x, y, w, h, r, c)

// Draw folder icon (small, 14x12) - macOS-style
static void draw_folder_icon(int x, int y, uint32_t bg) {
    (void)bg;
    // Folder tab (top-left rounded flap)
    buf_fill_rounded(x, y, 6, 3, 1, COLOR_FOLDER);
    // Folder body with rounded corners
    buf_fill_rounded(x, y + 2, 14, 10, 2, COLOR_FOLDER);
    // Highlight line at top
    for (int i = 1; i < 13; i++) {
        if (x + i < win_w && y + 3 < win_h)
            win_buffer[(y + 3) * win_w + x + i] = 0x0088CCFF;
    }
}

// Draw file icon (small, 12x14) - document style
static void draw_file_icon(int x, int y, uint32_t bg) {
    (void)bg;
    // White document background with rounded corners
    buf_fill_rounded(x, y, 12, 14, 2, 0x00FFFFFF);
    buf_draw_rounded(x, y, 12, 14, 2, COLOR_FILE);
    // Document lines
    for (int i = 0; i < 3; i++) {
        int ly = y + 5 + i * 3;
        if (ly < win_h) {
            buf_fill_rect(x + 3, ly, 6, 1, 0x00CCCCCC);
        }
    }
    // Corner fold
    buf_fill_rect(x + 8, y, 4, 4, 0x00E0E0E0);
    // Fold diagonal
    for (int i = 0; i < 3; i++) {
        if (x + 8 + i < win_w && y + i < win_h)
            win_buffer[(y + i) * win_w + x + 8 + i] = COLOR_FILE;
    }
}

// ============ File Type Detection ============

// Get file extension (returns pointer to char after last '.')
static const char *get_extension(const char *filename) {
    const char *dot = 0;
    for (const char *p = filename; *p; p++) {
        if (*p == '.') dot = p + 1;
    }
    return dot;  // NULL if no extension
}

// Case-insensitive extension match
static int ext_match(const char *ext, const char *target) {
    if (!ext) return 0;
    while (*ext && *target) {
        char a = *ext++;
        char b = *target++;
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return 0;
    }
    return (*ext == 0 && *target == 0);
}

// Check if file contents are text (not binary)
// Reads first 512 bytes and checks for null bytes / control chars
static int is_text_file(const char *path) {
    void *file = api->open(path);
    if (!file) return 0;

    int size = api->file_size(file);
    if (size <= 0) return 1;  // Empty file counts as text

    // Read first 512 bytes
    char buf[512];
    int to_read = size < 512 ? size : 512;
    int n = api->read(file, buf, to_read, 0);
    if (n <= 0) return 1;

    // Check for binary indicators
    int non_printable = 0;
    for (int i = 0; i < n; i++) {
        unsigned char c = (unsigned char)buf[i];

        // Null byte = definitely binary
        if (c == 0) return 0;

        // Count non-printable chars (excluding common whitespace)
        if (c < 32 && c != '\t' && c != '\n' && c != '\r') {
            non_printable++;
        }
        // High bytes (128-255) could be UTF-8, allow some
        if (c >= 128 && c < 194) {
            // Invalid UTF-8 start byte
            non_printable++;
        }
    }

    // If more than 10% non-printable, probably binary
    if (non_printable * 10 > n) return 0;

    return 1;
}

// Open file with appropriate app based on type
static void open_file(const char *path, const char *filename) {
    const char *ext = get_extension(filename);

    // Image files -> viewer
    if (ext_match(ext, "png") || ext_match(ext, "jpg") ||
        ext_match(ext, "jpeg") || ext_match(ext, "bmp") ||
        ext_match(ext, "gif")) {
        char *argv[2];
        argv[0] = "/bin/viewer";
        argv[1] = (char *)path;
        api->spawn_args("/bin/viewer", 2, argv);
        return;
    }

    // Audio files -> music
    if (ext_match(ext, "mp3") || ext_match(ext, "wav")) {
        char *argv[2];
        argv[0] = "/bin/music";
        argv[1] = (char *)path;
        api->spawn_args("/bin/music", 2, argv);
        return;
    }

    // Check if it's a text file
    if (is_text_file(path)) {
        char *argv[2];
        argv[0] = "/bin/textedit";
        argv[1] = (char *)path;
        api->spawn_args("/bin/textedit", 2, argv);
        return;
    }

    // Binary file - can't open
    // TODO: Show "Cannot open binary file" dialog
}

// ============ Recursive Delete (userspace implementation) ============

static int delete_recursive(const char *path) {
    // First try deleting as a file
    if (api->delete(path) == 0) {
        return 0;
    }

    // Try deleting as an empty directory
    if (api->delete_dir(path) == 0) {
        return 0;
    }

    // If that failed, it might be a non-empty directory
    // Open directory and iterate
    void *dir = api->open(path);
    if (!dir) {
        return -1;
    }

    char name[256];
    uint8_t type;
    char child_path[512];
    int idx = 0;

    // Collect all names first, then delete
    // (Can't delete while iterating - shifts indices)
    char all_names[4096];
    int name_offsets[128];
    int name_count = 0;
    int buf_pos = 0;

    while (api->readdir(dir, idx, name, sizeof(name), &type) == 0) {
        idx++;

        // Skip . and ..
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }

        // Store name
        if (name_count < 128 && buf_pos + (int)strlen(name) + 1 < 4096) {
            name_offsets[name_count++] = buf_pos;
            strcpy(all_names + buf_pos, name);
            buf_pos += strlen(name) + 1;
        }
    }

    // Now delete all collected entries
    for (int i = 0; i < name_count; i++) {
        // Build child path
        strcpy(child_path, path);
        int plen = strlen(child_path);
        if (plen > 0 && child_path[plen - 1] != '/') {
            child_path[plen] = '/';
            child_path[plen + 1] = '\0';
        }
        int clen = strlen(child_path);
        strcpy(child_path + clen, all_names + name_offsets[i]);

        // Recursively delete child
        delete_recursive(child_path);
    }

    // Now directory should be empty, delete it as directory
    return api->delete_dir(path);
}

// ============ File Operations ============

static void refresh_directory(void) {
    item_count = 0;
    selected_idx = -1;

    void *dir = api->open(current_path);
    if (!dir || !api->is_dir(dir)) {
        return;
    }

    // Add ".." if not at root
    if (strcmp(current_path, "/") != 0) {
        strcpy(items[item_count].name, "..");
        items[item_count].is_dir = 1;
        item_count++;
    }

    // Read directory entries
    char name[64];
    uint8_t type;
    int idx = 0;

    while (item_count < MAX_ITEMS && api->readdir(dir, idx, name, sizeof(name), &type) == 0) {
        // Skip . and ..
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            idx++;
            continue;
        }

        strcpy(items[item_count].name, name);
        items[item_count].is_dir = (type == 2);  // VFS_DIRECTORY = 2
        item_count++;
        idx++;
    }

    scroll_offset = 0;
}

static void navigate_to(const char *path) {
    strncpy_safe(current_path, path, sizeof(current_path));
    refresh_directory();
}

static void navigate_up(void) {
    if (strcmp(current_path, "/") == 0) return;

    // Find last slash
    char *last_slash = current_path;
    for (char *p = current_path; *p; p++) {
        if (*p == '/') last_slash = p;
    }

    if (last_slash == current_path) {
        // Go to root
        current_path[1] = '\0';
    } else {
        *last_slash = '\0';
    }

    refresh_directory();
}

static void enter_selected(void) {
    if (selected_idx < 0 || selected_idx >= item_count) return;

    file_entry_t *item = &items[selected_idx];

    if (strcmp(item->name, "..") == 0) {
        navigate_up();
        return;
    }

    // Build full path
    char full_path[256];
    if (strcmp(current_path, "/") == 0) {
        strcpy(full_path, "/");
        strcat(full_path, item->name);
    } else {
        strcpy(full_path, current_path);
        strcat(full_path, "/");
        strcat(full_path, item->name);
    }

    if (item->is_dir) {
        // Enter directory
        navigate_to(full_path);
    } else {
        // Open file with appropriate app
        open_file(full_path, item->name);
    }
}

static void build_item_path(int idx, char *out, size_t out_size) {
    if (strcmp(current_path, "/") == 0) {
        strcpy(out, "/");
        strcat(out, items[idx].name);
    } else {
        strcpy(out, current_path);
        strcat(out, "/");
        strcat(out, items[idx].name);
    }
}

// ============ Context Menu Actions ============

static void action_new_file(void) {
    // Add a fake entry at the end and start renaming it
    if (item_count >= MAX_ITEMS) return;

    creating_idx = item_count;
    strcpy(items[item_count].name, "");
    items[item_count].is_dir = 0;
    item_count++;

    selected_idx = creating_idx;
    renaming = 1;
    creating_new = 1;  // New file
    rename_buf[0] = '\0';
    rename_cursor = 0;

    // Scroll to show new item if needed
    if (selected_idx >= scroll_offset + MAX_VISIBLE) {
        scroll_offset = selected_idx - MAX_VISIBLE + 1;
    }
}

static void action_new_folder(void) {
    // Add a fake entry at the end and start renaming it
    if (item_count >= MAX_ITEMS) return;

    creating_idx = item_count;
    strcpy(items[item_count].name, "");
    items[item_count].is_dir = 1;
    item_count++;

    selected_idx = creating_idx;
    renaming = 1;
    creating_new = 2;  // New folder
    rename_buf[0] = '\0';
    rename_cursor = 0;

    // Scroll to show new item if needed
    if (selected_idx >= scroll_offset + MAX_VISIBLE) {
        scroll_offset = selected_idx - MAX_VISIBLE + 1;
    }
}

static void action_rename(void) {
    if (selected_idx < 0 || selected_idx >= item_count) return;
    if (strcmp(items[selected_idx].name, "..") == 0) return;

    renaming = 1;
    creating_new = 0;  // Just renaming
    strcpy(rename_buf, items[selected_idx].name);
    rename_cursor = strlen(rename_buf);
}

static void action_delete(void) {
    if (selected_idx < 0 || selected_idx >= item_count) return;
    if (strcmp(items[selected_idx].name, "..") == 0) return;

    char path[256];
    build_item_path(selected_idx, path, sizeof(path));

    // Use userspace recursive delete - works for both files and directories
    delete_recursive(path);

    refresh_directory();
}

static void action_open_textedit(void) {
    if (selected_idx < 0 || selected_idx >= item_count) return;
    if (items[selected_idx].is_dir) return;

    char path[256];
    build_item_path(selected_idx, path, sizeof(path));

    // Launch textedit with the file
    char *argv[2];
    argv[0] = "/bin/textedit";
    argv[1] = path;
    api->exec_args("/bin/textedit", 2, argv);
}

static void action_open_terminal(void) {
    // Set cwd to current path and launch terminal
    api->set_cwd(current_path);
    api->exec("/bin/term");
}

static void do_menu_action(int item) {
    menu_visible = 0;

    switch (item) {
        case 0: action_new_file(); break;
        case 1: action_new_folder(); break;
        case 2: action_rename(); break;
        case 3: action_delete(); break;
        case 4: action_open_textedit(); break;
        case 5: action_open_terminal(); break;
    }
}

static void finish_rename(void) {
    if (!renaming) return;

    renaming = 0;

    // Empty name - cancel
    if (strlen(rename_buf) == 0) {
        if (creating_new) {
            // Remove the fake entry we added
            item_count--;
            creating_new = 0;
            creating_idx = -1;
        }
        return;
    }

    if (creating_new) {
        // Actually create the file/folder now
        char path[256];
        strcpy(path, current_path);
        if (strcmp(path, "/") != 0) strcat(path, "/");
        strcat(path, rename_buf);

        if (creating_new == 1) {
            api->create(path);
        } else {
            api->mkdir(path);
        }

        creating_new = 0;
        creating_idx = -1;
        refresh_directory();
    } else {
        // Renaming existing item
        if (strcmp(rename_buf, items[selected_idx].name) == 0) return;

        char path[256];
        build_item_path(selected_idx, path, sizeof(path));

        api->rename(path, rename_buf);
        refresh_directory();
    }
}

static void cancel_rename(void) {
    renaming = 0;

    if (creating_new) {
        // Remove the fake entry we added
        item_count--;
        creating_new = 0;
        creating_idx = -1;
    }
}

// ============ Drawing ============

static void draw_path_bar(void) {
    // Background
    buf_fill_rect(0, 0, win_w, PATH_BAR_HEIGHT + 4, COLOR_BG);
    // Rounded path input field
    buf_fill_rounded(4, 4, win_w - 8, PATH_BAR_HEIGHT - 4, 6, COLOR_PATH_BG);
    buf_draw_rounded(4, 4, win_w - 8, PATH_BAR_HEIGHT - 4, 6, COLOR_BORDER);

    // Draw path
    buf_draw_string_clip(10, 7, current_path, COLOR_FG, COLOR_PATH_BG, win_w - 20);
}

static void draw_item(int idx, int y) {
    file_entry_t *item = &items[idx];
    int is_selected = (idx == selected_idx);

    uint32_t bg = is_selected ? COLOR_SELECTED : COLOR_BG;
    uint32_t fg = is_selected ? COLOR_SEL_TEXT : COLOR_FG;

    // Background - clear first, then draw rounded selection
    buf_fill_rect(0, y, win_w - SCROLL_WIDTH, ITEM_HEIGHT, COLOR_BG);
    if (is_selected) {
        buf_fill_rounded(2, y + 1, win_w - SCROLL_WIDTH - 4, ITEM_HEIGHT - 2, 4, COLOR_SELECTED);
    }

    // Icon
    int icon_x = 6;
    int icon_y = y + 3;
    if (item->is_dir) {
        draw_folder_icon(icon_x, icon_y, bg);
    } else {
        draw_file_icon(icon_x, icon_y, bg);
    }

    // Name
    int text_x = 24;
    int text_y = y + 2;

    if (renaming && idx == selected_idx) {
        // Draw rename input box with rounded corners
        buf_fill_rounded(text_x, y + 1, win_w - SCROLL_WIDTH - text_x - 6, ITEM_HEIGHT - 2, 4, COLOR_PATH_BG);
        buf_draw_rounded(text_x, y + 1, win_w - SCROLL_WIDTH - text_x - 6, ITEM_HEIGHT - 2, 4, COLOR_BORDER);
        buf_draw_string_clip(text_x + 4, text_y, rename_buf, COLOR_FG, COLOR_PATH_BG, win_w - SCROLL_WIDTH - text_x - 14);

        // Cursor
        int cursor_x = text_x + 4 + rename_cursor * 8;
        for (int cy = y + 3; cy < y + ITEM_HEIGHT - 3; cy++) {
            if (cursor_x < win_w)
                win_buffer[cy * win_w + cursor_x] = COLOR_FG;
        }
    } else {
        buf_draw_string_clip(text_x, text_y, item->name, fg, is_selected ? COLOR_SELECTED : COLOR_BG, win_w - SCROLL_WIDTH - text_x - 4);
    }
}

static void draw_file_list(void) {
    int y = PATH_BAR_HEIGHT + 4;

    // Background
    buf_fill_rect(0, PATH_BAR_HEIGHT, win_w, win_h - PATH_BAR_HEIGHT, COLOR_BG);

    // Items
    for (int i = 0; i < MAX_VISIBLE && (scroll_offset + i) < item_count; i++) {
        draw_item(scroll_offset + i, y);
        y += ITEM_HEIGHT;
    }

    // Scroll bar track (subtle)
    buf_fill_rect(win_w - SCROLL_WIDTH, PATH_BAR_HEIGHT + 4, SCROLL_WIDTH, win_h - PATH_BAR_HEIGHT - 8, COLOR_BG);

    // Scroll thumb - modern rounded pill style
    if (item_count > MAX_VISIBLE) {
        int track_h = win_h - PATH_BAR_HEIGHT - 12;
        int thumb_h = (MAX_VISIBLE * track_h) / item_count;
        if (thumb_h < 24) thumb_h = 24;
        int thumb_y = PATH_BAR_HEIGHT + 6 + (scroll_offset * track_h) / item_count;

        // Rounded scroll thumb
        buf_fill_rounded(win_w - SCROLL_WIDTH + 3, thumb_y, SCROLL_WIDTH - 6, thumb_h, 4, 0x00CCCCCC);
    }
}

static void draw_context_menu(void) {
    if (!menu_visible) return;

    int menu_h = MENU_ITEM_COUNT * MENU_ITEM_HEIGHT + 8;

    // Adjust position if menu goes off screen
    int mx = menu_x;
    int my = menu_y;
    if (mx + MENU_WIDTH > win_w) mx = win_w - MENU_WIDTH;
    if (my + menu_h > win_h) my = win_h - menu_h;

    // Shadow (simple offset shadow)
    buf_fill_rounded(mx + 2, my + 2, MENU_WIDTH, menu_h, 8, 0x00666666);

    // Background with rounded corners
    buf_fill_rounded(mx, my, MENU_WIDTH, menu_h, 8, COLOR_MENU_BG);
    buf_draw_rounded(mx, my, MENU_WIDTH, menu_h, 8, COLOR_BORDER);

    // Items
    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        int item_y = my + 4 + i * MENU_ITEM_HEIGHT;

        if (i == menu_hover) {
            // Rounded selection highlight
            buf_fill_rounded(mx + 4, item_y, MENU_WIDTH - 8, MENU_ITEM_HEIGHT, 4, COLOR_MENU_HL);
            buf_draw_string(mx + 12, item_y + 4, menu_items[i], COLOR_SEL_TEXT, COLOR_MENU_HL);
        } else {
            buf_draw_string(mx + 12, item_y + 4, menu_items[i], COLOR_FG, COLOR_MENU_BG);
        }
    }
}

static void draw_all(void) {
    draw_path_bar();
    draw_file_list();
    draw_context_menu();
    api->window_invalidate(window_id);
}

// ============ Input Handling ============

static int item_at_point(int x, int y) {
    if (y < PATH_BAR_HEIGHT + 2) return -1;
    if (x > win_w - SCROLL_WIDTH) return -1;

    int row = (y - PATH_BAR_HEIGHT - 2) / ITEM_HEIGHT;
    int idx = scroll_offset + row;

    if (idx >= 0 && idx < item_count) return idx;
    return -1;
}

static int menu_item_at_point(int x, int y) {
    if (!menu_visible) return -1;

    int menu_h = MENU_ITEM_COUNT * MENU_ITEM_HEIGHT + 4;
    int mx = menu_x;
    int my = menu_y;
    if (mx + MENU_WIDTH > win_w) mx = win_w - MENU_WIDTH;
    if (my + menu_h > win_h) my = win_h - menu_h;

    if (x < mx || x >= mx + MENU_WIDTH) return -1;
    if (y < my + 2 || y >= my + menu_h - 2) return -1;

    return (y - my - 2) / MENU_ITEM_HEIGHT;
}

static void handle_key(int key) {
    if (renaming) {
        if (key == '\r' || key == '\n') {
            finish_rename();
        } else if (key == 27) {  // Escape
            cancel_rename();
        } else if (key == '\b' || key == 127) {  // Backspace
            if (rename_cursor > 0) {
                for (int i = rename_cursor - 1; rename_buf[i]; i++) {
                    rename_buf[i] = rename_buf[i + 1];
                }
                rename_cursor--;
            }
        } else if (key >= 32 && key < 127 && rename_cursor < 62) {
            // Insert character
            int len = strlen(rename_buf);
            for (int i = len + 1; i > rename_cursor; i--) {
                rename_buf[i] = rename_buf[i - 1];
            }
            rename_buf[rename_cursor++] = (char)key;
        }
        return;
    }

    // Close menu on any key
    if (menu_visible) {
        menu_visible = 0;
        return;
    }

    // Navigation keys
    if (key == 0x100) {  // Up
        if (selected_idx > 0) {
            selected_idx--;
            if (selected_idx < scroll_offset) scroll_offset = selected_idx;
        }
    } else if (key == 0x101) {  // Down
        if (selected_idx < item_count - 1) {
            selected_idx++;
            if (selected_idx >= scroll_offset + MAX_VISIBLE)
                scroll_offset = selected_idx - MAX_VISIBLE + 1;
        }
    } else if (key == '\r' || key == '\n') {
        enter_selected();
    } else if (key == '\b' || key == 127) {  // Backspace - go up
        navigate_up();
    }
}

// ============ Main ============

int main(kapi_t *kapi, int argc, char **argv) {
    (void)argc;
    (void)argv;

    api = kapi;

    if (!api->window_create) {
        api->puts("files: window API not available\n");
        return 1;
    }

    // Create window
    window_id = api->window_create(100, 80, WIN_WIDTH, WIN_HEIGHT + 18, "Files");
    if (window_id < 0) {
        api->puts("files: failed to create window\n");
        return 1;
    }

    win_buffer = api->window_get_buffer(window_id, &win_w, &win_h);
    if (!win_buffer) {
        api->puts("files: failed to get window buffer\n");
        api->window_destroy(window_id);
        return 1;
    }

    // Initialize graphics context
    gfx_init(&gfx, win_buffer, win_w, win_h, api->font_data);

    // Initialize with cwd
    api->get_cwd(current_path, sizeof(current_path));
    refresh_directory();

    // Initial draw
    draw_all();

    // Event loop
    int running = 1;
    int last_click_time = 0;
    int last_click_idx = -1;

    while (running) {
        int event_type, data1, data2, data3;
        while (api->window_poll_event(window_id, &event_type, &data1, &data2, &data3)) {
            switch (event_type) {
                case WIN_EVENT_CLOSE:
                    running = 0;
                    break;

                case WIN_EVENT_MOUSE_DOWN: {
                    int x = data1, y = data2;
                    int btn = data3;

                    // Check context menu click
                    if (menu_visible) {
                        int mi = menu_item_at_point(x, y);
                        if (mi >= 0) {
                            do_menu_action(mi);
                        } else {
                            menu_visible = 0;
                        }
                        draw_all();
                        break;
                    }

                    // Cancel rename on click elsewhere
                    if (renaming) {
                        finish_rename();
                        draw_all();
                    }

                    // Right click - show context menu
                    if (btn & MOUSE_BTN_RIGHT) {
                        int idx = item_at_point(x, y);
                        if (idx >= 0) selected_idx = idx;
                        menu_x = x;
                        menu_y = y;
                        menu_visible = 1;
                        menu_hover = -1;
                        draw_all();
                        break;
                    }

                    // Left click
                    int idx = item_at_point(x, y);
                    if (idx >= 0) {
                        // Double-click detection
                        int current_time = (int)api->get_uptime_ticks();
                        if (idx == last_click_idx && (current_time - last_click_time) < 50) {
                            // Double click
                            enter_selected();
                        } else {
                            selected_idx = idx;
                            last_click_idx = idx;
                            last_click_time = current_time;
                        }
                    } else {
                        selected_idx = -1;
                    }
                    draw_all();
                    break;
                }

                case WIN_EVENT_MOUSE_MOVE: {
                    if (menu_visible) {
                        int mi = menu_item_at_point(data1, data2);
                        if (mi != menu_hover) {
                            menu_hover = mi;
                            draw_all();
                        }
                    }
                    break;
                }

                case WIN_EVENT_KEY:
                    handle_key(data1);
                    draw_all();
                    break;

                case WIN_EVENT_RESIZE:
                    // Re-fetch buffer with new dimensions
                    win_buffer = api->window_get_buffer(window_id, &win_w, &win_h);
                    gfx_init(&gfx, win_buffer, win_w, win_h, api->font_data);
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
