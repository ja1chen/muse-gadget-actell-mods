#!/usr/bin/env python3
"""Validate both renderers and reversible hooks against the pinned SDK archive."""
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
# Preserve the approved legacy branch independently of the new renderer.
assert hashlib.sha256((AVATAR / 'src/muse_pixel.c').read_bytes()).hexdigest() == 'd3731211b57e2e97e4606801533ba89555baa9459d5f7e5186917847172c6ec1'
report = {'sdk_commit': '812c46f9fe42b69d916613a9dc702cbac5ff7b54',
          'sanitizers': ['address', 'undefined'], 'leak_detection': False, 'runs': {},
          'physical_board_verified': False, 'previews_modified': False}
temporary_root = ROOT / 'local/build/tmp'
temporary_root.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='actell-avatar-host-', dir=temporary_root) as directory:
    temp = Path(directory)
    sdk = temp / 'sdk'
    component = sdk / 'esp32/components/muse'
    component.mkdir(parents=True)
    with tarfile.open(ARCHIVE) as archive:
        originals = {name: archive.extractfile('esp32/components/muse/' + name).read()
                     for name in ('muse_pixel.h', 'muse_state.h', 'muse_ui.c', 'Kconfig')}
    for name, data in originals.items():
        (component / name).write_bytes(data)
    stage = [sys.executable, str(AVATAR / 'tools/stage.py'), '--sdk-root', str(sdk)]
    subprocess.run(stage, check=True)
    staged = component / 'avatar/muse_pixel.c'
    snapshot = {str(p.relative_to(component)): p.read_bytes() for p in component.rglob('*') if p.is_file()}
    subprocess.run(stage, check=True)
    assert snapshot == {str(p.relative_to(component)): p.read_bytes() for p in component.rglob('*') if p.is_file()}
    # The committed review patch must be exactly equivalent to stage.py hooks.
    review = temp / 'review'
    for name, data in originals.items():
        dest = review / 'esp32/components/muse' / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
    patch = ROOT / 'bridge/patches/muse-gadget-sdk/avatar.patch'
    subprocess.run(['git', 'apply', '--check', str(patch)], cwd=review, check=True)
    subprocess.run(['git', 'apply', str(patch)], cwd=review, check=True)
    for name in originals:
        assert (review / 'esp32/components/muse' / name).read_bytes() == (component / name).read_bytes()
    common = shlex.split(os.environ.get('CC', 'clang')) + [
        '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-g',
        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
        '-DACTELL_AVATAR_TEST', '-I' + str(AVATAR / 'include'), '-I' + str(component)]
    for branch in (0, 1):
        for name, sources in {
            'source_dispatch': [AVATAR / 'src/muse_pixel_dispatch.c'],
            'staged_override': [staged],
        }.items():
            label = f'{name}_v{branch}'
            binary = temp / label
            subprocess.run(common + [f'-DCONFIG_MUSE_AVATAR_V2={branch}', *map(str, sources),
                           str(AVATAR / 'tests/host_test.c'), '-lm', '-o', str(binary)], check=True)
            tested = subprocess.run([str(binary)], check=True, text=True, capture_output=True,
                                    env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0'})
            report['runs'][label] = tested.stdout.strip()
            print(label + ': ' + tested.stdout.strip(), flush=True)
    # Actual ESP_PLATFORM interfaces, including opt-in external BSS.
    fake = temp / 'fakes'
    fake.mkdir()
    section = '__DATA,__avatar_bss' if sys.platform == 'darwin' else '.ext_ram.bss'
    (fake / 'esp_attr.h').write_text(f'#define EXT_RAM_BSS_ATTR __attribute__((section("{section}")))\n')
    for branch in (0, 1):
        (fake / 'sdkconfig.h').write_text(f'#define CONFIG_MUSE_AVATAR_V2 {branch}\n#define CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY 1\n')
        subprocess.run(shlex.split(os.environ.get('CC', 'clang')) + ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
            '-DESP_PLATFORM', '-I' + str(fake), '-I' + str(AVATAR / 'tests/esp_fakes'), '-I' + str(component),
            '-c', str(staged), '-o', str(temp / f'esp-{branch}.o')], check=True)
    subprocess.run([sys.executable, str(AVATAR / 'tools/check-v13.py'), '--include', str(component), '--output', str(temp)], check=True)
    report['pixels'] = json.loads((temp / 'pixel-report.json').read_text())
    # Refuse rollback over subsequent UI edits; restore exactly when unchanged.
    ui = component / 'muse_ui.c'
    installed = ui.read_bytes()
    ui.write_bytes(installed + b'\n/* later font hooks */\n')
    refused = subprocess.run(stage + ['--rollback'], capture_output=True)
    assert refused.returncode and ui.read_bytes() == installed + b'\n/* later font hooks */\n'
    ui.write_bytes(installed)
    subprocess.run(stage + ['--rollback'], check=True)
    assert all((component / name).read_bytes() == data for name, data in originals.items())
    assert not any((component / 'avatar').iterdir())
    (component / 'avatar/muse_pixel.c').write_text('/* existing custom avatar */\n')
    assert subprocess.run(stage, capture_output=True).returncode
    assert all((component / name).read_bytes() == data for name, data in originals.items())
    report['staging'] = 'idempotent, patch-equivalent, exact rollback, conflicting edits/overrides refused'
output = ROOT / 'local/validation/current/avatar-host.json'
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(report, indent=2) + '\n')
