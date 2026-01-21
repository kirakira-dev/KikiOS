/*
 * which - locate a command
 *
 * Usage: which <command>
 * Searches /bin for the command and prints the full path if found.
 */

#include "../lib/kiki.h"

static kapi_t *api;

static void out_puts(const char *s) {
    if (api->stdio_puts) api->stdio_puts(s);
    else api->puts(s);
}

int main(kapi_t *k, int argc, char **argv) {
    api = k;

    if (argc < 2) {
        out_puts("Usage: which <command>\n");
        return 1;
    }

    int status = 0;

    for (int i = 1; i < argc; i++) {
        // Build path /bin/<command>
        char path[256];
        int j = 0;

        // Add /bin/ prefix
        const char *prefix = "/bin/";
        while (*prefix) {
            path[j++] = *prefix++;
        }

        // Add command name
        const char *cmd = argv[i];
        while (*cmd && j < 255) {
            path[j++] = *cmd++;
        }
        path[j] = '\0';

        // Check if file exists
        void *file = k->open(path);
        if (file && !k->is_dir(file)) {
            out_puts(path);
            out_puts("\n");
        } else {
            out_puts(argv[i]);
            out_puts(" not found\n");
            status = 1;
        }
    }

    return status;
}
