/*
 * Raspberry Pi USB HID Keyboard Driver
 * Converts USB HID boot keyboard reports to ASCII
 */

#include "../hal.h"
#include "../../printf.h"
#include "../../string.h"

// Key buffer
#define KEY_BUF_SIZE 64
static int key_buffer[KEY_BUF_SIZE];
static volatile int key_buf_read = 0;
static volatile int key_buf_write = 0;

// Previous report for detecting key changes
static uint8_t prev_report[8] = {0};

// Key repeat timing (in ticks, 10ms each)
#define REPEAT_DELAY_TICKS  30   // 300ms initial delay
#define REPEAT_RATE_TICKS   2    // 20ms repeat rate (~50 chars/sec)

// Per-key repeat state
typedef struct {
    uint8_t scancode;
    uint64_t press_tick;       // When first pressed
    uint64_t last_repeat_tick; // Last repeat event
} key_repeat_state_t;

static key_repeat_state_t repeat_keys[6];  // Max 6 simultaneous keys
static int num_repeat_keys = 0;
static uint8_t current_modifiers = 0;      // Track modifiers for repeat

// USB HID modifier bits
#define MOD_LCTRL   (1 << 0)
#define MOD_LSHIFT  (1 << 1)
#define MOD_LALT    (1 << 2)
#define MOD_LGUI    (1 << 3)
#define MOD_RCTRL   (1 << 4)
#define MOD_RSHIFT  (1 << 5)
#define MOD_RALT    (1 << 6)
#define MOD_RGUI    (1 << 7)

// USB HID scancode to ASCII (unshifted)
static const char hid_to_ascii[128] = {
    0, 0, 0, 0,                     // 0x00-0x03: Reserved
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',  // 0x04-0x10
    'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',  // 0x11-0x1D
    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',  // 0x1E-0x27
    '\n',   // 0x28: Enter
    0x1B,   // 0x29: Escape
    '\b',   // 0x2A: Backspace
    '\t',   // 0x2B: Tab
    ' ',    // 0x2C: Space
    '-', '=', '[', ']', '\\',  // 0x2D-0x31
    0,      // 0x32: Non-US #
    ';', '\'', '`', ',', '.', '/',  // 0x33-0x38
    0,      // 0x39: Caps Lock
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0x3A-0x45: F1-F12
    0, 0, 0,  // 0x46-0x48: PrintScreen, ScrollLock, Pause
    0, 0, 0,  // 0x49-0x4B: Insert, Home, PageUp
    0x7F,   // 0x4C: Delete
    0, 0,   // 0x4D-0x4E: End, PageDown
    0, 0, 0, 0,  // 0x4F-0x52: Arrow keys (Right, Left, Down, Up)
};

// USB HID scancode to ASCII (shifted)
static const char hid_to_ascii_shift[128] = {
    0, 0, 0, 0,
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    '!', '@', '#', '$', '%', '^', '&', '*', '(', ')',
    '\n', 0x1B, '\b', '\t', ' ',
    '_', '+', '{', '}', '|',
    0,
    ':', '"', '~', '<', '>', '?',
};

// Special key codes (values >= 0x100)
#define KEY_UP      0x100
#define KEY_DOWN    0x101
#define KEY_LEFT    0x102
#define KEY_RIGHT   0x103
#define KEY_HOME    0x104
#define KEY_END     0x105
#define KEY_DELETE  0x106

static void key_buffer_put(int c) {
    int next = (key_buf_write + 1) % KEY_BUF_SIZE;
    if (next != key_buf_read) {
        key_buffer[key_buf_write] = c;
        key_buf_write = next;
    }
}

// Check if a scancode is newly pressed (not in previous report)
static int is_new_key(uint8_t scancode, uint8_t *prev) {
    for (int i = 2; i < 8; i++) {
        if (prev[i] == scancode) {
            return 0;  // Was already pressed
        }
    }
    return 1;
}

