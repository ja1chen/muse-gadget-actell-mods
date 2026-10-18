#!/usr/bin/env python3
"""Import private sdkconfig-style KV into a local sdkconfig without displaying values.

Only this tool should read the user's secrets file. Do not inspect that file,
generated sdkconfig, sdkconfig.old, generated headers, binaries or build logs
with commands that can expose credentials.
"""
import argparse
import json
import os
from pathlib import Path
import re
import stat
import tempfile
import base64
import subprocess
from urllib.parse import quote

ROOT = Path(__file__).resolve().parents[2]
TEMPLATE = {
    'CONFIG_GADGET_SDK_TOKEN': 'REPLACE_WITH_GADGET_SDK_TOKEN',
    'CONFIG_DOUBAO_TTS_API_KEY': 'REPLACE_WITH_DOUBAO_API_KEY',
}


class ConfigError(Exception):
    pass


def secrets_path(path):
    # The user chooses the input location, including outside this checkout.
    # Resolve directory aliases (e.g. macOS /var), but never follow a file link.
    path = Path(path).expanduser().absolute()
    if path.is_symlink():
        raise ConfigError('secrets file symlinks are not allowed')
    return path.parent.resolve() / path.name


def confined(path, area):
    path = Path(path).absolute()
    # Do not follow symlinks, including directory aliases within local/.
    if any(part.is_symlink() for part in (path, *path.parents)):
        raise ConfigError('symlinks are not allowed')
    resolved = path.resolve()
    if not resolved.is_relative_to((ROOT / area).resolve()):
        raise ConfigError(f'path must be inside {area}/')
    return resolved


def parse_config(text):
    result = {}
    for number, line in enumerate(text.splitlines(), 1):
        line = line.strip()
        disabled = re.fullmatch(r'# (CONFIG_[A-Z0-9_]+) is not set', line)
        if disabled:
            key, entry = disabled[1], False
        elif not line or line.startswith('#'):
            continue
        else:
            assignment = re.fullmatch(r'(CONFIG_[A-Z0-9_]+)\s*=\s*(.*)', line)
            if not assignment:
                raise ConfigError(f'line {number}: expected CONFIG_NAME=value')
            key, raw = assignment.groups()
            if raw in ('y', 'n'):
                entry = raw == 'y'
            else:
                try:
                    entry = json.loads(raw)
                except (ValueError, RecursionError):
                    raise ConfigError(f'line {number}: expected a double-quoted string') from None
                if not isinstance(entry, str):
                    raise ConfigError(f'line {number}: expected a double-quoted string')
        if key not in TEMPLATE:
            raise ConfigError(f'line {number}: unsupported field')
        if key in result:
            raise ConfigError(f'line {number}: duplicate field')
        result[key] = entry
    return result


def format_config(values):
    lines = []
    for key, entry in values.items():
        encoded = ('y' if entry else 'n') if type(entry) is bool else json.dumps(entry, ensure_ascii=True)
        lines.append(f'{key}={encoded}')
    return '\n'.join(lines) + '\n'


def value(text, name, maximum=2048):
    if (not isinstance(text, str) or not text or len(text) > maximum or
            any(ord(c) < 33 or ord(c) > 126 for c in text) or
            text.startswith(('REPLACE_', '<'))):
        raise ConfigError(f'{name}: invalid or placeholder value')
    return text


def validate(config, sdk_only=False):
    if 'CONFIG_GADGET_SDK_TOKEN' not in config or set(config) - set(TEMPLATE):
        raise ConfigError('missing or unsupported fields')
    token = value(config['CONFIG_GADGET_SDK_TOKEN'], 'CONFIG_GADGET_SDK_TOKEN')
    if not re.fullmatch(r'mgst_[A-Za-z0-9_-]{42}[AEIMQUYcgkosw048]', token):
        raise ConfigError('CONFIG_GADGET_SDK_TOKEN: expected an SDK token')
    if sdk_only:
        return {'CONFIG_GADGET_SDK_TOKEN': token}
    key = config.get('CONFIG_DOUBAO_TTS_API_KEY', '')
    if not isinstance(key, str):
        raise ConfigError('CONFIG_DOUBAO_TTS_API_KEY: expected a string')
    if key:
        key = value(key, 'CONFIG_DOUBAO_TTS_API_KEY', 512)
    return {
        'CONFIG_GADGET_SDK_TOKEN': token,
        'CONFIG_DOUBAO_TTS': bool(key),
        'CONFIG_DOUBAO_TTS_API_KEY': key,
    }


