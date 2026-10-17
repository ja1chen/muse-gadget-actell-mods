#!/usr/bin/env python3
"""Validate actual raw binding, procedural equivalence and bounded AA rendering."""
from pathlib import Path
import argparse, ctypes as C, hashlib, json, os, random, re, shlex, struct, subprocess, zlib
A=Path(__file__).resolve().parents[1];ROOT=A.parents[2]
p=argparse.ArgumentParser();p.add_argument('--include',type=Path,required=True);p.add_argument('--raw',type=Path,required=True)
p.add_argument('--output',type=Path,default=ROOT/'local/validation/avatar-raw-aa');a=p.parse_args();a.output=a.output.resolve()
if not a.output.is_relative_to(ROOT/'local'):p.error('Output must remain local')
a.output.mkdir(parents=True,exist_ok=True)
data=a.raw.read_bytes();spec=json.loads((A/'source-manifest.json').read_text())['raw_aa_update']
assert len(data)==spec['raw_bytes'] and hashlib.sha256(data).hexdigest()==spec['raw_sha256']
assert data[:8]==b'ACTAA001' and struct.unpack_from('<III',data,8)==(1,64,len(data)-64)
assert zlib.crc32(data[64:])==struct.unpack_from('<I',data,20)[0]
views=(A/'src/muse_pixel_aa_assets.h').read_text()
record_offset=int(re.search(r'aa_records.*aa_file\+(\d+)u',views)[1]);record_count=int(re.search(r'AA_RECORDS_COUNT (\d+)u',views)[1]);pixel_bytes=int(re.search(r'AA_PIXEL_BYTES (\d+)u',views)[1])
head_offset=int(re.search(r'aa_head_ids.*aa_file\+(\d+)u',views)[1]);head_count=int(re.search(r'AA_HEAD_IDS_COUNT (\d+)u',views)[1]);heads=set(struct.unpack_from('<'+'H'*head_count,data,head_offset))
for i in range(record_count):
    offset,length,x,y,w,h=struct.unpack_from('<IIhhHH',data,record_offset+i*16)
    assert w and h and offset<=pixel_bytes and length<=pixel_bytes-offset and length==w*h*(2 if i in heads else 1)
    if i not in heads:assert all((v&15)+(v>>4)<=15 for v in data[64+offset:64+offset+length])
class Pose(C.Structure):
    _fields_=[('mode',C.c_int),('t',C.c_float),('mode_t',C.c_float),('level',C.c_float),('happy',C.c_float),('mouth_level',C.c_float),('mouth_level_valid',C.c_bool)]
libs={}
for name,source in [('geometry',A/'src/muse_pixel_geometry.c'),('raw',A/'src/muse_pixel_160.c')]:
    out=a.output/(name+'.so')
    subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c11','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC','-DACTELL_AVATAR_TEST','-DAVATAR_SHARED','-I'+str(a.include),'-I'+str(A/'include'),str(source),str(A/'src/actell_avatar.c'),str(A/'tests/host_test.c'),'-lm','-o',str(out)],check=True)
    lib=C.CDLL(str(out));lib.muse_pixel_render.argtypes=[C.POINTER(Pose)];lib.muse_pixel_scale.argtypes=[C.POINTER(C.c_uint16)]+[C.c_int]*5
    lib.preview_clock.argtypes=[C.c_uint64];lib.actell_avatar_set_enabled.argtypes=[C.c_bool];lib.actell_avatar_set_activity.argtypes=[C.c_int,C.c_uint32]
    lib.muse_pixel_set_size(160);lib.actell_avatar_set_enabled(True);libs[name]=lib
raw=libs['raw'];raw.muse_avatar_aa_bind.argtypes=[C.c_void_p,C.c_size_t];raw.muse_avatar_aa_bind.restype=C.c_bool;raw.muse_avatar_aa_ready.restype=C.c_bool
mapped=C.create_string_buffer(data);buf=(C.c_uint16*25600)()
def frame(lib,pose,action=0):
    lib.preview_clock(0);lib.actell_avatar_set_activity(action,10000);lib.muse_pixel_render(C.byref(pose));lib.muse_pixel_scale(buf,160,0,159,0,159);return bytes(buf)
rng=random.Random(23)
for i in range(2000):
    pose=Pose(rng.randrange(7),rng.random()*120,rng.random()*3,rng.random(),rng.random(),rng.random(),bool(i%2));action=rng.randrange(3)
    assert frame(raw,pose,action)==frame(libs['geometry'],pose,action),i
assert raw.muse_avatar_aa_bind(mapped,len(data))
for mode in range(9):
    for k in range(240):frame(raw,Pose(mode if mode<7 else 1,k*.037,k*.037,(k%101)/100,0,(k%101)/100,True),mode-6 if mode>=7 else 0)
assert raw.muse_avatar_aa_errors()==0
for n in [0,1,63,len(data)-1,len(data)+1]:assert not raw.muse_avatar_aa_bind(mapped,n) and raw.muse_avatar_aa_ready()
corrupt=C.create_string_buffer(data);corrupt[1024]=bytes([data[1024]^1]);assert not raw.muse_avatar_aa_bind(corrupt,len(data)) and raw.muse_avatar_aa_ready()
for i in range(100):
    raw.muse_avatar_aa_unbind();pose=Pose(4,1,1,.2,0,.4,True);assert frame(raw,pose)==frame(libs['geometry'],pose)
    assert raw.muse_avatar_aa_bind(mapped,len(data));frame(raw,pose)
