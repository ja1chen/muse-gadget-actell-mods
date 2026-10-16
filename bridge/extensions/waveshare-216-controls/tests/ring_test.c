#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#define LV_MIN(a,b) ((a)<(b)?(a):(b))
#define LV_MAX(a,b) ((a)>(b)?(a):(b))
#define LV_ABS(a) ((a)<0?-(a):(a))
#define LV_PART_MAIN 0
#define RING_SLAB_ROWS 52
static int s_h = 480;
typedef struct { int32_t x1,y1,x2,y2; } lv_area_t;
typedef struct { lv_area_t _clip_area; } lv_layer_t;
typedef struct { lv_layer_t *layer; } lv_event_t;
static void *s_ring;
static lv_area_t original;
static unsigned calls;
static lv_layer_t *lv_event_get_layer(lv_event_t *e) { return e->layer; }
static void lv_obj_get_coords(void *obj, lv_area_t *out) { (void)obj; *out=(lv_area_t){4,4,475,475}; }
static int lv_area_get_width(const lv_area_t *a) { return a->x2-a->x1+1; }
static int lv_obj_get_style_arc_width(void *obj,int part) { (void)obj;(void)part;return 6; }
static void lv_event_stop_processing(lv_event_t *e) { (void)e; }
static bool lv_area_intersect(lv_area_t *out,const lv_area_t *a,const lv_area_t *b) {
    *out=(lv_area_t){LV_MAX(a->x1,b->x1),LV_MAX(a->y1,b->y1),LV_MIN(a->x2,b->x2),LV_MIN(a->y2,b->y2)};
    return out->x1<=out->x2 && out->y1<=out->y2;
}
static void lv_obj_event_base(void *obj,lv_event_t *e) {
    (void)obj;
    lv_area_t a=e->layer->_clip_area;
    assert(a.x1>=original.x1 && a.x2<=original.x2 && a.y1>=original.y1 && a.y2<=original.y2);
    assert(a.x1<=a.x2 && a.y1<=a.y2);
#if CONFIG_MUSE_BOARD_WAVESHARE_S3_216
    const int x[] = { 126,239,352 }; /* ring coordinates use integer center */
    for(int i=0;i<3;i++) {
        int half=i==1?16:20;
        lv_area_t gap={x[i]-half+1,4,x[i]+half,43}, intersection;
        assert(!lv_area_intersect(&intersection,&a,&gap));
    }
#endif
    calls++;
}
#include "sdk_ring.inc"
int main(void) {
    (void)s_h;
    lv_layer_t layer;
    lv_event_t e={&layer};
    const lv_area_t clips[]={{0,0,479,479},{100,10,150,60},{200,0,270,40},{300,20,380,70},{0,50,479,479}};
    for(unsigned i=0;i<sizeof(clips)/sizeof(clips[0]);i++) {
        original=layer._clip_area=clips[i];
        on_ring_draw(&e);
        assert(layer._clip_area.x1==original.x1 && layer._clip_area.y1==original.y1 &&
               layer._clip_area.x2==original.x2 && layer._clip_area.y2==original.y2);
    }
    assert(calls>0);
    printf("ring: production clipping, original clip bounds/restoration, board=%d passed\n",CONFIG_MUSE_BOARD_WAVESHARE_S3_216);
}
