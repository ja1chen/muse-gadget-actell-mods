#include "cJSON.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
static int event,updates;
static uint32_t lease;
void actell_avatar_set_enabled(bool enabled){assert(enabled);}
void actell_avatar_set_activity(int activity,uint32_t ttl){event=activity;lease=ttl;updates++;}
extern void actell_activity_observe(uint64_t,const char*,const cJSON*,const cJSON*);
static void observe(const char*op,const char*state){
 cJSON*p=cJSON_Parse("{\"tool\":\"tap\"}");
 cJSON*r=cJSON_Parse("{\"run_id\":\"run\",\"call_id\":\"call\"}");
 cJSON_AddStringToObject(r,"state",state);actell_activity_observe(1,op,p,r);cJSON_Delete(p);cJSON_Delete(r);
}
int main(void){
 observe("tools.call","pending_approval");assert(event==0);
 observe("calls.get","queued");assert(event==0);
 observe("calls.get","running");assert(event==2&&lease==1500);
 observe("calls.get","succeeded");assert(event==2&&lease==350);
 int n=updates;observe("calls.get","running");assert(updates==n);
 observe("tools.call","succeeded");assert(updates==n);
 cJSON *p=cJSON_Parse("{\"tool\":\"tap\"}"),*r=cJSON_Parse("{\"run_id\":\"newrun\",\"call_id\":\"newcall\",\"state\":\"running\"}");
 actell_activity_observe(2,"tools.call",p,r);assert(event==2);
 actell_activity_observe(2,"runs.cancel",p,r);assert(event==0);
 n=updates;actell_activity_observe(2,"calls.get",p,r);assert(updates==n);
 cJSON_Delete(p);cJSON_Delete(r);
 puts("activity test passed: approval/queue silence, verified events, terminal fence");return 0;
}
