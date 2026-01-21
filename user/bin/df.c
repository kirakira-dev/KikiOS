/*
 * df - display disk space usage
 *
 * Usage: df [-h]
 *   -h  human-readable (KB/MB)
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
        // GB
        print_num(kb / (1024 * 1024));
        out_putc('.');
        print_num((kb % (1024 * 1024)) / (1024 * 102));  // One decimal
        out_puts("G");
    } else if (kb >= 1024) {
        // MB
        print_num(kb / 1024);
        out_putc('.');
        print_num((kb % 1024) / 102);
        out_puts("M");
    } else {
        print_num(kb);
        out_puts("K");
    }
}

int main(kapi_t *k, int argc, char **argv) {
    api = k;

    int human = 0;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == 'h') {
            human = 1;
        }
    }

    int total = k->get_disk_total();
    int freek = k->get_disk_free();
    int used = total - freek;
    int percent = (total > 0) ? (used * 100 / total) : 0;

    // Header
    out_puts("Filesystem      Size     Used    Avail  Use%  Mounted on\n");

    // Data row
    out_puts("/dev/disk0  ");

    if (human) {
        print_human(total);
        out_puts("    ");
        print_human(used);
        out_puts("    ");
        print_human(freek);
    } else {
        print_num(total);
        out_puts("K  ");
        print_num(used);
        out_puts("K  ");
        print_num(freek);
        out_puts("K");
    }

    out_puts("   ");
    print_num(percent);
    out_puts("%   /\n");

    return 0;
}
