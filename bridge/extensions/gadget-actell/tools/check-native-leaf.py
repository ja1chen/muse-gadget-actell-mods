#!/usr/bin/env python3
"""Check the real ESP leaf verifier using a native build of the same Mbed TLS.

Requires cryptography for fresh synthetic, memory-only certificate generation.
Outputs a status-only report under local/validation/slan; never uses device keys.
"""
import argparse
import ctypes
from datetime import datetime, timedelta, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import socket
import ssl
import tempfile
import threading


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--mbedtls-root',type=Path,required=True)
    p.add_argument('--build-dir',type=Path,required=True)
    p.add_argument('--prepare',action='store_true',help='build the local native Mbed TLS validation dependency')
    a=p.parse_args()
    from cryptography import x509
    from cryptography.hazmat.primitives import hashes,serialization
    from cryptography.hazmat.primitives.asymmetric import ec,rsa
    from cryptography.x509.oid import NameOID,ExtendedKeyUsageOID
    root=Path(__file__).resolve().parents[4];ext=root/'bridge/extensions/gadget-actell';comp=ext/'components/actell_bridge'
    out=root/'local/validation/slan';out.mkdir(parents=True,exist_ok=True)
    lib=out/'leaf-native.dylib';mb=a.mbedtls_root.resolve();build=a.build_dir.resolve()
    if a.prepare:
        compat=build.parent/(build.name+'-compat');(compat/'mbedtls').mkdir(parents=True,exist_ok=True)
        for header in mb.rglob('include/mbedtls/private/*.h'):
            link=compat/'mbedtls'/header.name
            if not link.exists():link.symlink_to(header)
        (compat/'sdkconfig.h').write_text('#pragma once\n/* Synthetic native validation: no ESP hardware acceleration. */\n')
        logdir=root/'local/logs/slan';logdir.mkdir(parents=True,exist_ok=True)
        with (logdir/'native-mbedtls-config.log').open('w') as log:
            subprocess.run(['cmake','-S',str(mb),'-B',str(build),'-DGEN_FILES=OFF','-DENABLE_TESTING=OFF','-DENABLE_PROGRAMS=OFF','-DUSE_SHARED_MBEDTLS_LIBRARY=ON','-DUSE_STATIC_MBEDTLS_LIBRARY=OFF','-DDISABLE_PACKAGE_CONFIG_AND_INSTALL=ON','-DCMAKE_C_FLAGS=-I'+str(compat)],check=True,stdout=log,stderr=subprocess.STDOUT)
        with (logdir/'native-mbedtls-build.log').open('w') as log:
            subprocess.run(['cmake','--build',str(build),'-j','6'],check=True,stdout=log,stderr=subprocess.STDOUT)
    include=[mb/'include',mb/'tf-psa-crypto/include',mb/'tf-psa-crypto/drivers/builtin/include',ext/'tests/stubs',comp,comp/'include']
    command=['cc','-std=c11','-Wall','-Wextra','-Werror','-shared','-fPIC']
    for path in include:command+=['-I',str(path)]
    command+=['-Wl,-rpath,'+str(build/'library'),'-Wl,-rpath,'+str(build/'tf-psa-crypto/core'),str(comp/'actell_leaf.c'),str(comp/'actell_slan.c'),str(ext/'tests/slan_leaf_test.c'),str(build/'library/libmbedtls.dylib'),str(build/'library/libmbedx509.dylib'),str(build/'tf-psa-crypto/core/libtfpsacrypto.dylib'),'-o',str(lib)]
    subprocess.run(command,check=True)
    native=ctypes.CDLL(str(lib));native.slan_test_leaf.argtypes=[ctypes.c_char_p]*4;native.slan_test_leaf.restype=ctypes.c_int
    native.slan_test_handshake.argtypes=[ctypes.c_char_p,ctypes.c_char_p];native.slan_test_handshake.restype=ctypes.c_int
    now=datetime.now(timezone.utc)
    def cert(*,key=None,san='actell-fixture.local',cn='actell-fixture.local',ca=False,eku=ExtendedKeyUsageOID.SERVER_AUTH,before=None,after=None,signer=None):
        key=key or ec.generate_private_key(ec.SECP256R1())
        subject=x509.Name([x509.NameAttribute(NameOID.COMMON_NAME,cn)])
        b=x509.CertificateBuilder().subject_name(subject).issuer_name(subject).public_key(key.public_key()).serial_number(x509.random_serial_number()).not_valid_before(before or now-timedelta(days=1)).not_valid_after(after or now+timedelta(days=2)).add_extension(x509.BasicConstraints(ca=ca,path_length=None),critical=True)
        if san is not None:b=b.add_extension(x509.SubjectAlternativeName([x509.DNSName(san)]),critical=False)
        if eku is not None:b=b.add_extension(x509.ExtendedKeyUsage([eku]),critical=False)
        c=b.sign(signer or key,hashes.SHA256())
        return c.public_bytes(serialization.Encoding.PEM),hashlib.sha256(c.public_bytes(serialization.Encoding.DER)).hexdigest()
    def check(pem,pin=b'',host=b'actell-fixture.local'):
        out=ctypes.create_string_buffer(65)
        return native.slan_test_leaf(pem,host,pin,out)==1,out.value.decode()
    results={}
    good,pin=cert();ok,actual=check(good);results['valid_p256_san_time_self_signature']=ok and actual==pin
    results['same_der_pin']=check(good,pin.encode())[0]
    replacement,_=cert();results['same_host_new_leaf_rejected']=not check(replacement,pin.encode())[0]
    for name,kwargs in {'wrong_san':{'san':'other.local'},'cn_without_san':{'san':None},'expired':{'before':now-timedelta(days=3),'after':now-timedelta(days=1)},'future':{'before':now+timedelta(days=1),'after':now+timedelta(days=3)},'ca':{'ca':True},'wrong_eku':{'eku':ExtendedKeyUsageOID.CLIENT_AUTH},'p384':{'key':ec.generate_private_key(ec.SECP384R1())},'rsa':{'key':rsa.generate_private_key(public_exponent=65537,key_size=2048)},'invalid_self_signature':{'signer':ec.generate_private_key(ec.SECP256R1())}}.items():
        pem,_=cert(**kwargs);results[name+'_rejected']=not check(pem)[0]
    results['bundle_rejected']=not check(good+replacement)[0]
    # Ephemeral synthetic private keys stay in a 0700 temporary directory, with
    # 0600 files, and are deleted when the loopback TLS checks finish.
    def handshake(expected):
        key=ec.generate_private_key(ec.SECP256R1());pem,pin=cert(key=key)
        with tempfile.TemporaryDirectory(dir=root/'local/build/tmp') as directory:
            directory=Path(directory);keyfile=directory/'key.pem';certfile=directory/'cert.pem'
            keyfile.write_bytes(key.private_bytes(serialization.Encoding.PEM,serialization.PrivateFormat.PKCS8,serialization.NoEncryption()));keyfile.chmod(0o600);certfile.write_bytes(pem);certfile.chmod(0o600)
            context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);context.load_cert_chain(certfile,keyfile)
            sni=[];context.set_servername_callback(lambda sock,name,ctx:sni.append(name))
            listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen(1);listener.settimeout(10)
            port=listener.getsockname()[1]
            def serve():
                try:
                    client,_=listener.accept()
                    with client:
                        try:
                            with context.wrap_socket(client,server_side=True):pass
                        except ssl.SSLError:pass
                finally:listener.close()
            thread=threading.Thread(target=serve);thread.start()
            try:result=native.slan_test_handshake(str(port).encode(),pin.encode() if expected=='same' else expected)
            finally:thread.join(timeout=15)
            assert sni == ["actell-fixture.local"], "TLS SNI must use stable SAN, not the socket address"
            return result
    results['native_tls_self_signed_handshake']=handshake(b'')==1
    results['native_tls_same_pin_handshake']=handshake('same')==1
    results['native_tls_replacement_pin_rejected']=handshake(b'a'*64)==0
    (out/'native-leaf.json').write_text(json.dumps({'checks':results,'real_firmware_verifier':True,'real_mbedtls_source':True,'tls_socket_handshake_tested':True,'real_device_tested':False},indent=2)+'\n')
    for name,ok in results.items():print(('PASS ' if ok else 'FAIL ')+name)
    return 0 if all(results.values()) else 1


if __name__=='__main__':raise SystemExit(main())
