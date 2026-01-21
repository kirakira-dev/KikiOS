/*
 * hexdump - display file contents in hexadecimal
 *
 * Usage: hexdump [-C] <file>
 *   -C  canonical hex+ASCII format
 *
 * Default: hex dump format (16 bytes per line)
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
    // Print 8-digit hex offset
    for (int i = 7; i >= 0; i--) {
        out_putc(hex_chars[(offset >> (i * 4)) & 0xF]);
    }
}

static int is_printable(unsigned char c) {
    return c >= 0x20 && c < 0x7F;
}

int main(kapi_t *k, int argc, char **argv) {
    api = k;

    int canonical = 0;
    const char *filename = NULL;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == 'C') {
            canonical = 1;
        } else {
            filename = argv[i];
        }
    }

    if (!filename) {
        out_puts("Usage: hexdump [-C] <file>\n");
        return 1;
    }

    void *file = k->open(filename);
    if (!file) {
        out_puts("hexdump: ");
        out_puts(filename);
        out_puts(": No such file\n");
        return 1;
    }

    if (k->is_dir(file)) {
        out_puts("hexdump: ");
        out_puts(filename);
        out_puts(": Is a directory\n");
        return 1;
    }

    unsigned char buf[16];
    size_t offset = 0;
    int bytes;

    while ((bytes = k->read(file, (char *)buf, 16, offset)) > 0) {
        if (canonical) {
            // Canonical format: offset | hex | ASCII
            print_hex_offset(offset);
            out_puts("  ");

            // First 8 bytes
            for (int i = 0; i < 8; i++) {
                if (i < bytes) {
                    print_hex_byte(buf[i]);
                    out_putc(' ');
                } else {
                    out_puts("   ");
                }
            }
            out_putc(' ');

            // Second 8 bytes
            for (int i = 8; i < 16; i++) {
                if (i < bytes) {
                    print_hex_byte(buf[i]);
                    out_putc(' ');
                } else {
                    out_puts("   ");
                }
            }

            // ASCII representation
            out_puts(" |");
            for (int i = 0; i < bytes; i++) {
                out_putc(is_printable(buf[i]) ? buf[i] : '.');
            }
            out_puts("|\n");
        } else {
            // Simple hex format
            print_hex_offset(offset);
            out_putc(' ');

            for (int i = 0; i < bytes; i++) {
                out_putc(' ');
                print_hex_byte(buf[i]);
            }
            out_putc('\n');
        }

        offset += bytes;
    }

    // Print final offset for canonical format
    if (canonical) {
        print_hex_offset(offset);
        out_putc('\n');
    }

    return 0;
}
