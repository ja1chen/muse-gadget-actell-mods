#!/usr/bin/env python3
"""Run host tools with all generated files confined to the local workspace."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[2]
LOCAL = ROOT / 'local'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['test', 'avatar', 'console', 'mock'])
    args, extra = parser.parse_known_args()
    if extra and args.command != 'console':
        parser.error('extra arguments are supported only for console')
    for directory in ['logs/current', 'validation/current', 'cache/python', 'build/tmp']:
        (LOCAL / directory).mkdir(parents=True, exist_ok=True)
    env = {**os.environ, 'PYTHONPATH': str(ROOT / 'bridge/host'),
           'PYTHONPYCACHEPREFIX': str(LOCAL / 'cache/python'),
           'TMPDIR': str(LOCAL / 'build/tmp')}
    py = sys.executable
    if args.command == 'test':
        if not shutil.which('node') or not shutil.which('cc'):
            parser.error('test requires Node.js and a C compiler (cc)')
        commands = [
            [py, '-m', 'unittest', 'discover', '-s', 'bridge/host/tests', '-v'],
            [py, '-m', 'unittest', 'discover', '-s', 'bridge/extensions/muse-fonts/tools', '-p', 'test_*.py', '-v'],
            [py, '-m', 'unittest', 'discover', '-s', 'bridge/extensions/waveshare-216-controls/tools', '-p', 'test_*.py', '-v'],
            ['node', '--check', 'bridge/extensions/muse-fonts/tools/generate.js'],
            ['node', '--check', 'bridge/host/web_console/app.js'],
            ['node', '--check', 'bridge/host/web_console/presentation.js'],
            ['node', 'bridge/tools/validation/console-dom-review.js'],
            ['node', 'bridge/tools/validation/avatar-ui-order-review.js'],
            [py, 'bridge/tools/validation/run-observer-review.py'],
        ]
    elif args.command == 'avatar':
        commands = [[py, 'bridge/tools/validation/run-avatar-host.py']]
    elif args.command == 'console':
        commands = [[py, '-m', 'muse_client.web_console', *extra]]
    else:
        commands = [[py, '-m', 'muse_client.mock_server']]
    log_path = LOCAL / 'logs/current' / (args.command + '.log')
    with log_path.open('a', encoding='utf-8') as log:
        log.write('\nRun: ' + datetime.now(timezone.utc).isoformat() + '\n')
        for command in commands:
            title = '$ ' + ' '.join(command[1:] if command[0] == py else command)
            print(title, flush=True)
            log.write(title + '\n')
            with subprocess.Popen(command, cwd=ROOT, env=env, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, text=True, bufsize=1) as process:
                try:
                    for line in process.stdout:
                        print(line, end='', flush=True)
                        log.write(line)
                    status = process.wait()
                except KeyboardInterrupt:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                    return 130
            log.write(f'Exit: {status}\n')
            log.flush()
            if status:
                return status
    print(f'Log: {log_path.relative_to(ROOT)}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
