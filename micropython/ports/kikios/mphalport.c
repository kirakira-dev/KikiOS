// MicroPython HAL implementation for KikiOS

#include "py/mpconfig.h"
#include "py/runtime.h"
#include "mphalport.h"

// Forward declaration - kapi is set by main.c
typedef struct kapi kapi_t;
extern kapi_t *mp_kikios_api;

// Character that triggers keyboard interrupt (Ctrl+C = 3)
static int interrupt_char = -1;

void mp_hal_set_interrupt_char(int c) {
    interrupt_char = c;
}

// KikiOS special key codes (must match kernel/keyboard.c)
#define SPECIAL_KEY_UP     0x100
#define SPECIAL_KEY_DOWN   0x101
#define SPECIAL_KEY_LEFT   0x102
#define SPECIAL_KEY_RIGHT  0x103
#define SPECIAL_KEY_HOME   0x104
#define SPECIAL_KEY_END    0x105
#define SPECIAL_KEY_DELETE 0x106

// Escape sequence buffer for converting special keys to VT100
static char escape_buf[8];
static int escape_idx = 0;
static int escape_len = 0;

// Helper to check if there's a key (use stdio hook if available)
static int mp_has_key(void) {
    if (mp_kikios_api->stdio_has_key) {
        return mp_kikios_api->stdio_has_key();
    }
    return mp_kikios_api->has_key();
}

// Helper to get a character (use stdio hook if available)
static int mp_getc(void) {
    if (mp_kikios_api->stdio_getc) {
        return mp_kikios_api->stdio_getc();
    }
    return mp_kikios_api->getc();
}

// Read one character from keyboard (blocking)
int mp_hal_stdin_rx_chr(void) {
    // If we have buffered escape sequence chars, return them
    if (escape_idx < escape_len) {
        return escape_buf[escape_idx++];
    }
    escape_idx = 0;
    escape_len = 0;

    // Wait for key (use stdio hook if available)
    while (!mp_has_key()) {
        mp_kikios_api->yield();
    }
    int c = mp_getc();

    // KikiOS keyboard sends '\n' for Enter, but MicroPython readline expects '\r'
    if (c == '\n') {
        c = '\r';
    }

    // Convert KikiOS special keys to VT100 escape sequences
    // Format: ESC [ <letter>
    if (c >= 0x100) {
        escape_buf[0] = 0x1b;  // ESC
        escape_buf[1] = '[';
        switch (c) {
            case SPECIAL_KEY_UP:
                escape_buf[2] = 'A';
                escape_len = 3;
                break;
            case SPECIAL_KEY_DOWN:
                escape_buf[2] = 'B';
                escape_len = 3;
                break;
            case SPECIAL_KEY_RIGHT:
                escape_buf[2] = 'C';
                escape_len = 3;
                break;
            case SPECIAL_KEY_LEFT:
                escape_buf[2] = 'D';
                escape_len = 3;
                break;
            case SPECIAL_KEY_HOME:
                escape_buf[2] = 'H';
                escape_len = 3;
                break;
            case SPECIAL_KEY_END:
                escape_buf[2] = 'F';
                escape_len = 3;
                break;
            case SPECIAL_KEY_DELETE:
                escape_buf[2] = '3';
                escape_buf[3] = '~';
                escape_len = 4;
                break;
            default:
                // Unknown special key, ignore
                return mp_hal_stdin_rx_chr();
        }
        escape_idx = 1;  // Return first char (ESC), buffer the rest
        return escape_buf[0];
    }

    // Check for interrupt character
    if (c == interrupt_char) {
        mp_sched_keyboard_interrupt();
    }

    return c;
}

// Helper to output a character (use stdio hook if available)
static void mp_putc(char c) {
    if (mp_kikios_api->stdio_putc) {
        mp_kikios_api->stdio_putc(c);
    } else {
        mp_kikios_api->putc(c);
    }
}

// Write string to console
mp_uint_t mp_hal_stdout_tx_strn(const char *str, mp_uint_t len) {
    for (mp_uint_t i = 0; i < len; i++) {
        mp_putc(str[i]);
    }
    return len;
}

// Note: mp_hal_stdout_tx_strn_cooked is provided by shared/runtime/stdout_helpers.c
// It uses mp_hal_stdout_tx_strn which we define above with stdio hook support

// Milliseconds since boot
mp_uint_t mp_hal_ticks_ms(void) {
    // KikiOS ticks at 100Hz, so multiply by 10 to get ms
    return mp_kikios_api->get_uptime_ticks() * 10;
}

// Microseconds since boot
mp_uint_t mp_hal_ticks_us(void) {
    // KikiOS only has 10ms resolution, approximate
    return mp_kikios_api->get_uptime_ticks() * 10000;
}

// CPU ticks (not really supported, use ms)
mp_uint_t mp_hal_ticks_cpu(void) {
    return mp_hal_ticks_ms();
}

// Sleep for milliseconds
void mp_hal_delay_ms(mp_uint_t ms) {
    mp_kikios_api->sleep_ms(ms);
}

// Sleep for microseconds (rounds up to ms)
void mp_hal_delay_us(mp_uint_t us) {
    mp_kikios_api->sleep_ms((us + 999) / 1000);
}
