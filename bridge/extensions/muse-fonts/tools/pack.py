#!/usr/bin/env python3
"""Build/verify a deterministic, indexed read-only resource container (host tooling).

Independent code: Apache-2.0. Font payloads retain OFL-1.1.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[4]
EXTENSION = Path(__file__).resolve().parents[1]
HEADER = struct.Struct('<8sIIII32s8s')
MAGIC = b'ACTRES01'
ALIGNMENT = 65536
CAPACITY = 0x7D0000
MAX_INDEX = 8192
MAX_ENTRIES = 32
ID = re.compile(r'[a-z0-9][a-z0-9_.-]{0,95}\Z')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def align(n):
    return (n + ALIGNMENT - 1) // ALIGNMENT * ALIGNMENT


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':')).encode()


def verify_binary_font(data):
    """Check converter tables and glyph bounds; does not substitute for bitmap decoding."""
    tables, position = {}, 0
    while position < len(data):
        if len(data) - position < 8:
            raise ValueError('Truncated binary font table')
        size, label = struct.unpack_from('<I4s', data, position)
        if size < 8 or size % 4 or size > len(data) - position or label in tables:
            raise ValueError('Invalid binary font table bounds')
        tables[label] = data[position:position + size]
        position += size
    if list(tables) != [b'head', b'cmap', b'loca', b'glyf']:
        raise ValueError('Unsupported font table layout')
    head, loca, glyf = tables[b'head'], tables[b'loca'], tables[b'glyf']
    if len(head) != 48 or struct.unpack_from('<I', head, 8)[0] != 1:
        raise ValueError('Unsupported font header')
    if struct.unpack_from('<H', head, 12)[0] != 3 or head[36] != 0 or head[37] not in (2, 4):
        raise ValueError('Unsupported font profile')
    if head[34] not in (0, 1) or head[35] not in (0, 1) or head[41] not in (0, 1, 2):
        raise ValueError('Unsupported font encoding')
    if len(loca) < 12:
        raise ValueError('Truncated glyph index')
    count = struct.unpack_from('<I', loca, 8)[0]
    width = 4 if head[34] else 2
    if count < 2 or 12 + count * width > len(loca):
        raise ValueError('Invalid glyph index length')
    previous = 8
    for i in range(count):
        offset = struct.unpack_from('<I' if width == 4 else '<H', loca, 12 + i * width)[0]
        if offset < previous or offset >= len(glyf):
            raise ValueError('Out-of-range glyph offset')
        previous = offset
    return {'glyphs': count - 1, 'bpp': head[37], 'size': struct.unpack_from('<H', head, 14)[0]}


def create(assets, profile):
    """Assets are (public logical ID, format, bytes), never source paths."""
    if not assets or len(assets) > MAX_ENTRIES:
        raise ValueError('Invalid resource count')
    entries, payloads, position = [], [], ALIGNMENT
    for name, fmt, data in sorted(assets):
        if not ID.fullmatch(name) or not ID.fullmatch(fmt) or not data:
            raise ValueError('Invalid resource ID, format, or empty payload')
        entries.append(dict(id=name, format=fmt, offset=position, length=len(data), sha256=digest(data)))
        payloads.append((position, data))
        position = align(position + len(data))
    if len({entry['id'] for entry in entries}) != len(entries):
        raise ValueError('Duplicate resource ID')
    total = payloads[-1][0] + len(payloads[-1][1])
    directory = dict(reader_abi=1, profile=profile, entries=entries)
    encoded = canonical(directory)
    if len(encoded) > MAX_INDEX or total > 16 * 1024 * 1024:
        raise ValueError('Container limit exceeded')
    image = bytearray(b'\xff') * total
    image[:HEADER.size] = HEADER.pack(MAGIC, 1, len(encoded), len(entries), total,
                                     hashlib.sha256(encoded).digest(), bytes(8))
    image[HEADER.size:HEADER.size + len(encoded)] = encoded
    for offset, data in payloads:
        image[offset:offset + len(data)] = data
    return bytes(image)


def verify(image, required=None):
    if len(image) < HEADER.size or len(image) > 16 * 1024 * 1024:
        raise ValueError('Invalid container size')
    magic, version, index_len, count, total, expected, reserved = HEADER.unpack_from(image)
    if magic != MAGIC or version != 1 or reserved != bytes(8):
        raise ValueError('Unsupported container header')
    if total != len(image) or not 0 < index_len <= MAX_INDEX or not 0 < count <= MAX_ENTRIES:
        raise ValueError('Invalid container bounds')
    encoded = image[HEADER.size:HEADER.size + index_len]
    if hashlib.sha256(encoded).digest() != expected:
        raise ValueError('Directory hash mismatch')
    directory = json.loads(encoded)
    if not isinstance(directory, dict) or canonical(directory) != encoded or directory.get('reader_abi') != 1:
        raise ValueError('Invalid directory encoding or reader ABI')
    entries = directory.get('entries')
    if not isinstance(entries, list) or len(entries) != count:
        raise ValueError('Invalid resource count')
    previous_end, names = ALIGNMENT, set()
    for entry in entries:
        if not isinstance(entry, dict):
            raise ValueError('Invalid resource entry')
        name, fmt = entry.get('id'), entry.get('format')
        offset, length = entry.get('offset'), entry.get('length')
        if not isinstance(name, str) or not ID.fullmatch(name) or name in names:
            raise ValueError('Invalid/duplicate resource ID')
        if not isinstance(fmt, str) or not ID.fullmatch(fmt):
            raise ValueError('Invalid resource format')
        if type(offset) is not int or type(length) is not int or length <= 0:
            raise ValueError('Invalid resource bounds')
        if offset % ALIGNMENT or offset < previous_end or offset > total or length > total - offset:
            raise ValueError('Overlapping/out-of-range resource')
        if digest(image[offset:offset + length]) != entry.get('sha256'):
            raise ValueError('Resource hash mismatch: ' + name)
        if fmt == 'lv-font-conv-bin-1.5.3':
            verify_binary_font(image[offset:offset + length])
        previous_end = offset + length
        names.add(name)
    if previous_end != total:
        raise ValueError('Unexpected container tail')
    for name, fmt in (required or {}).items():
        if not any(entry['id'] == name and entry['format'] == fmt for entry in entries):
            raise ValueError('Required resource missing or incompatible: ' + name)
    return directory


def build(font, extra):
    if not re.fullmatch(r'actell_device_sans_(regular|medium)_(24|28)_[24]', font):
        raise ValueError('Invalid candidate name')
    report_path = ROOT / 'local/validation/caption-fonts' / (font + '.json')
    report = json.loads(report_path.read_text())
    binary = (ROOT / 'local/build/caption-fonts' / (font + '.font')).read_bytes()
    if digest(binary) != report['binary_sha256']:
        raise ValueError('Font input hash mismatch')
    metrics = verify_binary_font(binary)
    if any(metrics[key] != report[key] for key in metrics):
        raise ValueError('Binary font metrics mismatch')
    assets = [
        ('caption.font', 'lv-font-conv-bin-1.5.3', binary),
        ('caption.provenance', 'json', canonical(report)),
        ('licenses.ofl', 'text.utf8', (EXTENSION / 'licenses/SourceHanSans-OFL.txt').read_bytes()),
        ('source.manifest', 'json', (EXTENSION / 'manifest.json').read_bytes())]
    for name, fmt, filename in extra:
        assets.append((name, fmt, Path(filename).read_bytes()))
    image = create(assets, font)
    directory = verify(image, {'caption.font': 'lv-font-conv-bin-1.5.3'})
    reserve = align(max(256 * 1024, (len(binary) + 9) // 10))
    font_only = create(assets[:4], font)
    font_budget = align(len(font_only)) + reserve
    result = dict(profile=font, package_bytes=len(image), package_sha256=digest(image),
                  font_binary_bytes=len(binary), font_upgrade_reserve_bytes=reserve,
                  font_with_notices_and_reserve_bytes=font_budget,
                  proposed_resource_capacity_bytes=CAPACITY,
                  remaining_for_other_resources_bytes=CAPACITY - font_budget,
                  current_package_fits=len(image) <= CAPACITY,
                  package_with_font_reserve_fits=align(len(image)) + reserve <= CAPACITY,
                  font_table_bounds_checked=True, payload_hashes_checked=True,
                  reader_abi=1, firmware_reader_implemented=True,
                  target_loaded=False, partition_changed=False, flashed=False)
    output = ROOT / 'local/build/read-only-resources' / font
    output.mkdir(parents=True, exist_ok=True)
    (output / 'resources.bin').write_bytes(image)
    (output / 'directory.json').write_text(json.dumps(directory, indent=2) + '\n')
    (output / 'THIRD_PARTY_NOTICES.txt').write_bytes(assets[2][2])
    report_dir = ROOT / 'local/validation/read-only-resources'
    report_dir.mkdir(parents=True, exist_ok=True)
    (report_dir / (font + '.json')).write_text(json.dumps(result, indent=2) + '\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    builder = sub.add_parser('build')
    builder.add_argument('--font', required=True)
    builder.add_argument('--asset', nargs=3, action='append', default=[], metavar=('ID', 'FORMAT', 'FILE'))
    checker = sub.add_parser('verify')
    checker.add_argument('image', type=Path)
    checker.add_argument('--require', nargs=2, action='append', default=[], metavar=('ID', 'FORMAT'))
    args = parser.parse_args()
    if args.command == 'build':
        print(json.dumps(build(args.font, args.asset), indent=2))
    else:
        print(json.dumps(verify(args.image.read_bytes(), dict(args.require)), indent=2))


if __name__ == '__main__':
    main()
