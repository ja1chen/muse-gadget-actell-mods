"""Synthetic runner checks; no IDF execution, real secrets, network or hardware."""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from subprocess import CompletedProcess

spec = importlib.util.spec_from_file_location('font_build_transport', Path(__file__).with_name('build.py'))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class TransportBuildTests(unittest.TestCase):
    def run_transport(self, selected):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder).resolve()
            sdk = root / 'local/build/sdk'
            (sdk / 'esp32/components/doubao_tts').mkdir(parents=True)
            (sdk / 'esp32/components/doubao_tts/CMakeLists.txt').write_text('# synthetic\n')
            components = root / 'components'
            components.mkdir()
            argv = ['build.py', '--sdk', str(sdk), '--components-dir', str(components),
                    '--mode', 'on', '--with-tts', '--secrets', str(root / 'synthetic.conf')]
            if selected:
                argv += ['--tts-transport', selected]
            expected = selected or 'bidirectional'
            label = 'private-fonts-tts' + ('-http' if expected == 'http' else '')
            build = root / 'local/build/caption-firmware' / label
            check_inputs = []

            def fake_run(command, **kwargs):
                if 'apply-secrets.py' in str(command[1]):
                    self.assertNotIn('--sdk-token-only', command)
                    seed = (build / 'sdkconfig').read_text()
                    wanted = ('CONFIG_DOUBAO_TTS_BIDIRECTIONAL=y' if expected == 'bidirectional' else
                              '# CONFIG_DOUBAO_TTS_BIDIRECTIONAL is not set')
                    self.assertIn(wanted, seed)
                    return CompletedProcess(command, 0, 'TTS enabled;', '')
                if command[0] == 'synthetic-compiler':
                    check_inputs.append(kwargs['input'])
                    return CompletedProcess(command, 0, '', '')
                self.assertEqual(command[command.index('-B') + 1], str(build))
                (build / 'muse-gadget.bin').write_bytes(b'synthetic image')
                return CompletedProcess(command, 0)

            old_mask = os.umask(0o077)
            try:
                with patch.object(runner, 'ROOT', root), patch('sys.argv', argv), \
                     patch.dict(os.environ, {'IDF_PATH': str(root / 'idf')}), \
                     patch.object(runner.subprocess, 'run', side_effect=fake_run), \
                     patch.object(runner.shutil, 'which', return_value='synthetic-compiler'), \
                     contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(runner.main(), 0)
            finally:
                os.umask(old_mask)
            report = json.loads((root / 'local/validation/caption-fonts' / ('firmware-' + label + '.json')).read_text())
            self.assertEqual(report['tts_transport'], expected)
            self.assertIn(f'#if !!CONFIG_DOUBAO_TTS_BIDIRECTIONAL != {int(expected == "bidirectional")}', check_inputs[0])
            self.assertFalse(report['flashed'])
            self.assertEqual((build / 'sdkconfig').stat().st_mode & 0o077, 0)

    def test_default_is_bidirectional(self):
        self.run_transport(None)

    def test_explicit_bidirectional(self):
        self.run_transport('bidirectional')

    def test_http_uses_isolated_output_and_disabled_macro(self):
        self.run_transport('http')

    def test_transport_requires_combined_build(self):
        args = ['build.py', '--sdk', 'unused', '--components-dir', 'unused', '--mode', 'on',
                '--tts-transport', 'http']
        with patch('sys.argv', args), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as raised:
            runner.main()
        self.assertEqual(raised.exception.code, 2)
