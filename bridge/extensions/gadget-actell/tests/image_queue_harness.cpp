/* SPDX-License-Identifier: Apache-2.0 */
#include "actell_image_stream.h"
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#define CONFIG_ACTELL_BRIDGE 1
#define pdTRUE 1
#define pdMS_TO_TICKS(n) (n)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
using noise_ctrl_session_generation_t=uint64_t;
struct cJSON {int unused;};
static unsigned deleted=0,image_freed=0;
static unsigned char *tracked_image;
static bool allocation_fail=false,queue_full=false;
static void *s_result_q=(void*)1;
static void cJSON_Delete(cJSON *p){if(p){deleted++;std::free(p);}}
static void release(void *p){
 if(p&&p==tracked_image){image_freed++;tracked_image=nullptr;}
 std::free(p);
}
static char *duplicate(const char *s){return allocation_fail?nullptr: ::strdup(s);}
#define free release
#define strdup duplicate
// ACTUAL_PENDING_RESULT
static pending_result received;
static int xQueueSend(void *,const void *item,unsigned){
 if(queue_full)return 0;received=*(const pending_result*)item;return 1;
}
// ACTUAL_QUEUE_RESULT
static void submit(uint64_t generation,const char *id){
 cJSON *result=(cJSON*)malloc(sizeof(cJSON));assert(result);
 tracked_image=(unsigned char*)malloc(133150);assert(tracked_image);
 queue_result(generation,id,result,{tracked_image,133150});
}
int main(){
 submit(0,"synthetic");assert(deleted==1&&image_freed==1);
 submit(1,nullptr);assert(deleted==2&&image_freed==2);
 s_result_q=nullptr;submit(1,"synthetic");assert(deleted==3&&image_freed==3);s_result_q=(void*)1;
 allocation_fail=true;submit(1,"synthetic");assert(deleted==4&&image_freed==4);allocation_fail=false;
 queue_full=true;submit(1,"synthetic");assert(deleted==5&&image_freed==5);queue_full=false;
 submit(1,"synthetic");assert(deleted==5&&image_freed==5&&received.image.data==tracked_image);
 /* Simulate the SDK's stale-session/disconnect drain after ownership transfer. */
 free(received.request_id);cJSON_Delete(received.result);free(received.image.data);
 assert(deleted==6&&image_freed==6);return 0;
}
