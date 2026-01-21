/*
 * tail - output the last part of files
 *
 * Usage: tail [-n lines] <file>
 * Default: 10 lines
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

static int parse_int(const char *s) {
    int n = 0;
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        s++;
    }
    return n;
}

int main(kapi_t *k, int argc, char **argv) {
    api = k;

    int num_lines = 10;
    const char *filename = NULL;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == 'n') {
            if (argv[i][2]) {
                num_lines = parse_int(argv[i] + 2);
            } else if (i + 1 < argc) {
                num_lines = parse_int(argv[++i]);
            }
        } else if (argv[i][0] == '-' && argv[i][1] >= '0' && argv[i][1] <= '9') {
            num_lines = parse_int(argv[i] + 1);
        } else {
            filename = argv[i];
        }
    }

    if (!filename) {
        out_puts("Usage: tail [-n lines] <file>\n");
        return 1;
    }

    void *file = k->open(filename);
    if (!file) {
        out_puts("tail: ");
        out_puts(filename);
        out_puts(": No such file\n");
        return 1;
    }

    if (k->is_dir(file)) {
        out_puts("tail: ");
        out_puts(filename);
        out_puts(": Is a directory\n");
        return 1;
    }

    int file_size = k->file_size(file);
    if (file_size <= 0) {
        return 0;  // Empty file
    }

    // Allocate buffer for the file (or portion of it)
    // For simplicity, read up to 64KB from the end
    int buf_size = (file_size < 65536) ? file_size : 65536;
    char *buf = k->malloc(buf_size + 1);
    if (!buf) {
        out_puts("tail: out of memory\n");
        return 1;
    }

    // Read from end
    size_t offset = (file_size > buf_size) ? file_size - buf_size : 0;
    int bytes = k->read(file, buf, buf_size, offset);

    if (bytes <= 0) {
        k->free(buf);
        return 0;
    }

    // Find the start position for last N lines
    int line_count = 0;
    int start_pos = bytes;

    // Count backwards from end to find N newlines
    for (int i = bytes - 1; i >= 0; i--) {
        if (buf[i] == '\n') {
            line_count++;
            if (line_count > num_lines) {
                start_pos = i + 1;
                break;
            }
        }
    }

    // If we didn't find enough newlines, start from beginning
    if (line_count <= num_lines && start_pos == bytes) {
        start_pos = 0;
    }

    // Output from start_pos to end
    for (int i = start_pos; i < bytes; i++) {
        out_putc(buf[i]);
    }

    k->free(buf);
    return 0;
}
