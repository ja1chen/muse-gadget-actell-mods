#!/usr/bin/env python3
"""Run the actual device adapter with host read-only partition/crypto fakes (macOS)."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pack

ROOT = Path(__file__).resolve().parents[4]
EXT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--components-dir', required=True, type=Path)
    args = parser.parse_args()
    if sys.platform != 'darwin':
        raise SystemExit('This harness uses macOS CommonCrypto; no target verification claimed')
    build = ROOT / 'local/build/caption-fonts/port-host'
    build.mkdir(parents=True, exist_ok=True)
    binary = build / 'resource-port'
    cjson = args.components_dir.resolve() / 'espressif__cjson/cJSON'
    lvgl = args.components_dir.resolve() / 'lvgl__lvgl'
    subprocess.run(['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-deprecated-declarations',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-DLV_CONF_SKIP',
                    '-I' + str(EXT / 'tests/fakes'), '-I' + str(EXT / 'include'), '-I' + str(cjson), '-I' + str(lvgl),
                    *map(str, sorted((EXT / 'src').glob('*.c'))), str(cjson / 'cJSON.c'),
                    str(EXT / 'tests/resource_port_test.c'), '-o', str(binary)], check=True)
    original = (ROOT / 'local/build/read-only-resources/actell_device_sans_regular_24_4/resources.bin').read_bytes()
    cases = [('missing', None, False), ('valid', original, True)]
    corrupt = bytearray(original); corrupt[65536] ^= 1
    cases.append(('payload-corrupt', corrupt, False))
    corrupt = bytearray(original); corrupt[8] = 2
    cases.append(('unknown-abi', corrupt, False))
    directory = pack.verify(original)
    assets = [(e['id'], e['format'], original[e['offset']:e['offset'] + e['length']]) for e in directory['entries']]
    cases.append(('wrong-font-format', pack.create([(a, 'incompatible.font' if a == 'caption.font' else f, b) for a, f, b in assets], 'synthetic'), False))
    broken = []
    for name, fmt, data in assets:
        if name == 'caption.font':
            data = bytearray(data); data[34] = 255; data = bytes(data)
        broken.append((name, fmt, data))
    cases.append(('font-invalid-valid-container-hashes', pack.create(broken, 'synthetic'), False))
    fields = list(pack.HEADER.unpack_from(original))
    index = json.loads(original[64:64 + fields[2]])
    index['entries'][0]['offset'] = 0xFFFFFF
    encoded = pack.canonical(index); fields[2] = len(encoded); fields[5] = hashlib.sha256(encoded).digest()
    corrupt = bytearray(original); corrupt[:64] = pack.HEADER.pack(*fields); corrupt[64:64+len(encoded)] = encoded
    cases.append(('offset-invalid-valid-directory-hash', corrupt, False))
    results = []
    for name, image, expected in cases:
        filename = build / (name + '.bin')
        if image is not None: filename.write_bytes(image)
        run = subprocess.run([str(binary), str(filename) if image is not None else '-', str(int(expected))], capture_output=True, text=True, check=True)
        print(name + ': ' + run.stdout.strip())
        results.append(dict(case=name, expected_active=expected, passed=True))
    (ROOT / 'local/validation/caption-fonts/resource-port-host.json').write_text(json.dumps(dict(cases=results, sanitizer=True, host_partition_mock=True, hardware_verified=False), indent=2) + '\n')


if __name__ == '__main__':
    main()
