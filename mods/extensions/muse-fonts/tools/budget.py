#!/usr/bin/env python3
"""Compile generated fonts with the target compiler; measure allocated object sections.

This is an isolated LVGL compatibility/size check, not an app link or device test.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[4]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True, type=Path)
    parser.add_argument('--lvgl-root', required=True, type=Path)
    parser.add_argument('--app-capacity', type=int, default=4 * 1024 * 1024)
    parser.add_argument('--reserve', type=int, default=256 * 1024,
                        help='Required free app space; screening policy, not a hardware guarantee')
    args = parser.parse_args()
    if args.app_capacity <= 0 or args.reserve < 0:
        parser.error('capacity must be positive and reserve nonnegative')
    output = ROOT / 'local/validation/caption-fonts'
    logs = ROOT / 'local/logs/caption-fonts'
    logs.mkdir(parents=True, exist_ok=True)
    rows = []
    for font in sorted((ROOT / 'local/build/caption-fonts').glob('actell_device_sans_*.c')):
        report = json.loads((output / (font.stem + '.json')).read_text())
        if hashlib.sha256(font.read_bytes()).hexdigest() != report['output_sha256']:
            raise SystemExit('Generated font hash mismatch: ' + font.name)
        obj = font.with_suffix('.o')
        command = [str(args.compiler.resolve()), '-c', '-Os', '-DLV_CONF_SKIP',
                   '-DLV_FONT_FMT_TXT_LARGE=1', '-DLV_USE_FONT_COMPRESSED=1',
                   '-I' + str(args.lvgl_root.resolve()), str(font), '-o', str(obj)]
        run = subprocess.run(command, capture_output=True, text=True)
        (logs / (font.stem + '.log')).write_text(run.stdout + run.stderr)
        if run.returncode:
            raise SystemExit('Font object compilation failed; see local/logs/caption-fonts')
        size_tool = args.compiler.with_name(args.compiler.name.removesuffix('gcc') + 'size')
        result = subprocess.run([str(size_tool.resolve()), '-A', str(obj)],
                                capture_output=True, text=True, check=True).stdout
        sections = {name: int(size) for name, size in
                    re.findall(r'^(\.[\w.]+)\s+(\d+)\s+\d+\s*$', result, re.M)}
        allocated = sum(size for name, size in sections.items()
                        if name.startswith(('.text', '.rodata', '.data')))
        rows.append({'name': font.stem, 'sections': sections,
                     'font_allocated_bytes': allocated,
                     'font_alone_exceeds_app': allocated > args.app_capacity,
                     'available_for_program_with_reserve': args.app_capacity - args.reserve - allocated,
                     'object_sha256': hashlib.sha256(obj.read_bytes()).hexdigest()})
    if not rows:
        raise SystemExit('No generated fonts; run generate.js first')
    report = {'app_capacity_bytes': args.app_capacity, 'required_reserve_bytes': args.reserve,
              'scope': 'ESP32-S3 font objects with LV_CONF_SKIP, large glyph descriptors and compression; not final linked firmware',
              'firmware_linked': False, 'partition_changed': False, 'flashed': False, 'candidates': rows}
    (output / 'budget.json').write_text(json.dumps(report, indent=2) + '\n')
    for row in rows:
        print(row['name'], row['font_allocated_bytes'],
              'bytes; budget left for program:', row['available_for_program_with_reserve'])


if __name__ == '__main__':
    main()
