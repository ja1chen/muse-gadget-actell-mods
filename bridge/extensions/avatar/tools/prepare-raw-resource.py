#!/usr/bin/env python3
"""Append reviewed AA bytes without moving existing resource payloads. No device I/O."""
from pathlib import Path
import argparse, hashlib, importlib.util, json, os

ROOT=Path(__file__).resolve().parents[4]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--current',type=Path,required=True)
p.add_argument('--raw',type=Path,required=True)
p.add_argument('--alignment-gap',type=Path,help='read-back bytes from old package end to aligned append offset')
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output=a.output.resolve()
if not a.output.is_relative_to(ROOT/'local'):p.error('Outputs must remain under local/')
os.umask(0o077);a.output.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('resource_pack',ROOT/'bridge/extensions/muse-fonts/tools/pack.py')
pack=importlib.util.module_from_spec(spec);spec.loader.exec_module(pack)
old=a.current.read_bytes();directory=pack.verify(old,{'caption.font':'lv-font-conv-bin-1.5.3'})
raw=a.raw.read_bytes();manifest=json.loads((ROOT/'bridge/extensions/avatar/source-manifest.json').read_text())['raw_aa_update']
if len(raw)!=manifest['raw_bytes'] or pack.digest(raw)!=manifest['raw_sha256']:p.error('Unexpected AA asset')
if any(e['id']=='avatar.aa' for e in directory['entries']):p.error('Avatar entry already exists; inspect before updating')
offset=pack.align(len(old));total=offset+len(raw)
if pack.align(total)>pack.CAPACITY:p.error('Insufficient shared-resource capacity')
entry=dict(id='avatar.aa',format='actell-avatar-aa-raw-v1',offset=offset,length=len(raw),sha256=pack.digest(raw))
directory['entries'].append(entry);encoded=pack.canonical(directory)
if len(encoded)>pack.MAX_INDEX or len(directory['entries'])>pack.MAX_ENTRIES:p.error('Directory capacity exceeded')
if offset>len(old):
    if not a.alignment_gap:p.error('Read back the alignment gap; do not assume erased Flash')
    gap=a.alignment_gap.read_bytes()
    if len(gap)!=offset-len(old):p.error('Alignment gap length mismatch')
else:gap=b''
image=bytearray(b'\xff')*total;image[:len(old)]=old;image[len(old):offset]=gap;image[offset:]=raw
image[:65536]=b'\xff'*65536
image[:64]=pack.HEADER.pack(pack.MAGIC,1,len(encoded),len(directory['entries']),total,hashlib.sha256(encoded).digest(),bytes(8))
image[64:64+len(encoded)]=encoded
pack.verify(image,{'caption.font':'lv-font-conv-bin-1.5.3','avatar.aa':'actell-avatar-aa-raw-v1'})
for e in directory['entries'][:-1]:
    lo=e['offset'];hi=lo+e['length'];assert image[lo:hi]==old[lo:hi]
(a.output/'resources.bin').write_bytes(image)
(a.output/'new-directory.bin').write_bytes(image[:65536])
(a.output/'old-directory.bin').write_bytes(old[:65536])
(a.output/'avatar-aa.raw').write_bytes(raw)
plan=dict(partition_offset=0x830000,partition_capacity=pack.CAPACITY,
          previous_bytes=len(old),package_bytes=total,avatar_offset=offset,avatar_bytes=len(raw),
          existing_payloads_unchanged=True,existing_offsets_unchanged=True,alignment_gap_preserved=True,
          old_sha256=pack.digest(old),new_sha256=pack.digest(image),
          free_bytes=pack.CAPACITY-total,write_order=['unused aligned avatar range','directory','inactive app'])
(a.output/'plan.json').write_text(json.dumps(plan,indent=2)+'\n');print(json.dumps(plan))
