/* SPDX-License-Identifier: Apache-2.0 */
#include "sdkconfig.h"
#include "actell_guard.h"
#include "cJSON.h"
#include <string.h>
#if CONFIG_ACTELL_BRIDGE_AVATAR_ACTIVITY
/* The approved avatar exports these allocation-free, nonblocking setters.
 * Weak references keep absence of the optional renderer failure-isolated. */
extern void actell_avatar_set_enabled(bool enabled) __attribute__((weak));
extern void actell_avatar_set_activity(int activity, uint32_t ttl_ms) __attribute__((weak));
typedef struct { char run[129],call[129]; int activity; bool terminal; } tracked;
static tracked calls[16];
static uint64_t last_generation;
static unsigned count,cancel_count;
static char cancelled_runs[8][129];
static bool exhausted;
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
#endif
/* Called only by the one bridge worker after authenticated identity validation.
 * Never wait, allocate, perform I/O, or let presentation change RPC outcomes. */
void actell_activity_observe(uint64_t generation,const char*op,const cJSON*params,const cJSON*result) {
#if CONFIG_ACTELL_BRIDGE_AVATAR_ACTIVITY
    if(!actell_avatar_set_enabled||!actell_avatar_set_activity)return;
    if(generation!=last_generation) {
        memset(calls,0,sizeof(calls));memset(cancelled_runs,0,sizeof(cancelled_runs));count=0;cancel_count=0;exhausted=false;last_generation=generation;
        actell_avatar_set_enabled(true);actell_avatar_set_activity(0,0);
    }
    if(!strcmp(op,"runs.cancel")) {
        const char *run=text(result,"run_id");
        if(actell_id_valid(run)) {
            bool known=false;for(unsigned i=0;i<cancel_count;i++)if(!strcmp(cancelled_runs[i],run))known=true;
            if(!known){if(cancel_count<8)strcpy(cancelled_runs[cancel_count++],run);else exhausted=true;}
            for(unsigned i=0;i<count;i++)if(!strcmp(calls[i].run,run))calls[i].terminal=true;
        }
        actell_avatar_set_activity(0,0);return;
    }
    if(strcmp(op,"tools.call")&&strcmp(op,"calls.get"))return;
    const char*run=text(result,"run_id"),*call=text(result,"call_id"),*state=text(result,"state");
    if(exhausted||!actell_id_valid(run)||!actell_id_valid(call)||!state)return;
    for(unsigned i=0;i<cancel_count;i++)if(!strcmp(cancelled_runs[i],run))return;
    unsigned index=0;
    for(;index<count;index++)if(!strcmp(calls[index].run,run)&&!strcmp(calls[index].call,call))break;
    if(index==count) {
        int activity=!strcmp(op,"tools.call")?classify(text(params,"tool")):0;
        if(!activity||count==16)return;
        strcpy(calls[count].run,run);strcpy(calls[count].call,call);calls[count++].activity=activity;
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
