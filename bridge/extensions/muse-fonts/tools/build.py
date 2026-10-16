#!/usr/bin/env python3
"""Build font-only or combined font/TTS firmware with protected private outputs.

Requires an activated ESP-IDF 6.0.1 environment and previously resolved components.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', required=True, type=Path)
    parser.add_argument('--components-dir', required=True, type=Path)
    parser.add_argument('--mode', choices=['on', 'off', 'headless'], required=True)
    parser.add_argument('--secrets', type=Path, help='private import; SDK-only unless --with-tts; values never displayed')
    parser.add_argument('--with-tts', action='store_true', help='build combined fonts/TTS using the normal private importer')
    parser.add_argument('--tts-transport', choices=['bidirectional', 'http'],
                        help='requires --with-tts; defaults to bidirectional; HTTP output is isolated')
    args = parser.parse_args()
    if args.tts_transport and not args.with_tts:
        parser.error('--tts-transport requires --with-tts')
    tts_transport = (args.tts_transport or 'bidirectional') if args.with_tts else None
    private = args.secrets is not None
    if private and args.mode != 'on':
        parser.error('private SDK-token build supports the independent font-on profile only')
    if args.with_tts and (not private or args.mode != 'on'):
        parser.error('--with-tts requires --mode on and --secrets')
    if private:
        os.umask(0o077)
    sdk = args.sdk.resolve() / 'esp32'
    if not sdk.is_relative_to(ROOT / 'local/build'):
        parser.error('Only a staged SDK under local/build may be built')
    if args.with_tts and not (sdk / 'components/doubao_tts/CMakeLists.txt').is_file():
        parser.error('Stage the TTS extension before building combined firmware')
    idf = Path(os.environ['IDF_PATH'])
    label = 'private-fonts-tts' if args.with_tts else 'private-on' if private else args.mode
    if tts_transport == 'http':
        label += '-http'
    build = ROOT / 'local/build/caption-firmware' / label
    build.mkdir(parents=True, exist_ok=True)
    log_dir = ROOT / 'local/logs/caption-fonts'
    log_dir.mkdir(parents=True, exist_ok=True)
    public = build / 'public.defaults'
    public.write_text('CONFIG_GADGET_SDK_TOKEN=""\nCONFIG_HOMEHUB_WIFI_SSID=""\nCONFIG_HOMEHUB_WIFI_PASSWORD=""\n'
                      'CONFIG_HOMEHUB_TUNNEL=n\nCONFIG_ACTELL_BRIDGE=y\nCONFIG_ACTELL_CAPTION_FONT=' +
                      ('y' if args.mode == 'on' else 'n') + '\n' +
                      ('CONFIG_MUSE_CJK_FONT=y\n' if args.mode != 'headless' else '') +
                      (('CONFIG_DOUBAO_TTS_BIDIRECTIONAL=y\n' if tts_transport == 'bidirectional' else
                        '# CONFIG_DOUBAO_TTS_BIDIRECTIONAL is not set\n') if args.with_tts else '') +
                      ('CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="components/actell_fonts/partitions.resources.csv"\n' if args.mode == 'on' else ''))
    defaults = [sdk / 'sdkconfig.defaults']
    if args.mode == 'headless':
        defaults += [ROOT / 'bridge/extensions/headless-console/sdkconfig.headless-n16r8']
    else:
        defaults += [sdk / 'devices/sdkconfig.muse', sdk / 'devices/sdkconfig.muse-waveshare-s3-216']
    defaults += [ROOT / 'bridge/extensions/gadget-actell/overlays/sdkconfig.bridge', public]
    # This directory is owned solely by this public-config runner. Recreate
    # its seed so prior builds cannot silently retain a stale font/table mode.
    (build / 'sdkconfig').write_text(public.read_text())
    if private:
        imported = subprocess.run([sys.executable, str(ROOT / 'bridge/tools/apply-secrets.py'),
                                   '--secrets', str(args.secrets), '--sdkconfig', str(build / 'sdkconfig'),
                                   *([] if args.with_tts else ['--sdk-token-only'])], capture_output=True, text=True)
        if imported.returncode:
            print('Private token import failed; values withheld.')
            return imported.returncode
        if args.with_tts and 'TTS enabled;' not in imported.stdout:
            print('Combined build requires an enabled TTS provider; no values displayed.')
            return 1
        print('SDK and TTS configuration imported; values withheld.' if args.with_tts else
              'SDK token imported through tool; provider fields excluded; values withheld.', flush=True)
    extra = sorted(p.resolve() for p in args.components_dir.iterdir() if (p / 'CMakeLists.txt').is_file())
    env = {**os.environ, 'IDF_COMPONENT_MANAGER': '1', 'IDF_COMPONENT_CACHE_PATH': str(ROOT / 'local/cache/idf-components'),
           'PYTHONPYCACHEPREFIX': str(ROOT / 'local/cache/python'),
           'TMPDIR': str(ROOT / 'local/build/tmp'), 'CCACHE_DIR': str(ROOT / 'local/cache/ccache')}
    command = [sys.executable, str(idf / 'tools/idf.py'), '-G', 'Unix Makefiles', '-B', str(build),
               '-DIDF_TARGET=esp32s3', '-DSDKCONFIG=' + str(build / 'sdkconfig'),
               '-DSDKCONFIG_DEFAULTS=' + ';'.join(map(str, defaults)),
               '-DEXTRA_COMPONENT_DIRS=' + ';'.join(map(str, extra)), 'build']
    log_path = log_dir / ('firmware-' + label + '.log')
    with log_path.open('w') as log:
        run = subprocess.run(command, cwd=sdk, env=env, stdout=log, stderr=subprocess.STDOUT)
    if private:
        # Some build steps override umask. Keep all private artifacts owner-only.
        for path in [build, *build.rglob('*')]:
            if not path.is_symlink():
                path.chmod(path.stat().st_mode & ~0o077)
    if run.returncode:
        if private:
            print('Private build failed; log contents withheld. Diagnose with public configuration.')
            return run.returncode
        print('\n'.join(log_path.read_text().splitlines()[-55:]))
        return run.returncode
    image = build / 'muse-gadget.bin'
    if args.with_tts:
        compiler = shutil.which('xtensa-esp32s3-elf-gcc')
        if not compiler:
            print('Target compiler unavailable for combined feature checks.')
            return 1
        # The compiler consumes the private header; neither header nor values
        # are exposed. Check only the required feature switches.
        check = subprocess.run([compiler, '-x', 'c', '-fsyntax-only', '-include',
                                str(build / 'config/sdkconfig.h'), '-'], input=
                               '#if !CONFIG_ACTELL_CAPTION_FONT || !CONFIG_DOUBAO_TTS || !CONFIG_MUSE_HATCH || !CONFIG_MUSE_BOARD_WAVESHARE_S3_216\n#error Combined features missing\n#endif\n' +
                               f'#if !!CONFIG_DOUBAO_TTS_BIDIRECTIONAL != {int(tts_transport == "bidirectional")}\n#error TTS transport mismatch\n#endif\n',
                               capture_output=True, text=True)
        if check.returncode:
            print('Combined target feature check failed; private details withheld.')
            return 1
    if not private:
        config = (build / 'config/sdkconfig.h').read_text()
        expected = '#define CONFIG_ACTELL_CAPTION_FONT 1'
        if (expected in config) != (args.mode == 'on'):
            raise SystemExit('Unexpected font Kconfig state')
    report = dict(mode=label, sdk_commit='812c46f9fe42b69d916613a9dc702cbac5ff7b54',
                  app_bytes=image.stat().st_size, app_sha256=None if private else hashlib.sha256(image.read_bytes()).hexdigest(),
                  capacity_bytes=4 * 1024 * 1024 if args.mode != 'headless' else 2 * 1024 * 1024,
                  private_configuration_used=private, provider_credentials_imported=args.with_tts,
                  combined_features_verified=args.with_tts, tts_transport=tts_transport, device_partition_changed=False,
                  shared_resource_table=args.mode == 'on', flashed=False)
    report['free_bytes'] = report['capacity_bytes'] - report['app_bytes']
    destination = ROOT / 'local/validation/caption-fonts'
    destination.mkdir(parents=True, exist_ok=True)
    (destination / ('firmware-' + label + '.json')).write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
