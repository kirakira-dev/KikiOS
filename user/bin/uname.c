/*
 * uname - print system information
 *
 * Usage: uname [-a] [-s] [-n] [-r] [-m]
 *   -s  kernel name (KikiOS)
 *   -n  hostname (vibeos)
 *   -r  kernel release (0.1)
 *   -m  machine (aarch64)
 *   -a  all of the above
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

    int show_s = 0, show_n = 0, show_r = 0, show_m = 0;

    if (argc < 2) {
        // Default: just kernel name
        show_s = 1;
    } else {
        for (int i = 1; i < argc; i++) {
            if (argv[i][0] == '-') {
                for (int j = 1; argv[i][j]; j++) {
                    switch (argv[i][j]) {
                        case 'a': show_s = show_n = show_r = show_m = 1; break;
                        case 's': show_s = 1; break;
                        case 'n': show_n = 1; break;
                        case 'r': show_r = 1; break;
                        case 'm': show_m = 1; break;
                    }
                }
            }
        }
    }

    // If nothing specified after parsing, default to -s
    if (!show_s && !show_n && !show_r && !show_m) {
        show_s = 1;
    }

    int first = 1;

    if (show_s) {
        if (!first) out_putc(' ');
        out_puts("KikiOS");
        first = 0;
    }

    if (show_n) {
        if (!first) out_putc(' ');
        out_puts("vibeos");
        first = 0;
    }

    if (show_r) {
        if (!first) out_putc(' ');
        out_puts("0.1");
        first = 0;
    }

    if (show_m) {
        if (!first) out_putc(' ');
        out_puts("aarch64");
        first = 0;
    }

    out_putc('\n');

    return 0;
}
