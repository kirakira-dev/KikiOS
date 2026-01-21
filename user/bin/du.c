/*
 * du - estimate file space usage
 *
 * Usage: du [-h] [-s] <path...>
 *   -h  human-readable (K, M, G)
 *   -s  summary only (total for each argument)
 *
 * Shows disk usage in 1K blocks by default.
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

static void print_human(int kb) {
    if (kb >= 1024 * 1024) {
        print_num(kb / (1024 * 1024));
        out_putc('G');
    } else if (kb >= 1024) {
        print_num(kb / 1024);
        out_putc('M');
    } else {
        print_num(kb);
        out_putc('K');
    }
}

static int str_len(const char *s) {
    int len = 0;
    while (*s++) len++;
    return len;
}

static void str_cpy(char *dst, const char *src) {
    while ((*dst++ = *src++));
}

// Calculate size recursively
// Returns size in bytes
static int calc_size(const char *path, int summary, int human) {
    void *node = api->open(path);
    if (!node) {
        return 0;
    }

    if (!api->is_dir(node)) {
        // Regular file
        int size = api->file_size(node);
        int kb = (size + 1023) / 1024;  // Round up to 1K blocks
        if (kb == 0) kb = 1;  // Minimum 1K

        if (!summary) {
            if (human) {
                print_human(kb);
            } else {
                print_num(kb);
            }
            out_putc('\t');
            out_puts(path);
            out_putc('\n');
        }
        return size;
    }

    // Directory - recurse
    int total = 0;
    char name[256];
    uint8_t type;
    int idx = 0;

    while (api->readdir(node, idx, name, sizeof(name), &type) == 0) {
        idx++;

        // Skip . and ..
        if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
            continue;
        }

        // Build full path
        char child_path[512];
        str_cpy(child_path, path);
        int plen = str_len(child_path);
        if (plen > 0 && child_path[plen - 1] != '/') {
            child_path[plen++] = '/';
            child_path[plen] = '\0';
        }
        str_cpy(child_path + plen, name);

        total += calc_size(child_path, summary, human);
    }

    // Print directory total
    int kb = (total + 1023) / 1024;
    if (kb == 0) kb = 1;

    if (!summary) {
        if (human) {
            print_human(kb);
        } else {
            print_num(kb);
        }
        out_putc('\t');
        out_puts(path);
        out_putc('\n');
    }

    return total;
}

int main(kapi_t *k, int argc, char **argv) {
    api = k;

    int human = 0;
    int summary = 0;
    const char *paths[16];
    int path_count = 0;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (int j = 1; argv[i][j]; j++) {
                switch (argv[i][j]) {
                    case 'h': human = 1; break;
                    case 's': summary = 1; break;
                }
            }
        } else if (path_count < 16) {
            paths[path_count++] = argv[i];
        }
    }

    // Default to current directory
    if (path_count == 0) {
        char cwd[256];
        k->get_cwd(cwd, sizeof(cwd));
        paths[0] = cwd;
        path_count = 1;

        int total = calc_size(cwd, summary, human);

        if (summary) {
            int kb = (total + 1023) / 1024;
            if (kb == 0) kb = 1;

            if (human) {
                print_human(kb);
            } else {
                print_num(kb);
            }
            out_putc('\t');
            out_puts(cwd);
            out_putc('\n');
        }
        return 0;
    }

    for (int i = 0; i < path_count; i++) {
        int total = calc_size(paths[i], summary, human);

        if (summary) {
            int kb = (total + 1023) / 1024;
            if (kb == 0) kb = 1;

            if (human) {
                print_human(kb);
            } else {
                print_num(kb);
            }
            out_putc('\t');
            out_puts(paths[i]);
            out_putc('\n');
        }
    }

    return 0;
}
