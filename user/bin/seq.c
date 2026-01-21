/*
 * seq - print a sequence of numbers
 *
 * Usage: seq [first [increment]] last
 *   seq 5          -> 1 2 3 4 5
 *   seq 2 5        -> 2 3 4 5
 *   seq 1 2 10     -> 1 3 5 7 9
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

// Print integer
static void print_int(int n) {
    if (n < 0) {
        out_putc('-');
        n = -n;
    }

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
    api = k;

    if (argc < 2) {
        out_puts("Usage: seq [first [increment]] last\n");
        return 1;
    }

    int first = 1;
    int increment = 1;
    int last;

    if (argc == 2) {
        // seq last
        last = parse_int(argv[1]);
    } else if (argc == 3) {
        // seq first last
        first = parse_int(argv[1]);
        last = parse_int(argv[2]);
    } else {
        // seq first increment last
        first = parse_int(argv[1]);
        increment = parse_int(argv[2]);
        last = parse_int(argv[3]);
    }

    if (increment == 0) {
        out_puts("seq: increment cannot be zero\n");
        return 1;
    }

    // Generate sequence
    if (increment > 0) {
        for (int i = first; i <= last; i += increment) {
            print_int(i);
            out_putc('\n');
        }
    } else {
        for (int i = first; i >= last; i += increment) {
            print_int(i);
            out_putc('\n');
        }
    }

    return 0;
}
