"""Execute real command metadata and the pinned SDK registration serializer.

All identity/configuration values below are synthetic. No device or network.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
COMP = ROOT / 'bridge/extensions/gadget-actell/components/actell_bridge'


class CommandMetadataTests(unittest.TestCase):
    def test_full_official_registration_with_and_without_images(self):
        archive = ROOT / 'local/upstream/archives/muse-gadget-sdk-812c46f.tar.gz'
        self.assertEqual(hashlib.sha256(archive.read_bytes()).hexdigest(),
                         '0a7548404b4aa25ad0cc3ad0f9fee1ad3a3245a03a14b02293da3a83bde69359')
        cjson = ROOT / 'local/build/slan-complete-sdk/esp32/managed_components/espressif__cjson/cJSON'
        if not (cjson / 'cJSON.c').is_file():
            cjson = ROOT / 'local/build/slan-final-stage/esp32/managed_components/espressif__cjson/cJSON'
        self.assertTrue((cjson / 'cJSON.c').is_file(), 'Use existing staged cJSON dependency')
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            path = 'esp32/main/noise_control.cpp'
            source = tmp / path
            source.parent.mkdir(parents=True)
            with tarfile.open(archive) as tar:
                source.write_bytes(tar.extractfile(path).read())
            subprocess.run(['git', 'init', '-q', str(tmp)], check=True)
            subprocess.run(['git', 'apply', '--include=' + path,
                            str(ROOT / 'bridge/patches/muse-gadget-sdk/actell-hook.patch')],
                           cwd=tmp, check=True)
            actual = source.read_text().split('static cJSON *string_param(', 1)[1]
            actual = 'static cJSON *string_param(' + actual.split('// ---- OTA callback', 1)[0]
            stubs = r'''
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "cJSON.h"
#include "sdkconfig.h"
extern "C" void actell_bridge_register(cJSON *);
static char s_wifi_ssid[33],s_node_id[129],s_display_name[129],s_register_req_id[40];
struct esp_app_desc_t { char version[33]; };
static const esp_app_desc_t *esp_app_get_description(){static esp_app_desc_t d={"synthetic-test-version"};return &d;}
static void copy_wifi_ssid(char *out,size_t n){snprintf(out,n,"%s",s_wifi_ssid);}
static void make_uuid(char *out,size_t n){snprintf(out,n,"11111111-1111-4111-8111-111111111111");}
static bool led_status_display_info(int *w,int *h){*w=410;*h=502;return true;}
static int led_status_display_bits(){return 4;}
static bool ota_is_enabled(){return true;}
'''
            harness = tmp / 'register.cpp'
            harness.write_text(stubs + actual + r'''
int main(){
    memset(s_wifi_ssid,'x',sizeof(s_wifi_ssid)-1);
    memset(s_node_id,'n',sizeof(s_node_id)-1);
    memset(s_display_name,'d',sizeof(s_display_name)-1);
    char *s=build_register_json();
    if(!s)return 2;
    puts(s);free(s);return 0;
}
''')
            subprocess.run(['cc', '-c', str(cjson / 'cJSON.c'), '-I', str(cjson),
                            '-o', str(tmp / 'cjson.o')], check=True)
            for images in (0, 1):
                (tmp / 'sdkconfig.h').write_text(
                    '#define CONFIG_ACTELL_BRIDGE 1\n'
                    f'#define CONFIG_ACTELL_BRIDGE_IMAGES {images}\n' + ''.join(
                        f'#define CONFIG_{key} {value}\n' for key, value in {
                            'HOMEHUB_TUNNEL': 1, 'HOMEHUB_DISPLAY_COMMANDS': 1,
                            'HOMEHUB_LED_BACKEND_MUSE': 0, 'HOMEHUB_VOICE': 1,
                            'HOMEHUB_SENSECAP_SENSORS': 1, 'HOMEHUB_RETERMINAL_SHT4X': 0,
                            'MUSE_WATCHER_CAMERA': 1}.items()))
                subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-c',
                                str(COMP / 'actell_commands.c'), '-I', str(tmp), '-I', str(cjson),
                                '-I', str(COMP / 'include'), '-I', str(COMP.parents[1] / 'tests/stubs'),
                                '-o', str(tmp / 'commands.o')], check=True)
                binary = tmp / 'register'
                subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                                str(harness), str(tmp / 'commands.o'), str(tmp / 'cjson.o'),
                                '-I', str(tmp), '-I', str(cjson), '-o', str(binary)], check=True)
                raw = subprocess.check_output([str(binary)])
                self.assertLess(len(raw), 8192)
                commands = json.loads(raw)['params']['commands_v2']
                self.assertEqual('actell.image' in commands, bool(images))
                rpc = commands['actell.rpc']
                self.assertEqual(set(rpc['required']), {'op', 'params_json'})
                self.assertTrue(all(p['type'] == 'string' for p in rpc['required'].values()))
                description = rpc['description']
                self.assertIn("user's paired iPhone", description)
                self.assertIn('capabilities -> runs.open -> tools.call -> calls.get', description)
                self.assertIn('never repeat under a new ID', description)
                self.assertIn('deadline_ms', description)
                details = rpc['required']['params_json']['description']
                first = details.split('Example complete command arguments: ', 1)[1].split('. Example', 1)[0]
                self.assertEqual(json.loads(first), {'op': 'capabilities', 'params_json': '{}'})
                poll = details.split('Example polling arguments: ', 1)[1].split('. Success', 1)[0]
                self.assertEqual(json.loads(json.loads(poll)['params_json']),
                                 {'run_id': 'your-original-run', 'call_id': 'your-original-call'})
                if not images:
                    self.assertIn('does not expose images', description)


if __name__ == '__main__':
    unittest.main()
