#!/usr/bin/env python3
"""Check quiet-speaking closure against the exact rounded/shaded source baseline.

Reconstructs the prior source by undoing only the reviewed mouth branch and
checks its published digest. Generated libraries/baseline/report stay local.
"""
import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import random
import shlex
import struct
import subprocess

A=Path(__file__).resolve().parents[1]
ROOT=A.parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--include',type=Path,required=True)
p.add_argument('--output',type=Path,default=ROOT/'local/validation/avatar-mouth-close')
a=p.parse_args();a.output=a.output.resolve()
if not a.output.is_relative_to(ROOT/'local'):p.error('Generated data must remain under local/')
a.output.mkdir(parents=True,exist_ok=True)
source=A/'src/muse_pixel_160.c'
s=source.read_text()
start=s.index('    else if(m==MUSE_MODE_SPEAKING) {',s.index('    float surprise=0;'))
end=s.index('    else if(m==MUSE_MODE_LISTENING)',start)
previous=s[:start]+'    else if(m==MUSE_MODE_SPEAKING) precise_oval(cx,ey+7,1.8f,.65f+level*2.5f,INK);\n'+s[end:]
previous=previous.replace('    float mouth_level=p->mouth_level_valid ? (isfinite(p->mouth_level)?clamp(p->mouth_level):0) : level;\n','')
assert hashlib.sha256(previous.encode()).hexdigest()=='acb4f40ed2d99b8c1f259c37d1ded1b810d7dc10ab374a0576b5c50e1402c3d2'
assert hashlib.sha256(source.read_bytes()).hexdigest()==json.loads((A/'source-manifest.json').read_text())['sha256']['src/muse_pixel_160.c']
baseline=a.output/'baseline.c';baseline.write_text(previous)
prior_closure=previous.replace('    else if(m==MUSE_MODE_SPEAKING) precise_oval(cx,ey+7,1.8f,.65f+level*2.5f,INK);\n','    else if(m==MUSE_MODE_SPEAKING) {\n      /* Closure only: leave the upstream level envelope and open mouth intact.\n       * pose.level <= .05 closes to a one-native-pixel line (160-grid).\n       * Input normalization above maps non-finite values to zero. */\n      if(level<=.05f) {\n        int mx=(int)lroundf((cx+.5f)*2.5f);\n        int my=(int)lroundf((ey+7.5f)*2.5f+nod_offset);\n        for(int x=mx-4;x<=mx+4;x++)native_pixel(x,my,INK);\n      } else precise_oval(cx,ey+7,1.8f,.65f+level*2.5f,INK);\n    }\n')
assert hashlib.sha256(prior_closure.encode()).hexdigest()=='33ba3c099bcd6ad3a611b376069dd915014bb73c143ef8b57ee2cd8f0186529b'
prior=a.output/'prior-closure.c';prior.write_text(prior_closure)
class Pose(C.Structure):
    _fields_=[('mode',C.c_int),('t',C.c_float),('mode_t',C.c_float),('level',C.c_float),('happy',C.c_float),('mouth_level',C.c_float),('mouth_level_valid',C.c_bool)]
libs={}
for name,src in [('baseline',baseline),('prior',prior),('candidate',source)]:
    out=a.output/(name+'.so')
    subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c11','-O2','-shared','-fPIC','-DACTELL_AVATAR_TEST','-DAVATAR_SHARED',
        '-I'+str(a.include),'-I'+str(A/'include'),str(src),str(A/'src/actell_avatar.c'),str(A/'tests/host_test.c'),'-lm','-o',str(out)],check=True)
    lib=C.CDLL(str(out));lib.muse_pixel_render.argtypes=[C.POINTER(Pose)]
    lib.muse_pixel_scale.argtypes=[C.POINTER(C.c_uint16)]+[C.c_int]*5
    lib.preview_clock.argtypes=[C.c_uint64];lib.actell_avatar_set_enabled.argtypes=[C.c_bool]
    lib.actell_avatar_set_activity.argtypes=[C.c_int,C.c_uint32]
    lib.muse_pixel_set_size(160);lib.actell_avatar_set_enabled(True);libs[name]=lib
buf=(C.c_uint16*25600)()
def frame(lib,pose,action=0):
    lib.preview_clock(0);lib.actell_avatar_set_activity(action,10000)
    lib.muse_pixel_render(C.byref(pose));lib.muse_pixel_scale(buf,160,0,159,0,159);return bytes(buf)
