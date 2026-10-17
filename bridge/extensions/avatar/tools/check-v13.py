#!/usr/bin/env python3
"""Compare unchanged v13 behavior and dirty sampling; quiet mouth has its own oracle."""
from pathlib import Path
import argparse, subprocess as sp, ctypes as C, random, json, os, shlex
A=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--include',type=Path,required=True);p.add_argument('--output',type=Path,required=True);args=p.parse_args()
V=args.output;V.mkdir(parents=True,exist_ok=True)
common=shlex.split(os.environ.get('CC','clang'))+['-O2','-DACTELL_AVATAR_TEST','-DMUSE_AVATAR_ROUNDED_FACE=0','-DMUSE_AVATAR_SHADING=0','-I'+str(args.include),'-I'+str(A/'include')]
def run(cmd): return sp.run(list(map(str,cmd)),check=True,capture_output=True,text=True)
# Shared exact v13 reference and staged implementation expose the same API.
for label,src in [('reference',A/'tests/reference_v13.c'),('new',A/'src/muse_pixel_160.c'),('old',A/'src/muse_pixel.c')]:
 run([*common,'-shared','-fPIC','-DAVATAR_SHARED',src,A/'src/actell_avatar.c',A/'tests/host_test.c','-lm','-o',V/f'{label}.so'])
class Pose(C.Structure):_fields_=[('mode',C.c_int),('t',C.c_float),('mode_t',C.c_float),('level',C.c_float),('happy',C.c_float),('mouth_level',C.c_float),('mouth_level_valid',C.c_bool)]
libs=[C.CDLL(str(V/f'{x}.so')) for x in ['reference','new']]
for lib in libs:
 lib.preview_clock.argtypes=[C.c_uint64];lib.actell_avatar_set_enabled(True)
 lib.muse_pixel_render.argtypes=[C.POINTER(Pose)];lib.muse_pixel_scale.argtypes=[C.POINTER(C.c_uint16),C.c_int,C.c_int,C.c_int,C.c_int,C.c_int]
arrs=[(C.c_uint16*(320*320))() for _ in libs];rng=random.Random(13);count=0
# Every frame in all 9 preview states, including cached repeats, fade and nod.
for mode,action in [(i,0) for i in range(7)]+[(1,1),(1,2)]:
 for f in range(200):
  # Quiet speaking is an intentional change, separately checked against the shaded baseline.
  pose=Pose(mode,f*.04,f*.04,max((f%11)/10,.050001) if mode==4 else (f%11)/10,0)
  for lib,buf in zip(libs,arrs):
   lib.preview_clock(0);lib.actell_avatar_set_activity(action,10000);lib.muse_pixel_set_size(320);lib.muse_pixel_render(C.byref(pose));lib.muse_pixel_scale(buf,320,0,319,0,319)
  assert bytes(arrs[0])==bytes(arrs[1]),(mode,action,f);count+=1
# Random cross-state transitions plus non-origin strips at multiple sizes.
for i in range(600):
 p=Pose(rng.randrange(7),rng.random()*130,rng.random()*5,rng.random(),rng.choice([0,.8]));act=rng.randrange(3);n=rng.choice([1,63,64,128,159,160,161,320]);x=rng.randrange(n);y=rng.randrange(n)
 if p.mode==4:p.level=max(p.level,.050001)
 for lib,buf in zip(libs,arrs):
  lib.preview_clock(0);lib.actell_avatar_set_activity(act,10000);lib.muse_pixel_set_size(n);lib.muse_pixel_render(C.byref(p));C.memset(buf,0,C.sizeof(buf));lib.muse_pixel_scale(buf,n,x,n-1,y,n-1)
 assert bytes(arrs[0])==bytes(arrs[1]),('random',i);count+=1
# Dirty-grid cache comparison: any changed screen pixel must map to changed grid cell.
lib=libs[1];prev=None;dirty_checks=0
for f in range(200):
 lib.actell_avatar_set_activity(0,0);lib.muse_pixel_set_size(320);lib.muse_pixel_render(C.byref(Pose(2,f*.04,f*.04,.3,0)));lib.muse_pixel_scale(arrs[1],320,0,319,0,319);now=list(arrs[1]);grid=[now[y*2*320+x*2] for y in range(160) for x in range(160)]
 if prev:
  pg,ps=prev
  for y in range(320):
   for x in range(320):
    if now[y*320+x]!=ps[y*320+x]:assert grid[(y//2)*160+x//2]!=pg[(y//2)*160+x//2]
  dirty_checks+=1
 prev=(grid,now)
result={'exact_pixel_cases':count,'dirty_frame_pairs':dirty_checks,'physical_board_verified':False}
(V/'pixel-report.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
