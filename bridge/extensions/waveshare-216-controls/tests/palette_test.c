#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "sdk_colors.h"
#define LV_OBJ_FLAG_HIDDEN 1
#define LV_OBJ_FLAG_CLICKABLE 2
#define LV_OBJ_FLAG_CLICK_FOCUSABLE 4
#define LV_OBJ_FLAG_SCROLLABLE 8
#define LV_OPA_TRANSP 0
#define LV_OPA_COVER 255
#define LV_TEXT_ALIGN_CENTER 0
#define LV_LABEL_LONG_MODE_CLIP 0
#define LV_SYMBOL_VOLUME_MID "on"
#define LV_SYMBOL_MUTE "off"
enum { MUSE_MODE_IDLE, MUSE_MODE_LISTENING, MUSE_MODE_SPEAKING };
typedef int muse_mode_t;
typedef int lv_font_t;
static lv_font_t lv_font_montserrat_24, lv_font_montserrat_28;
typedef struct { int align,x,y; } muse_button_hint_t;
static struct { muse_button_hint_t speaker_hint,power_hint; } board, *muse_board=&board;
typedef struct { uint32_t text, bg; const char *label; int opa,border,outline,flags; } lv_obj_t;
static lv_obj_t mic, spk = { .label = "" }, pwr;
static lv_obj_t *s_ptt_hint = &mic, *s_spk_hint = &spk, *s_pwr_hint = &pwr;
static lv_obj_t *s_tv, *s_settings;
static bool speaker_on = true;
static lv_obj_t *lv_label_create(lv_obj_t *scr) {
    (void)scr; static int count; lv_obj_t *obj = count++ ? &pwr : &spk;
    *obj=(lv_obj_t){.label="",.opa=255,.border=2,.outline=2,.flags=14};return obj;
}
static void lv_obj_remove_style_all(lv_obj_t *obj) { obj->opa=obj->border=obj->outline=0; }
static void lv_obj_set_style_text_font(lv_obj_t *obj,const lv_font_t *font,int part) { (void)obj;(void)font;(void)part; }
static void lv_obj_set_style_text_align(lv_obj_t *obj,int align,int part) { (void)obj;(void)align;(void)part; }
static void lv_obj_set_style_bg_opa(lv_obj_t *obj,int opa,int part) { (void)part;obj->opa=opa; }
static void lv_obj_set_style_border_width(lv_obj_t *obj,int n,int part) { (void)part;obj->border=n; }
static void lv_obj_set_style_outline_width(lv_obj_t *obj,int n,int part) { (void)part;obj->outline=n; }
static void lv_obj_remove_flag(lv_obj_t *obj,int flags) { obj->flags &= ~flags; }
static void lv_obj_set_style_radius(lv_obj_t *obj,int n,int part) { (void)obj;(void)n;(void)part; }
static void lv_obj_set_size(lv_obj_t *obj,int w,int h) { (void)obj;(void)w;(void)h; }
static void lv_label_set_long_mode(lv_obj_t *obj,int mode) { (void)obj;(void)mode; }
static void lv_obj_align(lv_obj_t *obj,int align,int x,int y) { (void)obj;(void)align;(void)x;(void)y; }
static lv_obj_t *make_label(lv_obj_t *scr,const lv_font_t *font,uint32_t color) {
    (void)font;lv_obj_t *obj=lv_label_create(scr);obj->text=color;return obj;
}
static lv_obj_t *lv_tileview_get_tile_active(lv_obj_t *tv) { return tv; }
static void lv_obj_set_flag(lv_obj_t *obj, int flag, bool value) { (void)obj;(void)flag;(void)value; }
static uint32_t lv_color_hex(uint32_t color) { return color; }
static uint32_t lv_color_black(void) { return 0; }
static void lv_obj_set_style_text_color(lv_obj_t *obj, uint32_t color, int part) { (void)part;obj->text=color; }
static void lv_obj_set_style_bg_color(lv_obj_t *obj, uint32_t color, int part) { (void)part;obj->bg=color; }
static void color_mic(lv_obj_t *obj, uint32_t color) { obj->bg=color; }
static uint32_t muse_pixel_accent(muse_mode_t mode) { (void)mode;return 0x123456; }
static bool muse_settings_speaker_on(void) { return speaker_on; }
static const char *lv_label_get_text(lv_obj_t *obj) { return obj->label; }
static void lv_label_set_text(lv_obj_t *obj, const char *text) { obj->label=text; }
#include "sdk_palette.inc"
int main(void) {
    (void)muse_pixel_accent; (void)lv_color_black; (void)muse_board; (void)lv_obj_set_style_bg_color; (void)make_label; (void)lv_font_montserrat_24;
    (void)lv_obj_set_style_radius; (void)lv_obj_remove_style_all;
    (void)lv_obj_set_style_text_font; (void)lv_obj_set_style_text_align;
    (void)lv_obj_set_style_border_width; (void)lv_obj_set_style_outline_width;
#if CONFIG_MUSE_BOARD_WAVESHARE_S3_216
    s_spk_hint=make_control_icon(NULL,&board.speaker_hint,"",COLOR_DIM);
    s_pwr_hint=make_control_icon(NULL,&board.power_hint,"power",COLOR_DIM);
#else
    s_spk_hint=make_control_icon(NULL,&board.speaker_hint,"",0xffd060);
    s_pwr_hint=make_control_icon(NULL,&board.power_hint,"power",0xff8080);
#endif
#if CONFIG_MUSE_BOARD_WAVESHARE_S3_216
    assert(spk.opa==255 && spk.border==0 && spk.outline==0 && spk.flags==0);
    assert(pwr.opa==255 && pwr.border==0 && pwr.outline==0 && pwr.flags==0);
#else
    assert(spk.opa==255 && pwr.opa==255);
#endif
#if CONFIG_MUSE_BOARD_WAVESHARE_S3_216
    assert(pwr.text==COLOR_DIM);
#else
    assert(pwr.text==0xff8080);
#endif
    update_control_hints(MUSE_MODE_IDLE);
#if CONFIG_MUSE_BOARD_WAVESHARE_S3_216
    assert(mic.bg == COLOR_DIM && spk.text == COLOR_DIM && spk.bg == 0);
#else
    assert(mic.bg == 0x80bfff && spk.text == 0xffd060);
#endif
    update_control_hints(MUSE_MODE_LISTENING);
#if CONFIG_MUSE_BOARD_WAVESHARE_S3_216
    assert(mic.bg == muse_pixel_accent(MUSE_MODE_LISTENING));
#else
    assert(mic.bg == 0x64d4ff);
#endif
    speaker_on=false; update_control_hints(MUSE_MODE_SPEAKING);
#if CONFIG_MUSE_BOARD_WAVESHARE_S3_216
    assert(mic.bg == COLOR_DIM && spk.text == COLOR_DIM && spk.bg == 0);
#else
    assert(mic.bg == 0x80bfff && spk.text == 0x8b7845);
#endif
    speaker_on=true; update_control_hints(MUSE_MODE_IDLE);
#if CONFIG_MUSE_BOARD_WAVESHARE_S3_216
    assert(spk.text == COLOR_DIM && spk.bg == 0);
#endif
    puts("palette: gray glyphs, original recording accent, black backdrop and no border passed");
}
