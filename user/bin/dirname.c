/*
 * dirname - strip last component from file name
 *
 * Usage: dirname <path>
 * Examples:
 *   dirname /usr/bin/sort   -> /usr/bin
 *   dirname stdio.h         -> .
 *   dirname /usr/           -> /
 *   dirname /               -> /
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
        out_puts("Usage: dirname <path>\n");
        return 1;
    }

    const char *path = argv[1];

    // Find the length
    int len = 0;
    while (path[len]) len++;

    // Remove trailing slashes (but not the first one)
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

    if (last_slash < 0) {
        // No slash found - return "."
        out_puts(".\n");
    } else if (last_slash == 0) {
        // Slash at start - return "/"
        out_puts("/\n");
    } else {
        // Remove trailing slashes from directory part
        int dir_len = last_slash;
        while (dir_len > 1 && path[dir_len - 1] == '/') {
            dir_len--;
        }

        // Print directory part
        for (int i = 0; i < dir_len; i++) {
            out_putc(path[i]);
        }
        out_putc('\n');
    }

    return 0;
}
