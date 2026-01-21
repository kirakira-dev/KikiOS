/*
 * uptime - show system uptime
 */

#include "../lib/kiki.h"

static kapi_t *api;

static void out_putc(char c) {
    if (api->stdio_putc) api->stdio_putc(c);
    else api->putc(c);
}

static void out_puts(const char *s) {
    if (api->stdio_puts) api->stdio_puts(s);
    else api->puts(s);
}

static void print_num(unsigned long n) {
    if (n == 0) {
        out_putc('0');
        return;
    }
    char buf[20];
    int i = 0;
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

    // Get ticks (100 ticks/second with 10ms timer)
    unsigned long ticks = k->get_uptime_ticks();

    // Convert to time units
    unsigned long total_seconds = ticks / 100;
    unsigned long hours = total_seconds / 3600;
    unsigned long minutes = (total_seconds % 3600) / 60;
    unsigned long seconds = total_seconds % 60;

    out_puts("up ");

    if (hours > 0) {
        print_num(hours);
        out_puts("h ");
    }

    if (hours > 0 || minutes > 0) {
        print_num(minutes);
        out_puts("m ");
    }

    print_num(seconds);
    out_puts("s (");
    print_num(ticks);
    out_puts(" ticks)\n");

    return 0;
}
