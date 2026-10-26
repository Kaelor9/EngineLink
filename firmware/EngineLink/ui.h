#pragma once
// EngineLink web interface is embedded in flash: offline, no CDN dependencies.
static const char ENGINE_UI[] PROGMEM=R"ELWEB(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#0b1420">
<title>EngineLink · Marine Precision</title>
<style>
:root{color-scheme:dark;--bg:#0b1420;--panel:#132435;--panel2:#192e40;--edge:#2a4252;--text:#edf5f7;--muted:#92adbd;--teal:#46cabc;--amber:#eebc77;--red:#f19998;--green:#80dcb4}
*{box-sizing:border-box}html{scroll-behavior:smooth}body{margin:0;background:radial-gradient(ellipse at 70% -20%,#193845,transparent 58%),var(--bg);color:var(--text);font:14px/1.6 system-ui,-apple-system,Segoe UI,Arial,sans-serif;font-variant-numeric:tabular-nums}button,input,select{font:inherit}button{cursor:pointer}a{color:inherit;text-decoration:none}h1,h2,h3,p{margin-top:0}h1{font-size:clamp(23px,3vw,32px);line-height:1.2;letter-spacing:-.6px}h2{font-size:18px}h3{font-size:15px}small{font-size:11px}.muted,.hint{color:var(--muted)}.eyebrow{font-size:10px;text-transform:uppercase;letter-spacing:1.4px;color:var(--muted);font-weight:700}.wrap{max-width:1240px;margin:0 auto;padding:0 28px 50px}header{display:flex;align-items:center;justify-content:space-between;border-bottom:1px solid var(--edge);height:85px;gap:18px}.brand{display:flex;align-items:center;gap:12px;font-weight:760;font-size:20px;letter-spacing:-.5px}.brandlogo{width:39px;height:39px;border-radius:12px;background:linear-gradient(135deg,#16616a,#1ab1a1);display:grid;place-items:center;color:#efffff;font-size:21px}.brand sub{font-size:9px;letter-spacing:2px;display:block;font-weight:600;color:var(--muted);line-height:1.2}.status{display:flex;align-items:center;gap:10px;color:var(--muted);font-size:12px}.dot{width:8px;height:8px;background:var(--amber);border-radius:50%;display:inline-block}.dot.good{background:var(--green)}.dot.bad{background:var(--red)}.nav{display:flex;gap:5px;padding:16px 0 24px;overflow:auto;scrollbar-width:thin}.nav button{white-space:nowrap;background:none;color:var(--muted);border:1px solid transparent;border-radius:11px;padding:9px 16px}.nav button[aria-selected=true]{background:var(--panel2);border-color:var(--edge);color:var(--text)}.nav button:hover{color:var(--text)}main section.page{display:none}main section.page.visible{display:block}.top{margin-bottom:22px;display:flex;align-items:center;justify-content:space-between;gap:14px}.chip{border:1px solid var(--edge);border-radius:20px;background:#203749;color:#cadbe1;padding:5px 11px;font-size:11px}.chip.amber{color:var(--amber);border-color:#72563c}.columns{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px}.panel{border:1px solid var(--edge);border-radius:18px;background:var(--panel);padding:22px;min-width:0}.panel.alt{background:var(--panel2)}.panel p{color:#b1c4ce}.grid3{display:grid;grid-template-columns:repeat(3,1fr);gap:14px}.kpi{background:#16293a;border:1px solid #2e4655;border-radius:15px;padding:20px}.kpi .number{font-size:clamp(29px,3.5vw,48px);letter-spacing:-1.5px;line-height:1.2;font-weight:750;margin:12px 0 4px}.kpi .unit{font-size:14px;color:var(--muted);font-weight:450;letter-spacing:0}.big-number{font-size:clamp(46px,6vw,74px);font-weight:760;line-height:1.1;letter-spacing:-2px}.big-number .unit{font-size:19px;font-weight:500;letter-spacing:0;color:var(--muted)}.subtext{color:var(--muted);font-size:12px}.source{font-size:11px;color:#6ed1c2}.divider{border-top:1px solid var(--edge);margin:18px 0}.split{display:flex;align-items:center;justify-content:space-between;gap:12px}.row{display:flex;align-items:center;gap:10px;flex-wrap:wrap}.badge{font-size:10px;padding:3px 8px;border:1px solid #355c5b;background:#193d42;border-radius:6px;color:#92ded3;text-transform:uppercase;letter-spacing:.6px}.notice{border:1px solid #575244;border-radius:12px;background:#302f2c;padding:13px 15px;color:#e8c996;font-size:12px}.notice.ok{border-color:#315851;background:#1c3838;color:#a2e6d6}.btn{display:inline-flex;align-items:center;justify-content:center;gap:8px;background:#203a4b;border:1px solid #46616f;color:var(--text);border-radius:11px;min-height:43px;padding:9px 16px;font-weight:650;font-size:13px}.btn.primary{background:#b7ede4;color:#103037;border-color:#b7ede4}.btn.secondary{background:transparent}.btn:disabled{opacity:.48;cursor:not-allowed}.btn:hover:not(:disabled){filter:brightness(1.15)}.formgrid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px}.field{display:flex;flex-direction:column;gap:7px;margin-bottom:15px}.field label{font-weight:650;font-size:12px;color:#bfd2db}.input{width:100%;min-height:43px;background:#0c1d2b;border:1px solid #355266;border-radius:10px;padding:10px 12px;color:var(--text);font-size:14px}.input:focus-visible,button:focus-visible{outline:2px solid #9ee6dc;outline-offset:2px}.choose{display:grid;grid-template-columns:1fr 1fr;gap:12px}.choice{padding:20px;border-radius:13px;border:1px solid #365266;background:#172c3d;text-align:left;color:#c4d7df;min-height:135px}.choice strong{display:block;color:var(--text);font-size:15px;margin-bottom:9px}.choice.active{border-color:var(--teal);box-shadow:inset 0 0 0 1px #3fbcaf}.choice .hint{font-size:12px}.steps{display:flex;align-items:center;gap:8px;margin:18px 0 28px}.step{flex:1;height:4px;background:#2c4557;border-radius:9px}.step.done{background:#48bcae}.code{font:13px/1.6 ui-monospace,SFMono-Regular,Consolas,Monaco,monospace;color:#b9ebe4;word-break:break-word}.traffic{background:#0a1b29;border:1px solid #254458;border-radius:12px;min-height:198px;max-height:400px;overflow:auto;padding:12px}.line{padding:8px 9px;border-bottom:1px solid #203649;display:grid;grid-template-columns:72px 1fr;gap:8px}.line:last-child{border-bottom:0}.line .rx{color:#4fc8bd;font-size:11px}.line .rx.bad{color:var(--red)}.mini{font-size:11px;color:var(--muted)}.statrow{display:flex;justify-content:space-between;gap:16px;padding:10px 0;border-bottom:1px solid #254051;font-size:13px}.statrow:last-child{border-bottom:none}.statrow strong{font-weight:650;text-align:right}.hide{display:none!important}.alertbar{border:1px solid #4f534c;background:#262f31;color:#e6d5ab;padding:12px 15px;border-radius:12px;margin-bottom:20px}.toast{position:fixed;bottom:25px;right:25px;border-radius:11px;border:1px solid #49887f;background:#173e40;color:#e4fefa;padding:13px 17px;max-width:min(420px,90vw);display:none;z-index:20;box-shadow:0 12px 30px #0007}.wizard{width:min(680px,100%);margin:30px auto 0}.wizard .panel{padding:33px}.termhelp{display:none;background:#203747;border:1px solid #3f5969;border-radius:11px;padding:16px;margin-top:11px;font-size:12px}.termhelp.visible{display:block}.inline-link{border:0;background:transparent;color:#61d1c4;padding:0;text-decoration:underline;text-underline-offset:3px}.footer{border-top:1px solid var(--edge);margin-top:45px;padding:20px 0;color:#728e9d;font-size:11px}.ledsample{width:65px;height:65px;border-radius:50%;background:#1a998d;border:3px solid #345368;box-shadow:0 0 17px #0ea89255}.bars{height:6px;background:#213e4b;border-radius:6px;margin-top:17px}.bars div{height:100%;background:#42bbae;border-radius:6px;width:0}.setupIntro{font-size:15px;color:#b9ccd5!important}.wstep{display:none}.wstep.active{display:block}.maintitle{margin-bottom:6px}.details{color:#9cb8c5;font-size:12px}.tag{display:inline-flex;gap:6px;align-items:center}.num{font-variant-numeric:tabular-nums}
@media(max-width:800px){.columns,.grid3{grid-template-columns:1fr}.wrap{padding:0 16px 35px}.panel{padding:19px}.wizard .panel{padding:21px}.nav button{padding:9px 12px}.top{align-items:flex-start;flex-direction:column}.status{font-size:11px}}@media(max-width:490px){header{height:75px}.brand{font-size:17px}.brandlogo{width:33px;height:33px}.choose,.formgrid{grid-template-columns:1fr}.line{grid-template-columns:52px 1fr}.big-number{font-size:53px}.panel{padding:16px}.choice{min-height:110px}.status .long{display:none}}
</style>
</head>
<body><div class="wrap">
<header><div class="brand"><span class="brandlogo">≈</span><span>EngineLink<sub>MARINE PRECISION</sub></span></div><div class="status"><span id="onlineDot" class="dot"></span><span id="online">Connecting</span><span id="modeBadge" class="chip">PASSIVE</span></div></header>
<div id="wizard" class="wizard hide">
  <div class="panel">
  <div class="eyebrow">ENGINE LINK / FIRST-TIME SETUP</div><h1 id="wizardHeading" style="margin-top:12px">Welcome to EngineLink</h1>
  <p class="setupIntro">Configure your network and engines. All choices can be changed later.</p>
  <div class="steps"><div class="step done" id="stepbar1"></div><div class="step" id="stepbar2"></div><div class="step" id="stepbar3"></div></div>
  <div class="wstep active" id="wstep1">
   <h2>How would you like to connect?</h2>
   <div class="choose"><button class="choice active" type="button" data-network="ap" onclick="pickNetwork('ap')"><strong>EngineLink Wi-Fi</strong><span class="hint">Connect directly to EngineLink. No router or internet is needed.</span></button><button class="choice" type="button" data-network="router" onclick="pickNetwork('router')"><strong>Connect to a router</strong><span class="hint">Connect EngineLink to your boat's existing wireless network.</span></button></div>
   <div id="routerFields" class="hide" style="margin-top:20px"><div class="field"><label for="wifiSSID">Router Wi-Fi name (SSID)</label><input class="input" id="wifiSSID" autocomplete="off" placeholder="Your boat's Wi-Fi"></div><div class="field"><label for="wifiPass">Router Wi-Fi password</label><input class="input" id="wifiPass" type="password" autocomplete="off" placeholder="Router password"></div></div>
   <div class="field" style="margin-top:21px"><label for="apPass">EngineLink Wi-Fi password</label><input class="input" id="apPass" type="password" minlength="8" placeholder="Leave blank to keep current password"><span class="hint">Factory password: 12345678. You may change it now or later in Settings.</span></div>
  </div>
  <div class="wstep" id="wstep2">
   <h2>Assign your engines</h2><p>Identify which physical J1708 connector belongs to each engine.</p>
   <div class="field"><label for="enginePort">Port engine is connected to</label><select class="input" id="enginePort"><option value="1">Channel 1 · J7</option><option value="2">Channel 2 · J6</option><option value="0">Not assigned yet</option></select></div>
   <div class="notice">To identify the connections, start one engine at a time and check the passive channel activity in Traffic. EngineLink never sends J1708 commands.</div>
  </div>
  <div class="wstep" id="wstep3">
   <h2>Date & Time</h2><p>EngineLink can work without internet. Timestamps are marked as unknown until a source is available.</p>
   <div class="field"><label for="setupTime">Time source</label><select class="input" id="setupTime"><option value="auto">Automatic</option><option value="phone">Phone / browser</option><option value="gps">NMEA2000 / GPS</option><option value="ntp">Router / NTP</option><option value="manual">Manual (not synchronized)</option></select></div>
   <div class="notice">EngineLink will keep its own Wi-Fi active as a fallback, even when using a router. The NMEA2000 interface is not activated until it is enabled in Settings.</div>
  </div>
  <div class="split" style="margin-top:24px"><button class="btn secondary" id="wizardBack" onclick="wizardBack()" disabled>Back</button><button class="btn primary" id="wizardNext" onclick="wizardNext()">Continue</button></div>
  <p id="wizardError" class="mini" style="color:#f5aeac;margin:15px 0 0"></p>
  </div>
</div>

<div id="application" class="hide">
<nav class="nav" aria-label="Primary"><button aria-selected="true" data-page="fuel" onclick="page('fuel')">Fuel Overview</button><button aria-selected="false" data-page="traffic" onclick="page('traffic')">Traffic</button><button aria-selected="false" data-page="history" onclick="page('history')">History</button><button aria-selected="false" data-page="device" onclick="page('device')">Device</button><button aria-selected="false" data-page="settings" onclick="page('settings')">Settings</button></nav>
<div id="alertbar" class="alertbar hide">Some data is unavailable.</div>
<main>
<section id="fuel" class="page visible">
<div class="top"><div><div class="eyebrow">ENGINE INSTRUMENTS</div><h1 class="maintitle">Fuel Overview</h1><div class="subtext">Live fuel consumption · Verified ECU data only</div></div><span class="chip">LIVE INPUT</span></div>
<div class="panel alt" style="margin-bottom:16px"><div class="eyebrow">TOTAL FUEL CONSUMPTION</div><div class="big-number" id="totalFuel">— <span class="unit">L/h</span></div><div class="subtext" id="fuelSource">Waiting for valid fuel data from both channels.</div></div>
<div class="columns">
 <article class="panel"><div class="split"><div class="eyebrow">PORT ENGINE</div><span class="badge" id="portPort">UNASSIGNED</span></div><div class="kpi" style="margin-top:14px"><div class="eyebrow">FUEL FLOW</div><div class="number" id="portFuel">— <span class="unit">L/h</span></div><div class="statrow"><span class="hint">Engine RPM</span><strong id="portRpm">—</strong></div><div class="statrow"><span class="hint">Observed fuel this boot</span><strong id="portObserved">—</strong></div></div></article>
 <article class="panel"><div class="split"><div class="eyebrow">STARBOARD ENGINE</div><span class="badge" id="stbdPort">UNASSIGNED</span></div><div class="kpi" style="margin-top:14px"><div class="eyebrow">FUEL FLOW</div><div class="number" id="stbdFuel">— <span class="unit">L/h</span></div><div class="statrow"><span class="hint">Engine RPM</span><strong id="stbdRpm">—</strong></div><div class="statrow"><span class="hint">Observed fuel this boot</span><strong id="stbdObserved">—</strong></div></div></article>
</div>
<div class="grid3" style="margin-top:16px">
 <article class="kpi"><div class="eyebrow">FUEL ECONOMY</div><div class="number" id="fuelEconomy">— <span class="unit">L/nm</span></div><div class="mini">Requires fresh NMEA2000 SOG</div></article>
 <article class="kpi"><div class="eyebrow">SPEED OVER GROUND</div><div class="number" id="speed">— <span class="unit">kn</span></div><div class="mini">GPS / NMEA2000</div></article>
 <article class="kpi"><div class="eyebrow">CURRENT BOOT SESSION</div><div class="number" id="sessionFuel">— <span class="unit">L*</span></div><div class="mini">*Only integrated across valid reported intervals</div></article>
</div>
<div class="notice" style="margin-top:16px">Fuel rates are displayed only when valid J1587 PID 183 data arrive. EngineLink cannot assume every ECU supplies this parameter. Missing data are never interpreted as zero.</div>
</section>
<section id="traffic" class="page">
<div class="top"><div><div class="eyebrow">BUS OBSERVABILITY</div><h1>J1708 Traffic</h1><div class="subtext">Read-only Packet Inspector · No transmission</div></div><span class="chip">RX ONLY</span></div>
<div class="columns"><div class="panel"><div class="split"><h2 style="margin-bottom:0">Channel monitor</h2><select class="input" style="width:auto" id="channelChoice" onchange="drawTraffic()"><option value="0">Channel 1 · J7</option><option value="1">Channel 2 · J6</option></select></div>
  <div class="split" style="margin:13px 0"><span class="mini" id="channelCount">0 frames</span><div class="row"><button class="btn secondary" onclick="toggleTrafficPause()" id="pauseTraffic">Pause</button><button class="btn secondary" onclick="copyLastPacket()">Copy HEX</button></div></div>
  <div class="traffic" id="trafficMonitor"><span class="hint">Waiting for actual J1708 messages…</span></div><p class="mini">Traffic is collected in the background even when the monitor is paused.</p></div>
<div class="panel"><h2>Selected packet</h2><div class="eyebrow">RAW HEX DATA</div><div class="code" id="packetHex" style="background:#0a1b29;padding:16px;border-radius:11px;margin:10px 0 20px">—</div><div class="statrow"><span class="hint">Channel</span><strong id="packetChannel">—</strong></div><div class="statrow"><span class="hint">MID</span><strong id="packetMID">—</strong></div><div class="statrow"><span class="hint">Checksum</span><strong id="packetValid">—</strong></div><div class="statrow"><span class="hint">Timestamp</span><strong id="packetTime">—</strong></div><p class="details" style="margin-top:15px">Frames are separated by UART hardware receive timeout and validated using the additive J1708 checksum. Hardware framing still requires testing with real engine traffic.</p></div></div>
</section>
<section id="history" class="page"><div class="top"><div><div class="eyebrow">RECORDING & HISTORY</div><h1>Fuel History</h1><div class="subtext">Automatic engine-period capture · Raw evidence preserved</div></div><span class="chip">SESSION BASED</span></div><div class="columns">
<div class="panel"><h2>Current boot session</h2><div class="statrow"><span>Session file</span><strong id="logName">—</strong></div><div class="statrow"><span>Raw frames saved</span><strong id="logRows">0</strong></div><div class="statrow"><span>SD status</span><strong id="logStatus">—</strong></div><div class="statrow"><span>Logging</span><strong id="logRunning">—</strong></div><div class="row" style="margin-top:23px"><a class="btn primary" href="/api/log" target="_blank" rel="noopener">Download CSV</a><button class="btn" onclick="post('/api/sd/start')">Start logging</button><button class="btn secondary" onclick="post('/api/sd/stop')">Stop logging</button></div></div>
<div class="panel"><h2>Fuel accounting</h2><p>The firmware automatically accumulates fuel during consecutive valid PID 183 readings. Engine stops do not automatically create or close boat trips.</p><div class="statrow"><span>J7 · observed fuel</span><strong id="histJ7">—</strong></div><div class="statrow"><span>J6 · observed fuel</span><strong id="histJ6">—</strong></div><div class="statrow"><span>Data gaps</span><strong id="histGaps">—</strong></div><div class="notice" style="margin-top:20px">These boot-session totals are incomplete if time or fuel data are missing. Cross-restart trip grouping and persistent trip totals are not yet implemented.</div></div></div></section>
<section id="device" class="page"><div class="top"><div><div class="eyebrow">SYSTEM & HARDWARE</div><h1>EngineLink Device</h1><div class="subtext">Live hardware health and diagnostics</div></div><span class="chip" id="timeBadge">TIME UNKNOWN</span></div>
<div class="columns"><div class="panel"><h2>Hardware status</h2><div class="statrow"><span>Uptime</span><strong id="uptime">—</strong></div><div class="statrow"><span>Heap available</span><strong id="freeHeap">—</strong></div><div class="statrow"><span>PSRAM</span><strong id="psram">—</strong></div><div class="statrow"><span>Optoisolated GPIO38</span><strong id="opto">—</strong></div><div class="statrow"><span>NMEA2000 controller</span><strong id="canStatus">—</strong></div><div class="statrow"><span>NMEA2000 RX frames</span><strong id="canFrames">—</strong></div></div>
<div class="panel"><h2>Network & time</h2><div class="statrow"><span>Direct Wi-Fi</span><strong>EngineLink · 192.168.4.1</strong></div><div class="statrow"><span>Wi-Fi clients</span><strong id="apClients">—</strong></div><div class="statrow"><span>Router</span><strong id="routerStatus">—</strong></div><div class="statrow"><span>Time source</span><strong id="clockSource">—</strong></div><div class="statrow"><span>Last known UTC</span><strong id="clockUtc">—</strong></div><div class="statrow"><span>Wi-Fi disconnects</span><strong id="apLeaves">—</strong></div><div class="row" style="margin-top:15px"><button class="btn" onclick="syncPhone()">Sync time from phone</button></div></div>
<div class="panel"><h2>Status LED</h2><div class="row"><div class="ledsample" id="ledSample"></div><div><div class="eyebrow">GPIO48</div><div class="hint">Physical addressable RGB LED</div></div></div><div class="field" style="margin-top:22px"><label for="ledMode">LED control</label><select class="input" id="ledMode"><option value="auto">Automatic system status</option><option value="manual">Manual color</option></select></div><div class="row"><input type="color" id="ledColor" value="#26b8ae" style="height:44px;width:90px"><div style="flex:1"><label for="ledBright" class="mini">Brightness</label><input type="range" min="1" max="180" value="34" id="ledBright" style="width:100%"></div></div><button class="btn primary" style="margin-top:16px" onclick="applyLED()">Apply LED</button></div>
<div class="panel"><h2>Communication health</h2><div class="statrow"><span>J7 frames / invalid</span><strong id="healthJ7">—</strong></div><div class="statrow"><span>J6 frames / invalid</span><strong id="healthJ6">—</strong></div><div class="statrow"><span>UART buffer drops</span><strong id="healthDrops">—</strong></div><div class="statrow"><span>Minimum heap</span><strong id="minHeap">—</strong></div><div class="notice" style="margin-top:18px">The PCB cannot measure its isolated 5 V rails in software. Verify both separately before attaching any engine bus.</div></div></div></section>
<section id="settings" class="page"><div class="top"><div><div class="eyebrow">SYSTEM CONFIGURATION</div><h1>Settings</h1><div class="subtext">Change onboarding choices without resetting the device</div></div><span class="chip">LOCAL</span></div>
<div class="columns"><div class="panel"><h2>Connection</h2><div class="choose" style="margin-bottom:18px"><button class="choice" data-settings-network="ap" onclick="pickSettingsNetwork('ap')"><strong>EngineLink Wi-Fi</strong><span class="hint">Direct connection without a router.</span></button><button class="choice" data-settings-network="router" onclick="pickSettingsNetwork('router')"><strong>Connect to a router</strong><span class="hint">Use an existing local network.</span></button></div><div id="settingsRouterFields"><div class="field"><label for="settingsSSID">Router network name</label><input class="input" id="settingsSSID" placeholder="Wi-Fi SSID"></div><div class="field"><label for="settingsPass">Router password</label><input class="input" id="settingsPass" placeholder="Leave blank to keep existing" type="password"></div></div><div class="field"><label for="settingsAPPass">EngineLink Wi-Fi password</label><input class="input" id="settingsAPPass" type="password" placeholder="Leave blank to keep existing"></div></div>
<div class="panel"><h2>Engines & time</h2><div class="field"><label for="settingsPort">Port engine connector</label><select class="input" id="settingsPort"><option value="1">J7 / Channel 1</option><option value="2">J6 / Channel 2</option><option value="0">Unassigned</option></select></div><div class="field"><label for="settingsTime">Time source</label><select class="input" id="settingsTime"><option value="auto">Automatic</option><option value="phone">Phone / browser</option><option value="gps">NMEA2000 / GPS</option><option value="ntp">Router / NTP</option><option value="manual">Manual / none</option></select></div><div class="field"><label style="display:flex;gap:12px;align-items:center"><input id="settingsCAN" type="checkbox">Enable NMEA2000 listen-only receiver</label><div class="notice">CAN hardware has not been fully validated. Listen-only mode never transmits CAN frames or acknowledgments.</div></div><button class="btn primary" onclick="saveSettings()" style="width:100%">Save settings</button></div></div>
<div class="panel" style="margin-top:16px"><div class="split"><div><div class="eyebrow">ADVANCED SETTINGS</div><h2 style="margin-top:8px">J1708 Terminal</h2></div><span class="badge">READ ONLY</span></div><div class="row" style="margin-bottom:16px"><select class="input" style="max-width:230px" id="terminalChannel" onchange="drawTerminal()"><option value="0">Channel 1 · J7</option><option value="1">Channel 2 · J6</option></select><span class="hint">9600 baud / 8N1</span></div><div class="eyebrow" style="margin-bottom:8px">SERIAL MONITOR</div><div class="traffic" id="terminalMonitor"><span class="hint">Waiting for J1708 frames…</span></div><div class="eyebrow" style="margin-top:22px">TERMINAL / HEX COMPOSER</div><div class="field"><label for="terminalDraft">HEX message</label><input class="input code" id="terminalDraft" placeholder="e.g. 80 BE 00 00" disabled></div><button class="btn" disabled>Send frame (not enabled)</button> <button class="inline-link" style="margin:10px" onclick="toggleHelp()">Having trouble writing? ℹ️</button><div class="termhelp" id="termHelp"><strong>About J1708 messages</strong><p>J1708 / J1587 uses binary bytes, not normal text commands. A frame begins with an MID, followed by parameter IDs (PIDs), data bytes and an additive checksum.</p><p>PID 190 is engine speed (0.25 RPM/bit). PID 183 is instantaneous fuel rate (1/64 US gallon/hour per bit). Parameters and ECU support must be validated against real traffic.</p><p>Transmission is intentionally disabled until J1708 arbitration, electrical safety and command protocols have been separately verified. These help examples must never be sent to a live engine.</p></div></div>
</section></main></div>
<div id="toast" class="toast" role="status"></div><div class="footer">EngineLink · Marine Precision · 0.1.0 alpha · Passive capture only · Fuel data subject to ECU support</div>
</div>
<script>
(function(){
'use strict';
const el=id=>document.getElementById(id);
let state=null,appStarted=false,wizardStep=1,networkChoice='ap',settingsNetwork='ap';
let trafficPaused=false, trafficSnapshot=[],pollTimer=null,pollAbort=null,pollFailures=0,commandBusy=false,lastOk=0,currentPage='fuel';
const fmt=(n,d=1)=>n===null||n===undefined||!Number.isFinite(Number(n))?'—':Number(n).toLocaleString('da-DK',{minimumFractionDigits:d,maximumFractionDigits:d});
const txt=(id,s)=>{const e=el(id);if(e)e.textContent=s};
function toast(message,bad=false){const e=el('toast');e.textContent=message;e.style.borderColor=bad?'#c98286':'#49887f';e.style.display='block';clearTimeout(window.__toast);window.__toast=setTimeout(()=>e.style.display='none',4300)}
function setOnline(ok){const d=el('onlineDot');d.className='dot '+(ok?'good':'bad');txt('online',ok?'Connected':'Reconnecting'+(lastOk?' · '+Math.round((Date.now()-lastOk)/1000)+' s':''))}
function schedulePoll(ms){clearTimeout(pollTimer);pollTimer=setTimeout(poll,ms)}
async function request(path,params){
 if(commandBusy){toast('Another command is in progress.',true);return null}
 commandBusy=true;
 if(pollAbort)pollAbort.abort();
 const c=new AbortController(),t=setTimeout(()=>c.abort(),6500);
 try{
  const r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(params||{}),cache:'no-store',signal:c.signal});
  const result=await r.json();
  if(!r.ok||result.ok===false){toast(result.message||'Request failed',true);return null}
  toast(result.message||'Saved');
  return result;
 }catch(e){toast('No response from EngineLink. Check Wi-Fi connection.',true);return null}
 finally{clearTimeout(t);commandBusy=false;schedulePoll(300)}
}
function switchWizard(step){
 wizardStep=step;
 for(let i=1;i<=3;i++){el('wstep'+i).classList.toggle('active',i===step);el('stepbar'+i).classList.toggle('done',i<=step)}
 txt('wizardHeading',step===1?'Welcome to EngineLink':step===2?'Engine configuration':'Date & Time');
 el('wizardBack').disabled=step===1;
 txt('wizardNext',step===3?'Finish setup':'Continue');
 txt('wizardError','');
}
window.pickNetwork=function(value){networkChoice=value;document.querySelectorAll('[data-network]').forEach(b=>b.classList.toggle('active',b.dataset.network===value));el('routerFields').classList.toggle('hide',value!=='router')}
window.wizardBack=function(){if(wizardStep>1)switchWizard(wizardStep-1)}
window.wizardNext=async function(){
 if(wizardStep===1&&networkChoice==='router'&&!el('wifiSSID').value.trim()){txt('wizardError','Enter the router Wi-Fi network name.');return}
 if(wizardStep<3){switchWizard(wizardStep+1);return}
 const saved=await request('/api/setup',{network:networkChoice,ssid:el('wifiSSID').value.trim(),pass:el('wifiPass').value,apPass:el('apPass').value,port:el('enginePort').value,time:el('setupTime').value,can:'0'});
 if(saved){el('wizard').classList.add('hide');el('application').classList.remove('hide');appStarted=true;schedulePoll(200);if(el('setupTime').value==='auto'||el('setupTime').value==='phone')setTimeout(syncPhone,900)}
}
window.page=function(name){currentPage=name;document.querySelectorAll('.page').forEach(x=>x.classList.toggle('visible',x.id===name));document.querySelectorAll('.nav button').forEach(b=>b.setAttribute('aria-selected',String(b.dataset.page===name)));if(name==='traffic')drawTraffic();if(name==='settings')drawTerminal()}
window.pickSettingsNetwork=function(name){settingsNetwork=name;document.querySelectorAll('[data-settings-network]').forEach(b=>b.classList.toggle('active',b.dataset.settingsNetwork===name));el('settingsRouterFields').classList.toggle('hide',name!=='router')}
function loadSettings(s){
 pickSettingsNetwork(s.config.router?'router':'ap');
 el('settingsSSID').value=s.network.ssid||'';
 el('settingsPort').value=String(s.config.portChannel);
 el('settingsTime').value=s.config.timePref;
 el('settingsCAN').checked=s.config.canListen;
}
window.saveSettings=async function(){
 const oldPass=el('settingsAPPass').value;
 const success=await request('/api/settings',{network:settingsNetwork,ssid:el('settingsSSID').value.trim(),pass:el('settingsPass').value,apPass:oldPass,port:el('settingsPort').value,time:el('settingsTime').value,can:el('settingsCAN').checked?'1':'0'});
 if(success){
  el('settingsAPPass').value='';el('settingsPass').value='';
  if(oldPass)toast('Wi-Fi password changed. Reconnect to EngineLink with your new password.');
 }
}
window.syncPhone=async function(){
 if(!state)return;
 if(!(state.config.timePref==='auto'||state.config.timePref==='phone')){toast('Select Phone or Automatic in time settings to sync.',true);return}
 await request('/api/phone-time',{ms:String(Date.now()),zone:Intl.DateTimeFormat().resolvedOptions().timeZone||'UTC'});
}
window.applyLED=()=>request('/api/led',{mode:el('ledMode').value,hex:el('ledColor').value.slice(1),brightness:el('ledBright').value});
window.post=path=>request(path,{});
window.toggleHelp=()=>el('termHelp').classList.toggle('visible');
window.toggleTrafficPause=function(){trafficPaused=!trafficPaused;txt('pauseTraffic',trafficPaused?'Resume':'Pause');if(!trafficPaused)drawTraffic()}
function filteredPackets(ch){return trafficSnapshot.filter(e=>e.ch===ch)}
function drawLines(elementId,channel){
 const root=el(elementId);
 const frames=filteredPackets(channel);
 if(!frames.length){root.innerHTML='<span class="hint">No messages received on this channel.</span>';return}
 root.replaceChildren();
 frames.forEach((entry,index)=>{
   const line=document.createElement('div');line.className='line';
   const prefix=document.createElement('span');prefix.className='rx'+(entry.ok?'':' bad');prefix.textContent=(entry.ok?'RX':'ERR')+' #'+(index+1);
   const hex=document.createElement('span');hex.className='code';hex.textContent=entry.hex;
   line.append(prefix,hex);
   line.addEventListener('click',()=>{txt('packetHex',entry.hex);txt('packetMID',entry.mid);txt('packetValid',entry.ok?'VALID':'INVALID');txt('packetChannel',entry.ch===0?'J7':'J6');txt('packetTime',(entry.ms/1000).toFixed(2)+' s since boot')});
   root.append(line);
 });
}
window.drawTraffic=function(){
 if(!state||trafficPaused)return;
 const ch=Number(el('channelChoice').value);
 drawLines('trafficMonitor',ch);
 const selected=filteredPackets(ch)[0];
 const counts=state.channels[ch];
 txt('channelCount',counts.frames+' frames · '+counts.invalid+' invalid · '+counts.drops+' lost in queue');
 if(selected){txt('packetHex',selected.hex);txt('packetMID',selected.mid);txt('packetValid',selected.ok?'VALID':'INVALID');txt('packetChannel',ch===0?'J7':'J6');txt('packetTime',(selected.ms/1000).toFixed(2)+' s since boot')}
}
window.drawTerminal=function(){if(!state)return;drawLines('terminalMonitor',Number(el('terminalChannel').value))}
window.copyLastPacket=async function(){
 const ch=Number(el('channelChoice').value),p=filteredPackets(ch)[0];
 if(!p)return;
 try{await navigator.clipboard.writeText(p.hex);toast('HEX copied')}catch(e){toast('Copy unavailable in this browser.',true)}
}
function render(s){
 if(!appStarted){el('wizard').classList.toggle('hide',s.setup);el('application').classList.toggle('hide',!s.setup);appStarted=s.setup;if(s.setup)loadSettings(s)}
 state=s;
 const a=s.channels;
 const port=s.fuel.portChannel,stbd=s.fuel.starboardChannel;
 txt('portPort',port<0?'UNASSIGNED':a[port].name);
 txt('stbdPort',stbd<0?'UNASSIGNED':a[stbd].name);
 txt('totalFuel',s.fuel.total===null?'— L/h':fmt(s.fuel.total)+' L/h');
 txt('fuelSource',s.fuel.total===null?'Waiting for fresh valid PID 183 from both assigned engines.':'Both values originate from the received ECU messages.');
 function renderMotor(prefix,idx){
  const x=idx<0?null:a[idx];
  txt(prefix+'Fuel',x?.fuel===null||!x?'— L/h':fmt(x.fuel)+' L/h');
  txt(prefix+'Rpm',x?.rpm===null||!x?'—':fmt(x.rpm,0)+' RPM');
  txt(prefix+'Observed',x?fmt(x.observedLiters,3)+' L*':'—');
 }
 renderMotor('port',port);renderMotor('stbd',stbd);
 txt('fuelEconomy',s.fuel.litersPerNm===null?'— L/nm':fmt(s.fuel.litersPerNm,2)+' L/nm');
 txt('speed',s.fuel.sogKnots===null?'— kn':fmt(s.fuel.sogKnots,1)+' kn');
 txt('sessionFuel',fmt(a[0].observedLiters+a[1].observedLiters,3)+' L*');
 txt('logName',s.sd.filename||'None');
 txt('logRows',s.sd.rows);
 txt('logStatus',s.sd.status);
 txt('logRunning',s.sd.logging?'Recording':'Stopped');
 txt('histJ7',fmt(a[0].observedLiters,3)+' L*');txt('histJ6',fmt(a[1].observedLiters,3)+' L*');
 txt('histGaps',a[0].gaps+a[1].gaps);
 txt('uptime',Math.floor(s.uptime/3600)+'h '+Math.floor(s.uptime%3600/60)+'m '+s.uptime%60+'s');
 txt('freeHeap',Math.round(s.hardware.heap/1024)+' KB');
 txt('minHeap',Math.round(s.hardware.minHeap/1024)+' KB');
 txt('psram',fmt(s.hardware.psram/1048576,1)+' MB');
 txt('opto',s.hardware.opto?'HIGH':'LOW');
 txt('canStatus',s.hardware.canReady?'Listen only':'Disabled');
 txt('canFrames',s.hardware.nmeaRx);
 txt('apClients',s.network.apClients);
 txt('routerStatus',s.network.staConnected?s.network.staIP:(s.config.router?'Disconnected':'Not selected'));
 txt('clockSource',s.timeSource);
 txt('clockUtc',s.timeKnown?new Date(s.utc*1000).toLocaleString():'Unknown');
 txt('apLeaves',s.network.apLeaves+' (reason '+s.network.apReason+')');
 txt('timeBadge',s.timeKnown?'CLOCK SYNCED':'TIME UNKNOWN');
 txt('healthJ7',a[0].frames+' / '+a[0].invalid);
 txt('healthJ6',a[1].frames+' / '+a[1].invalid);
 txt('healthDrops',a[0].drops+a[1].drops);
 txt('modeBadge','PASSIVE');
 if(!trafficPaused)trafficSnapshot=s.recent||[];
 if(currentPage==='traffic')drawTraffic();
 if(currentPage==='settings')drawTerminal();
 const warning=!s.sd.logging?'SD logging is off. Raw data may not survive a restart.':(!s.timeKnown?'No absolute time source available. Recording uses uptime timestamps.':'');
 el('alertbar').classList.toggle('hide',!warning);
 txt('alertbar',warning);
}
async function poll(){
 if(commandBusy){schedulePoll(700);return}
 const c=new AbortController();
 pollAbort=c;
 const timeout=setTimeout(()=>c.abort(),4700);
 try{
  const response=await fetch('/api/state',{cache:'no-store',signal:c.signal});
  if(!response.ok)throw new Error('HTTP '+response.status);
  const data=await response.json();
  pollFailures=0;lastOk=Date.now();setOnline(true);
  render(data);
  if(!state.timeKnown&&(state.config.timePref==='phone'||state.config.timePref==='auto')&&state.setup&&!window.__clockRequested){window.__clockRequested=true;setTimeout(syncPhone,900)}
 }catch(e){if(!commandBusy){pollFailures++;setOnline(false)}}
 finally{clearTimeout(timeout);pollAbort=null;schedulePoll(commandBusy?900:document.hidden?12000:pollFailures?Math.min(10000,2000+pollFailures*2000):3600)}
}
document.addEventListener('visibilitychange',()=>{if(!document.hidden)schedulePoll(150)});
window.addEventListener('pageshow',()=>schedulePoll(150));
schedulePoll(100);
})();
</script>
</body></html>)ELWEB";
