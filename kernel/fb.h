/*
 * KikiOS Framebuffer Driver
 */

#ifndef FB_H
#define FB_H

#include <stdint.h>

// Framebuffer info
extern uint32_t fb_width;
extern uint32_t fb_height;
extern uint32_t fb_pitch;  // Bytes per row
extern uint32_t *fb_base;  // Pointer to pixel memory

// Initialize framebuffer
int fb_init(void);

// Basic drawing
void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color);
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_clear(uint32_t color);

// Colors (32-bit XRGB)
#define COLOR_BLACK   0x00000000
#define COLOR_WHITE   0x00FFFFFF
#define COLOR_GREEN   0x0000FF00
#define COLOR_AMBER   0x00FFBF00
#define COLOR_RED     0x00FF0000
#define COLOR_BLUE    0x000000FF
#define COLOR_CYAN    0x0000FFFF

// Text drawing
void fb_draw_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
void fb_draw_char_fg_only(uint32_t x, uint32_t y, char c, uint32_t fg);  // For batch rendering
void fb_draw_string(uint32_t x, uint32_t y, const char *s, uint32_t fg, uint32_t bg);

// Hardware double buffering (Pi only)
int fb_has_hw_double_buffer(void);   // Returns 1 if hardware double buffering available
int fb_flip(int buffer);             // Switch visible buffer (0 or 1)
uint32_t *fb_get_backbuffer(void);   // Get pointer to current backbuffer

#endif
