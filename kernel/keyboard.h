/*
 * KikiOS Keyboard Driver
 *
 * Virtio keyboard input
 */

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

// Initialize keyboard
int keyboard_init(void);

// Get a character (returns -1 if none available)
int keyboard_getc(void);

// Check if key is available
int keyboard_has_key(void);

// IRQ handler (called from irq.c)
void keyboard_irq_handler(void);

// Get the keyboard's IRQ number
uint32_t keyboard_get_irq(void);

// Enable interrupt-driven mode (disables polling)
void keyboard_enable_irq_mode(void);

#endif
