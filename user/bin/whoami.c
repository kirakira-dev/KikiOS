/*
 * whoami - print current user name
 *
 * KikiOS has a single user: "user"
 */

#include "../lib/kiki.h"

int main(kapi_t *k, int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (k->stdio_puts) {
        k->stdio_puts("user\n");
    } else {
        k->puts("user\n");
    }

    return 0;
}
