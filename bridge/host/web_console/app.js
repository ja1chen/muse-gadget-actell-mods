'use strict';
const $ = id => document.getElementById(id);
let csrf = '', currentRun = null, runDeadline = 0, opening = false, cancelling = false, busy = false, logs = [], imageURL = null;
const id = () => crypto.randomUUID();
function log(label, value) {
  logs.push(`${new Date().toLocaleTimeString()} ${label}\n${typeof value === 'string' ? value : JSON.stringify(value, null, 2)}`);
  logs = logs.slice(-40); $('output').textContent = logs.join('\n\n').slice(-40000);
}
function controls() {
  ['apps','shot','send'].forEach(k => $(k).disabled = busy || cancelling || !csrf);
  $('cancel').disabled = !currentRun || !csrf || opening || cancelling;
  $('run').textContent = currentRun ? `run: ${currentRun}` : '尚未创建 run';
}
async function request(path, body) {
  const response = await fetch(path, {method: 'POST', credentials: 'same-origin', headers: {'Content-Type':'application/json','X-CSRF-Token':csrf}, body: JSON.stringify(body)});
  const value = await response.json();
  if (!response.ok || value.ok === false) throw new Error(JSON.stringify(value));
  return Object.prototype.hasOwnProperty.call(value, 'result') ? value.result : value;
}
const rpc = (op, params) => request('/api/rpc', {op, params}).finally(() => { try {window.dispatchEvent(new Event('actell-rpc-settled'));} catch (_) {} });
async function tool(name) {
  if (busy || cancelling || !csrf) return;
  busy = true; controls();
  let run;
  try {
    if (!currentRun) {
      currentRun = id(); runDeadline = Date.now()+60000; opening = true; controls();
      try { await rpc('runs.open', {run_id:currentRun, deadline_ms:runDeadline}); } finally { opening=false; controls(); }
    }
    run = currentRun;
    const call = id();
    log('提交模拟工具', {tool:name,run_id:run,call_id:call});
    const result = await rpc('tools.call', {run_id:run,call_id:call,deadline_ms:runDeadline,tool:name,arguments:{}});
    log('模拟工具结果', result);
    const payload = result.result || result;
    if (name === 'screenshot' && payload.image_id && currentRun === run) {
      const query = new URLSearchParams({run_id:run,image_id:payload.image_id,generation:payload.generation});
      const res = await fetch('/api/image?'+query, {credentials:'same-origin'});
      if (!res.ok) throw new Error('测试图读取失败');
      const blob = await res.blob();
      if (currentRun !== run) return;
      if (imageURL) URL.revokeObjectURL(imageURL);
      imageURL = URL.createObjectURL(blob); $('image').src=imageURL; $('preview').hidden=false;
    }
  } catch (error) {log('失败；不会自动重试', error.message);} finally {busy=false;controls();}
}
$('apps').onclick = () => tool('list_apps');
$('shot').onclick = () => tool('screenshot');
$('cancel').onclick = async () => {
  if (!currentRun || opening || cancelling) return;
  const run = currentRun; cancelling=true; controls();
  try {const result=await rpc('runs.cancel',{run_id:run});log('取消结果',result);if(currentRun===run){currentRun=null;runDeadline=0;if(imageURL){URL.revokeObjectURL(imageURL);imageURL=null;}$('image').removeAttribute('src');$('preview').hidden=true;}}
  catch(error){log('取消未确认；请保留原 run ID 排查',error.message);}
  finally {cancelling=false;controls();}
};
$('textForm').onsubmit = async event => {
  event.preventDefault(); const text=$('prompt').value.trim(); if(!text || busy || cancelling || !csrf)return;
  busy=true;controls();
  try{log('文字通路状态',await request('/api/natural-language',{text}));}
  catch(error){log('真实 Muse 文字尚未集成',error.message);}
  finally{busy=false;controls();}
};
$('clear').onclick=()=>{logs=[];$('output').textContent='显示已清空。服务器 run 状态未改变。';};
window.addEventListener('pagehide',()=>{if(imageURL)URL.revokeObjectURL(imageURL);});
controls();
fetch('/api/session',{credentials:'same-origin',cache:'no-store'}).then(async response=>{
  if(!response.ok)throw new Error('session unavailable');const session=await response.json();
  if(session.mode!=='mock')throw new Error('此界面仅允许 mock 模式');
  csrf=session.csrf_token;try {window.dispatchEvent(new Event('actell-session-ready'));} catch (_) {}$('connection').textContent='已连接 · localhost MOCK adapter';log('会话', {mode:session.mode,natural_language_supported:session.natural_language_supported});controls();
}).catch(error=>{$('connection').textContent='连接失败';log('连接失败',error.message);controls();});
