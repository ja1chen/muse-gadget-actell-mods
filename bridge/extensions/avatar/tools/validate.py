#!/usr/bin/env python3
"""Compile original C against pinned SDK headers, sanitize it, render preview artifacts."""
from pathlib import Path
import argparse, ctypes as C, json, math, os, subprocess, tarfile, tempfile, time
from PIL import Image, ImageDraw, ImageFont
ROOT=Path(__file__).resolve().parents[1]
class Pose(C.Structure):
    _fields_=[('mode',C.c_int),('t',C.c_float),('mode_t',C.c_float),('level',C.c_float),('happy',C.c_float)]
def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--sdk-archive',type=Path,required=True)
    parser.add_argument('--output',type=Path,default=ROOT.parents[2]/'local/validation/avatar-preview')
    args=parser.parse_args(); args.output.mkdir(parents=True,exist_ok=True)
    temporary_root=ROOT.parents[2]/'local/build/tmp'
    temporary_root.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='avatar-host-',dir=temporary_root) as temp:
        tmp=Path(temp)
        with tarfile.open(args.sdk_archive) as ar:
            for name in ['muse_pixel.h','muse_state.h']:
                member=next(m for m in ar.getmembers() if m.name.endswith('/components/muse/'+name))
                (tmp/name).write_bytes(ar.extractfile(member).read())
        sources=[ROOT/'src/muse_pixel.c',ROOT/'src/actell_avatar.c',ROOT/'tests/host_test.c']
        common=[os.environ.get('CC','cc'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-DACTELL_AVATAR_TEST','-I'+str(ROOT/'include'),'-I'+str(tmp)]+list(map(str,sources))+['-lm']
        subprocess.run(common+['-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-o',str(tmp/'test')],check=True)
        tested=subprocess.run([str(tmp/'test')],capture_output=True,text=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
        if tested.returncode:
            print(tested.stdout, tested.stderr)
            tested.check_returncode()
        print(tested.stdout.strip())
        subprocess.run(common+['-DAVATAR_SHARED','-shared','-fPIC','-o',str(tmp/'avatar.so')],check=True)
        lib=C.CDLL(str(tmp/'avatar.so'))
        lib.muse_pixel_render.argtypes=[C.POINTER(Pose)]
        lib.muse_pixel_scale.argtypes=[C.POINTER(C.c_uint16),C.c_int,C.c_int,C.c_int,C.c_int,C.c_int]
        lib.actell_avatar_set_enabled.argtypes=[C.c_bool]
        lib.actell_avatar_set_activity.argtypes=[C.c_int,C.c_uint32]
        lib.preview_clock.argtypes=[C.c_uint64]
        names=['boot','idle','listening','thinking','speaking','error','off','phone_read','phone_write']
        labels=['启动','待机','聆听','思考','说话','错误','关闭','看手机','操作手机']
        fontpath=Path('/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc')
        cn=ImageFont.truetype(str(fontpath),24) if fontpath.exists() else ImageFont.load_default()
        en=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',13)
        panels=[]; unique={};buf=(C.c_uint16*(64*64))();lib.muse_pixel_set_size(64)
        start=time.perf_counter()
        for i,name in enumerate(names):
            frames=[]
            for f in range(60):
                sec=f*.04
                lib.preview_clock(int(sec*1000));lib.actell_avatar_set_enabled(True)
                lib.actell_avatar_set_activity(i-6 if i>=7 else 0,1500)
                pose=Pose(i if i<7 else 1,sec,sec,.5+.45*math.sin(sec*8),0)
                lib.muse_pixel_render(C.byref(pose));lib.muse_pixel_scale(buf,64,0,63,0,63)
                rgb=bytes(channel for v in buf for channel in (((v>>11)&31)*255//31,((v>>5)&63)*255//63,(v&31)*255//31))
                frame=Image.frombytes('RGB',(64,64),rgb).resize((320,320),Image.Resampling.NEAREST)
                frames.append(frame)
            if i>=7:
                strip=Image.new('RGB',(1272,246),'#111723')
                for j,f in enumerate([0,5,10,15,20,25]):
                    strip.paste(frames[f].resize((192,192),Image.Resampling.NEAREST),(j*212+10,46))
                    d=ImageDraw.Draw(strip)
                    d.text((j*212+10,3),labels[i],font=cn,fill='#e9e6dc')
                    d.text((j*212+110,12),f'{f*.04:.2f}s',font=en,fill='#99a5b6')
                strip.save(args.output/(name+'_phases.png'))
            unique[name]=len(set(im.tobytes() for im in frames))
            frames[0].save(args.output/(name+'.gif'),save_all=True,append_images=frames[1:],duration=40,loop=0,optimize=False,disposal=2)
            still=frames[12 if name=='off' else 25]
            still.save(args.output/(name+'.png'))
            panel=Image.new('RGB',(340,390),'#111723');panel.paste(still,(10,62));draw=ImageDraw.Draw(panel);draw.text((14,4),labels[i],font=cn,fill='#e9e6dc');draw.text((15,37),name,font=en,fill='#99a5b6');panels.append(panel)
        sheet=Image.new('RGB',(1020,1170),'#111723')
        for i,panel in enumerate(panels):sheet.paste(panel,((i%3)*340,(i//3)*390))
        sheet.save(args.output/'states.png')
        phases=Image.new('RGB',(1272,492),'#111723')
        for j,name in enumerate(['phone_read','phone_write']):
            with Image.open(args.output/(name+'_phases.png')) as strip: phases.paste(strip,(0,j*246))
        phases.save(args.output/'phone_action_phases.png')
        # Standalone animation includes all nine states with readable labels.
        demo=[]
        for i,name in enumerate(names):
            with Image.open(args.output/(name+'.gif')) as gif:
                for n in range(gif.n_frames):
                    gif.seek(n);frame=Image.new('RGB',(340,390),'#111723');frame.paste(gif.convert('RGB'),(10,62));draw=ImageDraw.Draw(frame);draw.text((14,4),labels[i],font=cn,fill='#e9e6dc');draw.text((15,37),name,font=en,fill='#99a5b6');demo.append(frame)
        demo[0].save(args.output/'all_states.gif',save_all=True,append_images=demo[1:],duration=40,loop=0,disposal=2)
        report={'sdk_commit':'812c46f9fe42b69d916613a9dc702cbac5ff7b54','host_sanitizers':['address','undefined'],'leak_sanitizer':'disabled: container ptrace; renderer/controller contain no heap allocation','host_test':tested.stdout.strip(),'preview_source':'Actual muse_pixel_render + muse_pixel_scale C output, no illustrations','unique_frames':unique,'preview_seconds':round(time.perf_counter()-start,3),'unverified':['ESP-IDF target build','ESP32-S3 timing and RAM','physical AMOLED output','end-to-end Actell-to-device event transport']}
        assert all(count>1 for count in unique.values()),unique
        (args.output/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps(report,indent=2))
if __name__=='__main__':main()
