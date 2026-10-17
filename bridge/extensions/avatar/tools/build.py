#!/usr/bin/env python3
"""Build Waveshare avatar variants without accessing devices.

Run inside the existing ESP-IDF 6.0.1 environment after staging all extensions.
Outputs are isolated under local/build/avatar-v13 and local/logs/avatar-v13.
Private configuration is imported only by apply-secrets.py; output is withheld.
"""
import argparse
import json
import os
import shutil
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--sdk', type=Path, required=True)
p.add_argument('--components-dir', type=Path, required=True)
p.add_argument('--mode', choices=['v13', 'legacy'], required=True)
p.add_argument('--shading', choices=['on', 'off'], default='on', help='v13: static shading or the same rounded face without shading')
p.add_argument('--internal-bss', action='store_true', help='diagnose v13 internal RAM budget; default uses board PSRAM')
p.add_argument('--secrets', type=Path, help='explicit private configuration path; imported without displaying values')
a = p.parse_args()
private = a.secrets is not None
if private:
    os.umask(0o077)
sdk = a.sdk.resolve() / 'esp32'
if not sdk.is_relative_to(ROOT / 'local/build'):
    p.error('Only staged SDKs under local/build may be built')
with_controls = (sdk / 'components/muse/boards/waveshare_216_controls.h').is_file()
label = (('v13-shaded' if a.shading == 'on' else 'v13-flat') if a.mode == 'v13' else 'legacy') + ('-internal' if a.internal_bss else '')
if private:
    label = 'private-' + label
build = ROOT / 'local/build/avatar-v13' / label
build.mkdir(parents=True, exist_ok=True)
# Public seeds cannot authenticate. Private seeds are populated only by the importer.
seed = ('# PUBLIC SYNTHETIC BUILD ONLY; no network or playback test.\n'
        'CONFIG_GADGET_SDK_TOKEN=""\nCONFIG_HOMEHUB_WIFI_SSID=""\nCONFIG_HOMEHUB_WIFI_PASSWORD=""\n'
        'CONFIG_HOMEHUB_TUNNEL=n\nCONFIG_ACTELL_BRIDGE=y\nCONFIG_ACTELL_CAPTION_FONT=y\n'
        'CONFIG_MUSE_CJK_FONT=y\nCONFIG_DOUBAO_TTS_API_KEY="SYNTHETIC-NOT-A-VALID-KEY"\n'
        'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="components/actell_fonts/partitions.resources.csv"\n'
        f'CONFIG_MUSE_AVATAR_V2={"y" if a.mode == "v13" else "n"}\n'
        f'CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY={"n" if a.internal_bss else "y"}\n')
if with_controls:
    seed += (ROOT / 'bridge/extensions/waveshare-216-controls/overlays/sdkconfig.controls').read_text()
if private:
    seed = seed.replace('# PUBLIC SYNTHETIC BUILD ONLY; no network or playback test.',
                        '# Private build seed; credentials imported separately.').replace('SYNTHETIC-NOT-A-VALID-KEY', '')
public = build / 'public.defaults'
public.write_text(seed)
(build / 'sdkconfig').write_text(seed)
if private:
    imported = subprocess.run([sys.executable, str(ROOT / 'bridge/tools/apply-secrets.py'),
                               '--secrets', str(a.secrets), '--sdkconfig', str(build / 'sdkconfig')],
                              capture_output=True, text=True)
    if imported.returncode or 'TTS enabled;' not in imported.stdout:
        raise SystemExit('Private import failed or TTS is disabled; values withheld.')
    print('SDK and TTS configuration imported; values withheld.', flush=True)
defaults = [sdk / 'sdkconfig.defaults', sdk / 'devices/sdkconfig.muse',
            sdk / 'devices/sdkconfig.muse-waveshare-s3-216',
            ROOT / 'bridge/extensions/gadget-actell/overlays/sdkconfig.bridge', public]
components = sorted(path.resolve() for path in a.components_dir.iterdir() if (path / 'CMakeLists.txt').is_file())
env = {**os.environ, 'IDF_COMPONENT_MANAGER': '1',
       'IDF_COMPONENT_CACHE_PATH': str(ROOT / 'local/cache/idf-components'),
       'PYTHONPYCACHEPREFIX': str(ROOT / 'local/cache/python'),
       'TMPDIR': str(ROOT / 'local/build/tmp'), 'CCACHE_DIR': str(ROOT / 'local/cache/ccache')}
command = [sys.executable, str(Path(os.environ['IDF_PATH']) / 'tools/idf.py'), '-G', 'Unix Makefiles',
           '-B', str(build), '-DIDF_TARGET=esp32s3', '-DSDKCONFIG=' + str(build / 'sdkconfig'),
           '-DSDKCONFIG_DEFAULTS=' + ';'.join(map(str, defaults)),
           '-DEXTRA_COMPONENT_DIRS=' + ';'.join(map(str, components)),
           '-DCMAKE_C_FLAGS=-DMUSE_AVATAR_SHADING=' + str(int(a.shading == 'on')), 'build']
log = ROOT / 'local/logs/avatar-v13' / (label + '.log')
log.parent.mkdir(parents=True, exist_ok=True)
report_path = ROOT / 'local/validation/avatar-v13' / (label + '.json')
report_path.unlink(missing_ok=True)
with log.open('w') as output:
    status = subprocess.run(command, cwd=sdk, env=env, stdout=output, stderr=subprocess.STDOUT).returncode
if private:
    for path in [build, *build.rglob('*'), log]:
        if not path.is_symlink():
            path.chmod(path.stat().st_mode & ~0o077)
if status:
    print('Private build failed; log contents withheld. Diagnose with public configuration.' if private else
          '\n'.join(log.read_text().splitlines()[-65:]))
    raise SystemExit(status)
compiler = shutil.which('xtensa-esp32s3-elf-gcc')
if not compiler:
    raise SystemExit('Target compiler unavailable for feature checks.')
checks = '#if !CONFIG_ACTELL_CAPTION_FONT || !CONFIG_DOUBAO_TTS || !CONFIG_MUSE_HATCH || !CONFIG_MUSE_BOARD_WAVESHARE_S3_216 || !CONFIG_ACTELL_BRIDGE\n#error Required features missing\n#endif\n'
if with_controls:
    checks += '#if !CONFIG_LV_FONT_MONTSERRAT_24\n#error Controls font missing\n#endif\n'
checks += f'#if !!CONFIG_MUSE_AVATAR_V2 != {int(a.mode == "v13")}\n#error Wrong avatar branch\n#endif\n'
checks += f'#if !!CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY != {int(not a.internal_bss)}\n#error Wrong BSS placement\n#endif\n'
verified = subprocess.run([compiler, '-x', 'c', '-fsyntax-only', '-include',
                           str(build / 'config/sdkconfig.h'), '-'], input=checks,
                          capture_output=True, text=True)
if verified.returncode:
    raise SystemExit('Target feature checks failed; configuration contents withheld.')
report = dict(mode=label, shading=a.mode == 'v13' and a.shading == 'on', app_bytes=(build / 'muse-gadget.bin').stat().st_size,
              app_capacity=4 * 1024 * 1024, synthetic_configuration=not private,
              private_configuration_used=private,
              fonts_tts_bridge_compiled=True, controls_staged=with_controls, flashed=False, physical_board_verified=False)
report['free_bytes'] = report['app_capacity'] - report['app_bytes']
output = report_path
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report))
