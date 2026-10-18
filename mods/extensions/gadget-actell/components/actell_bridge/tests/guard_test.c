/* SPDX-License-Identifier: Apache-2.0 */
#include "actell_guard.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Synthetic distinct digests exercise guard policy, not the SHA-256 adapter. */
static actell_admission admit(actell_guard *g,uint64_t gen,const char *id,const char *op,unsigned n){
 unsigned char hash[32]={0};memcpy(hash,&n,sizeof(n));return actell_guard_admit(g,gen,id,op,hash);
}
int main(void){
 actell_guard *g=calloc(1,sizeof(*g));assert(g);
 assert(admit(g,0,"a","capabilities",0)==ACTELL_ADMIT_INVALID);actell_guard_session(g,5);
 assert(admit(g,5,"invoke-1","tools.call",1)==ACTELL_ADMIT_NEW);
 assert(admit(g,5,"invoke-1","tools.call",1)==ACTELL_ADMIT_PENDING);
 assert(admit(g,4,"other","capabilities",2)==ACTELL_ADMIT_INVALID);
 assert(actell_guard_finish(g,5,"invoke-1"));assert(!actell_guard_finish(g,5,"invoke-1"));
 assert(admit(g,5,"invoke-1","tools.call",1)==ACTELL_ADMIT_FINISHED);
 /* More than 64 finished reads never exhaust the work window or write fence. */
 for(unsigned i=2;i<4096;i++){char id[32];snprintf(id,sizeof(id),"read-%u",i);
  assert(admit(g,5,id,"calls.get",i)==ACTELL_ADMIT_NEW);assert(actell_guard_finish(g,5,id));}
 assert(g->write_used==1 && g->used==64);
 /* Old write fence survives all work-slot recycling and operation changes. */
 assert(admit(g,5,"invoke-1","capabilities",1)==ACTELL_ADMIT_FINISHED);
 assert(admit(g,5,"invoke-1","tools.call",1)==ACTELL_ADMIT_FINISHED);
 actell_guard_session(g,6);assert(!actell_guard_finish(g,5,"invoke-1"));
 for(unsigned i=0;i<ACTELL_HISTORY;i++){char id[32];snprintf(id,sizeof(id),"active-%u",i);assert(admit(g,6,id,"calls.get",i)==ACTELL_ADMIT_NEW);}
 assert(admit(g,6,"overflow","calls.get",9999)==ACTELL_ADMIT_BUSY);
 assert(actell_guard_finish(g,6,"active-0"));assert(admit(g,6,"after-finish","calls.get",9999)==ACTELL_ADMIT_NEW);
 actell_guard_session(g,7);
 for(unsigned i=0;i<ACTELL_WRITE_HISTORY;i++) {
  char id[32];snprintf(id,sizeof(id),"write-%u",i);
  const char *op=i<ACTELL_WRITE_HISTORY-ACTELL_STOP_RESERVE?"tools.call":"runs.cancel";
  assert(admit(g,7,id,op,i)==ACTELL_ADMIT_NEW);assert(actell_guard_finish(g,7,id));
  if(i==ACTELL_WRITE_HISTORY-ACTELL_STOP_RESERVE-1)
   assert(admit(g,7,"blocked-action","tools.call",9999)==ACTELL_ADMIT_WRITE_LIMIT);
 }
 assert(admit(g,7,"blocked-stop","runs.cancel",9999)==ACTELL_ADMIT_WRITE_LIMIT);
 assert(admit(g,7,"read-still-works","runs.get",9999)==ACTELL_ADMIT_NEW);
 actell_guard_session(g,7);assert(g->write_used==ACTELL_WRITE_HISTORY);
 actell_guard_session(g,8);assert(g->write_used==0);
 unsigned char hash[32]={0};unsigned n=15;memcpy(hash,&n,sizeof(n));
 assert(admit(g,8,"never-queued","tools.call",n)==ACTELL_ADMIT_NEW);
 actell_guard_unsent(g,8,"never-queued",hash);assert(g->used==0 && g->write_used==0);
 assert(admit(g,8,"never-queued","tools.call",n)==ACTELL_ADMIT_NEW);
 assert(actell_guard_finish(g,8,"never-queued"));
 actell_guard_unsent(g,8,"never-queued",hash);assert(g->used==1 && g->write_used==1);
 assert(!actell_id_valid(""));assert(!actell_id_valid("a/b"));char big[130];memset(big,'a',129);big[129]=0;assert(!actell_id_valid(big));
 assert(actell_op_valid("companion.hello"));assert(actell_op_valid("images.get"));assert(!actell_op_valid("device.unpair"));assert(actell_op_mutates("tools.call"));assert(!actell_op_mutates("calls.get"));
 unsigned w=0,h=0;unsigned char jpeg[]={255,216,255,192,0,8,8,0,2,0,3,0,255,217};
 assert(actell_jpeg_dimensions(jpeg,sizeof(jpeg),&w,&h)&&w==3&&h==2);jpeg[9]=255;assert(!actell_jpeg_dimensions(jpeg,sizeof(jpeg),&w,&h));assert(!actell_jpeg_dimensions(jpeg,3,&w,&h));
 free(g);puts("guard passed: terminal duplicates, 4094 reads, retained writes, cancel reserve, queue rollback, generation fencing");return 0;
}
