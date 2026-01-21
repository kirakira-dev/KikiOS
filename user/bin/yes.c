/*
 * yes - output a string repeatedly until killed
 *
 * Usage: yes [string]
 * Default string is "y"
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

    const char *str = "y";
    if (argc > 1) {
        str = argv[1];
    }

    // Output string repeatedly
    // Yield occasionally to not hog CPU and allow Ctrl+C
    int count = 0;
    while (1) {
        out_puts(str);
        out_putc('\n');
        count++;

        // Yield every 100 lines to allow interruption
        if (count >= 100) {
            k->yield();
            count = 0;
        }
    }

    return 0;
}
