"""Streamed result bytes against Python's independent base64 oracle; synthetic only."""
import base64
from pathlib import Path
import subprocess
import tempfile
import tarfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
EXT = ROOT / 'mods/extensions/gadget-actell'
COMP = EXT / 'components/actell_bridge'


class ImageStreamTests(unittest.TestCase):
    def test_chunk_boundaries_padding_maximum_and_resume(self):
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            binary = tmp / 'stream'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', '-I', str(COMP / 'include'),
                str(COMP / 'actell_image_stream.c'), str(EXT / 'tests/image_stream_test.c'),
                '-o', str(binary)], check=True)
            for size in (1, 2, 3, 4, 5, 127, 133150, 256 * 1024):
                jpeg = bytes((i * 73 + 19) % 256 for i in range(size))
                expected = (b'{"method":"link.result","payload":{"format":"jpeg-base64",'
                            b'"data_base64":"' + base64.b64encode(jpeg) + b'","width":2}}')
                (tmp / 'jpeg').write_bytes(jpeg)
                (tmp / 'expected').write_bytes(expected)
                for block in (1, 7, 1024, 8192):
                    with self.subTest(size=size, block=block):
                        subprocess.run([str(binary), str(tmp / 'jpeg'),
                                        str(tmp / 'expected'), str(block)], check=True)

    def test_actual_sdk_sender_length_prefix_pause_resume_and_cleanup(self):
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            source = tmp / 'esp32/main/noise_control.cpp'
            source.parent.mkdir(parents=True)
            with tarfile.open(ROOT / 'local/upstream/archives/muse-gadget-sdk-812c46f.tar.gz') as tar:
                source.write_bytes(tar.extractfile('esp32/main/noise_control.cpp').read())
            subprocess.run(['git', 'init', '-q', str(tmp)], check=True)
            subprocess.run(['git', 'apply', '--include=esp32/main/noise_control.cpp',
                str(ROOT / 'mods/patches/muse-gadget-sdk/actell-hook.patch')], cwd=tmp, check=True)
            actual = source.read_text()
            actual = actual[actual.index('struct pending_json_body {'):actual.index('// ---- Tunnel stream multiplexing')]
            harness = (EXT / 'tests/image_sender_harness.cpp').read_text().replace('// ACTUAL_SENDER', actual)
            (tmp / 'sender.cpp').write_text(harness)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                '-I', str(COMP / 'include'), '-c', str(COMP / 'actell_image_stream.c'), '-o', str(tmp / 'stream.o')], check=True)
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                '-I', str(COMP / 'include'), str(tmp / 'sender.cpp'), str(tmp / 'stream.o'), '-o', str(tmp / 'sender')], check=True)
            for size in (1, 2, 3, 6140, 133150, 256 * 1024):
                jpeg = bytes((i * 73 + 19) % 256 for i in range(size))
                expected = (b'{"method":"link.result","payload":{"format":"jpeg-base64",'
                            b'"data_base64":"' + base64.b64encode(jpeg) + b'","width":2}}')
                (tmp / 'jpeg').write_bytes(jpeg)
                (tmp / 'expected').write_bytes(expected)
                with self.subTest(size=size):
                    subprocess.run([str(tmp / 'sender'), str(tmp / 'jpeg'), str(tmp / 'expected')], check=True)

    def test_actual_result_queue_ownership_on_rejection_and_delivery(self):
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            source = tmp / 'esp32/main/noise_control.cpp'
            source.parent.mkdir(parents=True)
            with tarfile.open(ROOT / 'local/upstream/archives/muse-gadget-sdk-812c46f.tar.gz') as tar:
                source.write_bytes(tar.extractfile('esp32/main/noise_control.cpp').read())
            subprocess.run(['git', 'init', '-q', str(tmp)], check=True)
            subprocess.run(['git', 'apply', '--include=esp32/main/noise_control.cpp',
                str(ROOT / 'mods/patches/muse-gadget-sdk/actell-hook.patch')], cwd=tmp, check=True)
            actual = source.read_text()
            pending = actual[actual.index('struct pending_result {'):actual.index('static QueueHandle_t s_result_q')]
            queue = actual[actual.index('static void queue_result('):actual.index('static void noise_ota_status(')]
            harness = (EXT / 'tests/image_queue_harness.cpp').read_text()
            harness = harness.replace('// ACTUAL_PENDING_RESULT', pending).replace('// ACTUAL_QUEUE_RESULT', queue)
            (tmp / 'queue.cpp').write_text(harness)
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                '-I', str(COMP / 'include'), str(tmp / 'queue.cpp'), '-o', str(tmp / 'queue')], check=True)
            subprocess.run([str(tmp / 'queue')], check=True)
