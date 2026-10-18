/* SPDX-License-Identifier: Apache-2.0
 * Bounded reader for the lv_font_conv 1.5.3 no-kerning binary profile.
 * All table and glyph data remains in the caller's immutable mapped buffer.
 */
#include "actell_font_core.h"
#include <limits.h>
#include <string.h>

static uint16_t u16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }
static uint32_t u32(const uint8_t *p) { return u16(p) | (uint32_t)u16(p + 2) << 16; }
static bool inside(size_t offset, size_t size, size_t length) { return offset <= length && size <= length - offset; }

typedef struct { const uint8_t *p; size_t size, pos; bool ok; } bits_t;
static unsigned bits(bits_t *b, unsigned n)
{
    if (n > 16 || b->pos > b->size * 8 || n > b->size * 8 - b->pos) { b->ok = false; return 0; }
    unsigned v = 0;
    while (n--) { v = (v << 1) | ((b->p[b->pos / 8] >> (7 - b->pos % 8)) & 1); b->pos++; }
    return v;
}
static int signed_bits(bits_t *b, unsigned n)
{
    unsigned v = bits(b, n);
    return n && (v & (1u << (n - 1))) ? (int)v - (int)(1u << n) : (int)v;
}
static uint32_t location(const actell_font_data_t *f, uint32_t i)
{
    const uint8_t *p = f->loca + 12 + (size_t)i * f->loc_width;
    return f->loc_width == 4 ? u32(p) : u16(p);
}

bool actell_font_glyph_id(const actell_font_data_t *f, uint32_t id, actell_glyph_t *g)
{
    if (!f || !g || !id || id >= f->count) return false;
    size_t start = location(f, id), end = id + 1 < f->count ? location(f, id + 1) : f->glyf_size;
    if (start < 8 || start > end || end > f->glyf_size) return false;
    bits_t b = { f->glyf + start, end - start, 0, true };
    memset(g, 0, sizeof(*g));
    g->id = id;
    g->advance = f->advance_bits ? bits(&b, f->advance_bits) : u16(f->head + 30);
    g->x = signed_bits(&b, f->xy_bits); g->y = signed_bits(&b, f->xy_bits);
    g->width = bits(&b, f->wh_bits); g->height = bits(&b, f->wh_bits);
    g->bits = b.p; g->bytes = b.size; g->bit_offset = b.pos;
    return b.ok && g->advance <= 128 && g->width <= 64 && g->height <= 64;
}

bool actell_font_open(actell_font_data_t *out, const void *data, size_t length)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!data || length > 16u * 1024 * 1024) return false;
    actell_font_data_t f = {0};
    const uint8_t *p = data;
    const char *names[] = {"head", "cmap", "loca", "glyf"};
    size_t offset = 0, sizes[4];
    const uint8_t *tables[4];
    for (unsigned i = 0; i < 4; i++) {
        if (!inside(offset, 8, length)) return false;
        size_t size = u32(p + offset);
        if (size < 8 || size % 4 || !inside(offset, size, length) || memcmp(p + offset + 4, names[i], 4)) return false;
        tables[i] = p + offset; sizes[i] = size; offset += size;
    }
    if (offset != length || sizes[0] != 48 || sizes[1] < 12 || sizes[2] < 12) return false;
    f.head = tables[0]; f.cmap = tables[1]; f.loca = tables[2]; f.glyf = tables[3];
    f.cmap_size = sizes[1]; f.loca_size = sizes[2]; f.glyf_size = sizes[3];
    if (u32(f.head + 8) != 1 || u16(f.head + 12) != 3 || f.head[34] > 1 || f.head[35] > 1 ||
        f.head[36] || (f.head[37] != 2 && f.head[37] != 4) || f.head[41] > 2 || f.head[42]) return false;
    f.bpp = f.head[37]; f.xy_bits = f.head[38]; f.wh_bits = f.head[39]; f.advance_bits = f.head[40];
    f.compression = f.head[41]; f.loc_width = f.head[34] ? 4 : 2;
    int ascent = u16(f.head + 16), descent = (int16_t)u16(f.head + 18);
    if (ascent <= 0 || descent > 0 || ascent - descent > 64) return false;
    f.line_height = ascent - descent; f.baseline = -descent;
    if (!f.xy_bits || f.xy_bits > 8 || !f.wh_bits || f.wh_bits > 7 || f.advance_bits > 8) return false;
    f.count = u32(f.loca + 8); f.maps = u32(f.cmap + 8);
    if (f.count < 2 || f.count > 65536 || !inside(12, (size_t)f.count * f.loc_width, f.loca_size) ||
        !f.maps || f.maps > 256 || !inside(12, (size_t)f.maps * 16, f.cmap_size)) return false;
    uint32_t previous = 8, range_end = 0;
    for (uint32_t i = 0; i < f.count; i++) {
        uint32_t loc = location(&f, i);
        if (loc < previous || loc >= f.glyf_size) return false;
        previous = loc;
        actell_glyph_t g;
        if (i && !actell_font_glyph_id(&f, i, &g)) return false;
    }
    for (uint32_t i = 0; i < f.maps; i++) {
        const uint8_t *m = f.cmap + 12 + i * 16;
        uint32_t start = u32(m + 4), count = u16(m + 8), entries = u16(m + 12), type = m[14];
        if (!count || start < range_end || start > 0x10ffff || count > 0x110000 - start || type > 3) return false;
        range_end = start + count;
        size_t bytes = type == 0 ? entries : type == 1 ? (size_t)entries * 4 : type == 3 ? (size_t)entries * 2 : 0;
        if (type != 2 && (!entries || !inside(u32(m), bytes, f.cmap_size) || u32(m) < 12 + f.maps * 16)) return false;
        if (type == 0 && entries != count) return false;
        uint32_t last = 0;
        for (uint32_t j = 0; j < (type == 1 || type == 3 ? entries : 0); j++) {
            uint32_t cp = u16(f.cmap + u32(m) + j * 2);
            if (cp >= count || (j && cp <= last)) return false;
            last = cp;
        }
    }
    *out = f;
    return true;
}

