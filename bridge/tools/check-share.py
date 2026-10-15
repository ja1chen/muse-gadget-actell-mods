#!/usr/bin/env python3
"""Check Git-visible files for private artifacts and common sensitive literals.

Reports filenames and line numbers only, never matching secret values. This is
a preflight aid; review the diff before publishing. It does not validate tokens.
"""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
PATTERNS = {
    'personal home path': re.compile(r'/(?:Users|home)/[A-Za-z0-9_.-]+/'),
    'private cloud object ID': re.compile(r'\b(?:libfile_|page_|file_)[0-9a-f]{24,}\b'),
    'signing team ID': re.compile(r'DEVELOPMENT_TEAM\s*=\s*"?[A-Z0-9]{10}\b'),
    'private key material': re.compile(r'^\s*(?:\+)?-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----'),
    'API credential shape': re.compile(r'\b(?:sk-(?:proj-|ant-)?[A-Za-z0-9_-]{24,}|gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,}|AKIA[A-Z0-9]{16})\b'),
}


def git(*args, **kwargs):
    return subprocess.run(['git', *args], cwd=ROOT, check=True, capture_output=True, **kwargs).stdout


def main():
    git('rev-parse', '--git-dir')
    paths = sorted(set(p for p in git('ls-files', '-z', '--cached', '--others', '--exclude-standard').split(b'\0') if p))
    tracked = [p for p in git('ls-files', '-z', '--cached').split(b'\0') if p]
    ignored = subprocess.run(['git', 'check-ignore', '--no-index', '-z', '--stdin'],
                             cwd=ROOT, input=b'\0'.join(tracked) + (b'\0' if tracked else b''),
                             capture_output=True)
    if ignored.returncode not in (0, 1):
        raise RuntimeError(ignored.stderr.decode())
    problems = []
    for path in ignored.stdout.split(b'\0'):
        if path:
            problems.append((path.decode(), 0, 'tracked despite ignore rule'))
    for raw in paths:
        name = raw.decode()
        path = ROOT / name
        if name.startswith('local/'):
            problems.append((name, 0, 'local-only file exposed to Git'))
            continue
        if name.endswith('.patch') and not name.startswith('bridge/patches/muse-gadget-sdk/'):
            problems.append((name, 0, 'patch workflow is restricted to Muse Gadget SDK'))
        if path.is_symlink():
            problems.append((name, 0, 'symlink requires explicit sharing review'))
            continue
        if not path.is_file():
            continue
        data = path.read_bytes()
        if b'\0' in data[:8192]:
            continue
        try:
            text = data.decode('utf-8')
        except UnicodeDecodeError:
            continue
        for number, line in enumerate(text.splitlines(), 1):
            for label, pattern in PATTERNS.items():
                if pattern.search(line):
                    problems.append((name, number, label))
    for name, number, label in problems:
        print(f'{name}:{number}: {label}')
    print(f'Checked {len(paths)} Git-visible files; {len(problems)} finding(s).')
    return bool(problems)


if __name__ == '__main__':
    raise SystemExit(main())
