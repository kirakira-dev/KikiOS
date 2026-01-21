/*
 * hostname - print system hostname
 *
 * KikiOS hostname is "vibeos"
 */

#include "../lib/kiki.h"

int main(kapi_t *k, int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (k->stdio_puts) {
        k->stdio_puts("vibeos\n");
    } else {
        k->puts("vibeos\n");
    }

    return 0;
}
