/*
 * clear - clear the terminal screen
 */

#include "../lib/kiki.h"

int main(kapi_t *k, int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (k->stdio_putc) {
        // Terminal mode - send form feed
        k->stdio_putc('\f');
    } else {
        // Console mode - call clear directly
        k->clear();
    }

    return 0;
}
