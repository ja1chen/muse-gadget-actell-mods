#!/usr/bin/env python3
"""Check patch order and exact staged source against minimal verified SDK files."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[4]
EXT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--with-tts', action='store_true', help='also stage and verify the optional TTS extension')
    parser.add_argument('--with-avatar', action='store_true', help='stage v13 before fonts and verify combined UI')
    args = parser.parse_args()
    archive = ROOT / 'local/upstream/archives/muse-gadget-sdk-812c46f.tar.gz'
    assert hashlib.sha256(archive.read_bytes()).hexdigest() == '0a7548404b4aa25ad0cc3ad0f9fee1ad3a3245a03a14b02293da3a83bde69359'
    patches = [ROOT / 'bridge/patches/muse-gadget-sdk' / name for name in ['headless.patch', 'actell-hook.patch', 'caption-font.patch']]
    if args.with_tts:
        patches.append(ROOT / 'bridge/patches/muse-gadget-sdk/doubao-tts.patch')
    if args.with_avatar:
        patches.append(ROOT / 'bridge/patches/muse-gadget-sdk/avatar.patch')
    names = set()
    for patch in patches:
        names.update(re.findall(r'^--- a/(.+)$', patch.read_text(), re.M))
    local = ROOT / 'local/build/tmp'
    local.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='font-stage-', dir=local) as directory:
        sdk = Path(directory)
        with tarfile.open(archive) as tar:
            original_ui = tar.extractfile('esp32/components/muse/muse_ui.c').read().decode()
            for name in sorted(names):
                p = sdk / name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes(tar.extractfile(name).read())
        for patch in patches[:2]:
            subprocess.run(['git', 'apply', '--check', str(patch)], cwd=sdk, check=True)
            subprocess.run(['git', 'apply', str(patch)], cwd=sdk, check=True)
        if args.with_avatar:
            subprocess.run([sys.executable, str(ROOT / 'bridge/extensions/avatar/tools/stage.py'),
                            '--sdk-root', str(sdk)], check=True)
            # A valid avatar journal must not bless later, unrelated UI changes.
            ui = sdk / 'esp32/components/muse/muse_ui.c'
            installed = ui.read_bytes()
            ui.write_bytes(installed + b'\n/* unrelated change */\n')
            rejected = subprocess.run([sys.executable, str(EXT / 'tools/stage.py'), str(sdk), '--check'],
                                      capture_output=True)
            assert rejected.returncode and not (sdk / 'esp32/components/actell_fonts').exists()
            ui.write_bytes(installed)
        subprocess.run([sys.executable, str(EXT / 'tools/stage.py'), str(sdk), '--check'], check=True)
        subprocess.run([sys.executable, str(EXT / 'tools/stage.py'), str(sdk)], check=True)
        ui = (sdk / 'esp32/components/muse/muse_ui.c').read_text()
        first = original_ui.index('    int cap_h = 2 * lv_font_get_line_height(&lv_font_unscii_16)')
        last = original_ui.index('    s_big_y =', first)
        assert original_ui[first:last] in ui  # original caption/meter/character geometry
        assert "int cw = lv_font_get_glyph_width(&lv_font_unscii_16, 'M', ' ');" in ui
        assert 'int h = 3 * pitch - CAPTION_LINE_SPACE;' in ui
        assert 'heard_lines = 2' not in ui and 'int old_h =' not in ui
        target = sdk / 'esp32/components/actell_fonts'
        files = [*EXT.glob('src/*.c'), *EXT.glob('include/*.h'), *EXT.glob('licenses/*.txt'), *[EXT / name for name in ['Kconfig','CMakeLists.txt','idf_component.yml','partitions.resources.csv']]]
        for source in files:
            assert source.read_bytes() == (target / source.relative_to(EXT)).read_bytes()
        check = subprocess.run([sys.executable, str(EXT / 'tools/stage.py'), str(sdk)], capture_output=True)
        assert check.returncode != 0
        if args.with_avatar:
            ui = (sdk / 'esp32/components/muse/muse_ui.c').read_text()
            assert 'l->px = MUSE_LAYOUT_PX * cell;' in ui
            assert 'int h = 3 * pitch - CAPTION_LINE_SPACE;' in ui and 'actell_caption_set_page' in ui
            assert 'static lv_area_t r[MUSE_PX_H]' in ui
        if args.with_tts:
            tts = ROOT / 'bridge/extensions/doubao-tts'
            for extra in [['--check'], []]:
                subprocess.run([sys.executable, str(tts / 'tools/stage.py'), str(sdk), *extra], check=True)
            source = tts / 'components/doubao_tts'
            for path in source.rglob('*'):
                if path.is_file():
                    assert path.read_bytes() == (sdk / 'esp32/components/doubao_tts' / path.relative_to(source)).read_bytes()
            cmake = (sdk / 'esp32/components/muse/CMakeLists.txt').read_text()
            assert 'doubao_tts' in cmake and 'actell_fonts' in cmake
            ui = (sdk / 'esp32/components/muse/muse_ui.c').read_text()
            assert 'int h = 3 * pitch - CAPTION_LINE_SPACE;' in ui and 'actell_caption_set_page' in ui
            session = (sdk / 'esp32/components/muse/muse_chat_session.cpp').read_text()
            assert 'pump_tts_pcm' in session and '#define TEXT_MAX 8192' in session
            check = subprocess.run([sys.executable, str(tts / 'tools/stage.py'), str(sdk)], capture_output=True)
            assert check.returncode != 0
    report = {'fixed_archive_verified':True, 'ordered_patches_apply':True, 'staged_files_equal_source':True,
              'duplicate_staging_rejected':True, 'files':len(files), 'tts_staged':args.with_tts,
              'avatar_staged':args.with_avatar}
    output = ROOT / 'local/validation/caption-fonts' / ('staging' + ('-avatar' if args.with_avatar else '') + ('-with-tts' if args.with_tts else '') + '.json')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
