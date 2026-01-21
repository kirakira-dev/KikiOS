/*
 * KikiOS TrueType Font Renderer
 *
 * Uses stb_truetype to render TTF fonts loaded from disk.
 */

#include "ttf.h"
#include "vfs.h"
#include "memory.h"
#include "string.h"
#include "printf.h"

// Configure stb_truetype for our environment
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION

// Use our memory functions
#define STBTT_malloc(x,u)  ((void)(u), malloc(x))
#define STBTT_free(x,u)    ((void)(u), free(x))

// Use our string functions
#define STBTT_memcpy  memcpy
#define STBTT_memset  memset
#define STBTT_strlen  strlen

// Disable assert (or make it a no-op)
#define STBTT_assert(x)  ((void)0)

#include "../vendor/stb_truetype.h"

// Font file path
#define FONT_PATH "/fonts/Roboto/Roboto-Regular.ttf"

// Maximum cached glyphs per size/style combo
#define MAX_CACHED_GLYPHS 128

// Cache entry
typedef struct {
    int codepoint;
    int style;
    ttf_glyph_t glyph;
} glyph_cache_entry_t;

// Per-size cache
typedef struct {
    int size;
    float scale;
    glyph_cache_entry_t entries[MAX_CACHED_GLYPHS];
    int count;
} size_cache_t;

// Global state
static uint8_t *font_data = NULL;
static int font_data_size = 0;
static stbtt_fontinfo font_info;
static int ttf_ready = 0;

// Caches for different sizes (must cover all sizes used by browser)
#define NUM_SIZE_CACHES 8
static size_cache_t size_caches[NUM_SIZE_CACHES];
static int size_cache_sizes[NUM_SIZE_CACHES] = {
    FONT_SIZE_SMALL,   // 12
    14,                // browser small
    FONT_SIZE_NORMAL,  // 16
    18,                // browser h4
    20,                // browser h3
    FONT_SIZE_LARGE,   // 24
    28,                // browser h1
    FONT_SIZE_XLARGE   // 32
};

// Temporary buffer for italic shearing
static uint8_t *temp_bitmap = NULL;
static int temp_bitmap_size = 0;

static size_cache_t *get_size_cache(int size) {
    // First try exact match
    for (int i = 0; i < NUM_SIZE_CACHES; i++) {
        if (size_caches[i].size == size) {
            return &size_caches[i];
        }
    }
    // Find closest size
    int best = 0;
    int best_diff = 1000;
    for (int i = 0; i < NUM_SIZE_CACHES; i++) {
        int diff = size_caches[i].size - size;
        if (diff < 0) diff = -diff;
        if (diff < best_diff) {
            best_diff = diff;
            best = i;
        }
    }
    return &size_caches[best];
}

int ttf_init(void) {
    if (ttf_ready) return 0;

    // Open font file
    vfs_node_t *font_file = vfs_lookup(FONT_PATH);
    if (!font_file) {
        printf("TTF: Failed to open %s\n", FONT_PATH);
        return -1;
    }

    // Get file size
    font_data_size = font_file->size;
    if (font_data_size <= 0) {
        printf("TTF: Invalid font file size\n");
        return -1;
    }

    // Allocate buffer
    font_data = malloc(font_data_size);
    if (!font_data) {
        printf("TTF: Failed to allocate %d bytes for font\n", font_data_size);
        return -1;
    }

    // Read font data
    int bytes_read = vfs_read(font_file, (char *)font_data, font_data_size, 0);
    if (bytes_read != font_data_size) {
        printf("TTF: Failed to read font file (got %d, expected %d)\n", bytes_read, font_data_size);
        free(font_data);
        font_data = NULL;
        return -1;
    }

    // Initialize stb_truetype
    int offset = stbtt_GetFontOffsetForIndex(font_data, 0);
    if (!stbtt_InitFont(&font_info, font_data, offset)) {
        printf("TTF: Failed to initialize font\n");
        free(font_data);
        font_data = NULL;
        return -1;
    }

    // Initialize size caches
    for (int i = 0; i < NUM_SIZE_CACHES; i++) {
        size_caches[i].size = size_cache_sizes[i];
        size_caches[i].scale = stbtt_ScaleForPixelHeight(&font_info, (float)size_cache_sizes[i]);
        size_caches[i].count = 0;
    }

    // Allocate temp bitmap for transformations (max glyph size)
    temp_bitmap_size = FONT_SIZE_XLARGE * FONT_SIZE_XLARGE * 2;
    temp_bitmap = malloc(temp_bitmap_size);

    ttf_ready = 1;
    printf("TTF: Loaded %s (%d bytes)\n", FONT_PATH, font_data_size);
    return 0;
}

