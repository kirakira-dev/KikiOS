/*
 * ps - report process status
 *
 * Usage: ps
 * Shows all running processes with PID, state, and name.
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

static void print_num_padded(int n, int width) {
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

    // Pad with spaces
    while (i < width) {
        out_putc(' ');
        width--;
    }

    while (i > 0) {
        out_putc(buf[--i]);
    }
}

static const char *state_name(int state) {
    switch (state) {
        case 0: return "FREE  ";
        case 1: return "READY ";
        case 2: return "RUN   ";
        case 3: return "BLOCK ";
        case 4: return "ZOMBIE";
        default: return "???   ";
    }
}

int main(kapi_t *k, int argc, char **argv) {
    (void)argc;
    (void)argv;
    api = k;

    out_puts("  PID  STATE   NAME\n");

    // Iterate through all process slots (MAX_PROCESSES = 16)
    for (int i = 0; i < 16; i++) {
        char name[32];
        int state;

        if (k->get_process_info(i, name, sizeof(name), &state)) {
            // Slot is in use (state != FREE)
            if (state != 0) {  // Skip FREE slots
                print_num_padded(i, 5);
                out_puts("  ");
                out_puts(state_name(state));
                out_puts("  ");
                out_puts(name);
                out_putc('\n');
            }
        }
    }

    return 0;
}
