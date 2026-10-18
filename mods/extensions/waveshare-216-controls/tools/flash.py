#!/usr/bin/env python3
"""Flash verified controls firmware into the inactive slot; preserve device data.

Requires explicit user authorization. Do not print raw device status or tool logs.
"""
import os,sys,json,subprocess,struct,zlib
from pathlib import Path
import argparse
from serial.tools import list_ports
os.umask(0o077)
ROOT=Path(__file__).resolve().parents[4]
os.chdir(ROOT)
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build', required=True, type=Path)
parser.add_argument('--resources', required=True, type=Path)
parser.add_argument('--check', action='store_true', help='read-only preflight, no flash writes')
a=parser.parse_args()
build=a.build.resolve()
if not build.is_relative_to(ROOT/'local/build'):
    parser.error('Build must be under local/build')
resource=a.resources.resolve()
if not resource.is_file(): parser.error('Existing resource image required for verification')
cred=ROOT/'local/credentials/waveshare-216-controls'
logs=ROOT/'local/logs/waveshare-216-controls'
report_dir=ROOT/'local/validation/waveshare-216-controls'
for d in (cred,logs,report_dir):d.mkdir(parents=True,exist_ok=True)
cred.chmod(0o700)
ports=[p.device for p in list_ports.comports() if 'usbmodem' in p.device or 'usbserial' in p.device]
if len(ports)!=1:raise SystemExit('Expected one attached serial board; no operation performed')
port=ports[0];(cred/'port').write_text(port)
sys.path.insert(0,str(Path('local/build/controls-firmware/esp32/tools/muse').resolve()))
from chat import Board
try:
    with Board(port) as board:status=board.status()
except Exception:raise SystemExit('Board status unavailable; details withheld')
if not status or status.get('board')!='Waveshare ESP32-S3-Touch-AMOLED-2.16':raise SystemExit('Unexpected board; refusing write')
print('Attached Waveshare 2.16 confirmed.',flush=True)
def run(name,*args,after='no-reset'):
    r=subprocess.run([sys.executable,'-m','esptool','--port',port,'--baud','921600','--after',after,*map(str,args)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (logs/(name+'.log')).write_bytes(r.stdout)
    if r.returncode:raise SystemExit(name+' failed; device remains in download mode; private details withheld')
    print(name+': passed',flush=True)
    return r.stdout.decode(errors='replace')
security=run('security','get-security-info')
if 'Secure Boot: Disabled' not in security or 'Flash Encryption: Disabled' not in security:raise SystemExit('Unexpected security state; refusing write')
flash=run('capacity','flash-id')
if 'ESP32-S3' not in flash or '16MB' not in flash:raise SystemExit('Unexpected Flash capacity; refusing write')
meta=json.loads(Path('local/validation/caption-fonts/firmware-private-fonts-tts-controls.json').read_text())
if not meta['combined_features_verified'] or not meta['provider_credentials_imported'] or not meta.get('controls_verified'):raise SystemExit('Combined feature verification missing')
app=build/'muse-gadget.bin'
if build.name != meta['mode'] or app.stat().st_size != meta['app_bytes']:raise SystemExit('Build report does not match selected image')
if not 0<app.stat().st_size<=0x400000:raise SystemExit('Invalid image size')
run('before-prefix','read-flash','0','0x20000',cred/'before-prefix.bin')
run('before-product','read-flash','0x820000','0x2000',cred/'before-product.bin')
b=(cred/'before-prefix.bin').read_bytes()
table=(build/'partition_table/partition-table.bin').read_bytes()
entries={}
for off in range(0,len(table),32):
    if table[off:off+2]!=b'\xaa\x50':break
    _,kind,sub,offset,length,label,flags=struct.unpack_from('<HBBII16sI',table,off)
    entries[label.split(b'\0')[0].decode()] = (kind,sub,offset,length,flags)
expected={'ota_0':(0,16,0x20000,0x400000,0),'ota_1':(0,17,0x420000,0x400000,0),
          'otadata':(1,0,0x1d000,0x2000,0),'actell_assets':(1,64,0x830000,0x7d0000,0)}
if any(entries.get(k)!=v for k,v in expected.items()):raise SystemExit('Unexpected app/resource partition geometry')
if b[0x10000:0x10000+len(table)]!=table:raise SystemExit('Device table differs; refusing app-only flash')
run('existing-resources','verify-flash','0x830000',resource)
valid=[]
for i in range(2):
    o=0x1d000+i*4096;seq=struct.unpack_from('<I',b,o)[0];state=struct.unpack_from('<I',b,o+24)[0];crc=struct.unpack_from('<I',b,o+28)[0]
    if seq!=0xffffffff and state not in (3,4) and zlib.crc32(b[o:o+4],0xffffffff)==crc:
        if state not in (2,0xffffffff):raise SystemExit('OTA transition in progress; refusing update')
        valid.append((seq,i))
if not valid:raise SystemExit('Boot selection not established; refusing write')
seq,index=max(valid);active=(seq-1)%2;target=1-active;nextseq=seq+1
while (nextseq-1)%2!=target:nextseq+=1
if nextseq>=0xffffffff:raise SystemExit('OTA sequence exhausted')
sector=bytearray(b'\xff'*4096);struct.pack_into('<I',sector,0,nextseq);struct.pack_into('<I',sector,24,2);struct.pack_into('<I',sector,28,zlib.crc32(sector[:4],0xffffffff))
(cred/'ota-switch.bin').write_bytes(sector)
offset=entries['ota_'+str(target)][2];switch=0x1d000+(1-index)*4096
plan={'previous_slot':active,'target_slot':target,'app_offset':offset,'app_bytes':app.stat().st_size,
      'ota_selection_offset':switch,'resources_verified':True,'write_scope':'inactive app and one OTA selection sector'}
(report_dir/'flash-plan.json').write_text(json.dumps(plan,indent=2)+'\n')
print(json.dumps(plan),flush=True)
if a.check:
    run('preflight-reset','flash-id',after='hard-reset')
    raise SystemExit(0)
run('write-inactive-app','write-flash',hex(offset),app)
run('verify-inactive-app','verify-flash',hex(offset),app)
run('select-controls-slot','write-flash',hex(switch),cred/'ota-switch.bin')
run('verify-boot-selection','verify-flash',hex(switch),cred/'ota-switch.bin')
run('after-prefix','read-flash','0','0x20000',cred/'after-prefix.bin')
run('after-product','read-flash','0x820000','0x2000',cred/'after-product.bin')
after=(cred/'after-prefix.bin').read_bytes()
for o,n in [(0,0x10000),(0x10000,0x1000),(0x11000,0xc000),(0x1f000,0x1000),(0x1d000+index*4096,4096)]:
    if b[o:o+n]!=after[o:o+n]:raise SystemExit('Protected prefix changed; do not reboot')
if (cred/'before-product.bin').read_bytes()!=(cred/'after-product.bin').read_bytes():raise SystemExit('Product data changed; do not reboot')
result={'previous_slot':active,'controls_slot':target,'app_bytes':app.stat().st_size,'app_verified':True,'persistent_data_verified':True,'existing_resources_verified':True,'resources_rewritten':False,'partition_table_rewritten':False,'rebooted':False}
(report_dir/'flash.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
