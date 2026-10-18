#!/usr/bin/env python3
"""Desktop-only 8x8 geometry coverage generator. Requires numpy; C compiler for head."""
from pathlib import Path
import ctypes as C,subprocess,json,hashlib,struct,zlib
import numpy as np
import argparse, shutil
A=Path(__file__).resolve().parents[1];R=A.parents[2]/'local/build/avatar-raw-aa/generator';V=R/'validation';V.mkdir(parents=True,exist_ok=True);(R/'assets').mkdir(exist_ok=True)
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--include',type=Path,help='staged Muse component containing muse_pixel.h and muse_state.h');args=parser.parse_args()
if args.include:
 (R/'include').mkdir(exist_ok=True)
 for name in ['muse_pixel.h','muse_state.h']:shutil.copyfile(args.include/name,R/'include'/name)
if not all((R/'include'/name).is_file() for name in ['muse_pixel.h','muse_state.h']):parser.error('Provide --include for the staged SDK component')
s=(A/'src/muse_pixel_geometry.c').read_text()
s=s.replace('static void native_pixel(', 'static float sample_x=.5f,sample_y=.5f;\nstatic void native_pixel(')
for a,b in [('x+.5f-X','x+sample_x-X'),('y+.5f-Y','y+sample_y-Y'),('i+.5f-X','i+sample_x-X'),('j+.5f-Y','j+sample_y-Y')]:s=s.replace(a,b)
s+='''\nvoid offline_head(float p,float hx,float hy,float sx,float sy,uint32_t *out){
 sample_x=sx;sample_y=sy;pitch=p;nod_offset=2*p;cache_valid=false;memset(fb,0,sizeof fb);
 head_and_ears(31+hx/2.5f,28+hy/2.5f);
 for(int i=0;i<W*H;i++)out[i]=palette[fb[i]];
}\n'''
(V/'head_generator.c').write_text(s)
subprocess.run(['cc','-O2','-shared','-fPIC','-DACTELL_AVATAR_TEST','-DAVATAR_SHARED','-I'+str(R/'include'),'-I'+str(A/'include'),str(V/'head_generator.c'),str(A/'src/actell_avatar.c'),str(A/'tests/host_test.c'),'-lm','-o',str(V/'head_generator.so')],check=True)
l=C.CDLL(str(V/'head_generator.so'));l.offline_head.argtypes=[C.c_float]*5+[C.POINTER(C.c_uint32)]
b=(C.c_uint32*25600)();N=8
blob=bytearray();records=[]

def add(a,wide=False,key=0):
 yy,xx=np.where(a!=0)
 if len(xx):x0,x1,y0,y1=int(xx.min()),int(xx.max()+1),int(yy.min()),int(yy.max()+1);crop=a[y0:y1,x0:x1]
 else:x0=y0=0;x1=y1=1;crop=a[:1,:1]
 data=crop.astype("<u2" if wide else "u1").tobytes();off=len(blob);blob.extend(data);rec=[off,len(data),x0,y0,x1-x0,y1-y0,key];records.append(rec);return len(records)-1
