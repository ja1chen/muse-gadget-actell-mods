#!/usr/bin/env python3
"""Check combined boot without initiating any speech/cloud synthesis."""
import os,sys,json,time,termios,re,argparse
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
os.chdir(ROOT)
import serial
os.umask(0o077)
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--review-only',action='store_true',help='review the existing private capture without resetting')
args=parser.parse_args()
report_path=Path('local/validation/waveshare-216-controls/flash.json')
flash=json.loads(report_path.read_text())
if not flash['persistent_data_verified'] or not flash['app_verified']:raise SystemExit('Flash verification missing')
port=Path('local/credentials/waveshare-216-controls/port').read_text()
if not args.review_only:
    s=serial.Serial(port=None,baudrate=115200,timeout=.2);s.dtr=False;s.rts=False;s.port=port;s.open()
    attrs=termios.tcgetattr(s.fileno());attrs[2]&=~termios.HUPCL;termios.tcsetattr(s.fileno(),termios.TCSANOW,attrs)
    s.reset_input_buffer();s.dtr=False;s.rts=True;time.sleep(.1);s.rts=False
    end=time.monotonic()+45;data=bytearray()
    with Path('local/logs/waveshare-216-controls/boot.log').open('wb') as log:
        while time.monotonic()<end:
            chunk=s.read(4096)
            if chunk:log.write(chunk);data.extend(chunk)
    s.close()
text=Path('local/logs/waveshare-216-controls/boot.log').read_bytes().decode(errors='replace')
loaded=[int(v,16) for v in re.findall(r'Loaded app from partition at offset (0x[0-9a-fA-F]+)',text)]
ready=re.search(r'Read-only caption resource ready: (\d+) glyphs, line height (\d+)',text)
pages=re.search(r'reply pages: (\d+) x (\d+) heard, (\d+) x (\d+) read',text)
sys.path.insert(0,str(Path('local/build/controls-firmware/esp32/tools/muse').resolve()))
from chat import Board
try:
    with Board(port) as board:status=board.status()
except Exception:status=None
dev=status.get('device',{}) if status else {}
result={'boot_capture_seconds':45,'controls_slot_loaded':0x20000+flash['controls_slot']*0x400000 in loaded,'font_ready':bool(ready),'glyph_count':int(ready[1]) if ready else None,'heard_lines':int(pages[2]) if pages else None,'read_lines':int(pages[4]) if pages else None,'panic_count':len(re.findall(r'Guru Meditation|assert failed|Stack smashing|abort\(\)',text,re.I)),'power_control_init_failed': 'PWR controls unavailable' in text,'power_latch_error_count':text.count('PWR latch error'),'watchdog_count':len(re.findall(r'task_wdt:|Interrupt wdt timeout|Task watchdog got triggered',text,re.I)),'wifi_connected':dev.get('wifi',{}).get('state')=='connected','paired':dev.get('link',{}).get('paired') is True,'muse_session_connected':dev.get('hatch',{}).get('state')=='Connected','speaker_enabled':dev.get('speaker'),'speech_triggered':False,'tts_cloud_playback_verified':False}
Path('local/validation/waveshare-216-controls/boot.json').write_text(json.dumps(result,indent=2)+'\n')
flash['rebooted']=True;report_path.write_text(json.dumps(flash,indent=2)+'\n');print(json.dumps(result))
if not result['controls_slot_loaded'] or not result['font_ready'] or result['panic_count'] or result['watchdog_count'] or result['power_control_init_failed']:raise SystemExit(1)