// Translate HID scancode to character (returns 0 for non-character keys)
static int scancode_to_char(uint8_t scancode, uint8_t modifiers) {
    int shift = (modifiers & (MOD_LSHIFT | MOD_RSHIFT)) != 0;
    int ctrl = (modifiers & (MOD_LCTRL | MOD_RCTRL)) != 0;
    int c = 0;

    // Handle special keys
    if (scancode == 0x52) c = KEY_UP;
    else if (scancode == 0x51) c = KEY_DOWN;
    else if (scancode == 0x50) c = KEY_LEFT;
    else if (scancode == 0x4F) c = KEY_RIGHT;
    else if (scancode == 0x4A) c = KEY_HOME;
    else if (scancode == 0x4D) c = KEY_END;
    else if (scancode == 0x4C) c = KEY_DELETE;
    else if (scancode < 128) {
        // Regular keys
        if (shift) {
            c = hid_to_ascii_shift[scancode];
        }
        if (c == 0) {
            c = hid_to_ascii[scancode];
        }

        // Handle Ctrl+key
        if (ctrl && c >= 'a' && c <= 'z') {
            c = c - 'a' + 1;
        } else if (ctrl && c >= 'A' && c <= 'Z') {
            c = c - 'A' + 1;
        }
    }

    return c;
}

// Check if a key is in the current report
static int is_key_in_report(uint8_t scancode, uint8_t *report) {
    for (int i = 2; i < 8; i++) {
        if (report[i] == scancode) return 1;
    }
    return 0;
}

// Add a key to repeat tracking
static void add_repeat_key(uint8_t scancode) {
    if (num_repeat_keys >= 6) return;
    uint64_t now = hal_timer_get_ticks();
    repeat_keys[num_repeat_keys].scancode = scancode;
    repeat_keys[num_repeat_keys].press_tick = now;
    repeat_keys[num_repeat_keys].last_repeat_tick = now;
    num_repeat_keys++;
}

// Remove released keys from repeat tracking
static void remove_released_keys(uint8_t *report) {
    int write_idx = 0;
    for (int i = 0; i < num_repeat_keys; i++) {
        if (is_key_in_report(repeat_keys[i].scancode, report)) {
            // Key still held - keep it
            if (write_idx != i) {
                repeat_keys[write_idx] = repeat_keys[i];
            }
            write_idx++;
        }
    }
    num_repeat_keys = write_idx;
}

// Process a HID report and generate key events
static void process_hid_report(uint8_t *report) {
    current_modifiers = report[0];

    // Remove keys that were released
    remove_released_keys(report);

    // Process each pressed key in bytes 2-7
    for (int i = 2; i < 8; i++) {
        uint8_t scancode = report[i];
        if (scancode == 0) continue;

        // Only process newly pressed keys
        if (!is_new_key(scancode, prev_report)) continue;

        int c = scancode_to_char(scancode, current_modifiers);
        if (c != 0) {
            key_buffer_put(c);
            // Track this key for repeat
            add_repeat_key(scancode);
        }
    }

    // Save current report as previous
    memcpy(prev_report, report, 8);
}

// Check for key repeats and generate events
static void check_key_repeats(void) {
    if (num_repeat_keys == 0) return;

    uint64_t now = hal_timer_get_ticks();

    for (int i = 0; i < num_repeat_keys; i++) {
        key_repeat_state_t *k = &repeat_keys[i];
        uint64_t held_time = now - k->press_tick;

        // Has initial delay passed?
        if (held_time >= REPEAT_DELAY_TICKS) {
            uint64_t since_last = now - k->last_repeat_tick;

            // Time for a repeat?
            if (since_last >= REPEAT_RATE_TICKS) {
                int c = scancode_to_char(k->scancode, current_modifiers);
                if (c != 0) {
                    key_buffer_put(c);
                }
                k->last_repeat_tick = now;
            }
        }
    }
}

// Poll USB keyboard and process any reports
static void poll_usb_keyboard(void) {
    uint8_t report[8];
    int ret = hal_usb_keyboard_poll(report, 8);

    if (ret > 0) {
        // Got a report, process it
        process_hid_report(report);
    }

    // Check for key repeats
    check_key_repeats();
}

/*
 * HAL Interface Implementation
 */

int hal_keyboard_init(void) {
    // USB init is done separately in kernel main
    return 0;
}

int hal_keyboard_getc(void) {
    // Poll USB keyboard for new data
    poll_usb_keyboard();

    // Return from buffer if available
    if (key_buf_read == key_buf_write) {
        return -1;
    }

    int c = key_buffer[key_buf_read];
    key_buf_read = (key_buf_read + 1) % KEY_BUF_SIZE;
    return c;
}

uint32_t hal_keyboard_get_irq(void) {
    // USB keyboard uses polling, no dedicated IRQ
    return 0;
}

void hal_keyboard_irq_handler(void) {
    // USB keyboard is polled, not interrupt-driven (yet)
    poll_usb_keyboard();
}
