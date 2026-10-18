#!/usr/bin/env python3
"""Check real ELF symbols, compiled sources, image budgets and preserved partitions."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[4]


def partitions(data):
    result = []
    for offset in range(0, len(data), 32):
        record = data[offset:offset + 32]
        if record[:2] != b'\xaa\x50':
            break
        magic, kind, subtype, start, size, label, flags = struct.unpack('<HBBII16sI', record)
        result.append(dict(type=kind, subtype=subtype, offset=start, size=size,
                           label=label.rstrip(b'\0').decode(), flags=flags))
    if not result:
        raise ValueError('No partition entries')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True, type=Path)
    args = parser.parse_args()
    prefix = args.compiler.name.removesuffix('gcc')
    results = {}
    for mode in ['on', 'off', 'headless']:
        build = ROOT / 'local/build/caption-firmware' / mode
        report = json.loads((ROOT / 'local/validation/caption-fonts' / ('firmware-' + mode + '.json')).read_text())
        binary = (build / 'muse-gadget.bin').read_bytes()
        assert report['app_sha256'] == hashlib.sha256(binary).hexdigest()
        assert report['free_bytes'] >= 256 * 1024
        nm = subprocess.check_output([str(args.compiler.with_name(prefix + 'nm')), '-S', str(build / 'muse-gadget.elf')], text=True)
        symbols = ['actell_caption_init', 'actell_font_decode', 'actell_caption_page']
        assert all((symbol in nm) == (mode == 'on') for symbol in symbols)
        assert 'actell_device_sans_' not in nm  # no generated giant font arrays in the app
        commands = json.loads((build / 'compile_commands.json').read_text())
        reader_sources = [c['file'].split('/')[-1] for c in commands if '/components/actell_fonts/src/' in c['file']]
        assert len(reader_sources) == (3 if mode == 'on' else 0)
        size = subprocess.check_output([str(args.compiler.with_name(prefix + 'size')), '-A', str(build / 'muse-gadget.elf')], text=True)
        sections = {name: int(length) for name, length in re.findall(r'^(\.[\w.]+)\s+(\d+)\s+\d+\s*$', size, re.M)}
        results[mode] = dict(app_bytes=len(binary), free_bytes=report['free_bytes'],
                             new_font_symbols_present=mode == 'on', compiled_reader_sources=reader_sources,
                             sections={key: sections.get(key, 0) for key in ['.flash.text','.flash.rodata','.iram0.text','.dram0.data','.dram0.bss']},
                             partitions=partitions((build / 'partition_table/partition-table.bin').read_bytes()))
    assert results['on']['partitions'][:-1] == results['off']['partitions']
    resource = results['on']['partitions'][-1]
    assert resource['label'] == 'actell_assets' and resource['offset'] == 0x830000 and resource['size'] == 0x7d0000
    delta = {key: results['on']['sections'][key] - results['off']['sections'][key] for key in results['on']['sections']}
    report = dict(builds=results, on_minus_off_sections=delta,
                  original_partition_entries_unchanged=True, new_resource_arrays_linked=False,
                  physical_device_verified=False, flashed=False)
    (ROOT / 'local/validation/caption-fonts/firmware-matrix.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'app_bytes': {mode: result['app_bytes'] for mode, result in results.items()},
                      'on_minus_off_sections': delta, 'partition_preservation': True}))


if __name__ == '__main__':
    main()
