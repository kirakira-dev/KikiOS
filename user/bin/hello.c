/*
 * Hello World - First KikiOS userspace program!
 */

#include "../lib/kiki.h"

int main(kapi_t *k, int argc, char **argv) {
    (void)argc; (void)argv;  // unused
    k->set_color(COLOR_GREEN, COLOR_BLACK);
    k->puts("Hello from userspace!\n");
    k->set_color(COLOR_WHITE, COLOR_BLACK);
    k->puts("The vibes are immaculate.\n");
    return 0;
}
