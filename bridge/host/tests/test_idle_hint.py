"""Run the pinned SDK handler with the optional, non-owning idle-hint adapter."""
import hashlib
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
CJSON = ROOT / 'local/build/slan-complete-sdk/esp32/managed_components/espressif__cjson/cJSON'


class IdleHintTests(unittest.TestCase):
    def test_actual_handler_does_not_bind_resource_lifetime_to_local_chat(self):
        archive = ROOT / 'local/upstream/archives/muse-gadget-sdk-812c46f.tar.gz'
        self.assertEqual(hashlib.sha256(archive.read_bytes()).hexdigest(),
                         '0a7548404b4aa25ad0cc3ad0f9fee1ad3a3245a03a14b02293da3a83bde69359')
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            with tarfile.open(archive) as tar:
                tar.extractall(tmp, filter='data')
            subprocess.run(['git', 'apply', str(ROOT / 'bridge/patches/muse-gadget-sdk/actell-hook.patch')], cwd=tmp, check=True)
            test = tmp / 'esp32/tests/test_muse_chat_session.py'
            source = test.read_text()
            source = source.replace('#include "cJSON.h"', '''#include "cJSON.h"
#define CONFIG_ACTELL_BRIDGE 1
static int observed;
static void actell_bridge_observe_event(const cJSON *event) {
    assert(event); observed++;
}''')
            source = source.replace('int main(int argc, char **argv) {', r'''
static void independent_hint() {
    begin(); s_turn.phase = P_IDLE; s_last_seq = 500; observed = 0;
    cJSON *root = cJSON_Parse("{\"type\":\"event\",\"event\":\"task.status\",\"seq\":499,\"payload\":{\"status\":\"completed\"}}");
    on_event(root);
    assert(observed == 1 && s_turn.phase == P_IDLE && !s_turn.agent_busy);
    assert(!captions && !s_turn.nmsgs && s_last_seq == 500);
    cJSON_Delete(root);
    begin(); observed = 0;
    event("delta.text_append", "cloud-reply", "unrelated-user", "Cloud text");
    assert(observed == 1 && !captions && !s_turn.nmsgs);
    event("delta.text_append", "own-reply", "note", "Our text");
    assert(observed == 2 && captions == 1 && s_turn.nmsgs == 1);
}
int main(int argc, char **argv) {''')
            source = source.replace('case 2: bounded_rejections(); break;',
                                    'case 2: bounded_rejections(); break;\n    case 3: independent_hint(); break;')
            source = source.replace("flags = ['-Wall'", "flags = ['-fsanitize=address,undefined', '-Wall'")
            source = source.replace("'-c', str(JSON / 'cJSON.c')", "'-Wno-deprecated-declarations', '-c', str(JSON / 'cJSON.c')")
            source += '''\n    def test_hint_precedes_local_turn_and_sequence_filters_without_changing_routing(self):
        self.run_case(3)
'''
            test.write_text(source)
            env = dict(os.environ, CJSON_SOURCE_DIR=str(CJSON))
            subprocess.run([os.sys.executable, '-m', 'unittest', 'discover', '-s', str(test.parent), '-p', test.name, '-v'], env=env, check=True)


if __name__ == '__main__':
    unittest.main()
