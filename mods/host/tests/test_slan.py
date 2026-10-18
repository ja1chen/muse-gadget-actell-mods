"""SLAN/1 strict C codec and actual worker fault model; no device/network access."""
import base64
import ctypes as c
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[3]
EXT=ROOT/'mods/extensions/gadget-actell'
COMP=EXT/'components/actell_bridge'
class Message(c.Structure):
    _fields_=[('request',c.c_char*37),('name',c.c_char*65),('state',c.c_char*16),('client',c.c_char*37),('secret',c.c_char*44),('host',c.c_char*254),('pin',c.c_char*65),('error',c.c_char*32),('port',c.c_uint),('remaining',c.c_uint)]
class SLANTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory();lib=Path(cls.tmp.name)/'slan.dylib'
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-shared','-fPIC','-I',str(COMP/'include'),str(COMP/'actell_slan.c'),'-o',str(lib)],check=True)
        cls.lib=c.CDLL(str(lib));cls.lib.slan_decode.argtypes=[c.c_int,c.c_char_p,c.c_size_t,c.POINTER(Message)];cls.lib.slan_decode.restype=c.c_bool
        cls.lib.slan_claim_matches.argtypes=[c.POINTER(Message),c.c_char_p,c.c_char_p,c.c_uint,c.c_char_p];cls.lib.slan_claim_matches.restype=c.c_bool
        cls.lib.slan_private_ipv4.argtypes=[c.c_uint32];cls.lib.slan_private_ipv4.restype=c.c_bool
        cls.lib.slan_lan_ipv4.argtypes=[c.c_uint32,c.c_uint32,c.c_uint32];cls.lib.slan_lan_ipv4.restype=c.c_bool
        cls.lib.slan_secret_decode.argtypes=[c.c_char_p,c.c_void_p];cls.lib.slan_secret_decode.restype=c.c_bool
    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()
    def decode(self,kind,obj):
        raw=obj if isinstance(obj,bytes) else json.dumps(obj,ensure_ascii=False,separators=(',',':')).encode()
        m=Message();return self.lib.slan_decode(kind,raw,len(raw),c.byref(m)),m
    def approval(self):return dict(v=1,request_id='11111111-1111-4111-8111-111111111111',state='approved',client_id='22222222-2222-4222-8222-222222222222',secret='A'*43,host='actell-fixture.local',port=8765,certificate_sha256='a'*64,claim_remaining_s=300)
    def test_frozen_contract_and_dummy_bytes(self):
        self.assertEqual(hashlib.sha256((ROOT/'docs/protocol/slan-v1.md').read_bytes()).hexdigest(),'d1ecce424490d4f8e7ea3dc927619f04b7c3fe8b9b522ee2dcacd286c23cee75')
        request=(EXT/'tests/slan-dummy-request.json').read_bytes()
        self.assertEqual(hashlib.sha256(request).hexdigest(),'10fb78b0779f29b6d17a37395de02c967943129bb45a997c053bce0beeb90e77')
        self.assertTrue(self.decode(0,request)[0]);self.assertTrue(self.decode(1,(EXT/'tests/slan-dummy-claim.json').read_bytes())[0])
    def test_pin_host_port_binding(self):
        ok,m=self.decode(3,self.approval());self.assertTrue(ok)
        args=[b'11111111-1111-4111-8111-111111111111',b'actell-fixture.local',8765,b'a'*64]
        self.assertTrue(self.lib.slan_claim_matches(c.byref(m),*args))
        for i,value in enumerate([b'33333333-3333-4333-8333-333333333333',b'other.local',8766,b'b'*64]):
            altered=args.copy();altered[i]=value;self.assertFalse(self.lib.slan_claim_matches(c.byref(m),*altered))
    def test_strict_json(self):
        valid=json.dumps(self.approval(),separators=(',',':')).encode()
        for raw in [b'\xef\xbb\xbf'+valid,valid+b'x',valid.replace(b'"v":1',b'"v":1,"\\u0076":1'),valid.replace(b'"v":1',b'"v":1.0'),valid.replace(b'"state":"approved"',b'"state":"approved","extra":1'),valid.replace(b'actell-fixture',b'\xc0\x80'),valid.replace(b'actell-fixture',b'\\u0000'),valid.replace(b'"port":8765',b'"port":4294967296'),b' '*4097]:
            self.assertFalse(self.decode(3,raw)[0])
        for key,values in {'host':['localhost','127.0.0.1','A.local','a..local','-a.local','a.local/path'],'secret':['A'*42+'B','A'*43+'=','/'*43],'port':[0,65536,8765.0],'claim_remaining_s':[0,301],'client_id':['A'*36]}.items():
            for value in values:
                msg=self.approval();msg[key]=value;self.assertFalse(self.decode(3,msg)[0],(key,value))
    def test_names_and_pending_errors(self):
        for name in ('Desk Muse','桌面 Muse'):
            self.assertTrue(self.decode(0,dict(v=1,request_id=self.approval()['request_id'],device_name=name))[0])
        for name in ('','x'*65,'bad\nname','bad\x85name'):
            self.assertFalse(self.decode(0,dict(v=1,request_id=self.approval()['request_id'],device_name=name))[0])
        self.assertTrue(self.decode(2,dict(v=1,request_id=self.approval()['request_id'],state='pending',retry_after_s=2))[0])
        self.assertTrue(self.decode(4,dict(v=1,error='rejected'))[0])
    def test_rfc1918_and_canonical_secret(self):
        import ipaddress
        for ip in ('10.0.0.1','172.16.0.1','172.31.255.254','192.168.1.2'):self.assertTrue(self.lib.slan_private_ipv4(int(ipaddress.IPv4Address(ip))))
        for ip in ('127.0.0.1','169.254.1.1','172.32.0.1','8.8.8.8','224.0.0.1','0.0.0.0'):self.assertFalse(self.lib.slan_private_ipv4(int(ipaddress.IPv4Address(ip))))
        for raw in (bytes(32),bytes(range(32)),b'\xff'*32):
            out=(c.c_ubyte*32)();encoded=base64.urlsafe_b64encode(raw).rstrip(b'=');self.assertTrue(self.lib.slan_secret_decode(encoded,out));self.assertEqual(bytes(out),raw)
    def test_same_wifi_subnet_policy(self):
        import ipaddress
        ip=lambda s:int(ipaddress.IPv4Address(s))
        for remote in ('192.168.1.2','192.168.1.254'):
            self.assertTrue(self.lib.slan_lan_ipv4(ip(remote),ip('192.168.1.10'),ip('255.255.255.0')))
        for remote in ('10.0.0.1','192.168.2.1','192.168.1.0','192.168.1.255','192.168.1.10','8.8.8.8'):
            self.assertFalse(self.lib.slan_lan_ipv4(ip(remote),ip('192.168.1.10'),ip('255.255.255.0')))
    def test_real_discovery_distinct_srv_san_and_dhcp_sanitized(self):
        binary=Path(self.tmp.name)/'discovery';stubs=EXT/'tests/stubs'
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(stubs),'-I',str(COMP),'-I',str(COMP/'include'),str(COMP/'actell_slan.c'),str(COMP/'actell_discovery.c'),str(EXT/'tests/slan_discovery_test.c'),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)

    def test_real_worker_fault_model_sanitized(self):
        binary=Path(self.tmp.name)/'worker';stubs=EXT/'tests/stubs'
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(stubs),'-I',str(COMP),'-I',str(COMP/'include'),str(COMP/'actell_slan.c'),str(COMP/'actell_pairing.c'),str(EXT/'tests/slan_pairing_test.c'),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)

    def test_real_nvs_record_model_sanitized(self):
        binary=Path(self.tmp.name)/'nvs';stubs=EXT/'tests/stubs'
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(stubs),'-I',str(COMP),'-I',str(COMP/'include'),str(COMP/'actell_slan.c'),str(COMP/'actell_guard.c'),str(COMP/'actell_provision.c'),str(EXT/'tests/slan_nvs_test.c'),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)

    def test_settings_page_release_uses_real_hook(self):
        import tarfile
        path='esp32/components/muse/muse_settings_ui.c'
        with tempfile.TemporaryDirectory() as tmp:
            tmp=Path(tmp);target=tmp/path;target.parent.mkdir(parents=True)
            with tarfile.open(ROOT/'local/upstream/archives/muse-gadget-sdk-812c46f.tar.gz') as archive:
                target.write_bytes(archive.extractfile(path).read())
            subprocess.run(['git','init','-q',str(tmp)],check=True)
            subprocess.run(['git','apply','--include='+path,str(ROOT/'mods/patches/muse-gadget-sdk/actell-hook.patch')],cwd=tmp,check=True)
            source=target.read_text();start=source.index('static void drop(lv_obj_t *p)');begin=source.index('{',start);depth=1;end=begin+1
            while depth:
                if source[end]=='{':depth+=1
                elif source[end]=='}':depth-=1
                end+=1
            actual=source[start:end]
            harness='#include <assert.h>\n#include <stddef.h>\n#define CONFIG_ACTELL_BRIDGE 1\ntypedef int lv_obj_t;\nstatic lv_obj_t *s_actell,*s_wifi,*s_hatch,*s_ble,*s_sound,*s_sleep,*s_battery,*s_power,*s_text;\nstatic unsigned deleted;\nstatic void lv_obj_delete_async(lv_obj_t*p){(void)p;deleted++;}\n'+actual+'\nint main(void){lv_obj_t page=0;s_actell=&page;drop(&page);assert(!s_actell&&deleted==1);s_wifi=&page;drop(&page);assert(!s_wifi&&deleted==2);return 0;}\n'
            test=tmp/'release.c';test.write_text(harness);binary=tmp/'release'
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(test),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

    def test_pairing_ui_has_no_service_selection(self):
        patch=(ROOT/'mods/patches/muse-gadget-sdk/actell-hook.patch').read_text()
        added='\n'.join(line[1:] for line in patch.splitlines() if line.startswith('+') and not line.startswith('+++'))
        self.assertGreater(added.index('s_actell_auto=switch_row'),added.index('s_actell_forget=button'))
        self.assertNotIn('s_actell_choices',added)
        self.assertNotIn('on_actell_choose',added)
        self.assertNotIn('choose your iPhone',added)
        self.assertIn('then approve this Muse on iPhone',added)
        worker=(COMP/'actell_pairing.c').read_text()
        self.assertNotIn('Choose your iPhone',worker)
        self.assertIn('count == 1',worker)
        self.assertIn('count > 1',worker)
