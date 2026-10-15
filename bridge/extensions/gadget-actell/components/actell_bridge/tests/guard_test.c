#include "actell_guard.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
 actell_guard g={0};assert(!actell_guard_accept(&g,0,"a"));actell_guard_session(&g,5);
 assert(actell_guard_accept(&g,5,"invoke-1"));assert(!actell_guard_accept(&g,5,"invoke-1"));assert(!actell_guard_accept(&g,4,"other"));
 assert(actell_guard_finish(&g,5,"invoke-1"));assert(!actell_guard_finish(&g,5,"invoke-1"));
 actell_guard_session(&g,6);assert(!actell_guard_finish(&g,5,"invoke-1"));assert(actell_guard_accept(&g,6,"invoke-1"));
 for(unsigned i=1;i<ACTELL_HISTORY;i++){char id[32];snprintf(id,sizeof(id),"id-%u",i);assert(actell_guard_accept(&g,6,id));}
 assert(!actell_guard_accept(&g,6,"overflow"));actell_guard_session(&g,0);assert(!actell_guard_accept(&g,6,"old"));
 assert(!actell_id_valid(""));assert(!actell_id_valid("a/b"));char big[130];memset(big,'a',129);big[129]=0;assert(!actell_id_valid(big));
 assert(actell_op_valid("companion.hello"));assert(actell_op_valid("images.get"));assert(!actell_op_valid("device.unpair"));assert(actell_op_mutates("tools.call"));assert(!actell_op_mutates("calls.get"));
 unsigned w=0,h=0;unsigned char jpeg[]={255,216,255,192,0,8,8,0,2,0,3,0,255,217};
 assert(actell_jpeg_dimensions(jpeg,sizeof(jpeg),&w,&h)&&w==3&&h==2);
 jpeg[9]=255;assert(!actell_jpeg_dimensions(jpeg,sizeof(jpeg),&w,&h));
 assert(!actell_jpeg_dimensions(jpeg,3,&w,&h));
 puts("guard tests passed: generation fencing, exactly-once completion attempt, dedup budget, identity/op validation");return 0;
}
