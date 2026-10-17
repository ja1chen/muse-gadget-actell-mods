"""Run the real gadget HTTP receiver with bounded synthetic SDK responses."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
EXT = ROOT / 'bridge/extensions/gadget-actell'
COMP = EXT / 'components/actell_bridge'


class HTTPResultTests(unittest.TestCase):
    def test_actual_clock_requires_this_boot_sync(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'clock'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined',
                '-I', str(EXT / 'tests/http_stubs'), '-I', str(EXT / 'tests/stubs'),
                '-I', str(COMP), '-I', str(COMP / 'include'),
                str(COMP / 'actell_clock.c'), str(EXT / 'tests/clock_test.c'),
                '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_large_completed_results_and_fail_closed_boundaries(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'http-results'
            subprocess.run([
                'cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                '-I', str(EXT / 'tests/http_stubs'), '-I', str(EXT / 'tests/stubs'),
                '-I', str(COMP), '-I', str(COMP / 'include'),
                str(COMP / 'actell_http.c'), str(COMP / 'actell_buffer.c'),
                str(EXT / 'tests/http_result_test.c'), '-o', str(binary)
            ], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
