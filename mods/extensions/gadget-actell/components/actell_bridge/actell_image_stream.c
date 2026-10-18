/* SPDX-License-Identifier: Apache-2.0 */
#include "actell_image_stream.h"
#include <stdint.h>
#include <string.h>
bool actell_image_layout_init(actell_image_layout *s,const char *metadata,size_t bytes,size_t limit){
 if(!s)return false;
 memset(s,0,sizeof(*s));
 if(!metadata||!bytes||bytes>(SIZE_MAX-2)/4*3)return false;
 const char *needle="\"data_base64\":\"\"",*p=strstr(metadata,needle);
 if(!p||strstr(p+strlen(needle),needle))return false;
 size_t n=strlen(metadata),encoded=4*((bytes+2)/3);
 if(n>limit||encoded>limit-n)return false;
 s->prefix=(size_t)(p-metadata)+strlen(needle)-1;
 s->metadata_length=n;s->encoded_length=encoded;s->wire_length=n+encoded;
 return true;
}
size_t actell_image_stream_read(const actell_image_layout *s,const char *metadata,
 const unsigned char *jpeg,size_t bytes,size_t offset,unsigned char *out,size_t capacity){
 static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
 if(!s||!metadata||!jpeg||!out||!bytes||offset>=s->wire_length)return 0;
 size_t done=0;
 while(done<capacity&&offset<s->wire_length){
  if(offset<s->prefix){
   size_t n=s->prefix-offset;if(n>capacity-done)n=capacity-done;
   memcpy(out+done,metadata+offset,n);done+=n;offset+=n;
  }else if(offset-s->prefix<s->encoded_length){
   size_t pos=offset-s->prefix,index=(pos/4)*3,part=pos%4;
   if(index>=bytes)return 0;
   unsigned a=jpeg[index],b=index+1<bytes?jpeg[index+1]:0,c=index+2<bytes?jpeg[index+2]:0;
   unsigned char group[4]={(unsigned char)alphabet[a>>2],(unsigned char)alphabet[((a&3)<<4)|(b>>4)],
    index+1<bytes?(unsigned char)alphabet[((b&15)<<2)|(c>>6)]:'=',index+2<bytes?(unsigned char)alphabet[c&63]:'='};
   size_t n=4-part;if(n>capacity-done)n=capacity-done;
   memcpy(out+done,group+part,n);done+=n;offset+=n;
  }else{
   size_t at=offset-s->encoded_length,n=s->metadata_length-at;
   if(n>capacity-done)n=capacity-done;
   memcpy(out+done,metadata+at,n);done+=n;offset+=n;
  }
 }
 return done;
}
