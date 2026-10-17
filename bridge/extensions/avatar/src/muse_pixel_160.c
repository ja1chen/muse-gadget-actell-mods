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

#define W 160
#define H 160
/* Fixed, bounded framebuffer: no heap allocation or runtime failure path.
 * 25,600 bytes of BSS; optional external BSS may be selected by the application.
 * Do not put this on the UI task stack. */
#if defined(ESP_PLATFORM) && defined(CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY) && CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
#include "esp_attr.h"
#define AVATAR_BUFFER_ATTR EXT_RAM_BSS_ATTR
#else
#define AVATAR_BUFFER_ATTR
#endif
static AVATAR_BUFFER_ATTR uint8_t fb[W * H];
static uint16_t scale_map[512];
static int mapped_size;
static AVATAR_BUFFER_ATTR uint8_t head_cache[W * H];
static float cache_cx, cache_hy, cache_nod, cache_pitch;
static bool cache_valid;
static float brow_closure;
static float nod_offset;
static float pitch;
/* Independent compile-time fallbacks; V2 off still selects original 64-grid. */
#ifndef MUSE_AVATAR_ROUNDED_FACE
#define MUSE_AVATAR_ROUNDED_FACE 1
#endif
#ifndef MUSE_AVATAR_SHADING
#define MUSE_AVATAR_SHADING 1
#endif
#ifndef MUSE_AVATAR_DYNAMIC_LIGHT
#define MUSE_AVATAR_DYNAMIC_LIGHT 0
#endif
#define AVATAR_PALETTE_SIZE (MUSE_AVATAR_SHADING ? 48 : 16)
static uint16_t colors[AVATAR_PALETTE_SIZE];
static int size = 320;
enum { BLACK, OUTLINE, SHADE, WHITE, LIGHT, EAR, EARTOP, INK, BLUSH,
       MINT, GOLD, EAR_INNER, EAR_RIM, RED, DIM, BLUE };
#ifndef AVATAR_ROOT_SHADE
#define AVATAR_ROOT_SHADE 0xe5daca
#endif
static const uint32_t palette[AVATAR_PALETTE_SIZE] = {
    0x000000,0x343b4c,0xb9bec8,0xe9e6dc,0xfffbef,0x697386,0xfffdf8,
    0x283142,0xf0afa7,0x76d9bd,0xf0bf60,0xefdbc3,AVATAR_ROOT_SHADE,0xf27979,
    0x171e2b,0x91c9f1
#if MUSE_AVATAR_SHADING
,0xfffbef,0xfef9ed,0xfdf7ea,0xfbf5e8,0xfaf3e5,0xf9f1e3,0xf8efe0,0xf6edde,0xf5ebdb,0xf4e9d9,0xf3e7d6,0xf2e5d4,0xf0e3d1,0xefe1cf,0xeedfcc,0xedddca,0xebdcc7,0xeadac5,0xe9d8c2,0xe8d6c0,0xe6d4bd,0xe5d2bb,0xe4d0b8,0xe3ceb6,0xe2ccb3,0xe0cab1,0xdfc8ae,0xdec6ac,0xddc4a9,0xdbc2a7,0xdac0a4,0xd9bea2
#endif
};
static void native_pixel(int x,int y,int c){y+=5;if((unsigned)x<W&&(unsigned)y<H)fb[y*W+x]=(uint8_t)c;}
static void rect(int x,int y,int w,int h,int c){for(int j=(int)lroundf(y*2.5f+nod_offset);j<(int)lroundf((y+h)*2.5f+nod_offset);j++)for(int i=(int)lroundf(x*2.5f);i<(int)lroundf((x+w)*2.5f);i++)native_pixel(i,j,c);}
static void pixel(int x,int y,int c){rect(x,y,1,1,c);}
static void precise_oval(float cx,float cy,float rx,float ry,int c){
 float X=(cx+.5f)*2.5f,Y=(cy+.5f)*2.5f+nod_offset,RX=rx*2.5f,RY=ry*2.5f;
 if(RX<.1f||RY<.1f)return;
 for(int y=(int)floorf(Y-RY);y<(int)ceilf(Y+RY);y++)for(int x=(int)floorf(X-RX);x<(int)ceilf(X+RX);x++){
 float dx=(x+.5f-X)/RX,dy=(y+.5f-Y)/RY;if(dx*dx+dy*dy<=1)native_pixel(x,y,c);}}
