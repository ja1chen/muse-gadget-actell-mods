#!/usr/bin/env python3
"""Stage the optional TTS component and Muse session hook into a pinned SDK copy."""
import argparse
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PATCH = ROOT.parents[1] / 'patches/muse-gadget-sdk/doubao-tts.patch'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('sdk', type=Path)
    p.add_argument('--check', action='store_true')
    a = p.parse_args()
    target = a.sdk / 'esp32/components/doubao_tts'
    if target.exists():
        p.error('refusing to overwrite existing TTS component')
    subprocess.run(['git', 'apply', '--check', str(PATCH)], cwd=a.sdk, check=True)
    if not a.check:
        shutil.copytree(ROOT / 'components/doubao_tts', target)
        subprocess.run(['git', 'apply', str(PATCH)], cwd=a.sdk, check=True)


if __name__ == '__main__':
    main()
