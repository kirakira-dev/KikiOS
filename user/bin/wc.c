/*
 * wc - word, line, and byte count
 *
 * Usage: wc [-l] [-w] [-c] <file...>
 *   -l  lines only
 *   -w  words only
 *   -c  bytes/chars only
 * Default: all three
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

static void print_num(int n, int width) {
    char buf[16];
    int i = 0;

    if (n == 0) {
        buf[i++] = '0';
    } else {
        while (n > 0) {
            buf[i++] = '0' + (n % 10);
            n /= 10;
        }
    }

    // Pad with spaces
    while (i < width) {
        out_putc(' ');
        width--;
    }

    // Print digits in reverse
    while (i > 0) {
        out_putc(buf[--i]);
    }
}

static int is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

int main(kapi_t *k, int argc, char **argv) {
    api = k;

    int show_lines = 0, show_words = 0, show_bytes = 0;
    int file_count = 0;
    const char *files[16];

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (int j = 1; argv[i][j]; j++) {
                switch (argv[i][j]) {
                    case 'l': show_lines = 1; break;
                    case 'w': show_words = 1; break;
                    case 'c': show_bytes = 1; break;
                }
            }
        } else {
            if (file_count < 16) {
                files[file_count++] = argv[i];
            }
        }
    }

    // Default: show all
    if (!show_lines && !show_words && !show_bytes) {
        show_lines = show_words = show_bytes = 1;
    }

    if (file_count == 0) {
        out_puts("Usage: wc [-lwc] <file...>\n");
        return 1;
    }

    int total_lines = 0, total_words = 0, total_bytes = 0;
    int status = 0;

    for (int f = 0; f < file_count; f++) {
        void *file = k->open(files[f]);
        if (!file) {
            out_puts("wc: ");
            out_puts(files[f]);
            out_puts(": No such file\n");
            status = 1;
            continue;
        }

        if (k->is_dir(file)) {
            out_puts("wc: ");
            out_puts(files[f]);
            out_puts(": Is a directory\n");
            status = 1;
            continue;
        }

        int lines = 0, words = 0, bytes = 0;
        int in_word = 0;
        char buf[512];
        size_t offset = 0;
        int n;

        while ((n = k->read(file, buf, sizeof(buf), offset)) > 0) {
            for (int i = 0; i < n; i++) {
                bytes++;

                if (buf[i] == '\n') {
                    lines++;
                }

                if (is_whitespace(buf[i])) {
                    in_word = 0;
                } else {
                    if (!in_word) {
                        words++;
                        in_word = 1;
                    }
                }
            }
            offset += n;
        }

        // Print counts
        if (show_lines) print_num(lines, 8);
        if (show_words) print_num(words, 8);
        if (show_bytes) print_num(bytes, 8);
        out_putc(' ');
        out_puts(files[f]);
        out_putc('\n');

        total_lines += lines;
        total_words += words;
        total_bytes += bytes;
    }

    // Print total if multiple files
    if (file_count > 1) {
        if (show_lines) print_num(total_lines, 8);
        if (show_words) print_num(total_words, 8);
        if (show_bytes) print_num(total_bytes, 8);
        out_puts(" total\n");
    }

    return status;
}
