/* SPDX-License-Identifier: Apache-2.0 */
#include "actell_image_stream.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char *load(const char *path,size_t *n){
 FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long size=ftell(f);assert(size>0);
 *n=(size_t)size;rewind(f);unsigned char *p=malloc(*n+1);assert(p);
 assert(fread(p,1,*n,f)==*n);p[*n]=0;fclose(f);return p;
}
int main(int argc,char **argv){
 assert(argc==4);size_t bytes,expected_size;
 unsigned char *jpeg=load(argv[1],&bytes),*expected=load(argv[2],&expected_size);
 const char *metadata="{\"method\":\"link.result\",\"payload\":{\"format\":\"jpeg-base64\",\"data_base64\":\"\",\"width\":2}}";
 actell_image_layout s;assert(actell_image_layout_init(&s,metadata,bytes,expected_size));
 assert(s.wire_length==expected_size);
 assert(!actell_image_layout_init(&s,metadata,bytes,expected_size-1));
 assert(!actell_image_layout_init(&s,metadata,0,expected_size));
 assert(!actell_image_layout_init(&s,"{\"data_base64\":\"x\"}",bytes,expected_size));
 assert(!actell_image_layout_init(&s,"{\"data_base64\":\"\",\"other\":{\"data_base64\":\"\"}}",bytes,expected_size));
 assert(actell_image_layout_init(&s,metadata,bytes,expected_size));
 size_t block=(size_t)strtoul(argv[3],NULL,10);assert(block&&block<=8192);
 unsigned char buffer[8192];size_t offset=0;
 while(offset<s.wire_length){
  size_t n=actell_image_stream_read(&s,metadata,jpeg,bytes,offset,buffer,block);
  assert(n&&n<=block&&n<=s.wire_length-offset);
  assert(!memcmp(buffer,expected+offset,n));offset+=n;
 }
 assert(!actell_image_stream_read(&s,metadata,jpeg,bytes,offset,buffer,block));
 /* A paused send can resume at any byte, including mid-base64 quartet. */
 for(size_t i=0;i<expected_size;i+=97){
  size_t n=actell_image_stream_read(&s,metadata,jpeg,bytes,i,buffer,17);
  assert(n&&n<=expected_size-i&&!memcmp(buffer,expected+i,n));
 }
 free(jpeg);free(expected);return 0;
}
