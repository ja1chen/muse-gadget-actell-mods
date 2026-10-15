/* SPDX-License-Identifier: Apache-2.0 */
#include "actell_caption_font.h"
#include "actell_font_core.h"
#include "lvgl.h"
#include "esp_partition.h"
#include "esp_log.h"
#include "psa/crypto.h"
#include "cJSON.h"
#include <math.h>
#include <stdatomic.h>
#include <string.h>

static actell_font_data_t data;
static lv_font_t font;
static atomic_bool active;
static atomic_uint page = ATOMIC_VAR_INIT((3u << 16) | 256u);
static bool tried;
static esp_partition_mmap_handle_t mapping;
static uint32_t u32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static bool integer(const cJSON *v, uint32_t *out)
{
    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble < 0 ||
        v->valuedouble > 16777216 || floor(v->valuedouble) != v->valuedouble) return false;
    *out = (uint32_t)v->valuedouble; return true;
}
static bool hash(const void *p, size_t n, const uint8_t *expected)
{
    uint8_t digest[32];
    size_t written;
    return psa_crypto_init() == PSA_SUCCESS &&
           psa_hash_compute(PSA_ALG_SHA_256, p, n, digest, sizeof(digest), &written) == PSA_SUCCESS &&
           written == sizeof(digest) && memcmp(digest, expected, 32) == 0;
}
static bool text_hash(const void *p, size_t n, const cJSON *item)
{
    if (!cJSON_IsString(item) || strlen(item->valuestring) != 64) return false;
    const char *hex = "0123456789abcdef";
    uint8_t expected[32];
    for (unsigned i = 0; i < 32; i++) {
        const char *a = strchr(hex, item->valuestring[2*i]), *b = strchr(hex, item->valuestring[2*i+1]);
        if (!a || !b) return false;
        expected[i] = (a - hex) * 16 + (b - hex);
    }
    return hash(p, n, expected);
}
static bool identifier(const cJSON *v)
{
    if (!cJSON_IsString(v) || !v->valuestring[0] || strlen(v->valuestring) > 96) return false;
    for (const char *p = v->valuestring; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || (p != v->valuestring && strchr("_.-", *p)))) return false;
    return true;
}
static bool descriptor(const lv_font_t *unused, lv_font_glyph_dsc_t *out, uint32_t cp, uint32_t next)
{
    (void)unused; (void)next;
    actell_glyph_t g;
    if (!actell_font_glyph(&data, cp, &g)) return false;
    out->adv_w = g.advance; out->box_w = g.width; out->box_h = g.height;
    out->ofs_x = g.x; out->ofs_y = g.y; out->stride = 0;
    out->format = (lv_font_glyph_format_t)data.bpp;
    out->is_placeholder = false; out->gid.index = g.id;
    return true;
}
static const void *bitmap(lv_font_glyph_dsc_t *g, lv_draw_buf_t *buf)
{
    actell_glyph_t glyph;
    if (g->req_raw_bitmap || !buf || !actell_font_glyph_id(&data, g->gid.index, &glyph)) return NULL;
    size_t stride = lv_draw_buf_width_to_stride(glyph.width, LV_COLOR_FORMAT_A8);
    if (!actell_font_decode(&data, &glyph, buf->data, buf->data_size, stride)) return NULL;
    lv_draw_buf_flush_cache(buf, NULL);
    return buf;
}

