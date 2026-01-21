/*
 * KikiOS Mouse Driver
 *
 * Virtio tablet/mouse input for GUI
 */

#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>

// Mouse button states
#define MOUSE_BTN_LEFT   0x01
#define MOUSE_BTN_RIGHT  0x02
#define MOUSE_BTN_MIDDLE 0x04

// Initialize mouse
int mouse_init(void);

// Get current mouse position (absolute, 0-32767 range from device)
void mouse_get_pos(int *x, int *y);

// Get current mouse position scaled to screen
void mouse_get_screen_pos(int *x, int *y);

// Get button state (bitmask of MOUSE_BTN_*)
uint8_t mouse_get_buttons(void);

// Check if mouse has new events
int mouse_has_event(void);

// Set mouse position (for mouse capture/warp)
void mouse_set_pos(int x, int y);

// Get mouse delta (returns accumulated movement and clears it)
void mouse_get_delta(int *dx, int *dy);

// Poll for mouse events (call regularly)
void mouse_poll(void);

// Get the mouse's IRQ number
uint32_t mouse_get_irq(void);

// IRQ handler - called from irq.c
void mouse_irq_handler(void);

#endif