static void oval(int cx,int cy,int rx,int ry,int c){precise_oval(cx,cy,rx+.5f,ry+.5f,c);}
static void outlined(int cx,int cy,int rx,int ry,int c){oval(cx,cy,rx,ry,OUTLINE);if(rx>1&&ry>1)oval(cx,cy-1,rx-1,ry-1,c);}
static void phone_round(float x,float y,float w,float h,float r,int c){
 float X=x*2.5f,Y=y*2.5f+nod_offset,WW=w*2.5f,HH=h*2.5f,R=r*2.5f;
 for(int j=(int)floorf(Y);j<(int)ceilf(Y+HH);j++)for(int i=(int)floorf(X);i<(int)ceilf(X+WW);i++){
 float py=j+.5f-Y;float taper=1-.14f*(py/HH);float px=(i+.5f-X-WW*.5f)/taper+WW*.5f;float dx=fmaxf(fmaxf(R-px,px-(WW-R)),0),dy=fmaxf(fmaxf(R-py,py-(HH-R)),0);
 if(dx*dx+dy*dy<=R*R)native_pixel(i,j,c);
 }
}
static float smooth_union(float a,float b,float k){float h=fmaxf(k-fabsf(a-b),0)/k;return fminf(a,b)-h*h*k*.25f;}
#ifndef MUSE_AVATAR_EXACT_DISTANCE_PREFILTER
#define MUSE_AVATAR_EXACT_DISTANCE_PREFILTER 1
#endif
#if MUSE_AVATAR_EXACT_DISTANCE_PREFILTER
/* Conservative squared-distance certificates. Only the ambiguous boundary band
 * uses the original hypotf/smooth-union expression below. Union(a,b,k) is in
 * [min(a,b)-k/4,min(a,b)]; two ears can lower distance by at most 1.75.
 * The extra .01 margin protects threshold rounding; no pose quantization,
 * lookup interpolation, new persistent cache, or changed animation timing.
 */
static bool head_below(float px,float py,float qx,float qy,float threshold){
 if(py>0){
  float ax=px/(19.5f*2.5f),ay=py/(17.32f*2.5f);
  float radius=1+threshold/(17.32f*2.5f);
  return ax*ax+ay*ay < radius*radius;
 }
 float ax=fmaxf(qx,0),ay=fmaxf(qy,0);
 float radius=threshold-fminf(fmaxf(qx,qy),0)+16*2.5f;
 return radius>0 && ax*ax+ay*ay < radius*radius;
}
#endif
/* Local-space, bounded palette lighting: no new timer, cache key or buffer.
 * Default light is static; geometric movement still changes shaded pixels.
 * Both exact boundary and certified interior paths use the same fill. */
