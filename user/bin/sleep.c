/*
 * sleep - pause for a specified number of seconds
 *
 * Usage: sleep <seconds>
 */

#include "../lib/kiki.h"

static kapi_t *api;

static void out_puts(const char *s) {
    if (api->stdio_puts) api->stdio_puts(s);
    else api->puts(s);
}

// Simple atoi
static int parse_int(const char *s) {
    int n = 0;
    int neg = 0;

    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; }

    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        s++;
    }

    return neg ? -n : n;
}

int main(kapi_t *k, int argc, char **argv) {
    api = k;

    if (argc < 2) {
        out_puts("Usage: sleep <seconds>\n");
        return 1;
    }

    int seconds = parse_int(argv[1]);
    if (seconds <= 0) {
        out_puts("sleep: invalid time interval\n");
        return 1;
    }

    // Sleep in 1-second chunks, yielding to allow interruption
    for (int i = 0; i < seconds; i++) {
        k->sleep_ms(1000);
    }

    return 0;
}
