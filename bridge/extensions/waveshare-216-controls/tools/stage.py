#!/usr/bin/env python3
"""Apply/check/reverse only the pinned, source-verified 2.16 controls patch."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[4]
EXT = Path(__file__).resolve().parents[1]
PATCH = ROOT / 'bridge/patches/muse-gadget-sdk/waveshare-216-controls.patch'
CAPTION = ROOT / 'bridge/patches/muse-gadget-sdk/caption-font.patch'
MANIFEST = EXT / 'manifest.json'
HEADER = Path('esp32/components/muse/boards/waveshare_216_controls.h')


def paths(patch):
    return [Path(line[6:]) for line in patch.read_text().splitlines() if line.startswith('--- a/')]


def git_apply(sdk, patch, *flags):
    subprocess.run(['git', 'apply', *flags, str(patch)], cwd=sdk, check=True,
                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('sdk', type=Path)
    p.add_argument('--check', action='store_true')
    p.add_argument('--reverse', action='store_true')
    a = p.parse_args()
    sdk = a.sdk.resolve()
    if not sdk.is_relative_to(ROOT / 'local/build') or not sdk.is_dir():
        p.error('SDK must be an existing copy under local/build')
    spec = json.loads(MANIFEST.read_text())
    archive = ROOT / spec['archive']
    if hashlib.sha256(archive.read_bytes()).hexdigest() != spec['archive_sha256']:
        p.error('Pinned SDK archive hash mismatch')
    touched = paths(PATCH)
    before = {str(f): hashlib.sha256((sdk / f).read_bytes()).hexdigest() for f in touched}
    # Compare whole source files, never guess from a matching patch hunk. The
    # caption variant is explicitly supported; all other patches touch disjoint
    # files. No config, generated header, log or firmware is inspected.
    temp_root = ROOT / 'local/build/tmp'
    temp_root.mkdir(parents=True, exist_ok=True)
    matched = None
    with tempfile.TemporaryDirectory(prefix='controls-stage-', dir=temp_root) as d:
        temp = Path(d)
        with tarfile.open(archive) as tar:
            originals = {f: tar.extractfile(str(f)).read() for f in set(touched + paths(CAPTION))}
        for caption in (False, True):
            for f, data in originals.items():
                (temp / f).parent.mkdir(parents=True, exist_ok=True)
                (temp / f).write_bytes(data)
            if caption:
                git_apply(temp, CAPTION)
            raw = {f: (temp / f).read_bytes() for f in touched}
            git_apply(temp, PATCH)
            patched = {f: (temp / f).read_bytes() for f in touched}
            for applied, expected in ((False, raw), (True, patched)):
                if all((sdk / f).read_bytes() == data for f, data in expected.items()):
                    matched = applied
        if matched is None:
            p.error('Unknown SDK source or conflicting changes; no files changed')
    header = sdk / HEADER
    source = EXT / 'include/waveshare_216_controls.h'
    if matched:
        if not header.is_file() or header.read_bytes() != source.read_bytes():
            p.error('Staged controls header missing or modified; no files changed')
    elif header.exists():
        p.error('Refusing to overwrite an existing controls header')
    if matched == (not a.reverse):
        print('Already applied' if matched else 'Already absent')
        return
    flags = ['--reverse'] if a.reverse else []
    git_apply(sdk, PATCH, '--check', *flags)
    if a.check:
        print('Verified pinned source; ' + ('reverse' if a.reverse else 'apply') + ' check passed')
        return
    git_apply(sdk, PATCH, *flags)
    if a.reverse:
        header.unlink()
    else:
        header.write_bytes(source.read_bytes())
    report = {'sdk_commit': spec['sdk_commit'], 'reversed': a.reverse,
              'before_sha256': before,
              'after_sha256': {str(f): hashlib.sha256((sdk / f).read_bytes()).hexdigest() for f in touched},
              'patch_sha256': hashlib.sha256(PATCH.read_bytes()).hexdigest(),
              'header_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
              'hardware_verified': False}
    dest = ROOT / 'local/validation/waveshare-216-controls'
    dest.mkdir(parents=True, exist_ok=True)
    (dest / ('rollback.json' if a.reverse else 'stage.json')).write_text(json.dumps(report, indent=2) + '\n')
    print('Reversed' if a.reverse else 'Applied')


if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as error:
        raise SystemExit('Patch preflight/application failed; inspect source compatibility') from error