/* Only called during UI initialization, before publishing active to chat tasks. */
bool actell_caption_init(void)
{
    if (tried) return actell_caption_active();
    tried = true;
    const esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "actell_assets");
    const void *mapped;
    if (!partition || partition->size < 65536 || partition->size > 0x7d0000 ||
        esp_partition_mmap(partition, 0, partition->size, ESP_PARTITION_MMAP_DATA, &mapped, &mapping) != ESP_OK) goto fail;
    const uint8_t *p = mapped;
    uint32_t index_len = u32(p + 12), count = u32(p + 16), total = u32(p + 20);
    if (memcmp(p, "ACTRES01", 8) || u32(p + 8) != 1 || !index_len || index_len > 8192 ||
        !count || count > 32 || total > partition->size || total < 65536 ||
        memcmp(p + 56, (uint8_t[8]){0}, 8) || !hash(p + 64, index_len, p + 24)) goto unmap;
    const char *end;
    cJSON *root = cJSON_ParseWithLengthOpts((const char *)p + 64, index_len, &end, false);
    if (!root || end != (const char *)p + 64 + index_len) { cJSON_Delete(root); goto unmap; }
    uint32_t abi;
    cJSON *entries = cJSON_GetObjectItemCaseSensitive(root, "entries");
    bool valid = cJSON_IsObject(root) && integer(cJSON_GetObjectItemCaseSensitive(root, "reader_abi"), &abi) && abi == 1 &&
                 cJSON_IsArray(entries) && cJSON_GetArraySize(entries) == (int)count;
    const uint8_t *font_bytes = NULL; uint32_t font_length = 0, previous_end = 65536;
    const char *ids[32];
    for (uint32_t i = 0; valid && i < count; i++) {
        cJSON *entry = cJSON_GetArrayItem(entries, i);
        cJSON *id = cJSON_GetObjectItemCaseSensitive(entry, "id"), *format = cJSON_GetObjectItemCaseSensitive(entry, "format");
        uint32_t offset, length;
        valid = identifier(id) && identifier(format) &&
                integer(cJSON_GetObjectItemCaseSensitive(entry, "offset"), &offset) &&
                integer(cJSON_GetObjectItemCaseSensitive(entry, "length"), &length);
        if (!valid) break;
        ids[i] = id->valuestring;
        for (uint32_t j = 0; j < i; j++) if (!strcmp(ids[j], ids[i])) valid = false;
        if (!length || offset % 65536 || offset < previous_end || offset > total || length > total - offset ||
            !text_hash(p + offset, length, cJSON_GetObjectItemCaseSensitive(entry, "sha256"))) { valid = false; break; }
        previous_end = offset + length;
        if (!strcmp(id->valuestring, "caption.font")) {
            valid = !strcmp(format->valuestring, "lv-font-conv-bin-1.5.3");
            font_bytes = p + offset; font_length = length;
        }
    }
    valid = valid && previous_end == total && font_bytes && actell_font_open(&data, font_bytes, font_length);
    actell_glyph_t question;
    valid = valid && actell_font_glyph(&data, '?', &question);
    cJSON_Delete(root);
    if (!valid) goto unmap;
    font = (lv_font_t){.get_glyph_dsc = descriptor, .get_glyph_bitmap = bitmap,
                       .line_height = data.line_height, .base_line = data.baseline,
                       .kerning = LV_FONT_KERNING_NONE, .dsc = &data};
    atomic_store_explicit(&active, true, memory_order_release);
    ESP_LOGI("actell_fonts", "Read-only caption resource ready: %u glyphs, line height %u", (unsigned)data.count - 1, data.line_height);
    return true;
unmap:
    esp_partition_munmap(mapping);
fail:
    ESP_LOGW("actell_fonts", "Shared font unavailable or incompatible; using original font");
    return false;
}
bool actell_caption_active(void) { return atomic_load_explicit(&active, memory_order_acquire); }
const lv_font_t *actell_caption_font(void) { return actell_caption_active() ? &font : NULL; }
bool actell_caption_supported(uint32_t cp) { actell_glyph_t g; return actell_caption_active() && actell_font_glyph(&data, cp, &g); }
size_t actell_caption_normalize(const char *text, char *out, size_t cap) { return actell_font_normalize(&data, text, out, cap); }
void actell_caption_set_page(int width, int lines)
{
    if (width > 0 && width <= 480 && lines > 0 && lines <= 16) atomic_store(&page, ((unsigned)lines << 16) | (unsigned)width);
}
bool actell_caption_page(const char *text, size_t at, char *out, size_t cap)
{
    unsigned dimensions = atomic_load(&page);
    return actell_caption_active() && actell_font_page(&data, text, at, out, cap, dimensions & 65535, dimensions >> 16);
}
