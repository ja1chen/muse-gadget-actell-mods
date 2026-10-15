#!/usr/bin/env python3
"""Run the delivered C sanitizer harness without Pillow or changing previews."""
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[3]
AVATAR = ROOT / 'bridge/extensions/avatar'
ARCHIVE = ROOT / 'local/upstream/archives/muse-gadget-sdk-812c46f.tar.gz'
EXPECTED = '0a7548404b4aa25ad0cc3ad0f9fee1ad3a3245a03a14b02293da3a83bde69359'
assert hashlib.sha256(ARCHIVE.read_bytes()).hexdigest() == EXPECTED
report = {'sdk_commit': '812c46f9fe42b69d916613a9dc702cbac5ff7b54',
          'sanitizers': ['address', 'undefined'], 'runs': {},
          'physical_board_verified': False, 'previews_modified': False}
temporary_root = ROOT / 'local/build/tmp'
temporary_root.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='actell-avatar-host-', dir=temporary_root) as directory:
    temp = Path(directory)
    with tarfile.open(ARCHIVE) as archive:
        for name in ('muse_pixel.h', 'muse_state.h'):
            (temp / name).write_bytes(archive.extractfile('esp32/components/muse/' + name).read())
    common = shlex.split(os.environ.get('CC', 'clang')) + [
        '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-g',
        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
        '-DACTELL_AVATAR_TEST', '-I' + str(AVATAR / 'include'), '-I' + str(temp)]
    # Stage from current extension sources each time. Historical SDK build copies
    # can predate the current activity implementation and are not test inputs.
    sdk = temp / 'sdk'
    component = sdk / 'esp32/components/muse'
    component.mkdir(parents=True)
    (component / 'muse_pixel.h').write_bytes((temp / 'muse_pixel.h').read_bytes())
    subprocess.run([sys.executable, str(AVATAR / 'tools/stage.py'),
                    '--sdk-root', str(sdk)], check=True)
    staged = component / 'avatar/muse_pixel.c'
    assert staged.read_text() == (AVATAR / 'src/actell_avatar.c').read_text() + '\n' + (AVATAR / 'src/muse_pixel.c').read_text()
    for name, sources in {
        'original_sources': [AVATAR / 'src/muse_pixel.c', AVATAR / 'src/actell_avatar.c'],
        'official_single_translation_unit': [staged],
    }.items():
        binary = temp / name
        command = common + [str(p) for p in sources] + [str(AVATAR / 'tests/host_test.c'), '-lm', '-o', str(binary)]
        subprocess.run(command, check=True)
        tested = subprocess.run([str(binary)], check=True, text=True, capture_output=True,
                                env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0'})
        report['runs'][name] = tested.stdout.strip()
        print(name + ': ' + tested.stdout.strip())
(ROOT / 'local/validation/current').mkdir(parents=True, exist_ok=True)
(ROOT / 'local/validation/current/avatar-host.json').write_text(json.dumps(report, indent=2) + '\n')
