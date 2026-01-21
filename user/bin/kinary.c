/*
 * Kinary - KikiOS Binary/Hex Viewer
 *
 * View files in hexadecimal format with ASCII display.
 * Terminal-based tool similar to hexdump.
 *
 * Usage: kinary <file>
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

static const char hex_chars[] = "0123456789abcdef";

static void print_hex_byte(unsigned char b) {
    out_putc(hex_chars[(b >> 4) & 0xF]);
    out_putc(hex_chars[b & 0xF]);
}

static void print_hex_offset(int offset) {
    for (int i = 7; i >= 0; i--) {
        out_putc(hex_chars[(offset >> (i * 4)) & 0xF]);
    }
}

static int is_printable(unsigned char c) {
    return c >= 0x20 && c < 0x7F;
}

int main(kapi_t *k, int argc, char **argv) {
    api = k;

    if (argc < 2) {
        out_puts("Kinary - KikiOS Binary/Hex Viewer\n\n");
        out_puts("Usage: kinary <file>\n\n");
        out_puts("Displays file contents in hex+ASCII format.\n");
        out_puts("Output format: OFFSET  HEX BYTES  |ASCII|\n");
        return 1;
    }

    const char *filename = argv[1];

    void *file = k->open(filename);
    if (!file) {
        out_puts("kinary: ");
        out_puts(filename);
        out_puts(": No such file\n");
        return 1;
    }

    if (k->is_dir(file)) {
        out_puts("kinary: ");
        out_puts(filename);
        out_puts(": Is a directory\n");
        return 1;
    }

    // Print header
    out_puts("Kinary - ");
    out_puts(filename);
    out_puts("\n");
    out_puts("--------  ");
    out_puts("00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F  ");
    out_puts("|0123456789ABCDEF|\n");
    out_puts("--------  ");
    out_puts("-----------------------------------------------  ");
    out_puts("-----------------\n");

    unsigned char buf[16];
    size_t offset = 0;
    int bytes;

    while ((bytes = k->read(file, (char *)buf, 16, offset)) > 0) {
        // Print offset
        print_hex_offset(offset);
        out_puts("  ");

        // Print hex bytes (first 8)
        for (int i = 0; i < 8; i++) {
            if (i < bytes) {
                print_hex_byte(buf[i]);
                out_putc(' ');
            } else {
                out_puts("   ");
            }
        }
        out_putc(' ');

        // Print hex bytes (next 8)
        for (int i = 8; i < 16; i++) {
            if (i < bytes) {
                print_hex_byte(buf[i]);
                out_putc(' ');
            } else {
                out_puts("   ");
            }
        }

        // Print ASCII representation
        out_puts(" |");
        for (int i = 0; i < 16; i++) {
            if (i < bytes) {
                out_putc(is_printable(buf[i]) ? buf[i] : '.');
            } else {
                out_putc(' ');
            }
        }
        out_puts("|\n");

        offset += bytes;
    }

    // Print final offset
    out_puts("--------\n");
    out_puts("Total: ");
    
    // Print size in decimal
    char size_str[16];
    int si = 0;
    size_t n = offset;
    if (n == 0) size_str[si++] = '0';
    else {
        char tmp[16];
        int ti = 0;
        while (n > 0) { tmp[ti++] = '0' + (n % 10); n /= 10; }
        while (ti > 0) size_str[si++] = tmp[--ti];
    }
    size_str[si] = '\0';
    out_puts(size_str);
    out_puts(" bytes (0x");
    print_hex_offset(offset);
    out_puts(")\n");

    return 0;
}
