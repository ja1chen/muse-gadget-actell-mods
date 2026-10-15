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
 static const char *ops[]={"companion.hello","capabilities","runs.open","runs.get","runs.cancel","tools.call","calls.get","images.get"};

 if (!op) return false;

 for(unsigned i=0;
i<sizeof(ops)/sizeof(*ops);
i++) if(!strcmp(op,ops[i]))return true;

 return false;

}
bool actell_op_mutates(const char *op) { return op && (!strcmp(op,"tools.call")||!strcmp(op,"runs.open")||!strcmp(op,"runs.cancel"));
 }
void actell_guard_session(actell_guard *g,uint64_t generation){ if(g->generation!=generation){memset(g,0,sizeof(*g));
g->generation=generation;
} }
bool actell_guard_accept(actell_guard*g,uint64_t generation,const char*id){
 if(!generation||generation!=g->generation||!actell_id_valid(id)||g->used>=ACTELL_HISTORY)return false;

 for(unsigned i=0;
i<g->used;
i++)if(!strcmp(g->receipts[i].id,id))return false;

 strcpy(g->receipts[g->used++].id,id);
return true;

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
