/* SPDX-License-Identifier: Apache-2.0 */
#include "sdkconfig.h"
#include "actell_guard.h"
#include "cJSON.h"
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#else
#define EXT_RAM_BSS_ATTR
#endif
#if CONFIG_ACTELL_BRIDGE_AVATAR_ACTIVITY
/* The approved avatar exports these allocation-free, nonblocking setters.
 * Weak references keep absence of the optional renderer failure-isolated. */
extern void actell_avatar_set_enabled(bool enabled) __attribute__((weak));
extern void actell_avatar_set_activity(int activity, uint32_t ttl_ms) __attribute__((weak));
typedef struct { char run[129],call[129]; int activity; bool terminal; } tracked;
/* Worker-only presentation history does not need internal/DMA-capable RAM. */
static EXT_RAM_BSS_ATTR tracked calls[16];
static uint64_t last_generation;
static unsigned count,cancel_count;
static EXT_RAM_BSS_ATTR char cancelled_runs[8][129];
static int job_activity;
static bool job_open;
static const char *text(const cJSON*j,const char*k) {
    const cJSON*v=cJSON_GetObjectItemCaseSensitive(j,k);
    return cJSON_IsString(v)?v->valuestring:NULL;
}
static int classify(const char *tool) {
    if(!tool)return 0;
    if(!strcmp(tool,"screenshot")||!strcmp(tool,"list_apps"))return 1;
    if(!strcmp(tool,"tap")||!strcmp(tool,"drag")||!strcmp(tool,"home")||!strcmp(tool,"launch_app")||!strcmp(tool,"type_text"))return 2;
    return 0;
}
static void reset_generation(uint64_t generation){
    if(generation==last_generation)return;
    memset(calls,0,sizeof(calls));memset(cancelled_runs,0,sizeof(cancelled_runs));
    count=0;cancel_count=0;job_activity=0;job_open=false;last_generation=generation;
    actell_avatar_set_enabled(true);actell_avatar_set_activity(0,0);
}
#endif
/* One serialized worker owns these presentation-only leases. They never
 * imply phone execution/approval and never add a delay to the command. */
void actell_activity_begin(uint64_t generation,const char *command,uint32_t ttl_ms){
#if CONFIG_ACTELL_BRIDGE_AVATAR_ACTIVITY
    if(!actell_avatar_set_enabled||!actell_avatar_set_activity)return;
    reset_generation(generation);
    job_open=true;
    job_activity=command&&!strncmp(command,"actell.",7)?classify(command+7):0;
    actell_avatar_set_activity(job_activity,job_activity?ttl_ms:0);
#else
    (void)generation;(void)command;(void)ttl_ms;
#endif
}
void actell_activity_end(uint64_t generation,bool success){
#if CONFIG_ACTELL_BRIDGE_AVATAR_ACTIVITY
    if(!actell_avatar_set_activity||generation!=last_generation||!job_open)return;
    int activity=job_activity;job_activity=0;job_open=false;
    actell_avatar_set_activity(success?activity:0,success&&activity?350:0);
#else
    (void)generation;(void)success;
#endif
}
/* Called only by the one bridge worker after authenticated identity validation.
 * Never wait, allocate, perform I/O, or let presentation change RPC outcomes. */
void actell_activity_observe(uint64_t generation,const char*op,const cJSON*params,const cJSON*result) {
#if CONFIG_ACTELL_BRIDGE_AVATAR_ACTIVITY
    if(!actell_avatar_set_enabled||!actell_avatar_set_activity)return;
    reset_generation(generation);
    /* Internal cancel/terminal receipts must not erase an in-flight screenshot
     * while its retained image is downloading. The outer job owns the pose. */
    if(job_open)return;
    if(!strcmp(op,"runs.cancel")) {
        const char *run=text(result,"run_id");
        if(actell_id_valid(run)) {
            bool known=false;for(unsigned i=0;i<cancel_count;i++)if(!strcmp(cancelled_runs[i],run))known=true;
            if(!known){if(cancel_count<8)strcpy(cancelled_runs[cancel_count++],run);else {memmove(cancelled_runs,cancelled_runs+1,7*sizeof(cancelled_runs[0]));strcpy(cancelled_runs[7],run);}}
            for(unsigned i=0;i<count;i++)if(!strcmp(calls[i].run,run))calls[i].terminal=true;
        }
        actell_avatar_set_activity(0,0);return;
    }
    if(strcmp(op,"tools.call")&&strcmp(op,"calls.get"))return;
    const char*run=text(result,"run_id"),*call=text(result,"call_id"),*state=text(result,"state");
    if(!actell_id_valid(run)||!actell_id_valid(call)||!state)return;
    for(unsigned i=0;i<cancel_count;i++)if(!strcmp(cancelled_runs[i],run))return;
    unsigned index=0;
    for(;index<count;index++)if(!strcmp(calls[index].run,run)&&!strcmp(calls[index].call,call))break;
    if(index==count) {
        int activity=!strcmp(op,"tools.call")?classify(text(params,"tool")):0;
        if(!activity)return;
        if(count==16){
            for(index=0;index<count&&!calls[index].terminal;index++);
            if(index==count)return;
        }else index=count++;
        memset(&calls[index],0,sizeof(calls[index]));
        strcpy(calls[index].run,run);strcpy(calls[index].call,call);calls[index].activity=activity;
    }
    tracked *item=&calls[index];if(item->terminal)return;
    if(!strcmp(state,"running"))actell_avatar_set_activity(item->activity,1500);
    else if(!strcmp(state,"succeeded")) {
        item->terminal=true;actell_avatar_set_activity(item->activity,350);
    } else {
        actell_avatar_set_activity(0,0);
        if(strcmp(state,"pending_approval")&&strcmp(state,"queued"))item->terminal=true;
    }
#else
    (void)generation;(void)op;(void)params;(void)result;
#endif
}
