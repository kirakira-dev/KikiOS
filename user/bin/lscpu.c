/*
 * lscpu - display CPU information
 *
 * Usage: lscpu
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

static void print_num(uint32_t n) {
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

int main(kapi_t *k, int argc, char **argv) {
    (void)argc;
    (void)argv;
    api = k;

    out_puts("Architecture:        aarch64\n");

    out_puts("CPU(s):              ");
    print_num(k->get_cpu_cores());
    out_putc('\n');

    out_puts("Model name:          ");
    out_puts(k->get_cpu_name());
    out_putc('\n');

    out_puts("CPU MHz:             ");
    print_num(k->get_cpu_freq_mhz());
    out_putc('\n');

    // Calculate total RAM in MB
    size_t ram_bytes = k->get_ram_total();
    uint32_t ram_mb = (uint32_t)(ram_bytes / (1024 * 1024));
    out_puts("Total RAM:           ");
    print_num(ram_mb);
    out_puts(" MB\n");

    return 0;
}
