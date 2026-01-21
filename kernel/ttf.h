/*
 * KikiOS TrueType Font Renderer
 *
 * Loads TTF from disk, renders glyphs at various sizes.
 * Uses stb_truetype under the hood.
 */

#ifndef TTF_H
#define TTF_H

#include <stdint.h>

// Font styles (can be OR'd together)
#define FONT_STYLE_NORMAL  0
#define FONT_STYLE_BOLD    1
#define FONT_STYLE_ITALIC  2

// Pre-defined font sizes
#define FONT_SIZE_SMALL   12
#define FONT_SIZE_NORMAL  16
#define FONT_SIZE_LARGE   24
#define FONT_SIZE_XLARGE  32

// Rendered glyph info
typedef struct {
    uint8_t *bitmap;     // Grayscale bitmap (caller must not free)
    int width;           // Bitmap width in pixels
    int height;          // Bitmap height in pixels
    int xoff;            // X offset from cursor to top-left of bitmap
    int yoff;            // Y offset from cursor to top-left of bitmap
    int advance;         // How much to advance cursor after this glyph
} ttf_glyph_t;

// Initialize TTF system - call after FAT32 is ready
// Returns 0 on success, -1 on failure
int ttf_init(void);

// Check if TTF system is initialized
int ttf_is_ready(void);

// Get a rendered glyph
// Returns pointer to glyph info, or NULL on failure
// The bitmap data is cached internally - do not free it
ttf_glyph_t *ttf_get_glyph(int codepoint, int size, int style);

// Get font metrics for a given size
void ttf_get_metrics(int size, int *ascent, int *descent, int *line_gap);

// Get advance width for a character (for layout calculations)
int ttf_get_advance(int codepoint, int size);

// Get kerning between two characters
int ttf_get_kerning(int cp1, int cp2, int size);

#endif
