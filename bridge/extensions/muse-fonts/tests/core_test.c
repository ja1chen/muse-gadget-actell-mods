/* SPDX-License-Identifier: Apache-2.0 */
#include "actell_font_core.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    assert(argc == 2);
    FILE *file = fopen(argv[1], "rb"); assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    long size = ftell(file); assert(size > 0);
    rewind(file);
    unsigned char *bytes = malloc((size_t)size); assert(bytes);
    assert(fread(bytes, 1, (size_t)size, file) == (size_t)size); fclose(file);
    actell_font_data_t font;
    assert(actell_font_open(&font, bytes, (size_t)size));
    unsigned char output[4096];
    for (uint32_t id = 1; id < font.count; id++) {
        actell_glyph_t glyph;
        assert(actell_font_glyph_id(&font, id, &glyph));
        assert(actell_font_decode(&font, &glyph, output, sizeof(output), glyph.width));
        if (glyph.width && glyph.height) {
            assert(!actell_font_decode(&font, &glyph, output, 0, glyph.width));
            actell_glyph_t truncated = glyph; truncated.bytes = 0;
            assert(!actell_font_decode(&font, &truncated, output, sizeof(output), glyph.width));
        }
    }
    char page[400];
    const char *texts[] = {"", "\n\n", "中文，标点。English WWW iii 123", "a\xf0\x9f", "\xed\xa0\x80", "\xff\xc0\x80", "https://example.com/abcdefghijklmnop?x=1", "「中文」测试……"};
    for (unsigned i = 0; i < sizeof(texts)/sizeof(texts[0]); i++) {
        for (size_t cap = 0; cap <= sizeof(page); cap++) {
            for (size_t at = 0; at < strlen(texts[i]) + 2; at++)
                actell_font_page(&font, texts[i], at, page, cap, 120, 2);
        }
    }
    /* Malformed table/header input must be safely rejected, even without hashes. */
    for (size_t cut = 0; cut < 128; cut++) assert(!actell_font_open(&font, bytes, cut));
    for (unsigned i = 0; i < 128; i++) {
        size_t at = i % 80; unsigned char old = bytes[at]; bytes[at] ^= (unsigned char)(i + 1);
        if (actell_font_open(&font, bytes, (size_t)size)) {
            actell_glyph_t glyph;
            if (actell_font_glyph(&font, '?', &glyph)) actell_font_decode(&font, &glyph, output, sizeof(output), glyph.width);
        }
        bytes[at] = old;
    }
    free(bytes);
    puts("PASS bounded metadata, all-glyph decoding, malformed input and UTF-8/page capacities (ASan/UBSan)");
}
