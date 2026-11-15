#pragma once
// Original EngineLink-inspired visual styling. This is an independent lab UI.
// No external assets/CDN. Compatible with isolated AP use.
static const char WIFI_LAB_HTML[] PROGMEM = R"ELLAB(
<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>EngineLink | ASYNC LAB v3.1</title>
<style>
:root{color-scheme:dark;--bg:#091723;--panel:#112737;--line:#284151;--accent:#45d8ce;--muted:#97b3c2;--text:#e6f4f5;--orange:#f6be72}
*{box-sizing:border-box}html,body{margin:0;min-height:100%;font:14px system-ui,Segoe UI,sans-serif;background:var(--bg);color:var(--text)}
header{position:sticky;top:0;background:#091723eb;backdrop-filter:blur(10px);border-bottom:1px solid var(--line);z-index:5}
.head{max-width:1100px;margin:auto;padding:14px 20px;display:flex;justify-content:space-between;align-items:center;gap:16px}
.logo{font-weight:750;font-size:18px;letter-spacing:.1px}.logo b{color:var(--accent)}.tag{color:var(--orange);font-size:11px;font-weight:650}
main{max-width:1100px;margin:0 auto;padding:16px 20px 50px}
h1{font-size:23px;font-weight:650;margin:8px 0 4px}h2{font-size:15px;margin:0 0 12px}p{line-height:1.55;color:var(--muted)}
.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px}
.panel{background:var(--panel);border:1px solid var(--line);border-radius:12px;padding:17px;margin:12px 0;min-width:0}
.panel.span{grid-column:1/-1}.banner{border-left:3px solid var(--orange);padding:8px 12px;background:#422d1c;color:#ffdbad;border-radius:5px}
.metrics{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px}
.metric{background:#0d1e2b;padding:12px;border-radius:9px;min-width:0}.metric .val{font-size:20px;font-weight:720;color:#91e7e2;overflow-wrap:anywhere}
.metric .caption{font-size:11px;color:var(--muted);margin-bottom:5px}.ok{color:#8ce5ae}.bad{color:#ffbb80}
button{background:var(--accent);border:0;border-radius:7px;color:#063039;padding:10px 13px;font-weight:700;cursor:pointer;margin:3px}
button:disabled{opacity:.4;cursor:default}button.secondary{background:#21475b;color:#e4f3f8}
input{border-radius:6px;background:#081b2a;border:1px solid #376075;color:white;padding:10px;min-width:0}
label{display:block;margin:8px 0;color:#a8c3cd}
.forms{display:flex;gap:8px;flex-wrap:wrap;align-items:center}
.forms>input{flex:1;min-width:120px}pre{font:11px ui-monospace,monospace;white-space:pre-wrap;word-break:break-word;color:#b3d3dc;line-height:1.5}
#chart{width:100%;height:116px;border-radius:7px;background:#0c202c}
a{color:#71e0d9}footer{color:var(--muted);font-size:12px;margin-top:22px}
@media(max-width:700px){.grid{grid-template-columns:1fr}.metrics{grid-template-columns:repeat(2,minmax(0,1fr))}.panel.span{grid-column:auto}}
</style></head><body>
<header><div class="head"><div class="logo">Engine<b>Link</b> <span class="tag">ASYNC LAB v3.1</span></div><div id="dot">● Loading</div></div></header>
<main><h1>Connection diagnostics</h1>
<p>ESP32-S3-WROOM-1-N8R8 • AsyncTCP + ESPAsyncWebServer • WebSocket live telemetry • OTA retained • read-only hardware test</p>
<div class="banner">Experimental firmware. No J1708 transmission. No engine control. Do not use on an operating boat. Full USB flashing erases internal data; OTA uses the app partition.</div>
<section class="panel" style="margin:16px 0">
<h2>Connection and recovery status</h2>
<p>No JSON export needed. This text comes directly from the currently running firmware.</p>
<pre id="health">Connecting to EngineLink…</pre>
<div class="forms"><button class="secondary" onclick="refreshHealth()">Read device health</button>
<a href="/lab/health" target="_blank" rel="noopener">Open plain-text status</a>
<a href="/lab/status" target="_blank" rel="noopener">Open JSON (optional)</a></div>
<pre id="errors" style="color:#ffbf96"></pre></section>
<div class="grid">
<section class="panel span"><h2>Connection performance</h2>
<div class="metrics">
<div class="metric"><div class="caption">Last download (KiB/s)</div><div class="val" id="speed">—</div></div>
<div class="metric"><div class="caption">HTTP response (ms)</div><div class="val" id="latency">—</div></div>
<div class="metric"><div class="caption">Free heap (KiB)</div><div class="val" id="heap">—</div></div>
</div><p>Tests use the local device only, without internet. Run multiple times from the same computer to compare versions.</p>
<div class="forms"><button onclick="benchmark(64)">Test 64 KiB</button><button onclick="benchmark(512)">Test 512 KiB</button><button onclick="benchmark(2048)">Test 2 MiB</button><button class="secondary" onclick="latencyTest()">10× HTTP RTT</button></div>
<div id="benchmsg" style="color:var(--muted);margin-top:9px"></div><p id="wsStatus">WebSocket: connecting…</p><canvas id="chart" width="800" height="120"></canvas></section>
<section class="panel"><h2>Access point & router</h2><pre id="network">Loading…</pre>
<label>Router SSID</label><input id="ssid" maxlength="32" placeholder="Wi-Fi name">
<label>Router password</label><input id="pass" type="password" placeholder="Password">
<div><button onclick="saveRouter()">Connect router</button><button class="secondary" onclick="forgetRouter()">Forget</button></div>
<p>Test AP remains active as <strong>EngineLink-ChatGPT</strong> at 192.168.4.1. AP+STA shares one Wi-Fi radio/channel.</p></section>
<section class="panel"><h2>ESP32-S3 hardware</h2><pre id="hardware">Loading…</pre><p>PSRAM, memory, CPU clock, Wi-Fi channel, signal and driver counters. No settings are changed by refreshing this page.</p></section>
<section class="panel span"><h2>EngineLink API-shaped test</h2>
<p>WebSocket live updates every second plus /api/live and /api/state endpoints, resembling the normal page's networking status without pretending to have trip or engine data. Original visual UI at <a href="/original">/original</a> is for styling comparison only.</p>
<pre id="api">Loading…</pre></section>
<section class="panel"><h2>Export evidence</h2><button onclick="saveReport()">Download full JSON report</button><p>Export includes software version, Wi-Fi statistics and hardware status, but not Wi-Fi password.</p></section>
<section class="panel"><h2>Firmware update — app-only OTA</h2>
<p>This updater is essential for remote recovery. Install only a trustworthy EngineLink app-only .bin (not the 8 MiB USB firmware image).</p>
<input id="fw" type="file" accept=".bin"><label><input id="agree" type="checkbox"> I have a valid recovery plan and accept the OTA risk</label>
<button id="flash" onclick="flashFirmware()">Flash application image</button>
<pre id="ota">No update in progress</pre></section>
</div><footer>ASYNC LAB 0.0.3.1 • Offline interface • ESPAsyncWebServer + WebSocket • <a href="/lab/status">Raw status</a> • <a href="/lab/report">Report</a></footer>
</main><script>
let measurements=[];
const $=id=>document.getElementById(id);
const fmt=v=>Number.isFinite(v)?v.toFixed(1):'—';
async function getJson(url){
  const r=await fetch(url+'?t='+Date.now(),{cache:'no-store',redirect:'error'});
  const contentType=r.headers.get('content-type')||'unknown';
  const raw=await r.text();
  if(!r.ok)throw Error('HTTP '+r.status+' for '+url+': '+raw.slice(0,120));
  if(!contentType.includes('json')||!raw.trim().startsWith('{'))
    throw Error(url+' returned '+contentType+' rather than JSON; HTTP '+r.status+'; starts with: '+raw.slice(0,140));
  try{return JSON.parse(raw)}catch(e){throw Error('Invalid JSON from '+url+': '+e.message+'; starts with '+raw.slice(0,140))}
}
let ws=null,lastLive=0,wsRetry=1000,wsTimer=null;
async function refreshHealth(){
 try{
   const r=await fetch('/lab/health?t='+Date.now(),{cache:'no-store',redirect:'error'});
   const raw=await r.text();
   $('health').textContent=(r.ok?'HTTP '+r.status+'\n':'HTTP ERROR '+r.status+'\n')+raw.slice(0,1800);
 }catch(e){$('health').textContent='Cannot reach /lab/health: '+e.message}
}
function render(d){
  lastLive=Date.now();
  $('dot').textContent='● '+(d.network.ap.clients>0?'AP connected':'AP online');
  $('errors').textContent='';
  if(d.boot){$('health').textContent='VERSION='+d.version+'\nBOOT_VERIFIED='+d.boot.verified+
    '\nOTA_PENDING='+d.boot.pendingOta+'\nSTATE='+d.boot.state+
    '\nHTTP_CLIENT_REQUESTS='+d.boot.healthRequests+'\nUPTIME_MS='+d.uptimeMs;}
  $('heap').textContent=fmt(d.hardware.freeHeap/1024);
  $('network').textContent=JSON.stringify(d.network,null,2);
  $('hardware').textContent=JSON.stringify(d.hardware,null,2);
  $('api').textContent=JSON.stringify({version:d.version,uptimeMs:d.uptimeMs,clients:d.network.ap.clients,connections:d.http,ws:d.websocket,engines:d.engines,nmea:d.nmea},null,2);
}
async function refresh(){
  try{render(await getJson('/lab/status'))}
  catch(e){
    $('dot').textContent='● Status unavailable';
    $('errors').textContent='Status error: '+e.message;
    await refreshHealth();
  }
}
function connectWs(){
  if(ws && (ws.readyState===WebSocket.CONNECTING || ws.readyState===WebSocket.OPEN))return;
  ws=new WebSocket((location.protocol==='https:'?'wss://':'ws://')+location.host+'/ws');
  ws.onopen=()=>{wsRetry=1000;$('wsStatus').textContent='WebSocket: connected • automatic updates every 1 s'};
  ws.onmessage=e=>{try{render(JSON.parse(e.data))}catch(err){
     $('wsStatus').textContent='WebSocket data error: '+err.message;
     $('errors').textContent='Unexpected WebSocket payload: '+String(e.data).slice(0,180);
  }};
  ws.onclose=()=>{
    $('wsStatus').textContent='WebSocket: disconnected, retrying…';
    clearTimeout(wsTimer);wsTimer=setTimeout(connectWs,wsRetry);
    wsRetry=Math.min(10000,wsRetry*2);
  };
  ws.onerror=()=>{try{ws.close()}catch(e){}};
}
function draw(){
let c=$('chart'),ctx=c.getContext('2d');ctx.clearRect(0,0,c.width,c.height);
ctx.strokeStyle='#274f61';ctx.beginPath();ctx.moveTo(0,100);ctx.lineTo(c.width,100);ctx.stroke();
let a=measurements.slice(-24);if(!a.length)return;
let max=Math.max(25,...a);ctx.strokeStyle='#45d8ce';ctx.lineWidth=3;ctx.beginPath();
a.forEach((v,i)=>{let x=20+i*(c.width-40)/Math.max(1,a.length-1),y=106-v/max*88;i?ctx.lineTo(x,y):ctx.moveTo(x,y)});ctx.stroke();
ctx.fillStyle='#a2e3e6';ctx.fillText('KiB/s • max '+Math.round(max),12,14);
}
async function benchmark(kb){
$('benchmsg').textContent='Transferring '+kb+' KiB…';
try{let t=performance.now(),r=await fetch('/api/speedtest?kb='+kb+'&r='+Date.now(),{cache:'no-store'});
if(!r.ok)throw Error('HTTP '+r.status);let b=await r.arrayBuffer(),ms=performance.now()-t;
if(b.byteLength!==kb*1024)throw Error('Incomplete transfer: '+b.byteLength);
let speed=b.byteLength/1024/ms*1000;measurements.push(speed);$('speed').textContent=fmt(speed);
$('benchmsg').textContent=kb+' KiB in '+fmt(ms)+' ms = '+fmt(speed)+' KiB/s';draw();
}catch(e){$('benchmsg').textContent=e.message}
}
async function latencyTest(){
$('benchmsg').textContent='Measuring HTTP latency…';try{
let a=[];for(let i=0;i<10;i++){let t=performance.now();let r=await fetch('/api/rtt?i='+i+'&t='+Date.now(),{cache:'no-store'});if(!r.ok)throw Error('HTTP '+r.status);a.push(performance.now()-t)}
a.sort((x,y)=>x-y);let med=(a[4]+a[5])/2;$('latency').textContent=fmt(med);
$('benchmsg').textContent='10 requests: median '+fmt(med)+' ms, min '+fmt(a[0])+' ms, max '+fmt(a[9])+' ms';
}catch(e){$('benchmsg').textContent=e.message}
}
async function post(url,body){
let r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
let d=await r.json();alert(d.message||JSON.stringify(d));await refresh()
}
async function saveRouter(){let s=$('ssid').value,p=$('pass').value;if(!s)return alert('Enter router SSID');
await post('/lab/router','ssid='+encodeURIComponent(s)+'&password='+encodeURIComponent(p))}
async function forgetRouter(){if(confirm('Forget saved lab router settings?'))await post('/lab/forget','')}
function saveReport(){window.location='/lab/report?t='+Date.now()}
async function flashFirmware(){
let file=$('fw').files[0],out=$('ota');if(!file||!$('agree').checked)return alert('Choose an app-only .bin and confirm');
if(file.size<100000||file.size>3200000)return alert('Invalid app image size');
if(!confirm('Flash '+file.name+'? Your Wi-Fi connection will drop during reboot.'))return;
out.textContent='Uploading '+file.name+' ('+file.size+' bytes). DO NOT DISCONNECT.';
try{let b=await file.arrayBuffer();if(new Uint8Array(b)[0]!==0xE9)throw Error('Not an ESP32 app image');
let r=await fetch('/lab/ota',{method:'POST',headers:{'Content-Type':'application/octet-stream','X-EngineLink-Test':'CONFIRM-OTA'},body:b});
let j=await r.json();out.textContent=JSON.stringify(j);if(r.ok)out.textContent+=' • Reboot imminent. Reconnect to the network in a moment.';
}catch(e){out.textContent='OTA failed: '+e.message}
}
refreshHealth();refresh();connectWs();
// Fallback: if WebSocket data stalls, use a sparse HTTP health request.
setInterval(()=>{if(Date.now()-lastLive>7000){refresh();refreshHealth()}},5000);
</script></body></html>
)ELLAB";