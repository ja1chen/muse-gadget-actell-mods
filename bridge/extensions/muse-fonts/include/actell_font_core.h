/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *head, *cmap, *loca, *glyf;
    size_t cmap_size, loca_size, glyf_size;
    uint32_t count, maps;
    uint16_t line_height, baseline;
    uint8_t bpp, xy_bits, wh_bits, advance_bits, loc_width, compression;
} actell_font_data_t;

typedef struct {
    uint32_t id;
    uint16_t advance, width, height;
    int16_t x, y;
    const uint8_t *bits;
    size_t bytes, bit_offset;
} actell_glyph_t;

/* No allocation; input must remain mapped for the lifetime of the font. */
bool actell_font_open(actell_font_data_t *font, const void *data, size_t length);
bool actell_font_glyph(const actell_font_data_t *font, uint32_t codepoint, actell_glyph_t *glyph);
bool actell_font_glyph_id(const actell_font_data_t *font, uint32_t id, actell_glyph_t *glyph);
bool actell_font_decode(const actell_font_data_t *font, const actell_glyph_t *glyph,
                        uint8_t *output, size_t capacity, size_t stride);
uint32_t actell_utf8(const char *text, size_t *bytes);
size_t actell_font_normalize(const actell_font_data_t *font, const char *text,
                            char *out, size_t capacity);
bool actell_font_page(const actell_font_data_t *font, const char *text, size_t at,
                      char *out, size_t capacity, int width, int lines);
