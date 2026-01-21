/*
 * grep - search for patterns in files
 *
 * Usage: grep [-i] [-n] [-v] <pattern> <file...>
 *   -i  case insensitive
 *   -n  show line numbers
 *   -v  invert match (show non-matching lines)
 *
 * Simple substring matching only (no regex).
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

static void print_num(int n) {
    char buf[16];
    int i = 0;

    if (n == 0) {
        out_putc('0');
        return;
    }

    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }

    while (i > 0) {
        out_putc(buf[--i]);
    }
}

static char to_lower(char c) {
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    return c;
}

// Check if pattern is found in line (substring search)
static int contains(const char *line, const char *pattern, int case_insensitive) {
    for (int i = 0; line[i]; i++) {
        int j = 0;
        int match = 1;

        while (pattern[j]) {
            char lc = line[i + j];
            char pc = pattern[j];

            if (lc == '\0') {
                match = 0;
                break;
            }

            if (case_insensitive) {
                lc = to_lower(lc);
                pc = to_lower(pc);
            }

            if (lc != pc) {
                match = 0;
                break;
            }
            j++;
        }

        if (match) return 1;
    }
    return 0;
}

int main(kapi_t *k, int argc, char **argv) {
    api = k;

    int case_insensitive = 0;
    int show_line_numbers = 0;
    int invert = 0;
    const char *pattern = NULL;
    const char *files[16];
    int file_count = 0;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (int j = 1; argv[i][j]; j++) {
                switch (argv[i][j]) {
                    case 'i': case_insensitive = 1; break;
                    case 'n': show_line_numbers = 1; break;
                    case 'v': invert = 1; break;
                }
            }
        } else if (!pattern) {
            pattern = argv[i];
        } else if (file_count < 16) {
            files[file_count++] = argv[i];
        }
    }

    if (!pattern || file_count == 0) {
        out_puts("Usage: grep [-inv] <pattern> <file...>\n");
        return 1;
    }

    int show_filename = (file_count > 1);
    int status = 1;  // No matches found yet

    for (int f = 0; f < file_count; f++) {
        void *file = k->open(files[f]);
        if (!file) {
            out_puts("grep: ");
            out_puts(files[f]);
            out_puts(": No such file\n");
            continue;
        }

        if (k->is_dir(file)) {
            out_puts("grep: ");
            out_puts(files[f]);
            out_puts(": Is a directory\n");
            continue;
        }

        // Read file line by line
        char line[1024];
        int line_pos = 0;
        int line_num = 0;
        char buf[512];
        size_t offset = 0;
        int bytes;

        while ((bytes = k->read(file, buf, sizeof(buf), offset)) > 0) {
            for (int i = 0; i < bytes; i++) {
                if (buf[i] == '\n' || line_pos >= 1023) {
                    line[line_pos] = '\0';
                    line_num++;

                    // Check for match
                    int match = contains(line, pattern, case_insensitive);
                    if (invert) match = !match;

                    if (match) {
                        status = 0;  // Found at least one match

                        if (show_filename) {
                            out_puts(files[f]);
                            out_putc(':');
                        }
                        if (show_line_numbers) {
                            print_num(line_num);
                            out_putc(':');
                        }
                        out_puts(line);
                        out_putc('\n');
                    }

                    line_pos = 0;
                } else {
                    line[line_pos++] = buf[i];
                }
            }
            offset += bytes;
        }

        // Handle last line without newline
        if (line_pos > 0) {
            line[line_pos] = '\0';
            line_num++;

            int match = contains(line, pattern, case_insensitive);
            if (invert) match = !match;

            if (match) {
                status = 0;

                if (show_filename) {
                    out_puts(files[f]);
                    out_putc(':');
                }
                if (show_line_numbers) {
                    print_num(line_num);
                    out_putc(':');
                }
                out_puts(line);
                out_putc('\n');
            }
        }
    }

    return status;
}
