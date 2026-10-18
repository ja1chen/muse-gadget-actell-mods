#!/usr/bin/env python3
"""Compare every real glyph against converter pixels, then run sanitizer checks."""
import ctypes as C
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[4]
EXT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'local/build/caption-fonts/host'
REPORTS = ROOT / 'local/validation/caption-fonts'


class Font(C.Structure):
    _fields_ = [(x, C.c_void_p) for x in ['head', 'cmap', 'loca', 'glyf']] + \
               [(x, C.c_size_t) for x in ['cmap_size', 'loca_size', 'glyf_size']] + \
               [(x, C.c_uint32) for x in ['count', 'maps']] + \
               [(x, C.c_uint16) for x in ['line_height', 'baseline']] + \
               [(x, C.c_uint8) for x in ['bpp', 'xy_bits', 'wh_bits', 'advance_bits', 'loc_width', 'compression']]


class Glyph(C.Structure):
    _fields_ = [('id', C.c_uint32)] + [(x, C.c_uint16) for x in ['advance', 'width', 'height']] + \
               [(x, C.c_int16) for x in ['x', 'y']] + [('bits', C.c_void_p)] + \
               [(x, C.c_size_t) for x in ['bytes', 'bit_offset']]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--font', action='append', help='candidate ID; defaults to the native 16px profile')
    args = parser.parse_args()
    names = args.font or ['actell_device_sans_regular_16_4']
    for name in names:
        if not re.fullmatch(r'actell_device_sans_(regular|medium)_(16|24|28)_[24]', name):
            parser.error('Invalid font profile')
    BUILD.mkdir(parents=True, exist_ok=True)
    sources = [str(EXT / 'src' / name) for name in ['actell_font_core.c', 'actell_caption_layout.c']]
    common = ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-I' + str(EXT / 'include')]
    library = BUILD / 'libfont.dylib'
    subprocess.run(common + ['-shared', '-fPIC', *sources, '-o', str(library)], check=True)
    lib = C.CDLL(str(library))
    lib.actell_font_open.argtypes = [C.POINTER(Font), C.c_void_p, C.c_size_t]
    lib.actell_font_open.restype = C.c_bool
    lib.actell_font_glyph.argtypes = [C.POINTER(Font), C.c_uint32, C.POINTER(Glyph)]
    lib.actell_font_glyph.restype = C.c_bool
    lib.actell_font_decode.argtypes = [C.POINTER(Font), C.POINTER(Glyph), C.c_void_p, C.c_size_t, C.c_size_t]
    lib.actell_font_decode.restype = C.c_bool
    lib.actell_font_page.argtypes = [C.POINTER(Font), C.c_char_p, C.c_size_t, C.c_void_p, C.c_size_t, C.c_int, C.c_int]
    lib.actell_font_page.restype = C.c_bool
    lib.actell_font_normalize.argtypes = [C.POINTER(Font), C.c_char_p, C.c_void_p, C.c_size_t]
    lib.actell_font_normalize.restype = C.c_size_t
    results = []
    for oracle_file in [REPORTS / (name + '.oracle.json') for name in names]:
        name = oracle_file.name.removesuffix('.oracle.json')
        binary = ROOT / 'local/build/caption-fonts' / (name + '.font')
        data = C.create_string_buffer(binary.read_bytes())
        font = Font()
        assert lib.actell_font_open(C.byref(font), data, len(data) - 1), name
        native = '_16_' in name
        if native:
            assert font.line_height == 17 and font.baseline == 3
        glyph, pixels = Glyph(), C.create_string_buffer(4096)
        oracle = json.loads(oracle_file.read_text())
        assert len(oracle) == font.count - 1
        for expected in oracle:
            assert lib.actell_font_glyph(C.byref(font), expected['code'], C.byref(glyph)), (name, expected['code'])
            assert all(getattr(glyph, key) == expected[key] for key in ['advance', 'width', 'height', 'x', 'y'])
            if native:
                assert glyph.y >= -3 and glyph.y + glyph.height <= 14
            assert lib.actell_font_decode(C.byref(font), C.byref(glyph), pixels, 4096, glyph.width)
            assert hashlib.sha256(pixels.raw[:glyph.width * glyph.height]).hexdigest() == expected['sha256'], (name, expected['code'])
        tests = ['中文，标点。「你好！」English word boundary', 'WWW iii 0123456789', 'https://example.com/very/long/path/without/spaces', '第一行\n\n第三行', '未知😀𠀀组合e\u0301', '流式追加的文本会逐渐增长。English sentence.']
        out = C.create_string_buffer(400)
        for text in tests:
            raw = text.encode()
            for prefix in range(1, len(raw) + 1):
                try: raw[:prefix].decode()
                except UnicodeDecodeError: continue
                for at in range(prefix):
                    assert lib.actell_font_page(C.byref(font), raw[:prefix], at, out, 400, 240, 3)
                    shown = out.value.decode()
                    assert len(shown.split('\n')) <= 3
                    page_lines = shown.split('\n')
                    for line_number, line in enumerate(page_lines):
                        if line and any('\u2e80' <= c <= '\u9fff' for c in raw[:prefix].decode()):
                            assert line[0] not in '，。！？、）」』】》', (name, line)
                            if line_number + 1 < len(page_lines):
                                assert line[-1] not in '（「『【《', (name, line)
                        width = 0
                        for ch in line:
                            assert lib.actell_font_glyph(C.byref(font), ord(ch), C.byref(glyph))
                            width += glyph.advance
                        assert width <= 240, (name, line, width)
        assert not lib.actell_font_page(C.byref(font), b'', 0, out, 400, 240, 3)
        results.append(dict(profile=name, glyphs_compared=len(oracle), exact_pixel_hashes=True, layout_widths_checked=True))
        print(name, len(oracle), 'exact glyphs and pixel layout PASS', flush=True)
    sanitizer = BUILD / 'font-sanitizer'
    subprocess.run(common + ['-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', *sources,
                             str(EXT / 'tests/core_test.c'), '-o', str(sanitizer)], check=True)
    run = subprocess.run([str(sanitizer), str(ROOT / 'local/build/caption-fonts' / (names[0] + '.font'))],
                         check=True, capture_output=True, text=True, env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0'})
    print(run.stdout.strip())
    (REPORTS / 'font-host.json').write_text(json.dumps(dict(candidates=results, sanitizer=run.stdout.strip(), device_verified=False), indent=2) + '\n')


if __name__ == '__main__':
    main()
