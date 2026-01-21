/*
 * KikiOS ping command
 *
 * Usage: ping <ip>
 * Example: ping 10.0.2.2
 */

#include "../lib/kiki.h"

static kapi_t *k;

// Simple output helper
static void out_puts(const char *s) {
    if (k->stdio_puts) k->stdio_puts(s);
    else k->puts(s);
}

static void out_putc(char c) {
    if (k->stdio_putc) k->stdio_putc(c);
    else k->putc(c);
}

// Print a number
static void out_num(int n) {
    if (n < 0) {
        out_putc('-');
        n = -n;
    }
    if (n == 0) {
        out_putc('0');
        return;
    }
    char buf[12];
    int i = 0;
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i > 0) {
        out_putc(buf[--i]);
    }
}

// Check if string is an IP address (contains only digits and dots)
static int is_ip_address(const char *s) {
    while (*s) {
        if ((*s < '0' || *s > '9') && *s != '.') return 0;
        s++;
    }
    return 1;
}

// Parse IP address from string "a.b.c.d"
static int parse_ip(const char *s, uint32_t *ip) {
    int parts[4] = {0, 0, 0, 0};
    int part = 0;
    int val = 0;

    while (*s) {
        if (*s >= '0' && *s <= '9') {
            val = val * 10 + (*s - '0');
            if (val > 255) return -1;
        } else if (*s == '.') {
            if (part >= 3) return -1;
            parts[part++] = val;
            val = 0;
        } else {
            return -1;
        }
        s++;
    }

    if (part != 3) return -1;
    parts[3] = val;

    *ip = MAKE_IP(parts[0], parts[1], parts[2], parts[3]);
    return 0;
}

// Print IP address
static void print_ip(uint32_t ip) {
    out_num((ip >> 24) & 0xff);
    out_putc('.');
    out_num((ip >> 16) & 0xff);
    out_putc('.');
    out_num((ip >> 8) & 0xff);
    out_putc('.');
    out_num(ip & 0xff);
}

int main(kapi_t *kapi, int argc, char **argv) {
    k = kapi;

    if (argc < 2) {
        out_puts("Usage: ping <ip or hostname>\n");
        out_puts("Example: ping 10.0.2.2\n");
        out_puts("         ping google.com\n");
        return 1;
    }

    uint32_t ip;
    const char *target = argv[1];

    // Check if it's an IP address or hostname
    if (is_ip_address(target)) {
        if (parse_ip(target, &ip) < 0) {
            out_puts("Invalid IP address: ");
            out_puts(target);
            out_puts("\n");
            return 1;
        }
    } else {
        // It's a hostname - resolve via DNS
        out_puts("Resolving ");
        out_puts(target);
        out_puts("...\n");

        ip = k->dns_resolve(target);
        if (ip == 0) {
            out_puts("Could not resolve hostname: ");
            out_puts(target);
            out_puts("\n");
            return 1;
        }
    }

    out_puts("PING ");
    out_puts(target);
    out_puts(" (");
    print_ip(ip);
    out_puts(")\n");

    int sent = 0;
    int received = 0;

    // Send 4 pings
    for (int i = 0; i < 4; i++) {
        int result = k->net_ping(ip, i + 1, 1000);  // 1 second timeout

        if (result == 0) {
            out_puts("Reply from ");
            print_ip(ip);
            out_puts(": seq=");
            out_num(i + 1);
            out_puts("\n");
            received++;
        } else {
            out_puts("Request timed out: seq=");
            out_num(i + 1);
            out_puts("\n");
        }
        sent++;

        // Wait a bit between pings
        k->sleep_ms(500);
    }

    out_puts("\n--- ");
    print_ip(ip);
    out_puts(" ping statistics ---\n");
    out_num(sent);
    out_puts(" packets transmitted, ");
    out_num(received);
    out_puts(" received, ");
    out_num(((sent - received) * 100) / sent);
    out_puts("% packet loss\n");

    return 0;
}
