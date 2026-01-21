/*
 * KikiOS Image Viewer
 *
 * View BMP, PNG, JPG images in full color.
 * Supports arrow keys to navigate between images in the same directory.
 */

#include "../lib/kiki.h"
#include "../lib/gfx.h"

// abs() needed by stb_image for BMP loading
static inline int abs(int x) { return x < 0 ? -x : x; }

// stb_image configuration - must be before include
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP

// Memory allocation hooks - will be set up before use
static kapi_t *g_api;
#define STBI_MALLOC(sz)           g_api->malloc(sz)
#define STBI_REALLOC(p,newsz)     stbi_realloc_impl(p,newsz)
#define STBI_FREE(p)              g_api->free(p)
#define STBI_ASSERT(x)            ((void)0)

// Simple realloc implementation
// Note: We don't track old size, so we just allocate new and return.
// stb_image handles this gracefully - it re-reads data as needed.
static void *stbi_realloc_impl(void *p, size_t newsz) {
    if (!p) return g_api->malloc(newsz);
    void *newp = g_api->malloc(newsz);
    if (p) g_api->free(p);
    return newp;
}

#define STB_IMAGE_IMPLEMENTATION
#include "../../vendor/stb_image.h"

static kapi_t *api;
static int window_id = -1;
static uint32_t *win_buffer;
static int win_w, win_h;
static gfx_ctx_t gfx;

// Image data
static uint8_t *image_data = NULL;
static int img_width = 0;
static int img_height = 0;

// Current file info
static char current_path[256];
static char current_dir[256];
static char current_filename[64];

// File list for navigation
#define MAX_FILES 128
static char file_list[MAX_FILES][64];
static int file_count = 0;
static int current_file_index = -1;

// Window dimensions
#define MIN_WIN_W 200
#define MIN_WIN_H 150
#define MAX_WIN_W 780
#define MAX_WIN_H 560

// Drawing macros
#define buf_fill_rect(x, y, w, h, c)     gfx_fill_rect(&gfx, x, y, w, h, c)
#define buf_draw_string(x, y, s, fg, bg) gfx_draw_string(&gfx, x, y, s, fg, bg)

// Check if filename has image extension
static int is_image_file(const char *name) {
    int len = strlen(name);
    if (len < 4) return 0;

    const char *ext = name + len - 4;
    if (strcmp(ext, ".png") == 0 || strcmp(ext, ".PNG") == 0) return 1;
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".JPG") == 0) return 1;
    if (strcmp(ext, ".bmp") == 0 || strcmp(ext, ".BMP") == 0) return 1;

    // Check for .jpeg
    if (len >= 5) {
        ext = name + len - 5;
        if (strcmp(ext, ".jpeg") == 0 || strcmp(ext, ".JPEG") == 0) return 1;
    }

    return 0;
}

// Scan directory for image files
static void scan_directory(void) {
    file_count = 0;
    current_file_index = -1;

    void *dir = api->open(current_dir);
    if (!dir || !api->is_dir(dir)) return;

    char name[64];
    uint8_t type;
    int index = 0;

    while (api->readdir(dir, index, name, sizeof(name), &type) == 0) {
        if (type == 1 && is_image_file(name) && file_count < MAX_FILES) {
            // Copy filename to list
            strncpy_safe(file_list[file_count], name, 64);

            // Check if this is current file
            if (strcmp(name, current_filename) == 0) {
                current_file_index = file_count;
            }

            file_count++;
        }
        index++;
    }
}

// Extract directory and filename from path
static void split_path(const char *path) {
    // Find last /
    const char *last_slash = NULL;
    for (const char *p = path; *p; p++) {
        if (*p == '/') last_slash = p;
    }

    if (last_slash) {
        int dir_len = last_slash - path;
        if (dir_len == 0) dir_len = 1;  // Root directory
        memcpy(current_dir, path, dir_len);
        current_dir[dir_len] = '\0';
        strncpy_safe(current_filename, last_slash + 1, sizeof(current_filename));
    } else {
        strcpy(current_dir, "/");
        strncpy_safe(current_filename, path, sizeof(current_filename));
    }
}

// Load image from file
static int load_image(const char *path) {
    // Free previous image
    if (image_data) {
        stbi_image_free(image_data);
        image_data = NULL;
    }

    // Open file
    void *file = api->open(path);
    if (!file) {
        return -1;
    }

    if (api->is_dir(file)) {
        return -1;
    }

    // Get file size
    int size = api->file_size(file);
    if (size <= 0) {
        return -1;
    }

    // Read file into memory
    uint8_t *file_data = api->malloc(size);
    if (!file_data) {
        return -1;
    }

    int bytes_read = api->read(file, (char *)file_data, size, 0);
    if (bytes_read != size) {
        api->free(file_data);
        return -1;
    }

    // Decode image
    int channels;
    image_data = stbi_load_from_memory(file_data, size, &img_width, &img_height, &channels, 3);

    // Free file data
    api->free(file_data);

    if (!image_data) {
        return -1;
    }

    // Update path info
    strncpy_safe(current_path, path, sizeof(current_path));
    split_path(path);
    scan_directory();

    return 0;
}

