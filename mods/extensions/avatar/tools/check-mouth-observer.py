#!/usr/bin/env python3
"""Exercise current staged state observer with asynchronous producer/UI schedules and host mutexes."""
from pathlib import Path
import ctypes as C, subprocess, json, math, tempfile, argparse, os, shlex
R=Path(__file__).resolve().parents[1]
ROOT=R.parents[2]
p=argparse.ArgumentParser();p.add_argument('--include',type=Path,required=True);p.add_argument('--output',type=Path,default=ROOT/'local/validation/avatar-mouth-stable')
options=p.parse_args();options.output=options.output.resolve()
if not options.output.is_relative_to(ROOT/'local'):p.error('Output must remain under local/')
options.output.mkdir(parents=True,exist_ok=True)
workspace=tempfile.TemporaryDirectory(prefix='mouth-observer-',dir=ROOT/'local/build/tmp');T=Path(workspace.name)
text=(options.include/'muse_state.c').read_text()
def function(name):
 start=text.rfind('\n',0,text.index(name+'('))+1;brace=text.index('{',start);depth=1;i=brace+1
 while depth:
  depth+=(text[i]=='{')-(text[i]=='}');i+=1
 return text[start:i]
functions='\n'.join(function(n) for n in ['muse_state_set_mode','muse_state_set_level','muse_state_take_mouth_sample'])
source='''#include <pthread.h>
#include <math.h>
#include "actell_mouth_envelope.h"
static pthread_mutex_t s_mouth_lock=PTHREAD_MUTEX_INITIALIZER;
#define portENTER_CRITICAL(p) pthread_mutex_lock(p)
#define portEXIT_CRITICAL(p) pthread_mutex_unlock(p)
static muse_mouth_sample_t s_mouth_sample;
static muse_mode_t s_mode;
static int64_t s_mode_since_us;
static float s_level;
static _Thread_local int64_t clock_us;
static int64_t esp_timer_get_time(void){return clock_us;}
'''+functions+'''
float shared_level(void){return s_level;}
int current_mode(void){return s_mode;}
static actell_mouth_envelope_t env;
void reset(void){s_mode=MUSE_MODE_IDLE;s_mouth_sample=(muse_mouth_sample_t){0};env=(actell_mouth_envelope_t){0};clock_us=0;muse_state_set_mode(MUSE_MODE_SPEAKING);}
void produce(float level,int64_t us){clock_us=us;muse_state_set_level(level);}
void mode(int value,int64_t us){clock_us=us;muse_state_set_mode((muse_mode_t)value);}
float consume(int64_t us){clock_us=us;muse_mouth_sample_t p=muse_state_take_mouth_sample();return actell_mouth_step(&env,&p,us);}
'''
(T/'observer.c').write_text(source)
subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c11','-Wall','-Wextra','-Werror','-O2','-shared','-fPIC','-pthread','-I'+str(options.include),'-I'+str(R/'src'),str(T/'observer.c'),'-lm','-o',str(T/'observer.so')],check=True)
l=C.CDLL(str(T/'observer.so'));l.produce.argtypes=[C.c_float,C.c_int64];l.consume.argtypes=[C.c_int64];l.consume.restype=C.c_float;l.mode.argtypes=[C.c_int,C.c_int64]
report={}
def run(steps,phase,fn,end=5000,ui_first=False):
 l.reset();events=[(t,1 if ui_first else 0,'p') for t in range(0,end+1,20)];t=phase;i=0
 while t<=end:events.append((t,0 if ui_first else 1,'u'));t+=steps[i%len(steps)];i+=1
 out=[]
 for t,order,kind in sorted(events):
  if kind=='p':l.produce(fn(t),t*1000)
  else:out.append((t,l.consume(t*1000)))
 return out