int ttf_is_ready(void) {
    return ttf_ready;
}

// Apply faux bold (draw shifted copy)
// stride = row stride in bytes, content_w = actual content width
static void apply_bold(uint8_t *bitmap, int stride, int content_w, int h) {
    // Add pixels shifted right by 1
    for (int y = 0; y < h; y++) {
        // Work right to left so we don't overwrite source pixels
        for (int x = content_w; x > 0; x--) {
            int idx = y * stride + x;
            int src_idx = y * stride + x - 1;
            int val = bitmap[idx] + bitmap[src_idx];
            bitmap[idx] = (val > 255) ? 255 : val;
        }
    }
}

// Apply faux italic (shear transform)
// stride = row stride in bytes, content_w = actual content width to shear
static void apply_italic(uint8_t *bitmap, int stride, int content_w, int h, int *new_w) {
    if (!temp_bitmap) return;

    // Shear amount: ~0.2 (about 12 degrees)
    float shear = 0.2f;
    int max_shift = (int)((h - 1) * shear) + 1;
    int out_w = content_w + max_shift;

    // Make sure output fits in allocated space
    if (out_w > stride) out_w = stride;
    if (stride * h > temp_bitmap_size) return;

    // Clear temp
    memset(temp_bitmap, 0, stride * h);

    // Shear: each row shifts right based on distance from bottom
    for (int y = 0; y < h; y++) {
        int shift = (int)((h - 1 - y) * shear);
        for (int x = 0; x < content_w; x++) {
            int dst_x = x + shift;
            if (dst_x < stride) {
                temp_bitmap[y * stride + dst_x] = bitmap[y * stride + x];
            }
        }
    }

    // Copy back (same size as input)
    memcpy(bitmap, temp_bitmap, stride * h);
    *new_w = out_w;
}