def speaking(name,level):return frame(libs[name],Pose(4,1,1,level,0))
ink=((0x28>>3)<<11)|((0x31>>2)<<5)|(0x42>>3)
def mouth(data):
    pixels=(C.c_uint16*25600).from_buffer_copy(data)
    xy=[(x,y) for y in range(82,104) for x in range(70,88) if pixels[y*160+x]==ink]
    assert xy
    return dict(width=max(x for x,y in xy)-min(x for x,y in xy)+1,height=max(y for x,y in xy)-min(y for x,y in xy)+1,pixels=len(xy))
threshold_bits=struct.unpack('<I',struct.pack('<f',.05))[0]
below=struct.unpack('<f',struct.pack('<I',threshold_bits-1))[0]
above=struct.unpack('<f',struct.pack('<I',threshold_bits+1))[0]
levels=[0,.01,below,.05,above,.050001,.1,.5,1]
measurements={str(v):{name:mouth(speaking(name,v)) for name in libs} for v in levels}
for level in [0,.01,below,.05]:assert mouth(speaking('candidate',level))==dict(width=9,height=1,pixels=9)
for level in [above,.050001,.1,.5,1]:assert speaking('candidate',level)==speaking('baseline',level)
for level in [float('nan'),float('inf'),float('-inf'),-1]:assert speaking('candidate',level)==speaking('candidate',0)
assert speaking('candidate',2)==speaking('candidate',1)
rng=random.Random(31)
for i in range(3000):
    pose=Pose(rng.choice([0,1,2,3,5,6]),rng.random()*120,rng.random()*3,rng.random(),rng.random(),rng.random(),True);action=rng.randrange(3)
    assert frame(libs['baseline'],pose,action)==frame(libs['candidate'],pose,action),('non-speaking',i)
for i in range(1000):
    pose=Pose(4,rng.random()*120,rng.random()*3,.05001+rng.random()*.94999,rng.random())
    assert frame(libs['baseline'],pose)==frame(libs['candidate'],pose),('speaking',i)
# Every low-level change is confined to the fixed native mouth region.
for i in range(200):
    pose=Pose(4,rng.random()*120,rng.random()*3,rng.random()*.05,rng.random())
    old=bytearray(frame(libs['baseline'],pose));new=bytearray(frame(libs['candidate'],pose))
    assert mouth(new)==dict(width=9,height=1,pixels=9)
    for y in range(82,104):
        lo=(y*160+70)*2;hi=(y*160+88)*2;old[lo:hi]=new[lo:hi]
    assert old==new,('outside-mouth',i)
for i in range(1000):
    pose=Pose(4,rng.random()*120,rng.random()*3,rng.random(),rng.random(),rng.random(),False)
    assert frame(libs['prior'],pose)==frame(libs['candidate'],pose),('invalid-channel-fallback',i)
# The independent channel affects only the mouth; omitted valid flag uses level.
for i in range(1000):
    pose=Pose(4,rng.random()*120,rng.random()*3,rng.random(),rng.random(),rng.random(),True)
    independent=bytearray(frame(libs['candidate'],pose))
    unchanged=bytearray(frame(libs['candidate'],Pose(pose.mode,pose.t,pose.mode_t,pose.level,pose.happy)))
    for y in range(82,104):
        lo=(y*160+70)*2;hi=(y*160+88)*2
        independent[lo:hi]=unchanged[lo:hi]
    assert independent==unchanged,('independent-outside-mouth',i)
for value in [float('nan'),float('inf'),-1,0]:
    assert frame(libs['candidate'],Pose(4,1,1,1,0,value,True))==frame(libs['candidate'],Pose(4,1,1,1,0,0,True))
heights=[]
for i in range(16):
    data=frame(libs['candidate'],Pose(4,1,1,1,0,i/15,True))
    heights.append(mouth(data)['height'])
assert heights==list(range(1,17)),heights
assert frame(libs['candidate'],Pose(4,1,1,1,0,2,True))==frame(libs['candidate'],Pose(4,1,1,1,0,1,True))
result=dict(representable_mouth_heights=heights,independent_channel_frames=1000,invalid_channel_fallback_frames=1000,levels=measurements,non_speaking_exact_frames=3000,speaking_above_threshold_exact_frames=1000,
            quiet_frames_unchanged_outside_mouth=200,nonfinite_normalization_passed=True,
            source_change_confined_to_speaking_mouth=True,ui_audio_envelope_changed=False,
            physical_board_verified=False,flashed=False)
(a.output/'mouth.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result))
