/*
 * KikiOS Mouse Cursor
 *
 * Handles drawing and updating the mouse cursor
 */

#ifndef CURSOR_H
#define CURSOR_H

#include <stdint.h>

// Initialize cursor system
void cursor_init(void);

// Show/hide cursor
void cursor_show(void);
void cursor_hide(void);

// Update cursor position and redraw
void cursor_update(void);

// Draw cursor at specific position (for direct control)
void cursor_draw(int x, int y);

// Get cursor position
void cursor_get_pos(int *x, int *y);

#endif
