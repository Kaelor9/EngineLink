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
and looks never change the mode; see 3.3 and decision P1).

**F5 — Shared state written from the Wi-Fi event task.** `onWiFi()`
(`wifi.ino:114-130`) writes `staDownSince`, `staIpAt`, `networkIpEpoch`,
`mdnsRefreshPending`… from the system event task (core 0) while
`serviceNetwork()` (core 1) reads and clears them (`wifi.ino:224-236`). Most
are plain `uint32_t`/`bool`, not atomic by contract. → Stage 3: the callback
only appends to the trace ring (already under `traceMux`) and increments a
`volatile uint32_t wifiEventSeq`; the state machine consumes events from the
ring on the main loop.

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
the AP (P1) costs a netif and driver buffers (measure; expected < 10 KB).
→ Stage 3 acceptance: lowest free heap after 1 h ≥ 40 KB, largest block
≥ 32 KB, reported in the Wi-Fi report.

**F15 — iPhone leaves the own Wi-Fi (reason 8) after ~17 s (0.16.4 report).**
Not reproduced on 0.16.5 (phone stayed, 6 HTTP requests). Likely iOS deciding
"no internet" and preferring another known network; the firmware cannot see
this. The plan does **not** fake internet (captive-portal answers, DHCP option
114 via `WiFi.AP.enableDhcpCaptivePortal()` would make iOS open a login sheet
and is wrong for an app). → Stage 8 boat test measures it; decision P4 if it
recurs.

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
                STANDALONE with a result code.
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

### 3.3 Radio modes per state (P1, see section 7)

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
      if mode != WIFI_AP_STA: web.closeClients(); WiFi.mode(WIFI_AP_STA)
      if !apUp: startAP()        // softAPConfig + softAP(...,apChannelNow,false,6) + setSleep(false) + applyTxPower(false)
      if want == RADIO_APSTA_JOIN: WiFi.setAutoReconnect(false); WiFi.begin(ssid, pass or nullptr)
      else if STA connected or connecting: WiFi.STA.disconnect(false, 0)   // esp_wifi_disconnect(), no wait, no reconnect (auto-reconnect is off here)
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
EV_CMD_RETURN_OK                   from the app's "switch now" (P2; optional)
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
| `RETURN_IDLE_MS` | 600 000 | P3: a phone joined but no HTTP request for 10 min counts as "no phone" |
| `BIND_DELAY_MS` | 600 | HTTP/mDNS (re)bind after an IP epoch change (unchanged) |

Timers are `uint32_t` millis with wrap-safe subtraction, as today.

### 3.5 Transitions

```
BOOT:
  rapidBoots >= 3            → STANDALONE (not saved; counter cleared)       [D9]
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
      && lookAllowed(phoneJoined, appIdleMs)      → start look (scan); nextLookAt += lookEvery
  EV_SCAN_DONE(seen=true)    → if (!phoneJoined || appIdleMs >= RETURN_IDLE_MS || returnOkFromApp)
                                     → ROUTER_TEST (testKind=RETURN, saved creds)
                               else   → routerBackSeen=true (exposed to the app; P2), stay
  EV_SCAN_DONE(seen=false)   → stay
  EV_CMD_FORGET              → clear creds, save, stay (stops the looks)

ROUTER_TEST (AP stays up, STA begin() once, auto-reconnect off):
  EV_STA_GOT_IP              → testResult=OK; if SETUP: cfg.router=true, ssid, pass → saveSettings()
                               save rch; → ROUTER  radio RADIO_STA (AP goes down here, D1)
  EV_STA_DISCONNECTED(r):
      r in {15, 202, 204, 205, 210, 211} → testResult=WRONG_CODE (205/210/211 texts differ, see 3.8)
      r == 201                           → testResult=NOT_FOUND
      other                              → keep waiting (Arduino's first_connect retry happens once)
      on a result: WiFi.STA.disconnect(false, 0) → STANDALONE; if RETURN: lookEvery = next LOOK_AFTER_FAIL step
  t >= TEST_MS               → testResult=TIMEOUT → as above
  EV_CMD_CONNECT while testing → reply 409 "A router test is running"
```

Rules that hold in every state:

- `serviceNetwork()` is the only caller of `radioApply()`, scans and
  `MDNS`; the event callback never calls a `WiFi.*` function (D14).