bool actell_font_glyph(const actell_font_data_t *f, uint32_t cp, actell_glyph_t *g)
{
    if (!f || !f->head) return false;
    uint32_t lo = 0, hi = f->maps;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        const uint8_t *m = f->cmap + 12 + mid * 16;
        uint32_t start = u32(m + 4), length = u16(m + 8), id = u16(m + 10), type = m[14];
        if (cp < start) { hi = mid; continue; }
        if (cp - start >= length) { lo = mid + 1; continue; }
        uint32_t relative = cp - start, n = u16(m + 12);
        const uint8_t *d = f->cmap + u32(m);
        if (type == 2) id += relative;
        else if (type == 0) id += d[relative];
        else {
            uint32_t a = 0, b = n;
            while (a < b) { uint32_t k = a + (b - a) / 2; if (u16(d + k * 2) < relative) a = k + 1; else b = k; }
            if (a == n || u16(d + a * 2) != relative) return false;
            id += type == 3 ? a : u16(d + n * 2 + a * 2);
        }
        return actell_font_glyph_id(f, id, g);
    }
    return false;
}

/* RLE state machine adapted from LVGL 9.5.0 fmt_txt (MIT),
 * Copyright (c) 2025 LVGL Kft. See licenses/LVGL-MIT.txt.
 * Independently bounded per glyph.
 * No static decompressor state, heap allocation, or reads beyond this glyph. */
bool actell_font_decode(const actell_font_data_t *f, const actell_glyph_t *g,
                        uint8_t *output, size_t capacity, size_t stride)
{
    if (!f || !g || g->width > 64 || g->height > 64 || stride < g->width ||
        (g->height && (stride > capacity / g->height)) || (!output && g->width && g->height)) return false;
    bits_t b = {g->bits, g->bytes, g->bit_offset, true};
    unsigned state = 0, previous = 0, count = 0;
    bool first = true;
    uint8_t row[64] = {0};
    for (unsigned y = 0; y < g->height; y++) for (unsigned x = 0; x < g->width; x++) {
        unsigned v;
        if (!f->compression) v = bits(&b, f->bpp);
        else if (state == 0) {
            v = bits(&b, f->bpp);
            if (!first && v == previous) { state = 1; count = 0; }
            first = false; previous = v;
        } else if (state == 1) {
            unsigned repeated = bits(&b, 1);
            count++; v = previous;
            if (repeated && count == 11) {
                count = bits(&b, 6);
                if (count) state = 2;
                else { v = bits(&b, f->bpp); previous = v; state = 0; }
            } else if (!repeated) { v = bits(&b, f->bpp); previous = v; state = 0; }
        } else {
            v = previous;
            if (--count == 0) { v = bits(&b, f->bpp); previous = v; state = 0; }
        }
        if (!b.ok) return false;
        if (f->compression == 1) { row[x] ^= v; v = row[x]; }
        output[y * stride + x] = (uint8_t)(v * 255 / ((1u << f->bpp) - 1));
    }
    return true;
}
