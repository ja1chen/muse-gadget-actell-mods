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
    args = parser.parse_args()
    archive = ROOT / 'local/upstream/archives/muse-gadget-sdk-812c46f.tar.gz'
    assert hashlib.sha256(archive.read_bytes()).hexdigest() == '0a7548404b4aa25ad0cc3ad0f9fee1ad3a3245a03a14b02293da3a83bde69359'
    patches = [ROOT / 'bridge/patches/muse-gadget-sdk' / name for name in ['headless.patch', 'actell-hook.patch', 'caption-font.patch']]
    if args.with_tts:
        patches.append(ROOT / 'bridge/patches/muse-gadget-sdk/doubao-tts.patch')
    names = set()
    for patch in patches:
        names.update(re.findall(r'^--- a/(.+)$', patch.read_text(), re.M))
    local = ROOT / 'local/build/tmp'
    local.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='font-stage-', dir=local) as directory:
        sdk = Path(directory)
        with tarfile.open(archive) as tar:
            for name in sorted(names):
                p = sdk / name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes(tar.extractfile(name).read())
        for patch in patches[:2]:
            subprocess.run(['git', 'apply', '--check', str(patch)], cwd=sdk, check=True)
            subprocess.run(['git', 'apply', str(patch)], cwd=sdk, check=True)
        subprocess.run([sys.executable, str(EXT / 'tools/stage.py'), str(sdk), '--check'], check=True)
        subprocess.run([sys.executable, str(EXT / 'tools/stage.py'), str(sdk)], check=True)
        target = sdk / 'esp32/components/actell_fonts'
        files = [*EXT.glob('src/*.c'), *EXT.glob('include/*.h'), *EXT.glob('licenses/*.txt'), *[EXT / name for name in ['Kconfig','CMakeLists.txt','idf_component.yml','partitions.resources.csv']]]
        for source in files:
            assert source.read_bytes() == (target / source.relative_to(EXT)).read_bytes()
        check = subprocess.run([sys.executable, str(EXT / 'tools/stage.py'), str(sdk)], capture_output=True)
        assert check.returncode != 0
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
            assert 'heard_lines = 2' in ui and 'actell_caption_set_page' in ui
            session = (sdk / 'esp32/components/muse/muse_chat_session.cpp').read_text()
            assert 'pump_tts_pcm' in session and '#define TEXT_MAX 8192' in session
            check = subprocess.run([sys.executable, str(tts / 'tools/stage.py'), str(sdk)], capture_output=True)
            assert check.returncode != 0
    report = {'fixed_archive_verified':True, 'ordered_patches_apply':True, 'staged_files_equal_source':True,
              'duplicate_staging_rejected':True, 'files':len(files), 'tts_staged':args.with_tts}
    output = ROOT / 'local/validation/caption-fonts' / ('staging-with-tts.json' if args.with_tts else 'staging.json')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
