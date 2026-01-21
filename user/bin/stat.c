/*
 * stat - display file status
 *
 * Usage: stat <file...>
 * Shows file size and type.
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

    if (n < 0) {
        out_putc('-');
        n = -n;
    }

    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }

    while (i > 0) {
        out_putc(buf[--i]);
    }
}

int main(kapi_t *k, int argc, char **argv) {
    api = k;

    if (argc < 2) {
        out_puts("Usage: stat <file...>\n");
        return 1;
    }

    int status = 0;

    for (int i = 1; i < argc; i++) {
        void *file = k->open(argv[i]);
        if (!file) {
            out_puts("stat: cannot stat '");
            out_puts(argv[i]);
            out_puts("': No such file or directory\n");
            status = 1;
            continue;
        }

        out_puts("  File: ");
        out_puts(argv[i]);
        out_putc('\n');

        out_puts("  Size: ");
        print_num(k->file_size(file));
        out_puts(" bytes\n");

        out_puts("  Type: ");
        if (k->is_dir(file)) {
            out_puts("directory\n");
        } else {
            out_puts("regular file\n");
        }

        if (i < argc - 1) {
            out_putc('\n');
        }
    }

    return status;
}