static int skin_fill(float px,float py){
#if MUSE_AVATAR_SHADING
 float nx=px*(1/48.75f),ny=py*(1/43.30f);
 float shade=.10f+.18f*nx+.24f*ny+.27f*(nx*nx+ny*ny);
#if MUSE_AVATAR_DYNAMIC_LIGHT
 shade+=pitch*.075f*(ny+.45f);
#endif
 shade=fminf(1,fmaxf(0,shade));
 static const uint8_t thresholds[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
 int hx=(int)(px+160),hy=(int)(py+160);
 float step=shade*31.0f;int band=(int)step;
 if(band<31 && step-band>(thresholds[((hy&3)<<2)|(hx&3)]+.5f)*(1/16.0f))band++;
 return 16+band;
#else
 (void)px;(void)py;return LIGHT;
#endif
}
/* Circular ear lobes and softly blended roots; true 160-grid geometry. */
static void head_and_ears(float cx,float hy){
 if(cache_valid && cx==cache_cx && hy==cache_hy && nod_offset==cache_nod && pitch==cache_pitch){
 memcpy(fb,head_cache,sizeof fb);return;
 }
 float X=(cx+.5f)*2.5f,Y=(hy+.5f)*2.5f+nod_offset;
 for(int y=0;y<H;y++)for(int x=0;x<W;x++){
 float px=x+.5f-X,py=(y+.5f-Y)/(1-.08f*pitch);
 float lower=fmaxf(0,py/(17.5f*2.5f));
 px/=1-(MUSE_AVATAR_ROUNDED_FACE ? .04f : .14f)*lower;
 float qx=fabsf(px)-(19.5f-16)*2.5f,qy=fabsf(py)-(17.5f-16)*2.5f;
#if MUSE_AVATAR_EXACT_DISTANCE_PREFILTER
 /* A certified interior pixel cannot become outline after either union.
  * A certified exterior pixel remains outside even at maximum blending. */
 float elx=px+14*2.5f,erx=px-14*2.5f,epy=py+15*2.5f;
 float el2=elx*elx+epy*epy,er2=erx*erx+epy*epy;
 const float inner=5.5f*2.5f-1.81f,outer=5.5f*2.5f+1.76f;
 if(head_below(px,py,qx,qy,-1.81f) || el2<inner*inner || er2<inner*inner){
  native_pixel(x,y,skin_fill(px,py));continue;
 }
 if(!head_below(px,py,qx,qy,1.76f) && el2>outer*outer && er2>outer*outer)continue;
#endif
 float d=hypotf(fmaxf(qx,0),fmaxf(qy,0))+fminf(fmaxf(qx,qy),0)-16*2.5f;
 if(py>0)d=(hypotf(px/(19.5f*2.5f),py/(17.32f*2.5f))-1)*(17.32f*2.5f);
 for(int side=-1;side<=1;side+=2){float e=hypotf(px-side*14*2.5f,py+15*2.5f)-5.5f*2.5f;d=smooth_union(d,e,3.5f);}
 if(d<=0)native_pixel(x,y,d< -1.8f?skin_fill(px,py):OUTLINE);
 }
 for(int side=-1;side<=1;side+=2){
 float ex=cx+side*14,ey=hy-15;
 precise_oval(ex+.55f,ey+.8f,2.8f,3.1f,EAR_RIM);
 precise_oval(ex+.85f,ey+.5f,2.0f,2.3f,EAR_INNER);
 precise_oval(ex-.65f,ey-.6f,2.5f,2.55f,LIGHT);
 }
 memcpy(head_cache,fb,sizeof fb);
 cache_cx=cx;cache_hy=hy;cache_nod=nod_offset;cache_pitch=pitch;cache_valid=true;
}
static int roundi(float f) { return (int)lroundf(f); }
static float clamp(float f) { return f<0?0:f>1?1:f; }
static float smooth(float t){t=clamp(t);return t*t*(3-2*t);}
static float shift(float t,float start,float from,float to){return from+(to-from)*smooth((t-start)/.24f);}
static float blink_at(float t,float at){float d=t-at;if(d<0||d>.24f)return 0;return d<.08f?smooth(d/.08f):1-smooth((d-.08f)/.16f);}
/* One shared dark eye + small catchlight. No sclera. */
static void eye(float x,float y,float openness,float gx,float gy,float wide){
 float X=(x+.5f+gx)*2.5f,Y=(y+.5f+gy)*2.5f+nod_offset,RX=6.25f,RY=(3.5f+wide)*2.5f*(1-.12f*pitch),lid=RY*fmaxf(.08f,openness);
 for(int j=(int)floorf(Y-RY-1);j<=(int)ceilf(Y+RY+1);j++)for(int i=(int)floorf(X-RX-1);i<=(int)ceilf(X+RX+1);i++){
 float dx=i+.5f-X,dy=j+.5f-Y;if(dx*dx/(RX*RX)+dy*dy/(RY*RY)>1||dy>lid||dy < -lid)continue;
 int c=INK;if(openness>.6f&&dx>=-1.7f&&dx<=.3f&&dy>=-3.2f&&dy<=-1.2f)c=EARTOP;native_pixel(i,j,c);}}
static void paw(float x,float y,float r){precise_oval(x,y,r+.55f,r+.55f,OUTLINE);precise_oval(x,y-.35f,r,r,LIGHT);}
uint32_t muse_pixel_accent(muse_mode_t mode) {
    static const uint32_t accents[MUSE_MODE_COUNT] = {
        0xf0bf60,0x76d9bd,0x91c9f1,0xc0a7e8,0x76d9bd,0xf27979,0x697386
    };
    return (unsigned)mode<MUSE_MODE_COUNT?accents[mode]:accents[MUSE_MODE_IDLE];
}
void muse_pixel_set_size(int px) {
 size=px<1?1:px>512?512:px;
 if(mapped_size!=size){for(int i=0;i<size;i++)scale_map[i]=(uint16_t)(i*W/size);mapped_size=size;}
}
void muse_pixel_scale(uint16_t *dst,int stride,int x0,int x1,int y0,int y1) {
    if(!dst || x0<0 || y0<0 || x1>=size || y1>=size || x1<x0 || y1<y0) return;
    if(stride<x1-x0+1 || stride>512) return;
    if(mapped_size!=size)muse_pixel_set_size(size);
    for(int y=y0;y<=y1;y++) {
        const uint8_t *row=fb+(size_t)scale_map[y]*W;
        for(int x=x0;x<=x1;x++)
            dst[(size_t)(y-y0)*(size_t)stride+(size_t)(x-x0)]=colors[row[scale_map[x]]];
    }
}
void muse_pixel_render(const muse_pose_t *p) {
    if(!p) return;
    float t=isfinite(p->t)?fmodf(fabsf(p->t),120.0f):0;
    float mt=isfinite(p->mode_t)?fminf(fmaxf(p->mode_t,0),3600):0;
    float level=isfinite(p->level)?clamp(p->level):0;
    float mouth_level=p->mouth_level_valid ? (isfinite(p->mouth_level)?clamp(p->mouth_level):0) : level;
    muse_mode_t m=(unsigned)p->mode<MUSE_MODE_COUNT?p->mode:MUSE_MODE_IDLE;
    actell_avatar_activity_t action=actell_avatar_activity();
    /* Phone props never obscure boot, shutdown, errors or active conversation. */
    if(m!=MUSE_MODE_IDLE && m!=MUSE_MODE_THINKING) action=ACTELL_AVATAR_NONE;
    float fade=m==MUSE_MODE_OFF?clamp(1.0f-mt/1.3f):1.0f;
    for(int i=0;i<AVATAR_PALETTE_SIZE;i++) {
        unsigned r=(unsigned)(((palette[i]>>16)&255)*fade),g=(unsigned)(((palette[i]>>8)&255)*fade),b=(unsigned)((palette[i]&255)*fade);
        colors[i]=(uint16_t)(((r>>3)<<11)|((g>>2)<<5)|(b>>3));
    }
    memset(fb,0,sizeof fb);brow_closure=0;
    float phase=fmodf(t,8.0f),nod=0;
    if(m==MUSE_MODE_LISTENING){
        float q=fmodf(phase,4.0f);
        if(q>.65f&&q<1.35f)nod=sinf((q-.65f)/.70f*3.14159265f);
        if(q>1.43f&&q<2.13f)nod=.9f*sinf((q-1.43f)/.70f*3.14159265f);
    }
    pitch=nod;nod*=2.0f;nod_offset=nod;
    bool legacy=m==MUSE_MODE_BOOT||m==MUSE_MODE_ERROR||m==MUSE_MODE_OFF;
    int bob=legacy?roundi(sinf(t*2.6f)):0,dx=0,dy=bob;
    if(m==MUSE_MODE_BOOT)dy+=roundi(15*(1-clamp(mt/.65f)));
    if(m==MUSE_MODE_ERROR&&mt<.65f)dx=roundi(sinf(mt*55)*2);
    if(m==MUSE_MODE_OFF)dy+=roundi(clamp(mt)*4);
    bool pet=p->happy>.1f&&m!=MUSE_MODE_ERROR&&m!=MUSE_MODE_OFF;
    int cx=31+dx,hy=28+dy;
    nod_offset=nod; /* No ground shadow. */
    head_and_ears(cx,hy);
    nod_offset=nod+pitch*7.0f;
    int ey=hy;
    if(legacy){
      int blink=fmodf(t,3.5f)<.13f;
      if(m==MUSE_MODE_BOOT&&mt<.65f)blink=1;
      if(m==MUSE_MODE_OFF&&mt>.35f)blink=1;
      if(m==MUSE_MODE_ERROR){for(int k=-2;k<=2;k++){pixel(cx-7+k,ey+k,INK);pixel(cx-7+k,ey-k,INK);pixel(cx+7+k,ey+k,INK);pixel(cx+7+k,ey-k,INK);}}
      else{eye(cx-7,ey,blink?0:1,0,0,0);eye(cx+7,ey,blink?0:1,0,0,0);}
    }else{
      float gx=0,gy=0,wide=0,open=1,close=0,furrow=0;
      if(action==ACTELL_AVATAR_READ){
        float q=fmodf(phase,4.0f);gx=-.65f+1.3f*smooth((q-.60f)/.16f);gy=.65f;
        /* Top-left .60s and top-right 1.14s fixation, full Z cycle 4s. */
        if(q>1.90f){float u=smooth((q-1.90f)/.18f);gx=.65f-1.3f*u;gy=.65f+.65f*u;}
        if(q>2.62f){float u=smooth((q-2.62f)/.16f);gx=-.65f+1.3f*u;gy=1.30f;}
        if(q>3.82f){float u=smooth((q-3.82f)/.18f);gx=.65f-1.3f*u;gy=1.30f-.65f*u;}
        close=blink_at(phase,3.08f)+blink_at(phase,6.95f);
      }else if(action==ACTELL_AVATAR_WRITE){
        gy=.95f;gx=.45f;if(phase>2.3f)gx=shift(phase,2.3f,.45f,-.4f);if(phase>4.25f)gx=shift(phase,4.25f,-.4f,.45f);
        close=blink_at(phase,2.75f)+blink_at(phase,6.8f);
      }else if(m==MUSE_MODE_LISTENING){
        wide=.45f;gy=-.15f;if(phase>3.1f)gx=shift(phase,3.1f,0,.4f);if(phase>4.3f)gx=shift(phase,4.3f,.4f,0);close=blink_at(phase,4.85f);
      }else if(m==MUSE_MODE_THINKING){
        float q=fmodf(phase,8.0f/3.0f),hold=8.0f/6.0f-.18f;
        gx=q<hold?-.9f:q<hold+.18f?-.9f+1.8f*smooth((q-hold)/.18f):q<2*hold+.18f?.9f:.9f-1.8f*smooth((q-2*hold-.18f)/.18f);gy=-.95f;open=1.0f;
        close=0; /* Thinking keeps eyes open. */
        furrow=1; /* Persistent single-sided questioning brow. */
      }else if(m==MUSE_MODE_SPEAKING){
        gy=.35f-.5f*clamp(level/.2f);if(phase>2.7f)gx=shift(phase,2.7f,0,-.5f);if(phase>3.5f)gx=shift(phase,3.5f,-.5f,0);
        close=blink_at(phase,1.9f)+blink_at(phase,5.2f)+blink_at(phase,5.52f);
      }else{
        /* Look left, rest, back to viewer, right, rest, return. */
        if(phase>1.0f)gx=shift(phase,1.0f,0,-1.25f);
        if(phase>2.75f)gx=shift(phase,2.75f,-1.25f,0);
        if(phase>4.2f)gx=shift(phase,4.2f,0,1.25f);
        if(phase>6.25f)gx=shift(phase,6.25f,1.25f,0);
        close=blink_at(phase,3.3f)+blink_at(phase,6.9f);
      }
      open*=1-clamp(close);if(pet&&m!=MUSE_MODE_THINKING)open*=.45f;
      brow_closure=furrow;eye(cx-7,ey,open,gx,gy,wide);brow_closure=0;eye(cx+7,ey,open,gx,gy,wide);
      if(furrow>.08f){
        /* A short, gently slanted single brow. No permanent angry mask. */
        for(int i=0;i<13;i++){float xx=(cx-9.8f+.5f-.6f)*2.5f+i,yy=(ey-5.6f+.5f-.95f)*2.5f+nod_offset+.20f*i;native_pixel((int)lroundf(xx),(int)lroundf(yy),INK);native_pixel((int)lroundf(xx),(int)lroundf(yy)+1,INK);}
      }
    }
    float surprise=0;
    if(action==ACTELL_AVATAR_READ){surprise=smooth((phase-2.0f)/.22f)*(1-smooth((phase-4.0f)/.35f));}
    if(surprise>.02f)precise_oval(cx,ey+7,1.45f,.5f+1.4f*surprise,INK);
    else if(m==MUSE_MODE_SPEAKING) {
      if(!p->mouth_level_valid) {
        /* Existing callers keep the problem-1 level mapping. */
        if(level<=.05f) {
          int mx=(int)lroundf((cx+.5f)*2.5f);
          int my=(int)lroundf((ey+7.5f)*2.5f+nod_offset);
          for(int x=mx-4;x<=mx+4;x++)native_pixel(x,my,INK);
        } else precise_oval(cx,ey+7,1.8f,.65f+level*2.5f,INK);
      } else {
        /* Integer-height ellipse: every height 1..16 exists, including 2/4/etc.
         * No 1-line-to-minimum-oval gate jump at a raw audio threshold. */
        int mx=(int)lroundf((cx+.5f)*2.5f);
        int my=(int)lroundf((ey+7.5f)*2.5f+nod_offset);
        int h=1+(int)lroundf(mouth_level*15.0f);
        int top=my-h/2;
        for(int y=0;y<h;y++) {
          float ny=((float)y+.5f-(float)h*.5f)/((float)h*.5f);
          for(int x=-4;x<=4;x++) {
            float nx=(float)x/4.5f;
            if(nx*nx+ny*ny<=1.0f)native_pixel(mx+x,top+y,INK);
          }
        }
      }
    }
    else if(m==MUSE_MODE_LISTENING) precise_oval(cx,ey+7,1.7f,1.1f,INK);
    else if(m==MUSE_MODE_ERROR || m==MUSE_MODE_OFF) rect(cx-2,ey+7,5,1,INK);
    else {pixel(cx-2,ey+6,INK);rect(cx-1,ey+7,3,1,INK);pixel(cx+2,ey+6,INK);}
    rect(cx-12,ey+5,3,1,BLUSH);rect(cx+10,ey+5,3,1,BLUSH);

    nod_offset=nod;
    if(action) {
        int py=action==ACTELL_AVATAR_READ?39+bob:38+bob;
        int left=action==ACTELL_AVATAR_READ?25:27;
        int right=left+12;
        if(action==ACTELL_AVATAR_WRITE) {
            /* Simple small floating hand, like the speaking gesture. Its
             * small in/out swing can tuck behind the phone's right edge.
             * No arm, elbow, long forearm or multi-joint motion. */
            float q=fmodf(t,3.8f),tap=0;
            if(q>.65f&&q<1.05f)tap=sinf((q-.65f)/.4f*3.14159265f);
            if(q>1.45f&&q<1.85f)tap=sinf((q-1.45f)/.4f*3.14159265f);
            paw(right+3.5f-tap*2.5f,py+6+tap*.6f,2.8f);
        }
        /* Narrow portrait phone BACK only: 13 x 19 pixels, fully inside
         * the canvas. One flat matte shell and rear camera; no side rail,
         * highlight stripe, side button, display bezel or screen contents. */
        phone_round(left,py,13,19,2.0f,OUTLINE);
        phone_round(left+1,py+1,11,17,1.25f,EAR);
        rect(left+2,py+2,3,4,OUTLINE);pixel(left+3,py+3,BLUE);
        /* Holding paws wrap the near edges, separate from the far-side tapping
         * hand. Read uses BOTH hands; write keeps only its left holding hand. */
        outlined(left-1,py+8,3,3,LIGHT);
        if(action==ACTELL_AVATAR_READ) outlined(right,py+8,3,3,LIGHT);
    } else if(m==MUSE_MODE_LISTENING) {
        outlined(cx-19,hy+8,4,5,WHITE);
        int r=3+(int)(level*4);
        for(int k=0;k<r;k++){pixel(5+k/3,23-k,BLUE);pixel(58-k/3,23+k,BLUE);}
    } else if(m==MUSE_MODE_THINKING) {
        outlined(cx+6,hy+16,4,3,WHITE);
        for(int i=0;i<3;i++){int active=(int)(phase/1.25f)%3;float local=fmodf(phase,1.25f);float raised=i==active?(local<.15f?smooth(local/.15f):local>1.10f?1-smooth((local-1.10f)/.15f):1):0;precise_oval(42+i*7,4.0f-raised*1.1f,1.4f+raised*.3f,1.4f+raised*.3f,MINT);}
    } else if(m==MUSE_MODE_SPEAKING || m==MUSE_MODE_OFF || pet) {
        if(m==MUSE_MODE_OFF){int wave=roundi(sinf(t*8)*3);outlined(cx+21+wave/2,hy+9+wave,4,4,WHITE);}
        else{float q=fmodf(t,4.6f),wave=0;if(q>.6f&&q<2.1f)wave=sinf((q-.6f)/1.5f*3.14159265f);paw(cx+21+wave*.8f,hy+10-wave*3,3.7f);if(pet)paw(cx-21,hy+9-wave*2,3.7f);}
    }
    if(m==MUSE_MODE_BOOT) { for(int i=0;i<3;i++) if(mt>i*0.18f) rect(24+i*6,5,3,2,GOLD); }
    if(m==MUSE_MODE_ERROR) {rect(55,10,2,6,RED);rect(55,18,2,2,RED);}
    if(pet) {pixel(10,9,MINT);rect(9,10,3,1,MINT);pixel(10,11,MINT);pixel(53,7,GOLD);}
}
