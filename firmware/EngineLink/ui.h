#pragma once
// EngineLink web interface is embedded in flash: offline, no CDN dependencies.
static const char ENGINE_UI[] PROGMEM=R"ELWEB(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#0a1420"><meta name="apple-mobile-web-app-capable" content="yes"><meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<title>EngineLink</title>
<style>
:root{color-scheme:dark;--bg:#0a1420;--panel:#112131;--panel2:#162a3c;--inset:#0c1b29;--edge:#243d50;--edge2:#30506a;--text:#eef5f8;--muted:#8fa9ba;--subtle:#6b8596;--teal:#46cabc;--teal2:#9fe7dd;--sky:#7fb4ff;--amber:#f0c06e;--red:#f39a96;--green:#7ddcb0;--r:18px;--safe-b:env(safe-area-inset-bottom)}
*{box-sizing:border-box}html{-webkit-text-size-adjust:100%}body{margin:0;background:radial-gradient(ellipse at 75% -15%,#183747,transparent 55%),var(--bg);color:var(--text);font:14px/1.55 system-ui,-apple-system,"Segoe UI",Arial,sans-serif;font-variant-numeric:tabular-nums;-webkit-font-smoothing:antialiased;min-height:100vh}
button,input,select{font:inherit;color:inherit}button{cursor:pointer}a{color:inherit;text-decoration:none}h1,h2,h3,p{margin:0}h1{font-size:clamp(22px,3vw,30px);line-height:1.2;letter-spacing:-.5px}h2{font-size:16px;letter-spacing:-.2px}h3{font-size:14px}
.wrap{max-width:1180px;margin:0 auto;padding:0 24px calc(48px + var(--safe-b))}
.muted{color:var(--muted)}.subtle{color:var(--subtle)}.mini{font-size:12px;color:var(--muted)}.eyebrow{font-size:10.5px;text-transform:uppercase;letter-spacing:1.3px;color:var(--muted);font-weight:700}.mono{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}
header{display:flex;align-items:center;justify-content:space-between;gap:16px;height:74px;border-bottom:1px solid var(--edge)}
.brand{display:flex;align-items:center;gap:11px;font-weight:760;font-size:18px;letter-spacing:-.4px}.logo{width:36px;height:36px;border-radius:11px;background:linear-gradient(135deg,#145d66,#1ab1a1);display:grid;place-items:center}.brand small{display:block;font-size:9px;letter-spacing:2px;font-weight:650;color:var(--muted);line-height:1.1}
.hstatus{display:flex;align-items:center;gap:10px;font-size:12px;color:var(--muted)}.pill{display:inline-flex;align-items:center;gap:7px;border:1px solid var(--edge);background:#14283a;border-radius:99px;padding:5px 11px;white-space:nowrap}.dot{width:8px;height:8px;border-radius:50%;background:var(--amber);flex:none}.dot.good{background:var(--green);box-shadow:0 0 0 3px #7ddcb022}.dot.bad{background:var(--red)}.dot.off{background:#47606f}
nav.tabs{display:flex;gap:4px;padding:14px 0 20px}nav.tabs button{display:flex;align-items:center;gap:8px;background:none;border:1px solid transparent;color:var(--muted);border-radius:11px;padding:8px 14px;white-space:nowrap}nav.tabs button svg{width:17px;height:17px}nav.tabs button[aria-selected=true]{background:var(--panel2);border-color:var(--edge);color:var(--text)}nav.tabs button:hover{color:var(--text)}
.page{display:none}.page.visible{display:block;animation:fade .18s ease-out}@keyframes fade{from{opacity:.4}to{opacity:1}}
.top{display:flex;align-items:flex-end;justify-content:space-between;gap:14px;margin-bottom:18px}.top .sub{color:var(--muted);font-size:13px;margin-top:3px}
.grid{display:grid;gap:14px}.g2{grid-template-columns:repeat(2,minmax(0,1fr))}.g3{grid-template-columns:repeat(3,minmax(0,1fr))}.g4{grid-template-columns:repeat(4,minmax(0,1fr))}
.card{border:1px solid var(--edge);border-radius:var(--r);background:var(--panel);padding:20px;min-width:0}.card.alt{background:var(--panel2)}.card h2{margin-bottom:12px}.card>p{color:#b2c5cf}
.split{display:flex;align-items:center;justify-content:space-between;gap:12px}.row{display:flex;align-items:center;gap:9px;flex-wrap:wrap}.stack>*+*{margin-top:12px}
.big{font-size:clamp(46px,7vw,76px);font-weight:760;line-height:1.05;letter-spacing:-2px}.big .u,.num .u{font-size:.32em;font-weight:500;letter-spacing:0;color:var(--muted);margin-left:4px}.num{font-size:clamp(28px,3.6vw,40px);font-weight:740;letter-spacing:-1px;line-height:1.15}
.kpi{background:var(--inset);border:1px solid #213a4c;border-radius:14px;padding:15px}.kpi .num{font-size:clamp(22px,2.6vw,30px);margin-top:6px}
.chip{display:inline-flex;align-items:center;gap:6px;border:1px solid var(--edge2);border-radius:99px;background:#1a3346;color:#cfe0e7;padding:3px 10px;font-size:11px;white-space:nowrap}.chip.teal{border-color:#2f6a66;background:#163d40;color:var(--teal2)}.chip.amber{border-color:#6e5634;background:#352c1d;color:var(--amber)}.chip.red{border-color:#76403f;background:#3a2023;color:var(--red)}.chip.green{border-color:#2f6a52;background:#173a2f;color:var(--green)}
.stat{display:flex;justify-content:space-between;gap:16px;padding:9px 0;border-bottom:1px solid #1f384a;font-size:13px}.stat:last-child{border-bottom:0}.stat>span:first-child{color:var(--muted)}.stat strong{font-weight:620;text-align:right;min-width:0;overflow-wrap:anywhere}
.bar{height:6px;border-radius:6px;background:#1e3a4a;overflow:hidden;margin-top:8px}.bar>div{height:100%;width:0;background:linear-gradient(90deg,#2aa79a,#7fe0d4);border-radius:6px;transition:width .5s}
.notice{border:1px solid #574f3d;border-radius:12px;background:#2c2a24;padding:12px 14px;color:#e8cc98;font-size:12.5px}.notice.info{border-color:#2f5368;background:#152a3a;color:#b9d6e6}.notice.ok{border-color:#2f5a51;background:#17332f;color:#a5e6d6}.notice.bad{border-color:#74403f;background:#331f22;color:#f5b9b5}
.btn{display:inline-flex;align-items:center;justify-content:center;gap:8px;background:#1d384a;border:1px solid #3d5c6e;color:var(--text);border-radius:11px;min-height:42px;padding:8px 15px;font-weight:640;font-size:13px;text-align:center}.btn.primary{background:#b7ede4;color:#0f2f36;border-color:#b7ede4}.btn.ghost{background:transparent}.btn.danger{background:transparent;border-color:#7a4545;color:var(--red)}.btn.small{min-height:34px;padding:5px 11px;font-size:12px}.btn:disabled,.btn[aria-disabled=true]{opacity:.45;cursor:not-allowed;pointer-events:none}.btn:hover:not(:disabled){filter:brightness(1.12)}.btn.block{width:100%}
.field{display:flex;flex-direction:column;gap:6px;margin-bottom:14px}.field label,.lbl{font-weight:640;font-size:12px;color:#bcd0da}.input{width:100%;min-height:42px;background:var(--inset);border:1px solid #31506a;border-radius:10px;padding:9px 12px;font-size:15px}.hint{font-size:12px;color:var(--muted)}
:focus-visible{outline:2px solid #9ee6dc;outline-offset:2px}
.seg{display:inline-flex;background:var(--inset);border:1px solid var(--edge);border-radius:11px;padding:3px;gap:3px}.seg button{border:0;background:none;color:var(--muted);border-radius:8px;padding:6px 13px;font-size:13px;font-weight:600}.seg button[aria-pressed=true]{background:#24445a;color:var(--text)}
.choose{display:grid;grid-template-columns:1fr 1fr;gap:11px}.choice{padding:16px;border-radius:13px;border:1px solid #33506a;background:#152a3b;text-align:left;color:#c4d7df;min-height:112px}.choice strong{display:block;color:var(--text);font-size:15px;margin-bottom:6px}.choice.active{border-color:var(--teal);box-shadow:inset 0 0 0 1px #3fbcaf}.choice .hint{font-size:12px}
.switch{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:6px 0}.switch input{appearance:none;-webkit-appearance:none;width:44px;height:26px;border-radius:99px;background:#2a4456;position:relative;flex:none;transition:background .2s;margin:0}.switch input::after{content:"";position:absolute;top:3px;left:3px;width:20px;height:20px;border-radius:50%;background:#dbe7ec;transition:transform .2s}.switch input:checked{background:#2aa79a}.switch input:checked::after{transform:translateX(18px)}
details.more{border:1px solid var(--edge);border-radius:var(--r);background:var(--panel);margin-top:14px}details.more>summary{list-style:none;cursor:pointer;padding:16px 20px;display:flex;align-items:center;justify-content:space-between;gap:12px;font-weight:650}details.more>summary::-webkit-details-marker{display:none}details.more>summary::after{content:"";width:8px;height:8px;border-right:2px solid var(--muted);border-bottom:2px solid var(--muted);transform:rotate(45deg);transition:transform .2s;flex:none}details.more[open]>summary::after{transform:rotate(225deg)}details.more>.body{padding:0 20px 20px}details.more summary .mini{font-weight:400}
.list{background:var(--inset);border:1px solid #213c4f;border-radius:12px;max-height:380px;overflow:auto}.pkt{display:grid;grid-template-columns:64px 74px 1fr;gap:10px;align-items:center;padding:8px 11px;border-bottom:1px solid #1b3245;cursor:pointer;font-size:12.5px}.pkt:last-child{border-bottom:0}.pkt:hover,.pkt.sel{background:#16304a}.pkt .t{color:var(--subtle);font-size:11px}.pkt .hex{color:#b9ebe4;overflow-wrap:anywhere}.tag{font-size:10.5px;font-weight:700;letter-spacing:.4px;color:var(--teal)}.tag.bad{color:var(--red)}
table{width:100%;border-collapse:collapse;font-size:12.5px}th{text-align:left;color:var(--muted);font-weight:600;font-size:11px;text-transform:uppercase;letter-spacing:.6px;padding:8px 10px;border-bottom:1px solid var(--edge);white-space:nowrap}td{padding:8px 10px;border-bottom:1px solid #1b3245;vertical-align:top}tr:last-child td{border-bottom:0}.tablewrap{background:var(--inset);border:1px solid #213c4f;border-radius:12px;overflow:auto;max-height:460px}
.health{display:grid;grid-template-columns:repeat(auto-fill,minmax(230px,1fr));gap:10px}.hitem{display:flex;gap:11px;align-items:flex-start;background:var(--inset);border:1px solid #213c4f;border-radius:13px;padding:13px}.hitem .dot{margin-top:6px}.hitem b{display:block;font-size:13px}.hitem span{font-size:12px;color:var(--muted)}
.engine .head{display:flex;align-items:center;justify-content:space-between;gap:10px;margin-bottom:14px}.engine .name{font-weight:700;font-size:15px}.param{display:flex;justify-content:space-between;font-size:12.5px;padding:6px 0;border-bottom:1px dashed #22394a}.param:last-child{border-bottom:0}.param span:first-child{color:var(--muted)}
.chart{width:100%;height:180px;display:block}.legend{display:flex;gap:14px;font-size:12px;color:var(--muted);flex-wrap:wrap}.legend i{display:inline-block;width:10px;height:3px;border-radius:2px;margin-right:6px;vertical-align:middle}
.steps{display:flex;gap:6px;margin:16px 0 22px}.steps div{flex:1;height:4px;border-radius:9px;background:#2a4557}.steps div.done{background:#48bcae}
.wizard{max-width:660px;margin:28px auto 0}.wizard .card{padding:28px}.wstep{display:none}.wstep.active{display:block}
.flow{counter-reset:s;display:grid;gap:10px}.flowstep{display:flex;gap:12px;align-items:flex-start;padding:12px;border-radius:13px;border:1px solid #213c4f;background:var(--inset)}.flowstep::before{counter-increment:s;content:counter(s);width:26px;height:26px;border-radius:50%;display:grid;place-items:center;font-size:12px;font-weight:700;background:#21384a;color:var(--muted);flex:none}.flowstep.now{border-color:#3a8f86}.flowstep.now::before{background:#2aa79a;color:#062022}.flowstep.done::before{content:"✓";background:#1f4a44;color:var(--teal2)}.flowstep>div{flex:1;min-width:0}
.led{width:56px;height:56px;border-radius:50%;border:3px solid #345368;background:#172b3a;flex:none;transition:background .3s,box-shadow .3s}
.progress{height:8px;border-radius:8px;background:#1e3a4a;overflow:hidden}.progress div{height:100%;width:0;background:#7fe0d4;transition:width .2s}
.toast{position:fixed;left:50%;transform:translateX(-50%);bottom:calc(88px + var(--safe-b));border-radius:12px;border:1px solid #49887f;background:#163c3e;color:#e4fefa;padding:12px 16px;max-width:min(440px,92vw);display:none;z-index:30;box-shadow:0 14px 34px #0008;font-size:13px}
.alert{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:16px}
.g3k{grid-template-columns:repeat(3,minmax(0,1fr))}.hide{display:none!important}.right{text-align:right}.nowrap{white-space:nowrap}
footer{border-top:1px solid var(--edge);margin-top:40px;padding:18px 0;color:var(--subtle);font-size:11px}
.modal{position:fixed;inset:0;background:#030a10cc;display:none;align-items:center;justify-content:center;z-index:40;padding:20px}.modal.open{display:flex}.modal .card{max-width:440px;width:100%}
@media(max-width:900px){.g4{grid-template-columns:repeat(2,minmax(0,1fr))}.g3{grid-template-columns:1fr 1fr}}
@media(max-width:760px){.choice{min-height:0}.wrap{padding:0 16px calc(96px + var(--safe-b))}.g2,.g3{grid-template-columns:1fr}header{height:64px}.brand{font-size:16px}.logo{width:32px;height:32px}.hstatus .long{display:none}
nav.tabs{position:fixed;left:0;right:0;bottom:0;z-index:20;margin:0;padding:6px 6px calc(6px + var(--safe-b));background:#0b1825f2;border-top:1px solid var(--edge);-webkit-backdrop-filter:blur(14px);backdrop-filter:blur(14px);justify-content:space-around;gap:0}nav.tabs button{flex-direction:column;gap:3px;font-size:10.5px;padding:6px 4px;flex:1;border:0}nav.tabs button[aria-selected=true]{background:none;color:var(--teal2)}nav.tabs button svg{width:22px;height:22px}
.top{flex-direction:column;align-items:flex-start;margin-top:18px}.card{padding:16px}.wizard .card{padding:20px}.pkt{grid-template-columns:52px 1fr}.pkt .t{display:none}.wide{display:none}.g3k .num{font-size:20px}.choose{grid-template-columns:1fr}}
</style>
</head>
<body><div class="wrap">
<header><div class="brand"><span class="logo"><svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="#effffd" stroke-width="2.2" stroke-linecap="round"><path d="M3 9c3-3 5 3 9 0s6 3 9 0M3 15c3-3 5 3 9 0s6 3 9 0"/></svg></span><span>EngineLink<small>MARINE PRECISION</small></span></div>
<div class="hstatus"><span class="pill long" id="clockPill">--:--</span><span class="pill"><span id="onlineDot" class="dot"></span><span id="online">Connecting</span></span></div></header>

<div id="wizard" class="wizard hide"><div class="card">
 <div class="eyebrow">First-time setup</div><h1 id="wizardHeading" style="margin-top:10px">Welcome to EngineLink</h1>
 <p class="muted" style="margin-top:6px">Three short steps. Everything can be changed later in Settings.</p>
 <div class="steps"><div class="done" id="sb1"></div><div id="sb2"></div><div id="sb3"></div></div>
 <div class="wstep active" id="ws1">
  <h2 style="margin-bottom:12px">How should your phone connect?</h2>
  <div class="choose"><button class="choice active" type="button" data-net="ap"><strong>EngineLink Wi-Fi</strong><span class="hint">Join EngineLink directly. No router or internet needed.</span></button><button class="choice" type="button" data-net="router"><strong>Connect to a router</strong><span class="hint">EngineLink also joins your boat's Wi-Fi network.</span></button></div>
  <div id="wRouter" class="hide" style="margin-top:16px"><div class="field"><label for="wSSID">Router Wi-Fi name (SSID)</label><input class="input" id="wSSID" autocomplete="off" autocapitalize="off"></div><div class="field"><label for="wPass">Router password</label><input class="input" id="wPass" type="password" autocomplete="off"></div></div>
  <div class="field" style="margin-top:16px"><label for="wAP">New EngineLink Wi-Fi password <span class="subtle">· optional</span></label><input class="input" id="wAP" type="password" minlength="8" placeholder="Leave blank to keep 12345678"><span class="hint">At least 8 characters. Your phone will need to re-join with the new password.</span></div>
 </div>
 <div class="wstep" id="ws2">
  <h2 style="margin-bottom:6px">Which connector is the port engine?</h2><p class="muted" style="margin-bottom:14px">Start one engine and watch which connector shows activity below.</p>
  <div class="grid g2" style="margin-bottom:14px"><div class="kpi"><div class="eyebrow">J7 · Channel 1</div><div class="num" id="wAct0">—</div><div class="mini" id="wActT0">No traffic</div></div><div class="kpi"><div class="eyebrow">J6 · Channel 2</div><div class="num" id="wAct1">—</div><div class="mini" id="wActT1">No traffic</div></div></div>
  <div class="field"><label for="wPort">Port engine is on</label><select class="input" id="wPort"><option value="1">J7 · Channel 1 (starboard on J6)</option><option value="2">J6 · Channel 2 (starboard on J7)</option><option value="0">Not sure yet · decide later</option></select></div>
  <div class="notice info">EngineLink only listens. It never sends anything to the engines.</div>
 </div>
 <div class="wstep" id="ws3">
  <h2 style="margin-bottom:6px">Date and time</h2><p class="muted" style="margin-bottom:14px">EngineLink has no clock battery. It takes the time from your phone, GPS or the internet.</p>
  <div class="field"><label for="wTime">Time source</label><select class="input" id="wTime"><option value="auto">Automatic (GPS, then internet, then phone)</option><option value="phone">Phone / browser</option><option value="gps">NMEA 2000 GPS only</option><option value="ntp">Internet (via router) only</option><option value="manual">None · use uptime only</option></select></div>
  <div class="notice info">The EngineLink Wi-Fi always stays on, so you can always reconnect at <b>192.168.4.1</b>.</div>
 </div>
 <div class="split" style="margin-top:22px"><button class="btn ghost" id="wBack" disabled>Back</button><button class="btn primary" id="wNext">Continue</button></div>
 <p id="wErr" class="mini" style="color:var(--red);margin-top:12px"></p>
</div></div>

<div id="app" class="hide">
<nav class="tabs" aria-label="Main">
 <button data-page="fuel" aria-selected="true"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M12 3c3.5 4.4 6 7.6 6 11a6 6 0 0 1-12 0c0-3.4 2.5-6.6 6-11z"/></svg>Fuel</button>
 <button data-page="traffic"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M3 12h4l3-7 4 14 3-7h4"/></svg>Data</button>
 <button data-page="history"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="8.5"/><path d="M12 7.5V12l3 2"/></svg>History</button>
 <button data-page="device"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><rect x="6" y="6" width="12" height="12" rx="2"/><path d="M9 2v4M15 2v4M9 18v4M15 18v4M2 9h4M2 15h4M18 9h4M18 15h4"/></svg>Device</button>
 <button data-page="settings"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M4 6h10M18 6h2M4 12h4M12 12h8M4 18h12M20 18h0"/><circle cx="16" cy="6" r="2"/><circle cx="10" cy="12" r="2"/><circle cx="18" cy="18" r="2"/></svg>Settings</button>
</nav>
<div id="alert" class="notice alert hide"><span id="alertText"></span><button class="btn small" id="alertAction"></button></div>
<main>
<!-- FUEL -->
<section id="fuel" class="page visible">
 <div class="top"><div><div class="eyebrow">Engine instruments</div><h1>Fuel Overview</h1><div class="sub">Live values from the engine ECUs. Missing data is shown as —, never as 0.</div></div><span class="chip" id="fuelChip">Waiting for data</span></div>
 <div class="card alt">
  <div class="split" style="align-items:flex-start"><div><div class="eyebrow">Total fuel flow</div><div class="big" id="total">—<span class="u">L/h</span></div><div class="mini" id="totalNote">Waiting for fuel rate from both engines.</div></div>
  <div class="grid" style="grid-template-columns:auto auto;gap:22px;text-align:right"><div><div class="eyebrow">Economy</div><div class="num" id="lnm">—<span class="u">L/nm</span></div></div><div><div class="eyebrow">Speed</div><div class="num" id="sog">—<span class="u">kn</span></div></div></div></div>
 </div>
 <div class="grid g2" style="margin-top:14px">
  <article class="card engine" id="eng0"></article><article class="card engine" id="eng1"></article>
 </div>
 <div class="card" style="margin-top:14px">
  <div class="split" style="margin-bottom:10px"><h2 style="margin:0">Last hour</h2><div class="seg" id="chartMode"><button aria-pressed="true" data-m="fuel">Fuel</button><button aria-pressed="false" data-m="rpm">RPM</button></div></div>
  <svg class="chart" id="chart" viewBox="0 0 600 180" preserveAspectRatio="none" aria-label="Trend"></svg>
  <div class="split"><div class="legend"><span><i style="background:var(--teal)"></i>Port</span><span><i style="background:var(--sky)"></i>Starboard</span></div><span class="mini" id="chartNote">Samples every 10 s</span></div>
 </div>
 <div class="grid g2" style="margin-top:14px">
  <div class="card"><div class="split" style="margin-bottom:6px"><h2 style="margin:0">Trip</h2><button class="btn small ghost" id="tripReset">Reset trip</button></div>
   <div class="mini" id="tripSince" style="margin-bottom:10px">—</div>
   <div class="grid g2"><div class="kpi"><div class="eyebrow">Fuel used</div><div class="num" id="tripFuel">—</div></div><div class="kpi"><div class="eyebrow">Engine time</div><div class="num" id="tripTime">—</div></div></div>
   <div class="stat" style="margin-top:8px"><span>Port / starboard</span><strong id="tripSplit">—</strong></div>
  </div>
  <div class="card"><h2>Lifetime</h2>
   <div class="stat"><span>Fuel recorded · J7</span><strong id="life0">—</strong></div>
   <div class="stat"><span>Fuel recorded · J6</span><strong id="life1">—</strong></div>
   <div class="stat"><span>Engine hours · J7 / J6</span><strong id="lifeH">—</strong></div>
   <div class="stat"><span>This boot</span><strong id="bootFuel">—</strong></div>
   <p class="mini" style="margin-top:8px">Counted only between valid ECU readings at most 5 s apart. Gaps are not filled in, so totals can be lower than reality.</p>
  </div>
 </div>
</section>

<!-- DATA / TRAFFIC -->
<section id="traffic" class="page">
 <div class="top"><div><div class="eyebrow">Bus observability · receive only</div><h1>Engine Data</h1><div class="sub">See exactly what each J1708 connector receives.</div></div><div class="seg" id="chSeg"><button aria-pressed="true" data-ch="0">J7</button><button aria-pressed="false" data-ch="1">J6</button></div></div>
 <div class="grid g4" style="margin-bottom:14px">
  <div class="kpi"><div class="eyebrow">Messages / s</div><div class="num" id="rate">—</div></div>
  <div class="kpi"><div class="eyebrow">Total received</div><div class="num" id="frames">—</div></div>
  <div class="kpi"><div class="eyebrow">Checksum errors</div><div class="num" id="invalid">—</div></div>
  <div class="kpi"><div class="eyebrow">Lost in buffer</div><div class="num" id="drops">—</div></div>
 </div>
 <div class="grid g2">
  <div class="card"><div class="split" style="margin-bottom:12px"><h2 style="margin:0">Live messages</h2><button class="btn small ghost" id="pauseBtn">Pause</button></div>
   <div class="list" id="pktList"><div class="mini" style="padding:14px">Waiting for J1708 messages…</div></div>
   <p class="mini" style="margin-top:10px">Tap a message to decode it. Recording continues in the background while paused.</p></div>
  <div class="card"><div class="split" style="margin-bottom:12px"><h2 style="margin:0">Message details</h2><button class="btn small ghost" id="copyBtn">Copy HEX</button></div>
   <div class="mono" id="dHex" style="background:var(--inset);border:1px solid #213c4f;border-radius:11px;padding:13px;color:#b9ebe4;overflow-wrap:anywhere">Select a message</div>
   <div class="stat" style="margin-top:8px"><span>Source (MID)</span><strong id="dMid">—</strong></div><div class="stat"><span>Checksum</span><strong id="dSum">—</strong></div><div class="stat"><span>Received</span><strong id="dTime">—</strong></div>
   <div id="dPids" style="margin-top:10px"></div></div>
 </div>
 <div class="card" style="margin-top:14px"><div class="split" style="margin-bottom:6px"><h2 style="margin:0">Bus discovery</h2><span class="chip" id="pidCount">0 parameters</span></div>
  <p class="mini" style="margin-bottom:12px">Every parameter (PID) the ECU has sent on this connector, with its latest raw bytes. "Standard value" uses SAE J1587 scaling and is <b>not yet verified</b> for your engines.</p>
  <div class="tablewrap"><table><thead><tr><th>MID</th><th>PID</th><th>Parameter</th><th>Raw</th><th class="right">Value</th><th class="right wide">Count</th><th class="right wide">Age</th></tr></thead><tbody id="pidRows"><tr><td colspan="7" class="mini">Nothing received yet.</td></tr></tbody></table></div>
 </div>
 <div class="card" style="margin-top:14px" id="testCard">
  <div class="split"><div><div class="eyebrow">Passive capture · guided</div><h2 style="margin:6px 0 0">Engine Test &amp; Calibration</h2></div><span class="chip" id="testChip">Ready</span></div>
  <p style="margin:10px 0 14px">Record raw data while you run the engine at a few steady RPM levels. With reference readings from your own instruments, the real fuel scaling can be identified later.</p>
  <div class="notice" style="margin-bottom:14px">Only run engines with the vessel safely secured. Keep jumpers SJ1/SJ2 open and confirm both isolated 5 V supplies first.</div>
  <div class="flow">
   <div class="flowstep" id="fs1"><div><b>Choose connector and start</b><div class="row" style="margin-top:9px"><select class="input" id="tCh" style="max-width:220px"><option value="0">Both connectors</option><option value="1">J7 · Channel 1</option><option value="2">J6 · Channel 2</option></select><button class="btn primary" id="tStart">Start recording</button></div><div class="mini" style="margin-top:6px" id="tNeedSD"></div></div></div>
   <div class="flowstep" id="fs2"><div><b>Mark each steady stage</b><div class="mini">Hold each stage 30–60 s. Enter what your own gauge shows.</div>
    <div class="grid g3" style="margin-top:10px"><div class="field" style="margin:0"><label for="tPhase">Stage</label><select class="input" id="tPhase"><option value="idle">Idle</option><option value="low">Low RPM</option><option value="medium">Medium RPM</option><option value="high">High RPM</option><option value="cruise">Cruise</option><option value="custom">Custom</option></select></div><div class="field" style="margin:0"><label for="tRpm">Gauge RPM</label><input class="input" id="tRpm" type="number" inputmode="numeric" min="0" max="6000" placeholder="optional"></div><div class="field" style="margin:0"><label for="tFuel">Reference L/h</label><input class="input" id="tFuel" type="number" inputmode="decimal" min="0" step="0.1" placeholder="optional"></div></div>
    <div class="row" style="margin-top:10px"><button class="btn" id="tMark" disabled>Mark stage</button><span class="mini" id="tStage">—</span></div></div></div>
   <div class="flowstep" id="fs3"><div><b>Stop and download</b><div class="row" style="margin-top:9px"><button class="btn ghost" id="tStop" disabled>Stop &amp; save</button><a class="btn ghost" id="tDl" href="/api/test/download" download="EngineLink-Motor-Test.csv" aria-disabled="true">Download CSV</a></div></div></div>
  </div>
  <div class="grid g3k" style="margin-top:14px"><div class="kpi"><div class="eyebrow">Recorded</div><div class="num" id="tRows">0</div></div><div class="kpi"><div class="eyebrow">Duration</div><div class="num" id="tDur">—</div></div><div class="kpi"><div class="eyebrow">Invalid</div><div class="num" id="tInv">0</div></div></div>
  <p class="mini" style="margin-top:10px">If your phone loses Wi-Fi, EngineLink keeps recording to the SD card as long as it has power. File: <span id="tFile">—</span></p>
 </div>
</section>

<!-- HISTORY -->
<section id="history" class="page">
 <div class="top"><div><div class="eyebrow">Recording</div><h1>History</h1><div class="sub">Everything is recorded automatically. No need to start or stop trips.</div></div><span class="chip" id="logChip">—</span></div>
 <div class="grid g2">
  <div class="card"><h2>Engine runs</h2><p class="mini" style="margin-bottom:10px">A run starts when an engine exceeds 300 RPM and ends 10 s after it stops. Runs are kept separate and saved to SEGMENTS.CSV.</p>
   <div class="tablewrap"><table><thead><tr><th>Engine</th><th>Started</th><th class="right">Duration</th><th class="right">Fuel</th><th class="right">Max RPM</th></tr></thead><tbody id="segRows"><tr><td colspan="5" class="mini">No engine runs since start-up.</td></tr></tbody></table></div>
   <div class="row" style="margin-top:12px"><a class="btn" href="/api/segments.csv" data-dl>Download all runs (CSV)</a></div>
  </div>
  <div class="card"><h2>Raw data log</h2>
   <div class="stat"><span>Status</span><strong id="logStatus">—</strong></div>
   <div class="stat"><span>File</span><strong id="logName" class="mono">—</strong></div>
   <div class="stat"><span>Messages saved</span><strong id="logRows">—</strong></div>
   <div class="stat"><span>Write errors</span><strong id="logErr">—</strong></div>
   <div class="row" style="margin-top:14px"><a class="btn primary" href="/api/log" data-dl>Download session CSV</a><button class="btn ghost" id="logToggle">Stop logging</button></div>
   <p class="mini" style="margin-top:10px">A new file is created each time EngineLink starts. Remove the SD card only when logging is stopped or the device is off.</p>
  </div>
 </div>
</section>

<!-- DEVICE -->
<section id="device" class="page">
 <div class="top"><div><div class="eyebrow">System</div><h1>Device</h1><div class="sub" id="healthSummary">Checking…</div></div><span class="chip" id="verChip">—</span></div>
 <div class="health" id="health"></div>
 <div class="grid g2" style="margin-top:14px">
  <div class="card"><div class="split" style="margin-bottom:12px"><h2 style="margin:0">Status LED</h2><span class="chip">Automatic</span></div>
   <div class="row" style="gap:16px;margin-bottom:12px"><div class="led" id="ledDot"></div><div><b id="ledName">—</b><div class="mini">What the LED on the board shows right now</div></div></div>
   <div id="ledLegend"></div>
   <button class="btn ghost block" id="ledTest" style="margin-top:12px">Show all LED signals (16 s)</button></div>
  <div class="card"><h2>Network &amp; time</h2>
   <div class="stat"><span>EngineLink Wi-Fi</span><strong id="nAP">—</strong></div>
   <div class="stat"><span>Address</span><strong>192.168.4.1 · enginelink.local</strong></div>
   <div class="stat"><span>Connected phones</span><strong id="nClients">—</strong></div>
   <div class="stat"><span>Router</span><strong id="nRouter">—</strong></div>
   <div class="stat"><span>Clock</span><strong id="nClock">—</strong></div>
   <div class="row" style="margin-top:12px"><button class="btn ghost" id="syncBtn">Set time from this phone</button></div></div>
 </div>
 <details class="more"><summary><span>Technical details <span class="mini">· memory, counters, reset reason</span></span></summary><div class="body grid g2">
  <div><div class="stat"><span>Uptime</span><strong id="xUp">—</strong></div><div class="stat"><span>Last restart reason</span><strong id="xReset">—</strong></div><div class="stat"><span>Free memory / lowest</span><strong id="xHeap">—</strong></div><div class="stat"><span>PSRAM free / total</span><strong id="xPsram">—</strong></div><div class="stat"><span>Frame backlog</span><strong id="xQueue">—</strong></div><div class="stat"><span>Firmware size</span><strong id="xSketch">—</strong></div><div class="stat"><span>Build</span><strong id="xBuild">—</strong></div></div>
  <div><div class="stat"><span>J7 messages / invalid / line errors</span><strong id="xJ7">—</strong></div><div class="stat"><span>J6 messages / invalid / line errors</span><strong id="xJ6">—</strong></div><div class="stat"><span>UART overflows J7 / J6</span><strong id="xOvf">—</strong></div><div class="stat"><span>Phone joins / leaves (last reason)</span><strong id="xAp">—</strong></div><div class="stat"><span>Wi-Fi restarts</span><strong id="xApR">—</strong></div><div class="stat"><span>Router disconnects</span><strong id="xSta">—</strong></div><div class="stat"><span>NMEA 2000 frames</span><strong id="xCan">—</strong></div><div class="stat"><span>Isolated input GPIO38</span><strong id="xOpto">—</strong></div></div>
 </div></details>
</section>

<!-- SETTINGS -->
<section id="settings" class="page">
 <div class="top"><div><div class="eyebrow">Configuration</div><h1>Settings</h1><div class="sub">Changes are saved on EngineLink and survive restarts.</div></div></div>
 <div class="grid g2">
  <div class="card"><h2>Wi-Fi</h2>
   <div class="choose" style="margin-bottom:14px"><button class="choice" type="button" data-snet="ap"><strong>EngineLink Wi-Fi</strong><span class="hint">Direct connection, no router.</span></button><button class="choice" type="button" data-snet="router"><strong>Connect to a router</strong><span class="hint">Also join the boat's network.</span></button></div>
   <div id="sRouter" class="hide"><div class="field"><label for="sSSID">Router network name</label><input class="input" id="sSSID" autocapitalize="off"></div><div class="field"><label for="sPass">Router password</label><input class="input" id="sPass" type="password" placeholder="Leave blank to keep current"></div></div>
   <div class="field"><label for="sAP">EngineLink Wi-Fi password</label><input class="input" id="sAP" type="password" placeholder="Leave blank to keep current"><span class="hint" id="sAPHint"></span></div>
   <div class="field"><label for="sCh">EngineLink Wi-Fi channel</label><select class="input" id="sCh"><option value="1">1</option><option value="6">6</option><option value="11">11</option></select><span class="hint">Try another channel if the connection drops near other boats' Wi-Fi.</span></div>
  </div>
  <div class="card"><h2>Engines &amp; time</h2>
   <div class="field"><label for="sPort">Port engine connector</label><select class="input" id="sPort"><option value="1">J7 · Channel 1 (starboard on J6)</option><option value="2">J6 · Channel 2 (starboard on J7)</option><option value="0">Not assigned</option></select></div>
   <div class="field"><label for="sTime">Time source</label><select class="input" id="sTime"><option value="auto">Automatic (GPS, then internet, then phone)</option><option value="phone">Phone / browser</option><option value="gps">NMEA 2000 GPS only</option><option value="ntp">Internet (via router) only</option><option value="manual">None · use uptime only</option></select></div>
   <label class="switch"><span><span class="lbl">NMEA 2000 receiver</span><br><span class="hint">Listen-only: reads GPS speed and time, never transmits. The CAN hardware is not validated yet.</span></span><input type="checkbox" id="sCAN"></label>
  </div>
 </div>
 <button class="btn primary block" id="saveBtn" style="margin-top:14px">Save settings</button>

 <details class="more"><summary><span>Firmware update <span class="mini">· over Wi-Fi, no cable</span></span></summary><div class="body">
  <p class="mini" style="margin-bottom:12px">Download <b>enginelink-update.bin</b> from the EngineLink download page on a phone or computer, then select it here. Settings and fuel totals are kept. Do not use the USB file (firmware.bin).</p>
  <div class="row"><input type="file" id="otaFile" accept=".bin,application/octet-stream" class="input" style="max-width:360px"><button class="btn" id="otaBtn">Install update</button></div>
  <div class="progress hide" id="otaProg" style="margin-top:12px"><div></div></div><p class="mini" id="otaMsg" style="margin-top:8px"></p>
 </div></details>

 <details class="more"><summary><span>J1708 terminal <span class="mini">· advanced, read-only</span></span></summary><div class="body">
  <div class="row" style="margin-bottom:12px"><div class="seg" id="termSeg"><button aria-pressed="true" data-ch="0">J7</button><button aria-pressed="false" data-ch="1">J6</button></div><span class="mini">9600 baud · 8N1 · receive only</span></div>
  <div class="list mono" id="term" style="padding:10px;font-size:12px;max-height:260px"></div>
  <div class="field" style="margin-top:14px"><label for="termIn">HEX message</label><input class="input mono" id="termIn" placeholder="80 BE 00 00" disabled></div>
  <div class="row"><button class="btn" disabled>Send (disabled)</button><button class="btn ghost small" id="termHelpBtn">Having trouble writing? ℹ️</button></div>
  <div class="notice info hide" id="termHelp" style="margin-top:12px"><b>About J1708 messages</b><br>J1708/J1587 is binary, not text commands. A message is: MID (who sends) → one or more PIDs with data → checksum (all bytes sum to 0). PID 190 is engine speed (0.25 RPM/bit); PID 183 is fuel rate (1/64 US gal/h per bit). Sending is disabled until bus arbitration and electrical safety are verified; never send examples to a live engine.</div>
 </div></details>

 <details class="more"><summary><span>Maintenance <span class="mini">· restart, LED test, factory reset</span></span></summary><div class="body">
  <div class="row"><button class="btn ghost" id="rebootBtn">Restart EngineLink</button><button class="btn ghost" id="remountBtn">Re-read SD card</button><button class="btn danger" id="factoryBtn">Factory reset…</button></div>
  <p class="mini" style="margin-top:10px">Factory reset erases settings and fuel totals. Files on the SD card are not deleted.</p>
 </div></details>
</section>
</main>
<footer><span id="footVer">EngineLink</span> · Marine Precision · Passive receive-only capture · Fuel data depends on ECU support</footer>
</div>
</div>
<div class="modal" id="modal"><div class="card"><h2 id="mTitle">Confirm</h2><p class="muted" id="mText" style="margin:8px 0 14px"></p><div class="field hide" id="mField"><label for="mInput" id="mLabel"></label><input class="input" id="mInput" autocapitalize="characters"></div><div class="row" style="justify-content:flex-end"><button class="btn ghost" id="mNo">Cancel</button><button class="btn primary" id="mYes">OK</button></div></div></div>
<div id="toast" class="toast" role="status"></div>
<script>
(function(){
'use strict';
const $=id=>document.getElementById(id);
const txt=(id,s)=>{const e=$(id);if(e)e.textContent=s};
const html=(id,s)=>{const e=$(id);if(e)e.innerHTML=s};
const esc=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const ok=v=>v!==null&&v!==undefined&&Number.isFinite(Number(v));
const fmt=(n,d=1)=>ok(n)?Number(n).toLocaleString('da-DK',{minimumFractionDigits:d,maximumFractionDigits:d}):'—';
const unit=(v,d,u)=>ok(v)?fmt(v,d)+'<span class="u">'+u+'</span>':'—<span class="u">'+u+'</span>';
const dur=s=>{if(!ok(s))return'—';s=Math.max(0,Math.floor(s));const h=Math.floor(s/3600),m=Math.floor(s%3600/60);return h?h+' h '+String(m).padStart(2,'0')+' m':m?m+' m '+String(s%60).padStart(2,'0')+' s':s+' s'};
const ago=ms=>!ok(ms)?'never':ms<1500?'just now':ms<60000?Math.round(ms/1000)+' s ago':Math.round(ms/60000)+' min ago';

// SAE J1587 parameter names. Values use standard scaling and must be verified per ECU.
const s16=v=>v>32767?v-65536:v;
const F2C=f=>(f-32)/1.8;
const PID={
 84:['Road speed','km/h',v=>v*0.805,1],91:['Throttle position','%',v=>v*0.4,0],92:['Engine load','%',v=>v*0.5,0],
 94:['Fuel delivery pressure','bar',v=>v*0.0345,2],96:['Fuel level','%',v=>v*0.5,0],100:['Oil pressure','bar',v=>v*0.0345,2],
 102:['Boost pressure','bar',v=>v*0.00862,2],105:['Intake manifold temp.','°C',v=>F2C(v),0],110:['Coolant temperature','°C',v=>F2C(v),0],
 168:['Battery voltage','V',v=>v*0.05,1],174:['Fuel temperature','°C',v=>F2C(s16(v)*0.25),0],175:['Oil temperature','°C',v=>F2C(s16(v)*0.25),0],
 183:['Fuel rate','L/h',v=>v*3.785411784/64,2],190:['Engine speed','RPM',v=>v*0.25,0],247:['Total engine hours (ECU)','h',v=>v*0.05,1],250:['Total fuel used (ECU)','L',v=>v*0.473176,0],
 45:['Inlet air heater status'],70:['Parking brake'],85:['Cruise control status'],89:['PTO status'],97:['Water in fuel'],111:['Coolant level'],121:['Engine retarder status'],
 171:['Ambient air temperature'],173:['Exhaust gas temperature'],182:['Trip fuel'],184:['Instant fuel economy'],185:['Average fuel economy'],194:['Diagnostic codes'],
 234:['Software ID'],237:['VIN'],243:['Component ID'],244:['Trip distance'],245:['Total distance'],246:['Total vehicle hours'],248:['Total PTO hours'],251:['Clock'],252:['Date']
};
const MID={128:'Engine #1',130:'Transmission',140:'Instrument cluster',175:'Engine #2'};
const midName=m=>MID[m]?m+' · '+MID[m]:String(m);
const pidName=p=>PID[p]?PID[p][0]:'Unknown parameter';
function pidValue(p,bytes){
 const d=PID[p];if(!d||!d[2]||!bytes.length)return'';
 let raw=0;for(let i=Math.min(bytes.length,4)-1;i>=0;i--)raw=raw*256+bytes[i];
 const allFF=bytes.slice(0,4).every(b=>b===255);if(allFF)return'not available';
 const v=d[2](raw);return fmt(v,d[3])+' '+d[1];
}
const hexBytes=h=>String(h||'').split(' ').filter(x=>/^[0-9A-F]{2}$/i.test(x)).map(x=>parseInt(x,16));
function parseFrame(bytes){
 const out=[];if(bytes.length<3)return out;
 let i=1;const end=bytes.length-1;
 while(i<end){const p=bytes[i++];let w;if(p<=127)w=1;else if(p<=191)w=2;else if(p<=253){if(i>=end)break;w=bytes[i++]}else{out.push({pid:p,ext:true});break}
  if(w>end-i){out.push({pid:p,trunc:true});break}out.push({pid:p,data:bytes.slice(i,i+w)});i+=w}
 return out;
}

let S=null,started=false,settingsLoaded=false,wizStep=1,wizNet='ap',setNet='ap',page='fuel',dataCh=0,termCh=0,paused=false,snap=[],selKey=null;
let pollTimer=null,pollAbort=null,pollBusy=false,fails=0,busy=false,lastOk=0,lastBoot=null,restartWarned=false,pauseUntil=0,lastPidFetch=0,lastHistFetch=0,chartMode='fuel',hist=null,pidData=null;
let rateRef=[{t:0,f:0},{t:0,f:0}],rates=[null,null],wizRef=[null,null];

function toast(m,bad){const e=$('toast');e.textContent=m;e.style.borderColor=bad?'#c98286':'#49887f';e.style.background=bad?'#3a2224':'#163c3e';e.style.display='block';clearTimeout(window.__t);window.__t=setTimeout(()=>e.style.display='none',4500)}
function confirmBox(title,text,opts={}){return new Promise(res=>{txt('mTitle',title);txt('mText',text);$('mField').classList.toggle('hide',!opts.word);txt('mLabel',opts.word?'Type '+opts.word+' to confirm':'');$('mInput').value='';txt('mYes',opts.yes||'OK');$('mYes').className='btn '+(opts.danger?'danger':'primary');$('modal').classList.add('open');
 const done=v=>{$('modal').classList.remove('open');$('mYes').onclick=$('mNo').onclick=null;res(v)};
 $('mYes').onclick=()=>{if(opts.word&&$('mInput').value.trim().toUpperCase()!==opts.word){$('mInput').focus();return}done(true)};$('mNo').onclick=()=>done(false)})}
function setOnline(good){$('onlineDot').className='dot '+(good?'good':fails>2?'bad':'');txt('online',good?'Connected':fails>2?'Reconnecting'+(lastOk?' · '+Math.round((Date.now()-lastOk)/1000)+' s':''):'Checking…')}
function schedule(ms){clearTimeout(pollTimer);pollTimer=setTimeout(poll,ms)}
async function post(path,params,quiet){
 if(busy){toast('Please wait for the previous action.',true);return null}
 busy=true;if(pollAbort)pollAbort.abort();
 const c=new AbortController(),t=setTimeout(()=>c.abort(),7000);
 try{const r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(params||{}),cache:'no-store',signal:c.signal});
  const j=await r.json();if(!r.ok||j.ok===false){toast(j.message||'Request failed',true);return null}if(!quiet)toast(j.message||'Done');return j}
 catch(e){toast('No answer from EngineLink. Check that your phone is on the EngineLink Wi-Fi.',true);return null}
 finally{clearTimeout(t);busy=false;schedule(350)}
}
async function getJSON(path){const c=new AbortController(),t=setTimeout(()=>c.abort(),5000);try{const r=await fetch(path,{cache:'no-store',signal:c.signal});if(!r.ok)return null;return await r.json()}catch(e){return null}finally{clearTimeout(t)}}
document.querySelectorAll('[data-dl]').forEach(a=>a.addEventListener('click',pauseForDownload));
function pauseForDownload(){pauseUntil=Date.now()+30000;if(pollAbort)pollAbort.abort();schedule(30000)}

// ---------- Wizard ----------
function wizShow(n){wizStep=n;for(let i=1;i<=3;i++){$('ws'+i).classList.toggle('active',i===n);$('sb'+i).classList.toggle('done',i<=n)}
 txt('wizardHeading',['Welcome to EngineLink','Engine connectors','Date and time'][n-1]);$('wBack').disabled=n===1;txt('wNext',n===3?'Finish setup':'Continue');txt('wErr','')}
document.querySelectorAll('[data-net]').forEach(b=>b.onclick=()=>{wizNet=b.dataset.net;document.querySelectorAll('[data-net]').forEach(x=>x.classList.toggle('active',x===b));$('wRouter').classList.toggle('hide',wizNet!=='router')});
$('wBack').onclick=()=>{if(wizStep>1)wizShow(wizStep-1)};
$('wNext').onclick=async()=>{
 if(wizStep===1){if(wizNet==='router'&&!$('wSSID').value.trim()){txt('wErr','Enter the router network name.');return}const p=$('wAP').value;if(p&&p.length<8){txt('wErr','The EngineLink password needs at least 8 characters.');return}}
 if(wizStep<3){wizShow(wizStep+1);return}
 const r=await post('/api/setup',{network:wizNet,ssid:$('wSSID').value.trim(),pass:$('wPass').value,apPass:$('wAP').value,port:$('wPort').value,time:$('wTime').value,can:'0'});
 if(r){$('wizard').classList.add('hide');$('app').classList.remove('hide');started=true;settingsLoaded=false;const tp=$('wTime').value;if(tp==='auto'||tp==='phone')setTimeout(()=>syncPhone(true),900)}
};
function wizActivity(s){for(let c=0;c<2;c++){const f=s.channels[c].frames;if(wizRef[c]===null)wizRef[c]=f;const d=f-wizRef[c];txt('wAct'+c,f?f.toLocaleString('da-DK'):'—');txt('wActT'+c,!f?'No traffic':d>0?'Active · '+d+' new since you opened this':'Traffic seen earlier')}}

// ---------- Navigation ----------
document.querySelectorAll('nav.tabs button').forEach(b=>b.onclick=()=>go(b.dataset.page));
function go(p){page=p;document.querySelectorAll('.page').forEach(x=>x.classList.toggle('visible',x.id===p));document.querySelectorAll('nav.tabs button').forEach(b=>b.setAttribute('aria-selected',String(b.dataset.page===p)));window.scrollTo({top:0});if(S)renderPage();if(p==='traffic')fetchPids(true);if(p==='fuel')fetchHistory(true)}
function segInit(id,cb){const root=$(id);root.querySelectorAll('button').forEach(b=>b.onclick=()=>{root.querySelectorAll('button').forEach(x=>x.setAttribute('aria-pressed',String(x===b)));cb(b.dataset)})}
segInit('chSeg',d=>{dataCh=Number(d.ch);selKey=null;renderTraffic();renderPids()});
segInit('termSeg',d=>{termCh=Number(d.ch);renderTerminal()});
segInit('chartMode',d=>{chartMode=d.m;renderChart()});

// ---------- Fuel ----------
const engName=i=>i===0?'Port':'Starboard';
function engineCard(slot,idx){
 const root=$('eng'+slot),S0=S;
 if(idx<0){root.innerHTML='<div class="head"><span class="name">'+engName(slot)+' engine</span><span class="chip amber">Not assigned</span></div><p class="mini">Choose which connector belongs to which engine in Settings.</p><button class="btn small" onclick="document.querySelector(\'[data-page=settings]\').click()">Assign connectors</button>';return}
 const c=S0.channels[idx],run=c.running,has=ok(c.fuel)||ok(c.rpm);
 const state=run?'<span class="chip green"><span class="dot good"></span>Running · '+dur(c.runSeconds)+'</span>':has?'<span class="chip">Stopped</span>':c.frames?'<span class="chip amber">No engine values</span>':'<span class="chip red">No data on '+c.name+'</span>';
 const rpm=ok(c.rpm)?c.rpm:null;
 let params='';const P=c.params||{};
 [110,100,168,92,102,175,247,250].forEach(p=>{const x=P[p];if(!x)return;const bytes=[];for(let i=0;i<Math.min(x.len,4);i++)bytes.push(Math.floor(x.raw/Math.pow(256,i))%256);params+='<div class="param"><span>'+esc(pidName(p))+'</span><span>'+esc(pidValue(p,bytes))+'</span></div>'});
 root.innerHTML='<div class="head"><div><span class="name">'+engName(slot)+' engine</span> <span class="chip">'+c.name+'</span></div>'+state+'</div>'+
 '<div class="grid g2"><div><div class="eyebrow">Fuel flow</div><div class="num">'+unit(c.fuel,1,'L/h')+'</div><div class="mini">'+(ok(c.fuel)?'ECU · PID 183 · '+ago(c.fuelAge):c.fuelSamples?'Last value '+ago(c.fuelAge):'No fuel rate received')+'</div></div>'+
 '<div><div class="eyebrow">Engine speed</div><div class="num">'+(rpm===null?'—':fmt(rpm,0))+'<span class="u">RPM</span></div><div class="bar"><div style="width:'+(rpm===null?0:Math.min(100,rpm/40))+'%"></div></div></div></div>'+
 (params?'<div style="margin-top:14px"><div class="split"><span class="eyebrow">Engine details</span><span class="mini">Standard J1587 · unverified</span></div>'+params+'</div>':'');
}
function renderFuel(){
 const f=S.fuel,a=S.channels;
 html('total',unit(f.total,1,'L/h'));
 const anyFuel=ok(a[0].fuel)||ok(a[1].fuel);
 txt('totalNote',ok(f.total)?'Sum of both engines · ECU-reported':f.portChannel<0?'Assign the engine connectors in Settings to see a total.':anyFuel?'Only one engine reports fuel rate right now.':'Waiting for fuel rate (PID 183) from the engines.');
 const chip=$('fuelChip');if(ok(f.total)||anyFuel){chip.className='chip green';chip.textContent='Live ECU data'}else if(a[0].frames||a[1].frames){chip.className='chip amber';chip.textContent='Traffic, no fuel rate'}else{chip.className='chip';chip.textContent='Waiting for data'}
 html('lnm',unit(f.litersPerNm,2,'L/nm'));html('sog',unit(f.sogKnots,1,'kn'));
 engineCard(0,f.portChannel);engineCard(1,f.starboardChannel);
 const ti=a[0].tripLiters+a[1].tripLiters;
 txt('tripFuel',fmt(ti,1)+' L');txt('tripTime',dur(Math.max(a[0].tripSeconds,a[1].tripSeconds)));
 const p=f.portChannel,sb=f.starboardChannel;
 txt('tripSplit',p<0?fmt(a[0].tripLiters,1)+' L (J7) / '+fmt(a[1].tripLiters,1)+' L (J6)':fmt(a[p].tripLiters,1)+' L / '+fmt(a[sb].tripLiters,1)+' L');
 txt('tripSince',S.trip.startUtc>0?'Since '+new Date(S.trip.startUtc*1000).toLocaleString('da-DK',{dateStyle:'medium',timeStyle:'short'}):'Since first start-up');
 txt('life0',fmt(a[0].lifetimeLiters,1)+' L');txt('life1',fmt(a[1].lifetimeLiters,1)+' L');
 txt('lifeH',fmt(a[0].engineSeconds/3600,1)+' h / '+fmt(a[1].engineSeconds/3600,1)+' h');
 txt('bootFuel',(a[0].fuelSamples||a[1].fuelSamples)?fmt(a[0].observedLiters+a[1].observedLiters,2)+' L · '+(a[0].gaps+a[1].gaps)+' gaps':'—');
}
async function fetchHistory(force){if(page!=='fuel')return;if(!force&&Date.now()-lastHistFetch<20000)return;lastHistFetch=Date.now();const h=await getJSON('/api/history');if(h){hist=h;renderChart()}}
function renderChart(){
 const svg=$('chart');if(!hist||!hist.count){svg.innerHTML='';txt('chartNote','Collecting data… the trend appears after a few minutes');return}
 const p=S?S.fuel.portChannel:0,sb=S?S.fuel.starboardChannel:1;
 const order=p<0?[0,1]:[p,sb];
 const series=order.map(c=>chartMode==='fuel'?hist['fuel'+c]:hist['rpm'+c]);
 let max=0;series.forEach(s=>s.forEach(v=>{if(v!==null&&v>max)max=v}));
 if(max<=0){svg.innerHTML='';txt('chartNote','No '+(chartMode==='fuel'?'fuel rate':'RPM')+' received in the last hour');return}
 const nice=chartMode==='fuel'?Math.ceil(max/10)*10:Math.ceil(max/500)*500;
 const n=hist.count,W=600,H=180,pad=16,x=i=>n<2?W:i/(n-1)*W,y=v=>H-pad-(v/nice)*(H-2*pad);
 let out='';for(let g=1;g<=3;g++){const gy=y(nice*g/4);out+='<line x1="0" x2="600" y1="'+gy+'" y2="'+gy+'" stroke="#1f3a4c" stroke-width="1" vector-effect="non-scaling-stroke"/>'}
 const colors=['#46cabc','#7fb4ff'];
 series.forEach((s,k)=>{let d='',pen=false;s.forEach((v,i)=>{if(v===null){pen=false;return}d+=(pen?'L':'M')+x(i).toFixed(1)+' '+y(v).toFixed(1);pen=true});if(d)out+='<path d="'+d+'" fill="none" stroke="'+colors[k]+'" stroke-width="2.2" vector-effect="non-scaling-stroke" stroke-linejoin="round"/>'});
 svg.innerHTML=out;txt('chartNote','Top line '+nice+(chartMode==='fuel'?' L/h':' RPM')+' · last '+dur(n*hist.step)+' · gaps = no data');
}

// ---------- Traffic ----------
function rateUpdate(){const now=Date.now();for(let c=0;c<2;c++){const f=S.channels[c].frames,r=rateRef[c];if(r.t&&now>r.t&&f>=r.f)rates[c]=(f-r.f)/((now-r.t)/1000);rateRef[c]={t:now,f}}}
function renderTraffic(){
 const c=S.channels[dataCh];
 txt('rate',ok(rates[dataCh])?fmt(rates[dataCh],1):'—');txt('frames',c.frames.toLocaleString('da-DK'));txt('invalid',c.invalid.toLocaleString('da-DK'));txt('drops',String(c.drops));
 if(paused)return;
 const list=$('pktList'),items=snap.filter(e=>e.ch===dataCh);
 if(!items.length){list.innerHTML='<div class="mini" style="padding:14px">No messages on '+c.name+' yet. Check the cable and that the engine ignition is on.</div>';return}
 list.replaceChildren(...items.map(e=>{const d=document.createElement('div');const key=e.ms+':'+e.hex;d.className='pkt'+(key===selKey?' sel':'');
  d.innerHTML='<span class="tag'+(e.ok?'':' bad')+'">'+(e.ok?'MID '+e.mid:'ERROR')+'</span><span class="t">'+(e.ms/1000).toFixed(2)+' s</span><span class="hex mono">'+esc(e.hex)+'</span>';
  d.onclick=()=>{selKey=key;showDetail(e);renderTraffic()};return d}));
 if(!selKey)showDetail(items[0]);
}
function showDetail(e){
 txt('dHex',e.hex);txt('dMid',midName(e.mid));txt('dSum',e.ok?'Valid':'Invalid · message ignored');txt('dTime',dur(e.ms/1000)+' after start-up');
 if(!e.ok){html('dPids','<p class="mini">Messages with a bad checksum are logged but never decoded.</p>');return}
 const parts=parseFrame(hexBytes(e.hex));
 html('dPids',parts.length?'<div class="eyebrow" style="margin-bottom:6px">Parameters</div>'+parts.map(p=>p.ext?'<div class="param"><span>PID '+p.pid+' · extended page</span><span>not decoded</span></div>':p.trunc?'<div class="param"><span>PID '+p.pid+'</span><span>truncated</span></div>':'<div class="param"><span>'+p.pid+' · '+esc(pidName(p.pid))+'</span><span class="mono">'+p.data.map(b=>b.toString(16).toUpperCase().padStart(2,'0')).join(' ')+(pidValue(p.pid,p.data)?' → '+esc(pidValue(p.pid,p.data)):'')+'</span></div>').join(''):'<p class="mini">No parameters.</p>');
}
async function fetchPids(force){if(page!=='traffic')return;if(!force&&Date.now()-lastPidFetch<5000)return;lastPidFetch=Date.now();const j=await getJSON('/api/pids');if(j){pidData=j;renderPids()}}
function renderPids(){
 if(!pidData)return;const rows=pidData.pids.filter(p=>p.ch===dataCh).sort((a,b)=>a.mid-b.mid||a.pid-b.pid);
 txt('pidCount',rows.length+' parameters');
 html('pidRows',rows.length?rows.map(p=>{const v=pidValue(p.pid,hexBytes(p.hex));return'<tr><td title="'+esc(midName(p.mid))+'">'+p.mid+'</td><td class="mono">'+p.pid+'</td><td>'+esc(pidName(p.pid))+'</td><td class="mono">'+esc(p.hex)+'</td><td class="right nowrap">'+esc(v||'—')+'</td><td class="right wide">'+p.count.toLocaleString('da-DK')+'</td><td class="right nowrap wide">'+ago(p.age)+'</td></tr>'}).join(''):'<tr><td colspan="7" class="mini">Nothing decoded on this connector yet.</td></tr>');
}
$('pauseBtn').onclick=()=>{paused=!paused;txt('pauseBtn',paused?'Resume':'Pause');if(!paused)renderTraffic()};
$('copyBtn').onclick=async()=>{const h=$('dHex').textContent;if(!h||h==='Select a message')return;try{await navigator.clipboard.writeText(h);toast('HEX copied')}catch(e){toast('Copying is not available here. Select the text instead.',true)}};
function renderTest(){
 const t=S.test,sd=S.sd.ready;
 const chip=$('testChip');chip.className='chip '+(t.running?'teal':t.available?'green':'');chip.textContent=t.running?'Recording':t.available?'Saved':'Ready';
 $('tStart').disabled=t.running||!sd;$('tCh').disabled=t.running;$('tMark').disabled=!t.running;$('tStop').disabled=!t.running;
 $('tDl').setAttribute('aria-disabled',String(t.running||!t.available));
 txt('tNeedSD',sd?'':'Insert a microSD card to record.');
 $('fs1').className='flowstep '+(t.running||t.available?'done':'now');$('fs2').className='flowstep '+(t.running?'now':t.available?'done':'');$('fs3').className='flowstep '+(t.available&&!t.running?'now':'');
 txt('tStage',t.running?'Stage '+t.step+' · '+t.phase+' · '+dur(t.stepSeconds)+(t.referenceRPM!==null?' · gauge '+t.referenceRPM+' RPM':''):'—');
 txt('tRows',t.rows.toLocaleString('da-DK'));txt('tDur',t.running?dur(t.seconds):'—');txt('tInv',String(t.invalid));txt('tFile',t.file||'—');
}
$('tStart').onclick=()=>post('/api/test/start',{channel:$('tCh').value});
$('tMark').onclick=()=>post('/api/test/mark',{phase:$('tPhase').value,rpm:$('tRpm').value,fuel:$('tFuel').value});
$('tStop').onclick=()=>post('/api/test/stop',{});
$('tDl').addEventListener('click',pauseForDownload);

// ---------- History ----------
function renderHistory(){
 const sd=S.sd,chip=$('logChip');chip.className='chip '+(sd.logging?'green':sd.fault?'red':'amber');chip.textContent=sd.logging?'Recording':sd.ready?'Logging stopped':'No SD card';
 txt('logStatus',sd.status);txt('logName',sd.filename||'—');txt('logRows',sd.rows.toLocaleString('da-DK'));txt('logErr',String(sd.errors));
 txt('logToggle',sd.logging?'Stop logging':'Start logging');$('logToggle').disabled=S.test.running||(!sd.logging&&!sd.inserted);
 const segs=S.segments||[],p=S.fuel.portChannel;
 html('segRows',segs.length?segs.map(g=>{const who=p<0?'J'+(g.ch?6:7):(g.ch===p?'Port':'Starboard')+' · J'+(g.ch?6:7);const when=g.utc>0?new Date(g.utc*1000).toLocaleString('da-DK',{dateStyle:'short',timeStyle:'short'}):dur(g.start)+' after start-up';return'<tr><td>'+who+'</td><td>'+when+'</td><td class="right">'+dur(g.end-g.start)+'</td><td class="right">'+fmt(g.liters,2)+' L</td><td class="right">'+fmt(g.maxRpm,0)+'</td></tr>'}).join(''):'<tr><td colspan="5" class="mini">No engine runs since start-up.</td></tr>');
}
$('logToggle').onclick=()=>post(S&&S.sd.logging?'/api/sd/stop':'/api/sd/start',{});

// ---------- Device ----------
const LEDS=[['#1C6EED','Starting'],['#1C6EED','First-time setup','Breathing blue'],['#17A77E','Normal','Steady green'],['#E59A21','Router reconnecting','Amber pulse'],['#DF9D30','No SD card','Amber double flash'],['#E92E35','SD write error','Rapid red flash'],['#DF4150','J1708 overflow','Red double flash'],['#16C7CD','Engine test recording','Cyan pulse'],['#D8E4EA','Firmware update','White pulse']];
html('ledLegend',LEDS.slice(1).map(l=>'<div class="stat"><span><span class="dot" style="display:inline-block;background:'+l[0]+';margin-right:8px"></span>'+l[1]+'</span><strong>'+l[2]+'</strong></div>').join(''));
const RESET=['Unknown','Power on','External reset pin','Software restart','Crash (panic)','Interrupt watchdog','Task watchdog','Watchdog','Deep sleep','Brownout · supply voltage dipped','SDIO','USB','JTAG'];
function healthItems(){
 const s=S,a=s.channels,items=[];
 const add=(lvl,title,text)=>items.push({lvl,title,text});
 add(s.network.apStarted?'good':'bad','EngineLink Wi-Fi',s.network.apClients+' phone'+(s.network.apClients===1?'':'s')+' connected · channel '+s.network.apChannel+(s.config.defaultPassword?' · factory password':''));
 if(s.config.router)add(s.network.staConnected?'good':'warn','Router',s.network.staConnected?s.network.ssid+' · '+s.network.staIP:'Not connected to '+s.network.ssid+'. Retries while no phone is connected.');
 add(s.sd.fault?'bad':s.sd.logging?'good':s.sd.ready?'warn':'warn','SD card',s.sd.fault?s.sd.status+'. Check or replace the card, then re-read it in Settings.':s.sd.logging?'Recording · '+s.sd.rows.toLocaleString('da-DK')+' messages':s.sd.ready?'Inserted, logging stopped':'No card inserted');
 for(let c=0;c<2;c++){const x=a[c];add(!s.hardware.uartReady[c]?'bad':x.drops||x.uartFaults?'warn':x.frames?'good':'off','Connector '+x.name,!s.hardware.uartReady[c]?'Receiver failed to start':x.frames?x.frames.toLocaleString('da-DK')+' messages'+(x.invalid?' · '+x.invalid+' invalid':'')+(x.drops?' · '+x.drops+' lost':''):'No traffic yet')}
 add(s.timeKnown?'good':'warn','Clock',s.timeKnown?'From '+s.timeSource:'Unknown · tap "Set time from this phone"');
 if(s.config.canListen)add(s.hardware.canReady?'good':'bad','NMEA 2000',s.hardware.canReady?s.hardware.nmeaRx+' frames received':'Receiver failed to start');
 const rr=s.hardware.resetReason;add(rr===4||rr===5||rr===6||rr===7||rr===9?'warn':'good','Last restart',RESET[rr]||('Code '+rr));
 add(s.hardware.minHeap<40000?'warn':'good','Memory',Math.round(s.hardware.heap/1024)+' KB free');
 return items;
}
function renderDevice(){
 const s=S,items=healthItems(),bad=items.filter(i=>i.lvl==='bad').length,warn=items.filter(i=>i.lvl==='warn').length;
 txt('healthSummary',bad?bad+' problem'+(bad>1?'s':'')+' need attention':warn?'Working · '+warn+' thing'+(warn>1?'s':'')+' to check':'All systems normal');
 txt('verChip','v'+s.version);
 html('health',items.map(i=>'<div class="hitem"><span class="dot '+(i.lvl==='good'?'good':i.lvl==='bad'?'bad':i.lvl==='off'?'off':'')+'"></span><div><b>'+esc(i.title)+'</b><span>'+esc(i.text)+'</span></div></div>').join(''));
 const l=LEDS[s.led.status]||LEDS[0];$('ledDot').style.background=l[0];$('ledDot').style.boxShadow='0 0 20px '+l[0]+'77';txt('ledName',s.led.name+(s.led.testing?' · demo running on the board':''));
 txt('nAP','EngineLink · channel '+s.network.apChannel);txt('nClients',String(s.network.apClients));
 txt('nRouter',s.network.staConnected?s.network.ssid+' · '+s.network.staIP+(ok(s.network.rssi)?' · '+s.network.rssi+' dBm':''):s.config.router?'Not connected':'Not used');
 txt('nClock',s.timeKnown?new Date(s.utc*1000).toLocaleString('da-DK')+' · '+s.timeSource:'Unknown');
 const h=s.hardware,a=s.channels;
 txt('xUp',dur(s.uptime));txt('xReset',(RESET[h.resetReason]||'Code')+' ('+h.resetReason+')');txt('xHeap',Math.round(h.heap/1024)+' KB / '+Math.round(h.minHeap/1024)+' KB');
 txt('xPsram',fmt(h.freePsram/1048576,1)+' / '+fmt(h.psram/1048576,1)+' MB');txt('xQueue',s.queue.waiting+' of '+s.queue.capacity);txt('xSketch',h.sketchKB+' KB');txt('xBuild',s.build);
 txt('xJ7',a[0].frames+' / '+a[0].invalid+' / '+a[0].lineErrors);txt('xJ6',a[1].frames+' / '+a[1].invalid+' / '+a[1].lineErrors);txt('xOvf',a[0].uartFaults+' / '+a[1].uartFaults);
 txt('xAp',s.network.apJoins+' / '+s.network.apLeaves+' ('+s.network.apReason+')');txt('xApR',s.network.apStarts+' starts / '+s.network.apStops+' stops');txt('xSta',String(s.network.staDisconnects));
 txt('xCan',s.hardware.canReady?String(s.hardware.nmeaRx):'Receiver off');txt('xOpto',h.opto?'HIGH':'LOW');
}
$('ledTest').onclick=()=>post('/api/led/test',{});
$('syncBtn').onclick=()=>syncPhone(false);
async function syncPhone(auto){if(!S)return;if(!(S.config.timePref==='auto'||S.config.timePref==='phone')){if(!auto)toast('Time source is not set to Automatic or Phone. Change it in Settings.',true);return}await post('/api/phone-time',{ms:String(Date.now())},auto)}

// ---------- Settings ----------
document.querySelectorAll('[data-snet]').forEach(b=>b.onclick=()=>pickNet(b.dataset.snet));
function pickNet(n){setNet=n;document.querySelectorAll('[data-snet]').forEach(x=>x.classList.toggle('active',x.dataset.snet===n));$('sRouter').classList.toggle('hide',n!=='router')}
function loadSettings(s){pickNet(s.config.router?'router':'ap');$('sSSID').value=s.network.ssid||'';$('sPort').value=String(s.config.portChannel);$('sTime').value=s.config.timePref;$('sCAN').checked=s.config.canListen;$('sCh').value=String(s.config.apChannel);txt('sAPHint',s.config.defaultPassword?'Still the factory password 12345678. Changing it is a good idea.':'A custom password is set.')}
$('saveBtn').onclick=async()=>{
 const ap=$('sAP').value;if(ap&&ap.length<8){toast('The EngineLink password needs at least 8 characters.',true);return}
 if(setNet==='router'&&!$('sSSID').value.trim()){toast('Enter the router network name.',true);return}
 const r=await post('/api/settings',{network:setNet,ssid:$('sSSID').value.trim(),pass:$('sPass').value,apPass:ap,port:$('sPort').value,time:$('sTime').value,can:$('sCAN').checked?'1':'0',apch:$('sCh').value});
 if(r){$('sAP').value='';$('sPass').value='';settingsLoaded=false}
};
$('termHelpBtn').onclick=()=>$('termHelp').classList.toggle('hide');
function renderTerminal(){const items=snap.filter(e=>e.ch===termCh).slice().reverse();html('term',items.length?items.map(e=>'<div><span style="color:'+(e.ok?'#46cabc':'#f39a96')+'">'+(e.ok?'RX ':'ERR')+'</span> <span class="subtle">'+(e.ms/1000).toFixed(2).padStart(9,' ')+'</span>  '+esc(e.hex)+'</div>').join(''):'<span class="mini">Waiting for J1708 messages…</span>');const t=$('term');t.scrollTop=t.scrollHeight}
$('rebootBtn').onclick=async()=>{if(await confirmBox('Restart EngineLink?','Logging pauses for about 20 seconds while it restarts.',{yes:'Restart'}))post('/api/reboot',{})};
$('remountBtn').onclick=()=>post('/api/sd/remount',{});
$('factoryBtn').onclick=async()=>{if(await confirmBox('Factory reset','This erases all settings, the trip counter and lifetime fuel totals. Wi-Fi returns to EngineLink / 12345678.',{word:'ERASE',yes:'Erase everything',danger:true})){post('/api/factory-reset',{confirm:'ERASE'})}};
$('tripReset').onclick=async()=>{if(await confirmBox('Reset trip?','The trip fuel and engine time start again from zero. Lifetime totals are kept.',{yes:'Reset trip'}))post('/api/trip/reset',{})};
$('otaBtn').onclick=async()=>{
 const f=$('otaFile').files[0];if(!f){toast('Choose the enginelink-update.bin file first.',true);return}
 if(/merged|firmware\.bin$/i.test(f.name)&&!/update/i.test(f.name)){if(!await confirmBox('Is this the right file?','"'+f.name+'" looks like the USB image. The Wi-Fi update needs enginelink-update.bin.',{yes:'Try anyway'}))return}
 if(f.size>3000000){toast('This file is too large for a Wi-Fi update. Use enginelink-update.bin.',true);return}
 if(!await confirmBox('Install update?','EngineLink installs '+f.name+' ('+Math.round(f.size/1024)+' KB) and restarts. Keep this page open until it finishes.',{yes:'Install'}))return;
 busy=true;if(pollAbort)pollAbort.abort();clearTimeout(pollTimer);
 $('otaProg').classList.remove('hide');const bar=$('otaProg').firstElementChild;txt('otaMsg','Uploading…');
 const fd=new FormData();fd.append('firmware',f,f.name);
 const x=new XMLHttpRequest();x.open('POST','/api/update');x.timeout=180000;
 x.upload.onprogress=e=>{if(e.lengthComputable){const p=Math.round(e.loaded/e.total*100);bar.style.width=p+'%';txt('otaMsg',p<100?'Uploading… '+p+'%':'Verifying and installing…')}};
 x.onload=()=>{let j={};try{j=JSON.parse(x.responseText)}catch(e){}busy=false;if(x.status===200&&j.ok){txt('otaMsg',j.message);toast(j.message);restartWarned=true;schedule(15000)}else{txt('otaMsg',j.message||'Update failed');toast(j.message||'Update failed',true);schedule(1000)}};
 x.onerror=x.ontimeout=()=>{busy=false;txt('otaMsg','The connection dropped during upload. If EngineLink restarted, reload this page.');schedule(4000)};
 x.send(fd);
};

// ---------- Alerts ----------
function topAlert(){
 const s=S;
 if(s.sd.fault)return['bad',s.sd.status+'. Data is not being saved.','Re-read SD',()=>post('/api/sd/remount',{})];
 if(s.hardware.uartReady.some(r=>!r))return['bad','A J1708 receiver failed to start. Restart EngineLink.','Restart',()=>post('/api/reboot',{})];
 if(!s.sd.inserted)return['','No SD card. Live values work, but nothing is recorded.',null];
 if(!s.sd.logging&&!s.test.running)return['','SD logging is stopped. Raw data is not being saved.','Start logging',()=>post('/api/sd/start',{})];
 if(!s.timeKnown&&s.config.timePref!=='manual'&&s.config.timePref!=='gps'&&s.config.timePref!=='ntp')return['info','The clock is not set. Records use time since start-up.','Set time',()=>syncPhone(false)];
 return null;
}
function renderAlert(){const a=topAlert(),root=$('alert');if(!a){root.classList.add('hide');return}root.className='notice alert '+a[0];txt('alertText',a[1]);const b=$('alertAction');b.classList.toggle('hide',!a[2]);if(a[2]){b.textContent=a[2];b.onclick=a[3]}}

// ---------- Render & poll ----------
function renderPage(){if(page==='fuel')renderFuel();else if(page==='traffic'){renderTraffic();renderTest();renderPids()}else if(page==='history')renderHistory();else if(page==='device')renderDevice();else if(page==='settings')renderTerminal()}
function render(s){
 S=s;
 if(!started){$('wizard').classList.toggle('hide',s.setup);$('app').classList.toggle('hide',!s.setup);started=s.setup}
 if(!s.setup)wizActivity(s);
 if(s.setup&&!settingsLoaded){loadSettings(s);settingsLoaded=true}
 if(!paused)snap=s.recent||[];
 rateUpdate();renderAlert();renderPage();
 txt('footVer','EngineLink '+s.version);
 txt('clockPill',s.timeKnown?new Date(s.utc*1000).toLocaleTimeString('da-DK',{hour:'2-digit',minute:'2-digit'}):'Time unknown');
}
async function poll(){
 if(Date.now()<pauseUntil){schedule(Math.max(300,pauseUntil-Date.now()));return}
 if(pollBusy||busy){schedule(700);return}
 pollBusy=true;const c=new AbortController();pollAbort=c;const t=setTimeout(()=>c.abort(),5000);
 try{const r=await fetch('/api/state',{cache:'no-store',signal:c.signal});if(!r.ok)throw 0;const d=await r.json();
  if(lastBoot!==null&&d.bootId!==lastBoot&&!restartWarned){restartWarned=true;toast('EngineLink restarted ('+(RESET[d.hardware.resetReason]||'unknown reason')+').',d.hardware.resetReason!==3)}
  lastBoot=d.bootId;fails=0;lastOk=Date.now();setOnline(true);render(d);
  if(!d.timeKnown&&(d.config.timePref==='phone'||d.config.timePref==='auto')&&d.setup&&!window.__clk){window.__clk=true;setTimeout(()=>syncPhone(true),900)}
 }catch(e){if(!busy){fails++;setOnline(false)}}
 finally{clearTimeout(t);if(pollAbort===c)pollAbort=null;pollBusy=false;
  if(!busy&&fails===0){fetchPids(false);fetchHistory(false)}
  schedule(busy?800:document.hidden?15000:fails?Math.min(12000,2500+fails*1500):3000)}
}
document.addEventListener('visibilitychange',()=>{if(!document.hidden&&!pollBusy)schedule(150)});
window.addEventListener('pageshow',()=>{if(!pollBusy)schedule(150)});
schedule(80);
})();
</script>
</body></html>)ELWEB";