- After `EV_AP_START` or `EV_STA_GOT_IP`: `networkIpEpoch++`,
  `networkIpEventAt = now`; `BIND_DELAY_MS` later `web.rebindNetwork()`,
  `applyTxPower(true)`, then `startNameService()` (mDNS on whichever interface
  has the address). Ports 81/82 follow the epoch as today.
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
       "routerBack":<bool>}                                                            // new, P2
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
| 15, 204 handshake timeout; 202 `AUTH_FAIL` | `wrong-code` | "Wrong code or the router refused the login (code n)" — 202 can also be a router that refuses; the text says so (HANDOVER: 202 does not prove a wrong password) |
| 205 `CONNECTION_FAIL`, 203 `ASSOC_FAIL` | `wrong-code` | "Router refused the connection (code n)" |
| 210/211 no compatible security | `wrong-code` | "Router security not supported (code n)" — with D10 the scan decides WPA3; if the router is WPA3-only and `elSaeControlActive()` is false (built without the wrap) this is the expected result |
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
CI, and can be read on its own. Stages 1–6 are firmware; 7 is the optional
app part; 8 is the release. Order matters where stated.

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
    bool routerSaved=false, phoneJoined=false; uint32_t appIdleMs=UINT32_MAX;
    uint8_t knownChannel=0; bool returnOkFromApp=false;
    // outputs
    State state=STANDALONE; Radio radio=RADIO_APSTA_IDLE; bool radioDirty=false;
    TestKind testKind=TEST_NONE; TestResult testResult=TR_NONE; uint32_t testMs=0;
    bool wantLook=false; Look lookKind=LOOK_SKIP; bool saveCreds=false; bool routerBackSeen=false;
    uint32_t looks=0, looksSeen=0, returns=0, returnsFailed=0, lostEvents=0;
    // api
    void boot(uint32_t now, bool routerSaved, bool rapidBoots);
    void event(uint32_t now, uint8_t ev, uint8_t reason);   // ev = EV_* above
    void scanResult(uint32_t now, bool seen, uint8_t channel);
    void cmdConnect(uint32_t now); void cmdForget(uint32_t now);
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
8. Look sees the router, phone joined, appIdle 30 s → stays STANDALONE, `routerBackSeen==true`; appIdle 600 s → ROUTER_TEST (P3).
9. ROUTER_TEST(RETURN) → DISCONNECTED(202) → STANDALONE, `returnsFailed==1`, next look 300 s later, then 900 s, never more (D7 bound; HANDOVER lesson "wrong password cannot keep cutting the phone off").
10. cmdConnect in STANDALONE → ROUTER_TEST(SETUP); GOT_IP → `saveCreds==true`, ROUTER. cmdConnect → DISCONNECTED(201) → TR_NOT_FOUND, `saveCreds==false`, STANDALONE (F9).
11. ROUTER_TEST: DISCONNECTED(8) at 1 s is ignored; TIMEOUT at 20 s → TR_TIMEOUT.
12. Rapid boots → STANDALONE even with router saved (D9).
13. cmdForget in ROUTER and ROUTER_LOST → STANDALONE, `routerSaved=false`, looks stop.
14. `radioDirty` is true only on state changes that change the radio (count them: boot→ROUTER_JOIN→STANDALONE→TEST→ROUTER is 3 radio changes, looks are 0) — this is the F4 test.
15. millis wrap: all timers correct across `0xFFFFFF00 → 0x100`.

Acceptance: CI green; no Arduino includes in `wifistate.h`
(`grep -L Arduino`).

### 5.3 Stage 3 — New `wifi.ino` and `netsvc.ino` wired in

1. Create `netsvc.ino`; move the clock, NTP, mDNS, reason/trace texts and the
   GitHub report scheduling (`wifi.ino:253-263`) there unchanged. The report
   scheduling condition becomes `state==ROUTER` instead of `online`.
2. Rewrite `wifi.ino` as in 3.3–3.5: `onWiFi()` only `traceWifi()` (plus the
   existing counters, which are `volatile`) and `++wifiEventSeq`;
   `serviceNetwork()` drains new trace entries into `machine.event()`,
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

