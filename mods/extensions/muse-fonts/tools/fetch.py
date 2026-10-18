#!/usr/bin/env python3
"""Fetch public pinned font inputs with TLS and SHA-256 verification."""
import hashlib
import json
from pathlib import Path
import urllib.request

HERE = Path(__file__).resolve().parents[1]
ROOT = HERE.parents[2]


def main():
    manifest = json.loads((HERE / 'manifest.json').read_text())
    cache = ROOT / 'local/cache'
    cache.mkdir(parents=True, exist_ok=True)
    for name, spec in manifest['files'].items():
        target = cache / name
        if target.exists():
            data = target.read_bytes()
        else:
            with urllib.request.urlopen(spec['url'], timeout=120) as response:
                data = response.read()
        if hashlib.sha256(data).hexdigest() != spec['sha256']:
            raise SystemExit('SHA-256 mismatch: ' + name)
        if not target.exists():
            target.write_bytes(data)
        print('Verified', name)


if __name__ == '__main__':
    main()
