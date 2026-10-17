/* Actual worker presentation + receipt observer; no network or phone. */
#include "actell_bridge.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static bool enabled;
static int activity;
static unsigned ttl,updates;
void actell_avatar_set_enabled(bool on){enabled=on;}
void actell_avatar_set_activity(int value,uint32_t ms){activity=value;ttl=ms;updates++;}
void actell_activity_observe(uint64_t,const char*,const cJSON*,const cJSON*);
void actell_activity_begin(uint64_t,const char*,uint32_t);
void actell_activity_end(uint64_t,bool);
static void observe(uint64_t gen,const char *run,const char *op,const char *call,const char *state,const char *tool){
 cJSON *p=cJSON_CreateObject(),*r=cJSON_CreateObject();
 cJSON_AddStringToObject(p,"tool",tool);
 cJSON_AddStringToObject(r,"run_id",run);cJSON_AddStringToObject(r,"call_id",call);cJSON_AddStringToObject(r,"state",state);
 actell_activity_observe(gen,op,p,r);cJSON_Delete(p);cJSON_Delete(r);
}
int main(void){
 observe(1,"run","capabilities","read","succeeded","list_apps");assert(enabled&&!activity);
 observe(1,"run","tools.call","read","queued","screenshot");assert(!activity);
 observe(1,"run","calls.get","read","running","");assert(activity==1&&ttl==1500);
 observe(1,"run","calls.get","read","succeeded","");assert(activity==1&&ttl==350);
 unsigned n=updates;observe(1,"run","calls.get","read","succeeded","");assert(updates==n);
 /* Ordinary cleanup receipts cannot erase the active outer tool's pose. */
 actell_activity_begin(2,"actell.screenshot",40000);assert(activity==1&&ttl==40000);
 n=updates;
 observe(2,"run","tools.call","capture","queued","screenshot");
 observe(2,"run","calls.get","capture","succeeded","");
 observe(2,"run","runs.cancel","capture","cancelled","");
 assert(activity==1&&ttl==40000&&updates==n);
 /* Capturing, run cleanup and download share the same bounded display lease. */
 observe(2,"run","images.get","capture","succeeded","");assert(activity==1&&ttl==40000);
 actell_activity_end(2,true);assert(activity==1&&ttl==350);
 actell_activity_begin(2,"actell.tap",39900);assert(activity==2&&ttl==39900);
 actell_activity_end(1,false);assert(activity==2); /* stale end cannot clear a new generation */
 actell_activity_end(2,false);assert(!activity&&!ttl);
 actell_activity_begin(2,"actell.wait",40000);assert(!activity);actell_activity_end(2,true);assert(!activity);
 actell_activity_begin(2,"actell.call_status",40000);assert(!activity);actell_activity_end(2,true);assert(!activity);
 for(unsigned i=0;i<100;i++){
  actell_activity_begin(2,i%2?"actell.type_text":"actell.list_apps",40000);
  assert(activity==(i%2?2:1));actell_activity_end(2,true);
 }
 /* Standalone receipt tracking also reuses terminal slots and cancel history. */
 for(unsigned i=0;i<64;i++){
  char run[32],call[32];snprintf(run,sizeof(run),"run-%u",i);snprintf(call,sizeof(call),"call-%u",i);
  observe(3,run,"tools.call",call,"running","tap");assert(activity==2);
  observe(3,run,"calls.get",call,"succeeded","");assert(activity==2&&ttl==350);
  observe(3,run,"runs.cancel",call,"cancelled","");assert(!activity);
  n=updates;observe(3,run,"calls.get",call,"running","");assert(updates==n);
 }
 puts("activity: outer lifecycle covers queue/network/download, cleanup cannot erase pose, 100 jobs and 64 legacy runs, stale end and query silence passed");
 return 0;
}
