// Independent in-memory DOM/fetch regression. No browser or network.
const fs=require('fs'),vm=require('vm'),assert=require('assert');
const path=require('path');
const target=process.argv[2] || path.resolve(__dirname,'../../host/web_console/presentation.js');
const source=fs.readFileSync(target,'utf8');
const events={};const toggle={checked:false,disabled:true,addEventListener:(n,f)=>events[n]=f};const status={textContent:''};const pending=[];
const context={document:{getElementById:n=>n==='phoneActivityToggle'?toggle:status},csrf:'test-csrf',window:{addEventListener(){}},setInterval:()=>1,clearInterval(){},fetch:(url,opts={})=>new Promise((resolve,reject)=>pending.push({url,opts,resolve,reject}))};
vm.createContext(context);vm.runInContext(source,context);
const tick=()=>new Promise(r=>setImmediate(r));
function deliver(p,result){p.resolve({ok:true,json:async()=>({result})});}
(async()=>{assert.equal(pending.length,1);const older=pending.shift();toggle.checked=true;const changed=events.change();assert.equal(pending.length,1);const post=pending.shift();assert.equal(post.opts.method,'POST');deliver(post,{enabled:true,kind:'idle',sequence:2});await changed;assert(toggle.checked);deliver(older,{enabled:false,kind:'idle',sequence:1});await tick();assert(toggle.checked,'late GET rolled toggle back despite newer confirmed state');console.log('PASS optional presentation GET/POST out-of-order state fence');})().catch(e=>{console.error(e);process.exitCode=1});
