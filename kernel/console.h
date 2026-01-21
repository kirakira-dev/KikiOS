/*
 * KikiOS Text Console
 *
 * Terminal-like text output on framebuffer
 */

#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>

// Initialize console
void console_init(void);

// Output
void console_putc(char c);
void console_puts(const char *s);
void console_clear(void);
void console_clear_to_eol(void);  // Fast clear from cursor to end of line
void console_clear_region(int row, int col, int width, int height);  // Fast rect clear

// Cursor
void console_set_cursor(int row, int col);
void console_get_cursor(int *row, int *col);
void console_blink_cursor(void);
void console_set_cursor_enabled(int enabled);
void console_show_cursor(void);

// Colors
void console_set_color(uint32_t fg, uint32_t bg);

// Console dimensions (in characters)
int console_rows(void);
int console_cols(void);

#endif
