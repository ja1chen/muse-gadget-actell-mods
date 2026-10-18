from pathlib import Path
import re,struct,zlib,json,hashlib
A=Path(__file__).resolve().parents[1];R=A.parents[2]/'local/build/avatar-raw-aa/generator';P=R/'muse_pixel_aa_assets.h';s=P.read_text();f=(R/'assets/avatar-aa.raw').read_bytes();payload=bytearray(f[64:]);out=['/* Generated raw asset views; little-endian ESP32/host. No pixel array embedded. */','#pragma once','#include <stdint.h>','typedef struct {uint32_t offset,length;int16_t x,y;uint16_t w,h;} aa_record_t;'];dims={}
for typ,name,dim,body in re.findall(r'static const (aa_record_t|uint8_t|uint16_t) (\w+)(.*?) = \{(.*?)\};',s,re.S):
 while len(payload)%4:payload.append(0)
 offset=64+len(payload)
 vals=list(map(int,re.findall(r'-?\d+',body)))
 if typ=='aa_record_t':
  for i in range(0,len(vals),6):payload.extend(struct.pack('<IIhhHH',*vals[i:i+6]))
  count=len(vals)//6;out.append(f'#define {name} ((const aa_record_t*)(aa_file+{offset}u))')
 else:
  count=len(vals);payload.extend(struct.pack('<'+('B' if typ=='uint8_t' else 'H')*count,*vals)); second=re.search(r'\[\]\[(\d+)\]',dim)
  if second:
   width=int(second[1]);count//=width;out.append(f'#define {name} ((const {typ}(*)[{width}])(aa_file+{offset}u))')
  else:out.append(f'#define {name} ((const {typ}*)(aa_file+{offset}u))')
 out.append(f'#define {name.upper()}_COUNT {count}u');dims[name]={'offset':offset,'count':count}
crc=zlib.crc32(payload);header=struct.pack('<8sIIII',b'ACTAA001',1,64,len(payload),crc)+bytes(40)
out+= [f'#define AA_RAW_BYTES {len(payload)}u',f'#define AA_RAW_CRC32 0x{crc:08x}u',f'#define AA_PIXEL_BYTES {len(f)-64}u']
P.write_text('\n'.join(out)+'\n');(R/'assets/avatar-aa.raw').write_bytes(header+payload)
b=json.loads((R/'asset-budget.json').read_text());b.update({'raw_file_bytes':len(payload)+64,'pixel_bytes':len(f)-64,'metadata_bytes':len(payload)-(len(f)-64),'crc32':f'{crc:08x}','sha256':hashlib.sha256(header+payload).hexdigest(),'views':dims});(R/'asset-budget.json').write_text(json.dumps(b,indent=2))
