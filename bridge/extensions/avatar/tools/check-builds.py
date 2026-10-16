#!/usr/bin/env python3
"""Check public target ELF symbols, external BSS, image budgets and partitions."""
import argparse
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[4]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--compiler', required=True, type=Path)
a = p.parse_args()
prefix = a.compiler.name.removesuffix('gcc')
def tool(name, *args):
    return subprocess.check_output([str(a.compiler.with_name(prefix + name)), *map(str, args)], text=True)

results = {}
for mode in ['v13', 'legacy']:
    build = ROOT / 'local/build/avatar-v13' / mode
    report = json.loads((ROOT / 'local/validation/avatar-v13' / (mode + '.json')).read_text())
    assert report['synthetic_configuration'] and not report['flashed']
    elf = build / 'muse-gadget.elf'
    nm = tool('nm', '-S', elf)
    symbols = {name: {'address': int(address, 16), 'bytes': int(size, 16)}
               for address, size, kind, name in re.findall(r'^([0-9a-f]+) ([0-9a-f]+) (\w) (\S+)$', nm, re.M)}
    assert symbols['fb']['bytes'] == (25600 if mode == 'v13' else 4096)
    assert ('head_cache' in symbols) == (mode == 'v13')
    assert ('scale_map' in symbols) == (mode == 'v13')
    for feature in ['actell_caption_init', 'actell_avatar_activity', 'doubao_tts_start', 'actell_bridge_register']:
        assert feature in symbols, feature
    raw = tool('size', '-A', elf)
    sections = {name: {'bytes': int(size), 'address': int(address)}
                for name, size, address in re.findall(r'^(\.[\w.]+)\s+(\d+)\s+(\d+)\s*$', raw, re.M)}
    if mode == 'v13':
        ext = sections['.ext_ram.bss']
        for name in ['fb', 'head_cache']:
            assert ext['address'] <= symbols[name]['address'] < ext['address'] + ext['bytes']
        assert symbols['head_cache']['bytes'] == 25600 and symbols['scale_map']['bytes'] == 1024
    assert report['free_bytes'] >= 256 * 1024
    commands = json.loads((build / 'compile_commands.json').read_text())
    renderers = [c['file'] for c in commands if c['file'].endswith('/muse_pixel.c')]
    assert len(renderers) == 1 and renderers[0].endswith('/components/muse/avatar/muse_pixel.c')
    assert Path(renderers[0]).is_relative_to(ROOT / 'local/build')
    # Record the scratch array from the final map, rather than assuming lv_area_t size.
    mapping = (build / 'muse-gadget.map').read_text()
    scratch = re.findall(r'\.bss\.(r[.$]\d+)\s+0x[0-9a-f]+\s+(0x[0-9a-f]+) esp-idf/muse/libmuse.a\(muse_ui.c.obj\)', mapping)
    assert bool(scratch) == (mode == 'v13')
    results[mode] = dict(app_bytes=report['app_bytes'], free_bytes=report['free_bytes'],
                         sections=sections,
                         renderer_symbols={name: symbols[name] for name in ['fb','head_cache','scale_map'] if name in symbols},
                         dirty_scratch_bytes=sum(int(size,16) for name,size in scratch))
builds = ROOT / 'local/build/avatar-v13'
assert (builds / 'v13/partition_table/partition-table.bin').read_bytes() == (builds / 'legacy/partition_table/partition-table.bin').read_bytes()
keys = ['.flash.text', '.flash.rodata', '.dram0.bss', '.dram0.data', '.ext_ram.bss']
delta = {key: results['v13']['sections'].get(key, {}).get('bytes', 0) - results['legacy']['sections'].get(key, {}).get('bytes', 0) for key in keys}
output = dict(builds=results, v13_minus_legacy_bytes=delta,
              partitions_identical=True, runtime_dirty_rgb565_delta_bytes=51200-8192,
              physical_board_verified=False, peak_heap_measured=False, flashed=False)
dest = ROOT / 'local/validation/avatar-v13/matrix.json'
dest.write_text(json.dumps(output, indent=2) + '\n')
print(json.dumps(dict(app_bytes={key:value['app_bytes'] for key,value in results.items()},
                      v13_minus_legacy_bytes=delta, dirty_scratch_bytes=results['v13']['dirty_scratch_bytes'],
                      partitions_identical=True, flashed=False)))
