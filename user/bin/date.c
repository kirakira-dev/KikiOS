/*
 * date - display the current date and time
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

static void print_num(int n, int width) {
    // Print number with leading zeros
    char buf[12];
    int i = 0;
    int neg = 0;

    if (n < 0) {
        neg = 1;
        n = -n;
    }

    if (n == 0) {
        buf[i++] = '0';
    } else {
        while (n > 0) {
            buf[i++] = '0' + (n % 10);
            n /= 10;
        }
    }

    if (neg) out_putc('-');

    // Pad with leading zeros
    while (i < width) {
        out_putc('0');
        width--;
    }

    // Print digits in reverse
    while (i > 0) {
        out_putc(buf[--i]);
    }
}

static const char *day_names[] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

static const char *month_names[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

int main(kapi_t *k, int argc, char **argv) {
    (void)argc;
    (void)argv;
    api = k;

    int year, month, day, hour, minute, second, weekday;
    k->get_datetime(&year, &month, &day, &hour, &minute, &second, &weekday);

    // Format: "Mon Dec  8 14:30:45 2025"
    out_puts(day_names[weekday]);
    out_putc(' ');
    out_puts(month_names[month - 1]);
    out_putc(' ');
    if (day < 10) out_putc(' ');
    print_num(day, 1);
    out_putc(' ');
    print_num(hour, 2);
    out_putc(':');
    print_num(minute, 2);
    out_putc(':');
    print_num(second, 2);
    out_putc(' ');
    print_num(year, 4);
    out_putc('\n');

    return 0;
}