head_ids=[]
for k in range(68):
 p=k/64 if k<=64 else 0;hx=.5 if k in (65,67) else 0;hy=.5 if k in (66,67) else 0
 total=np.zeros((160,160,3),dtype=np.float64)
 for sy in range(N):
  for sx in range(N):
   l.offline_head(p,hx,hy,(sx+.5)/N,(sy+.5)/N,b);v=np.ctypeslib.as_array(b).reshape(160,160)
   total+=np.stack([(v>>16)&255,(v>>8)&255,v&255],axis=-1)
 rgb=np.rint(total/(N*N)).astype(np.uint32);v=((rgb[:,:,0]*31+127)//255)<<11|((rgb[:,:,1]*63+127)//255)<<5|((rgb[:,:,2]*31+127)//255)
 head_ids.append(add(v.astype(np.uint16),True));print('head',k,flush=True)
# All dynamic ellipse radius combinations from the C model, quantized to quarter-pixel.
def q(x):return int(np.floor(x*4+.5))
pairs=set()
for rx,ry in [(4.25,2.75),(11.25,13.75),(8.75,11.25),(11.25,8.75),(8.75,6.25),(11.25,11.25),(8.75,8.75),(6.25,6.25),(8.375,8.375),(7,7),(10.625,10.625),(9.25,9.25)]:pairs.add((q(rx),q(ry)))
for ry in range(q(1.625),q(7.875)+1):pairs.add((q(4.5),ry))
for ry in range(q(1.25),q(4.75)+1):pairs.add((q(3.625),ry))
for radius in range(q(3.5),q(4.25)+1):pairs.add((radius,radius))
ellipse_pairs=sorted(pairs);ellipse_ids=[]
# Cell origin for mask: center lies at rxceil + fraction; output rec retains cropped offset.
def ellipse(rx,ry,fx,fy,eye=False,op=1):
 rx/=4;ry/=4;ox=int(np.ceil(rx))+1;oy=int(np.ceil(ry))+1;w=2*ox+2;h=2*oy+2
 yy,xx=np.mgrid[0:h,0:w];cov=np.zeros((h,w));white=np.zeros((h,w))
 for sy in range(N):
  for sx in range(N):
   dx=xx+(sx+.5)/N-ox-fx/4;dy=yy+(sy+.5)/N-oy-fy/4
   inside=dx*dx/rx**2+dy*dy/ry**2<=1
   if eye:inside&=(abs(dy)<=ry*max(.08,op))
   hi=inside&(dx>=-1.7)&(dx<=.3)&(dy>=-3.2)&(dy<=-1.2)&(op>.6) if eye else np.zeros_like(inside)
   cov+=inside;white+=hi
 allalpha=np.rint(cov*15/(N*N)).astype(np.uint8);wa=np.rint(white*15/(N*N)).astype(np.uint8)
 value=(allalpha-wa)|(wa<<4) if eye else allalpha
 return value,ox,oy
for rx,ry in ellipse_pairs:
 row=[]
 for fy in range(4):
  for fx in range(4):
   a,ox,oy=ellipse(rx,ry,fx,fy);idx=add(a);records[idx][2]-=ox;records[idx][3]-=oy;row.append(idx)
 ellipse_ids.append(row)
# Eye radius 6.25, ry 7.5..10.0, openness 0..1 in 1/32 increments.
eye_ids=[]
for ry in range(30,41):
 for op in range(33):
  row=[]
  for fy in range(4):
   for fx in range(4):
    a,ox,oy=ellipse(25,ry,fx,fy,True,op/32);idx=add(a);records[idx][2]-=ox;records[idx][3]-=oy;row.append(idx)
  eye_ids.append(row)
# Tapered rounded phone masks, original exact local-space shape.
phone_ids=[]
for w,h,r in [(32.5,47.5,5),(27.5,42.5,3.125)]:
 row=[]
 for fy in range(4):
  for fx in range(4):
   yy,xx=np.mgrid[0:int(np.ceil(h))+2,0:int(np.ceil(w))+2];cov=np.zeros_like(xx,dtype=float)
   for sy in range(N):
    for sx in range(N):
     py=yy+(sy+.5)/N-fy/4;rawpx=xx+(sx+.5)/N-fx/4;px=(rawpx-w*.5)/(1-.14*(py/h))+w*.5
     dx=np.maximum(np.maximum(r-px,px-(w-r)),0);dy=np.maximum(np.maximum(r-py,py-(h-r)),0)
     cov+=(dx*dx+dy*dy<=r*r)&(py>=0)&(py<h)&(rawpx>=0)&(rawpx<w)
   row.append(add(np.rint(cov*15/(N*N)).astype(np.uint8)))
 phone_ids.append(row)
# Fixed original thinking brow, continuous strip gives antialiased slant.
brow_ids=[]
for fy in range(4):
 for fx in range(4):
  yy,xx=np.mgrid[0:7,0:16];cov=np.zeros_like(xx,dtype=float)
  for sy in range(N):
   for sx in range(N):
    x=xx+(sx+.5)/N-fx/4;y=yy+(sy+.5)/N-fy/4;cov+=(x>=0)&(x<13)&(y>=.2*x)&(y<.2*x+2)
  brow_ids.append(add(np.rint(cov*15/(N*N)).astype(np.uint8)))
# Stable mouth masks, same 1..16 integer heights as current observer renderer.
mouth_ids=[]
for h in range(1,17):
 yy,xx=np.mgrid[0:h,-4:5];cov=np.zeros((h,9))
 if h==1:cov[:]=64
 else:
  for sy in range(8):
   for sx in range(8):
    nx=(xx+(sx+.5)/8-.5)/4.5;ny=(yy+(sy+.5)/8-h*.5)/(h*.5);cov+=(nx*nx+ny*ny<=1)
 data=np.rint(cov*15/64).astype('uint8').tobytes();mouth_ids.append(len(records));records.append([len(blob),len(data),-4,0,9,h,0]);blob.extend(data)
crc=zlib.crc32(blob);header=struct.pack('<8sIIII',b'ACTAA001',1,64,len(blob),crc)+bytes(40)
(R/'assets/avatar-aa.raw').write_bytes(header+blob)
out=['/* SPDX-License-Identifier: Apache-2.0; generated offline, do not hand-edit. */','#pragma once','#include <stdint.h>', 'typedef struct { uint32_t offset,length; int16_t x,y; uint16_t w,h; } aa_record_t;']
out.append('static const aa_record_t aa_records[] = {\n'+',\n'.join('{'+','.join(map(str,r[:6]))+'}' for r in records)+'\n};')
def arr(name,rows):out.append('static const uint16_t '+name+'[][16] = {\n'+',\n'.join('{'+','.join(map(str,r))+'}' for r in rows)+'\n};')
out.append('static const uint16_t aa_head_ids[] = {'+','.join(map(str,head_ids))+'};')
out.append('static const uint8_t aa_ellipse_sizes[][2] = {'+','.join('{%s,%s}'%p for p in ellipse_pairs)+'};')
arr('aa_ellipse_ids',ellipse_ids);arr('aa_eye_ids',eye_ids);arr('aa_phone_ids',phone_ids);out.append('static const uint16_t aa_brow_ids[] = {'+','.join(map(str,brow_ids))+'};')
out.append('static const uint16_t aa_mouth_ids[] = {'+','.join(map(str,mouth_ids))+'};')
p=R/'muse_pixel_aa_assets.h';p.write_text('\n'.join(out)+'\n')
meta={'samples_per_axis':N,'head_poses':65,'half_pixel_static_variants':3,'ellipse_shapes':len(pairs),'eye_shapes':len(eye_ids),'records':len(records),'blob_bytes':len(blob),'metadata_bytes':len(records)*16+len(head_ids)*2+len(pairs)*34+len(eye_ids)*32+64+32,'header_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'compressed':False,'scope':'head, ears, eyes, ellipse mouth/paws/dots, phone, thinking brow; axis-aligned block accents retain intentional pixel shapes','pose_pitch_error_bound':1/128,'center_radius_step_pixels':.25,'openness_step':1/32}
(R/'asset-budget.json').write_text(json.dumps(meta,indent=2));print(json.dumps(meta,indent=2))
