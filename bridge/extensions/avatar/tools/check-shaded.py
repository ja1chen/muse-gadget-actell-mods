#!/usr/bin/env python3
"""Check rounded/shaded prefilter equivalence, exact v13 fallback and cache cadence.

Uses the calling host suite's hash-verified SDK headers. No devices or services.
"""
import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import random
import shlex
import subprocess

A = Path(__file__).resolve().parents[1]
ROOT = A.parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--include', type=Path, required=True)
p.add_argument('--output', type=Path, default=ROOT/'local/validation/avatar-shaded')
a = p.parse_args()
a.output = a.output.resolve()
if not a.output.is_relative_to(ROOT/'local'):
    p.error('Generated libraries and reports must remain under local/')
a.output.mkdir(parents=True, exist_ok=True)
source = A/'src/muse_pixel_160.c'
assert hashlib.sha256(source.read_bytes()).hexdigest() == 'acb4f40ed2d99b8c1f259c37d1ded1b810d7dc10ab374a0576b5c50e1402c3d2'

class Pose(C.Structure):
    _fields_ = [('mode', C.c_int), ('t', C.c_float), ('mode_t', C.c_float),
                ('level', C.c_float), ('happy', C.c_float)]


def compile_lib(name, flags=(), src=source):
    target = a.output/(name+'.so')
    command = shlex.split(os.environ.get('CC', 'cc')) + [
        '-std=c11', '-O2', '-shared', '-fPIC', '-DACTELL_AVATAR_TEST', '-DAVATAR_SHARED',
        '-I'+str(a.include), '-I'+str(A/'include'), *flags,
        str(src), str(A/'src/actell_avatar.c'), str(A/'tests/host_test.c'), '-lm', '-o', str(target)]
    subprocess.run(command, check=True)
    lib = C.CDLL(str(target))
    lib.muse_pixel_render.argtypes = [C.POINTER(Pose)]
    lib.muse_pixel_scale.argtypes = [C.POINTER(C.c_uint16)] + [C.c_int]*5
    lib.preview_clock.argtypes = [C.c_uint64]
    lib.actell_avatar_set_enabled.argtypes = [C.c_bool]
    lib.actell_avatar_set_activity.argtypes = [C.c_int, C.c_uint32]
    lib.actell_avatar_set_enabled(True)
    lib.muse_pixel_set_size(160)
    return lib


libs = {
    'static': compile_lib('static'),
    'static_exact': compile_lib('static-exact', ['-DMUSE_AVATAR_EXACT_DISTANCE_PREFILTER=0']),
    'dynamic': compile_lib('dynamic', ['-DMUSE_AVATAR_DYNAMIC_LIGHT=1']),
    'dynamic_exact': compile_lib('dynamic-exact', ['-DMUSE_AVATAR_DYNAMIC_LIGHT=1', '-DMUSE_AVATAR_EXACT_DISTANCE_PREFILTER=0']),
    'v13_fallback': compile_lib('v13-fallback', ['-DMUSE_AVATAR_SHADING=0', '-DMUSE_AVATAR_ROUNDED_FACE=0']),
    'reference': compile_lib('reference', src=A/'tests/reference_v13.c'),
}
pairs = [('static', 'static_exact'), ('dynamic', 'dynamic_exact'), ('v13_fallback', 'reference')]
buf = (C.c_uint16*25600)()


def render(lib, pose, action):
    lib.preview_clock(0)
    lib.actell_avatar_set_activity(action, 10000)
    lib.muse_pixel_render(C.byref(pose))
    lib.muse_pixel_scale(buf, 160, 0, 159, 0, 159)
    return bytes(buf)


rng = random.Random(17)
for i in range(6000):
    pose = Pose(2 if i < 4000 else rng.randrange(7),
                i*.003 if i < 4000 else rng.random()*120,
                rng.random()*5, rng.random(), rng.random())
    action = rng.randrange(3)
    for left, right in pairs:
        assert render(libs[left], pose, action) == render(libs[right], pose, action), (i, left, right)
# Count cache rebuilds separately from production timing.
s = source.read_text()
anchor = ' float X=(cx+.5f)*2.5f,Y=(hy+.5f)*2.5f+nod_offset;'
assert s.count('static bool cache_valid;') == 1 and s.count(anchor) == 1
s = s.replace('static bool cache_valid;', 'static bool cache_valid;\nunsigned shaded_cache_misses;')
s = s.replace(anchor, ' shaded_cache_misses++;\n'+anchor)
instrumented = a.output/'cache-counted.c'
instrumented.write_text(s)
lib = compile_lib('cache-counted', src=instrumented)
lib.actell_avatar_set_activity(0, 0)
for i in range(200):
    lib.muse_pixel_render(C.byref(Pose(2, i*.04, i*.04, .5, 0)))
misses = C.c_uint.in_dll(lib, 'shaded_cache_misses').value
assert misses == 74, misses
result = dict(exact_comparison_cases=18000, cases_per_pair=6000, pairs=pairs,
              cache_misses_per_200_listening_frames=misses, offline_aa_enabled=False,
              host_only=True, physical_board_verified=False,
              renderer_sha256=hashlib.sha256(source.read_bytes()).hexdigest())
(a.output/'shaded.json').write_text(json.dumps(result, indent=2)+'\n')
print(json.dumps(result))
