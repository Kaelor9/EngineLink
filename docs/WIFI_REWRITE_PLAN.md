# Wi-Fi 2.0 implementation plan

Status: **plan only, nothing built yet** (2026-11-05). Written for whoever
builds Wi-Fi 2.0, person or AI. It is meant to be followed without guessing:
every change names the file, the function and the test that proves it.

Read first: `HANDOVER.md` (section "Wi-Fi"), `docs/TODO.md` ("Wi-Fi 2.0",
the owner's agreed design), `docs/STATUS.md`. This plan implements the TODO
design; where it adds a detail the TODO leaves open, section 7 says so.

Everything here was checked against the source in commit `b5c08fa`
(firmware 0.16.5), the Arduino ESP32 core **3.3.12** (`libraries/WiFi/src/STA.cpp`,
`libraries/Network/src/NetworkClient.cpp`, `WiFiScan.cpp`, `AP.cpp`), and the
device reports #95, #126, #141–#146 plus the 0.16.3–0.16.5 USB reports
summarised in `HANDOVER.md`.

---

## 1. Decisions that are fixed (do not reopen)

From the owner (`docs/TODO.md`, "Wi-Fi 2.0", 2026-11-04) and `HANDOVER.md`:

| # | Decision | Source |
|---|---|---|
| D1 | Four states: Standalone, Router test, Router, Router lost. Never two networks, except during the ~15 s router test. | TODO |
| D2 | Standalone: `EngineLink` / saved password, 192.168.4.1, channel 6 (saved `apch` honoured, default 6), max 6 phones. The simplest, most robust part; it is the fallback. | TODO, STATUS |
| D3 | Router: station only, **Arduino's own reconnect**, fast and stable. | TODO |
| D4 | Router lost: keep retrying 2 min, then Standalone. | TODO |
| D5 | Boot with a router saved: try the router up to 3 min (boat power starts router and EngineLink together), else Standalone. | TODO |
| D6 | Back to the router automatically when no phone is on the own Wi-Fi; if one is, the app asks "Router is back, switch now?" (app change, needs owner's OK). | TODO |
| D7 | No settings for retry minutes or number of tries. | TODO |
| D8 | `enginelink.local` works in both modes (iPhone home-screen app is bound to the address it was saved from). | TODO |
| D9 | Power off/on 3 times within 10 s → start in Standalone whatever is saved. | TODO |
| D10 | WPA2 unless the scan shows the router is WPA3-only; decided at connect, no stored security mode, no switch counter. Keep `wifiwpa3.cpp`. | TODO, HANDOVER |
| D11 | Follow the router's channel as a station; never ask for router changes; own Wi-Fi fixed; remember the router's channel for the quick look. | TODO, HANDOVER |
| D12 | Upgrade keeps the saved router, code and all data. | TODO |
| D13 | Remove `netpolicy.h`, `linktune.cpp` (radio test, channel survey), quiet-channel hints, channel/security counters, always-0 report fields; move clock/NTP, mDNS and GitHub reports out of `wifi.ino`. Keep `wifiwpa3.cpp`, `netboost.cpp`, USB serial commands, SD Wi-Fi log. | TODO |
| D14 | Lessons that must become tests: web server listens on the network that is up now (0.16.3); TX power set after the interface start and checked (0.16.4); power save off; USB printing never blocks; Wi-Fi calls only from the main loop, never from the event callback; `wifi.ino` under ~300 lines. | TODO |
| D15 | Transmit power fixed at 19.5 dBm (`TX_LEVEL_FIXED=4`). | HANDOVER |
| D16 | Do not touch the app UI for Wi-Fi work unless the owner approves (D6 and the router-test result text are the two asks). | HANDOVER |
| D17 | Marine safety, truthful telemetry, offline first, no router changes required, privacy (router name hidden in reports), "EngineLink" only. | CLAUDE.md |
| D18 | Built in **one release** (0.17.0) after the short 0.16.5 router baseline test the owner does on the boat. | TODO |

Added by the owner on 2026-11-05 (answers to section 7):

| # | Decision | Source |
|---|---|---|
| D19 | Standalone runs the radio as `WIFI_AP_STA` with an idle station interface, so scans and looks never change the radio mode (P1 approved). One `constexpr` reverts it. | owner, 2026-11-05 |
| D20 | The app changes are approved: router-test result text and the "Router is back, switch now?" prompt (P2 approved; D6 fully in force). | owner, 2026-11-05 |
| D21 | **A phone joined to the own Wi-Fi is never cut off automatically.** No idle timer counts as "no phone" (P3 rejected). EngineLink returns to the router only when no phone is joined, or when the owner taps "Switch now" in the app. | owner, 2026-11-05 |
| D22 | Answering Apple's captive-portal probe (P4) is deferred; not built in 0.17.0. | owner, 2026-11-05 |

---

## 2. Audit of the current implementation (0.16.5)

### 2.1 What exists

| File | Lines | Role today | Fate |
|---|---|---|---|
| `wifi.ino` | 264 | trace ring, reason texts, clock helpers, TX power, event callback, `applyNetwork()`, async scan, `serviceNetwork()` (fallback, 10-min retry, NTP, mDNS, HTTP rebind, GitHub report scheduling) | rewritten |
| `EngineLink.ino` 280–380 | ~100 | ~60 Wi-Fi globals, most from 0.12–0.15 and unused since 0.16.2 | pruned |
| `netpolicy.h` + `tools/test_netpolicy.cpp` | 65 + 80 | back-off and rescue rules, **not called anywhere** since 0.16.2 (only `netpolicy::PROBE_EVERY_MS` in a global initialiser) | removed; its look rules move to `wifistate.h` |
| `routerguard.h` + `tools/test_routerguard.cpp` | 15 + 25 | 120 s guard; `routerGuard.reset()` is still called in `webapi.ino` but nothing reads it | removed |
| `linktune.cpp/.h` | 158 | TX steps table, pings, channel survey task | removed except the two tables (moved) |
| `netreport.ino` | 409 | text + JSON report, ports 81/82 tasks, loopback self-check, radio-test task | report rewritten; 81/82 and self-check kept |
| `webserver.h` | 130 | port 80 server, connection pool, rebind, write hooks | small fix (2.2 F1) |
| `webapi.ino` | – | `/api/wifi/scan`, `/connect`, `/forget`, `/ap`, `/api/options`, `/api/live net`, `/api/speedlog` | handlers adjusted, routes unchanged |
| `serialcmd.ino` | 94 | `/WIFI.LOG`, `/WIFIREP.TXT`, serial commands | kept, log texts follow the new states |
| `wifiwpa3.cpp` | 29 | SAE on/off via `--wrap=esp_wifi_init` | kept as is |
| `netboost.cpp` | 54 | larger TCP send window per socket | kept as is |
| `tools/test_wifi_simple.py` | 23 | source contract for the 0.16.2 Wi-Fi | replaced by the contract in 5.9 |
| `tools/test_router_throughput_guard.py` | – | asserts on `linkTuneTask`, `runRadioTest`, `radioTestWanted=true` | assertions on removed code deleted, the rest kept |

### 2.2 Findings: bugs, risks and dependencies

Each finding has evidence and the stage (section 5) that fixes it.

**F1 — The 10 s main-loop stall is the core's write retry loop (verified).**
`webserver.h:129` `netWrite()` → `NetworkClient::write()`. In core 3.3.12
(`NetworkClient.cpp:388-440`) `write()` runs `select()` with a 1 s timeout up
to `WIFI_CLIENT_MAX_WRITE_RETRY = 10` times when the socket is not writable,
i.e. **10 s** when the phone stops acknowledging (screen off, walked away,
roaming). `setTimeout()` only sets `SO_SNDTIMEO`, which the `select` loop does
not use. The engine lock is released meanwhile (`netWaitBegin`), so the engine
task is fine, but `serviceNetwork()`, the LED, SD, the Wi-Fi log and all
fallback timers stand still, and the self-check cannot tell. This matches the
"web 10 s" longest loop part in the 0.16.5 report and "web 3983 ms" in #146.
→ Stage 1.

**F2 — Arduino auto-reconnect floods the trace and hides the first cause.**
`STA.cpp:164-172`: on every `DISCONNECTED` with auto-reconnect on, the core
calls `disconnect()` + `connect()` at once; the driver then reports code 201
followed by code 36 in the same millisecond, every ~1.57 s (#126: 95 events in
50 s). `traceWifi()` (`wifi.ino:4-19`) only merges a repeat of the *same*
type+reason, so the alternating 201/36 pairs fill all 64 slots. D3 keeps the
Arduino reconnect, so the trace must coalesce the pair. → Stage 1.

**F3 — The 10-minute return from fallback is a blind switch.**
`wifi.ino:233-236`: every 10 min, if no phone is joined, `applyNetwork()`
turns the AP off and tries the router for up to 2 min. With the router still
away that is a 2-minute hole in which nothing is reachable, every 10 minutes,
forever. Also a phone joining during those 2 min finds no network. → Stage 4
replaces it by a 0.1 s single-channel look while the AP stays on.

**F4 — Mode flips are the least understood risk.** The scan flips
`WIFI_AP → WIFI_AP_STA → WIFI_AP` (`wifi.ino:193-194, 198, 220`), the fallback
and return go through `WIFI_OFF` (`wifi.ino:169`). 0.16.0's "network
disappeared" was never root-caused; 0.16.2 lost the HTTP rebind in a rewrite.
The core's `mode()` waits up to 1 s for netif status bits. Fewer flips, in one
place, with a test on their count, is the mitigation. → Stage 3 (one function
`radioApply()`, Standalone runs as `WIFI_AP_STA` with the STA idle so scans
and looks never change the mode; see 3.3 and D19).

**F5 — Shared state written from the Wi-Fi event task.** `onWiFi()`
(`wifi.ino:114-130`) writes `staDownSince`, `staIpAt`, `networkIpEpoch`,
`mdnsRefreshPending`… from the system event task (core 0) while
`serviceNetwork()` (core 1) reads and clears them (`wifi.ino:224-236`). Most
are plain `uint32_t`/`bool`, not atomic by contract. → Stage 3: the callback
only appends, under `traceMux`, to two rings: the coalesced trace for the
report (as today) and a **raw event FIFO** (`WifiEv wifiEvents[16]`, type,
reason, ms, head/tail) for the state machine. The machine must see every
single event (two 202s in a row decide "wrong code", 3.5), which the coalesced
trace cannot give it. The main loop drains the FIFO each pass; an overflow
increments `wifiEventOverflow` (reported) and never blocks the callback.

**F6 — Dead state and false report lines.** `staRetryAt`, `staRetryFails`,
`staConnecting`, `routerProbe*`, `routerReturnFailed`, `staSecSwitch*`,
`saQuery*`, `txAutoSteps`, `txNoAnswer`, `staConflict`, `apRestarts`,
`apChannelChanges`, `routerFailure*`, `routerGuard` are declared
(`EngineLink.ino:285-372`) and reported but never set since 0.16.2. The report
prints "Reconnect attempts by EngineLink: 0 (pause 1, 2, 4, 8, then 15 s)",
which is not what the firmware does. `netreport.ino` is 409 lines of which
roughly half describe removed behaviour. → Stage 6.

**F7 — The TX power check is too lenient to notice "never set".**
`applyTxPower(true)` (`wifi.ino:99-107`) accepts a driver value within
±2 quarter-dBm of the target 78 (19.5 dBm). The driver's default maximum is
80 (20 dBm), which passes the check, so a power that was never applied is
reported as "set again 0 times" while the report line says "driver reports
20.00 dBm" (#146, 0.15.1). Router mode never calls `applyTxPower()` at all
(`connectRouter()` `wifi.ino:146-150`). Note: 19.5 vs 20 dBm does not matter
for the radio; what matters is that the report must tell the truth (D17).
→ Stage 1: tolerance 0 (the driver returns exactly the step it was given) and
the power is set after every interface start, checked after every IP epoch.

**F8 — Boot and loss use the same 120 s.** `wifi.ino:228`: 2 min from
`applyNetwork()`, whether at boot (router may still be starting, D5 says 3 min)
or after a loss (D4 says 2 min). → Stage 3.

**F9 — Wrong password is saved and locks the owner out.**
`handleWifiConnect()` (`webapi.ino:812-837`) saves `cfg.router=true`, SSID and
password *before* anything is tried, then switches STA-only. A typo means: own
Wi-Fi off, 2 min of failed logins, back to own Wi-Fi, and every later start
spends 3 min on the same wrong password. → Stage 4 (router test; credentials
saved only after an IP was obtained).

**F10 — Blocking core calls inside HTTP handlers.** `handleCfg()`
(`webapi.ino:727`) calls `startNameService()` (`MDNS.end()`+`MDNS.begin()`)
inside the request; `handleWifiConnect()` relies on a 1.2 s `networkSwitchAt`
delay so the reply reaches the phone before the radio changes. Both are
correct ideas; the plan keeps "reply first, act from the main loop" as a rule
and routes the mDNS restart through a pending flag. → Stage 3/6.

**F11 — `elAllowSae` only takes effect through `esp_wifi_init()`.** The wrap
runs when the driver is initialised, i.e. after `WiFi.mode(WIFI_OFF)`
(`WiFiGeneric.cpp:319-334` deinits) and the next `mode()`. Switching WPA3 on
for a WPA3-only router therefore needs a full radio restart (own Wi-Fi off for
~1 s). The 0.16.5 code does this on every `applyNetwork()`; the new design does
it only when the SAE flag must change. → Stage 3 (`radioApply()`), Stage 4.

**F12 — Rollback confirmation depends on Wi-Fi.** `EngineLink.ino:601`
confirms a new image after 60 s only if `apStarted` or the STA has an IP. With
D5 (router tried for 3 min at boot) a router that is slow to start delays the
confirmation to the fallback at 3 min; the bootloader's rollback window is not
time-limited, so this is safe, but the new state machine must keep `apStarted`
true in Standalone and set it only from the `AP_START` event. → Stage 3 test.

**F13 — Scan while the AP is up moves the radio off channel 6.** A full
13-channel active scan with 120 ms dwell is ~1.6 s plus passive time on 12/13
(country "01", world-safe default: 1–11 active, 12–13 passive; no country is
set anywhere in the firmware and none should be added, D11). Phones tolerate
this; a look on one channel is ~0.15 s. The rules from `netpolicy.h`
(`probeKind()`, tested) say: with a phone joined, only the short look on the
known channel, full scan only every fifth minute when no channel is known.
→ Stage 4 keeps these rules in `wifistate.h` and ports the tests.

**F14 — Memory headroom is small.** Reports show lowest free heap 55–57 KB and
largest block 41–57 KB (#126, #146). Running the STA interface idle alongside
the AP (D19) costs a netif and driver buffers (measure; expected < 10 KB).
→ Stage 3 acceptance: lowest free heap after 1 h ≥ 40 KB, largest block
≥ 32 KB, reported in the Wi-Fi report.

**F15 — iPhone leaves the own Wi-Fi (reason 8) after ~17 s (0.16.4 report).**
Not reproduced on 0.16.5 (phone stayed, 6 HTTP requests). Likely iOS deciding
"no internet" and preferring another known network; the firmware cannot see
this. The plan does **not** fake internet (captive-portal answers, DHCP option
114 via `WiFi.AP.enableDhcpCaptivePortal()` would make iOS open a login sheet
and is wrong for an app). → Stage 8 boat test measures it; the owner deferred
the question (D22), so nothing is built unless T1 shows it recurring.

**F16 — Keep-alive contracts others depend on.** `networkIpEpoch` /
`networkIpEventAt` drive the port 81/82 rebind (`netreport.ino:238-246`), the
port 80 rebind (`wifi.ino:244-250`) and the mDNS restart. `web.closeClients()`
must run before the driver stops (pbuf crash, `webserver.h:19-21`).
`lastHttpMs` is set by all three listeners (contract in
`test_router_throughput_guard.py`). `apStarted` and `WiFi.status()` gate the
rollback confirmation. `routerConfigured()` is used by `led.ino`, `updates.ino`,
`webapi.ino`, `serialcmd.ino`. `routerHold` is read by `webapi.ino:768`,
`netreport.ino`, `serialcmd.ino`, `diagnosis.ino`. These names stay or get a
one-line compatibility accessor (section 3.6).

**F17–F21 — added 2026-11-05 from the boat test (details in section 10).**
F17: the own Wi-Fi's uplink works (join, DHCP Discover) while the downlink
to the phone fails (no TCP connection reaches port 80, DHCP asked twice,
SA Query unanswered); cause open — gate in 10.6. F18: the softAP always runs
PMF on this core (IDF 5.5, `capable` deprecated), so a phone re-joining a
link it considers dead costs 3–97 s of "connected but dead" (reason 2 series
ending in 209). F19: 0.16.2 dropped `esp_wifi_set_bandwidth(AP, HT20)`,
the explicit country and `esp_wifi_set_inactive_time(AP, 600)` that every
working version had (10.3 table). F20: the Wi-Fi report has no per-station
RSSI/PHY and does not print the AP's real bandwidth/channel/PMF state.
F21: a power-on reset occurred when the boat 12 V was connected while the
board ran on USB (hardware; to be confirmed by the owner and measured in
10.6 step 4).

---

## 3. Target architecture

### 3.1 Files after the rewrite

| File | Purpose | Size target |
|---|---|---|
| `wifistate.h` (**new**, pure C++, no Arduino) | the state machine: states, timers, inputs, actions. Host-tested by `tools/test_wifistate.cpp`. | ≤ 220 lines |
| `wifi.ino` (**rewritten**) | event callback → trace ring; `radioApply()` (the only place that calls `WiFi.mode/softAP/begin/disconnect`); scan and look; `serviceNetwork()` that feeds `wifistate` and performs its actions; TX power; HTTP rebind. | ≤ 300 lines (contract test) |
| `netsvc.ino` (**new**) | clock helpers (`validWall`, `setClockFromUtc`, `allowTime`, NTP start/notify), mDNS (`startNameService`, `validHostName`), GitHub device-report scheduling, reason/trace texts. Moved verbatim from `wifi.ino`/`serviceNetwork()`. | ~120 lines |
| `netreport.ino` (**slimmed**) | report text + JSON with only fields that are set; ports 81/82; loopback self-check. | ≤ 260 lines |
| `txpower.h` (**new**, tiny) | `elTxPower[5]`, `elTxName[5]` moved out of `linktune.cpp`. | 12 lines |
| removed | `netpolicy.h`, `routerguard.h`, `linktune.cpp`, `linktune.h`, `tools/test_netpolicy.cpp`, `tools/test_routerguard.cpp`, `tools/test_wifi_simple.py` (replaced by `tools/test_wifi_contract.py`) | |

### 3.2 States (D1) and their meaning

```
STANDALONE      own Wi-Fi on (AP up on apChannel), STA idle (no connect).
                With a router saved: a "look" every LOOK_MS (60 s).
ROUTER_TEST     own Wi-Fi stays on; STA tries the router once (≤ TEST_MS=20 s).
                Entered from STANDALONE by /api/wifi/connect (setup) or by a
                successful look (return). Ends in ROUTER (AP off) or back in
                STANDALONE with a result code. A RETURN test starts only when
                no phone is joined (D21) or after "Switch now" (D20).
ROUTER_JOIN     boot only (D5): STA only, Arduino reconnect on, up to
                BOOT_JOIN_MS=180 s without an IP → STANDALONE.
ROUTER          STA only with an IP. Arduino reconnect on.
ROUTER_LOST     STA only, no IP (DISCONNECTED or LOST_IP seen). Arduino
                reconnect keeps trying. After LOST_MS=120 s without an IP →
                STANDALONE. An IP → ROUTER.
```

The owner's four states map as: Standalone = `STANDALONE`; Router test =
`ROUTER_TEST`; Router = `ROUTER_JOIN` + `ROUTER`; Router lost = `ROUTER_LOST`.
`ROUTER_JOIN` is separate only because its timeout differs (3 min vs 2 min).

### 3.3 Radio modes per state (D19)

| State | `WiFi.getMode()` | AP | STA |
|---|---|---|---|
| STANDALONE | `WIFI_AP_STA` | up, apChannel | started, idle (never `begin()`), used only for scans/looks |
| ROUTER_TEST | `WIFI_AP_STA` | up (hops to the router's channel when the STA associates) | `begin()`, auto-reconnect **off** |
| ROUTER_JOIN / ROUTER / ROUTER_LOST | `WIFI_STA` | off | `begin()`, auto-reconnect **on** |

Why `WIFI_AP_STA` in Standalone and not `WIFI_AP`: the STA interface must
exist to scan or look; creating/destroying it means a mode flip each time
(F4). An idle STA is not a second network (D1 holds: nothing is served on it,
no `begin()` is called). It is one `constexpr bool STANDALONE_APSTA = true;`
in `wifi.ino`; set to `false` and the code uses `WIFI_AP` and flips to
`WIFI_AP_STA` only for the duration of a scan/look (the 0.16.5 behaviour), so
the choice is reversible if the boat test shows a problem.

Only `radioApply(RadioMode want, bool allowSae)` changes the mode:

```
RadioMode: RADIO_APSTA_IDLE, RADIO_APSTA_JOIN, RADIO_STA
radioApply(want, allowSae):
  if (allowSae != elAllowSae) { web.closeClients(); WiFi.mode(WIFI_OFF); elAllowSae = allowSae; }   // F11
  WiFi.persistent(false); WiFi.setHostname(cfg.hostName)   // before any interface starts
  switch want:
    RADIO_APSTA_IDLE / RADIO_APSTA_JOIN:
      WiFi.setAutoReconnect(false)                 // FIRST: coming from ROUTER_LOST the core is still retrying (see 3.5 note)
      if mode != WIFI_AP_STA: web.closeClients(); WiFi.mode(WIFI_AP_STA)
      if !apUp: startAP()        // softAPConfig + softAP(...,apChannelNow,false,6) + setSleep(false) + applyTxPower(false)
      if want == RADIO_APSTA_JOIN: WiFi.begin(ssid, pass or nullptr)
      else if STA connected or connecting: WiFi.STA.disconnect(false, 0)   // esp_wifi_disconnect(), no wait; auto-reconnect is already off
    RADIO_STA:
      if mode != WIFI_STA: web.closeClients(); WiFi.mode(WIFI_STA)   // takes the AP down
      WiFi.setSleep(false); WiFi.setAutoReconnect(true); applyTxPower(false)
      if not connected/connecting with the same ssid: WiFi.begin(ssid, pass or nullptr)
  ++radioApplies  (reported)
```

`startAP()` keeps the 0.16.5 line exactly:
`WiFi.softAP(cfg.apName.c_str(),cfg.apPassword.c_str(),apChannelNow,false,6)`
with `apChannelNow = (cfg.apChannel>=1&&cfg.apChannel<=11)?cfg.apChannel:6`
and `WiFi.softAPConfig(192.168.4.1, 192.168.4.1, 255.255.255.0)` before it.

### 3.4 Inputs and timers (all in `wifistate.h`)

Events (fed from the trace ring, main loop only):

```
EV_AP_START, EV_AP_STOP, EV_AP_STA_JOIN, EV_AP_STA_LEAVE(reason), EV_AP_STA_IP,
EV_STA_CONNECTED, EV_STA_GOT_IP, EV_STA_LOST_IP, EV_STA_DISCONNECTED(reason),
EV_SCAN_DONE(routerSeen, routerChannel, routerAuth)
EV_CMD_CONNECT(ssid, pass)        from /api/wifi/connect (reply sent first)
EV_CMD_FORGET                      from /api/wifi/forget
EV_CMD_RETURN_OK                   from the app's "Switch now" (D20, POST /api/wifi/return)
EV_RAPID_BOOTS                     at boot, when the 3-in-10-s rule fires (D9)
```

Constants (`wifistate.h`, `constexpr`, no settings — D7):

| Name | Value | Meaning |
|---|---|---|
| `BOOT_JOIN_MS` | 180 000 | D5: router tried at boot |
| `LOST_MS` | 120 000 | D4: router lost before Standalone |
| `TEST_MS` | 20 000 | router test budget (owner said ~15 s; 20 s covers one Arduino first-connect retry + DHCP) |
| `LOOK_MS` | 60 000 | look for the router from Standalone |
| `LOOK_AFTER_FAIL_MS` | 300 000, then 900 000 | after a return that failed (wrong code, router refuses): 5 min, then 15 min, forever. Resets to `LOOK_MS` after a success or a new `/api/wifi/connect`. |
| `LOOK_FULL_EVERY` | 5 | every fifth look scans all channels when a channel is known; always full when unknown and no phone joined |
| `LOOK_DWELL_MS` | 120 | active dwell per channel |
| `RAPID_BOOT_WINDOW_MS` | 10 000 | D9 |
| `RAPID_BOOT_COUNT` | 3 | D9 |
| `BIND_DELAY_MS` | 600 | HTTP/mDNS (re)bind after an IP epoch change (unchanged) |

There is deliberately **no** idle timer (D21): "phone joined" means
`WiFi.softAPgetStationNum() > 0`, nothing else. The machine input
`phoneJoined` is that boolean; `lastHttpMs` is no longer an input to the
Wi-Fi logic (it stays for the reports and the HTTP self-check).

Timers are `uint32_t` millis with wrap-safe subtraction, as today.

### 3.5 Transitions

```
BOOT:
  rapidBoots >= 3            → STANDALONE (not saved; counter cleared); autoReturn=false for this
                               boot: looks still run so the app can show "Switch now", but no automatic
                               RETURN test (the owner power-cycled to get the own Wi-Fi; a return 60 s
                               later would defeat that). A normal boot restores autoReturn.   [D9, D21]
  else routerConfigured()    → ROUTER_JOIN   radio RADIO_STA
  else                       → STANDALONE    radio RADIO_APSTA_IDLE

ROUTER_JOIN:
  EV_STA_GOT_IP              → ROUTER;  save rch=channel if changed; lookEvery=LOOK_MS
  t >= BOOT_JOIN_MS          → STANDALONE  (reason "router not reached in 3 min")

ROUTER:
  EV_STA_DISCONNECTED/LOST_IP→ ROUTER_LOST  (lostAt = now)                   [Arduino reconnects by itself]
  EV_CMD_FORGET              → cfg.router=false, ssid="", pass=""; save; STANDALONE
  EV_CMD_CONNECT             → never reaches the machine: the handler replies 409 unless
                               state==STANDALONE (the app offers Connect only when the scan is
                               allowed, webapi.ino:768, which is also STANDALONE-only).

ROUTER_LOST:
  EV_STA_GOT_IP              → ROUTER
  t - lostAt >= LOST_MS      → STANDALONE  (reason "router lost for 2 min")
  EV_CMD_FORGET              → as in ROUTER

STANDALONE:
  EV_CMD_CONNECT(ssid,pass)  → ROUTER_TEST (testKind=SETUP, pending creds kept in RAM only)
  routerConfigured() && t >= nextLookAt
      && lookKind(lookNo, knownChannel, phoneJoined) != LOOK_SKIP
                             → start look (scan); nextLookAt += lookEvery
  EV_SCAN_DONE(seen=true)    → if (!phoneJoined && autoReturn) → ROUTER_TEST (testKind=RETURN, saved creds,
                                                                   allowSae=lastJoinUsedSae)        [D6, D21]
                               else                 → routerBackSeen=true (app shows "Switch now"), stay
  EV_SCAN_DONE(seen=false)   → routerBackSeen=false, stay
  EV_CMD_RETURN_OK           → if routerBackSeen → ROUTER_TEST (testKind=RETURN, returnOkFromApp=true)
                               else ignored (handler already replied 409)
  EV_STA_GOT_IP (stale: a join that completed just as ROUTER_LOST gave up, or a late
    event after a test)      → if (!phoneJoined && autoReturn) → ROUTER (radio RADIO_STA)
                               else → WiFi.STA.disconnect(false,0); routerBackSeen=true; stay   [D21]
  EV_STA_DISCONNECTED(any)   → ignored (logged only): the core's first_connect retry and our own
                               disconnect at the end of a test both produce late events here
  EV_CMD_FORGET              → clear creds, save, routerBackSeen=false, stay (stops the looks)

ROUTER_TEST (AP stays up, STA begin() once, auto-reconnect off):
  EV_STA_GOT_IP              → testResult=OK; lastJoinUsedSae=allowSae; if SETUP: cfg.router=true, ssid,
                               pass → saveSettings(); save rch; → ROUTER  radio RADIO_STA (AP goes down here, D1)
  EV_STA_DISCONNECTED(r):
      r in {210, 211} and !saeTried and elSaeControlActive()
                                         → saeTried=true; radioApply(RADIO_APSTA_JOIN, allowSae=true); keep testing
                                           (D10: WPA3 only when the router demands it; the AP blips ~1 s, 3.3/F11)
      r in {15, 202, 204, 205, 203, 210, 211} → ++authFails; if authFails >= 2 → testResult=WRONG_CODE
                                           (one auth failure is not proof: HANDOVER "202 does not prove a wrong
                                           password"; the core's first_connect retry supplies the second attempt
                                           within a few seconds, STA.cpp:160-163)
      r == 201                           → testResult=NOT_FOUND (deterministic: no beacon on 2.4 GHz)
      other                              → keep waiting
      on a result: WiFi.STA.disconnect(false, 0) → STANDALONE; if RETURN: lookEvery = next LOOK_AFTER_FAIL step
                               then, if WiFi.channel() != apChannelNow (the AP followed the router's channel
                               while the station associated and the driver leaves it there): startAP() again
                               so the own Wi-Fi is back on its fixed channel (D2). Phones rejoin by themselves.
  EV_AP_STA_JOIN while testKind==RETURN && !returnOkFromApp
                             → abort: WiFi.STA.disconnect(false,0) → STANDALONE, routerBackSeen=true,
                               no failure penalty (lookEvery unchanged)                          [D21]
  t >= TEST_MS               → testResult = authFails ? WRONG_CODE : TIMEOUT → as above
  EV_CMD_CONNECT / EV_CMD_RETURN_OK while testing → handler replies 409 "A router test is running"

ROUTER_JOIN (boot):
  EV_STA_DISCONNECTED(210|211) and !saeTried and elSaeControlActive()
                             → saeTried=true; radioApply(RADIO_STA, allowSae=true)   (same D10 rule; STA only, nothing to disturb)
```

Note on leaving `ROUTER_LOST`/`ROUTER_JOIN` for `STANDALONE`: the core's
auto-reconnect is still running (`STA.cpp:164-172`). `radioApply()` turns it
off **before** the mode change (3.3), otherwise the station keeps joining
behind the AP and a late success would give two networks (D1) or cut a phone
off (D21). The stale `EV_STA_GOT_IP` rule above is the belt to that brace.

Rules that hold in every state:

- `serviceNetwork()` is the only caller of `radioApply()`, scans and
  `MDNS`; the event callback never calls a `WiFi.*` function (D14).
- After `EV_AP_START` or `EV_STA_GOT_IP`: `networkIpEpoch++`,
  `networkIpEventAt = now`; `BIND_DELAY_MS` later `web.rebindNetwork()`,
  `applyTxPower(true)`, then `startNameService()` (mDNS on whichever interface
  has the address). Ports 81/82 follow the epoch as today. `EV_AP_STOP` (the
  AP going down after a successful test) sets only `mdnsRefreshPending`, so
  the name is re-announced on the router interface; it does not bump the
  epoch (the listeners bind to all interfaces).
- `apChannelNow` in `/api/live`, the report and the log is read from
  `WiFi.channel()` when the AP is up, never assumed (D17).
- `WiFi.setSleep(false)` after every mode change (power save off, D14).
- A state change writes one line to `/WIFI.LOG` (`wifiLog()`) and one to
  `Serial`, both without passwords.

### 3.6 Names other files rely on (keep)

| Name | Where used | New definition |
|---|---|---|
| `bool routerConfigured()` | led, updates, webapi, serialcmd, netreport | `cfg.router && cfg.ssid.length()` (unchanged) |
| `bool routerHold` | webapi:768, netreport, serialcmd, diagnosis | becomes `inline bool routerHold()`? **No**: keep a plain `bool routerHold` kept equal to `state==STANDALONE && routerConfigured()` by `serviceNetwork()`, so the readers compile unchanged. Meaning: "a router is saved but the own Wi-Fi is on". |
| `bool apStarted` | EngineLink.ino:601 rollback, serialcmd, netreport | set true on `EV_AP_START`, false on `EV_AP_STOP` or when `RADIO_STA` is applied |
| `volatile uint32_t networkIpEpoch, networkIpEventAt` | netreport 81/82 rebind | unchanged semantics |
| `uint32_t httpBoundEpoch, httpBoundAt` | diagnosis, netreport | unchanged |
| `volatile uint32_t lastHttpMs` | webserver.h, netreport | unchanged |
| `uint8_t apChannelNow` | webapi live, serialcmd | unchanged |
| `uint8_t routerKnownChannel` + NVS `rch` | storage, webapi | unchanged |
| `bool pendingNetwork; uint32_t networkSwitchAt` | webapi handlers | kept as the "reply first, act later" mechanism: handlers set `pendingCmd` + `networkSwitchAt=now+1200`; `serviceNetwork()` turns it into `EV_CMD_*` when due. `pendingNetwork` stays the flag's name. |
| `traceWifi(type, reason)`, `getWifiTrace()`, `RouterTrace`, `ROUTER_TRACE_CAP`, `traceTotal` | serialcmd (SD log), netreport | unchanged API; coalescing improved (F2) |
| `reasonText()`, `traceText()`, `stationSecurityName()` | netreport, serialcmd | moved to `netsvc.ino`, unchanged |
| `validWall()`, `setClockFromUtc()`, `allowTime()`, `ntpCallback`, `elapsedMs()` | trips, storage, webapi, nmea2000 | moved to `netsvc.ino`, unchanged |
| `startNameService()`, `validHostName()` | webapi, storage | moved to `netsvc.ino`; `handleCfg` sets `mdnsRefreshPending=true` instead of calling it (F10) |
| `applyTxPower(bool)` | wifi.ino only | kept, tolerance fixed (F7) |
| `elTxPower[]`, `elTxName[]`, `txLevel`, `TX_LEVEL_FIXED` | netreport, webapi state JSON | moved to `txpower.h` |
| `speedLog[]`, `SPEED_LOG_N` | webapi, netreport | unchanged |
| `radioTestWanted`, `linkTune*`, `linkScan*`, `runRadioTest`, `linkTuneTask` | netreport, webapi:981, EngineLink.ino:548, test_router_throughput_guard.py | **removed** everywhere (D13) |

### 3.7 API contract (routes unchanged, fields added)

`/api/live` → `net` (read by the app every second; keep every existing key):

```
"net":{"ap":<apName>,"host":<hostName>,"cl":<AP stations>,"ch":<apChannelNow>,
       "rt":<routerConfigured>,"sta":<STA has IP>,"ssid":<cfg.ssid>,"rssi":<or null>,
       "ip":<STA ip or null>,"conflict":false,"mode":"ap"|"sta","rescue":<routerHold>,
       "state":"standalone"|"router-test"|"router-join"|"router"|"router-lost",   // new
       "test":null|{"kind":"setup"|"return","result":"testing"|"ok"|"wrong-code"|"not-found"|"timeout","ms":<n>},  // new
       "routerBack":<bool>}                                                            // new, D20
```

`"mode"` keeps its 0.16.5 definition `(WiFi.getMode()&WIFI_MODE_AP)?"ap":"sta"`,
so the unchanged app shows "EngineLink's own Wi-Fi" in Standalone and during a
test, and "Connected to router"/"Connecting" in the STA states.

`POST /api/wifi/connect` (ssid, pass): validates as today (`webapi.ino:813-816`),
refuses with 409 while `state!=STANDALONE` ("Connect to EngineLink Wi-Fi
first") or while a scan/test runs. **Does not save** the credentials. Reply
text (English, app shows it as a toast): `"Testing <ssid> for 20 seconds. Keep
your phone on EngineLink Wi-Fi; the result shows here. If the code is right,
EngineLink moves to the router and its own Wi-Fi goes off."` Then
`pendingNetwork=true; networkSwitchAt=now+1200`.

`GET /api/wifi/test` (**new**, tiny): `{"state":...,"test":...}` — the same two
objects as in `/api/live`, for the app change in stage 7. Harmless without it.

`POST /api/wifi/forget`: unchanged reply; becomes `EV_CMD_FORGET` 1.2 s later.

`POST /api/wifi/return` (**new**, D20): 409 `"EngineLink is not on its own
Wi-Fi"` unless `state==STANDALONE`; 409 `"The router has not been seen yet"`
unless `routerBack` is true; otherwise reply `"Switching to <ssid>. Move your
phone to the router; the app follows."` and `pendingCmd=CMD_RETURN`,
`networkSwitchAt=now+1200` → `EV_CMD_RETURN_OK`. This is the only path on
which a joined phone loses the own Wi-Fi (D21: the owner asked for it).

`GET /api/wifi/scan?start=1`: unchanged JSON (`busy, done, nets[{s,r,lock,ch}]`,
and the legacy `channel, pass, firstPassNets, passes, channels, raw, failures,
durationMs, error` keys kept with their current values so the app's progress
text still renders). Allowed only in `STANDALONE`, not during a look or test.
A look that is due while a user scan runs is skipped to the next minute.

`POST /api/options` (first-run setup, `webapi.ino:309-345`): keeps writing
`cfg.router/ssid/apChannel/apPassword` and setting `pendingNetwork`. Change:
when `network=="router"` it must go through the router test too — set
`pendingCmd=CMD_CONNECT` with the given ssid/pass and **not** write
`cfg.router=true`/`cfg.ssid` (they are written by the test on success). AP
name/password/channel changes still apply with a `radioApply()` restart of the
AP only.

`GET /api/network-health`, `GET /api/network-report`, port 81 `/report`,
serial `wifi`: regenerated from the field list in 3.9.

`POST /api/speedlog`: unchanged except the `radioTestWanted` line is removed.

### 3.8 Reason codes shown as a test result

| Driver reason | Result | Report/log text |
|---|---|---|
| 201 `NO_AP_FOUND` | `not-found` | "Router not found (off, out of range, or 5 GHz only)" |
| 15, 204 handshake timeout; 202 `AUTH_FAIL` | `wrong-code` after the **second** such failure in one test (or at `TEST_MS` if there was one) | "Wrong code or the router refused the login (code n)" — 202 can also be a router that refuses; the text says so (HANDOVER: 202 does not prove a wrong password) |
| 205 `CONNECTION_FAIL`, 203 `ASSOC_FAIL` | `wrong-code` (same two-strikes rule) | "Router refused the connection (code n)" |
| 210/211 no compatible security | first time: retry once with WPA3 allowed (3.5); second time `wrong-code` | "Router security not supported (code n)" — reached when the WPA3 retry also fails or the build has no SAE switch (`elSaeControlActive()` false) |
| 200 beacon timeout, 8 leave, 36, others | keep waiting | logged only |

### 3.9 Report: the only fields that exist after the rewrite

Text report sections: header (version, uptime, **state**, radio mode, AP
channel, router name or "Not configured", IP, RSSI, negotiated security,
WPA3 switch active, TX power target/driver/reapplied, power save, PHY mode),
**timeline since boot** (state changes with uptime: at most 32 entries),
counters (`apJoins, apLeaves(lastReason), apDhcpLeases, apStarts,
staAssociations, staIps, staDisconnects(lastReason), lostIps, boots3`),
router test (`last kind/result/ms`), looks (`looks, looksSeen, returns,
returnsFailed, nextLookS`), router as seen by EngineLink (unchanged block from
`netreport.ino:164-178`: maker prefix, channel/width, country, cipher, RSSI),
speed tests (unchanged), system (unchanged), HTTP/web (unchanged plus
`httpWriteTimeouts`), latest Wi-Fi events (unchanged), JSON.

Deleted lines: "Reconnect attempts by EngineLink…", "Rescue looks… last known
router channel" (replaced by the looks block), the whole radio test/pings/
channel survey/"Quietest of channels" block, "Security switches", "Failure
stage at timeout", "Time before rescue", "Note: final failure stage…",
"restarted by the watchdog".

JSON keys removed: `associationFailures, retryFails, routerProbes,
routerProbeSeen, driverNudges, failedReason, failedPhase, failedAfterMs,
unexpectedReason, unexpectedDrops, linkTune, secMode, secSwitches,
saeConfigured, saeSetupError, lastFailureDescription, routerFallbacks,
routerAutoReturns, routerNextTryS, routerAttempts`. Added: `state, test,
looks, looksSeen, returns, returnsFailed, nextLookS, radioApplies, boots3,
httpWriteTimeouts, heapLowest, heapLargest`.

`diagnosis.ino:449-499` (`"wifi":{…}` in `/api/diag`): the app reads
`routerFailureReason, routerUnexpectedReason, staReason, staIpEvents,
routerUnexpectedDrops, routerNextTryS, routerAuthMode` (app.html:1760-1783).
Keep those keys: `routerFailureReason` = last test/lost reason,
`routerUnexpectedReason` = last reason while in ROUTER, `routerUnexpectedDrops`
= count of ROUTER→ROUTER_LOST, `routerNextTryS` = seconds to the next look
(null unless STANDALONE with a router saved). Delete the rest of that block's
dead keys (`conflict, chMoves, apStops, routerWaitSec, routerDriverNudges,
routerNativeAutoReconnect, routerAssociationFailures, staFails, staConnects,
routerHold→keep, routerFallbacks, routerAutoReturns`).

### 3.10 LED (ledpatterns.h is fixed; only the state→pattern mapping)

`led.ino:15-16` shows `S_ROUTER` ("Boat router not reached") when a router is
saved and there is no IP for > 15 s or in hold. New mapping, same patterns:

| State | LED |
|---|---|
| ROUTER | S_LIVE / S_READY as today |
| ROUTER_JOIN, ROUTER_LOST (after 15 s), STANDALONE with a router saved | S_ROUTER |
| STANDALONE without a router, ROUTER_TEST | S_LIVE / S_READY |

### 3.11 NVS keys

| Namespace / key | Type | Meaning | Change |
|---|---|---|---|
| `enginelink/router, ssid, wpass, apch, appass, apname, host` | existing | credentials and AP settings | unchanged (D12) |
| `enginelink/rch` | u8 | router channel for the look | unchanged; written only when it changes |
| `enginelink/smode` | u8 | WPA3 mode | **no longer read or written** (D10); left in place, removed by factory reset as today |
| `rlog/quick` | u8 | rapid-boot counter (D9) | **new**: read+1 and written in `setup()` right after `restartLogBoot()`; written to 0 by `loop()` once `millis() >= RAPID_BOOT_WINDOW_MS`. If the value read is ≥ `RAPID_BOOT_COUNT-1`, this boot is Standalone and the key is written to 0 at once. A crash loop also lands in Standalone, which is the safe side. Two NVS writes per boot are negligible wear. |

---

## 4. Test strategy

Four layers; every stage names which apply.

1. **Host tests** (CI, `g++ -std=c++17 -Wall -Wextra`): `tools/test_wifistate.cpp`
   drives `wifistate.h` with synthetic time and events. It must contain the
   scenarios in 5.2. Build line added to `.github/workflows/validate.yml`
   where `test_netpolicy` is today.
2. **Source contracts** (CI, Python): `tools/test_wifi_contract.py` (5.9)
   replaces `test_wifi_simple.py`; `test_router_throughput_guard.py` keeps
   its non-radio assertions.
3. **Compile** (CI, `build-engine.yml`): unchanged command; the
   `__wrap_esp_wifi_init` objdump check stays.
4. **Bench/boat** (owner, with the serial `wifi` report or the SD log): the
   protocol in 5.8 with pass/fail numbers. Nothing in this plan may be
   called "verified" before 5.8 is done on the board.

---

## 5. Stages

Each stage is one or two commits on the Wi-Fi 2.0 branch, compiles, passes
CI, and can be read on its own. Stages 1–6 are firmware; 7 is the app part;
8 is the release. Order matters where stated.

**Gate added 2026-11-05 (section 10):** Stages 2–8 do not start before the
root-cause gate in 10.6 has a result. Stage 1 may be built and tested on the
bench in the meantime because its three fixes are independent of the cause,
and 10.7 adds the AP radio-configuration block and the link diagnostics to it.

### 5.1 Stage 1 — Independent fixes (no behaviour redesign)

Purpose: remove the three known defects that would otherwise be blamed on the
rewrite.

1a. **Bounded HTTP writes (F1).** New `netwrite.cpp/.h`:
`size_t elWriteBounded(int fd, const uint8_t *b, size_t n, uint32_t perWaitMs)`
— loop: `select(fd, write set, timeout perWaitMs)`; on ready `send(fd, …,
MSG_DONTWAIT)`; on timeout or error return what was sent so far. In
`webserver.h` `netWrite()` and `_currentClientWrite_P()` call it with
`HTTP_IO_TIMEOUT_MS` (1000); a short write sets `writeTimedOut=true`,
increments `web.writeTimeouts`, and `serve()` stops that client and skips
keep-alive. `ChunkOut::send()` (`EngineLink.ino:136-140`) checks the return
of `web.sendContent()`'s underlying write (`wrote`/`writeTimedOut` flag on
`web`) and stops adding data for that request instead of looping; add
`bool aborted=false;` to `DlStat` (`model.h:443-445`) so the report can say
"download aborted (phone stopped reading)". Acceptance: with a phone that stops reading
(airplane mode mid-download), `loopPartMaxMs[0]` ("web") ≤ 1 200 ms; report
shows `httpWriteTimeouts ≥ 1`. Contract test: `"elWriteBounded(" in
body(webserver.h, "size_t netWrite(")` and `"WIFI_CLIENT_MAX_WRITE_RETRY" not
in firmware` (documentation grep).

1b. **TX power truth (F7).** `applyTxPower(check)`: tolerance `!=` instead of
`abs()<=2`; call `applyTxPower(false)` also after `WiFi.begin()` in the STA
path and `applyTxPower(true)` on every `httpBoundEpoch` change in both modes.
Report prints target and driver value in quarter-dBm units too. Acceptance:
USB `wifi` report in both modes says "driver reports 19.50 dBm", `txReapplied`
small and stable.

1c. **Trace coalescing (F2).** `traceWifi()`: a `DISCONNECTED` with reason 36
arriving ≤ 50 ms after a `DISCONNECTED` with another reason is folded into
that entry (`count` not incremented, `lastMs` updated). Repeats of the same
reason keep merging as today. Host test (new, small, pure function
`traceFold(prevType, prevReason, prevMs, type, reason, ms)` in `wifistate.h`):
sequence 201,36,201,36 at 0,0,1574,1574 ms → 1 entry, count 2. Acceptance on
the board: a router switched off for 2 min yields < 10 trace entries.

### 5.2 Stage 2 — `wifistate.h` + host test (pure logic)

Write `firmware/EngineLink/wifistate.h` with:

```cpp
namespace wifistate {
  enum State : uint8_t { STANDALONE, ROUTER_TEST, ROUTER_JOIN, ROUTER, ROUTER_LOST };
  enum TestKind : uint8_t { TEST_NONE, TEST_SETUP, TEST_RETURN };
  enum TestResult : uint8_t { TR_NONE, TR_TESTING, TR_OK, TR_WRONG_CODE, TR_NOT_FOUND, TR_TIMEOUT };
  enum Radio : uint8_t { RADIO_APSTA_IDLE, RADIO_APSTA_JOIN, RADIO_STA };
  enum Look : uint8_t { LOOK_SKIP, LOOK_CHANNEL, LOOK_ALL };
  struct Machine {
    // inputs the .ino keeps up to date before calling tick()
    bool routerSaved=false, phoneJoined=false;      // phoneJoined = softAPgetStationNum()>0, nothing else (D21)
    uint8_t knownChannel=0; bool saeAvailable=true;  // elSaeControlActive()
    // outputs
    State state=STANDALONE; Radio radio=RADIO_APSTA_IDLE; bool radioDirty=false;
    TestKind testKind=TEST_NONE; TestResult testResult=TR_NONE; uint32_t testMs=0;
    bool allowSae=false, saeTried=false, lastJoinUsedSae=false, returnOkFromApp=false, autoReturn=true;
    uint8_t authFails=0;
    bool wantLook=false; Look lookKind=LOOK_SKIP; bool saveCreds=false; bool routerBackSeen=false;
    uint32_t looks=0, looksSeen=0, returns=0, returnsFailed=0, lostEvents=0;
    // api
    void boot(uint32_t now, bool routerSaved, bool rapidBoots);
    void event(uint32_t now, uint8_t ev, uint8_t reason);   // ev = EV_* above
    void scanResult(uint32_t now, bool seen, uint8_t channel);
    void cmdConnect(uint32_t now); void cmdForget(uint32_t now); void cmdReturn(uint32_t now);
    void tick(uint32_t now);
    uint32_t nextLookInMs(uint32_t now) const;
  };
}
```

`tools/test_wifistate.cpp` scenarios (each an `assert` block with a comment
naming the rule):

1. Boot without router → STANDALONE, radio APSTA_IDLE, no looks ever.
2. Boot with router, IP at 1.4 s → ROUTER, radio STA (D5, #146 timing).
3. Boot with router, nothing for 180 s → STANDALONE at exactly ≥ 180 000 ms, not at 120 000 (F8).
4. ROUTER, DISCONNECTED(200) → ROUTER_LOST; GOT_IP at +50 s → ROUTER, `lostEvents==1` (#126 shape).
5. ROUTER_LOST for 120 s → STANDALONE (D4).
6. STANDALONE with router: looks at 60, 120, 180… s; `lookKind` = CHANNEL when `knownChannel` set and phone joined, ALL every 5th when no phone; SKIP when phone joined and channel unknown except every 5th (ported from `test_netpolicy.cpp`, keep its asserts).
7. Look sees the router, no phone → ROUTER_TEST(RETURN) → GOT_IP → ROUTER, `returns==1`.
8. Look sees the router, phone joined → stays STANDALONE for ever while the phone stays (simulate 24 h of looks), `routerBackSeen==true`, `returns==0` (D21). A later look that does not see the router → `routerBackSeen==false`. `cmdReturn()` with `routerBackSeen` → ROUTER_TEST(RETURN, `returnOkFromApp`) → GOT_IP → ROUTER. `cmdReturn()` without `routerBackSeen` → no change.
9. ROUTER_TEST(RETURN) → DISCONNECTED(202) → STANDALONE, `returnsFailed==1`, next look 300 s later, then 900 s, never more (D7 bound; HANDOVER lesson "wrong password cannot keep cutting the phone off").
10. cmdConnect in STANDALONE → ROUTER_TEST(SETUP); GOT_IP → `saveCreds==true`, ROUTER. cmdConnect → DISCONNECTED(201) → TR_NOT_FOUND, `saveCreds==false`, STANDALONE (F9). cmdConnect → DISCONNECTED(202) at 2 s → still testing; DISCONNECTED(202) at 5 s → TR_WRONG_CODE (two strikes). One 202 then GOT_IP at 8 s → TR_OK (a slow first handshake is not a wrong code).
11. ROUTER_TEST: DISCONNECTED(8) at 1 s is ignored; TIMEOUT at 20 s with no auth failure → TR_TIMEOUT; with one 202 → TR_WRONG_CODE.
12. Rapid boots → STANDALONE even with router saved (D9); `autoReturn==false`: a look that sees the router sets `routerBackSeen` but starts no test even with no phone joined; `cmdReturn()` still works.
13. cmdForget in ROUTER and ROUTER_LOST → STANDALONE, `routerSaved=false`, looks stop.
14. `radioDirty` is true only on state changes that change the radio (count them: boot→ROUTER_JOIN→STANDALONE→TEST→ROUTER is 3 radio changes, looks are 0) — this is the F4 test.
15. millis wrap: all timers correct across `0xFFFFFF00 → 0x100`.
16. STANDALONE, stale GOT_IP: no phone → ROUTER; phone joined → stays STANDALONE, `routerBackSeen==true`, output "disconnect STA" set (D21). A DISCONNECTED(8) in STANDALONE changes nothing.
17. ROUTER_TEST(RETURN) without `returnOkFromApp`: EV_AP_STA_JOIN at 3 s → STANDALONE, `routerBackSeen==true`, `returnsFailed==0`, next look still at 60 s (D21). With `returnOkFromApp` the same event is ignored and the test runs on.
18. ROUTER_TEST and ROUTER_JOIN: DISCONNECTED(210) → `allowSae==true`, `radioDirty==true`, still testing; a second 210 → TR_WRONG_CODE. With `saeAvailable=false` the first 210 is already TR_WRONG_CODE (D10).
19. Look timing after a failed return: 300 s, 900 s, 900 s…; after a successful return and a later loss the interval is 60 s again (3.4).
20. `lastJoinUsedSae`: boot join needs the 210 retry (scenario 18) → ROUTER with `lastJoinUsedSae==true`; later loss → STANDALONE → look seen → the RETURN test starts with `allowSae==true` straight away (no second AP blip). A `cmdForget()` clears it.

Acceptance: CI green; no Arduino includes in `wifistate.h`
(`grep -L Arduino`).

### 5.3 Stage 3 — New `wifi.ino` and `netsvc.ino` wired in

1. Create `netsvc.ino`; move the clock, NTP, mDNS, reason/trace texts and the
   GitHub report scheduling (`wifi.ino:253-263`) there unchanged. The report
   scheduling condition becomes `state==ROUTER` instead of `online`.
2. Rewrite `wifi.ino` as in 3.3–3.5: `onWiFi()` only `traceWifi()`, the
   raw FIFO push (F5) and the existing `volatile` counters;
   `serviceNetwork()` drains the FIFO into `machine.event()`,
   updates inputs, calls `tick()`, performs `radioApply()` when `radioDirty`,
   handles the bind epoch, calls `applyTxPower`, `startNameService()` via the
   pending flag, and writes log lines on state change.
3. `setup()`: replace `applyNetwork()` with `machine.boot(...)` +
   `serviceNetwork()`; add the D9 counter right after `restartLogBoot()`;
   `loop()` clears it after 10 s.
4. Delete the dead globals listed in F6 from `EngineLink.ino`; keep the names
   in 3.6. Remove `#include "netpolicy.h"`, `"routerguard.h"`, `"linktune.h"`;
   add `"wifistate.h"`, `"txpower.h"`. Remove `linkTuneTask` creation
   (`EngineLink.ino:548`) and `radioTestWanted`.
5. `webapi.ino`: `handleWifiConnect/Forget/Options/ApSave` set `pendingCmd`
   as in 3.7; `handleWifiScan` gates on `state==STANDALONE && !lookRunning`;
   `handleCfg` host change sets `mdnsRefreshPending`.
6. `led.ino`: mapping 3.10.
7. Replace `tools/test_wifi_simple.py` with `tools/test_wifi_contract.py`
   (5.9) and update `validate.yml` (also drop the `test_netpolicy` and
   `test_routerguard` steps, add `test_wifistate`).

Acceptance (bench, USB serial): boots standalone, phone joins, `HTTP/80
listening on 192.168.4.1` printed once, app loads; `wifi` report shows
`state standalone`, heap lowest ≥ 40 KB after 1 h (F14); with a router saved
and present: `state router` within 5 s of boot; with the router off: `router
lost` log line, `standalone` at +120 s; router back on: a look sees it within
60 s and (no phone joined) `router` again within 25 s.

### 5.4 Stage 4 — Router test, looks and return

Already specified by the machine; this stage implements the radio side:

1. `startLook(kind)`: `WiFi.scanNetworks(true, true, false, LOOK_DWELL_MS,
   kind==LOOK_CHANNEL ? routerKnownChannel : 0, cfg.ssid.c_str())` — the
   `ssid` argument (WiFiScan.h:39-40) makes the probe directed, so a router
   with a **hidden** SSID (the app allows entering one) is found too;
   `show_hidden=true` for the same reason. `scanInFlight=true`. In
   `serviceNetwork()`, when `WiFi.scanComplete()>=0`: find `cfg.ssid`
   (strongest BSSID), `saveKnownChannel(ch)` if seen,
   `machine.scanResult(now, seen, ch)`, `WiFi.scanDelete()`. A look never
   changes the mode (D19). Timeout 5 s → treated as not seen.
2. User scan (`/api/wifi/scan`) reuses the same scan code with `channel=0`
   and the 0.16.5 result snapshotting (`scanEntries`, dedupe by SSID keeping
   the strongest). WPA3-only detection: `auth==WIFI_AUTH_WPA3_PSK` on the
   chosen entry → `allowSae=true` for the test; otherwise `false`; a manual
   SSID not in the scan → `false` (D10). Nothing stored.
3. `ROUTER_TEST` radio: `radioApply(RADIO_APSTA_JOIN, allowSae)`. On
   `TR_OK`: if SETUP, write `cfg.router=true, cfg.ssid, cfg.wifiPassword`,
   `saveSettings()`; then `radioApply(RADIO_STA, allowSae)` — the AP stops
   here and only here. On any other result: `WiFi.STA.disconnect(false, 0)`,
   `radioApply(RADIO_APSTA_IDLE, false)` (restores WPA2 if SAE was tried; this
   is the one case with an `WIFI_OFF` cycle, F11).
4. `/api/wifi/test` and `/api/wifi/return` routes and the
   `state/test/routerBack` keys in `/api/live`.
5. `phoneJoined` is read once per `serviceNetwork()` pass from
   `WiFi.softAPgetStationNum()`; a RETURN test that sees `EV_AP_STA_JOIN` is
   aborted as in 3.5 (D21).
6. After every test that ends in STANDALONE: if `WiFi.channel() !=
   apChannelNow`, call `startAP()` again (3.5). Count it as `apStarts` so the
   report shows it; the bench check "apStarts==1 after a wrong code" below
   therefore applies only to a router on channel 6 — for a router on another
   channel expect `apStarts==2` and the phone rejoined within 10 s.

Acceptance (bench): wrong password → within 25 s the app's scan page is back,
own Wi-Fi never went away (phone stays joined; `apStarts==1`), `wifi` report
`test: setup wrong-code`; right password → `test: setup ok`, AP off, router IP,
`cfg.router` saved; power the router off for 5 min → standalone at +2 min,
back within 90 s of the router when no phone is joined; with a phone joined
EngineLink stays standalone (no channel hop, no drop) and the report shows
`routerBack: true` until the phone leaves or "Switch now" is tapped. Failed tests end with `WiFi.STA.disconnect(false, 0)`
(the non-waiting form; the core's `connect()` path uses `disconnect(true, 1000)`,
which waits up to 1 s, and must not be reached from a failed test).

### 5.5 Stage 5 — Rapid boots, rollback, serial and SD log texts

1. D9 counter (3.11) with a `boots3` counter in the report.
2. Rollback: verify `EngineLink.ino:601` still confirms in STANDALONE
   (`apStarted`) and ROUTER (`WiFi.status()==WL_CONNECTED`); contract test
   asserts the line is unchanged.
3. `serialcmd.ino`: the boot line uses the state name; `help` unchanged.
4. `serviceWifiLog()` logs state changes (one line each) in addition to events.

Acceptance: power-cycle 3× within 10 s with a router saved → `state
standalone` and LED S_ROUTER; a normal boot afterwards → router again.
OTA to a build with a deliberately broken `startAP()` (test build only) rolls
back after 60 s.

### 5.6 Stage 6 — Report, cleanup, docs

1. `netreport.ino`: field list 3.9; remove `appendLinkJson`, `linkTuneTask`,
   the radio-test block; delete `linktune.cpp/.h`, `netpolicy.h`,
   `routerguard.h` and their tests; `test_router_throughput_guard.py` loses the
   `linkTuneTask`/`runRadioTest`/`radioTestWanted` asserts (keep the rest) and
   its comment "(used by the rescue return rule)" at line 32 becomes "(for the
   reports and the HTTP self-check; the Wi-Fi logic does not read it, D21)".
2. `diagnosis.ino` wifi block per 3.9.
3. `HANDOVER.md`: replace the "Wi-Fi" paragraph and the 0.15.0 rules with the
   four states and link this plan; code-layout table rows for `wifi.ino`,
   `netsvc.ino`, `wifistate.h`, `txpower.h`; drop `netpolicy.h`, `linktune`,
   `routerguard.h` rows. `docs/STATUS.md`: Wi-Fi rows say "0.17.0 untested"
   until 5.8 is done. `docs/TODO.md`: move "Wi-Fi 2.0" to "released in 0.17.0,
   boat test pending" with the 5.8 list.

Acceptance: `wc -l wifi.ino < 300`, `netreport.ino < 260`, no reference to
removed symbols (`grep -rn "linkTune\|netpolicy\|routerGuard\|staSecMode"
firmware tools .github` empty), CI green.

### 5.7 Stage 7 — App changes (approved by the owner 2026-11-05, D20)

Minimal, in `firmware/ui/app.html` then `python3 tools/build_ui.py`. Part of
the same 0.17.0 release; the firmware works without them (routes are additive).

1. Router page: after `wifiConnect`, poll `/api/wifi/test` every second for
   25 s; show `testing` ("Testing <ssid>…"), `wrong-code` ("Wrong code or the
   router refused the login"), `not-found` ("Router not found"), `timeout`
   ("No answer from the router"), `ok` ("OK, address <ip>. Switch your phone
   to the router; the app follows."). The result is kept in `S.wifiTest` until
   the page is left.
2. "Router is back, switch now?": when `lv.net.routerBack` turns true (rising
   edge, once per app session) a toast "Router <ssid> is back" and, for as
   long as it stays true, a row on the Diagnostics Wi-Fi card and on the
   router page: "Router is back · Switch now" → confirm "Switch EngineLink to
   <ssid>? Its own Wi-Fi turns off. Move your phone to the router afterwards."
   → `POST /api/wifi/return`, then `BASE=''` and the existing `findHost()`
   recovery. No modal that reappears every second; dismissing the toast is
   enough (D21: staying is a valid choice).
3. Texts at app.html:1703 and 1719 updated to describe looks/return
   (no "until you choose Connect again", no "every 10 minutes").
4. `tools/test_mobile_ui_contract.py` keeps passing (it only pins the
   `wifiPass` input behaviour).

Acceptance: Playwright desktop and iPhone viewports render the three
texts; reduced motion unaffected (no animation added).

### 5.8 Stage 8 — Release 0.17.0 and boat test

Follow `docs/RELEASING.md`: bump `EL_VERSION` to `0.17.0`, README badge,
`docs/releases/v0.17.0.md` (what changed, what is untested, the test list
below), merge when CI is green (owner's standing instruction). Then the boat
protocol:

| # | Test | Pass criterion (from the `wifi` report / SD log) |
|---|---|---|
| T1 | Standalone, iPhone, 2 h, app open, USB **not** attached | `apLeaves` with reason 8 ≤ 1 per hour; longest "web" loop part ≤ 1 200 ms; speed test ≥ 400 kB/s; heap lowest ≥ 40 KB |
| T2 | Pick the router in the app with a **wrong** code | own Wi-Fi stays, result `wrong-code` within 25 s, `apStarts` unchanged, nothing saved |
| T3 | Pick the router with the right code | `ok`, router IP, app reachable at `http://enginelink.local` and the IP within 30 s |
| T4 | Router power off for 5 min, then on; no phone on the own Wi-Fi | `router-lost` at the drop, `standalone` at +120 s ± 5 s, `router` within 90 s of the router coming back; trace < 10 entries for the outage |
| T5 | Same as T4 but the iPhone joins the own Wi-Fi during the outage; leave the app open 15 min after the router is back, then close the app but keep the phone joined for another 15 min | stays standalone the whole 30 min (`returns==0`, phone never dropped, D21); `routerBack` true and the "Switch now" row shown; tapping it → router within 25 s. Repeat and instead turn the phone's Wi-Fi off: router within 90 s of the phone leaving |
| T6 | Boat power: router and EngineLink on together, 5 times, no phone joined | `router` within 3 min every time; if the router needs longer, `standalone` at 3 min and `router` within 2 min of the router being up (look + return); median time to IP noted |
| T7 | Power off/on 3 times within 10 s | `standalone`, LED S_ROUTER, `boots3==1`; next normal boot → `router` |
| T8 | Speed test 5× in each mode | router ≥ 400 kB/s, standalone ≥ 400 kB/s; round trips noted |
| T9 | Engines running during T4 | engine report `engineWait max` < 200 ms |
| T10 | 24 h on the router | `lostEvents`, `staDisconnects`, longest loop part, heap lowest recorded in STATUS |

Only after T1–T8 pass may `docs/STATUS.md` say "verified on the board".

### 5.9 `tools/test_wifi_contract.py` (replaces `test_wifi_simple.py`)

Static assertions (use `tools/fwsource.py`):

```
w = wifi.ino text; fw = firmware_source()
- len(w.splitlines()) < 300
- "delay(" not in w                                   (nothing blocks the loop)
- body(w,"void onWiFi(") contains no "WiFi." and no "MDNS." and no "esp_wifi_"   (D14)
- count of "WiFi.mode(" in fw == count inside body(w,"void radioApply(")         (one place changes the mode)
- body(w,"void startAP(") contains 'WiFi.softAPConfig(IPAddress(192,168,4,1)' and
  'WiFi.softAP(cfg.apName.c_str(),cfg.apPassword.c_str(),apChannelNow,false,6)'  (D2)
- body(w,"void radioApply(") contains "WiFi.setSleep(false)" and "applyTxPower(false)"  (D14)
- body(w,"void serviceNetwork(") contains "web.rebindNetwork()" and "applyTxPower(true)"  (0.16.3/0.16.4 lessons)
- "web.closeClients()" precedes every "WiFi.mode(" in radioApply (regex on the body)      (pbuf crash)
- "WiFi.scanNetworks(true," in w and "scanNetworks(false" not in fw            (async scans only)
- not re.search(r"print[f]?\([^;]*(apPassword|wifiPassword)", fw)             (never log passwords)
- "saveSettings()" not in body(webapi,"void handleWifiConnect(")               (F9: credentials saved only on TR_OK)
- "cfg.ssid=" not in body(webapi,"void handleOptions(") and not in body(webapi,"void handleWifiConnect(")   (P7/F9)
- "wifiEvents[" in body(w,"void onWiFi(") and "traceWifi(" in body(w,"void onWiFi(")   (F5: raw FIFO + trace)
- "scanNetworks(true,true,false," in body(w,"void startLook(") and "cfg.ssid.c_str()" in the same body   (hidden SSIDs)
- "elWriteBounded(" in body(webserver.h,"size_t netWrite(")                    (F1)
- "setTxTimeoutMs(0)" after "Serial.begin" in setup()                          (0.15.1)
- "wifistate.h" has no "#include <Arduino.h>", no "WiFi", no "millis("
- "esp_ota_mark_app_valid_cancel_rollback" line in loop() unchanged (string compare)  (F12)
- grep: no "netpolicy", "linkTune", "routerGuard", "staSecMode", "radioTestWanted" anywhere in firmware/ or .github/
```

---

## 6. Implementation rules for whoever builds it

- Work in the order of the stages; do not start Stage 3 before
  `test_wifistate.cpp` passes.
- Never call `WiFi.*`, `MDNS.*`, `esp_wifi_*` from the event callback, an HTTP
  handler or another task. Handlers set flags; `serviceNetwork()` acts ≥ 1.2 s
  later so the HTTP reply reaches the phone first.
- Every blocking core call (`WiFi.mode`, `WiFi.begin`, `WiFi.disconnect(true)`,
  `MDNS.begin`) happens inside `radioApply()`/`startNameService()` only, so the
  "network" loop part (`loopPartMaxMs[5]`) measures them. Target: ≤ 1 500 ms
  at a state change, ≤ 20 ms otherwise.
- No new settings, no UI changes outside Stage 7, no NVS format changes
  beyond `rlog/quick`, no change to J1708, trips, fuel or storage.
- Keep the English texts short and factual; never print a password; the
  router name is replaced in GitHub reports as today (`wifi.ino:260`).
- When something in this plan turns out wrong on the hardware, write what was
  observed into `docs/TODO.md` or `HANDOVER.md` in the same PR; do not fix it
  silently in a different way.

---

## 7. Decisions the plan made beyond the TODO, and the owner's answers (2026-11-05)

| # | Proposal | Owner | Now |
|---|---|---|---|
| P1 | Standalone runs the radio as `WIFI_AP_STA` with an idle STA instead of `WIFI_AP` | **approved** | D19; one `constexpr` reverts it |
| P2 | "Router is back, switch now?" and the router-test result text in the app | **approved** | D20; Stage 7 is part of 0.17.0 |
| P3 | A phone joined but idle for 10 min counts as "no phone" | **rejected** | D21; no idle timer anywhere in the Wi-Fi logic; a joined phone is never cut off except by "Switch now" |
| P4 | Answer Apple's `/hotspot-detect.html` so iOS treats the network as usable | **deferred** | D22; not built; revisit only if T1 shows iPhones leaving |
| P5 | Router test budget 20 s (owner said ~15 s) | no objection | 20 s: the core retries the first connect once (`STA.cpp:160-163`) and DHCP took 1.1–1.5 s on the boat |
| P6 | `apch` other than 6 stays honoured | no objection | yes; the UI still only offers 1/6/11 |
| P7 | `/api/options` (first-run flow) also goes through the router test | no objection | yes; otherwise F9 stays open on the setup screen |

Consequence of D21 the owner should know: a phone that stays associated to
the own Wi-Fi (for example an iPhone in a pocket that keeps the network) holds
EngineLink in Standalone indefinitely, so NTP, GitHub reports and "Check for
updates" wait until that phone leaves or "Switch now" is tapped. The AP drops a
phone that has really gone (no frames at all) only after the driver's
inactivity timeout (ESP-IDF default 5 min, `esp_wifi_set_inactive_time`), so a
return can lag the phone's departure by up to 5 min. Shortening that timeout
is **not** part of this plan; note it in TODO if T5 shows the lag matters.

---

## 8. Final review (2026-11-05): problems found and how the plan now handles them

Done after the owner's answers, reading the whole plan against the source and
the core once more. Each item is already folded into the sections above.

| # | Problem found | Where fixed |
|---|---|---|
| R1 | D9 (rapid boots → Standalone) conflicted with the automatic return: with no phone joined EngineLink would have been back on the router within ~2 min, defeating the owner's power-cycle. | 3.5 BOOT: `autoReturn=false` for a rapid-boot session; looks and "Switch now" still work. Scenario 12. |
| R2 | After a failed test the AP stays on the router's channel: in `WIFI_AP_STA` the driver moves the AP to the station's channel at association and leaves it there after the disconnect, so the "fixed channel 6" (D2) would drift silently. | 3.5 ROUTER_TEST exit: `startAP()` again when `WiFi.channel()!=apChannelNow`; `apChannelNow` always read from the driver. Stage 4 item 6. |
| R3 | One 202/15 declared "wrong code": a router that is slow on the first handshake (0.11.x reports: Zyxel codes 2/4/202 on the first tries) would be reported as a wrong password and, for a RETURN test, start the 5/15 min back-off. | 3.5/3.8: two auth-class failures (or one plus the 20 s budget) decide `wrong-code`; the core's own first-connect retry supplies the second attempt. Scenarios 10–11. |
| R4 | The coalesced trace ring cannot feed the state machine: repeated identical events become one entry with a count, so the machine would miss the second 202 needed for R3 and the SD log already has its own cursor on that ring. | F5: separate raw 16-entry event FIFO for the machine, overflow counted. Stage 3 item 2, contract test. |
| R5 | Looks used `show_hidden=false` and no SSID filter: a router with a hidden SSID (the app explicitly supports entering one) would never be seen, so EngineLink would never return to it. | Stage 4 item 1: directed scan with `cfg.ssid` and `show_hidden=true`. Contract test. |
| R6 | Leaving `ROUTER_LOST` for Standalone with the core's auto-reconnect still on: the station would keep joining behind the AP; a late success would be two networks (D1) or, with the stale `GOT_IP` rule, cut a joined phone off (D21). | 3.3 `radioApply()`: `setAutoReconnect(false)` **before** the mode change; 3.5 stale `GOT_IP` rule honours `phoneJoined` and `autoReturn`. Scenario 16. |
| R7 | WPA3-only router: after a boot join that needed the SAE retry, every later RETURN test would start with WPA2, fail with 210, and blip the AP for the SAE switch each time. | 3.5: `lastJoinUsedSae` (RAM only, D10 allows no NVS mode) seeds `allowSae` for RETURN tests. Scenario 20. |
| R8 | T6 as written failed on a router that boots slower than 3 min although the design handles it (fallback, look, return). | T6 criterion rewritten. |
| R9 | `test_router_throughput_guard.py` comments call `lastHttpMs` "the rescue return rule"; after D21 nothing in the Wi-Fi logic reads it. The assertions stay (reports and self-check still use it); only the comment is stale. | Stage 6 item 1: fix the comment when the file is edited anyway. |

Conflicts checked and found consistent: D1 vs D19 (idle STA is not a served
network; the only AP+STA activity is the test, which D1 allows); D3 vs 1c and
R6 (Arduino reconnect kept, bounded by `LOST_MS`, trace coalesced, switched off
when the AP comes up); D6/D20/D21 (automatic return only with no phone joined,
"Switch now" the only way a joined phone loses the own Wi-Fi, a phone joining
mid-test aborts the test); D7 vs the 5/15 min back-off (internal constants,
no setting); D10 vs the 210/211 retry (decided at connect from the driver's
answer, nothing stored in NVS); D12 (no NVS format change except the new
`rlog/quick` key); D14 (every lesson has a contract or host test in 5.9/5.2);
D16/D20 (Stage 7 is now in scope, nothing else in the app changes).

## 9. Risks that stay open after this plan

- `WIFI_AP_STA` idle STA: more RAM and a driver path the board has not run
  for long (F14, D19). Measured in Stage 3 and T1/T10; revert switch exists.
- AP channel hop during a test with a phone joined now happens only in the
  two cases the owner asked for: a SETUP test (the phone is on the own Wi-Fi
  by definition) and "Switch now". The phone drops for seconds and the app's
  `findHost()` recovers. A phone that joins during an automatic RETURN test
  aborts it (3.5), so it is never dropped by a test it did not ask for.
- A phone that never leaves the own Wi-Fi keeps EngineLink off the router
  (D21, section 7). This is the owner's choice; the reports show
  `routerBack: true` so it is visible, not silent.
- The look (`scanNetworks` on one channel) takes the radio off channel 6 for
  ~0.15 s once a minute while a phone is joined; with no channel known, a full
  scan (~2 s) every fifth minute. Phones tolerate this; T1 measures it
  (`apLeaves` with reason 8).
- iOS behaviour on networks without internet (F15) is outside the firmware's
  control.
- Arduino's immediate reconnect (D3) hammers a router that refuses EngineLink
  every 1.6 s for 2 min before Standalone; acceptable per the owner, and the
  trace no longer floods (1c).
- The 60 s rollback window confirms on `apStarted`; a new build whose AP
  comes up but whose web server is dead would be confirmed. The loopback
  self-check (`httpSelfCheckTask`) restarts the board after ~3 min in that
  case, which does **not** roll back. Known, unchanged from 0.16.5; noted for
  a later release.

## 10. Field test 2026-11-05 (0.16.5 on the boat): analysis and root-cause gate

Source: the owner's serial-console export of 2026-11-05 11:52 local (10:52
UTC) containing `/WIFIREP.TXT` (16 reports, 2026-11-04 14:11 UTC to
2026-11-05 10:48 UTC), `/WIFI.LOG` and one live `wifi` report at uptime
400 s. Owner's setup: 0.16.5 from factory settings, both J1708 channels and
NMEA 2000 connected (CAN listening off by default, `model.h:303`), 13.53 V
at the input, 3.301 V at the LDO output (DMM, static). Tests: (1) boat 12 V
only — phone joined, no web page at either address; (2) USB only — web page
opens but "Waiting for EngineLink…" / "Reconnecting"; (3) USB + 12 V — got
worse, then unreachable while the phone still showed the network. iPhone,
PC, Safari/Chrome and the home-screen app all tried; IP and `.local` tried.

Everything in 10.1–10.3 is read from the log and the source; 10.4 is
hypotheses; 10.5 lists what only a measurement can settle.

### 10.1 Telling today's tests apart from the older reports

| Reports in `/WIFIREP.TXT` | Firmware | Belongs to |
|---|---|---|
| 2026-11-04 14:11, 14:23, 14:26 UTC | 0.16.3 / 0.16.4 | yesterday, USB (8.5 dBm, "Power save ON") — old |
| 2026-11-04 14:47 UTC | 0.16.5 | yesterday, USB — old (the report quoted in HANDOVER) |
| "up 120s", reset reason 1, no phone | 0.16.5 | first boat 12 V power-on (no wall clock: no phone had set the time) |
| 2026-11-05 08:26 UTC (reason 11) | 0.16.5 | today, USB attached |
| "up 120s" reason 1, phone joined +26 s, **0 non-loopback TCP accepts** | 0.16.5 | today, boat 12 V only (test 1) |
| 2026-11-05 08:56 UTC, reason 1, speed tests 60–72 kB/s | 0.16.5 | today, 12 V (test 1, later boot) |
| "up 120s" reason 1, web stall 10 022 ms | 0.16.5 | today, 12 V |
| "up 8s", "up 120s" ×2, "up 27s", 09:25, 09:30 UTC (all reason 11) | 0.16.5 | today, USB only (test 2; the "up 8s"/"up 27s" ones are manual `wifi save`) |
| 2026-11-05 10:48 UTC and the live report at 400 s (reason 1) | 0.16.5 | today, test 3: the boot that followed connecting the boat 12 V while on USB |

Reading aid: reset reason 1 = power-on, 11 = USB/console reset. "reason 1
after 0 s" does **not** mean the previous session was short: the `alive`
marker is only written by `saveTotals()` (`storage.ino:78`), i.e. every 30 s
while an engine runs or at a clean restart; with engines off it stays 0.

### 10.2 Documented findings (from the log)

| # | Finding | Evidence |
|---|---|---|
| L1 | The AP, association and DHCP work in every boot: the phone joins within 4–42 s and gets a lease (`apDhcpLeases ≥ 1` in 13 of 14 boots with a phone). | all 5 Nov reports |
| L2 | In four boots the phone has a lease but **no TCP connection from the phone ever reaches port 80 within 2 min** (`httpExternalAccepts: 0`, `httpRequests: 2` = the loopback self-check only): the first 12 V boot and three USB boots. | "up 120s" reason 1 (+26 s join, 0 accepts); "up 120s" reason 11 ×2; 09:25 UTC |
| L3 | In two of those boots the phone asked DHCP **twice** within 1–2 s of joining (`DHCP leases assigned 2`, `x2 until +8s/+11s`). | "up 120s" and "up 27s" USB boots |
| L4 | The AP repeatedly reports the phone **leaving with reason 2 (AUTH_EXPIRE) 8–10 times in 4–20 s** for a single join (`joined 1 · left 8/10`), and each such series ends with **reason 209 (SA_QUERY_TIMEOUT)** 3–97 s after it began, followed by a new join. This happened on 0.16.4 at 8.5 dBm and on 0.16.5 at 19.5 dBm, on USB and on 12 V. | `/WIFI.LOG` 14:23:19→14:23:46, 14:26:53→14:26:59, 14:29:08→14:30:45, up 100s→123s, 09:20:21→09:20:24, 09:35:04→09:35:08 UTC; reports "left 11 (last code 2)", "left 8 (last code 2)" |
| L5 | The phone also leaves by itself (reason 8, "station leaving") 7–45 s after joining in six boots, and once with reason 3 at +171 s. | 09:29:14, 09:29:31, 09:25:21, 09:21:08, 09:23:31, 10:49:02 UTC |
| L6 | Every boot in which the phone exchanged data with the server shows the main loop blocked in the **web** part for 5.6–10.0 s (`web 5614 / 5773 / 6615 / 6675 / 9757 / 10 022 ms`), and the engine task waited the same time. The blocked time is a *consequence* of the dead link (F1: `NetworkClient::write()` retries 10 × 1 s while the phone does not acknowledge). | reports 08:26, 08:56, 09:30, 10:48 UTC and the 10 022 ms boots |
| L7 | The two speed tests that completed on 12 V reached **60 and 72 kB/s at 215–307 ms round trip** on the own Wi-Fi, against 650 kB/s (0.13.4, 2 Nov) and 265–346 kB/s at 91–99 ms (0.15.0, 4 Nov morning, USB). | 08:56 UTC |
| L8 | The driver reports 19.5 dBm, the self-check never failed more than once (`httpSelfFails ≤ 1`), heap lowest 76–78 KB, no HTTP parse failures, `httpRebinds: 1`, `apStarts 2` (one real start; the counter counts the config event twice). | all 5 Nov reports |
| L9 | No brownout (reason 9), watchdog (5/6/7) or crash (4) reset in any report today. The 10:46 UTC boot is a **power-on reset (reason 1) of a board that was on USB** at the moment the boat 12 V was connected (test 3), if the owner's sequence is as described. | 10:48 UTC report, `Recent restarts` |
| L10 | On the 0.16.3/0.16.4 reports (yesterday) the AP ran at 8.5 dBm with the station power-save flag on; the reason-2/209 series occurred there too. | 14:23, 14:26 UTC |

### 10.3 What the source says about these findings

- **The Wi-Fi driver is not stopped by the firmware's stalls.** The driver and
  lwIP run in their own tasks on core 0; the blocked main loop (L6) runs on
  core 1 and only holds `web.handleClient()`. Beacons, association, DHCP and
  TCP acknowledgements are handled by the driver whether the loop is blocked
  or not. L4/L5 are therefore not caused by L6; L6 is caused by whatever
  makes the phone stop acknowledging (the link dies first, then the write
  blocks). F1 remains a real defect: while blocked, nothing else in the loop
  runs and the app shows "Reconnecting".
- **L2 means the downlink (EngineLink → phone) fails while the uplink works.**
  Auth, association and DHCP Discover are phone → EngineLink and succeed;
  the DHCP Offer/Ack, the TCP SYN-ACK and the SA Query frames are
  EngineLink → phone. A second DHCP request (L3) is what a phone does when
  the first Ack did not arrive; zero accepted TCP connections after a lease
  is a SYN-ACK that never arrived; a SA Query timeout (L4, 209) is a frame the
  phone never answered. Three independent symptoms point the same way.
- **PMF is always on for the softAP in this core.** ESP-IDF 5.5
  (`esp_wifi_types_generic.h:505`): `capable` is "Deprecated … Device will
  always connect in PMF mode if other device also advertises PMF
  capability". iPhones and Windows PCs advertise PMF. The reason-209 events
  are the AP dropping a PMF association after an unanswered SA Query, which
  the AP sends when the phone tries to (re)associate while the AP still holds
  the old association. L4 is therefore the phone re-joining a link it
  considers dead while EngineLink considers it alive; the reason-2 series is
  the AP invalidating the stale authentication each time the phone retries.
  This is a *mechanism*, not the cause: the cause is why the phone stops
  hearing EngineLink.
- **0.16.2 removed four AP radio settings that every working version had.**
  `git show d82911b:firmware/EngineLink/wifi.ino` (0.15.1) versus
  `wifi.ino` today:

  | Setting | 0.12.15–0.15.1 | 0.16.2–0.16.5 |
  |---|---|---|
  | Country | `{"EU",1,13,20 dBm,MANUAL}` set explicitly | not set (driver default "01" world-safe) |
  | AP bandwidth | `esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20)` when not already HT20 | not set (driver default; `/api/diag` → `wifi.bw` prints the actual value) |
  | AP inactive time | `esp_wifi_set_inactive_time(WIFI_IF_AP, 600)` | not set (default 300 s) |
  | Power save | `WiFi.setSleep(false)` + `esp_wifi_set_ps(WIFI_PS_NONE)` | `WiFi.setSleep(false)` only (same effect) |
  | Station auto-reconnect | `setAutoReconnect(false)` before `WiFi.mode(WIFI_AP)` | not touched in AP mode |

  None of these is proven to matter, but HT20 vs HT40 is the one that changes
  what the phone has to decode (a 40 MHz beacon/data set on channel 6 spans
  channels 4–8), and it is the one difference between "worked" and "does
  not work" that the firmware controls. 10.6 step 0 reads the actual value.
- **The 0.16.0/0.16.1 failures do not clear 0.16.x.** 0.16.1 carried the
  0.12.15 Wi-Fi code and still failed on the boat on 4 Nov, which is why the
  rewrite happened; but 0.16.0 also added the TWAI/NMEA 2000 driver and the
  drive-test UI. Nothing in the log shows what 0.16.1 did on the radio (its
  reports are not in `/WIFIREP.TXT`, which starts with 0.16.3).
- **Lower TX power did not remove the symptom** (L10: 8.5 dBm on 0.16.3/
  0.16.4 showed the same reason-2/209 series). A pure "PA current droop"
  explanation would predict improvement at 8.5 dBm; it did not improve,
  though at 8.5 dBm the margin to the phone was also smaller, so this is
  weak evidence either way.
- **The symptom is not iOS-specific**: the owner saw it with a PC too (not in
  the log; owner's statement).
- **What the firmware cannot see**: the phone's RSSI as seen by the AP
  (`esp_wifi_ap_get_sta_list()` has it, the report does not print it), the
  AP's actual bandwidth/channel at run time (only in `/api/diag`, not in the
  Wi-Fi report), TX failures/retries per station (the driver exposes no
  counter), the router's channel today, the 3.3 V rail during a TX burst.

### 10.4 Hypotheses, ranked by how well they fit L1–L10

| # | Hypothesis | Fits | Does not fit / unknown | Decided by |
|---|---|---|---|---|
| H1 | **RF environment changed** (the Zyxel's automatic channel now overlaps channel 6, −39 dBm at the board on 2 Nov; or a new strong neighbour). The phone 1 m from EngineLink hears the router louder than EngineLink's frames. | L2/L3/L4 (downlink loss at the phone), L7 (0.15.0 already 3× slower RTT on 4 Nov morning than on 2 Nov when the router was on 11), "worked before". | Test 3 "got worse when 12 V was connected" without moving the board; one observation. | 10.6 step 0 (read the router's channel) and step 3 (router off 5 min — not a settings change). |
| H2 | **Power path**: the 3.3 V rail sags or is noisy during TX bursts (LDO transient response, buck ripple, dropout on a weak USB 5 V), the 12 V path back-feeds or dips the 5 V node when connected (L9), or a ground loop via USB + boat negative. Degrades TX spectrum/EVM → phone decodes EngineLink's frames poorly → L2/L3/L4/L7. | L9 (reset when 12 V connected while on USB), worse with 12 V (test 3), marine environment and age. | Also unstable on USB only (test 2); no brownout resets; 8.5 dBm did not help (L10). The LDO/buck part numbers and the ORing between USB and 12 V are not in `docs/HARDWARE.md`. | 10.6 step 4 (oscilloscope) and step 1 (bench at home). |
| H3 | **Firmware AP configuration regression in 0.16.2** (HT40 default, no country, inactive 300 s). | Exactly the versions without these settings fail; versions with them gave 650 kB/s. | 0.16.1 (old settings) also failed on 4 Nov, so at most part of the story; HT40 by default is not yet read from the board. | 10.6 step 0 (`wifi.bw`) and step 2 (install 0.13.4 from the version archive). |
| H4 | **Board/antenna degradation** (moisture, corrosion, a loose antenna area, a damaged decoupling capacitor) since 2 Nov. | "worked before", boat environment. | nothing in the log distinguishes it from H2. | 10.6 steps 1–2 (home bench with the version that worked). |
| H5 | **PMF/SA-Query interplay** makes every link loss cost 3–97 s of "connected but dead" (L4) and the phone's auto-leave (L5). | L4 timing. | It is a consequence of the link loss, not its origin; the same core ran 0.13.4 at 650 kB/s. | Only after H1–H4; if a mitigation is wanted it is `pmf_cfg.required=false` + shorter BSS idle, tested on the bench, never blind. |
| H6 | **Main-loop stalls (F1) as the root cause.** | — | Ruled out as root cause (10.3, first bullet); confirmed as an aggravator. | Stage 1a fixes the aggravation regardless. |

No hypothesis is accepted. H1 and H2 are the two with a cheap decisive test;
H3 rides on the same tests.

### 10.5 What only a measurement can settle

- The router's current 2.4 GHz channel and its signal at EngineLink's
  mounting place (read from the app's scan list or the router's status
  page; **no router setting is changed**).
- `wifi.bw` and `wifi.ch` from `http://192.168.4.1/api/diag` on 0.16.5
  (whether the AP runs 20 or 40 MHz today).
- The 3.3 V rail at the module pins and the 5 V node (buck output / USB)
  on an oscilloscope while the app is open and a speed test runs: minimum
  voltage, ripple, and what happens the instant 12 V is connected while on
  USB. Needs the LDO and buck part numbers (not in the repository) to set
  the pass limits; placeholders in step 4.
- The phone's RSSI as the AP sees it (not in the report today; added in
  10.7). Until then: the phone's own Wi-Fi signal indicator next to the
  board.

### 10.6 Troubleshooting procedure (fewest tests that separate the causes)

Do the steps in order; stop when the table at the end gives a decision.
Send the `wifi` report (USB console) or save it with `wifi save` after each
step. Each step is 5–15 min.

| Step | Where / supply / firmware | Do | Read |
|---|---|---|---|
| 0 | boat, any supply, 0.16.5 | Open Settings › Router › "Scan for networks" (standalone only; it does not connect) and note the boat router's channel and dBm; open `/api/diag` and note `wifi.bw`, `wifi.ch`. | Router channel within 4–8 ⇒ H1 likely; `bw: 40` ⇒ H3 possible. |
| 1 | **home bench**, USB from a charger (not the laptop), 0.16.5, iPhone + PC, 15 min of app use | Same test as test 2. | Stable (no `left … code 2` series, `web` part < 1 s, speed ≥ 300 kB/s) ⇒ the boat is the difference (H1/H2/H4-at-the-boat). Unstable ⇒ board or firmware (go to 2). |
| 2 | same place as step 1, **0.13.4** installed from the web installer's version list (erases settings; fine) | Same test. | Stable ⇒ firmware regression 0.14–0.16 (H3) — confirm by installing 0.15.1 and 0.16.5 again in the same session. Unstable ⇒ hardware (H2/H4): go to 4. |
| 3 | boat, 12 V only, the firmware that was stable in 1–2 | 15 min with the router on; then switch the **router off** (power, not settings) for 5 min and repeat. | Stable only with the router off ⇒ H1 (channel overlap/interference). Unstable both ways ⇒ H2 (boat power) or H4. |
| 4 | boat, oscilloscope on 3V3 (module pin, ground at the module) and on the 5 V node | (a) USB only, app open + speed test; (b) 12 V only, same; (c) on USB, connect 12 V and watch for a dip; (d) 12 V input ripple. Pass limits (placeholders until the regulator parts are known): 3V3 never < 3.0 V, ripple < 50 mVpp; 5 V node ≥ LDO dropout + 3.3 V at all times; no dip below 2.8 V on 3V3 in (c). | A fail in (b)/(c)/(d) ⇒ H2 confirmed. All pass ⇒ H4 or H1. |
| 5 | boat, 12 V, when 1–4 are inconclusive | Second phone or a laptop as the only client, 1 m from the board, 15 min. | Same series ⇒ board/environment; clean ⇒ the first phone's behaviour (L5). |

Decision table:

| Step 1 (home, 0.16.5) | Step 2 (home, 0.13.4) | Step 3 (boat, router off) | Conclusion | Next action |
|---|---|---|---|---|
| stable | — | router off helps | H1: boat RF environment | firmware: Stage 1 + 10.7 (HT20, diagnostics), keep channel 6 fixed (D2) but add the router's channel to the report so the overlap is visible; owner decides whether the standalone channel may be chosen from 1/6/11 at setup (already allowed by `/api/options`, no UI change needed). No router change (D17). |
| stable | — | no difference | H2/H4: boat power or board | step 4; hardware fix before any Wi-Fi 2.0 stage beyond Stage 1. |
| unstable | stable | — | H3: firmware regression | compare 0.13.4 → 0.16.5 AP settings (10.3 table) by reinstating them one at a time in a bench build; Stage 1 ships the result; Wi-Fi 2.0 keeps the explicit AP block (10.7). |
| unstable | unstable | — | hardware (H2/H4) regardless of place | step 4; the board, not the boat. |

Keep for the record: owner to add the LDO and buck part numbers, the USB/12 V
ORing arrangement and the decoupling at the module to `docs/HARDWARE.md`
during the next authorized change (the plan cannot set pass limits without
them).

### 10.7 Consequences for this plan

1. **Stage 0 gate (new):** no Stage 2–8 work before 10.6 has reached a row
   of the decision table. A rewrite on top of a downlink problem would be
   tested against noise and would be blamed for it.
2. **Stage 1 grows by an explicit AP radio block and link diagnostics**, all
   behaviour-neutral or restoring 0.15.1 behaviour:
   - `apRadioSetup()` in `wifi.ino`, called after every `softAP()`:
     `esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20)`,
     `esp_wifi_set_inactive_time(WIFI_IF_AP, 600)`, country as in 0.15.1
     (`{"EU",1,13,20,WIFI_COUNTRY_POLICY_MANUAL}`, only when not already set,
     because setting it restarts the AP), then `applyTxPower(false)` (the
     country resets the power — the 0.15.0 lesson). Contract test: all four
     strings in `body(w,"void apRadioSetup(")`.
   - Report and SD log print the AP's **actual** channel, bandwidth, PMF
     mode and inactive time read back from the driver, and for every joined
     station its RSSI and PHY flags from `esp_wifi_ap_get_sta_list()`
     (`wifi_sta_info_t.rssi/phy_11b/g/n/lr`), at join, at leave and in the
     2-minute report. This is the measurement 10.5 lacks.
   - `/api/diag` keeps `wifi.bw`/`wifi.ch` (used by 10.6 step 0).
   - The trace keeps reason 2 series as one entry with a count (1c already),
     and the log line names 209 as "AP: SA Query unanswered (phone did not
     hear EngineLink)" instead of the station-side router text.
3. **F1 (bounded writes) is now priority one in Stage 1**: every link loss
   costs 5–10 s of a frozen main loop today (L6), and the owner's
   "Waiting for EngineLink…" is partly that.
4. **New findings in section 2.2 numbering:** F17 = L2–L4 downlink loss with
   working uplink (cause open); F18 = PMF always on, 209 = SA Query at the
   AP; F19 = 0.16.2 dropped HT20/country/inactive-time; F20 = the Wi-Fi
   report lacks per-station RSSI and the AP's real bandwidth; F21 = a
   power-on reset when 12 V is connected while on USB (hardware, needs
   confirmation).
5. **T1 in 5.8 gains pass criteria**: `left … code 2` series = 0 per hour,
   `httpExternalAccepts > 0` within 30 s of every DHCP lease, station RSSI
   ≥ −60 dBm at 1 m.
6. **If 10.6 ends in H1 or H2/H4**, Wi-Fi 2.0 proceeds unchanged after the
   environment/hardware is fixed; the firmware changes in item 2 ship with
   it. **If it ends in H3**, the owner decides whether item 2 ships first as
   a 0.16.6 hotfix (D18 "one release" was about the rewrite, not about a
   restored setting).
7. Nothing here changes D1–D22.
