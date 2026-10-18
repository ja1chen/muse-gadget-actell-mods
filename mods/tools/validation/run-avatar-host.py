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
AVATAR = ROOT / 'mods/extensions/avatar'
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
                     for name in ('muse_pixel.h', 'muse_state.h', 'muse_state.c', 'muse_ui.c', 'Kconfig')}
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
    patch = ROOT / 'mods/patches/muse-gadget-sdk/avatar.patch'
    subprocess.run(['git', 'apply', '--check', str(patch)], cwd=review, check=True)
    subprocess.run(['git', 'apply', str(patch)], cwd=review, check=True)
    for name in originals:
        assert (review / 'esp32/components/muse' / name).read_bytes() == (component / name).read_bytes()
    common = shlex.split(os.environ.get('CC', 'clang')) + [
        '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-g',
        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
        '-DACTELL_AVATAR_TEST', '-I' + str(AVATAR / 'include'), '-I' + str(component)]
    # Exercise actual elapsed-time envelope code with sanitizers, independent of rendering.
    envelope = temp / 'mouth-envelope'
    subprocess.run(common + ['-I' + str(AVATAR/'src'), str(AVATAR/'tests/mouth_envelope_test.c'),
                            '-lm', '-o', str(envelope)], check=True)
    tested = subprocess.run([str(envelope)], check=True, text=True, capture_output=True,
                            env={**os.environ, 'ASAN_OPTIONS':'detect_leaks=0'})
    report['mouth_envelope'] = tested.stdout.strip()
    print(tested.stdout.strip(), flush=True)
    ui_source = (component/'muse_ui.c').read_text()
    assert 'muse_mouth_sample_t mouth_sample = muse_state_take_mouth_sample();' in ui_source
    assert 's_level += (level - s_level) * (level > s_level ? 0.6f : 0.2f);' in ui_source
    start = ui_source.index('float mouth_level = actell_mouth_step(')
    assert start < ui_source.index('if (muse_menu_tick(now))', start)
    assert ui_source.index('return;',start) > start
    for branch, flags, variant in [(0, [], 'legacy'), (1, [], 'shaded'),
                                    (1, ['-DMUSE_AVATAR_SHADING=0'], 'flat'),
                                    (1, ['-DMUSE_AVATAR_DYNAMIC_LIGHT=1'], 'dynamic')]:
        for name, sources in {
            'source_dispatch': [AVATAR / 'src/muse_pixel_dispatch.c'],
            'staged_override': [staged],
        }.items():
            label = f'{name}_{variant}'
            binary = temp / label
            subprocess.run(common + [*flags, f'-DCONFIG_MUSE_AVATAR_V2={branch}', *map(str, sources),
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
    (fake/'esp_memory_utils.h').write_text('#define esp_ptr_external_ram(p) ((p)!=0)\n')
    for branch in (0, 1):
        (fake / 'sdkconfig.h').write_text(f'#define CONFIG_MUSE_AVATAR_V2 {branch}\n#define CONFIG_SPIRAM 1\n#define CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY 1\n')
        subprocess.run(shlex.split(os.environ.get('CC', 'clang')) + ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
            '-DESP_PLATFORM', '-I' + str(fake), '-I' + str(AVATAR / 'tests/esp_fakes'), '-I' + str(component),
            '-c', str(component/'avatar'/('muse_pixel_160.c' if branch else 'muse_pixel_legacy.c')), '-o', str(temp / f'esp-{branch}.o')], check=True)
    subprocess.run([sys.executable, str(AVATAR / 'tools/check-v13.py'), '--include', str(component), '--output', str(temp)], check=True)
    shaded_output = ROOT / 'local/validation/avatar-shaded'
    subprocess.run([sys.executable,str(AVATAR/'tools/check-shaded.py'),'--include',str(component),'--output',str(shaded_output)],check=True)
    report['shaded'] = json.loads((shaded_output/'shaded.json').read_text())
    mouth_output = ROOT / 'local/validation/avatar-mouth-stable'
    subprocess.run([sys.executable,str(AVATAR/'tools/check-mouth-close.py'),'--include',str(component),'--output',str(mouth_output)],check=True)
    report['mouth_close'] = json.loads((mouth_output/'mouth.json').read_text())
    subprocess.run([sys.executable,str(AVATAR/'tools/check-mouth-observer.py'),'--include',str(component),'--output',str(mouth_output)],check=True)
    report['mouth_observer'] = json.loads((mouth_output/'observer-timing-results.json').read_text())
    report['pixels'] = json.loads((temp / 'pixel-report.json').read_text())
    performance = ROOT / 'local/validation/avatar-listening'
    subprocess.run([sys.executable, str(AVATAR / 'tools/check-listening-performance.py'),
                    '--include', str(component), '--output', str(performance)], check=True)
    report['listening_performance'] = json.loads((performance / 'performance.json').read_text())
    raw = ROOT/'local/build/avatar-raw-aa/assets/avatar-aa.raw'
    if raw.is_file():
        raw_output=ROOT/'local/validation/avatar-raw-aa'
        subprocess.run([sys.executable,str(AVATAR/'tools/check-raw-aa.py'),'--include',str(component),'--raw',str(raw),'--output',str(raw_output)],check=True)
        report['raw_aa']=json.loads((raw_output/'raw.json').read_text())
    else:
        report['raw_aa']={'bound_resource_checks':'skipped; raw resource not supplied'}
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
