/* SPDX-License-Identifier: Apache-2.0 */
#include "actell_guard.h"
#include <string.h>
bool actell_id_valid(const char *s) {
 if (!s || !*s) return false;

 unsigned n=0;
 for (;
*s;
s++,n++) if (n>=ACTELL_ID_MAX || !((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||(*s>='0'&&*s<='9')||*s=='_'||*s=='-')) return false;

 return true;

}
bool actell_op_valid(const char *op) {
 static const char *ops[]={"companion.hello","capabilities","runs.open","runs.get","runs.cancel","tools.call","calls.get","images.get","actell.screenshot","actell.tap","actell.drag","actell.home","actell.list_apps","actell.launch_app","actell.type_text","actell.wait","actell.call_status"};

 if (!op) return false;

 for(unsigned i=0;
i<sizeof(ops)/sizeof(*ops);
i++) if(!strcmp(op,ops[i]))return true;

 return false;

}
bool actell_op_mutates(const char *op) { return op && ((!strncmp(op,"actell.",7)&&strcmp(op,"actell.call_status")&&strcmp(op,"actell.wait"))||!strcmp(op,"tools.call")||!strcmp(op,"runs.open")||!strcmp(op,"runs.cancel"));
 }
void actell_guard_session(actell_guard *g,uint64_t generation){ if(g->generation!=generation){memset(g,0,sizeof(*g));
g->generation=generation;
} }
actell_admission actell_guard_admit(actell_guard *g,uint64_t generation,const char *id,
                                    const char *op,const unsigned char digest[ACTELL_ID_DIGEST]) {
 if(!generation || generation!=g->generation || !actell_id_valid(id) || !actell_op_valid(op) || !digest)
     return ACTELL_ADMIT_INVALID;
 for(unsigned i=0;i<g->used;i++)
     if(!strcmp(g->receipts[i].id,id))
         return g->receipts[i].finished ? ACTELL_ADMIT_FINISHED : ACTELL_ADMIT_PENDING;
 for(unsigned i=0;i<g->write_used;i++)
     if(!memcmp(g->write_ids[i],digest,ACTELL_ID_DIGEST))return ACTELL_ADMIT_FINISHED;
 bool mutates=actell_op_mutates(op);
 unsigned limit=!strcmp(op,"runs.cancel") ? ACTELL_WRITE_HISTORY : ACTELL_WRITE_HISTORY-ACTELL_STOP_RESERVE;
 if(mutates && g->write_used>=limit)return ACTELL_ADMIT_WRITE_LIMIT;
 unsigned slot=g->used;
 if(slot==ACTELL_HISTORY) {
     for(unsigned i=0;i<ACTELL_HISTORY;i++) {
         unsigned candidate=(g->next_slot+i)%ACTELL_HISTORY;
         if(g->receipts[candidate].finished) {slot=candidate;break;}
     }
     if(slot==ACTELL_HISTORY)return ACTELL_ADMIT_BUSY;
 } else g->used++;
 g->next_slot=(slot+1)%ACTELL_HISTORY;
 memset(&g->receipts[slot],0,sizeof(g->receipts[slot]));
 strcpy(g->receipts[slot].id,id);
 g->receipts[slot].mutates=mutates;
 if(mutates)memcpy(g->write_ids[g->write_used++],digest,ACTELL_ID_DIGEST);
 return ACTELL_ADMIT_NEW;
}
void actell_guard_unsent(actell_guard *g,uint64_t generation,const char *id,
                         const unsigned char digest[ACTELL_ID_DIGEST]) {
 if(!generation || generation!=g->generation || !id || !digest)return;
 for(unsigned i=0;i<g->used;i++) {
     actell_receipt *r=&g->receipts[i];
     if(strcmp(r->id,id) || r->finished)continue;
     if(r->mutates) {
         for(unsigned j=0;j<g->write_used;j++)if(!memcmp(g->write_ids[j],digest,ACTELL_ID_DIGEST)) {
             --g->write_used;
             memcpy(g->write_ids[j],g->write_ids[g->write_used],ACTELL_ID_DIGEST);
             memset(g->write_ids[g->write_used],0,ACTELL_ID_DIGEST);
             break;
         }
     }
     --g->used;
     g->receipts[i]=g->receipts[g->used];
     memset(&g->receipts[g->used],0,sizeof(g->receipts[g->used]));
     return;
 }
}
bool actell_guard_finish(actell_guard*g,uint64_t generation,const char*id){
 if(generation!=g->generation)return false;

 for(unsigned i=0;
i<g->used;
i++)if(!strcmp(g->receipts[i].id,id)){if(g->receipts[i].finished)return false;
g->receipts[i].finished=true;
return true;
}
 return false;

}
bool actell_jpeg_dimensions(const unsigned char*d,size_t n,unsigned*w,unsigned*h){
 if(!d||n<4||d[0]!=255||d[1]!=216||d[n-2]!=255||d[n-1]!=217)return false;

 size_t p=2;

 while(p+4<=n){
  if(d[p++]!=255)return false;

  while(p<n&&d[p]==255)p++;

  if(p>=n)return false;

  unsigned marker=d[p++];
if(marker==0xda||marker==0xd9)return false;

  if(p+2>n)return false;

  size_t len=((size_t)d[p]<<8)|d[p+1];
if(len<2||len>n-p)return false;

  if(marker==0xc0||marker==0xc1||marker==0xc2){
   if(len<8)return false;

   *h=((unsigned)d[p+3]<<8)|d[p+4];
*w=((unsigned)d[p+5]<<8)|d[p+6];

   return *w>=2&&*h>=2&&*w<=1280&&*h<=1280;

  }
  p+=len;

 }
 return false;

}
