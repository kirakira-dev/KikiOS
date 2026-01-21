/*
 * led - Control the ACT LED on Raspberry Pi
 *
 * Usage: led on|off|status
 * Only works on Pi hardware (no-op on QEMU).
 */

#include "../lib/kiki.h"

int main(kapi_t *k, int argc, char **argv) {
    if (argc != 2) {
        k->puts("Usage: led on|off|status\n");
        return 1;
    }

    if (strcmp(argv[1], "on") == 0) {
        k->led_on();
        k->puts("LED on\n");
    } else if (strcmp(argv[1], "off") == 0) {
        k->led_off();
        k->puts("LED off\n");
    } else if (strcmp(argv[1], "status") == 0) {
        if (k->led_status()) {
            k->puts("LED is on\n");
        } else {
            k->puts("LED is off\n");
        }
    } else {
        k->puts("Usage: led on|off|status\n");
        return 1;
    }

    return 0;
}
