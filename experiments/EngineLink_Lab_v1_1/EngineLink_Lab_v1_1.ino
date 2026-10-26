/*
  EngineLink Lab v1.1 - ESP32-S3-WROOM-1-N8R8

  Offline SoftAP dashboard, LED effects, dual J1708/J1587 bench demo,
  SD logging + R/W diagnostic, digital opto input and memory reporting.

  IMPORTANT ELECTRICAL RULES:
  - PHYSICAL J1708 TX IS DISABLED ON BOOT. Run only on the bench.
  - Engines and NMEA2000 networks MUST be disconnected.
  - Measure +5V_ISO1 to GND_ISO1 and +5V_ISO2 to GND_ISO2 (4.75..5.25 V).
  - Close SJ1 and SJ2 only for bench transmission; J7 pin1(B) to
    J6 pin1(B) and J7 pin2(A) to J6 pin2(A). DO NOT bridge ISO grounds.
  - Open SJ1/SJ2 before reconnecting real engine buses.
  - Generated J1587-like signals are DEMONSTRATION data, NOT actual engines.
  - CAN / NMEA2000 transceiver is NOT initialized and NEVER transmits.

  Arduino IDE: ESP32S3 Dev Module, 8 MB flash, OPI PSRAM (if fitted),
  USB CDC On Boot enabled, Serial Monitor 115200 baud.
  Requires Adafruit NeoPixel; WiFi, WebServer, SPI and SD come with ESP32 core.
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>

// Change these before long-term use. SoftAP is a SEPARATE Wi-Fi network.
const char* AP_SSID = "KAELOR-EngineLink";
const char* AP_PASS = "EngineLab26!"; // min. 8 chars; change as desired

constexpr uint8_t PIN_J7_RX = 21, PIN_J7_TX = 47; // J1708 #1
constexpr uint8_t PIN_J6_RX = 17, PIN_J6_TX = 18; // J1708 #2
constexpr uint8_t PIN_LED = 48;
constexpr uint8_t PIN_SD_CS = 10, PIN_SD_MOSI = 11;
constexpr uint8_t PIN_SD_SCK = 12, PIN_SD_MISO = 13, PIN_SD_DETECT = 14;
constexpr uint8_t PIN_OPTO = 38;
constexpr uint8_t PIN_CAN_TX = 15, PIN_CAN_RX = 16; // NOT USED in this version
constexpr uint32_t BUS_BAUD = 9600;
constexpr uint32_t RX_TIMEOUT_MS = 110;
constexpr uint8_t FRAME_LEN = 7;
constexpr uint8_t EVENT_CAP = 18;

WebServer server(80);
HardwareSerial &j7 = Serial1;
HardwareSerial &j6 = Serial2;
Adafruit_NeoPixel led(1, PIN_LED, NEO_GRB + NEO_KHZ800);

// OFF: no data. DEMO: software-generated bus frames only.
// PHYSICAL: transmits actual UART bytes across J7/J6 bench cable.
enum RunMode : uint8_t { STOPPED, DEMO, PHYSICAL };
enum LedMode : uint8_t { LED_OFF, LED_SOLID, LED_RAINBOW, LED_BREATH, LED_HEART, LED_TRAFFIC };
RunMode runMode = STOPPED;
LedMode ledMode = LED_OFF;
uint8_t ledBrightness = 40;
uint32_t ledRGB = 0x007BFF;
uint32_t lastLedMs = 0, lastSendMs = 0;
uint32_t lastPulseMs = 0;
bool pulseFailed = false;
uint32_t ledPrev = 0xFFFFFFFF;
uint16_t busIntervalMs = 1800;
uint16_t rpmTarget[2] = {825, 890};
uint16_t rpmLive[2] = {825, 890};
uint8_t loadTarget[2] = {36, 42};
uint8_t loadLive[2] = {36, 42};
uint8_t senderSelection = 0; // 0=alternate, 1=J7, 2=J6
uint8_t nextSender = 0;
uint32_t frameSeq = 0;
uint32_t rxOk[2] = {0, 0};
uint32_t rxErrors[2] = {0, 0};
uint32_t timeouts[2] = {0, 0};
uint32_t badChecksums[2] = {0, 0};
uint32_t rxExtra[2] = {0, 0};
uint32_t idleFaults = 0;
String lastError = "Ingen fejl";

bool sdMounted = false;
bool sdLogging = false;
uint32_t logRows = 0;
File csv;
const char* LOG_FILE = "/KLRLAB.CSV";
const char* TEST_FILE = "/KLRCHK.TXT";
String sdMessage = "Ikke initialiseret";

struct Event {
  uint32_t id = 0, ms = 0;
  uint8_t from = 0, to = 0, len = 0;
  uint16_t rpm = 0;
  uint8_t load = 0;
  bool success = false, physical = false;
  char hex[3 * FRAME_LEN + 2] = {};
  char comment[20] = {};
};
Event events[EVENT_CAP];
uint8_t eventWrite = 0, eventSize = 0;
uint32_t eventId = 0;

bool awaiting = false;
uint32_t txStartedMs = 0;
uint8_t activeDirection = 0;
uint8_t expected[FRAME_LEN] = {};
uint8_t received[FRAME_LEN] = {};
uint8_t receivedLen = 0;
uint16_t expectedRPM = 0;
uint8_t expectedLoad = 0;

// Embedded HTML/CSS/JS. No external CDN or internet required.
const char INDEX_HTML[] PROGMEM = R"ENGINELAB(
<!doctype html>
<html lang="da">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>KAELOR · EngineLink Lab 1.1</title>
<style>
:root{font:14px/1.5 system-ui,-apple-system,Segoe UI,Arial,sans-serif;color:#eaf1f6;background:#081522;--panel:#102234;--line:#264256;--muted:#9cb4c4;--teal:#49d8c5;--amber:#ffcd84}
*{box-sizing:border-box}body{margin:0;background:#081522}button,select,input{font:inherit}button{border:1px solid #375a70;color:#eaf1f6;background:#203648;border-radius:9px;padding:9px 13px;cursor:pointer;font-weight:650}button:hover{background:#2a4b60}button:disabled{opacity:.5;cursor:default}.primary{background:#38cbb8;color:#082b32;border-color:#38cbb8}.danger{color:#ffadb2;border-color:#8b4a53}.shell{max-width:1220px;margin:auto;padding:24px 22px 45px}.header{display:flex;justify-content:space-between;gap:20px;align-items:center;padding-bottom:18px;border-bottom:1px solid #274459}.brand{display:flex;gap:12px;align-items:center}.brandmark{width:39px;height:39px;border-radius:11px;background:#31c8b6;color:#04242b;display:grid;place-items:center;font-weight:900;font-size:22px}.brand b{font-size:17px;letter-spacing:.4px}.brand small{color:var(--muted);display:block;font-size:11px}.headright{display:flex;gap:8px;align-items:center;flex-wrap:wrap}.pill{display:inline-flex;align-items:center;gap:7px;border:1px solid #36566a;border-radius:50px;padding:5px 10px;color:#b8d4e0;background:#132c3d;font-size:12px}.ball{width:7px;height:7px;border-radius:50%;background:#43d6ac}.tabs{display:flex;gap:5px;border-bottom:1px solid var(--line);margin:21px 0 23px}.tab{background:none;border:none;border-bottom:2px solid transparent;border-radius:0;color:var(--muted);padding:11px 17px}.tab[aria-selected=true]{color:#eafaf6;border-bottom-color:var(--teal)}.view{display:none}.view.active{display:block}.sectiontop{margin-bottom:20px}.sectiontop h1{font-size:27px;line-height:1.15;margin:0 0 7px;letter-spacing:-.65px}.sectiontop p{margin:0;color:var(--muted);max-width:820px}.grid{display:grid;grid-template-columns:minmax(0,1.6fr) minmax(290px,1fr);gap:17px}.stack{display:grid;gap:17px}.card{background:var(--panel);border:1px solid var(--line);border-radius:15px;padding:19px;min-width:0}.card h2{font-size:16px;letter-spacing:-.2px;margin:0 0 14px}.card h3{font-size:13px;margin:0 0 6px}.muted{color:var(--muted)}.sm{font-size:12px}.overline{font-size:10px;text-transform:uppercase;letter-spacing:.95px;color:#90b0c5;font-weight:750}.row{display:flex;align-items:center;gap:9px;flex-wrap:wrap}.spread{justify-content:space-between}.hr{height:1px;background:var(--line);margin:16px 0}.engines{display:grid;grid-template-columns:minmax(0,1fr) 124px minmax(0,1fr);align-items:center;gap:10px;margin:15px 0 10px}.engine{border:1px solid #365569;border-radius:12px;background:#0b1b2b;padding:15px;min-height:155px}.engine.active{border-color:var(--teal);box-shadow:inset 0 0 0 1px #49d8c533;background:#102f3a}.engine small{color:var(--muted);display:block}.engine .name{font-weight:800;font-size:16px}.engine .rpm{font-size:29px;font-weight:800;letter-spacing:-.8px;margin-top:9px}.engine .unit{font-size:12px;color:#9ebdca}.route{text-align:center;color:var(--muted);font-size:11px}.route svg{display:block;width:100%;height:35px;margin:5px 0}.route strong{display:block;color:#cfe4eb;font-size:12px}.info{background:#122f3f;border:1px solid #28516a;padding:12px 13px;border-radius:10px;color:#b9d4e0;font-size:12px}.notice{border-left:3px solid #f2c57d;background:#2a2c30;padding:10px 13px;border-radius:6px;font-size:12px;color:#f1dcb4;margin-top:12px}.actions{display:flex;gap:8px;flex-wrap:wrap;margin:12px 0}.badge{font-size:11px;border-radius:7px;background:#16443e;color:#a1f0d9;padding:5px 8px;border:1px solid #2b6c63;font-weight:750}.badge.neutral{background:#273e53;color:#bdd7e6;border-color:#426378}.metrics{display:grid;grid-template-columns:repeat(3,1fr);gap:9px;margin:14px 0}.metric{border:1px solid #2e4b5f;border-radius:10px;background:#0b1c2c;padding:10px}.metric b{display:block;font-size:22px;line-height:1.3}.metric small{color:#9cb8c8;font-size:11px}.check{display:flex;gap:10px;align-items:start;margin:13px 0;color:#c4d6df;font-size:12px}.check input{margin-top:3px;accent-color:var(--teal)}.framebox{margin-top:17px}.framehead{display:flex;justify-content:space-between;align-items:start;gap:12px;margin-bottom:13px}.stepcontrols{display:flex;align-items:center;gap:7px;flex-wrap:wrap}.stepcontrols button{padding:7px 10px}.stepcontrols select{padding:7px 8px}.selector{font-size:12px;color:#abc6d5}.dataflow{display:grid;grid-template-columns:1fr 1fr;gap:11px}.data{border:1px solid #315068;background:#0b1c2c;border-radius:11px;padding:16px;min-height:158px}.data pre{white-space:pre-wrap;word-wrap:break-word;font:600 16px/1.8 ui-monospace,Consolas,monospace;color:#7df2da;margin:10px 0 0}.data .decval{font-size:27px;font-weight:800;margin:6px 0;color:#f1f8fc}.data small{color:#a9c1cf}.data .decodedrow{display:flex;justify-content:space-between;border-bottom:1px solid #243d4c;padding:6px 0}.data .decodedrow strong{color:#e1f8ef}.preview{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:10px}.preview .info{padding:11px}.preview strong{color:#dbf8f2}.control{background:#091b2b;border:1px solid #37596d;border-radius:8px;color:#f1f9fc;padding:9px;max-width:100%}input[type=range]{width:100%;accent-color:#41d1c0}.formgrid{display:grid;grid-template-columns:1fr 1fr;gap:16px}.controlblock{margin:8px 0 14px}.controlblock label{display:flex;justify-content:space-between;color:#afc9d7;font-size:12px;margin-bottom:5px}.controlblock strong{color:#61dfd0}details{border:1px solid #2a4a5d;border-radius:10px;padding:11px 14px;margin-top:14px}summary{cursor:pointer;color:#bdd5e1;font-weight:700}details .formgrid{margin-top:15px}.keyvalues{display:grid;grid-template-columns:1fr auto;gap:8px 10px;font-size:12px}.keyvalues span,.keyvalues strong{border-bottom:1px solid #254252;padding-bottom:8px}.keyvalues span{color:#a5c1d2}.keyvalues strong{text-align:right;color:#e0f5f5}.ledarea{display:flex;gap:20px;align-items:center;background:#0b1d2c;border:1px solid #2b475b;border-radius:12px;padding:22px;margin-bottom:16px}.leddisc{width:74px;height:74px;border-radius:50%;flex:none;border:3px solid #2d4b60;background:#0e2331}.ledcontrols{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1fr);gap:14px}.buttons{display:flex;gap:7px;flex-wrap:wrap;margin-top:10px}.pinmap{width:100%;border-collapse:collapse;font-size:12px}th,td{padding:10px 8px;border-bottom:1px solid #254354;text-align:left}td:last-child{text-align:right;color:#7fe8d4;font-family:ui-monospace,Consolas,monospace}th{color:#a8c2d2}.footer{padding:22px 0;color:#7d9aab;text-align:center;font-size:11px}.toast{position:fixed;bottom:20px;right:20px;background:#123f42;border:1px solid #45afa1;color:#ddfff5;padding:12px 18px;border-radius:10px;box-shadow:0 12px 35px #0006;display:none;max-width:min(95vw,400px);z-index:5}button:focus-visible,select:focus-visible,input:focus-visible,.tab:focus-visible{outline:2px solid #a4f0e3;outline-offset:2px}
@media(max-width:900px){.grid{grid-template-columns:1fr}.engines{grid-template-columns:1fr 90px 1fr}.metrics{grid-template-columns:repeat(3,1fr)}}
@media(max-width:600px){.shell{padding:17px 12px}.headright{width:100%}.sectiontop h1{font-size:24px}.engines{grid-template-columns:1fr 45px 1fr;gap:5px}.engine{padding:10px;min-height:137px}.engine .name{font-size:13px}.engine .rpm{font-size:23px}.route strong{font-size:10px}.dataflow,.preview,.formgrid,.ledcontrols{grid-template-columns:1fr}.framehead{flex-direction:column}.tab{padding:10px 12px}.card{padding:14px}.metrics b{font-size:18px}}
@media(prefers-reduced-motion:reduce){*{animation:none!important;transition:none!important}}
</style>
</head>
<body>
<div class="shell">
 <header class="header"><div class="brand"><div class="brandmark">K</div><div><b>KAELOR / EngineLink</b><small>LAB · ESP32-S3 · v1.1</small></div></div><div class="headright"><span class="pill"><span class="ball" id="conndot"></span><span id="connection">Forbinder…</span></span><span class="pill" id="uptime">--:--:--</span></div></header>
 <nav class="tabs" aria-label="Dashboard"><button class="tab" role="tab" aria-selected="true" data-view="overview" onclick="switchTab('overview')">Overblik</button><button class="tab" role="tab" aria-selected="false" data-view="control" onclick="switchTab('control')">LED &amp; SD-kort</button><button class="tab" role="tab" aria-selected="false" data-view="hardware" onclick="switchTab('hardware')">Hardware</button></nav>

 <main id="overview" class="view active">
  <div class="sectiontop"><h1>Se én datapakke ad gangen</h1><p>Her kan du følge, hvordan simulerede motordata går fra J1708-råbytes til RPM og en NMEA2000-forhåndsvisning. Trafikken kører i baggrunden; skærmen skifter i et roligt tempo.</p></div>
  <div class="grid">
   <section class="card"><div class="row spread"><h2 style="margin:0">De to J1708-kanaler</h2><span id="runbadge" class="badge neutral">STOPPET</span></div>
    <div class="engines"><div class="engine" id="engine1"><small>Kanal 1 · J7</small><div class="name">Motor A (demo)</div><div class="rpm" id="rpm1">—</div><span class="unit">RPM · MID 128</span><small>TX47 · RX21</small></div>
     <div class="route"><span class="overline">Bus A / B</span><svg viewBox="0 0 140 35" aria-hidden="true"><path d="M6 17 H134" stroke="#54798c" stroke-width="2"/><path id="directionarrow" d="M125 10 L134 17 L125 24" fill="none" stroke="#43cbb8" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"/></svg><strong id="directionlabel">J7 → J6</strong><span>9600 baud</span></div>
     <div class="engine" id="engine2"><small>Kanal 2 · J6</small><div class="name">Motor B (demo)</div><div class="rpm" id="rpm2">—</div><span class="unit">RPM · MID 175</span><small>TX18 · RX17</small></div></div>
    <div class="info" id="flowhint">Når en pakke vælges, fremhæves dens afsender. Ingen flyvende datapakker.</div>
    <div class="metrics"><div class="metric"><small>Godkendt J7 → J6</small><b id="j7ok">0</b></div><div class="metric"><small>Godkendt J6 → J7</small><b id="j6ok">0</b></div><div class="metric"><small>Registrerede fejl</small><b id="err">0</b></div></div>
   </section>
   <section class="card"><h2>Start og stop</h2><div class="actions"><button class="primary" onclick="start('demo')">Start softwaredemo</button><button onclick="stepDemo()">Send én demo-pakke</button><button class="danger" onclick="stopRun()">Stop</button></div><p class="muted sm">Softwaredemoen bruger ingen fysisk J1708-transmission. Begynd her.</p><div class="hr"></div><div class="overline">Fysisk bænk-test</div><label class="check"><input type="checkbox" id="verified"><span>Begge isolerede 5 V målt korrekt, motorerne er frakoblet, SJ1/SJ2 er lukkede, og J7 er forbundet til J6 (B–B, A–A).</span></label><button onclick="start('physical')">Start fysisk J1708</button><div class="notice">Fysisk TX må kun aktiveres på testbænken. Åbn SJ1/SJ2, før du tilslutter motorerne.</div></section>
  </div>
  <section class="card framebox"><div class="framehead"><div><h2 style="margin:0 0 5px">Rå data → afkodning</h2><div class="selector" id="selectedInfo">Start en demo, eller send én pakke manuelt.</div></div><div class="stepcontrols"><button id="pauseBtn" onclick="togglePause()">Pause visning</button><button aria-label="Forrige pakke" onclick="stepHistory(-1)">‹</button><button aria-label="Næste pakke" onclick="stepHistory(1)">›</button><button onclick="jumpLive()">Seneste</button><select id="viewspeed" class="control" aria-label="Tid per vist pakke" onchange="setViewSpeed()"><option value="3000">3 sek./pakke</option><option value="5000" selected>5 sek./pakke</option><option value="8000">8 sek./pakke</option><option value="0">Kun manuelt</option></select></div></div>
   <div class="dataflow"><div class="data"><div class="overline">Trin 1 · J1708 råbytes</div><pre id="raw">—</pre><p class="muted sm" id="rawdetail">Afventer datapakke</p></div><div class="data"><div class="overline">Trin 2 · J1587-afkodning (demo)</div><div class="decval" id="decodedrpm">— RPM</div><div class="decodedrow"><span>Motorbelastning</span><strong id="decodedload">—</strong></div><div class="decodedrow"><span>Checksum</span><strong id="checksum">—</strong></div><div class="decodedrow"><span>Resultat</span><strong id="packetResult">—</strong></div></div></div>
   <div class="preview"><div class="info"><span class="overline">Trin 3 · NMEA2000-forhåndsvisning</span><div><strong>PGN 127488</strong> · Engine Parameters, Rapid Update</div><div id="nmeaValue">Motorhastighed: —</div></div><div class="info">Dette viser kun et beregnet eksempel. <strong>CAN/NMEA2000 sender ikke noget.</strong> En faktisk PGN-pakke opbygges ikke i denne version.</div></div>
  </section>
  <details><summary>Justér de simulerede motorer og kommunikationsinterval</summary><div class="formgrid"><div><div class="controlblock"><label>Motor A – grundomdrejninger <strong id="rpm1val">825</strong></label><input id="setrpm1" type="range" min="0" max="3000" step="25" value="825" oninput="showSettings()"></div><div class="controlblock"><label>Motor A – belastning <strong id="load1val">36%</strong></label><input id="setload1" type="range" min="0" max="100" value="36" oninput="showSettings()"></div></div><div><div class="controlblock"><label>Motor B – grundomdrejninger <strong id="rpm2val">890</strong></label><input id="setrpm2" type="range" min="0" max="3000" step="25" value="890" oninput="showSettings()"></div><div class="controlblock"><label>Motor B – belastning <strong id="load2val">42%</strong></label><input id="setload2" type="range" min="0" max="100" value="42" oninput="showSettings()"></div></div></div><div class="row"><label for="direction">Sender:</label><select class="control" id="direction"><option value="0">Skift J7/J6 automatisk</option><option value="1">Kun J7</option><option value="2">Kun J6</option></select><label for="interval">Fysisk sendeinterval:</label><select class="control" id="interval"><option value="1200">1,2 sek.</option><option value="1800">1,8 sek.</option><option value="3000">3 sek.</option><option value="5000">5 sek.</option></select><button class="primary" onclick="setConfig()">Gem indstillinger</button></div><p class="muted sm">Sendeintervallet ændrer selve trafikken. Afspilningshastigheden ovenfor ændrer kun, hvor ofte visningen skifter.</p></details>
 </main>

 <main id="control" class="view"><div class="sectiontop"><h1>LED &amp; SD-kort</h1><p>Styr den fysiske RGB-status-LED, test microSD-kortet og gem J1708-hændelser i en CSV-fil.</p></div><div class="grid"><section class="card"><h2>Status-LED · SK6805 på GPIO48</h2><div class="ledarea"><div class="leddisc" id="ledball"></div><div><div class="overline">Aktuel tilstand</div><strong id="ledstate">Slukket</strong><p class="muted sm" style="margin-bottom:0">GPIO48 → level shifter → 330 Ω → DIN</p></div></div><div class="ledcontrols"><div><label class="overline" for="ledmode">Lyseffekt</label><select id="ledmode" class="control" style="width:100%"><option value="off">Slukket</option><option value="solid">Konstant</option><option value="rainbow">Rainbow</option><option value="breath">Breathing</option><option value="heart">Heartbeat</option><option value="traffic">Blink ved J1708-pakke</option></select></div><div><label class="overline" for="color">Farve</label><div class="row"><input id="color" type="color" value="#007bff" style="width:70px;height:39px;border:0;background:transparent"><button onclick="setLed()">Anvend</button></div></div></div><div class="controlblock" style="margin-top:15px"><label>Lysstyrke <strong id="brightval">40</strong> / 255</label><input type="range" min="1" max="180" id="bright" value="40" oninput="text('brightval',this.value)" onchange="setLed()"></div><div class="buttons"><button onclick="quickLed('#ff2020')">Rød</button><button onclick="quickLed('#16ff72')">Grøn</button><button onclick="quickLed('#276eff')">Blå</button><button onclick="quickLed('#ffffff')">Hvid</button><button onclick="quickLed('#000000',true)">Sluk</button></div></section><section class="card"><h2>microSD · SPI GPIO10–14</h2><div class="keyvalues"><span>Kort registreret</span><strong id="card">—</strong><span>SD monteret</span><strong id="mounted">—</strong><span>Loggede rækker</span><strong id="rows">—</strong><span>Status</span><strong id="sdmsg">—</strong></div><div class="actions" style="margin-top:20px"><button onclick="sendCmd('/api/sd/mount')">Montér SD</button><button onclick="sendCmd('/api/sd/test')">Skriv/læs testfil</button></div><div class="actions"><button class="primary" onclick="setLog(true)">Start CSV-log</button><button onclick="setLog(false)">Stop log</button><a href="/download" style="color:#6aead6;padding:9px">Hent CSV</a></div><div class="notice">Stop logningen, inden SD-kortet fjernes. Testfilen /KLRCHK.TXT kan blive overskrevet. CSV-loggen ligger i /KLRLAB.CSV.</div></section></div></main>

 <main id="hardware" class="view"><div class="sectiontop"><h1>Hardware &amp; pinout</h1><p>Diagnostik af det monterede print. Software kan ikke måle de isolerede forsyninger; brug multimeter ved PS1 og PS3.</p></div><div class="grid"><section class="card"><h2>Boardstatus</h2><div class="keyvalues"><span>Flash</span><strong id="flash">—</strong><span>PSRAM</span><strong id="psram">—</strong><span>Ledig heap</span><strong id="heap">—</strong><span>Isoleret indgang GPIO38</span><strong id="opto">—</strong><span>J1708 RX i tomgang</span><strong id="idle">—</strong><span>Wi-Fi-klienter</span><strong id="clients">—</strong><span>NMEA2000 / ISO1042</span><strong>Ikke aktiv – TX OFF</strong></div><div class="actions"><button onclick="sendCmd('/api/psram/test')">Test PSRAM</button><button onclick="sendCmd('/api/reset')">Nulstil J1708-tællere</button></div><div class="info">At RX viser HIGH, beviser ikke, at de isolerede spændinger ligger korrekt. Ingen spændingsmåling er indbygget.</div></section><section class="card"><h2>Pinoversigt fra diagrammet</h2><table class="pinmap"><thead><tr><th>Hardware</th><th>GPIO</th></tr></thead><tbody><tr><td>J1708 / J7</td><td>RX21 · TX47</td></tr><tr><td>J1708 / J6</td><td>RX17 · TX18</td></tr><tr><td>SD-kort (SPI + detect)</td><td>CS10 · MOSI11 · SCK12 · MISO13 · DET14</td></tr><tr><td>Status-LED</td><td>GPIO48</td></tr><tr><td>Isoleret input</td><td>GPIO38</td></tr><tr><td>NMEA2000 (inaktiv)</td><td>TX15 · RX16</td></tr><tr><td>I²C reserveret</td><td>SDA8 · SCL9</td></tr><tr><td>Native USB</td><td>D−19 · D+20</td></tr></tbody></table></section></div></main>
 <footer class="footer">KAELOR EngineLink Lab v1.1 · Demonstrationsdata er syntetiske · NMEA2000-transmission er deaktiveret</footer>
</div><div id="toast" class="toast" role="status" aria-live="polite"></div>
<script>
const $=id=>document.getElementById(id);
const history=[];let selectedId=null,paused=false,speedMs=5000,lastAdvance=0,first=true,polling=false,latestStatus=null;
function text(id,value){$(id).textContent=value}
function notify(message,error=false){const el=$('toast');el.textContent=message;el.style.display='block';el.style.borderColor=error?'#b95767':'#45afa1';clearTimeout(window._toast);window._toast=setTimeout(()=>el.style.display='none',4300)}
function switchTab(view){document.querySelectorAll('.view').forEach(el=>el.classList.toggle('active',el.id===view));document.querySelectorAll('.tab').forEach(el=>el.setAttribute('aria-selected',String(el.dataset.view===view)))}
async function post(path,body={}){try{let r=await fetch(path,{method:'POST',body:new URLSearchParams(body),cache:'no-store'});let data=await r.json();notify(data.message||'Udført',!r.ok||data.ok===false);await poll();return data}catch(e){notify('Ingen forbindelse til EngineLink',true)}}
function start(mode){if(mode==='physical'&&!$('verified').checked){notify('Mål de to isolerede forsyninger og bekræft afkobling fra motorerne først.',true);return}post('/api/run',{mode,verified:$('verified').checked?'1':'0'})}
function stopRun(){post('/api/run',{mode:'stop'})}
function stepDemo(){post('/api/demo/step')}
function sendCmd(path){post(path)}
function setLog(on){post('/api/sd/log',{on:on?'1':'0'})}
function setConfig(){post('/api/config',{direction:$('direction').value,interval:$('interval').value,rpm1:$('setrpm1').value,rpm2:$('setrpm2').value,load1:$('setload1').value,load2:$('setload2').value})}
function showSettings(){for(const k of ['rpm1','rpm2'])text(k+'val',$('set'+k).value);for(const k of ['load1','load2'])text(k+'val',$('set'+k).value+'%')}
function setLed(){post('/api/led',{mode:$('ledmode').value,color:$('color').value.replace('#',''),bright:$('bright').value})}
function quickLed(color,off=false){$('ledmode').value=off?'off':'solid';$('color').value=color;setLed()}
function setViewSpeed(){speedMs=Number($('viewspeed').value);paused=speedMs===0;lastAdvance=Date.now();updatePauseLabel();if(!paused)jumpLive()}
function updatePauseLabel(){text('pauseBtn',paused?'Fortsæt visning':'Pause visning')}
function togglePause(){paused=!paused;if(!paused&&speedMs===0){$('viewspeed').value='5000';speedMs=5000}lastAdvance=Date.now();updatePauseLabel()}
function mergeEvents(incoming){for(const e of incoming){if(!history.some(x=>x.id===e.id))history.push(e)}history.sort((a,b)=>a.id-b.id);if(history.length>120)history.splice(0,history.length-120)}
function stepHistory(direction){if(!history.length)return;const current=history.findIndex(x=>x.id===selectedId);const idx=current<0?history.length-1:Math.max(0,Math.min(history.length-1,current+direction));selectedId=history[idx].id;paused=true;lastAdvance=Date.now();updatePauseLabel();renderSelected()}
function jumpLive(){if(!history.length)return;selectedId=history[history.length-1].id;paused=speedMs===0;lastAdvance=Date.now();updatePauseLabel();renderSelected()}
function renderSelected(){const e=history.find(x=>x.id===selectedId);if(!e)return;text('selectedInfo','Pakke #'+e.id+' · '+(e.physical?'Fysisk UART':'Softwaredemo')+' · '+(e.ms/1000).toFixed(1)+' s · '+(paused?'Visning på pause':'Rolig afspilning'));
 text('directionlabel',e.from===1?'J7 → J6':'J6 → J7');const arrow=$('directionarrow');arrow.setAttribute('d',e.from===1?'M125 10 L134 17 L125 24':'M15 10 L6 17 L15 24');$('engine1').classList.toggle('active',e.from===1);$('engine2').classList.toggle('active',e.from===2);text('flowhint',e.from===1?'Motor A simulerer en J1587-meddelelse. J7 sender til J6.':'Motor B simulerer en J1587-meddelelse. J6 sender til J7.');text('rpm'+e.from,e.rpm);text('raw',e.hex||'Ingen bytes modtaget');text('rawdetail','MID '+(e.from===1?'128':'175')+' · PID 190 (RPM) · PID 92 (belastning) · 7 bytes');text('decodedrpm',e.ok?e.rpm+' RPM':'Afkodning fejlede');text('decodedload',e.ok?e.load+' %':'—');text('checksum',e.ok?'OK':'FEJL');text('packetResult',e.ok?'PASS':'FEJL');text('nmeaValue',e.ok?'Eksempel: RPM '+e.rpm+' → rå PGN-værdi '+(e.rpm*8)+' (0,125 RPM/trin)':'Ingen gyldig RPM til forhåndsvisning')}
function updateEvents(incoming){mergeEvents(incoming||[]);if(!history.length)return;const now=Date.now();if(selectedId===null){selectedId=history[history.length-1].id;lastAdvance=now;renderSelected()}else if(!paused&&speedMs>0&&now-lastAdvance>=speedMs&&history[history.length-1].id>selectedId){selectedId=history[history.length-1].id;lastAdvance=now;renderSelected()}}
async function poll(){if(polling)return;polling=true;try{const res=await fetch('/api/status',{cache:'no-store'});if(!res.ok)throw new Error('HTTP '+res.status);const s=await res.json();latestStatus=s;text('connection','Forbundet · '+s.clients+' klient(er)');$('conndot').style.background='#43d6ac';text('uptime',s.uptime);text('runbadge',s.mode==='physical'?'FYSISK TEST':s.mode==='demo'?'SOFTWAREDEMO':'STOPPET');text('j7ok',s.ok[0]);text('j6ok',s.ok[1]);text('err',s.errors[0]+s.errors[1]);text('card',s.card?'Indsat':'Intet kort');text('mounted',s.sdMounted?'Ja':'Nej');text('rows',s.rows+(s.logging?' · AKTIV LOG':' '));text('sdmsg',s.sdMsg);text('psram',s.psram?(s.psram/1048576).toFixed(1)+' MB':'Ikke fundet');text('flash',(s.flash/1048576).toFixed(1)+' MB');text('heap',(s.heap/1024).toFixed(0)+' KB');text('opto',s.opto===0?'LOW (aktiv)':'HIGH (inaktiv)');text('idle',(s.idle[0]?'HIGH':'LOW')+' / '+(s.idle[1]?'HIGH':'LOW'));text('clients',s.clients);text('ledstate',s.ledMode);let led=$('ledball');led.style.background=s.ledColor;led.style.boxShadow=s.ledMode==='off'?'none':'0 0 24px '+s.ledColor+'99';
 if(first){$('direction').value=s.direction;$('interval').value=String(s.interval);$('setrpm1').value=s.rpmTarget[0];$('setrpm2').value=s.rpmTarget[1];$('setload1').value=s.loadTarget[0];$('setload2').value=s.loadTarget[1];$('ledmode').value=s.ledMode;$('color').value='#'+s.ledHex;$('bright').value=s.brightness;text('brightval',s.brightness);showSettings();first=false}updateEvents(s.events)}catch(err){text('connection','Forbindelsen er afbrudt');$('conndot').style.background='#ff8192'}finally{polling=false}}
updatePauseLabel();poll();setInterval(poll,1100);
</script>
</body></html>

)ENGINELAB";

const char* ledModeName() {
  switch (ledMode) {
    case LED_SOLID: return "solid";
    case LED_RAINBOW: return "rainbow";
    case LED_BREATH: return "breath";
    case LED_HEART: return "heart";
    case LED_TRAFFIC: return "traffic";
    default: return "off";
  }
}

void setPixel(uint32_t color) {
  if (color == ledPrev) return;
  led.setPixelColor(0, color);
  led.show();
  ledPrev = color;
}

void updateLED() {
  if (millis() - lastLedMs < 35) return;
  lastLedMs = millis();
  const uint32_t t = millis();
  uint32_t color = 0;
  switch (ledMode) {
    case LED_SOLID: color = ledRGB; break;
    case LED_RAINBOW: color = led.ColorHSV((uint16_t)((t * 19UL) & 65535), 255, 255); break;
    case LED_BREATH: {
      float v = (sinf(t * 0.002618f - 1.570796f) + 1.0f) * 0.5f;
      uint8_t level = (uint8_t)(v * 255.0f);
      color = led.Color((uint8_t)(((ledRGB >> 16) & 255) * level / 255),
                        (uint8_t)(((ledRGB >> 8) & 255) * level / 255),
                        (uint8_t)((ledRGB & 255) * level / 255));
      break;
    }
    case LED_HEART: {
      uint32_t x = t % 1500;
      if (x < 90 || (x >= 220 && x < 300)) color = ledRGB;
      break;
    }
    case LED_TRAFFIC:
      if (t - lastPulseMs < 160) color = pulseFailed ? 0xFF1122 : 0x19DD79;
      break;
    default: break;
  }
  // Convert from 0xRRGGBB to actual pixel colour, NeoPixel applies brightness.
  setPixel(led.Color((color >> 16) & 255, (color >> 8) & 255, color & 255));
}

bool cardPresent() { return digitalRead(PIN_SD_DETECT) == LOW; }
void stopLogging() { if (sdLogging) { csv.flush(); csv.close(); sdLogging = false; } }

bool mountSD() {
  if (!cardPresent()) { sdMessage = "Kort mangler"; return false; }
  if (sdMounted) return true;
  SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  sdMounted = SD.begin(PIN_SD_CS, SPI, 4000000);
  sdMessage = sdMounted ? "SD monteret ved 4 MHz" : "SD.begin fejlede";
  return sdMounted;
}

bool testSD() {
  if (sdLogging) { sdMessage = "Stop CSV-logging før test"; return false; }
  if (!mountSD()) return false;
  const char test[] = "KAELOR EngineLink Lab SD readback test 2026";
  File f = SD.open(TEST_FILE, FILE_WRITE);
  if (!f) { sdMessage = "Kan ikke oprette testfil"; return false; }
  f.print(test);
  f.close();
  f = SD.open(TEST_FILE, FILE_READ);
  if (!f) { sdMessage = "Kan ikke læse testfil"; return false; }
  char buffer[sizeof(test)] = {};
  size_t n = f.readBytes(buffer, sizeof(test) - 1);
  f.close();
  bool ok = n == sizeof(test) - 1 && memcmp(buffer, test, sizeof(test) - 1) == 0;
  sdMessage = ok ? "WRITE + READ verificeret" : "Mismatch i testfil";
  return ok;
}

void appendLog(const Event &e) {
  if (!sdLogging) return;
  if (!cardPresent()) { sdLogging = false; sdMessage = "Kort fjernet - log stoppet"; return; }
  int n = csv.printf("%lu,%s,%s,%s,%u,%u,%u,%s,%s\n",
    (unsigned long)e.ms, e.physical ? "PHYSICAL" : "DEMO",
    e.from == 1 ? "J7" : "J6", e.from == 1 ? "J6" : "J7",
    (unsigned)e.rpm, (unsigned)e.load, (unsigned)e.success,
    e.hex, e.comment);
  if (n <= 0) { sdMessage = "SD skrivefejl - log stoppet"; stopLogging(); return; }
  ++logRows;
  if (logRows % 10 == 0) csv.flush();
}

void logEvent(uint8_t dir, const uint8_t* bytes, uint8_t len, bool success, bool physical,
              uint16_t rpm, uint8_t load, const char* comment) {
  Event &e = events[eventWrite];
  e.id = ++eventId; e.ms = millis();
  e.from = dir + 1; e.to = dir == 0 ? 2 : 1;
  e.len = len; e.success = success; e.physical = physical;
  e.rpm = rpm; e.load = load;
  if (success) { rpmLive[dir] = rpm; loadLive[dir] = load; }
  size_t pos = 0;
  for (int i = 0; i < len && i < FRAME_LEN && pos + 4 < sizeof(e.hex); i++) {
    pos += snprintf(e.hex + pos, sizeof(e.hex) - pos, "%02X%s", bytes[i], i + 1 < len ? " " : "");
  }
  e.hex[pos] = 0;
  snprintf(e.comment, sizeof(e.comment), "%s", comment);
  eventWrite = (eventWrite + 1) % EVENT_CAP;
  if (eventSize < EVENT_CAP) ++eventSize;
  appendLog(e);
  lastPulseMs = millis(); pulseFailed = !success;
}

uint8_t makeFrame(uint8_t *data, uint8_t dir, uint16_t &rpm, uint8_t &load) {
  // J1587 DEMO: MID=128 engine #1 or MID=175 engine #2;
  // PID 190 = engine speed / 0.25 RPM (uint16 LE), PID 92 = load / 0.5%.
  // This is illustrative payload, NOT a genuine motor ECU transmission.
  float wave = sinf(millis() * 0.0013f + (dir ? 1.9f : 0));
  int r = (int)rpmTarget[dir] + (int)(wave * 68.0f);
  rpm = (uint16_t)constrain(r, 0, 3000);
  load = (uint8_t)constrain((int)loadTarget[dir] + (int)(wave * 5), 0, 100);
  uint16_t raw = rpm * 4;
  data[0] = dir == 0 ? 128 : 175;
  data[1] = 190;
  data[2] = raw & 255;
  data[3] = raw >> 8;
  data[4] = 92;
  data[5] = load * 2;
  uint8_t sum = 0;
  for (uint8_t i = 0; i < FRAME_LEN - 1; ++i) sum = (uint8_t)(sum + data[i]);
  data[6] = (uint8_t)(0U - sum);
  return FRAME_LEN;
}

bool decodeFrame(const uint8_t *data, uint8_t n, uint16_t &rpm, uint8_t &load) {
  if (n != FRAME_LEN) return false;
  uint8_t sum = 0;
  for (uint8_t i = 0; i < n; ++i) sum = (uint8_t)(sum + data[i]);
  if (sum != 0) return false;
  if ((data[0] != 128 && data[0] != 175) || data[1] != 190 || data[4] != 92) return false;
  uint16_t raw = (uint16_t)data[2] | ((uint16_t)data[3] << 8);
  rpm = raw / 4;
  load = data[5] / 2;
  return true;
}

bool quietBus() {
  for (uint8_t i = 0; i < 8; ++i) {
    if (digitalRead(PIN_J7_RX) != HIGH || digitalRead(PIN_J6_RX) != HIGH) return false;
    delayMicroseconds(160);
  }
  return true;
}

void emptyUART(HardwareSerial &port) { while (port.available() > 0) (void)port.read(); }

void stopRun() {
  runMode = STOPPED;
  awaiting = false;
}

void finishPhysical(bool timedOut) {
  uint8_t dir = activeDirection;
  uint16_t rpm = 0; uint8_t load = 0;
  bool check = decodeFrame(received, receivedLen, rpm, load);
  bool match = receivedLen == FRAME_LEN && memcmp(expected, received, FRAME_LEN) == 0;
  bool good = check && match && !timedOut;
  if (good) ++rxOk[dir];
  else {
    ++rxErrors[dir];
    if (timedOut) ++timeouts[dir];
    if (receivedLen == FRAME_LEN && !check) ++badChecksums[dir];
    lastError = timedOut ? "Timeout / manglende RX" : "Mismatch / checksum";
  }
  logEvent(dir, received, receivedLen, good, true,
           good ? rpm : expectedRPM, good ? load : expectedLoad,
           good ? "PASS" : (timedOut ? "TIMEOUT" : "MISMATCH"));
  awaiting = false;
}

void servicePhysicalRx() {
  if (!awaiting) return;
  HardwareSerial &destination = activeDirection == 0 ? j6 : j7;
  while (destination.available() > 0 && receivedLen < FRAME_LEN) {
    int byteValue = destination.read();
    if (byteValue >= 0) received[receivedLen++] = (uint8_t)byteValue;
  }
  if (receivedLen == FRAME_LEN) {
    // Any unexpected extra bytes are recorded separately.
    if (destination.available()) {
      while (destination.available()) { (void)destination.read(); ++rxExtra[activeDirection]; }
    }
    finishPhysical(false);
  } else if (millis() - txStartedMs > RX_TIMEOUT_MS) {
    finishPhysical(true);
  }
}

void sendDemoOrPhysical() {
  if (runMode == STOPPED || awaiting) return;
  if (millis() - lastSendMs < busIntervalMs) return;
  lastSendMs = millis();
  const uint8_t dir = senderSelection == 0 ? nextSender : (senderSelection - 1);
  if (senderSelection == 0) nextSender ^= 1;
  uint8_t data[FRAME_LEN];
  uint16_t rpm = 0; uint8_t load = 0;
  makeFrame(data, dir, rpm, load);
  frameSeq++;

  if (runMode == DEMO) {
    uint16_t decodedRPM = 0; uint8_t decodedLoad = 0;
    bool ok = decodeFrame(data, FRAME_LEN, decodedRPM, decodedLoad);
    if (ok) ++rxOk[dir]; else ++rxErrors[dir];
    logEvent(dir, data, FRAME_LEN, ok, false, decodedRPM, decodedLoad, ok ? "PASS" : "FORMAT");
    return;
  }

  if (!quietBus()) {
    ++idleFaults; lastError = "J1708-bussen er ikke HIGH i tomgang";
    if (idleFaults >= 5) stopRun();
    return;
  }
  emptyUART(j7); emptyUART(j6);
  memcpy(expected, data, FRAME_LEN);
  expectedRPM = rpm; expectedLoad = load;
  activeDirection = dir;
  receivedLen = 0;
  HardwareSerial &source = dir == 0 ? j7 : j6;
  size_t written = source.write(data, FRAME_LEN);
  source.flush(); // 7 bytes at 9600 baud: approximately 7.3 ms.
  if (written != FRAME_LEN) {
    ++rxErrors[dir]; lastError = "UART TX skrev for få bytes";
    logEvent(dir, data, FRAME_LEN, false, true, rpm, load, "TXERROR");
    return;
  }
  awaiting = true;
  txStartedMs = millis();
}

String jsonBool(bool v) { return v ? "true" : "false"; }

void handleStatus() {
  String s; s.reserve(5100);
  uint32_t time = millis() / 1000;
  char uptime[24]; snprintf(uptime, sizeof(uptime), "%02lu:%02lu:%02lu",
       (unsigned long)(time / 3600), (unsigned long)((time / 60) % 60), (unsigned long)(time % 60));
  char ledHex[8]; snprintf(ledHex, sizeof(ledHex), "%06lX", (unsigned long)ledRGB);
  char ledColor[8]; snprintf(ledColor, sizeof(ledColor), "#%06lX", (unsigned long)ledRGB);
  s += "{\"mode\":\"";
  s += runMode == PHYSICAL ? "physical" : runMode == DEMO ? "demo" : "stopped";
  s += "\",\"uptime\":\""; s += uptime;
  s += "\",\"clients\":"; s += WiFi.softAPgetStationNum();
  s += ",\"ok\":["; s += rxOk[0]; s += ','; s += rxOk[1]; s += ']';
  s += ",\"errors\":["; s += rxErrors[0]; s += ','; s += rxErrors[1]; s += ']';
  s += ",\"rpmLive\":["; s += rpmLive[0]; s += ','; s += rpmLive[1]; s += ']';
  s += ",\"rpmTarget\":["; s += rpmTarget[0]; s += ','; s += rpmTarget[1]; s += ']';
  s += ",\"loadTarget\":["; s += loadTarget[0]; s += ','; s += loadTarget[1]; s += ']';
  s += ",\"direction\":"; s += senderSelection;
  s += ",\"interval\":"; s += busIntervalMs;
  s += ",\"card\":"; s += jsonBool(cardPresent());
  s += ",\"sdMounted\":"; s += jsonBool(sdMounted);
  s += ",\"logging\":"; s += jsonBool(sdLogging);
  s += ",\"rows\":"; s += logRows;
  s += ",\"sdMsg\":\""; s += sdMessage; s += '"';
  s += ",\"psram\":"; s += psramFound() ? ESP.getPsramSize() : 0;
  s += ",\"flash\":"; s += ESP.getFlashChipSize();
  s += ",\"heap\":"; s += ESP.getFreeHeap();
  s += ",\"opto\":"; s += digitalRead(PIN_OPTO);
  s += ",\"idle\":["; s += digitalRead(PIN_J7_RX) == HIGH ? "true" : "false";
  s += ','; s += digitalRead(PIN_J6_RX) == HIGH ? "true" : "false"; s += ']';
  s += ",\"ledMode\":\""; s += ledModeName(); s += '"';
  s += ",\"ledHex\":\""; s += ledHex; s += '"';
  s += ",\"ledColor\":\""; s += ledMode == LED_OFF ? "#0D1821" : ledColor; s += '"';
  s += ",\"brightness\":"; s += ledBrightness;
  s += ",\"events\":[";
  for (uint8_t i = 0; i < eventSize; ++i) {
    const Event &e = events[(eventWrite + EVENT_CAP - eventSize + i) % EVENT_CAP];
    if (i) s += ',';
    s += "{\"id\":"; s += e.id;
    s += ",\"ms\":"; s += e.ms;
    s += ",\"from\":"; s += e.from;
    s += ",\"ok\":"; s += jsonBool(e.success);
    s += ",\"physical\":"; s += jsonBool(e.physical);
    s += ",\"rpm\":"; s += e.rpm;
    s += ",\"load\":"; s += e.load;
    s += ",\"hex\":\""; s += e.hex;
    s += "\",\"comment\":\""; s += e.comment;
    s += "\"}";
  }
  s += "]}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", s);
}

void reply(bool ok, const String &msg, int code = 200) {
  // API messages here are fixed strings (no user content or quotes).
  server.send(code, "application/json", String("{\"ok\":") + jsonBool(ok) + ",\"message\":\"" + msg + "\"}");
}

void handleRun() {
  String mode = server.arg("mode");
  if (mode == "stop") { stopRun(); reply(true, "Stop. Ingen J1708-transmission."); return; }
  if (mode == "demo") { stopRun(); runMode = DEMO; lastSendMs = 0; reply(true, "Softwaredemo startet - ingen fysisk TX."); return; }
  if (mode == "physical") {
    if (server.arg("verified") != "1") { reply(false, "Kontroller isoleret 5 V og frakobl motorerne først.", 403); return; }
    if (!quietBus()) { reply(false, "En RX-indgang er LOW - kontroller begge isolerede 5 V.", 409); return; }
    stopRun(); runMode = PHYSICAL; idleFaults = 0; lastSendMs = 0;
    reply(true, "Fysisk J1708 bench-test startet."); return;
  }
  reply(false, "Ukendt driftstilstand", 400);
}

void handleDemoStep() {
  // One synthetic frame. This endpoint never sends UART data.
  if (runMode == PHYSICAL) {
    reply(false, "Stop fysisk J1708-test foerst.", 409);
    return;
  }
  const uint8_t dir = senderSelection == 0 ? nextSender : senderSelection - 1;
  if (senderSelection == 0) nextSender ^= 1;
  uint8_t data[FRAME_LEN];
  uint16_t rpm = 0; uint8_t load = 0;
  makeFrame(data, dir, rpm, load);
  uint16_t decodedRPM = 0; uint8_t decodedLoad = 0;
  bool ok = decodeFrame(data, FRAME_LEN, decodedRPM, decodedLoad);
  ++frameSeq;
  if (ok) ++rxOk[dir]; else ++rxErrors[dir];
  logEvent(dir, data, FRAME_LEN, ok, false, decodedRPM, decodedLoad,
           ok ? "PASS" : "FORMAT");
  reply(ok, ok ? "En syntetisk J1708-pakke genereret." : "Demo-afkodning fejlede.");
}

void handleConfig() {
  senderSelection = constrain((int)server.arg("direction").toInt(), 0, 2);
  busIntervalMs = constrain((int)server.arg("interval").toInt(), 1200, 5000);
  rpmTarget[0] = constrain((int)server.arg("rpm1").toInt(), 0, 3000);
  rpmTarget[1] = constrain((int)server.arg("rpm2").toInt(), 0, 3000);
  loadTarget[0] = constrain((int)server.arg("load1").toInt(), 0, 100);
  loadTarget[1] = constrain((int)server.arg("load2").toInt(), 0, 100);
  reply(true, "Motorindstillinger opdateret.");
}

void handleLed() {
  String m = server.arg("mode");
  if (m == "solid") ledMode = LED_SOLID;
  else if (m == "rainbow") ledMode = LED_RAINBOW;
  else if (m == "breath") ledMode = LED_BREATH;
  else if (m == "heart") ledMode = LED_HEART;
  else if (m == "traffic") ledMode = LED_TRAFFIC;
  else ledMode = LED_OFF;
  String color = server.arg("color");
  if (color.length() == 6) ledRGB = strtoul(color.c_str(), nullptr, 16) & 0xFFFFFF;
  ledBrightness = constrain((int)server.arg("bright").toInt(), 1, 180);
  led.setBrightness(ledBrightness);
  ledPrev = 0xFFFFFFFF;
  updateLED();
  reply(true, "Status-LED opdateret.");
}

void handleSDLog() {
  if (server.arg("on") == "0") { stopLogging(); reply(true, "CSV-logning stoppet."); return; }
  if (!mountSD()) { reply(false, sdMessage); return; }
  if (sdLogging) { reply(true, "CSV-logging kører allerede."); return; }
  bool fresh = !SD.exists(LOG_FILE);
  csv = SD.open(LOG_FILE, FILE_APPEND);
  if (!csv) { sdMessage = "Kunne ikke åbne CSV"; reply(false, sdMessage); return; }
  if (fresh) csv.println("millis,source,from,to,rpm,load_percent,pass,raw_hex,result");
  csv.flush();
  sdLogging = true; sdMessage = "CSV-logging aktiv";
  reply(true, "CSV-logging startet: /KLRLAB.CSV");
}

void handleDownload() {
  if (!sdMounted || !SD.exists(LOG_FILE)) { server.send(404, "text/plain", "Logfil findes ikke"); return; }
  if (sdLogging) csv.flush();
  File f = SD.open(LOG_FILE, FILE_READ);
  if (!f) { server.send(500, "text/plain", "Kunne ikke aabne logfil"); return; }
  server.sendHeader("Content-Disposition", "attachment; filename=KLRLAB.CSV");
  server.streamFile(f, "text/csv");
  f.close();
}

void handlePSRAMTest() {
  if (!psramFound()) { reply(false, "Ingen PSRAM - kontroller Arduino PSRAM-indstilling."); return; }
  constexpr size_t N = 64 * 1024;
  uint8_t *p = (uint8_t*)ps_malloc(N);
  if (!p) { reply(false, "Kunne ikke allokere 64 KB i PSRAM."); return; }
  for (size_t i = 0; i < N; ++i) p[i] = (uint8_t)(((i * 29u) ^ (i >> 3)) & 255);
  bool ok = true;
  for (size_t i = 0; i < N; ++i)
    if (p[i] != (uint8_t)(((i * 29u) ^ (i >> 3)) & 255)) { ok = false; break; }
  free(p);
  reply(ok, ok ? "64 KB PSRAM write/read PASS." : "PSRAM mismatch ved laesning!");
}

void setup() {
  // Force the addressable LED OFF before starting other peripherals.
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
  led.begin();
  led.setBrightness(ledBrightness);
  led.clear(); led.show(); ledPrev = 0;

  Serial.begin(115200);
  delay(5000); // Prior S3 tests needed time for USB CDC monitor to connect.
  Serial.println("\n=== KAELOR EngineLink Lab v1.1 ===");
  pinMode(PIN_SD_DETECT, INPUT); // PCB has external 10k pull-up
  pinMode(PIN_OPTO, INPUT);
  j7.begin(BUS_BAUD, SERIAL_8N1, PIN_J7_RX, PIN_J7_TX);
  j6.begin(BUS_BAUD, SERIAL_8N1, PIN_J6_RX, PIN_J6_TX);
  // CAN hardware (GPIO15/16) intentionally NOT started.

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  bool started = WiFi.softAP(AP_SSID, AP_PASS);
  if (!started) Serial.println("[ERROR] SoftAP kunne ikke starte");
  Serial.printf("[WIFI] SSID: %s | IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
  Serial.printf("[WIFI] Password: %s\n", AP_PASS);
  Serial.println("[SAFETY] BENCH ONLY. Engines disconnected. Verify both isolated 5 V rails.");
  Serial.println("[SAFETY] J1708 TX OFF until explicitly enabled on web page.");

  server.on("/", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "no-cache");
    server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
  });
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/run", HTTP_POST, handleRun);
  server.on("/api/config", HTTP_POST, handleConfig);
  server.on("/api/demo/step", HTTP_POST, handleDemoStep);
  server.on("/api/led", HTTP_POST, handleLed);
  server.on("/api/reset", HTTP_POST, []() {
    rxOk[0]=rxOk[1]=rxErrors[0]=rxErrors[1]=0;
    timeouts[0]=timeouts[1]=badChecksums[0]=badChecksums[1]=0;
    rxExtra[0]=rxExtra[1]=idleFaults=0;
    reply(true, "Statistik nulstillet.");
  });
  server.on("/api/sd/mount", HTTP_POST, []() {
    bool ok = mountSD(); reply(ok, sdMessage);
  });
  server.on("/api/sd/test", HTTP_POST, []() {
    bool ok = testSD(); reply(ok, sdMessage);
  });
  server.on("/api/sd/log", HTTP_POST, handleSDLog);
  server.on("/api/psram/test", HTTP_POST, handlePSRAMTest);
  server.on("/download", HTTP_GET, handleDownload);
  server.onNotFound([]() { server.send(404, "text/plain", "Unknown endpoint"); });
  server.begin();
  Serial.println("[READY] Open http://192.168.4.1 after joining EngineLink Wi-Fi.");
}

void loop() {
  server.handleClient();
  servicePhysicalRx();
  sendDemoOrPhysical();
  updateLED();
  // If SD card was removed, stop logical logging. Always remove with logging OFF.
  if (sdLogging && !cardPresent()) {
    sdLogging = false;
    sdMessage = "Kort fjernet under logging: status usikker";
  }
  delay(1); // Wi-Fi / FreeRTOS housekeeping
}