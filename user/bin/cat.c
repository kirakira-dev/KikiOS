/*
 * cat - concatenate and print files
 *
 * Supports multiple files: cat file1 file2
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
        out_puts("Usage: cat <file> [...]\n");
        return 1;
    }

    int status = 0;

    for (int i = 1; i < argc; i++) {
        void *file = k->open(argv[i]);
        if (!file) {
            out_puts("cat: ");
            out_puts(argv[i]);
            out_puts(": No such file\n");
            status = 1;
            continue;
        }

        if (k->is_dir(file)) {
            out_puts("cat: ");
            out_puts(argv[i]);
            out_puts(": Is a directory\n");
            status = 1;
            continue;
        }

        // Read and print file contents
        char buf[256];
        size_t offset = 0;
        int bytes;

        while ((bytes = k->read(file, buf, sizeof(buf) - 1, offset)) > 0) {
            buf[bytes] = '\0';
            out_puts(buf);
            offset += bytes;
        }
    }

    return status;
}
