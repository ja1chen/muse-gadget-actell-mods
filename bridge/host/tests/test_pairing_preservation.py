"""Execute patched SDK authentication flow with synthetic tokens, no service I/O."""
import hashlib
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[3]
class PairingPreservationTests(unittest.TestCase):
    def test_boot_and_auth_refusal_preserve_existing_pairing(self):
        archive=ROOT/'local/upstream/archives/muse-gadget-sdk-812c46f.tar.gz'
        self.assertEqual(hashlib.sha256(archive.read_bytes()).hexdigest(),'0a7548404b4aa25ad0cc3ad0f9fee1ad3a3245a03a14b02293da3a83bde69359')
        with tempfile.TemporaryDirectory() as directory:
            tmp=Path(directory);p=tmp/'esp32/main/app.c';p.parent.mkdir(parents=True)
            with tarfile.open(archive) as tar:p.write_bytes(tar.extractfile('esp32/main/app.c').read())
            subprocess.run(['git','init','-q',str(tmp)],check=True)
            subprocess.run(['git','apply','--include=esp32/main/app.c',str(ROOT/'bridge/patches/muse-gadget-sdk/actell-hook.patch')],cwd=tmp,check=True)
            s=p.read_text();start=s.index('static void handle_token_revoked_unlocked(');end=s.index('static bool accept_pairing_credentials(',start);functions=s[start:end]
            start=s.index('static bool ensure_access_token_ready_with_gate_held(');end=s.index('static int fetch_vms_with_refresh_with_gate_held',start);functions+=s[start:end]
            harness=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#define VM_API_ERR_AUTH -401
#define CONFIG_HOMEHUB_TOKEN_REFRESH_INTERVAL_DAYS 30
#define TOKEN_PERIODIC_REFRESH_INTERVAL_US 1000000
#define LED_STATE_ERROR 1
static unsigned deletes,refreshes,mints,writes,ui_errors;
static int rc=VM_API_ERR_AUTH;
static int64_t now,s_last_periodic_token_refresh_us;
static bool s_sdk_token_report_attempted;
static const char *TAG="synthetic-test";
typedef struct {char *access_token,*refresh_token;} vm_device_tokens_t;
typedef int vm_token_refresh_method_t;
#define VM_TOKEN_REFRESH_NONE 0
static void log_sink(const char *tag,const char *fmt,...){(void)tag;(void)fmt;}
#define ESP_LOGW(...) log_sink(__VA_ARGS__)
#define ESP_LOGI(...) log_sink(__VA_ARGS__)
static void ui_set_status(const char *s){assert(!strcmp(s,"auth_failed"));ui_errors++;}
static void led_status_set_state(int s){assert(s==LED_STATE_ERROR);}
static void auth_lock_take(void){}
static void auth_lock_give(void){}
static char *load_token_unlocked(const char *key){assert(!strcmp(key,"access_token")||!strcmp(key,"refresh_token"));return strdup("synthetic-token");}
static bool migrate_legacy_auth_token_locked(void){return false;}
static const char *identity_node_id(void){return "synthetic-node";}
static const char *identity_sdk_token(void){return "synthetic-sdk";}
static int64_t esp_timer_get_time(void){return now;}
static bool store_token_pair_unlocked(const char *a,const char *b){(void)a;(void)b;writes++;return true;}
static void store_pairing_username_unlocked(const char *s){(void)s;}
static void vm_device_tokens_free(vm_device_tokens_t *t){(void)t;}
static int vm_api_mint_device_token(const char *a,const char *id,vm_device_tokens_t *t){(void)a;(void)id;(void)t;mints++;return rc;}
static int vm_api_refresh_device_token(const char *a,const char *b,const char *id,vm_device_tokens_t *t,vm_token_refresh_method_t *m){(void)a;(void)b;(void)id;(void)t;(void)m;refreshes++;return rc;}
bool config_clear_pairing(void){deletes++;return true;}
bool config_clear_setup(void){deletes++;return true;}
'''+functions+r'''
int main(void){
 assert(ensure_access_token_ready_with_gate_held(false,true));assert(!refreshes&&!writes&&!deletes&&!s_sdk_token_report_attempted);
 assert(!ensure_access_token_ready_with_gate_held(true,false));assert(refreshes==1&&!writes&&!deletes&&ui_errors==1);
 now=2*TOKEN_PERIODIC_REFRESH_INTERVAL_US;assert(ensure_access_token_ready_with_gate_held(false,true));assert(refreshes==2&&!deletes);
 assert(!mint_and_store_device_tokens_unlocked("synthetic-legacy",NULL));assert(mints==1&&!deletes);
 rc=0;assert(ensure_access_token_ready_with_gate_held(true,false));assert(writes==1&&!deletes);
 puts("pairing preservation: boot skips gratuitous refresh; 401/failed mint retains identity; legitimate refresh still works");return 0;
}
'''
            (tmp/'auth.c').write_text(harness)
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(tmp/'auth.c'),'-o',str(tmp/'auth')],check=True)
            subprocess.run([str(tmp/'auth')],check=True)
