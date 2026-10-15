/* SPDX-License-Identifier: Apache-2.0 */
#include "actell_font_core.h"
#include <string.h>

uint32_t actell_utf8(const char *s, size_t *bytes)
{
    const uint8_t *p = (const uint8_t *)s;
    *bytes = *p ? 1 : 0;
    if (*p < 0x80) return *p;
    unsigned n = *p >= 0xf0 && *p <= 0xf4 ? 4 : *p >= 0xe0 && *p <= 0xef ? 3 : *p >= 0xc2 && *p <= 0xdf ? 2 : 0;
    if (!n) return 0xfffd;
    uint32_t cp = *p & (0x7f >> n);
    for (unsigned i = 1; i < n; i++) {
        if (!p[i] || (p[i] & 0xc0) != 0x80) return 0xfffd;
        cp = (cp << 6) | (p[i] & 0x3f);
    }
    if ((n == 3 && cp < 0x800) || (n == 4 && cp < 0x10000) || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return 0xfffd;
    *bytes = n;
    return cp;
}

static uint32_t shown(const actell_font_data_t *f, const char *s, size_t *bytes)
{
    uint32_t cp = actell_utf8(s, bytes);
    if (cp == '\n' || cp == 0) return cp;
    if (cp == '\t' || cp == '\r') return ' ';
    actell_glyph_t g;
    return actell_font_glyph(f, cp, &g) ? cp : '?';
}

size_t actell_font_normalize(const actell_font_data_t *f, const char *text, char *out, size_t cap)
{
    if (!cap) return 0;
    size_t count = 0;
    while (*text) {
        size_t n;
        uint32_t cp = shown(f, text, &n);
        size_t raw_n;
        uint32_t raw = actell_utf8(text, &raw_n);
        size_t put = cp == raw ? n : 1;
        if (put >= cap - count) break;
        if (cp == raw) memmove(out + count, text, put);
        else out[count] = (char)cp;
        count += put; text += n;
    }
    out[count] = 0;
    return count;
}

static bool cjk(uint32_t cp) { return (cp >= 0x2e80 && cp <= 0x9fff) || (cp >= 0xf900 && cp <= 0xfaff) || (cp >= 0xff00 && cp <= 0xffef); }
static bool closing(uint32_t cp)
{
    const uint32_t list[] = {')',']','}',',','.',':',';','!','?',0x2019,0x201d,0x3001,0x3002,0x3009,0x300b,0x300d,0x300f,0x3011,0x3015,0x3017,0x3019,0x301b,0xff01,0xff09,0xff0c,0xff0e,0xff1a,0xff1b,0xff1f,0xff3d,0xff5d,0xff60};
    for (size_t i = 0; i < sizeof(list)/sizeof(list[0]); i++) if (cp == list[i]) return true;
    return false;
}
static bool opening(uint32_t cp)
{
    const uint32_t list[] = {'(', '[', '{', 0x2018,0x201c,0x3008,0x300a,0x300c,0x300e,0x3010,0x3014,0x3016,0x3018,0x301a,0xff08,0xff3b,0xff5b};
    for (size_t i = 0; i < sizeof(list)/sizeof(list[0]); i++) if (cp == list[i]) return true;
    return false;
}

static bool line(const actell_font_data_t *f, const char **cursor, int width, const char **start, size_t *length)
{
    const char *p = *cursor;
    while (*p == ' ') p++;
    if (!*p) { *cursor = p; return false; }
    const char *end = p, *brk = NULL, *movable = NULL;
    int used = 0;
    uint32_t previous = 0;
    while (*end && *end != '\n') {
        size_t bytes;
        uint32_t cp = shown(f, end, &bytes);
        actell_glyph_t g;
        if (!bytes || !actell_font_glyph(f, cp, &g)) return false;
        bool boundary = end != p && !closing(cp) && !opening(previous) && (cjk(cp) || cjk(previous));
        if (used + g.advance > width && end != p) {
            if (boundary) brk = end;
            else if ((closing(cp) || opening(previous)) && !brk) brk = movable;
            break;
        }
        if (g.advance > width) return false;
        if (end != p && !closing(cp) && !opening(previous)) movable = end;
        if (cp == ' ' || boundary) brk = end;
        used += g.advance; end += bytes; previous = cp;
    }
    if (*end && *end != '\n' && *end != ' ' && brk && brk > p) end = brk;
    *start = p; *length = (size_t)(end - p);
    *cursor = *end == '\n' ? end + 1 : end;
    return end != p || *end == '\n';
}

bool actell_font_page(const actell_font_data_t *f, const char *text, size_t at,
                      char *out, size_t cap, int width, int lines)
{
    if (!out || !cap) return false;
    out[0] = 0;
    if (!f || !f->head || !text || width < 1 || lines < 1 || lines > 16) return false;
    size_t total = 0;
    while (text[total] && total <= 8192) total++;
    if (!total || total > 8192) return false;
    if (at >= total) at = total - 1;
    const char *p = text, *start;
    size_t length;
    int index = -1, n = 0;
    while (line(f, &p, width, &start, &length)) {
        index = n++;
        if ((size_t)(p - text) > at) break;
    }
    if (index < 0) return false;
    int first = lines > 1 ? index / (lines - 1) * (lines - 1) : index;
    size_t output = 0;
    bool emitted = false;
    p = text;
    for (n = 0; n < first + lines && line(f, &p, width, &start, &length); n++) {
        if (n < first) continue;
        if (length + (emitted ? 1 : 0) >= cap - output) break;
        if (emitted) out[output++] = '\n';
        /* Copy a whole raw line first, then normalize in place. Shrinks only. */
        memcpy(out + output, start, length); out[output + length] = 0;
        output += actell_font_normalize(f, out + output, out + output, cap - output);
        emitted = true;
    }
    out[output] = 0;
    return true;
}
