/*
 * Raspberry Pi USB HID Mouse Driver
 * Converts USB HID boot mouse reports to absolute screen coordinates
 */

#include "../hal.h"
#include "../../printf.h"
#include "../../string.h"
#include "usb/usb_hid.h"

// Screen dimensions (from framebuffer)
extern uint32_t fb_width, fb_height;

// Current mouse state
static int mouse_x = 400;      // Start at screen center
static int mouse_y = 300;
static int mouse_buttons = 0;
static int mouse_initialized = 0;

// USB HID boot mouse report format:
// Byte 0: Buttons (bit 0=left, bit 1=right, bit 2=middle)
// Byte 1: X displacement (signed 8-bit)
// Byte 2: Y displacement (signed 8-bit)
// Bytes 3-7: Optional wheel and extra data

static void process_mouse_report(uint8_t *report) {
    // Update buttons
    mouse_buttons = report[0] & 0x07;

    // Get signed displacements
    int8_t dx = (int8_t)report[1];
    int8_t dy = (int8_t)report[2];

    // Scale mouse movement (sensitivity adjustment)
    // USB mice report small deltas, so multiply for reasonable cursor speed
    int scale = 2;

    // Update position
    mouse_x += dx * scale;
    mouse_y += dy * scale;

    // Clamp to screen bounds
    int max_x = (int)fb_width - 1;
    int max_y = (int)fb_height - 1;

    if (mouse_x < 0) mouse_x = 0;
    if (mouse_x > max_x) mouse_x = max_x;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_y > max_y) mouse_y = max_y;
}

// Poll USB mouse and process reports
static void poll_usb_mouse(void) {
    uint8_t report[8];
    int ret;

    // Process all pending reports
    while ((ret = hal_usb_mouse_poll(report, 8)) > 0) {
        process_mouse_report(report);
    }
}

/*
 * HAL Interface Implementation
 */

int hal_mouse_init(void) {
    // Set initial position to center of screen
    if (fb_width > 0 && fb_height > 0) {
        mouse_x = fb_width / 2;
        mouse_y = fb_height / 2;
    }

    mouse_initialized = 1;
    printf("[MOUSE] Pi USB mouse driver initialized\n");
    return 0;
}

void hal_mouse_get_state(int *x, int *y, int *buttons) {
    // Poll for new data
    poll_usb_mouse();

    if (x) *x = mouse_x;
    if (y) *y = mouse_y;
    if (buttons) *buttons = mouse_buttons;
}

uint32_t hal_mouse_get_irq(void) {
    // USB mouse uses polling via USB IRQ, no dedicated mouse IRQ
    return 0;
}

void hal_mouse_irq_handler(void) {
    // USB mouse is polled via hal_usb_keyboard_tick which handles both
    poll_usb_mouse();
}

void hal_mouse_set_pos(int x, int y) {
    mouse_x = x;
    mouse_y = y;

    // Clamp to screen bounds
    int max_x = (int)fb_width - 1;
    int max_y = (int)fb_height - 1;
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_x > max_x) mouse_x = max_x;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_y > max_y) mouse_y = max_y;
}