def private_read(path, maximum=65536):
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    with os.fdopen(fd, 'r', encoding='utf-8') as source:
        info = os.fstat(source.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid():
            raise ConfigError('input must be an owned regular file')
        os.fchmod(source.fileno(), 0o600)
        if info.st_size > maximum:
            raise ConfigError('input file is too large')
        return source.read(maximum + 1)


def atomic_write(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix='.private-', dir=path.parent)
    try:
        with os.fdopen(fd, 'w', encoding='utf-8') as target:
            os.fchmod(target.fileno(), 0o600)
            target.write(content)
            target.flush()
            os.fsync(target.fileno())
        os.replace(name, path)
    finally:
        if os.path.exists(name):
            os.unlink(name)


def apply(secrets, sdkconfig, sdk_only=False):
    config = parse_config(private_read(secrets))
    values = validate(config, sdk_only=sdk_only)
    previous = private_read(sdkconfig, 1024 * 1024) if sdkconfig.exists() else ''
    # Remove stale settings from the former configurable resource/speaker.
    managed_keys = [*values, 'CONFIG_DOUBAO_TTS_RESOURCE_ID', 'CONFIG_DOUBAO_TTS_SPEAKER']
    if sdk_only:
        managed_keys += ['CONFIG_DOUBAO_TTS', 'CONFIG_DOUBAO_TTS_API_KEY']
    managed = re.compile(r'^(?:# )?(' + '|'.join(map(re.escape, managed_keys)) + r')(?:=| is not set)')
    lines = [line for line in previous.splitlines() if not managed.match(line)]
    for key, entry in values.items():
        if type(entry) is bool:
            lines.append(f'{key}=y' if entry else f'# {key} is not set')
        else:
            lines.append(f'{key}={json.dumps(entry, ensure_ascii=True)}')
    atomic_write(sdkconfig, '\n'.join(lines) + '\n')
    return values.get('CONFIG_DOUBAO_TTS')


def audit_share(secrets, root=ROOT):
    """Compare secrets inside this tool; return metadata only, never values.

    Check both working files and index blobs, including staged contents that
    have already been removed from the working copy. Do not scan ignored local
    build artifacts, private logs, the secrets directory or unrelated history.
    """
    values = validate(parse_config(private_read(secrets)))
    patterns = set()
    for name in TEMPLATE:
        secret = values.get(name, '')
        if secret:
            patterns.update([secret.encode(), json.dumps(secret)[1:-1].encode(),
                             base64.b64encode(secret.encode()), quote(secret, safe='').encode()])

    def git(*args):
        return subprocess.run(['git', *args], cwd=root, check=True, capture_output=True).stdout

    findings = set()

    def scan(kind, name, content):
        for pattern in patterns:
            start = 0
            while (position := content.find(pattern, start)) != -1:
                # Even filenames might contain a leaked credential. Redact
                # those too; no source line or matched bytes leave this tool.
                safe_name = name
                for value_bytes in patterns:
                    safe_name = safe_name.replace(value_bytes.decode(), '<redacted>')
                findings.add((kind, safe_name, content[:position].count(b'\n') + 1))
                start = position + len(pattern)

    paths = set(git('ls-files', '-z', '--cached', '--others', '--exclude-standard').split(b'\0'))
    for raw in paths - {b''}:
        name = os.fsdecode(raw)
        path = root / name
        if path.is_symlink() or not path.resolve().is_relative_to(root.resolve()):
            raise ConfigError('repository symlink requires separate sharing review')
        if path.is_file():
            scan('worktree', name, path.read_bytes())
    blobs = {}
    for entry in git('ls-files', '--stage', '-z').split(b'\0'):
        if not entry:
            continue
        info, raw = entry.split(b'\t', 1)
        mode, blob, stage = info.split()
        if mode in (b'120000', b'160000'):
            continue
        if blob not in blobs:
            blobs[blob] = git('cat-file', 'blob', blob.decode('ascii'))
        scan('index', os.fsdecode(raw), blobs[blob])
    return sorted(findings)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--secrets', type=Path, required=True,
                   help='user-selected private KV file; may be outside the project; supports ~')
    p.add_argument('--sdk-token-only', action='store_true', help='import only SDK token; do not write provider keys or switches')
    p.add_argument('--init', action='store_true', help='create an owner-only placeholder file; never overwrite')
    p.add_argument('--audit-share', action='store_true',
                   help='compare credentials with Git-visible working files and index; report locations only')
    p.add_argument('--sdkconfig', type=Path, help='destination inside local/build/; updates cached values too')
    a = p.parse_args()
    try:
        secrets = secrets_path(a.secrets)
        if a.sdk_token_only and (a.init or a.audit_share):
            raise ConfigError('--sdk-token-only requires an import; cannot combine with --init or --audit-share')
        if a.audit_share:
            if a.init or a.sdkconfig:
                raise ConfigError('--audit-share cannot be combined with --init or --sdkconfig')
            findings = audit_share(secrets)
            for kind, name, line in findings:
                print(f'{kind} {json.dumps(name, ensure_ascii=True)}:{line}: private credential match (value hidden)')
            print(f'Credential audit: {len(findings)} matching location(s); values hidden.')
            return bool(findings)
        elif a.init:
            if a.sdkconfig:
                raise ConfigError('--init cannot be combined with --sdkconfig')
            secrets.parent.mkdir(parents=True, exist_ok=True)
            fd = os.open(secrets, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
            with os.fdopen(fd, 'w', encoding='utf-8') as target:
                target.write('# Private sdkconfig-style configuration. Do not share.\n' + format_config(TEMPLATE))
            print('Created private placeholder file. Edit it locally; do not paste its contents into chat.')
        else:
            if not a.sdkconfig:
                raise ConfigError('--sdkconfig is required')
            sdkconfig = confined(a.sdkconfig, 'local/build')
            if secrets == sdkconfig:
                raise ConfigError('input and output must differ')
            enabled = apply(secrets, sdkconfig, sdk_only=a.sdk_token_only)
            if a.sdk_token_only:
                print('Applied SDK token only; provider fields excluded; values hidden; permissions 0600.')
            else:
                print('Applied Gadget SDK configuration; TTS ' + ('enabled' if enabled else 'disabled') +
                      '; values hidden; permissions 0600. Reconfigure before building.')
        return 0
    except ConfigError as e:
        print(f'Configuration rejected: {e}. No values displayed.')
    except (OSError, ValueError, UnicodeError, TypeError, RecursionError, subprocess.SubprocessError):
        # Exception messages may embed private input, paths or value fragments.
        print('Configuration failed: check file access and KV format. No values displayed.')
    return 1


if __name__ == '__main__':
    raise SystemExit(main())
