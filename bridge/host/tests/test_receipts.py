"""Actual asynchronous adapter and guard, with synthetic scheduler/transport."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
EXT = ROOT / 'bridge/extensions/gadget-actell'
COMP = EXT / 'components/actell_bridge'
CJSON = ROOT / 'local/build/slan-complete-sdk/esp32/managed_components/espressif__cjson/cJSON'


class ReceiptAdapterTests(unittest.TestCase):
    @unittest.skipUnless((CJSON / 'cJSON.c').is_file(), 'Prepare SDK cJSON dependency for adapter integration harness')
    def test_actual_adapter_duplicate_delivery_and_capacity(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'receipt-adapter'
            cjson_object = Path(directory) / 'cJSON.o'
            # Keep upstream macOS sprintf deprecations separate from our -Werror.
            subprocess.run(['cc', '-std=c11', '-Wno-deprecated-declarations',
                            '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                            '-c', str(CJSON / 'cJSON.c'), '-o', str(cjson_object)], check=True)
            subprocess.run([
                'cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                '-I', str(CJSON), '-I', str(EXT / 'tests/adapter_stubs'),
                '-I', str(EXT / 'tests/http_stubs'), '-I', str(EXT / 'tests/stubs'),
                '-I', str(COMP), '-I', str(COMP / 'include'),
                str(EXT / 'tests/receipt_adapter_test.c'), str(COMP / 'actell_guard.c'),
                str(cjson_object), '-lm', '-o', str(binary)
            ], check=True)
            subprocess.run([str(binary)], check=True)
