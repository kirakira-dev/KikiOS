/*
 * QEMU HAL Keyboard Stubs
 *
 * On QEMU, keyboard input is handled by the virtio-input driver in keyboard.c
 * These are just stubs to satisfy the HAL interface.
 */

#include "../hal.h"

int hal_keyboard_init(void) {
    // Virtio keyboard init is done in keyboard.c
    return 0;
}

int hal_keyboard_getc(void) {
    // Virtio keyboard handles this directly
    return -1;
}

uint32_t hal_keyboard_get_irq(void) {
    // IRQ handled by keyboard.c
    return 0;
}

void hal_keyboard_irq_handler(void) {
    // Handled by keyboard_irq_handler() in keyboard.c
}
