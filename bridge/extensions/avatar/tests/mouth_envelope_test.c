#include "actell_mouth_envelope.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 actell_mouth_envelope_t s={0};muse_mouth_sample_t p={.peak=1,.generation=1,.speaking=true,.heard_voice=true};
 for(int i=0;i<=25;i++){p.last_voice_us=i*40000;actell_mouth_step(&s,&p,i*40000);}
 p.peak=0;
 float v100=actell_mouth_step(&s,&p,1100000);assert(v100>0);
 assert(actell_mouth_step(&s,&p,1200000)==0);
 assert(actell_mouth_step(&s,&p,1500000)==0);
 p.speaking=false;assert(actell_mouth_step(&s,&p,1600000)==0&&s.value==0);
 p.speaking=true;p.generation++;p.peak=.1;p.last_voice_us=1700000;
 assert(actell_mouth_step(&s,&p,1700000)==0);assert(actell_mouth_step(&s,&p,1740000)>0);
 p.generation++;assert(actell_mouth_step(&s,&p,1780000)==0);
 assert(actell_mouth_step(&s,&p,1700000)==0); // clock reversal
 p.peak=NAN;assert(isfinite(actell_mouth_step(&s,&p,1800000)));
 p.peak=INFINITY;assert(isfinite(actell_mouth_step(&s,&p,1840000)));
 p.heard_voice=false;assert(actell_mouth_step(&s,&p,1880000)==0);
 puts("PASS 100/200/500ms silence, state reset/reentry, timestamp reversal, nonfinite");
}
