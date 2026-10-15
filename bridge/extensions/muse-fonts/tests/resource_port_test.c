/* SPDX-License-Identifier: Apache-2.0
 * Compile the actual device adapter against read-only partition fakes and
 * system SHA-256; no Flash write, network, or NVS API exists in this harness.
 */
#include "actell_caption_font.h"
#include "esp_partition.h"
#include "psa/crypto.h"
#include "lvgl.h"
#include <CommonCrypto/CommonDigest.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *image;
static size_t image_size;
static unsigned unmaps;
const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label)
{
    assert(type == 1 && subtype == 0x40 && !strcmp(label, "actell_assets"));
    static const esp_partition_t p = {.size = 0x7d0000};
    return image ? &p : NULL;
}
int esp_partition_mmap(const esp_partition_t *p, size_t offset, size_t size, int mode, const void **result, esp_partition_mmap_handle_t *handle)
{
    assert(!offset && size == p->size && !mode);
    *result = image; *handle = 1; return ESP_OK;
}
void esp_partition_munmap(esp_partition_mmap_handle_t handle) { assert(handle == 1); unmaps++; }
int psa_crypto_init(void) { return PSA_SUCCESS; }
int psa_hash_compute(int algorithm, const void *input, size_t size, uint8_t *output, size_t cap, size_t *written)
{
    assert(algorithm == PSA_ALG_SHA_256 && cap >= 32 && size <= 0x7d0000);
    CC_SHA256(input, (CC_LONG)size, output); *written = 32; return PSA_SUCCESS;
}
uint32_t lv_draw_buf_width_to_stride(uint32_t w, lv_color_format_t cf) { assert(cf == LV_COLOR_FORMAT_A8); return (w + 3) & ~3u; }
void lv_draw_buf_flush_cache(const lv_draw_buf_t *buf, const lv_area_t *area) { (void)buf; (void)area; }

int main(int argc, char **argv)
{
    assert(argc == 3);
    bool expected = atoi(argv[2]) != 0;
    if (strcmp(argv[1], "-")) {
        FILE *f = fopen(argv[1], "rb"); assert(f);
        fseek(f, 0, SEEK_END); image_size = (size_t)ftell(f); rewind(f);
        assert(image_size <= 0x7d0000);
        image = malloc(0x7d0000); assert(image); memset(image, 0xff, 0x7d0000);
        assert(fread(image, 1, image_size, f) == image_size); fclose(f);
    }
    assert(actell_caption_init() == expected);
    assert(actell_caption_active() == expected);
    assert(actell_caption_init() == expected); /* no retry/replay or duplicate mmap */
    if (expected) {
        const lv_font_t *font = actell_caption_font(); assert(font && font->line_height == 46);
        lv_font_glyph_dsc_t glyph = {.resolved_font = font};
        assert(font->get_glyph_dsc(font, &glyph, 0x4e2d, 0));
        uint8_t pixels[4096];
        lv_draw_buf_t buf = {.data = pixels, .data_size = sizeof(pixels)};
        assert(font->get_glyph_bitmap(&glyph, &buf) == &buf);
        buf.data_size = 0; assert(!font->get_glyph_bitmap(&glyph, &buf));
        assert(!actell_caption_supported(0x20000));
        char page[400]; actell_caption_set_page(240, 3);
        assert(actell_caption_page("中文，混排 English", 0, page, sizeof(page)));
        assert(!unmaps);
    } else {
        assert(!actell_caption_font());
        assert(unmaps == (image ? 1u : 0u));
    }
    free(image);
    puts(expected ? "PASS mapped reader and LVGL adapter" : "PASS safe original-font fallback");
}
