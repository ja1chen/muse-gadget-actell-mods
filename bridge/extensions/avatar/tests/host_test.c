/* SPDX-License-Identifier: Apache-2.0 */
#include "actell_avatar.h"
#include "muse_pixel.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <limits.h>
#include <math.h>
#include <string.h>
static uint64_t clock_ms;
uint64_t actell_avatar_test_clock_ms(void) { return clock_ms; }
void preview_clock(uint64_t value) { clock_ms=value; }
#ifndef AVATAR_SHARED
int main(void) {
    assert(!actell_avatar_enabled());
    actell_avatar_set_activity(ACTELL_AVATAR_READ,1500);
    assert(actell_avatar_activity()==ACTELL_AVATAR_NONE);
    actell_avatar_set_enabled(true);
    clock_ms=100;actell_avatar_set_activity(ACTELL_AVATAR_READ,1500);
    assert(actell_avatar_activity()==ACTELL_AVATAR_READ);
    clock_ms=1599;assert(actell_avatar_activity()==ACTELL_AVATAR_READ);
    clock_ms=1600;assert(actell_avatar_activity()==ACTELL_AVATAR_NONE);
    clock_ms=0x3ffffff0u;actell_avatar_set_activity(ACTELL_AVATAR_WRITE,100);
    clock_ms=0x40000020u;assert(actell_avatar_activity()==ACTELL_AVATAR_WRITE);
    clock_ms=0x40000054u;assert(actell_avatar_activity()==ACTELL_AVATAR_NONE);
    clock_ms=0xfffffff0u;actell_avatar_set_activity(ACTELL_AVATAR_READ,100);
    clock_ms=0x100000014ull;assert(actell_avatar_activity()==ACTELL_AVATAR_READ);
    clock_ms=0x100000054ull;assert(actell_avatar_activity()==ACTELL_AVATAR_NONE);
    actell_avatar_set_activity(ACTELL_AVATAR_WRITE,100);
    actell_avatar_set_enabled(false);actell_avatar_set_enabled(true);
    assert(actell_avatar_activity()==ACTELL_AVATAR_NONE);
    actell_avatar_set_activity((actell_avatar_activity_t)3,100);
    assert(actell_avatar_activity()==ACTELL_AVATAR_NONE);
    actell_avatar_set_activity(ACTELL_AVATAR_WRITE,0);
    assert(actell_avatar_activity()==ACTELL_AVATAR_NONE);
    static uint16_t pixels[514*514];
    for(int m=0;m<MUSE_MODE_COUNT;m++) for(int a=0;a<3;a++) for(int f=0;f<80;f++) {
        clock_ms=(uint32_t)f*40;actell_avatar_set_activity((actell_avatar_activity_t)a,1500);
        muse_pose_t pose={(muse_mode_t)m,f*.04f,f*.04f,(f%11)/10.0f,f%7==0?.8f:0};
        muse_pixel_render(&pose);
        const int sizes[]={1,63,64,65,320,512,900,-3};
        for(unsigned s=0;s<sizeof(sizes)/sizeof(sizes[0]);s++) {
            int n=sizes[s]<1?1:sizes[s]>512?512:sizes[s];
            muse_pixel_set_size(sizes[s]);
            pixels[0]=0xbeef; pixels[n*n+1]=0xcafe;
            muse_pixel_scale(pixels+1,n,0,n-1,0,n-1);
            assert(pixels[0]==0xbeef && pixels[n*n+1]==0xcafe);
            /* Non-origin strip, strided destination, matching LVGL contract. */
            if(n>8) muse_pixel_scale(pixels+1,n,3,n-2,5,n-1);
        }
    }
    static uint16_t base[4096], extra[4096];
    muse_pixel_set_size(64);
    for(int m=0;m<MUSE_MODE_COUNT;m++) {
        muse_pose_t p={(muse_mode_t)m,1,1,.5f,0};
        actell_avatar_set_enabled(false);muse_pixel_render(&p);
        muse_pixel_scale(base,64,0,63,0,63);
        actell_avatar_set_enabled(true);actell_avatar_set_activity(ACTELL_AVATAR_READ,1000);
        muse_pixel_render(&p);muse_pixel_scale(extra,64,0,63,0,63);
        assert((memcmp(base,extra,sizeof base)!=0)==(m==MUSE_MODE_IDLE || m==MUSE_MODE_THINKING));
        actell_avatar_set_activity(ACTELL_AVATAR_NONE,0);
        muse_pixel_render(&p);muse_pixel_scale(extra,64,0,63,0,63);
        assert(memcmp(base,extra,sizeof base)==0); /* no completion celebration */
    }
    muse_pose_t off={MUSE_MODE_OFF,2,2,0,0};
    muse_pixel_render(&off);muse_pixel_scale(base,64,0,63,0,63);
    for(int n=0;n<4096;n++) assert(base[n]==0);
    muse_pose_t bad={MUSE_MODE_IDLE,NAN,INFINITY,INFINITY,NAN};
    muse_pixel_render(&bad);
    bad.t=3.4e38f;bad.mode_t=-INFINITY;muse_pixel_render(&bad);
    muse_pixel_scale(pixels,INT_MAX,INT_MIN,INT_MAX,0,1);
    muse_pixel_scale(pixels,INT_MAX,0,1,0,1);
    clock_ms=100;actell_avatar_set_activity(ACTELL_AVATAR_READ,100);
    clock_ms=201;assert(actell_avatar_activity()==ACTELL_AVATAR_NONE);
    clock_ms=100+0x40000000u;assert(actell_avatar_activity()==ACTELL_AVATAR_NONE);
    puts("PASS: TTL, wraparound, invalid values, toggle, 1680 frames, 8 sizes, strips");
    return 0;
}
#endif