for name,fn in [('alternating12zero',lambda t:.12 if (t//20)%2==0 else 0),('cross0803',lambda t:.08 if (t//20)%2==0 else .03),('weak03',lambda t:.03),('weak03to08',lambda t:.03+(t%200)/200*.05)]:
 cases=0;maxjump=0;mins=[]
 for steps in [[30],[40],[80],[35,45]]:
  for phase in range(40):
   for first in [False,True]:
    a=[v for t,v in run(steps,phase,fn,ui_first=first) if t>=500]
    assert min(a)>0,(name,steps,phase,first,a)
    h=[1+math.floor(v*15+.5) for v in a];maxjump=max(maxjump,max(abs(x-y) for x,y in zip(h,h[1:])));mins.append(min(a));cases+=1
 report[name]={'phase_and_schedule_cases':cases,'false_closures_after_warmup':0,'max_mouth_height_jump_after_warmup':maxjump,'minimum_aperture':min(mins)}
# Full speech then true silence: all schedules and phases must close within one UI tick after 140ms.
latencies=[]
for steps in [[30],[40],[80],[35,45]]:
 for phase in range(40):
  a=run(steps,phase,lambda t:1 if t<=1000 else 0,end=1600)
  closed=next(t for t,v in a if t>1000 and v==0);latencies.append(closed-1000)
  assert 140<=closed-1000<=140+max(steps)
report['silence_close_latency_ms']={'min':min(latencies),'max':max(latencies),'includes_ui_quantization':True}
# State exit and reentry entirely between UI frames: generation prevents stale envelope.
l.reset();l.produce(.8,0);l.consume(0);l.produce(.8,40000);assert l.consume(40000)>0
l.mode(1,41000);l.mode(4,42000);assert l.consume(80000)==0
l.produce(.4,90000);assert l.consume(120000)>0;l.mode(1,130000);assert l.consume(160000)==0
report['between_ticks_exit_reentry_generation']='pass'
# Nonflat normal speech still visibly moves.
a=[v for t,v in run([40],0,lambda t:.8 if t%600<300 else .2) if t>=500]
h=[1+math.floor(v*15+.5) for v in a];assert max(h)-min(h)>=3
report['normal_speech_height_range']=[min(h),max(h)]
(options.output/'observer-timing-results.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
# The extracted functions above are the actual candidate source, not a separate observer model.
# Concurrent producer / single UI consumer: real candidate critical sections are
# mapped to a pthread mutex. This does not model ESP32 scheduling or ISR callers.
import threading
class Sample(C.Structure):
 _fields_=[('peak',C.c_float),('last_voice_us',C.c_int64),('generation',C.c_uint32),('speaking',C.c_bool),('heard_voice',C.c_bool)]
l.muse_state_take_mouth_sample.restype=Sample
l.shared_level.restype=C.c_float
# Snapshot clears only peak; a subsequent producer update remains observable.
l.reset();l.produce(.4,1000);first=l.muse_state_take_mouth_sample()
assert abs(first.peak-.4)<1e-6 and first.last_voice_us==1000
empty=l.muse_state_take_mouth_sample();assert empty.peak==0 and empty.last_voice_us==1000 and empty.heard_voice
l.produce(.8,2000);after=l.muse_state_take_mouth_sample();assert abs(after.peak-.8)<1e-6 and after.last_voice_us==2000
l.produce(float('nan'),3000);bad=l.muse_state_take_mouth_sample();assert bad.peak==0 and bad.last_voice_us==2000
assert math.isnan(l.shared_level())  # Preserve the preexisting shared-level semantics.
l.produce(2,4000);assert l.shared_level()==1 and l.muse_state_take_mouth_sample().peak==1
l.produce(-1,5000);assert l.shared_level()==0 and l.muse_state_take_mouth_sample().last_voice_us==4000
l.mode(6,6000);off=l.muse_state_take_mouth_sample();assert not off.speaking and off.peak==0 and not off.heard_voice
l.mode(4,7000);assert l.current_mode()==6 and l.muse_state_take_mouth_sample().generation==off.generation
l.mode(1,8000);l.mode(4,9000);assert l.current_mode()==4 and l.muse_state_take_mouth_sample().generation!=off.generation
report['snapshot_drain_normalization_shared_level_and_off_guard']='pass'
l.reset();done=threading.Event();observed=[]
def writer():
 for i in range(1,10001):l.produce((i%100+1)/100,i*1000)
 done.set()
th=threading.Thread(target=writer);th.start()
while not done.is_set():observed.append(l.muse_state_take_mouth_sample())
th.join();observed.append(l.muse_state_take_mouth_sample())
assert observed[-1].last_voice_us==9999000
assert all(a.last_voice_us<=b.last_voice_us for a,b in zip(observed,observed[1:]))
assert max(x.peak for x in observed)==1
assert all(x.generation==1 and x.speaking for x in observed)
report['concurrent_capture_10000_updates']='pass; mutex host model only'
(options.output/'observer-timing-results.json').write_text(json.dumps(report,indent=2));print('PASS concurrent capture 10000 updates')
