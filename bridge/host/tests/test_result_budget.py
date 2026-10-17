"""Pinned SDK result serializer: synthetic maximum JPEG/base64 and wire bounds."""
import hashlib
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
CJSON = ROOT/'local/build/slan-complete-sdk/esp32/managed_components/espressif__cjson/cJSON'

class ResultBudgetTests(unittest.TestCase):
    def test_actual_serializer_maximum_image_and_rejection(self):
        archive = ROOT/'local/upstream/archives/muse-gadget-sdk-812c46f.tar.gz'
        self.assertEqual(hashlib.sha256(archive.read_bytes()).hexdigest(),
                         '0a7548404b4aa25ad0cc3ad0f9fee1ad3a3245a03a14b02293da3a83bde69359')
        with tempfile.TemporaryDirectory() as d:
            tmp=Path(d);source=tmp/'esp32/main/noise_control.cpp';source.parent.mkdir(parents=True)
            with tarfile.open(archive) as tar:source.write_bytes(tar.extractfile('esp32/main/noise_control.cpp').read())
            subprocess.run(['git','init','-q',str(tmp)],check=True)
            subprocess.run(['git','apply','--include=esp32/main/noise_control.cpp',str(ROOT/'bridge/patches/muse-gadget-sdk/actell-hook.patch')],cwd=tmp,check=True)
            actual='static char *wrap_result_json('+source.read_text().split('static char *wrap_result_json(',1)[1].split('// ---- Reconnect policy',1)[0]
            harness=r'''
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include "cJSON.h"
#define CONFIG_ACTELL_BRIDGE 1
static size_t actell_bridge_result_limit(){return 360*1024;}
'''+actual+r'''
static char *wire(size_t n){
 cJSON *r=cJSON_CreateObject();cJSON_AddBoolToObject(r,"ok",true);cJSON_AddBoolToObject(r,"_actell",true);
 cJSON *p=cJSON_AddObjectToObject(r,"payload");cJSON_AddStringToObject(p,"format","jpeg-base64");
 char *data=(char*)malloc(n+1);assert(data);memset(data,'A',n);data[n]=0;cJSON_AddStringToObject(p,"data_base64",data);free(data);
 cJSON_AddStringToObject(p,"operation_id","synthetic-operation");cJSON_AddStringToObject(p,"coordinate_space","normalized-display-v1");
 cJSON_AddNumberToObject(p,"width",1280);cJSON_AddNumberToObject(p,"height",800);cJSON_AddStringToObject(p,"state","succeeded");
 cJSON_AddStringToObject(p,"image_status","available");cJSON_AddNumberToObject(p,"captured_at_ms",1234567800000.0);cJSON_AddNumberToObject(p,"expires_at_ms",1234569600000.0);
 return wrap_result_json("synthetic-invoke",r);
}
int main(){
 const size_t jpeg=256*1024,b64=4*((jpeg+2)/3);assert(b64==349528);
 char *s=wire(b64);assert(s&&strlen(s)<actell_bridge_result_limit());
 cJSON *r=cJSON_Parse(s);assert(r&&cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r,"ok")));
 cJSON *p=cJSON_GetObjectItemCaseSensitive(r,"payload");assert(strlen(cJSON_GetObjectItemCaseSensitive(p,"data_base64")->valuestring)==b64);
 assert(!cJSON_GetObjectItemCaseSensitive(r,"_actell"));cJSON_Delete(r);free(s);
 s=wire(0);size_t overhead=strlen(s);free(s);
 s=wire(actell_bridge_result_limit()-overhead);assert(strlen(s)==actell_bridge_result_limit());free(s);
 s=wire(actell_bridge_result_limit()-overhead+1);assert(strlen(s)<1024);r=cJSON_Parse(s);assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(r,"ok")));
 p=cJSON_GetObjectItemCaseSensitive(r,"payload");assert(!strcmp(cJSON_GetObjectItemCaseSensitive(p,"operation_id")->valuestring,"synthetic-operation"));cJSON_Delete(r);free(s);
 puts("wire budget: 256 KiB JPEG -> 349528 base64 bytes, metadata retained, exact bound/rejection passed");return 0;
}
'''
            (tmp/'budget.cpp').write_text(harness)
            subprocess.run(['cc','-std=c11','-Wno-deprecated-declarations','-fsanitize=address,undefined','-c',str(CJSON/'cJSON.c'),'-o',str(tmp/'cjson.o')],check=True)
            subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(tmp/'budget.cpp'),str(tmp/'cjson.o'),'-I',str(CJSON),'-o',str(tmp/'budget')],check=True)
            subprocess.run([str(tmp/'budget')],check=True)