# ASan/UBSan covers bound rendering and strips at the same sizes as the standard harness.
wrapper=a.output/'bound-sanitizer.c';binary=a.output/'bound-sanitizer'
wrapper.write_text('#define main unbound_harness_main\n#include "'+str(A/'tests/host_test.c')+'"\n#undef main\n#include "muse_pixel_aa_resource.h"\n#include <stdlib.h>\nint main(int argc,char **argv){assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);void *p=malloc('+str(len(data))+');assert(p);assert(fread(p,1,'+str(len(data))+',f)=='+str(len(data))+');fclose(f);assert(muse_avatar_aa_bind(p,'+str(len(data))+'));int result=unbound_harness_main();muse_avatar_aa_unbind();free(p);return result;}\n')
subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c11','-O2','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-DCONFIG_MUSE_AVATAR_V2=1','-DACTELL_AVATAR_TEST','-I'+str(a.include),'-I'+str(A/'include'),'-I'+str(A/'src'),str(A/'src/muse_pixel_160.c'),str(A/'src/actell_avatar.c'),str(wrapper),'-lm','-o',str(binary)],check=True)
subprocess.run([str(binary),str(a.raw)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
# Production memory guard: missing external-BSS configuration must fail compilation;
# an ESP-interface mock rejects binding and emits black when PSRAM addresses fail.
fake=a.output/'esp-fakes';fake.mkdir(exist_ok=True)
section='__DATA,__aa_psram' if __import__('sys').platform=='darwin' else '.ext_ram.bss'
(fake/'esp_attr.h').write_text('#define EXT_RAM_BSS_ATTR __attribute__((section("'+section+'")))\n')
(fake/'esp_memory_utils.h').write_text('#include <stdbool.h>\nbool esp_ptr_external_ram(const void *);\n')
config='#define CONFIG_SPIRAM 1\n#define CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY 1\n#define CONFIG_MUSE_AVATAR_V2 1\n'
(fake/'sdkconfig.h').write_text(config.replace('CONFIG_SPIRAM 1','CONFIG_SPIRAM 0'))
common=shlex.split(os.environ.get('CC','cc'))+['-std=c11','-O2','-DESP_PLATFORM','-DACTELL_AVATAR_TEST','-DAVATAR_SHARED','-I'+str(fake),'-I'+str(A/'tests/esp_fakes'),'-I'+str(a.include),'-I'+str(A/'include')]
refused=subprocess.run(common+['-fsyntax-only',str(A/'src/muse_pixel_160.c')],capture_output=True)
assert refused.returncode and b'refusing internal-RAM buffers' in refused.stderr
(fake/'sdkconfig.h').write_text(config)
memory=fake/'memory.c';memory.write_text('#include <stdbool.h>\nstatic bool ready;\nvoid mock_external_ready(bool r){ready=r;}\nbool esp_ptr_external_ram(const void *p){return p && ready;}\n')
mocklib=a.output/'esp-memory.so'
subprocess.run(common+['-shared','-fPIC',str(A/'src/muse_pixel_160.c'),str(A/'src/actell_avatar.c'),str(A/'tests/host_test.c'),str(memory),'-lm','-o',str(mocklib)],check=True)
mock=C.CDLL(str(mocklib));mock.muse_avatar_aa_bind.argtypes=[C.c_void_p,C.c_size_t];mock.muse_avatar_aa_bind.restype=C.c_bool
mock.muse_avatar_aa_memory_ready.restype=C.c_bool;mock.mock_external_ready.argtypes=[C.c_bool];mock.muse_pixel_render.argtypes=[C.POINTER(Pose)];mock.muse_pixel_scale.argtypes=[C.POINTER(C.c_uint16)]+[C.c_int]*5
mock.muse_pixel_set_size(160);pose=Pose(4,1,1,.5,0,.4,True)
assert not mock.muse_avatar_aa_memory_ready() and not mock.muse_avatar_aa_bind(mapped,len(data))
assert not any(frame(mock,pose))
mock.mock_external_ready(True);assert mock.muse_avatar_aa_memory_ready() and mock.muse_avatar_aa_bind(mapped,len(data));assert any(frame(mock,pose))
mock.mock_external_ready(False);assert not any(frame(mock,pose)) and not mock.muse_avatar_aa_bind(mapped,len(data))
adapter=a.output/'partition-test'
subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c11','-O2','-Wall','-Werror','-I'+str(A/'tests/aa_partition_stubs'),'-I'+str(A/'src'),'-I'+str(a.include),str(A/'src/muse_aa_partition.c'),str(A/'tests/aa_partition_test.c'),'-o',str(adapter)],check=True)
subprocess.run([str(adapter)],check=True)
report=dict(raw_record_bounds_checked=record_count,partition_lifecycle_and_error_tests=True,psram_configuration_required=True,bad_psram_rejects_bind_and_blanks=True,fallback_exact_frames=2000,aa_frames=2160,rebind_cycles=100,missing_masks=raw.muse_avatar_aa_errors(),corrupt_bind_preserves_previous=True,bound_sanitizers=True,physical_device_verified=False)
(a.output/'raw.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
