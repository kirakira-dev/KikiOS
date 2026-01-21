/*
 * lsusb - list USB devices
 *
 * Usage: lsusb
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

static void print_hex4(uint16_t n) {
    const char *hex = "0123456789abcdef";
    out_putc(hex[(n >> 12) & 0xF]);
    out_putc(hex[(n >> 8) & 0xF]);
    out_putc(hex[(n >> 4) & 0xF]);
    out_putc(hex[n & 0xF]);
}

static void print_num(int n, int width) {
    char buf[16];
    int i = 0;

    if (n == 0) {
        buf[i++] = '0';
    } else {
        while (n > 0) {
            buf[i++] = '0' + (n % 10);
            n /= 10;
        }
    }

    // Pad with zeros
    while (i < width) {
        out_putc('0');
        width--;
    }

    while (i > 0) {
        out_putc(buf[--i]);
    }
}

int main(kapi_t *k, int argc, char **argv) {
    (void)argc;
    (void)argv;
    api = k;

    int count = k->usb_device_count();

    if (count == 0) {
        out_puts("No USB devices (using virtio on QEMU)\n");
        return 0;
    }

    for (int i = 0; i < count; i++) {
        uint16_t vid, pid;
        char name[64];

        if (k->usb_device_info(i, &vid, &pid, name, sizeof(name)) == 0) {
            out_puts("Bus 001 Device ");
            print_num(i + 1, 3);
            out_puts(": ID ");
            print_hex4(vid);
            out_putc(':');
            print_hex4(pid);
            out_putc(' ');
            out_puts(name);
            out_putc('\n');
        }
    }

    return 0;
}
