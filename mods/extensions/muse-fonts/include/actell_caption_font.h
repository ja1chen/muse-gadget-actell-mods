/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
bool actell_caption_init(void);
bool actell_caption_active(void);
bool actell_caption_supported(uint32_t cp);
size_t actell_caption_normalize(const char *text, char *out, size_t cap);
void actell_caption_set_page(int width, int lines);
bool actell_caption_page(const char *text, size_t at, char *out, size_t cap);
/* LVGL header must be included by UI callers. */
struct _lv_font_t;
const struct _lv_font_t *actell_caption_font(void);
#ifdef __cplusplus
}
#endif
