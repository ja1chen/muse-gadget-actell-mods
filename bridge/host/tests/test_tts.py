"""Private configuration and native streaming regressions; synthetic inputs only."""
import importlib.util
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest
import base64

ROOT = Path(__file__).resolve().parents[3]
TOOL = ROOT / 'bridge/tools/apply-secrets.py'
EXT = ROOT / 'bridge/extensions/doubao-tts'
spec = importlib.util.spec_from_file_location('tts_secrets', TOOL)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class SecretsTests(unittest.TestCase):
    def setUp(self):
        # Confinement is tested separately; no real local credentials are read.
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name)
        self.source = self.path / 'synthetic.conf'
        self.target = self.path / 'sdkconfig'
        self.config = {
            'CONFIG_GADGET_SDK_TOKEN': 'mgst_' + 'SYNTHETIC_' + 'x' * 32 + 'A',
            'CONFIG_DOUBAO_TTS_API_KEY': 'SYNTHETIC_"quoted"_\\_key',
        }

    def write(self):
        self.source.write_text(module.format_config(self.config))

    def test_kv_comments_escapes_and_automatic_enable(self):
        text = '# synthetic comment\n\n' + module.format_config(self.config)
        self.assertEqual(module.parse_config(text), self.config)
        self.assertTrue(module.validate(module.parse_config(text))['CONFIG_DOUBAO_TTS'])
        for key in ['', None]:
            config = dict(self.config)
            if key is None:
                del config['CONFIG_DOUBAO_TTS_API_KEY']
            else:
                config['CONFIG_DOUBAO_TTS_API_KEY'] = key
            values = module.validate(module.parse_config(module.format_config(config)))
            self.assertFalse(values['CONFIG_DOUBAO_TTS'])
            self.assertEqual(values['CONFIG_DOUBAO_TTS_API_KEY'], '')

    def test_cached_values_replace_all_duplicates_and_preserve_unrelated(self):
        self.write()
        self.target.write_text('CONFIG_OTHER=y\nCONFIG_GADGET_SDK_TOKEN="old"\n'
                               'CONFIG_GADGET_SDK_TOKEN="duplicate"\n# CONFIG_DOUBAO_TTS is not set\n'
                               'CONFIG_DOUBAO_TTS_RESOURCE_ID="stale"\nCONFIG_DOUBAO_TTS_SPEAKER="stale"\n')
        module.apply(self.source, self.target)
        result = self.target.read_text()
        self.assertIn('CONFIG_OTHER=y\n', result)
        self.assertNotIn('CONFIG_DOUBAO_TTS_RESOURCE_ID', result)
        self.assertNotIn('CONFIG_DOUBAO_TTS_SPEAKER', result)
        self.assertEqual(result.count('CONFIG_GADGET_SDK_TOKEN='), 1)
        self.assertIn('CONFIG_DOUBAO_TTS=y\n', result)
        line = next(x for x in result.splitlines() if x.startswith('CONFIG_DOUBAO_TTS_API_KEY='))
        self.assertEqual(json.loads(line.split('=', 1)[1]), self.config['CONFIG_DOUBAO_TTS_API_KEY'])
        for p in [self.source, self.target]:
            self.assertEqual(stat.S_IMODE(p.stat().st_mode), 0o600)
        module.apply(self.source, self.target)
        self.assertEqual(self.target.read_text(), result)

    def test_import_preserves_transport_selection(self):
        self.write()
        for selection in ['CONFIG_DOUBAO_TTS_BIDIRECTIONAL=y\n',
                          '# CONFIG_DOUBAO_TTS_BIDIRECTIONAL is not set\n']:
            self.target.write_text(selection)
            module.apply(self.source, self.target)
            self.assertIn(selection, self.target.read_text())

    def test_disabled_clears_old_key(self):
        for omitted in [False, True]:
            self.config['CONFIG_DOUBAO_TTS_API_KEY'] = ''
            if omitted:
                del self.config['CONFIG_DOUBAO_TTS_API_KEY']
            self.write()
            self.target.write_text('CONFIG_DOUBAO_TTS=y\nCONFIG_DOUBAO_TTS_API_KEY="synthetic-old"\n')
            self.assertFalse(module.apply(self.source, self.target))
            result = self.target.read_text()
            self.assertIn('# CONFIG_DOUBAO_TTS is not set', result)
            self.assertIn('CONFIG_DOUBAO_TTS_API_KEY=""', result)
            self.assertNotIn('synthetic-old', result)

    def test_invalid_inputs_do_not_mutate_target(self):
        self.target.write_text('CONFIG_OTHER=y\n')
        good = module.format_config(self.config)
        bad = [
            good + 'CONFIG_DOUBAO_TTS_API_KEY="duplicate"\n', '[]',
            good + 'CONFIG_UNKNOWN="SYNTHETIC_PRIVATE"\n',
            good + 'CONFIG_DOUBAO_TTS=y\n',
            good + 'CONFIG_DOUBAO_TTS_BIDIRECTIONAL=y\n',
            good + 'CONFIG_DOUBAO_TTS_RESOURCE_ID="seed-icl-2.0"\n',
            good.replace('CONFIG_DOUBAO_TTS_API_KEY=', 'export CONFIG_DOUBAO_TTS_API_KEY='),
            good + 'CONFIG_DOUBAO_TTS_API_KEY=y; echo SYNTHETIC_PRIVATE\n',
            good.replace('CONFIG_DOUBAO_TTS_API_KEY=', 'CONFIG_DOUBAO_TTS_API_KEY=y # '),
            'CONFIG_GADGET_SDK_TOKEN="invalid"\n',
            module.format_config({**self.config, 'CONFIG_DOUBAO_TTS_API_KEY': False}),
            module.format_config({**self.config, 'CONFIG_DOUBAO_TTS_API_KEY': 'bad\nheader'}),
        ]
        for text in bad:
            self.source.write_text(text)
            with self.assertRaises(module.ConfigError):
                module.apply(self.source, self.target)
            self.assertEqual(self.target.read_text(), 'CONFIG_OTHER=y\n')

    def test_sdkconfig_can_exceed_secret_file_limit(self):
        self.write()
        self.target.write_text('# synthetic\n' * 10000)
        module.apply(self.source, self.target)
        self.assertGreater(self.target.stat().st_size, 65536)

    def test_path_escape_and_symlinks_rejected(self):
        with self.assertRaises(module.ConfigError):
            module.confined(self.source, 'local/credentials')
        link = self.path / 'alias'
        link.symlink_to(ROOT / 'local/credentials')
        with self.assertRaises(module.ConfigError):
            module.confined(link / 'unused.conf', 'local/credentials')

    def test_external_secrets_path_and_file_symlink(self):
        self.write()
        self.assertEqual(module.secrets_path(self.source), self.source.resolve())
        link = self.path / 'file-alias'
        link.symlink_to(self.source)
        with self.assertRaises(module.ConfigError):
            module.secrets_path(link)

    def cli_workspace(self):
        root = self.path / 'workspace'
        (root / 'bridge/tools').mkdir(parents=True)
        (root / 'bridge/tools/apply-secrets.py').write_bytes(TOOL.read_bytes())
        return root, root / 'bridge/tools/apply-secrets.py'

    def test_cli_imports_external_file_without_echoing_values(self):
        root, tool = self.cli_workspace()
        self.write()
        target = root / 'local/build/sdkconfig'
        result = subprocess.run([sys.executable, str(tool), '--secrets', str(self.source),
                                 '--sdkconfig', str(target)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('CONFIG_DOUBAO_TTS=y', target.read_text())
        self.assertNotIn(self.config['CONFIG_GADGET_SDK_TOKEN'], result.stdout + result.stderr)
        self.assertNotIn(self.config['CONFIG_DOUBAO_TTS_API_KEY'], result.stdout + result.stderr)

    def test_cli_requires_explicit_path_and_initializes_outside_project(self):
        root, tool = self.cli_workspace()
        result = subprocess.run([sys.executable, str(tool), '--init'], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((root / 'local').exists())
        external = self.path / 'private-config/device.conf'
        command = [sys.executable, str(tool), '--secrets', str(external), '--init']
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(stat.S_IMODE(external.stat().st_mode), 0o600)
        external.write_text('synthetic existing content')
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(external.read_text(), 'synthetic existing content')
        self.assertNotIn('synthetic existing content', result.stdout + result.stderr)

    def test_cli_errors_never_echo_invalid_payload(self):
        # A temporary synthetic workspace tests the real CLI, including path checks.
        root = self.path / 'workspace'
        (root / 'bridge/tools').mkdir(parents=True)
        (root / 'bridge/tools/apply-secrets.py').write_bytes(TOOL.read_bytes())
        secrets = root / 'local/credentials/synthetic.conf'
        secrets.parent.mkdir(parents=True)
        marker = 'SYNTHETIC_PRIVATE_MUST_NOT_APPEAR'
        secrets.write_text('{"' + marker + '":broken}')
        result = subprocess.run([sys.executable, str(root / 'bridge/tools/apply-secrets.py'),
                                 '--secrets', str(secrets), '--sdkconfig', str(root / 'local/build/sdkconfig')],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertNotIn(marker, result.stdout + result.stderr)
        self.assertNotIn('Traceback', result.stdout + result.stderr)

    def test_credential_audit_checks_index_and_encoded_worktree_without_values(self):
        root, tool = self.cli_workspace()
        subprocess.run(['git', 'init', '-q', str(root)], check=True, capture_output=True)
        self.write()
        tracked = root / 'public.txt'
        tracked.write_text(json.dumps(self.config['CONFIG_DOUBAO_TTS_API_KEY']))
        subprocess.run(['git', 'add', 'public.txt'], cwd=root, check=True, capture_output=True)
        tracked.write_text('safe working copy\n')
        (root / 'encoded.bin').write_bytes(base64.b64encode(self.config['CONFIG_GADGET_SDK_TOKEN'].encode()))
        leaked_name = self.config['CONFIG_GADGET_SDK_TOKEN'] + '.txt'
        (root / leaked_name).write_text(self.config['CONFIG_GADGET_SDK_TOKEN'])
        findings = module.audit_share(self.source, root)
        self.assertTrue(any(kind == 'index' and name == 'public.txt' for kind, name, _ in findings))
        self.assertTrue(any(kind == 'worktree' and name == 'encoded.bin' for kind, name, _ in findings))
        result = subprocess.run([sys.executable, str(tool), '--secrets', str(self.source), '--audit-share'],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        for value in self.config.values():
            self.assertNotIn(value, result.stdout + result.stderr)
        self.assertIn('<redacted>', result.stdout)
        self.assertNotIn('Traceback', result.stdout + result.stderr)

    def test_credential_audit_clean_repository(self):
        root, tool = self.cli_workspace()
        subprocess.run(['git', 'init', '-q', str(root)], check=True, capture_output=True)
        self.write()
        self.assertEqual(module.audit_share(self.source, root), [])
        result = subprocess.run([sys.executable, str(tool), '--secrets', str(self.source), '--audit-share'],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('0 matching location(s)', result.stdout)


class NativeStreamTests(unittest.TestCase):
    def test_bidirectional_protocol(self):
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / 'protocol'
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-g',
                            '-I', str(EXT / 'components/doubao_tts/include'),
                            str(EXT / 'components/doubao_tts/doubao_protocol.c'),
                            str(EXT / 'tests/protocol_test.c'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_disabled_provider_compiles_without_network_dependencies(self):
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            (directory / 'sdkconfig.h').write_text('/* synthetic disabled configuration */\n')
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(directory), '-I', str(EXT / 'components/doubao_tts/include'),
                            '-c', str(EXT / 'components/doubao_tts/doubao_tts.c'),
                            '-o', str(directory / 'disabled.o')], check=True)

    def test_fragmented_records_and_base64(self):
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / 'tts-stream'
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-g',
                            '-I', str(EXT / 'components/doubao_tts/include'),
                            str(EXT / 'components/doubao_tts/tts_stream.c'),
                            str(EXT / 'tests/stream_test.c'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
