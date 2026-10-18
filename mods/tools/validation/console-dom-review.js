// Host-only DOM stubs. No browser or real network is used.
const fs=require('fs'), vm=require('vm'), assert=require('assert');
const path=require('path');
const root=process.argv[2] || path.resolve(__dirname,'../../..');
const source=fs.readFileSync(path.join(root,'mods/host/web_console/app.js'),'utf8');
const html=fs.readFileSync(path.join(root,'mods/host/web_console/index.html'),'utf8');
const els=Object.fromEntries([...html.matchAll(/id="([^"]+)"/g)].map(m=>[m[1],{disabled:false,hidden:true,value:'',textContent:'',src:'',removeAttribute(k){delete this[k];}}]));
const requestLog=[],revoked=[];let runDeadline=0,uid=0, resolveCall=null, holdCall=false;
function response(value,status=200,blob=false){return {ok:status>=200&&status<300,json:async()=>value,blob:async()=>({synthetic:true})};}
async function fetch(url,options={}){
 if(url==='/api/session')return response({mode:'mock',csrf_token:'csrf',natural_language_supported:false});
 if(url.startsWith('/api/image'))return response({},200,true);
 const body=JSON.parse(options.body);requestLog.push({url,...body});
 if(url==='/api/natural-language')return response({ok:false,error:'no real model'},501);
 if(body.op==='runs.open'){runDeadline=body.params.deadline_ms;return response({ok:true,result:{run_id:body.params.run_id,state:'open'}});}
 if(body.op==='runs.cancel')return response({ok:true,result:{run_id:body.params.run_id,state:'cancelled'}});
 if(body.op==='tools.call'){
   assert(body.params.deadline_ms<=runDeadline,'call deadline exceeds run');
   const payload={ok:true,result:{run_id:body.params.run_id,call_id:body.params.call_id,state:'succeeded',result:body.params.tool==='screenshot'?{image_id:'img',generation:1}:{mock_only:true,apps:[]}}};
   if(holdCall){await new Promise(r=>resolveCall=r);}
   return response(payload);
 }
 throw Error('unexpected request');
}
const context={document:{getElementById:id=>{assert(els[id],`Missing DOM ${id}`);return els[id];}},crypto:{randomUUID:()=>`id-${++uid}`},fetch,Date,JSON,URLSearchParams,URL:{createObjectURL:()=>`blob:${uid}`,revokeObjectURL:u=>revoked.push(u)},window:{addEventListener(){}},setTimeout,clearTimeout};
vm.createContext(context);vm.runInContext(source,context);
const tick=()=>new Promise(r=>setImmediate(r));
(async()=>{await tick();assert(!els.shot.disabled);assert(els.cancel.disabled);
 await els.apps.onclick();assert(requestLog.some(x=>x.params?.tool==='list_apps'));
 await els.shot.onclick();assert(!els.preview.hidden);assert(els.image.src.startsWith('blob:'));
 await els.cancel.onclick();assert(els.preview.hidden);assert(!els.image.src);assert(revoked.length);
 holdCall=true;const pending=els.shot.onclick();await tick();assert(resolveCall);await els.cancel.onclick();resolveCall();await pending;assert(els.preview.hidden,'late screenshot resurrects canceled preview');
 els.prompt.value='<img src=x onerror=alert(1)>';await els.textForm.onsubmit({preventDefault(){}});assert(els.output.textContent.includes('真实 Muse 文字尚未集成'));
 assert(!source.includes('innerHTML'),'unsafe HTML insertion');
 console.log('PASS 6 DOM-stub checks: bindings, run/call deadline, apps+image, cancel cleanup, late result fence, honest unsupported text; no rendered-browser claim');
})().catch(e=>{console.error(e);process.exitCode=1});
