#!/usr/bin/env python3
"""Explicit local trusted-USB operation. Never log or accept secrets in argv."""
import argparse, json, os, select, stat, termios, time
from pathlib import Path

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port',required=True)
    p.add_argument('--invitation',required=True,type=Path)
    a=p.parse_args()
    mode=a.invitation.stat().st_mode
    if mode & (stat.S_IRWXG|stat.S_IRWXO):
        raise SystemExit('Invitation must be private: chmod 600 the file first.')
    raw=a.invitation.read_bytes()
    if len(raw)>8190: raise SystemExit('Invitation too large')
    obj=json.loads(raw)
    wire=json.dumps(obj,separators=(',',':')).encode()+b'\n'
    if len(wire)>8191: raise SystemExit('Invitation too large after serialization')
    fd=os.open(a.port,os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK)
    try:
        attrs=termios.tcgetattr(fd); attrs[0]=0;attrs[1]=0;attrs[2]=termios.CS8|termios.CREAD|termios.CLOCAL;attrs[3]=0
        attrs[4]=attrs[5]=termios.B115200;termios.tcsetattr(fd,termios.TCSANOW,attrs)
        print('Release the button during reset; after normal boot, hold the verified setup button for 2 seconds within 8 seconds. Waiting for USB setup.')
        buffer=b'';end=time.monotonic()+120;sent=False
        while time.monotonic()<end:
            if not select.select([fd],[],[],1)[0]:continue
            data=os.read(fd,4096)
            buffer=(buffer+data)[-8192:]
            if not sent and b'ACTELL USB SETUP READY\n' in buffer:
                # No retries: partial/uncertain writes need a fresh explicit setup window.
                offset=0
                while offset<len(wire):
                    if not select.select([],[fd],[],5)[1]:raise SystemExit('USB write timeout; setup unverified')
                    offset+=os.write(fd,wire[offset:])
                sent=True;buffer=b''
            if sent and b'ACTELL SETUP STORED\n' in buffer:
                print('Gadget confirmed private setup storage. Check Actell for per-device authorization.');return
            if b'ACTELL SETUP REJECTED\n' in buffer:raise SystemExit('Gadget rejected invitation; no success claimed')
        raise SystemExit('No confirmed setup result; do not assume credentials were stored')
    finally:os.close(fd)
if __name__=='__main__':main()
