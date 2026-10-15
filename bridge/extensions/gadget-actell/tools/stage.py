#!/usr/bin/env python3
"""Stage the standalone component and exact hook patch into a separate SDK copy."""
import argparse, pathlib, shutil, subprocess
p=argparse.ArgumentParser();p.add_argument('sdk',type=pathlib.Path);p.add_argument('--check',action='store_true');a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[1]
patch=root.parents[1]/'patches/muse-gadget-sdk/actell-hook.patch'
subprocess.run(['git','apply','--check',str(patch)],cwd=a.sdk,check=True)
if not a.check:
    target=a.sdk/'esp32/components/actell_bridge'
    if target.exists(): raise SystemExit('Refusing to overwrite existing component')
    shutil.copytree(root/'components/actell_bridge',target)
    subprocess.run(['git','apply',str(patch)],cwd=a.sdk,check=True)
