'use strict';
// Optional display only. Never submits/retries/cancels a tool or delays its RPC.
(() => {
  const toggle = document.getElementById('phoneActivityToggle');
  const status = document.getElementById('phoneActivityStatus');
  if (!toggle || !status) return;
  let token = '', enabled = false, uncertain = false, stopped = false, inFlight = false, toggling = false, timer = null;
  let revision = 0, lastSequence = -1;
  const labels = {phone_read:'看手机', phone_write:'操作手机', idle:'无扩展动作，使用正常角色状态'};
  function show(value) {
    if (!value || !Number.isInteger(value.sequence) || value.sequence < lastSequence) return;
    lastSequence=value.sequence;uncertain=false;enabled=value.enabled===true;
    toggle.checked = enabled;
    status.textContent = enabled ? `本地模拟事件：${labels[value.kind] || labels.idle}；未发送到板端。` : '扩展动作已关闭；正常七种角色动画不受影响。';
  }
  async function refresh() {
    if (stopped || inFlight || toggling || !token) return;
    const requestedRevision=revision;
    inFlight = true;
    try {
      const response = await fetch('/api/presentation', {credentials:'same-origin', cache:'no-store'});
      if (!response.ok) throw new Error('presentation unavailable');
      const value=await response.json();
      if(requestedRevision===revision&&!toggling)show(value.result);
    } catch (_) {if(requestedRevision===revision&&!toggling)status.textContent='可选表现状态暂无法确认；工具链不受影响。';}
    finally {inFlight=false;}
  }
  toggle.addEventListener('change', async () => {
    if(toggling)return;
    const requestedRevision=++revision;
    toggling=true;toggle.disabled = true;
    try {
      const response = await fetch('/api/presentation', {method:'POST',credentials:'same-origin',headers:{'Content-Type':'application/json','X-CSRF-Token':token},body:JSON.stringify({enabled:toggle.checked})});
      if (!response.ok) throw new Error('presentation unavailable');
      const value=await response.json();if(requestedRevision===revision)show(value.result);
    } catch (_) {
      uncertain=true;toggle.checked=enabled;
      status.textContent='开关结果未确认；保留上次已确认显示，工具链不受影响。';
    } finally {toggling=false;toggle.disabled=false;void refresh();}
  });
  window.addEventListener('actell-rpc-settled', () => {if(enabled||uncertain)void refresh();});
  window.addEventListener('pagehide', () => {stopped=true;clearInterval(timer);});
  function initialize() {
    if (token || typeof csrf === 'undefined' || !csrf) return;
    token=csrf;toggle.disabled=false;void refresh();
    timer=setInterval(()=>{if(enabled||uncertain)void refresh();},500);
  }
  window.addEventListener('actell-session-ready', initialize);
  initialize();
})();
