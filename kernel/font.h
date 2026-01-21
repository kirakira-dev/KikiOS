/*
 * KikiOS Bitmap Font
 *
 * 8x16 monospace font for console display
 */

#ifndef FONT_H
#define FONT_H

#include <stdint.h>

#define FONT_WIDTH  8
#define FONT_HEIGHT 16

// Font data - 256 characters, 16 bytes each (one byte per row)
extern const uint8_t font_data[256][16];

#endif
