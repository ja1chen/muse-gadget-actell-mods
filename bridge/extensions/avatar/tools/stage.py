#!/usr/bin/env python3
"""Stage the original renderer in the SDK's existing custom-avatar override.
Does not patch CMake, build, flash or contact a device. Existing overrides are
refused; remove/move one manually before replacing it.
"""
from pathlib import Path
import argparse
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser();p.add_argument('--sdk-root',type=Path,required=True)
    a=p.parse_args();component=a.sdk_root/'esp32/components/muse'
    if not (component/'muse_pixel.h').is_file():p.error('SDK root must contain esp32/components/muse/muse_pixel.h')
    target=component/'avatar'
    if any((target/n).exists() for n in ['muse_pixel.c','actell_avatar.h']):p.error('An avatar override exists; move it aside first. Nothing changed.')
    target.mkdir(exist_ok=True)
    # Single source translation unit matches the unmodified CMake override.
    combined=(ROOT/'src/actell_avatar.c').read_text()+'\n'+(ROOT/'src/muse_pixel.c').read_text()
    (target/'actell_avatar.h').write_bytes((ROOT/'include/actell_avatar.h').read_bytes())
    (target/'muse_pixel.c').write_text(combined)
    print('Staged original avatar at '+str(target)+'. No build or flash was run.')
if __name__=='__main__':main()
