// Dependency-free DOM/request-order regression. No browser network is used.
const fs=require('fs'),vm=require('vm'),assert=require('assert');
const source=fs.readFileSync(process.argv[2],'utf8');
function setup(){
  const events={},pending=[],timers=[];
  const toggle={checked:false,disabled:true,addEventListener:(n,f)=>events[n]=f};
  const status={textContent:''};
  const context={document:{getElementById:n=>n==='phoneActivityToggle'?toggle:status},csrf:'test-csrf',
    window:{addEventListener(){}},setInterval:f=>{timers.push(f);return 1;},clearInterval(){},
    fetch:(url,opts={})=>new Promise((resolve,reject)=>pending.push({url,opts,resolve,reject}))};
  vm.createContext(context);vm.runInContext(source,context);
  return {events,pending,toggle,status,timers};
}
const tick=()=>new Promise(r=>setImmediate(r));
function deliver(p,result){p.resolve({ok:true,json:async()=>({result})});}
(async()=>{
  {
    const {events,pending,toggle}=setup();
    const older=pending.shift();toggle.checked=true;const changed=events.change();
    const post=pending.shift();assert.equal(post.opts.method,'POST');
    deliver(post,{enabled:true,kind:'idle',sequence:2});await changed;
    assert(toggle.checked);
    deliver(older,{enabled:false,kind:'idle',sequence:1});await tick();
    assert(toggle.checked,'late GET must not overwrite a newer confirmed toggle');
  }
  {
    const {events,pending,toggle,status}=setup();
    deliver(pending.shift(),{enabled:true,kind:'idle',sequence:2});await tick();
    toggle.checked=false;const changed=events.change();pending.shift().reject(new Error('lost response'));
    await changed;
    assert(toggle.checked,'failed disable keeps last confirmed checkbox rather than claiming off');
    assert(status.textContent.includes('未确认'),'uncertain write must be labelled unconfirmed');
    deliver(pending.shift(),{enabled:false,kind:'idle',sequence:3});await tick();
    assert(!toggle.checked,'subsequent confirmed GET reconciles unknown outcome');
  }
  console.log('PASS optional presentation response ordering and uncertain-toggle reconciliation');
})().catch(error=>{console.error(error);process.exitCode=1;});
