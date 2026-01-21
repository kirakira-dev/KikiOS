/*
 * kill - terminate a process
 *
 * Usage: kill <pid>
 */

#include "../lib/kiki.h"

static kapi_t *api;

static void out_puts(const char *s) {
    if (api->stdio_puts) api->stdio_puts(s);
    else api->puts(s);
}

static int parse_int(const char *s) {
    int n = 0;
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        s++;
    }
    return n;
}

int main(kapi_t *k, int argc, char **argv) {
    api = k;

    if (argc < 2) {
        out_puts("Usage: kill <pid>\n");
        return 1;
    }

    int pid = parse_int(argv[1]);
    if (pid <= 0) {
        out_puts("kill: invalid pid\n");
        return 1;
    }

    int result = k->kill_process(pid);
    if (result < 0) {
        out_puts("kill: failed to kill process ");
        // Print pid manually
        char buf[16];
        int i = 0;
        int n = pid;
        while (n > 0) {
            buf[i++] = '0' + (n % 10);
            n /= 10;
        }
        while (i > 0) {
            if (api->stdio_putc) api->stdio_putc(buf[--i]);
            else api->putc(buf[--i]);
        }
        out_puts("\n");
        return 1;
    }

    return 0;
}
