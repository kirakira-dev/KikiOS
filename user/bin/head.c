/*
 * head - output the first part of files
 *
 * Usage: head [-n lines] <file>
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
            // -n <num> or -n<num>
            if (argv[i][2]) {
                num_lines = parse_int(argv[i] + 2);
            } else if (i + 1 < argc) {
                num_lines = parse_int(argv[++i]);
            }
        } else if (argv[i][0] == '-' && argv[i][1] >= '0' && argv[i][1] <= '9') {
            // -<num> shorthand
            num_lines = parse_int(argv[i] + 1);
        } else {
            filename = argv[i];
        }
    }

    if (!filename) {
        out_puts("Usage: head [-n lines] <file>\n");
        return 1;
    }

    void *file = k->open(filename);
    if (!file) {
        out_puts("head: ");
        out_puts(filename);
        out_puts(": No such file\n");
        return 1;
    }

    if (k->is_dir(file)) {
        out_puts("head: ");
        out_puts(filename);
        out_puts(": Is a directory\n");
        return 1;
    }

    // Read and output lines
    char buf[512];
    size_t offset = 0;
    int lines_printed = 0;
    int bytes;

    while (lines_printed < num_lines && (bytes = k->read(file, buf, sizeof(buf), offset)) > 0) {
        for (int i = 0; i < bytes && lines_printed < num_lines; i++) {
            out_putc(buf[i]);
            if (buf[i] == '\n') {
                lines_printed++;
            }
        }
        offset += bytes;
    }

    return 0;
}