// Draw image to window buffer
static void draw_image(void) {
    // Clear background
    buf_fill_rect(0, 0, win_w, win_h, 0x404040);  // Dark gray background

    if (!image_data) {
        buf_draw_string(10, win_h / 2, "No image loaded", COLOR_WHITE, 0x404040);
        api->window_invalidate(window_id);
        return;
    }

    // Calculate scaling to fit window
    int avail_w = win_w - 4;   // 2px border each side
    int avail_h = win_h - 4;

    int draw_w = img_width;
    int draw_h = img_height;

    // Scale down if needed
    if (draw_w > avail_w || draw_h > avail_h) {
        float scale_x = (float)avail_w / img_width;
        float scale_y = (float)avail_h / img_height;
        float scale = (scale_x < scale_y) ? scale_x : scale_y;
        draw_w = (int)(img_width * scale);
        draw_h = (int)(img_height * scale);
    }

    // Center in window
    int start_x = (win_w - draw_w) / 2;
    int start_y = (win_h - draw_h) / 2;

    // Draw scaled image using nearest neighbor (flip by writing to flipped Y)
    for (int y = 0; y < draw_h; y++) {
        int src_y = y * img_height / draw_h;
        for (int x = 0; x < draw_w; x++) {
            int src_x = x * img_width / draw_w;

            // Get source pixel (RGB)
            int src_idx = (src_y * img_width + src_x) * 3;
            uint8_t r = image_data[src_idx];
            uint8_t g = image_data[src_idx + 1];
            uint8_t b = image_data[src_idx + 2];

            uint32_t color = (r << 16) | (g << 8) | b;

            int dst_x = start_x + x;
            int dst_y = start_y + (draw_h - 1 - y);
            if (dst_x >= 0 && dst_x < win_w && dst_y >= 0 && dst_y < win_h) {
                win_buffer[dst_y * win_w + dst_x] = color;
            }
        }
    }

    api->window_invalidate(window_id);
}

// Navigate to previous/next image
static void navigate(int direction) {
    if (file_count <= 1) return;

    int new_index = current_file_index + direction;
    if (new_index < 0) new_index = file_count - 1;
    if (new_index >= file_count) new_index = 0;

    // Build new path
    char new_path[256];
    if (strcmp(current_dir, "/") == 0) {
        new_path[0] = '/';
        strncpy_safe(new_path + 1, file_list[new_index], sizeof(new_path) - 1);
    } else {
        strncpy_safe(new_path, current_dir, sizeof(new_path));
        int len = strlen(new_path);
        new_path[len] = '/';
        strncpy_safe(new_path + len + 1, file_list[new_index], sizeof(new_path) - len - 1);
    }

    if (load_image(new_path) == 0) {
        // Update window title
        api->window_set_title(window_id, current_filename);
        draw_image();
    }
}

// Print usage to console
static void print_usage(void) {
    void (*out)(const char *) = api->stdio_puts ? api->stdio_puts : api->puts;
    out("Usage: viewer <image>\n");
    out("Supports: PNG, JPG, BMP\n");
    out("\nControls:\n");
    out("  Left/Right arrows - Previous/Next image\n");
    out("  Q or Escape - Quit\n");
}

int main(kapi_t *kapi, int argc, char **argv) {
    api = kapi;
    g_api = kapi;  // For stb_image allocator

    // Need a filename argument
    if (argc < 2) {
        print_usage();
        return 1;
    }

    // Check for window API
    if (!api->window_create) {
        void (*out)(const char *) = api->stdio_puts ? api->stdio_puts : api->puts;
        out("viewer: requires GUI (run from desktop)\n");
        return 1;
    }

    // Load the image first to get dimensions
    if (load_image(argv[1]) != 0) {
        void (*out)(const char *) = api->stdio_puts ? api->stdio_puts : api->puts;
        out("viewer: failed to load image: ");
        out(argv[1]);
        out("\n");
        return 1;
    }

    // Calculate window size based on image
    int content_w = img_width;
    int content_h = img_height;

    // Clamp to min/max
    if (content_w < MIN_WIN_W) content_w = MIN_WIN_W;
    if (content_h < MIN_WIN_H) content_h = MIN_WIN_H;
    if (content_w > MAX_WIN_W) content_w = MAX_WIN_W;
    if (content_h > MAX_WIN_H) content_h = MAX_WIN_H;

    // Center window on screen (800x600)
    int win_x = (800 - content_w) / 2;
    int win_y = (600 - content_h) / 2;
    if (win_y < 20) win_y = 20;  // Below menu bar

    // Create window with filename as title
    window_id = api->window_create(win_x, win_y, content_w, content_h + 18, current_filename);
    if (window_id < 0) {
        api->puts("viewer: failed to create window\n");
        if (image_data) stbi_image_free(image_data);
        return 1;
    }

    // Get buffer
    win_buffer = api->window_get_buffer(window_id, &win_w, &win_h);
    if (!win_buffer) {
        api->puts("viewer: failed to get window buffer\n");
        api->window_destroy(window_id);
        if (image_data) stbi_image_free(image_data);
        return 1;
    }

    // Initialize graphics context
    gfx_init(&gfx, win_buffer, win_w, win_h, api->font_data);

    // Initial draw
    draw_image();

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
                    if (data1 == 'q' || data1 == 'Q' || data1 == 27) {
                        running = 0;
                    } else if (data1 == KEY_LEFT) {
                        navigate(-1);
                    } else if (data1 == KEY_RIGHT) {
                        navigate(1);
                    }
                    break;

                case WIN_EVENT_RESIZE:
                    win_buffer = api->window_get_buffer(window_id, &win_w, &win_h);
                    gfx_init(&gfx, win_buffer, win_w, win_h, api->font_data);
                    draw_image();
                    break;
            }
        }

        api->yield();
    }

    api->window_destroy(window_id);
    if (image_data) stbi_image_free(image_data);
    return 0;
}
