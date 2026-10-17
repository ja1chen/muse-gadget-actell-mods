"""Host integration tests against the hash-verified pinned SDK source."""
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[4]
EXT = Path(__file__).resolve().parents[1]
ARCHIVE = ROOT / 'local/upstream/archives/muse-gadget-sdk-812c46f.tar.gz'
PATCH = ROOT / 'bridge/patches/muse-gadget-sdk/waveshare-216-controls.patch'


class ControlsTests(unittest.TestCase):
    def setUp(self):
        self.assertEqual(hashlib.sha256(ARCHIVE.read_bytes()).hexdigest(),
                         '0a7548404b4aa25ad0cc3ad0f9fee1ad3a3245a03a14b02293da3a83bde69359')
        tmp = ROOT / 'local/build/tmp'
        tmp.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix='controls-test-', dir=tmp)
        self.addCleanup(self.temp.cleanup)
        self.sdk = Path(self.temp.name)
        with tarfile.open(ARCHIVE) as tar:
            for member in tar:
                if member.isfile() and member.name.startswith(('esp32/components/muse/', 'esp32/main/')):
                    target = self.sdk / member.name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(tar.extractfile(member).read())

    def stage(self, *flags, success=True):
        r = subprocess.run([sys.executable, str(EXT / 'tools/stage.py'), str(self.sdk), *flags],
                           capture_output=True, text=True)
        self.assertEqual(r.returncode == 0, success, r.stderr)
        return r.stdout

    def test_apply_idempotent_conflict_and_rollback(self):
        self.assertIn('check passed', self.stage('--check'))
        self.assertFalse((self.sdk / 'esp32/components/muse/boards/waveshare_216_controls.h').exists())
        self.stage()
        self.assertIn('Already applied', self.stage())
        header = self.sdk / 'esp32/components/muse/boards/waveshare_216_controls.h'
        original = header.read_bytes()
        header.write_bytes(original + b'\n/* conflicting header */\n')
        self.stage('--check', success=False)
        self.stage('--reverse', success=False)
        self.assertEqual(header.read_bytes(), original + b'\n/* conflicting header */\n')
        header.write_bytes(original)
        self.stage('--reverse', '--check')
        self.stage('--reverse')
        self.assertIn('Already absent', self.stage('--reverse'))
        p = self.sdk / 'esp32/components/muse/muse_ui.c'
        p.write_text(p.read_text() + '\n/* conflicting local change */\n')
        before = p.read_bytes()
        self.stage(success=False)
        self.assertEqual(p.read_bytes(), before)

    def test_caption_compatibility(self):
        subprocess.run(['git', 'apply', str(ROOT / 'bridge/patches/muse-gadget-sdk/caption-font.patch')],
                       cwd=self.sdk, check=True)
        p = self.sdk / 'esp32/components/muse/muse_ui.c'
        before = p.read_bytes()
        self.stage(); self.stage('--check'); self.stage('--reverse')
        self.assertEqual(p.read_bytes(), before)

    def test_all_existing_patch_hooks_preserved(self):
        for name in ('headless', 'actell-hook', 'caption-font', 'doubao-tts'):
            subprocess.run(['git', 'apply', str(ROOT / ('bridge/patches/muse-gadget-sdk/' + name + '.patch'))],
                           cwd=self.sdk, check=True)
        before = {p.relative_to(self.sdk): p.read_bytes() for p in self.sdk.rglob('*') if p.is_file()}
        self.stage(); self.stage('--reverse')
        after = {p.relative_to(self.sdk): p.read_bytes() for p in self.sdk.rglob('*') if p.is_file()}
        self.assertEqual(before, after)

    def test_avatar_caption_tts_combination_and_unknown_ui_rejection(self):
        for name in ('headless', 'actell-hook'):
            subprocess.run(['git', 'apply', str(ROOT / ('bridge/patches/muse-gadget-sdk/' + name + '.patch'))],
                           cwd=self.sdk, check=True)
        subprocess.run([sys.executable, str(ROOT / 'bridge/extensions/avatar/tools/stage.py'),
                        '--sdk-root', str(self.sdk)], check=True, capture_output=True)
        for name in ('caption-font', 'doubao-tts'):
            subprocess.run(['git', 'apply', str(ROOT / ('bridge/patches/muse-gadget-sdk/' + name + '.patch'))],
                           cwd=self.sdk, check=True)
        before = {p.relative_to(self.sdk): p.read_bytes() for p in self.sdk.rglob('*') if p.is_file()}
        self.stage('--check'); self.stage(); self.stage('--check'); self.stage('--reverse')
        after = {p.relative_to(self.sdk): p.read_bytes() for p in self.sdk.rglob('*') if p.is_file()}
        self.assertEqual(before, after)
        ui = self.sdk / 'esp32/components/muse/muse_ui.c'
        ui.write_bytes(ui.read_bytes() + b'\n/* unknown local UI change */\n')
        modified = ui.read_bytes()
        self.stage('--check', success=False)
        self.assertEqual(ui.read_bytes(), modified)

    def test_reducer_with_real_input_handlers(self):
        self.stage()
        muse = self.sdk / 'esp32/components/muse'
        macros = []
        for name in ('muse_board.h', 'muse_pmu.h'):
            macros.extend(line for line in (muse / name).read_text().splitlines()
                          if line.startswith(('#define MUSE_BTN_', '#define MUSE_PMU_KEY_')))
        (self.sdk / 'sdk_events.h').write_text('\n'.join(macros) + '\n')
        source = (muse / 'muse_input.c').read_text()
        start = source.index('static bool s_power_down, s_power_swallow;')
        end = source.index('static void input_task(', start)
        (self.sdk / 'sdk_input.inc').write_text(source[start:end])
        binary = self.sdk / 'controls-test'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-g',
                        '-fsanitize=address,undefined', '-DCONFIG_MUSE_BOARD_WAVESHARE_S3_216=1',
                        '-I'+str(self.sdk), '-I'+str(EXT / 'include'),
                        str(EXT / 'tests/controls_test.c'), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True, env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0'})

    def test_real_pmu_with_fake_i2c(self):
        self.stage()
        muse = self.sdk / 'esp32/components/muse'
        fake = self.sdk / 'fake'
        (fake / 'driver').mkdir(parents=True)
        # Copy only the two production source files into a stub include tree.
        for name in ('muse_pmu.c', 'muse_pmu.h'):
            (fake / name).write_bytes((muse / name).read_bytes())
        (fake / 'esp_err.h').write_text("""#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE -2
""")
        (fake / 'driver/i2c_master.h').write_text("""#pragma once
#include "esp_err.h"
typedef void *i2c_master_bus_handle_t;
typedef void *i2c_master_dev_handle_t;
typedef struct { int dev_addr_length, device_address, scl_speed_hz; } i2c_device_config_t;
#define I2C_ADDR_BIT_LEN_7 7
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t, const i2c_device_config_t *, i2c_master_dev_handle_t *);
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t, const uint8_t *, size_t, uint8_t *, size_t, int);
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t, const uint8_t *, size_t, int);
""")
        (fake / 'muse_state.h').write_text("""#pragma once
#include "esp_err.h"
typedef struct { bool usb, charging; int battery_pct, battery_mv; } muse_power_t;
""")
        (fake / 'esp_check.h').write_text("""#pragma once
#define ESP_RETURN_ON_ERROR(expr, tag, ...) do { int e = (expr); if (e) return e; } while (0)
#define ESP_RETURN_ON_FALSE(expr, code, tag, ...) do { if (!(expr)) return code; } while (0)
""")
        (fake / 'esp_log.h').write_text('#define ESP_LOGI(tag, ...) ((void)(tag))\n')
        binary = self.sdk / 'pmu-test'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-I'+str(fake),
                        str(fake / 'muse_pmu.c'), str(EXT / 'tests/pmu_test.c'),
                        '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True, env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0'})

    def test_ring_gaps_and_other_board_clip(self):
        self.stage()
        source = (self.sdk / 'esp32/components/muse/muse_ui.c').read_text()
        start = source.index('static void on_ring_draw(')
        end = source.index('static void build_screen(', start)
        (self.sdk / 'sdk_ring.inc').write_text(source[start:end])
        for board in (0, 1):
            binary = self.sdk / ('ring-test-' + str(board))
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-DCONFIG_MUSE_BOARD_WAVESHARE_S3_216='+str(board),
                            '-I'+str(self.sdk), str(EXT / 'tests/ring_test.c'), '-lm', '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True, env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0'})

    def test_real_board_polling_and_wait(self):
        self.stage()
        muse = self.sdk / 'esp32/components/muse'
        macros = []
        for name in ('muse_board.h', 'muse_pmu.h'):
            macros.extend(line for line in (muse / name).read_text().splitlines()
                          if line.startswith(('#define MUSE_BTN_', '#define MUSE_PMU_KEY_')))
        (self.sdk / 'sdk_events.h').write_text('\n'.join(macros)+'\n')
        source = (muse / 'boards/board_waveshare_s3_216.c').read_text()
        start = source.index('static unsigned poll_buttons(')
        end = source.index('static const muse_board_t s_board', start)
        (self.sdk / 'sdk_board.inc').write_text(source[start:end])
        binary = self.sdk / 'board-test'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-I'+str(self.sdk), '-I'+str(EXT / 'include'),
                        str(EXT / 'tests/board_test.c'), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True, env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0'})

    def test_gray_controls_palette_and_original_recording_highlight(self):
        self.stage()
        source = (self.sdk / 'esp32/components/muse/muse_ui.c').read_text()
        colors = [line for line in source.splitlines() if line.startswith('#define COLOR_')]
        (self.sdk / 'sdk_colors.h').write_text('\n'.join(colors)+'\n')
        start = source.index('static lv_obj_t *make_control_icon(')
        end = source.index('static void build_overlays(', start)
        (self.sdk / 'sdk_palette.inc').write_text(source[start:end])
        for board in (0, 1):
            binary = self.sdk / ('palette-test-'+str(board))
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-DCONFIG_MUSE_BOARD_WAVESHARE_S3_216='+str(board), '-DCONFIG_LV_FONT_MONTSERRAT_24=1',
                            '-I'+str(self.sdk), str(EXT / 'tests/palette_test.c'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True, env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0'})


if __name__ == '__main__':
    unittest.main()
