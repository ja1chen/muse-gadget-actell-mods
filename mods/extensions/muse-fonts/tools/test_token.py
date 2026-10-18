"""Synthetic SDK-only import checks: no provider credentials enter font firmware."""
import importlib.util
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[4]
spec = importlib.util.spec_from_file_location('sdk_only_secrets', ROOT / 'mods/tools/apply-secrets.py')
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


class TokenOnlyTests(unittest.TestCase):
    def test_sdk_only_cannot_limit_audit_or_initialize_secrets(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'synthetic.conf'
            source.write_text('CONFIG_GADGET_SDK_TOKEN="SYNTHETIC_PRIVATE_INVALID"\n')
            for mode in ['--audit-share', '--init']:
                result = subprocess.run([sys.executable, str(ROOT / 'mods/tools/apply-secrets.py'),
                                         '--secrets', str(source), '--sdk-token-only', mode],
                                        capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('--sdk-token-only requires an import', result.stdout)
                self.assertNotIn('SYNTHETIC_PRIVATE_INVALID', result.stdout + result.stderr)
                self.assertEqual(source.read_text(), 'CONFIG_GADGET_SDK_TOKEN="SYNTHETIC_PRIVATE_INVALID"\n')

    def test_provider_key_excluded_and_font_flag_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            source, target = Path(directory) / 'synthetic.conf', Path(directory) / 'sdkconfig'
            token = 'mgst_' + 'A' * 43  # explicitly synthetic token-shaped vector
            source.write_text(tool.format_config({'CONFIG_GADGET_SDK_TOKEN': token,
                                                  'CONFIG_DOUBAO_TTS_API_KEY': 'SYNTHETIC_PROVIDER_KEY'}))
            target.write_text('CONFIG_ACTELL_CAPTION_FONT=y\nCONFIG_GADGET_SDK_TOKEN="old"\n'
                              'CONFIG_DOUBAO_TTS=y\nCONFIG_DOUBAO_TTS_API_KEY="SYNTHETIC_OLD_PROVIDER"\n')
            tool.apply(source, target, sdk_only=True)
            result = target.read_text()  # synthetic output only
            self.assertIn('CONFIG_ACTELL_CAPTION_FONT=y', result)
            self.assertIn(token, result)
            self.assertNotIn('DOUBAO', result)
            self.assertNotIn('SYNTHETIC_PROVIDER_KEY', result)
            self.assertEqual(stat.S_IMODE(target.stat().st_mode), 0o600)

    def test_sdk_only_requires_valid_sdk_token(self):
        for config in [{}, {'CONFIG_GADGET_SDK_TOKEN': 'invalid'},
                       {'CONFIG_GADGET_SDK_TOKEN': 'mgst_' + 'A' * 43, 'CONFIG_UNKNOWN': 'synthetic'}]:
            with self.assertRaises(tool.ConfigError):
                tool.validate(config, sdk_only=True)

    def test_cli_hides_values_and_excludes_provider_fields(self):
        parent = ROOT / 'local/build/tmp'
        parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=parent) as directory:
            source, target = Path(directory) / 'synthetic.conf', Path(directory) / 'sdkconfig'
            token = 'mgst_' + 'A' * 43
            source.write_text(tool.format_config({'CONFIG_GADGET_SDK_TOKEN': token,
                                                  'CONFIG_DOUBAO_TTS_API_KEY': 'SYNTHETIC_PRIVATE_PROVIDER'}))
            result = subprocess.run([sys.executable, str(ROOT / 'mods/tools/apply-secrets.py'),
                                     '--secrets', str(source), '--sdkconfig', str(target), '--sdk-token-only'],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0)
            self.assertNotIn(token, result.stdout + result.stderr)
            self.assertNotIn('SYNTHETIC_PRIVATE_PROVIDER', result.stdout + result.stderr)
            self.assertNotIn('DOUBAO', target.read_text())

    def test_cli_rejection_does_not_echo_input(self):
        parent = ROOT / 'local/build/tmp'
        parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=parent) as directory:
            source, target = Path(directory) / 'synthetic.conf', Path(directory) / 'sdkconfig'
            source.write_text('CONFIG_GADGET_SDK_TOKEN="SYNTHETIC_PRIVATE_INVALID"\n')
            result = subprocess.run([sys.executable, str(ROOT / 'mods/tools/apply-secrets.py'),
                                     '--secrets', str(source), '--sdkconfig', str(target), '--sdk-token-only'],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertNotIn('SYNTHETIC_PRIVATE_INVALID', result.stdout + result.stderr)
            self.assertFalse(target.exists())
