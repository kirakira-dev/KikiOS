/*
 * readtest - read a file and discard data (benchmark disk read speed)
 *
 * Usage: readtest <file>
 *   Reads entire file, reports bytes read. No output to screen (no FB bottleneck).
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

static void print_num(unsigned long n) {
    char buf[24];
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

int main(kapi_t *k, int argc, char **argv) {
    api = k;

    if (argc < 2) {
        out_puts("usage: readtest <file>\n");
        return 1;
    }

    void *file = k->open(argv[1]);
    if (!file) {
        out_puts("readtest: cannot open ");
        out_puts(argv[1]);
        out_putc('\n');
        return 1;
    }

    if (k->is_dir(file)) {
        out_puts("readtest: is a directory\n");
        return 1;
    }

    unsigned long size = k->file_size(file);
    if (size == 0) {
        out_puts("readtest: empty file\n");
        return 0;
    }

    /* Allocate read buffer - 64KB chunks for multi-block reads */
    #define READ_BUF_SIZE (64 * 1024)
    char *buf = k->malloc(READ_BUF_SIZE);
    if (!buf) {
        out_puts("readtest: out of memory\n");
        return 1;
    }

    unsigned long total = 0;
    unsigned long offset = 0;

    while (offset < size) {
        unsigned long chunk = size - offset;
        if (chunk > READ_BUF_SIZE) chunk = READ_BUF_SIZE;

        int rd = k->read(file, buf, chunk, offset);
        if (rd <= 0) break;

        total += rd;
        offset += rd;
    }

    k->free(buf);

    out_puts("read ");
    print_num(total);
    out_puts(" bytes\n");

    return 0;
}
