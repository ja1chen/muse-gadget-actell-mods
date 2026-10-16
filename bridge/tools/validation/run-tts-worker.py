#!/usr/bin/env python3
"""Run synthetic TTS worker tests against the SDK's resolved cJSON source."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
EXT = ROOT / 'bridge/extensions/doubao-tts'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk-root', required=True, type=Path)
    a = p.parse_args()
    cjson = a.sdk_root.resolve() / 'esp32/managed_components/espressif__cjson/cJSON'
    if not (cjson / 'cJSON.c').is_file():
        p.error('stage and resolve the fixed SDK dependencies first; cJSON source is required')
    tmp_root = ROOT / 'local/build/tmp'
    tmp_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=tmp_root) as directory:
        temp = Path(directory)
        for header in ['sdkconfig.h', 'esp_crt_bundle.h', 'esp_heap_caps.h', 'esp_http_client.h',
                       'esp_log.h', 'esp_random.h', 'esp_timer.h', 'esp_transport.h',
                       'esp_transport_ssl.h', 'esp_transport_ws.h', 'freertos/FreeRTOS.h',
                       'freertos/queue.h', 'freertos/task.h', 'freertos/idf_additions.h']:
            path = temp / header
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#include "tts_platform.h"\n')
        binary = temp / 'tts-worker'
        cjson_object = temp / 'cjson.o'
        # Upstream cJSON uses sprintf; newer macOS SDKs deprecate it. Keep
        # project sources under -Werror without rewriting third-party code.
        subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11',
                        '-Wno-deprecated-declarations', '-fsanitize=address,undefined', '-g',
                        '-I', str(cjson), '-c', str(cjson / 'cJSON.c'),
                        '-o', str(cjson_object)], check=True)
        for mode, external, test in [(0, 1, 'worker_test.c'), (1, 1, 'ws_worker_test.c'),
                                     (1, 0, 'ws_worker_test.c')]:
            sources = [EXT / 'tests' / test, EXT / 'components/doubao_tts/tts_stream.c']
            if mode:
                sources += [EXT / 'components/doubao_tts/doubao_ws.c',
                            EXT / 'components/doubao_tts/doubao_protocol.c']
            subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-g',
                            f'-DCONFIG_DOUBAO_TTS_BIDIRECTIONAL={mode}',
                            f'-DCONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY={external}', '-I', str(temp),
                            '-I', str(EXT / 'tests/fakes'), '-I', str(cjson),
                            '-I', str(EXT / 'components/doubao_tts/include'),
                            *map(str, sources), str(cjson_object),
                            '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
