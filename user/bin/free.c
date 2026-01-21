/*
 * free - display memory usage
 *
 * Usage: free [-h] [-m] [-k]
 *   -h  human-readable
 *   -m  show in megabytes
 *   -k  show in kilobytes (default)
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

static void print_num_padded(size_t n, int width) {
    char buf[20];
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

    while (i > 0) {
        out_putc(buf[--i]);
    }
}

int main(kapi_t *k, int argc, char **argv) {
    api = k;

    int unit = 1024;  // Default: KB
    const char *suffix = "K";

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (int j = 1; argv[i][j]; j++) {
                switch (argv[i][j]) {
                    case 'm':
                        unit = 1024 * 1024;
                        suffix = "M";
                        break;
                    case 'k':
                        unit = 1024;
                        suffix = "K";
                        break;
                    case 'h':
                        // Human readable - use MB for clarity
                        unit = 1024 * 1024;
                        suffix = "M";
                        break;
                }
            }
        }
    }

    size_t total = k->get_ram_total();
    size_t used = k->get_mem_used();
    size_t freemem = k->get_mem_free();

    // Convert to units
    size_t total_u = total / unit;
    size_t used_u = used / unit;
    size_t free_u = freemem / unit;

    // Header
    out_puts("              total       used       free\n");

    // Mem row
    out_puts("Mem:    ");
    print_num_padded(total_u, 10);
    out_puts(suffix);
    print_num_padded(used_u, 10);
    out_puts(suffix);
    print_num_padded(free_u, 10);
    out_puts(suffix);
    out_putc('\n');

    return 0;
}
