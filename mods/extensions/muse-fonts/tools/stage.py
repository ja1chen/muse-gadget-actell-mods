#!/usr/bin/env python3
"""Stage the resource reader and checked SDK hooks; never touch credentials or Flash."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[4]
EXTENSION = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sdk', type=Path)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    sdk = args.sdk.resolve()
    if not sdk.is_relative_to(ROOT / 'local/build'):
        parser.error('Only a staged SDK under local/build may be modified')
    archive = ROOT / 'local/upstream/archives/muse-gadget-sdk-812c46f.tar.gz'
    if hashlib.sha256(archive.read_bytes()).hexdigest() != '0a7548404b4aa25ad0cc3ad0f9fee1ad3a3245a03a14b02293da3a83bde69359':
        parser.error('Wrong SDK archive hash')
    # Accept the pinned UI or the exact reversible avatar adaptation only.
    with tarfile.open(archive) as tar:
        baseline = tar.extractfile('esp32/components/muse/muse_ui.c').read()
        current = (sdk / 'esp32/components/muse/muse_ui.c').read_bytes()
        record = sdk / 'esp32/components/muse/.actell-avatar-v2-stage.json'
        avatar_ui = None
        if record.is_file():
            edit = json.loads(record.read_text())['edits']['muse_ui.c']
            if edit['before'].encode() == baseline and hashlib.sha256(current).hexdigest() == edit['after']:
                avatar_ui = current
        if current != baseline and current != avatar_ui:
            parser.error('UI differs from the pinned SDK (or hooks were already applied)')
    target = sdk / 'esp32/components/actell_fonts'
    if target.exists():
        parser.error('Refusing to overwrite an existing staged resource component')
    patch = ROOT / 'mods/patches/muse-gadget-sdk/caption-font.patch'
    subprocess.run(['git', 'apply', '--check', str(patch)], cwd=sdk, check=True)
    if not args.check:
        # Copy only implementation; downloaded/generated assets never enter the app component.
        target.mkdir()
        for name in ['include', 'src', 'licenses']:
            shutil.copytree(EXTENSION / name, target / name)
        for name in ['CMakeLists.txt', 'Kconfig', 'idf_component.yml', 'partitions.resources.csv']:
            shutil.copy2(EXTENSION / name, target / name)
        subprocess.run(['git', 'apply', str(patch)], cwd=sdk, check=True)


if __name__ == '__main__':
    main()
