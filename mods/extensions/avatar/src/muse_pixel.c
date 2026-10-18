/* SPDX-License-Identifier: Apache-2.0
 * Original character: a smooth milk-white floating round head with small
 * round hands and small cream ears with warm recessed inner arcs.
 * No pointed ears, hair-like accents, asymmetric shine, torso or legs. Original geometric pixel artwork; no
 * Jollybot renderer, imagery, palettes or motion code is copied here.
 */
#include "muse_pixel.h"
#include "actell_avatar.h"
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

#define W 64
#define H 64
static uint8_t fb[W * H];
static uint16_t colors[16];
static int size = 320;
enum { BLACK, OUTLINE, SHADE, WHITE, LIGHT, EAR, EARTOP, INK, BLUSH,
       MINT, GOLD, EAR_INNER, EAR_RIM, RED, DIM, BLUE };
#ifndef AVATAR_ROOT_SHADE
#define AVATAR_ROOT_SHADE 0xe5daca
#endif
static const uint32_t palette[16] = {
    0x000000,0x343b4c,0xb9bec8,0xe9e6dc,0xfffbef,0x697386,0x949cac,
    0x283142,0xf0afa7,0x76d9bd,0xf0bf60,0xefdbc3,AVATAR_ROOT_SHADE,0xf27979,
    0x171e2b,0x91c9f1
};
static void pixel(int x, int y, int c) { if ((unsigned)x < W && (unsigned)y < H) fb[y*W+x]=(uint8_t)c; }
static void rect(int x,int y,int w,int h,int c) { for(int j=y;j<y+h;j++) for(int i=x;i<x+w;i++) pixel(i,j,c); }
/* Bounded integer ellipse: no floating point in pixel loops. */
static void oval(int cx,int cy,int rx,int ry,int c) {
    for(int y=-ry;y<=ry;y++) for(int x=-rx;x<=rx;x++)
        if(x*x*ry*ry+y*y*rx*rx<=rx*rx*ry*ry) pixel(cx+x,cy+y,c);
}
static void outlined(int cx,int cy,int rx,int ry,int c) {
    oval(cx,cy,rx,ry,OUTLINE); if(rx>1&&ry>1) oval(cx,cy-1,rx-1,ry-1,c);
}
/* Rounded rectangle with a broad curved top: no one-pixel ear/head spikes. */
static void rounded(int x,int y,int w,int h,int r,int c) {
    for(int j=0;j<h;j++) for(int i=0;i<w;i++) {
        int dx=i<r?r-i:i>=w-r?i-(w-r-1):0;
        int dy=j<r?r-j:j>=h-r?j-(h-r-1):0;
        if(dx*dx+dy*dy<=r*r) pixel(x+i,y+j,c);
    }
}
static int roundi(float f) { return (int)lroundf(f); }
static float clamp(float f) { return f<0?0:f>1?1:f; }
static void eye(int x,int y,int gaze,int blink) {
    if(blink) rect(x-2,y,4,1,INK);
    else { oval(x,y,2,3,INK); pixel(x+gaze,y-1,LIGHT); }
}
uint32_t muse_pixel_accent(muse_mode_t mode) {
    static const uint32_t accents[MUSE_MODE_COUNT] = {
        0xf0bf60,0x76d9bd,0x91c9f1,0xc0a7e8,0x76d9bd,0xf27979,0x697386
    };
    return (unsigned)mode<MUSE_MODE_COUNT?accents[mode]:accents[MUSE_MODE_IDLE];
}
void muse_pixel_set_size(int px) { size=px<1?1:px>512?512:px; }
void muse_pixel_scale(uint16_t *dst,int stride,int x0,int x1,int y0,int y1) {
    if(!dst || x0<0 || y0<0 || x1>=size || y1>=size || x1<x0 || y1<y0) return;
    if(stride<x1-x0+1 || stride>512) return;
    for(int y=y0;y<=y1;y++) for(int x=x0;x<=x1;x++)
        dst[(size_t)(y-y0)*(size_t)stride+(size_t)(x-x0)]=colors[fb[(y*H/size)*W+x*W/size]];
}
void muse_pixel_render(const muse_pose_t *p) {
    if(!p) return;
    float t=isfinite(p->t)?fmodf(fabsf(p->t),120.0f):0;
    float mt=isfinite(p->mode_t)?fminf(fmaxf(p->mode_t,0),3600):0;
    float level=isfinite(p->level)?clamp(p->level):0;
    muse_mode_t m=(unsigned)p->mode<MUSE_MODE_COUNT?p->mode:MUSE_MODE_IDLE;
    actell_avatar_activity_t action=actell_avatar_activity();
    /* Phone props never obscure boot, shutdown, errors or active conversation. */
    if(m!=MUSE_MODE_IDLE && m!=MUSE_MODE_THINKING) action=ACTELL_AVATAR_NONE;
    float fade=m==MUSE_MODE_OFF?clamp(1.0f-mt/1.3f):1.0f;
    for(int i=0;i<16;i++) {
        unsigned r=(unsigned)(((palette[i]>>16)&255)*fade),g=(unsigned)(((palette[i]>>8)&255)*fade),b=(unsigned)((palette[i]&255)*fade);
        colors[i]=(uint16_t)(((r>>3)<<11)|((g>>2)<<5)|(b>>3));
    }
    memset(fb,0,sizeof fb);
    int bob=roundi(sinf(t*2.6f)), dx=0, dy=bob;
    if(m==MUSE_MODE_BOOT) dy+=roundi(18*(1-clamp(mt/0.65f)));
    if(m==MUSE_MODE_ERROR && mt<0.65f) dx=roundi(sinf(mt*55)*2);
    if(m==MUSE_MODE_SPEAKING) dy-=roundi(level*2);
    if(m==MUSE_MODE_OFF) dy+=roundi(clamp(mt)*4);
    bool pet=p->happy>0.1f && m!=MUSE_MODE_ERROR && m!=MUSE_MODE_OFF;
    if(pet) dy-=roundi(fabsf(sinf(t*9))*3);
    /* Reading leans the whole head down toward the near phone. Writing stays
     * a little farther back, leaving room to see the reaching hand. */
    int lean=action==ACTELL_AVATAR_READ?2+roundi((sinf(t*2.2f)+1)*1.0f):0;
    int cx=31+dx, hy=28+dy+lean;
    oval(31,58,15,2,DIM);
    /* Small bear-like round ears rise from the upper corners of the head.
     * One simple pale oval inside each ear, no folds or scalloped ear edges. */
    for(int side=-1;side<=1;side+=2) {
        int ex=cx+side*15, ey0=hy-15;
        rounded(ex-5,ey0-5,11,11,4,OUTLINE);
        rounded(ex-4,ey0-4,9,9,3,LIGHT);

    }
    /* Uniform creamy face: no dark tufts or asymmetric forehead shine. */
    rounded(cx-19,hy-17,39,35,13,OUTLINE);
    rounded(cx-18,hy-16,37,33,12,LIGHT);
    /* Fuse ear roots into the head. At this point the only dark strokes in
     * this region are the head/ear contours. A dark pixel surrounded on all
     * four sides by the joined cream silhouette is an INTERNAL seam, not
     * the exterior edge: erase it to cream. Keep the union's outer outline.
     * Recolouring preserves occupancy, so the single pass is order-independent. */
    for(int y=1;y<H-1;y++) for(int x=1;x<W-1;x++) {
        int i=y*W+x;
        if(fb[i]==OUTLINE && fb[i-1]!=BLACK && fb[i+1]!=BLACK &&
           fb[i-W]!=BLACK && fb[i+W]!=BLACK) fb[i]=LIGHT;
    }

    /* One continuous shallow lower-inner surface per ear, tapering into
     * the root. The upper-left opening remains cream, consistent with light
     * from upper left. No separate root dashes or decorative cavity marks. */
    for(int side=-1;side<=1;side+=2) {
        int ex=cx+side*15, ey0=hy-15;
        rounded(ex-2,ey0-2,5,6,2,EAR_RIM);
        rounded(ex-1,ey0-1,4,4,1,EAR_INNER);
        rounded(ex-3,ey0-3,5,5,2,LIGHT);
        rect(ex-1,ey0+3,3,1,EAR_RIM);
        pixel(ex-side,ey0+4,EAR_RIM);
    }

    int ey=hy+(action?4:0);
    int blink=fmodf(t,3.5f)<0.13f;
    if(m==MUSE_MODE_BOOT && mt<0.65f) blink=1;
    if(m==MUSE_MODE_OFF && mt>0.35f) blink=1;
    if(m==MUSE_MODE_ERROR) {
        for(int k=-2;k<=2;k++){pixel(cx-7+k,ey+k,INK);pixel(cx-7+k,ey-k,INK);pixel(cx+7+k,ey+k,INK);pixel(cx+7+k,ey-k,INK);}
    } else if(pet) {
        for(int k=0;k<3;k++){pixel(cx-9+k,ey-k,INK);pixel(cx-7+k,ey-2+k,INK);pixel(cx+5+k,ey-k,INK);pixel(cx+7+k,ey-2+k,INK);}
    } else if(action && !blink) {
        /* Low, inward-facing eyes visibly track the screen facing the head. */
        oval(cx-6,ey+1,2,2,INK);oval(cx+6,ey+1,2,2,INK);
        pixel(cx-5,ey+2,LIGHT);pixel(cx+5,ey+2,LIGHT);
    } else {
        int gaze=m==MUSE_MODE_THINKING?-1:0;
        eye(cx-7+gaze,ey,gaze,blink);eye(cx+7+gaze,ey,gaze,blink);
    }
    if(m==MUSE_MODE_SPEAKING) oval(cx,ey+7,2,1+(int)(level*3),INK);
    else if(m==MUSE_MODE_LISTENING) oval(cx,ey+7,2,2,INK);
    else if(m==MUSE_MODE_ERROR || m==MUSE_MODE_OFF) rect(cx-2,ey+7,5,1,INK);
    else {pixel(cx-2,ey+6,INK);rect(cx-1,ey+7,3,1,INK);pixel(cx+2,ey+6,INK);}
    rect(cx-12,ey+5,3,1,BLUSH);rect(cx+10,ey+5,3,1,BLUSH);

    if(action) {
        int py=action==ACTELL_AVATAR_READ?39+bob:38+bob;
        int left=action==ACTELL_AVATAR_READ?25:27;
        int right=left+12;
        if(action==ACTELL_AVATAR_WRITE) {
            /* Simple small floating hand, like the speaking gesture. Its
             * small in/out swing can tuck behind the phone's right edge.
             * No arm, elbow, long forearm or multi-joint motion. */
            int handx=right+3+roundi(sinf(t*7.0f)*2);
            int handy=py+6+roundi(cosf(t*7.0f));
            outlined(handx,handy,3,3,LIGHT);
        }
        /* Narrow portrait phone BACK only: 13 x 23 pixels, fully inside
         * the canvas. One flat matte shell and rear camera; no side rail,
         * highlight stripe, side button, display bezel or screen contents. */
        rect(left,py+1,13,21,OUTLINE);rect(left+1,py,11,23,OUTLINE);
        rect(left+1,py+1,11,21,EAR);
        rect(left+2,py+2,3,4,OUTLINE);pixel(left+3,py+3,BLUE);
        /* Holding paws wrap the near edges, separate from the far-side tapping
         * hand. Read uses BOTH hands; write keeps only its left holding hand. */
        outlined(left-1,py+14,3,3,LIGHT);
        if(action==ACTELL_AVATAR_READ) outlined(right+1,py+14,3,3,LIGHT);
    } else if(m==MUSE_MODE_LISTENING) {
        outlined(cx-19,hy+8,4,5,WHITE);
        int r=3+(int)(level*4);
        for(int k=0;k<r;k++){pixel(5+k/3,23-k,BLUE);pixel(58-k/3,23+k,BLUE);}
    } else if(m==MUSE_MODE_THINKING) {
        outlined(cx+6,hy+16,4,3,WHITE);
        for(int i=0;i<3;i++) oval(44+i*5,8-(i==((int)(t*3)%3)?2:0),1,1,MINT);
    } else if(m==MUSE_MODE_SPEAKING || m==MUSE_MODE_OFF || pet) {
        int wave=roundi(sinf(t*8)*3);
        outlined(cx+21+wave/2,hy+9+wave,4,4,WHITE);
        if(pet) outlined(cx-21,hy+7-wave,4,4,WHITE);
    }
    if(m==MUSE_MODE_BOOT) { for(int i=0;i<3;i++) if(mt>i*0.18f) rect(24+i*6,5,3,2,GOLD); }
    if(m==MUSE_MODE_ERROR) {rect(55,10,2,6,RED);rect(55,18,2,2,RED);}
    if(pet) {pixel(10,9,MINT);rect(9,10,3,1,MINT);pixel(10,11,MINT);pixel(53,7,GOLD);}
}