ttf_glyph_t *ttf_get_glyph(int codepoint, int size, int style) {
    if (!ttf_ready) return NULL;

    // Find appropriate size cache
    size_cache_t *cache = get_size_cache(size);

    // Check if already cached
    for (int i = 0; i < cache->count; i++) {
        if (cache->entries[i].codepoint == codepoint &&
            cache->entries[i].style == style) {
            return &cache->entries[i].glyph;
        }
    }

    // Need to render
    if (cache->count >= MAX_CACHED_GLYPHS) {
        // Cache full, evict all entries and free their bitmaps
        for (int i = 0; i < MAX_CACHED_GLYPHS; i++) {
            if (cache->entries[i].glyph.bitmap) {
                free(cache->entries[i].glyph.bitmap);
                cache->entries[i].glyph.bitmap = NULL;
            }
        }
        cache->count = 0;
    }

    glyph_cache_entry_t *entry = &cache->entries[cache->count];
    entry->codepoint = codepoint;
    entry->style = style;

    // Render glyph
    int w, h, xoff, yoff;
    uint8_t *bmp = stbtt_GetCodepointBitmap(
        &font_info, 0, cache->scale,
        codepoint, &w, &h, &xoff, &yoff
    );

    if (!bmp) {
        // No glyph for this codepoint
        entry->glyph.bitmap = NULL;
        entry->glyph.width = 0;
        entry->glyph.height = 0;
        entry->glyph.xoff = 0;
        entry->glyph.yoff = 0;
        entry->glyph.advance = size / 2;  // Default advance
        cache->count++;
        return &entry->glyph;
    }

    // Get advance width
    int advance, lsb;
    stbtt_GetCodepointHMetrics(&font_info, codepoint, &advance, &lsb);

    // For normal style, just copy the bitmap as-is
    if (style == FONT_STYLE_NORMAL) {
        entry->glyph.bitmap = malloc(w * h);
        if (!entry->glyph.bitmap) {
            stbtt_FreeBitmap(bmp, NULL);
            return NULL;
        }
        memcpy(entry->glyph.bitmap, bmp, w * h);
        stbtt_FreeBitmap(bmp, NULL);

        entry->glyph.width = w;
        entry->glyph.height = h;
        entry->glyph.xoff = xoff;
        entry->glyph.yoff = yoff;
        entry->glyph.advance = (int)(advance * cache->scale);

        cache->count++;
        return &entry->glyph;
    }

    // For styled text, allocate extra space
    int extra_w = 0;
    if (style & FONT_STYLE_ITALIC) {
        extra_w = (int)(h * 0.2f) + 2;
    }
    if (style & FONT_STYLE_BOLD) {
        extra_w += 1;
    }

    int alloc_w = w + extra_w;
    entry->glyph.bitmap = malloc(alloc_w * h);
    if (!entry->glyph.bitmap) {
        stbtt_FreeBitmap(bmp, NULL);
        return NULL;
    }

    // Copy with new stride
    memset(entry->glyph.bitmap, 0, alloc_w * h);
    for (int row = 0; row < h; row++) {
        memcpy(entry->glyph.bitmap + row * alloc_w, bmp + row * w, w);
    }
    stbtt_FreeBitmap(bmp, NULL);

    entry->glyph.width = alloc_w;  // Use allocated width as the stride
    entry->glyph.height = h;
    entry->glyph.xoff = xoff;
    entry->glyph.yoff = yoff;
    entry->glyph.advance = (int)(advance * cache->scale);

    // Apply styling
    int content_w = w;  // Track actual content width as we apply styles

    if (style & FONT_STYLE_BOLD) {
        apply_bold(entry->glyph.bitmap, alloc_w, content_w, h);
        content_w += 1;  // Bold adds 1 pixel width
        entry->glyph.advance += 1;
    }

    if (style & FONT_STYLE_ITALIC) {
        int new_w = alloc_w;
        apply_italic(entry->glyph.bitmap, alloc_w, content_w, h, &new_w);
        entry->glyph.width = new_w;
    }

    cache->count++;
    return &entry->glyph;
}

void ttf_get_metrics(int size, int *ascent, int *descent, int *line_gap) {
    if (!ttf_ready) {
        *ascent = size;
        *descent = 0;
        *line_gap = 0;
        return;
    }

    size_cache_t *cache = get_size_cache(size);

    int a, d, lg;
    stbtt_GetFontVMetrics(&font_info, &a, &d, &lg);

    *ascent = (int)(a * cache->scale);
    *descent = (int)(d * cache->scale);  // Note: descent is typically negative
    *line_gap = (int)(lg * cache->scale);
}

int ttf_get_advance(int codepoint, int size) {
    if (!ttf_ready) return size / 2;

    size_cache_t *cache = get_size_cache(size);

    int advance, lsb;
    stbtt_GetCodepointHMetrics(&font_info, codepoint, &advance, &lsb);
    return (int)(advance * cache->scale);
}

int ttf_get_kerning(int cp1, int cp2, int size) {
    if (!ttf_ready) return 0;

    size_cache_t *cache = get_size_cache(size);
    int kern = stbtt_GetCodepointKernAdvance(&font_info, cp1, cp2);
    return (int)(kern * cache->scale);
}
