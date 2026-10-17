#!/usr/bin/env python3
"""Compare exact pixels and head work with prefilter off/on; host timings only."""
import argparse
import ctypes as C
import json
import os
from pathlib import Path
import random
import shlex
import statistics
import subprocess
import time

AVATAR = Path(__file__).resolve().parents[1]
ROOT = AVATAR.parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--include', type=Path, required=True)
p.add_argument('--output', type=Path, default=ROOT / 'local/validation/avatar-listening')
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)


class Pose(C.Structure):
    _fields_ = [('mode', C.c_int), ('t', C.c_float), ('mode_t', C.c_float),
                ('level', C.c_float), ('happy', C.c_float), ('mouth_level', C.c_float), ('mouth_level_valid', C.c_bool)]


source = (AVATAR / 'src/muse_pixel_160.c').read_text()
instrumented = source
replacements = {
    'static void head_and_ears(float cx,float hy){':
        'unsigned long long perf_hypots,perf_misses;\n'
        'static float counted_hypot(float x,float y){perf_hypots++;return hypotf(x,y);}\n'
        'static void head_and_ears(float cx,float hy){',
    ' float X=(cx+.5f)*2.5f,Y=(hy+.5f)*2.5f+nod_offset;':
        ' perf_misses++;\n float X=(cx+.5f)*2.5f,Y=(hy+.5f)*2.5f+nod_offset;',
    'float d=hypotf(': 'float d=counted_hypot(',
    'd=(hypotf(': 'd=(counted_hypot(',
    'float e=hypotf(': 'float e=counted_hypot(',
}
for before, after in replacements.items():
    assert instrumented.count(before) == 1, before
    instrumented = instrumented.replace(before, after)
counted = a.output / 'counted.c'
counted.write_text(instrumented)
common = shlex.split(os.environ.get('CC', 'clang')) + [
    '-O2', '-shared', '-fPIC', '-DACTELL_AVATAR_TEST', '-DAVATAR_SHARED',
    '-I' + str(a.include), '-I' + str(AVATAR / 'include')]


def compile_lib(label, enabled, src):
    target = a.output / (label + '.so')
    subprocess.run(common + [f'-DMUSE_AVATAR_EXACT_DISTANCE_PREFILTER={enabled}',
        str(src), str(AVATAR / 'src/actell_avatar.c'), str(AVATAR / 'tests/host_test.c'),
        '-lm', '-o', str(target)], check=True)
    lib = C.CDLL(str(target.resolve()))
    lib.muse_pixel_render.argtypes = [C.POINTER(Pose)]
    lib.muse_pixel_scale.argtypes = [C.POINTER(C.c_uint16)] + [C.c_int] * 5
    lib.muse_pixel_set_size(160)
    return lib


libs = [compile_lib(f'counted-{v}', v, counted) for v in (0, 1)]
buffers = [(C.c_uint16 * (160 * 160))() for _ in libs]
rng = random.Random(20261016)
for i in range(12000):
    pose = Pose(2 if i < 8000 else rng.randrange(7),
                i * .001 if i < 8000 else rng.random() * 120,
                rng.random() * 8, rng.random(), rng.choice([0, .8]))
    for lib, buf in zip(libs, buffers):
        lib.muse_pixel_render(C.byref(pose))
        lib.muse_pixel_scale(buf, 160, 0, 159, 0, 159)
    assert bytes(buffers[0]) == bytes(buffers[1]), ('pixel mismatch', i)

results = {}
for enabled, lib in enumerate(libs):
    calls = C.c_ulonglong.in_dll(lib, 'perf_hypots')
    misses = C.c_ulonglong.in_dll(lib, 'perf_misses')
    calls.value = misses.value = 0
    for i in range(200):
        lib.muse_pixel_render(C.byref(Pose(2, i * .04, i * .04, .5, 0)))
    result = dict(hypot_calls=calls.value, cache_misses=misses.value)
    production = compile_lib(f'production-{enabled}', enabled, AVATAR / 'src/muse_pixel_160.c')
    for i in range(200):
        production.muse_pixel_render(C.byref(Pose(2, i * .04, i * .04, .5, 0)))
    samples = []
    for _ in range(5):
        start = time.perf_counter_ns()
        for i in range(2000):
            production.muse_pixel_render(C.byref(Pose(2, i * .04, i * .04, .5, 0)))
        samples.append((time.perf_counter_ns() - start) / 2000 / 1000)
    result.update(uninstrumented_mean_us=samples, mean_us=statistics.mean(samples))
    results[str(enabled)] = result
assert results['0']['cache_misses'] == results['1']['cache_misses']
assert results['1']['hypot_calls'] < results['0']['hypot_calls'] * .1
report = dict(full_grid_pixel_identical_cases=12000, host_only=True,
              physical_board_verified=False, results=results)
(a.output / 'performance.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report))
