/*
 * basename - strip directory and suffix from filenames
 *
 * Usage: basename <path> [suffix]
 * Examples:
 *   basename /usr/bin/sort       -> sort
 *   basename /foo/bar/           -> bar
 *   basename file.txt .txt       -> file
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

int main(kapi_t *k, int argc, char **argv) {
    api = k;

    if (argc < 2) {
        out_puts("Usage: basename <path> [suffix]\n");
        return 1;
    }

    const char *path = argv[1];
    const char *suffix = (argc > 2) ? argv[2] : NULL;

    // Find the length of the path
    int len = 0;
    while (path[len]) len++;

    // Remove trailing slashes
    while (len > 1 && path[len - 1] == '/') {
        len--;
    }

    // Find the last slash
    int last_slash = -1;
    for (int i = 0; i < len; i++) {
        if (path[i] == '/') {
            last_slash = i;
        }
    }

    // Get the base name start
    const char *base = (last_slash >= 0) ? path + last_slash + 1 : path;
    int base_len = len - (last_slash + 1);

    // Remove suffix if specified and matches
    if (suffix) {
        int suffix_len = 0;
        while (suffix[suffix_len]) suffix_len++;

        if (base_len > suffix_len) {
            // Check if suffix matches end of base
            int match = 1;
            for (int i = 0; i < suffix_len; i++) {
                if (base[base_len - suffix_len + i] != suffix[i]) {
                    match = 0;
                    break;
                }
            }
            if (match) {
                base_len -= suffix_len;
            }
        }
    }

    // Print result
    for (int i = 0; i < base_len; i++) {
        out_putc(base[i]);
    }
    out_putc('\n');

    return 0;
}