1. `startLook(kind)`: `WiFi.scanNetworks(true, false, false, LOOK_DWELL_MS,
   kind==LOOK_CHANNEL ? routerKnownChannel : 0)`; `scanInFlight=true`. In
   `serviceNetwork()`, when `WiFi.scanComplete()>=0`: find `cfg.ssid`
   (strongest BSSID), `saveKnownChannel(ch)` if seen,
   `machine.scanResult(now, seen, ch)`, `WiFi.scanDelete()`. A look never
   changes the mode (P1). Timeout 5 s → treated as not seen.
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
4. `/api/wifi/test` route and the `state/test/routerBack` keys in `/api/live`.

Acceptance (bench): wrong password → within 25 s the app's scan page is back,
own Wi-Fi never went away (phone stays joined; `apStarts==1`), `wifi` report
`test: setup wrong-code`; right password → `test: setup ok`, AP off, router IP,
`cfg.router` saved; power the router off for 5 min → standalone at +2 min,
back within 90 s of the router (no phone) — at most one 20 s AP channel hop
while the phone is joined. Failed tests end with `WiFi.STA.disconnect(false, 0)`
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
   `linkTuneTask`/`runRadioTest`/`radioTestWanted` asserts (keep the rest).
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

### 5.7 Stage 7 — App changes (only with the owner's OK, D6/D16)

Minimal, in `firmware/ui/app.html` then `python3 tools/build_ui.py`:

1. Router page: after `wifiConnect`, poll `/api/wifi/test` every second for
   25 s; show `testing` ("Testing <ssid>…"), `wrong-code` ("Wrong code or the
   router refused the login"), `not-found` ("Router not found"), `timeout`
   ("No answer from the router"), `ok` ("OK, address <ip>. Switch your phone
   to the router; the app follows."). The result is kept in `S.wifiTest` until
   the page is left.
2. Diagnostics Wi-Fi card: when `lv.net.routerBack` is true, a row "Router is
   back · Switch now" → `POST /api/wifi/return` (new route, becomes
   `EV_CMD_RETURN_OK`). Confirm dialog text: "Switch EngineLink to <ssid>?
   Its own Wi-Fi turns off. Move your phone to the router afterwards."
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
| T5 | Same as T4 but the iPhone joins the own Wi-Fi during the outage and the app is used | stays standalone while the app is in use; `routerBack` true in the report; back on the router 10 min after the last request (P3) or on "Switch now" |
| T6 | Boat power: router and EngineLink on together, 5 times | `router` within 3 min every time; median time to IP noted |
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

## 7. Decisions the plan makes beyond the TODO (owner may overrule)

| # | Decision | Default in this plan | Why |
|---|---|---|---|
| P1 | Standalone runs the radio as `WIFI_AP_STA` with an idle STA instead of `WIFI_AP` | **yes** (one `constexpr` to revert) | removes every mode flip for scans and looks (F4); one network is still served (D1) |
| P2 | "Router is back, switch now?" in the app | built in Stage 7 **only if the owner says yes**; firmware exposes `routerBack` either way | D6/D16 |
| P3 | Without the prompt, a phone joined but idle for 10 min counts as "no phone", so EngineLink returns to the router | **yes** | otherwise a phone left in the cabin keeps EngineLink off the router all day; 10 min was the 0.15 force rule the owner accepted |
| P4 | If T1 still shows iPhones leaving the own Wi-Fi: answer Apple's `/hotspot-detect.html` with the "Success" page so iOS treats the network as usable | **not built**; ask first | it fakes internet reachability; the owner may prefer honesty |
| P5 | Router test budget 20 s (owner said ~15 s) | 20 s | the core retries the first connect once (`STA.cpp:160-163`) and DHCP took 1.1–1.5 s on the boat |
| P6 | `apch` other than 6 stays honoured | yes | no destructive setting change; the UI still only offers 1/6/11 |
| P7 | `/api/options` (first-run flow) also goes through the router test | yes | otherwise F9 stays open on the setup screen |

---

## 8. Risks that stay open after this plan

- `WIFI_AP_STA` idle STA: more RAM and a driver path the board has not run
  for long (F14, P1). Measured in Stage 3 and T1/T10; revert switch exists.
- AP channel hop during a return test with a phone joined: the phone drops
  for seconds; the app's `findHost()` recovers, but a user mid-tap sees a
  pause. Bounded to one 20 s test per look that sees the router.
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
