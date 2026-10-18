#include "muse_aa_partition.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static bool ready,mem=true,bind_ok=true;static int map_err,calls,unmaps;static char order[64];static unsigned pos;
static void event(char x){order[pos++]=x;order[pos]=0;}
bool muse_avatar_aa_ready(void){return ready;}
bool muse_avatar_aa_memory_ready(void){return mem;}
bool muse_avatar_aa_bind(const void*p,size_t n){event('B');if(!bind_ok)return false;ready=true;return true;}
void muse_avatar_aa_unbind(void){event('U');ready=false;}
void muse_pixel_render(const muse_pose_t*p){assert(!ready);event('R');}
esp_err_t esp_partition_mmap(const esp_partition_t*p,size_t o,size_t n,int type,const void**out,esp_partition_mmap_handle_t*h){calls++;event('M');if(map_err)return map_err;*out=(void*)(uintptr_t)0x400000;*h=17;return ESP_OK;}
void esp_partition_munmap(esp_partition_mmap_handle_t h){assert(h==17);unmaps++;event('X');}
int main(void){const size_t N=2269340;esp_partition_t p={1,0x10000,0x400000};muse_pose_t pose={0};
 assert(muse_aa_partition_close(NULL)==ESP_OK);
 assert(muse_aa_partition_open(NULL,0,N)==ESP_ERR_INVALID_ARG);
 p.type=0;assert(muse_aa_partition_open(&p,0,N)==ESP_ERR_INVALID_ARG);p.type=1;
 assert(muse_aa_partition_open(&p,1,N)==ESP_ERR_INVALID_ARG);
 assert(muse_aa_partition_open(&p,0,N-1)==ESP_ERR_INVALID_ARG);
 assert(muse_aa_partition_open(&p,SIZE_MAX-3,N)==ESP_ERR_INVALID_ARG);
 assert(muse_aa_partition_open(&p,p.size-4,N)==ESP_ERR_INVALID_ARG);assert(calls==0);
 mem=false;assert(muse_aa_partition_open(&p,0,N)==ESP_ERR_INVALID_STATE);assert(calls==0);mem=true;
 ready=true;assert(muse_aa_partition_open(&p,0,N)==ESP_ERR_INVALID_STATE);ready=false;
 map_err=ESP_ERR_NO_MEM;assert(muse_aa_partition_open(&p,0,N)==ESP_ERR_NO_MEM);assert(unmaps==0);map_err=0;
 bind_ok=false;assert(muse_aa_partition_open(&p,0,N)==ESP_ERR_INVALID_CRC);assert(unmaps==1&&!ready);bind_ok=true;
 pos=0;order[0]=0;assert(muse_aa_partition_open(&p,4,N)==ESP_OK);assert(strcmp(order,"MB")==0);int prev=calls;
 assert(muse_aa_partition_open(&p,4,N)==ESP_ERR_INVALID_STATE);assert(calls==prev);
 assert(muse_aa_partition_close(NULL)==ESP_ERR_INVALID_ARG);assert(ready&&unmaps==1);
 assert(muse_aa_partition_close(&pose)==ESP_OK);assert(strcmp(order,"MBURX")==0);assert(!ready&&unmaps==2);
 assert(muse_aa_partition_close(&pose)==ESP_OK&&unmaps==2);
 assert(muse_aa_partition_open(&p,0,N)==ESP_OK);assert(muse_aa_partition_close(&pose)==ESP_OK);
 puts("PASS adapter: invalid type/range/alignment/length, no overflow, PSRAM guard, mmap failure, bind failure cleanup, no replacement, close order unbind-render-unmap, idempotent close");}
