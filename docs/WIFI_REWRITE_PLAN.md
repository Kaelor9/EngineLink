# Wi-Fi 2.0: the implementation plan

Status: **plan, nothing built yet** (revised 2026-11-05, third revision). This
is the single authoritative plan. It replaces its earlier revisions (same
file, git history) and must be followed stage by stage without guessing at
architecture decisions. Where it keeps analysis and decision history, that is
marked as history; instructions are only in sections 6–11.

Read first: `HANDOVER.md` (section "Wi-Fi"), `docs/TODO.md` ("Wi-Fi 2.0"),
`docs/STATUS.md`, `CLAUDE.md`. Everything here was checked against:

| Source | Version / commit | Used for |
|---|---|---|
| EngineLink firmware | `9485843` (0.16.5 source) | audit, names, routes |
| Arduino ESP32 core | **3.3.12**, built on **ESP-IDF 5.5.5** (`Chip: … Arduino core 3.3.12 · IDF v5.5.5` in every device report) | `libraries/WiFi/src/STA.cpp`, `AP.cpp`, `WiFiGeneric.cpp`, `WiFiScan.cpp`, `libraries/Network/src/NetworkClient.cpp`, `libraries/ESP32/examples/Camera/CameraWebServer/app_httpd.cpp`, `libraries/WiFiProv` |
| Arduino core prebuilt library configuration | `esp32-arduino-lib-builder` `configs/defconfig.common` / `defconfig.esp32s3` (master; matches the 5744-byte TCP window seen on the board) | `CONFIG_LWIP_TCP_SND_BUF_DEFAULT=5744`, `CONFIG_LWIP_MAX_SOCKETS=16`, `CONFIG_LWIP_TCP_MSS=1436`, `CONFIG_HTTPD_MAX_REQ_HDR_LEN=1024`, `CONFIG_HTTPD_WS_SUPPORT=y`, `CONFIG_ESP_WIFI_ENABLE_WPA3_SAE=y`, `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y`, `CONFIG_ESP_TASK_WDT_PANIC=y`, `CONFIG_FREERTOS_HZ=1000` |
| ESP-IDF | `release/v5.5`: `components/esp_http_server/include/esp_http_server.h`, `components/esp_wifi/include/esp_wifi.h`, `esp_wifi_types_generic.h`, `components/esp_wifi/Kconfig`, `components/lwip/Kconfig`, `docs/en/api-guides/wifi.rst`; docs pages *Wi-Fi Driver*, *HTTP Server*, *Wi-Fi Provisioning* (v5.5, esp32s3) | HTTP server defaults and async handlers, scan/AP+STA/reconnect rules, PMF, BSS max idle, provisioning transport |
| Arduino ESP32 docs | *Arduino as an ESP-IDF component* (latest; "Arduino Core ESP32 version (3.3.12) is now compatible with ESP-IDF v5.5") | option B facts |
| Device reports | GitHub #95, #126, #141–#146; the owner's 2026-11-05 serial export (16 SD reports + SD log + live report) | field evidence (section 13) |

---

## 1. Engineering principle (owner, 2026-11-05) and component policy

EngineLink is built as a production embedded system from Espressif's official,
documented components and established patterns. The owner's wishes are
functional requirements; their technical realisation is chosen by this plan.
A small product state machine is acceptable; re-implementing what the driver
or framework already does correctly is not. Real hardware faults are never
hidden behind software workarounds.

Added as decisions:

| # | Decision |
|---|---|
| D23 | **Component policy.** Use, in this order: (1) the ESP-IDF component or driver API (`esp_wifi`, `esp_event`, `esp_netif`, `esp_http_server`, `mdns`, `nvs`, `esp_sntp`, FreeRTOS primitives); (2) the Arduino ESP32 core's official wrapper of it when it is thin and complete (`WiFi`, `WiFi.AP`, `WiFi.STA`, `Preferences`, `ESPmDNS`, `Update`, `HTTPClient`, `SD`, `LittleFS`); (3) EngineLink code only for product logic (states, timers, reports, logs, the engine and trip domain). Anything that pokes into a component's internals or intercepts its initialisation is an **exception**: it lives in its own file, has a contract test, is listed in section 4, and has a removal plan. |
| D24 | **Hardware validation gates merge and release.** A firmware change that touches networking is merged and released only after the bench protocol and the boat protocol in section 9.9 have been run by the owner and reported as passed. "Merge once CI is green" (HANDOVER) does not apply to such changes. |
| D25 | **Pinned toolchain.** CI and the release workflow install `esp32:esp32@3.3.12` (not "latest") and the Adafruit NeoPixel version used today; a core/IDF update is its own change with the full test protocol. |

---

## 2. Product requirements (D1–D22, owner-approved, unchanged)

The owner's decisions from `docs/TODO.md` ("Wi-Fi 2.0", 2026-11-04) and from
the 2026-11-05 answers. They are requirements; their implementation is this
plan's job (section 6).

| # | Requirement | Source |
|---|---|---|
| D1 | Four states: Standalone, Router test, Router, Router lost. Never two networks, except during the ~15 s router test. | TODO |
| D2 | Standalone: `EngineLink` / saved password, 192.168.4.1, channel 6 (saved `apch` honoured, default 6), max 6 phones. The simplest, most robust part; it is the fallback. | TODO, STATUS |
| D3 | Router: station only, the driver/core's own reconnect, fast and stable. | TODO |
| D4 | Router lost: keep retrying 2 min, then Standalone. | TODO |
| D5 | Boot with a router saved: try the router up to 3 min, else Standalone. | TODO |
| D6 | Back to the router automatically when no phone is on the own Wi-Fi; if one is, the app asks "Router is back, switch now?". | TODO |
| D7 | No settings for retry minutes or number of tries. | TODO |
| D8 | `enginelink.local` works in both modes. | TODO |
| D9 | Power off/on 3 times within 10 s → start in Standalone whatever is saved. | TODO |
| D10 | WPA2 unless the scan shows the router is WPA3-only; decided at connect, no stored security mode, no switch counter. | TODO, HANDOVER |
| D11 | Follow the router's channel as a station; never ask for router changes; own Wi-Fi fixed; remember the router's channel for the quick look. | TODO, HANDOVER |
| D12 | Upgrade keeps the saved router, code and all data. | TODO |
| D13 | Remove the radio test, channel survey, quiet-channel hints, channel/security counters, always-0 report fields; keep the USB serial commands and the SD Wi-Fi log. | TODO |
| D14 | Lessons that must stay tests: web server listens on the network that is up now (0.16.3); TX power set after the interface start and checked (0.16.4); power save off; USB printing never blocks; Wi-Fi calls only from the main loop, never from the event callback; `wifi.ino` under ~300 lines. | TODO |
| D15 | Transmit power fixed at 19.5 dBm. | HANDOVER |
| D16 | App changes only where this plan lists them (D20, section 9.8). | HANDOVER |
| D17 | Marine safety, truthful telemetry, offline first, no router changes required, privacy, "EngineLink" only. | CLAUDE.md |
| D18 | Wi-Fi 2.0 ships in **one release** (0.17.0). | TODO |
| D19 | Standalone runs the radio as `WIFI_AP_STA` with an idle station interface (P1 approved). | owner 2026-11-05 |
| D20 | App changes approved: router-test result text and the "Router is back, switch now?" prompt (P2 approved). | owner 2026-11-05 |
| D21 | **A phone joined to the own Wi-Fi is never cut off automatically.** No idle timer. Return only when no phone is joined or on "Switch now". | owner 2026-11-05 |
| D22 | Answering Apple's captive-portal probe is deferred. | owner 2026-11-05 |

Compromises this plan makes against the original wording, for approval, are
in section 11.

---

## 3. Architecture decision

### 3.1 Options compared

| | A. Arduino core 3.3.12 build, official IDF components used directly | B. ESP-IDF 5.5 project with `espressif/arduino-esp32^3.3.12` as a component | C. Native ESP-IDF |
|---|---|---|---|
| What changes | `wifi.ino`, the HTTP server, the network helpers; build, CI, installer and the other 14 `.ino` files unchanged | Build system (`idf.py`, CMake, `sdkconfig`), CI (`validate.yml`, `build-engine.yml`, `pages.yml`), the version-archive tooling, **every `.ino` becomes `.cpp` with explicit prototypes** (the component doc: "Your main.cpp should be formatted like any other sketch" — there is no `.ino` preprocessing), then the same network work as A | Everything: 16 000 lines rewritten against IDF APIs (SD, LittleFS, NeoPixel, OTA, HTTP client, NVS all re-done) |
| Official components available | All of IDF 5.5 is linked into the prebuilt core: `esp_http_server` (the core's own CameraWebServer example uses it), `esp_wifi`, `esp_event`, `esp_netif`, `mdns`, `nvs`, `esp_sntp`, `wifi/network_provisioning` (`libraries/WiFiProv`) | same, plus the IDF component manager | same |
| Configuration control (`sdkconfig`) | **none**: the prebuilt libraries fix `CONFIG_LWIP_TCP_SND_BUF_DEFAULT=5744`, `CONFIG_LWIP_MAX_SOCKETS=16`, `CONFIG_ESP_WIFI_ENABLE_WPA3_SAE=y`, Wi-Fi buffer counts, BSS max idle off | full: the two exceptions in section 4 (TCP window, WPA3 switch) become config lines / normal init code | full |
| Maintenance | Core updates arrive as board-package versions (pinned by D25); Arduino `.ino` concatenation keeps its known quirks (prototype generator, globals shared by concatenation) | IDF + component versions pinned in `idf_component.yml`; compile-time strictness; longer CI (toolchain install), two build worlds to understand | one world, but a rewrite of a working domain layer nobody asked for |
| Risk now | smallest diff; the open hardware/RF question (section 13) is not confounded | a large mechanical conversion and a new pipeline *before* the network work, while the root cause of the field failure is still open; every subsystem needs re-validation on the boat | highest |
| Testability | host tests for pure logic (as today); bench + boat | same, plus IDF unit-test framework possible | same |

Verified facts behind the table: Arduino core 3.3.12 ↔ IDF 5.5 compatibility
(Arduino docs note); `esp_http_server` in the prebuilt core
(`app_httpd.cpp` includes `esp_http_server.h`, calls `httpd_start`); the
lib-builder `defconfig.common` values listed in the source table;
`wifi_provisioning` uses protocomm/protobuf over its own HTTP endpoints and
is driven by Espressif's phone apps or `esp_prov` (IDF docs, *Wi-Fi
Provisioning*); IDF's Wi-Fi guide makes reconnection "the application's
responsibility" and recommends an application reconnection strategy
(`wifi.rst`, "Wi-Fi Reconnect").

### 3.2 Decision

**Option A, with the component policy D23 applied strictly, and with option B
as the planned next step under measured triggers.**

Why A: it delivers every official component the design needs without a
build-system migration; it is the smallest technically sound change to a
16 000-line firmware that must keep reading engines, trips and NMEA 2000
unchanged; and it keeps the field problem of section 13 isolated from a
toolchain change. Switching to B now would not be "for the technology", but it
would be *before* its need is measured, and it would convert and re-validate
every file for a problem that may be hardware.

What A cannot do, kept as fenced exceptions (section 4): the lwIP TCP send
window is compile-time (5744 bytes) and WPA3 cannot be switched off per
connection by any IDF or core API, so `netboost.cpp` and `wifiwpa3.cpp` stay
until B.

**Triggers for B** (owner decides when one is met): (1) a core/IDF update
breaks `netboost.cpp`'s use of `lwip/priv/tcp_priv.h`; (2) the router
throughput criterion in section 7 cannot be met without `netboost.cpp` and the
owner wants the workaround gone; (3) a needed Wi-Fi/lwIP/`sdkconfig` option
(BSS max idle, buffer counts, console behaviour) is required by a field
finding. B is then its own plan: `.ino → .cpp`, `idf.py`, pinned
`espressif/arduino-esp32^3.3.12`, CI and installer pipeline, full protocol.

Option C is rejected: a whole-firmware rewrite with no requirement behind it;
"native IDF for networking only" inside an Arduino build is option A.

---

## 4. Component map: what each need is served by

| Need | Today (0.16.5) | Wi-Fi 2.0 | Status under D23 |
|---|---|---|---|
| HTTP server for the app and API | Arduino `WebServer` + `webserver.h` (connection pool, keep-alive header rewrite, write hooks) + two raw-socket listeners (ports 81, 82) + a loopback self-check task + `NetworkClient::write()` blocking the main loop 5–10 s (F1) | **`esp_http_server`** (IDF 5.5): one instance on port 80 in its own task, `httpd_req_async_handler_begin()` for long transfers, `lru_purge_enable`, `send_wait_timeout`; a 250-line adapter keeps the handler API (`web.arg()`, `web.send()`, …) so `webapi.ino`'s 83 routes change little (6.6) | official |
| Wi-Fi mode, netifs, DHCP server, hostname | Arduino `WiFi` | Arduino `WiFi` / `WiFi.AP` / `WiFi.STA` (thin wrappers over `esp_wifi` + `esp_netif`) | official wrapper |
| Scan with SSID/channel filter, AP bandwidth, inactive time, country, TX power | `WiFi.scanNetworks(…)`, nothing else since 0.16.2 | `esp_wifi_scan_start` via `WiFi.scanNetworks(async, show_hidden, passive, dwell, channel, ssid)`, `esp_wifi_set_bandwidth`, `esp_wifi_set_inactive_time`, `esp_wifi_set_country`, `esp_wifi_set_max_tx_power` | official |
| Station reconnect | the core's auto-reconnect (immediate `disconnect()`+`connect()` on every drop, `STA.cpp:164-172`) | same, bounded by the product timers (D3, D4); IDF leaves the strategy to the application | official |
| Fallback, return, boot join, router test | ad-hoc timers in `serviceNetwork()` | **`wifistate.h`**: one small product state machine, host-tested (allowed by the owner) | product logic |
| Events from the driver to the loop | callback writes shared globals | `WiFi.onEvent` → **FreeRTOS queue** (`xQueueSend` from the callback, `xQueueReceive` in the loop); the coalesced trace ring stays for the report | official primitive + product log |
| Setup of a router (credentials, result) | HTTP endpoints of the app | HTTP endpoints of the app, same semantics as the IDF provisioning manager (`WIFI_PROV_STA_AUTH_ERROR` / `AP_NOT_FOUND` / success; AP stays up during the attempt; credentials saved on success). `wifi_provisioning`/`network_provisioning` itself is **rejected**: protobuf over protocomm, meant for Espressif's phone apps or `esp_prov`, stops the SoftAP on success; a browser client would be a custom protobuf implementation | product logic, pattern from the official component |
| mDNS | `ESPmDNS` (IDF `mdns`) | same | official wrapper |
| Settings / NVS | `Preferences` | same | official wrapper |
| Time | `configTime` (IDF `esp_sntp`), phone, GPS | same | official wrapper |
| OTA | `Update` + `HTTPClient` + `NetworkClientSecure` | same; upload body read with `httpd_req_recv` | official wrapper |
| Watchdog, core dump, restart log | `esp_task_wdt` on the loop, core dump to flash (prebuilt config), `crashinfo.cpp` | same | official |
| Larger TCP send window per connection | `netboost.cpp` pokes `tcp_pcb->snd_buf` under `LOCK_TCPIP_CORE()` | **exception** (A only; a `CONFIG_LWIP_TCP_SND_BUF_DEFAULT` line under B) | fenced: own file, kill switch `EL_NETBOOST`, contract test, measured 7× on the router (#116 → #126), approval P8 |
| WPA2 on WPA2/WPA3 routers, WPA3 only when required | `wifiwpa3.cpp` + `-Wl,--wrap=esp_wifi_init` clears `CONFIG_FEATURE_WPA3_SAE_BIT` in `wifi_init_config_t.feature_caps` (a documented field) | **exception** (A only; under B it is three lines before the application's own `esp_wifi_init()`) | fenced: own file, CI objdump check, D10 |
| Radio test, channel survey, pings, back-off tables, router guard | `linktune.cpp/.h`, `netpolicy.h`, `routerguard.h` | **removed** (D13) | — |
| Diagnostics: report, SD log, serial commands | `netreport.ino`, `serialcmd.ino` | kept, slimmed to fields that exist; per-station RSSI/PHY from `esp_wifi_ap_get_sta_list()` added | product |

---

## 5. Audit of 0.16.5 (history; what each finding becomes)

Line numbers refer to commit `9485843`.

| # | Finding | Evidence | Resolved by |
|---|---|---|---|
| F1 | The 5–10 s main-loop stall is the core's `NetworkClient::write()`: `select()` 1 s × `WIFI_CLIENT_MAX_WRITE_RETRY=10` when the phone stops acknowledging (`NetworkClient.cpp:388-440`); the loop (LED, SD, timers, state logic) stands still. | 0.16.5 reports `web 5614–10 022 ms` | Stage 2: handlers run in the `esp_http_server` task; the loop never touches sockets. No bounded-write workaround. |
| F2 | The core's immediate reconnect produces 201+36 pairs every ~1.57 s; `traceWifi()` only merges identical repeats, so the first cause is pushed out (#126). | `STA.cpp:164-172`, `wifi.ino:4-19` | Stage 1c (fold the pair in the trace). D3 keeps the reconnect. |
| F3 | The 10-minute return is a blind switch: AP off for up to 2 min while the router may still be away (`wifi.ino:233-236`). | | Stage 5: one-channel look while the AP stays up. |
| F4 | Mode flips for scans (`WIFI_AP → WIFI_AP_STA → WIFI_AP`) and `WIFI_OFF` on every switch; 0.16.0's "network disappeared" never root-caused. | `wifi.ino:169,193-220` | D19: Standalone is `WIFI_AP_STA` with an idle station; one function changes the mode. |
| F5 | The event callback writes non-volatile shared state read by the loop. | `wifi.ino:114-130` vs `224-236` | Stage 4: callback → FreeRTOS queue + trace ring only. |
| F6 | ~60 dead globals from 0.12–0.15 and report lines describing removed behaviour ("Reconnect attempts by EngineLink: 0 (pause 1, 2, 4, 8, then 15 s)"). | `EngineLink.ino:285-372`, `netreport.ino` | Stage 7. |
| F7 | TX power check accepts the driver's unset 20 dBm as 19.5 dBm (`abs(q-78)<=2`); router mode never sets the power. | `wifi.ino:99-107`, #146 | Stage 1b. |
| F8 | Boot and loss both use 120 s; D5 says 3 min at boot. | `wifi.ino:228` | Stage 4. |
| F9 | A wrong router password is saved before it is tried and locks the owner out for 2–3 min per boot. | `webapi.ino:812-837` | Stage 5 (router test; save on success). |
| F10 | `handleCfg()` restarts mDNS inside the request; handlers rely on a 1.2 s delay before radio changes. | `webapi.ino:727` | kept as the rule "reply first, act from the loop" (6.3). |
| F11 | `elAllowSae` takes effect only through `esp_wifi_init()`, i.e. after `WIFI_OFF`. | `WiFiGeneric.cpp:319-334` | 6.2 `radioApply()`: only when the flag must change. |
| F12 | Rollback confirmation needs `apStarted` or a router IP (`EngineLink.ino:601`). | | kept; contract test. |
| F13 | Scans move the radio off channel 6; a one-channel look is ~0.15 s. | | Stage 5 rules (ported from `netpolicy.h` tests). |
| F14 | Lowest free heap 55–78 KB; an idle STA costs RAM. | #126, #146, 5 Nov | criterion in section 7. |
| F15 | iPhone leaves the own Wi-Fi by itself (reason 8). | 0.16.4, 5 Nov | measured in 9.9; D22. |
| F16 | Contracts others depend on (`networkIpEpoch`, `apStarted`, `routerConfigured()`, `routerHold`, `lastHttpMs`, …). | | 6.5. |
| F17 | Uplink works, downlink to the phone fails (0 TCP accepts after a lease, DHCP asked twice, SA Query unanswered). Cause open. | section 13 | Stage 0 gate. |
| F18 | The softAP always runs PMF on this core (IDF 5.5: `wifi_pmf_config_t.capable` "Deprecated … will always connect in PMF mode if other device also advertises PMF capability"); a phone re-joining a link it considers dead costs 3–97 s (reason 2 series ending in 209). | `esp_wifi_types_generic.h:505`, 5 Nov log | mechanism, not cause; `bss_max_idle_cfg` is `CONFIG_WIFI_BSS_MAX_IDLE_SUPPORT`-gated (off in the prebuilt core) — a B-only option. |
| F19 | 0.16.2 dropped `esp_wifi_set_bandwidth(AP, HT20)`, the explicit country and `esp_wifi_set_inactive_time(AP, 600)` that every working version had. | `d82911b:wifi.ino:225-262,452-470` | Stage 1a `apRadioSetup()`. |
| F20 | The report lacks per-station RSSI/PHY and the AP's real bandwidth/channel/PMF. | | Stage 1d. |
| F21 | A power-on reset occurred when the boat 12 V was connected while the board ran on USB (hardware, to be confirmed). | 5 Nov 10:48 UTC report | Stage 0 step 4. |

---

## 6. Target design

### 6.1 Files after the rewrite

| File | Purpose | Size target |
|---|---|---|
| `wifistate.h` (**new**, no Arduino includes) | the product state machine: states, timers, inputs, actions; host-tested by `tools/test_wifistate.cpp` | ≤ 220 lines |
| `wifi.ino` (**rewritten**) | `onWiFi()` → queue + trace; `radioApply()` (the only place that calls `WiFi.mode/softAP/begin/disconnect`); `apRadioSetup()`; scan and look; `serviceNetwork()` feeds `wifistate` and performs its actions; TX power | ≤ 300 lines (D14) |
| `httpd.ino` (**new**) | `esp_http_server` start/stop, the `Web` adapter class (6.6), the async transfer worker, static page with ETag/gzip, CORS, URL decoding | ≤ 300 lines |
| `netsvc.ino` (**new**) | clock helpers and NTP, mDNS (`startNameService`, `validHostName`), GitHub device-report scheduling, reason/trace texts — moved verbatim from `wifi.ino`/`serviceNetwork()` | ~120 lines |
| `netreport.ino` (**slimmed**) | report text + JSON with the fields of 6.8 only | ≤ 220 lines |
| `txpower.h` (**new**, tiny) | `elTxPower[5]`, `elTxName[5]` (from `linktune.cpp`) | 12 lines |
| `netboost.cpp/.h` (**kept, fenced**) | unchanged; `#if EL_NETBOOST` kill switch (default on); called from the adapter's open hook | exception |
| `wifiwpa3.cpp/.h` (**kept, fenced**) | unchanged | exception |
| removed | `webserver.h`, `netpolicy.h`, `routerguard.h`, `linktune.cpp/.h`, `tools/test_netpolicy.cpp`, `tools/test_routerguard.cpp`, `tools/test_wifi_simple.py` (→ `tools/test_wifi_contract.py`), the `liveHttpTask`/`speedHttpTask`/`httpSelfCheckTask`/`linkTuneTask` tasks and `NetworkServer liveServer/speedServer` | |

### 6.2 States and radio modes (D1, D19)

```
STANDALONE      own Wi-Fi on (AP on apChannel), STA idle (never begin()).
                With a router saved: a "look" every LOOK_MS (60 s).
ROUTER_TEST     own Wi-Fi stays on; STA tries the router once (≤ TEST_MS=20 s).
                Entered from STANDALONE by /api/wifi/connect (SETUP) or by a
                look that saw the router (RETURN). A RETURN test starts only
                when no phone is joined (D21) or after "Switch now" (D20).
                Ends in ROUTER (AP off) or back in STANDALONE with a result.
ROUTER_JOIN     boot only (D5): STA only, core reconnect on, up to
                BOOT_JOIN_MS=180 s without an IP → STANDALONE.
ROUTER          STA only with an IP. Core reconnect on.
ROUTER_LOST     STA only, no IP. Core reconnect keeps trying. After
                LOST_MS=120 s without an IP → STANDALONE. An IP → ROUTER.
```

| State | `WiFi.getMode()` | AP | STA |
|---|---|---|---|
| STANDALONE | `WIFI_AP_STA` | up, `apChannelNow` | started, idle; used only for scans/looks |
| ROUTER_TEST | `WIFI_AP_STA` | up (the driver moves it to the router's channel when the STA associates: *"the channel of the external AP … has higher priority over the ESP AP channel"*, `wifi.rst` §station/AP coexistence) | `begin()`, auto-reconnect **off** |
| ROUTER_JOIN / ROUTER / ROUTER_LOST | `WIFI_STA` | off | `begin()`, auto-reconnect **on** |

`constexpr bool STANDALONE_APSTA = true;` in `wifi.ino` reverts D19 to
`WIFI_AP` with a temporary `WIFI_AP_STA` for scans if the boat test demands it.

Only `radioApply(RadioMode want, bool allowSae)` changes the mode:

```
RadioMode: RADIO_APSTA_IDLE, RADIO_APSTA_JOIN, RADIO_STA
radioApply(want, allowSae):
  if (allowSae != elAllowSae) { httpdSuspend(); WiFi.mode(WIFI_OFF); elAllowSae = allowSae; }   // F11, rare
  WiFi.persistent(false); WiFi.setHostname(cfg.hostName)            // before any interface starts
  switch want:
    RADIO_APSTA_IDLE / RADIO_APSTA_JOIN:
      WiFi.setAutoReconnect(false)                                   // FIRST: coming from ROUTER_LOST the core is still retrying
      if mode != WIFI_AP_STA: httpdSuspend(); WiFi.mode(WIFI_AP_STA)
      if !apUp: startAP(); apRadioSetup()
      if want == RADIO_APSTA_JOIN: WiFi.begin(ssid, pass or nullptr)
      else if STA connected or connecting: WiFi.STA.disconnect(false, 0)   // esp_wifi_disconnect(), no wait
    RADIO_STA:
      if mode != WIFI_STA: httpdSuspend(); WiFi.mode(WIFI_STA)       // takes the AP down
      WiFi.setSleep(false); WiFi.setAutoReconnect(true); applyTxPower(false)
      if not connected/connecting with the same ssid: WiFi.begin(ssid, pass or nullptr)
  ++radioApplies  (reported)
```

`httpdSuspend()` = `httpd_sess_trigger_close()` on every open session before
the driver stops (the 0.16.x `closeClients()` lesson: received data waiting
in a socket points into driver buffers; `webserver.h:19-21`). The listener
itself needs no rebind: `esp_http_server` binds `INADDR_ANY`.

`startAP()` keeps the 0.16.5 line exactly:
`WiFi.softAP(cfg.apName.c_str(),cfg.apPassword.c_str(),apChannelNow,false,6)`
after `WiFi.softAPConfig(192.168.4.1, 192.168.4.1, 255.255.255.0)`, with
`apChannelNow = (cfg.apChannel>=1&&cfg.apChannel<=11)?cfg.apChannel:6`.

`apRadioSetup()` (restores F19, official calls only):
`esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20)`;
`esp_wifi_set_inactive_time(WIFI_IF_AP, 600)`; country
`{"EU",1,13,20,WIFI_COUNTRY_POLICY_MANUAL}` via `esp_wifi_set_country()` only
when `esp_wifi_get_country()` differs (setting it restarts the AP);
`WiFi.setSleep(false)`; then `applyTxPower(false)` (the country resets the
power — the 0.15.0 lesson).

### 6.3 Inputs, timers and transitions (`wifistate.h`)

Events, delivered from the callback through a FreeRTOS queue of 16 `WifiEv
{uint8_t type, reason; uint32_t ms;}` (overflow counted, never blocks the
callback):

```
EV_AP_START, EV_AP_STOP, EV_AP_STA_JOIN, EV_AP_STA_LEAVE(reason), EV_AP_STA_IP,
EV_STA_CONNECTED, EV_STA_GOT_IP, EV_STA_LOST_IP, EV_STA_DISCONNECTED(reason),
EV_SCAN_DONE(routerSeen, routerChannel, routerAuth)
EV_CMD_CONNECT(ssid, pass)   from /api/wifi/connect (reply sent first)
EV_CMD_FORGET                from /api/wifi/forget
EV_CMD_RETURN_OK             from /api/wifi/return (D20)
EV_RAPID_BOOTS               at boot (D9)
```

Constants (`constexpr`, no settings — D7):

| Name | Value | Meaning |
|---|---|---|
| `BOOT_JOIN_MS` | 180 000 | D5 |
| `LOST_MS` | 120 000 | D4 |
| `TEST_MS` | 20 000 | router test budget (P5) |
| `LOOK_MS` | 60 000 | look for the router from Standalone |
| `LOOK_AFTER_FAIL_MS` | 300 000, then 900 000 | after a failed return; reset to `LOOK_MS` after a success or a new `/api/wifi/connect` |
| `LOOK_FULL_EVERY` | 5 | every fifth look scans all channels when a channel is known; always full when unknown and no phone joined |
| `LOOK_DWELL_MS` | 120 | active dwell per channel |
| `RAPID_BOOT_WINDOW_MS`, `RAPID_BOOT_COUNT` | 10 000, 3 | D9 |
| `BIND_DELAY_MS` | 600 | mDNS re-announce after an IP epoch change |

There is **no** idle timer (D21): `phoneJoined` = `WiFi.softAPgetStationNum() > 0`.

```
BOOT:
  rapidBoots >= 3            → STANDALONE (not saved; counter cleared); autoReturn=false for this boot:
                               looks still run so the app can show "Switch now", no automatic RETURN test.
  else routerConfigured()    → ROUTER_JOIN   radio RADIO_STA
  else                       → STANDALONE    radio RADIO_APSTA_IDLE

ROUTER_JOIN:
  EV_STA_GOT_IP              → ROUTER; lastJoinUsedSae=allowSae; save rch if changed; lookEvery=LOOK_MS
  EV_STA_DISCONNECTED(210|211) and !saeTried and saeAvailable
                             → saeTried=true; radioApply(RADIO_STA, allowSae=true)         [D10]
  t >= BOOT_JOIN_MS          → STANDALONE  ("router not reached in 3 min")

ROUTER:
  EV_STA_DISCONNECTED / EV_STA_LOST_IP → ROUTER_LOST (lostAt = now; ++lostEvents)
  EV_CMD_FORGET              → cfg.router=false, ssid="", pass=""; save; lastJoinUsedSae=false; STANDALONE
  EV_CMD_CONNECT             → never reaches the machine (handler replies 409 unless STANDALONE)

ROUTER_LOST:
  EV_STA_GOT_IP              → ROUTER
  t - lostAt >= LOST_MS      → STANDALONE  ("router lost for 2 min")
  EV_CMD_FORGET              → as in ROUTER

STANDALONE:
  EV_CMD_CONNECT(ssid,pass)  → ROUTER_TEST (SETUP; credentials in RAM only; allowSae from the scan entry: WPA3-only → true)
  routerConfigured() && t >= nextLookAt && lookKind(lookNo, knownChannel, phoneJoined) != LOOK_SKIP
                             → start look; nextLookAt += lookEvery
  EV_SCAN_DONE(seen=true)    → if (!phoneJoined && autoReturn) → ROUTER_TEST (RETURN, saved creds, allowSae=lastJoinUsedSae)
                               else routerBackSeen=true, stay                                      [D6, D21]
  EV_SCAN_DONE(seen=false)   → routerBackSeen=false, stay
  EV_CMD_RETURN_OK           → if routerBackSeen → ROUTER_TEST (RETURN, returnOkFromApp=true) else ignored
  EV_STA_GOT_IP (stale)      → if (!phoneJoined && autoReturn) → ROUTER
                               else WiFi.STA.disconnect(false,0); routerBackSeen=true; stay        [D21]
  EV_STA_DISCONNECTED(any)   → ignored (logged): late events from the core's first-connect retry or our own disconnect
  EV_CMD_FORGET              → clear creds, save, routerBackSeen=false, stay

ROUTER_TEST (AP up, STA begin() once, auto-reconnect off):
  EV_STA_GOT_IP              → testResult=OK; lastJoinUsedSae=allowSae; if SETUP: cfg.router=true, ssid, pass →
                               saveSettings(); save rch; → ROUTER radio RADIO_STA (the AP goes down here, D1)
  EV_STA_DISCONNECTED(r):
      r in {210,211} and !saeTried and saeAvailable → saeTried=true; radioApply(RADIO_APSTA_JOIN, allowSae=true); keep testing
      r in {15,202,203,204,205,210,211} → ++authFails; authFails >= 2 → WRONG_CODE
                               (one auth failure is not proof — HANDOVER "202 does not prove a wrong password";
                                the core's first-connect retry, STA.cpp:160-163, supplies the second attempt)
      r == 201                 → NOT_FOUND
      other                    → keep waiting
      on a result: WiFi.STA.disconnect(false,0) → STANDALONE; if RETURN: lookEvery = next LOOK_AFTER_FAIL step;
                   if WiFi.channel() != apChannelNow: startAP() again (the AP followed the router's channel, D2)
  EV_AP_STA_JOIN while RETURN && !returnOkFromApp → abort: disconnect → STANDALONE, routerBackSeen=true, no penalty [D21]
  t >= TEST_MS               → authFails ? WRONG_CODE : TIMEOUT → as above
  EV_CMD_CONNECT / EV_CMD_RETURN_OK while testing → handler replies 409 "A router test is running"
```

Rules in every state: `serviceNetwork()` is the only caller of `radioApply()`,
scans and `MDNS`; the event callback never calls a `WiFi.*` function (D14).
After `EV_AP_START` or `EV_STA_GOT_IP`: `networkIpEpoch++`, and `BIND_DELAY_MS`
later `applyTxPower(true)` and `startNameService()`; `EV_AP_STOP` sets
`mdnsRefreshPending` only. `apChannelNow` in the report is read from
`WiFi.channel()` when the AP is up. Handlers never act on the radio: they set
`pendingCmd` + `networkSwitchAt = now + 1200` and `serviceNetwork()` turns it
into an `EV_CMD_*` when due (reply first, act later; F10). A state change
writes one line to `/WIFI.LOG` and to `Serial`, never a password.

Result texts (3.8 of the previous revision, unchanged): 201 → "Router not
found (off, out of range, or 5 GHz only)"; 15/202/204 → "Wrong code or the
router refused the login (code n)"; 203/205 → "Router refused the connection
(code n)"; 210/211 after the WPA3 retry → "Router security not supported
(code n)".

### 6.4 LED, NVS, rapid boots

LED (`ledpatterns.h` is fixed): ROUTER → S_LIVE/S_READY; ROUTER_JOIN,
ROUTER_LOST (after 15 s) and STANDALONE with a router saved → S_ROUTER;
STANDALONE without a router and ROUTER_TEST → S_LIVE/S_READY.

NVS: `enginelink/router, ssid, wpass, apch, appass, apname, host, rch`
unchanged (D12); `enginelink/smode` no longer read or written (D10);
**new** `rlog/quick` (u8): read+1 and written in `setup()` right after
`restartLogBoot()`, written 0 by `loop()` at `millis() >= 10 000`; a value
≥ 2 at boot means Standalone for this boot and the key is cleared at once. A
crash loop also lands in Standalone (safe side).

### 6.5 Names other files rely on (keep)

| Name | Users | Definition |
|---|---|---|
| `routerConfigured()` | led, updates, webapi, serialcmd, netreport | `cfg.router && cfg.ssid.length()` |
| `bool routerHold` | webapi, netreport, serialcmd, diagnosis | kept equal to `state==STANDALONE && routerConfigured()` by `serviceNetwork()` ("a router is saved but the own Wi-Fi is on") |
| `bool apStarted` | `EngineLink.ino:601` rollback, serialcmd, netreport | true on `EV_AP_START`, false on `EV_AP_STOP` or `RADIO_STA` |
| `volatile uint32_t networkIpEpoch, networkIpEventAt` | netreport, diagnosis | kept (mDNS/TX re-check timing; no listener rebind any more) |
| `uint8_t apChannelNow`, `routerKnownChannel` + NVS `rch` | webapi, serialcmd, storage | unchanged |
| `pendingNetwork`, `networkSwitchAt` | webapi handlers | the "reply first" mechanism (6.3) |
| `traceWifi()`, `getWifiTrace()`, `RouterTrace`, `ROUTER_TRACE_CAP`, `traceTotal` | serialcmd, netreport | unchanged API, folding improved (F2) |
| `reasonText()`, `traceText()`, `stationSecurityName()`, `validWall()`, `setClockFromUtc()`, `allowTime()`, `ntpCallback`, `elapsedMs()`, `startNameService()`, `validHostName()` | many | moved to `netsvc.ino`, unchanged |
| `applyTxPower(bool)` | wifi.ino | kept, tolerance fixed (F7) |
| `elTxPower[]`, `elTxName[]`, `txLevel`, `TX_LEVEL_FIXED` | netreport, webapi | `txpower.h` |
| `speedLog[]`, `SPEED_LOG_N` | webapi, netreport | unchanged |
| `web` (the handler-facing object) | webapi, downloads, sdexport, updates, diagnosis, gearbox, enginetest, nmea2000, led | the adapter in `httpd.ino` (6.6), same method names |
| `lastHttpMs`, `httpBusySince`, `httpHealPending`, `httpSelfFails/Heals/Restarts`, `httpBoundEpoch`, `radioTestWanted`, `linkTune*`, `linkScan*`, `netWaitBegin/End` | | **removed** |

### 6.6 HTTP server design (`httpd.ino`)

`esp_http_server`, one instance:

```
httpd_config_t c = HTTPD_DEFAULT_CONFIG();   // IDF 5.5 defaults: prio idle+5, stack 4096, 7 sockets, 8 handlers, 5 s recv/send wait
c.server_port = 80; c.core_id = 1; c.task_priority = tskIDLE_PRIORITY + 5;
c.stack_size = 8192;             // handlers build JSON Strings
c.max_uri_handlers = 96;         // 83 routes today
c.max_open_sockets = 8;          // + 3 internal = 11 of CONFIG_LWIP_MAX_SOCKETS=16; leaves mDNS, SNTP, HTTPClient, loopback
c.lru_purge_enable = true;       // a phone that walked away does not hold a slot
c.recv_wait_timeout = 5; c.send_wait_timeout = 5;   // a dead phone costs the server task ≤ 5 s, the loop nothing
c.uri_match_fn = httpd_uri_match_wildcard;
c.open_fn = elHttpdOpen;         // netboost on the new socket (exception, 4) + TCP_NODELAY
```

The `Web` adapter (≤ 250 lines) keeps the handler API so `webapi.ino` stays
readable: `on(path, method, fn)`, `arg(name)`, `hasArg(name)`, `header(name)`,
`uri()`, `send(code, type, body)`, `send_P(code, type, PGM, len)`,
`sendHeader(name, value)`, `sendContent(ptr, len)` (chunked), `client()`
replaced by `clientIsOnAp()`. Implementation: a thread-local
`httpd_req_t *cur`; query from `httpd_req_get_url_query_str()` +
`httpd_query_key_value()`; POST bodies ≤ 2 KB read with `httpd_req_recv()`
and parsed the same way (the app sends `application/x-www-form-urlencoded`,
`URLSearchParams`), with a 20-line URL decoder (`+`, `%xx`); headers via
`httpd_req_get_hdr_value_str()`; `Access-Control-Allow-Origin: *` on every
response (today's `enableCORS(true)`); the app page `/` with
`Content-Encoding: gzip`, `ETag`, 304 on `If-None-Match` as today
(`webapi.ino:922-928`).

Locking: every handler runs in the server task. The adapter takes `engineMx`
with a 2 s timeout before calling the handler (503 "EngineLink is busy" on
timeout) and **gives it back around every `httpd_resp_send*` call**, so a slow
phone never holds the engine task (today's `netWaitBegin/End`, now in one
place). The main loop keeps holding the lock while it works and releasing it
in `delay(1)` as today; the server task has the higher priority and gets it
at the next release.

Long transfers — `/api/speedtest`, `/api/rtt`, `/api/export/*.csv`,
`/api/log`, `/api/segments.csv`, `/api/backup`, `/api/nmea/bus.csv`,
`/api/trip/series`, `/api/event/series`, `/api/series/recent`,
`/api/export/fast.csv` — use the documented async pattern
(`httpd_req_async_handler_begin()` → one "xfer" task, 8 KB stack, core 1,
priority idle+4, queue depth 1 → `httpd_req_async_handler_complete()`); a
second transfer while one runs gets 503 "Another download is running". This
keeps today's property that a download cannot starve `/api/live`, with the
official mechanism instead of two raw-socket listeners. The `ChunkOut`
deflate buffer stays single-instance (the queue depth guarantees it).

Uploads (`/api/update`): the app sends the file as the raw request body
(`application/octet-stream`; `esp_http_server` has no multipart parser) and
the handler reads it with `httpd_req_recv()` in 2 KB blocks into `Update`,
checking the image header as today (`handleUpdateUpload`).

Ports 81 and 82 are gone: `/api/live` and `/report` are ordinary port-80
routes; the speed test is an async transfer on port 80. The app's two
`url.port='81'`/`'82'` lines (`app.html:325, 2198`) are removed (its port-80
fallback already exists for live data).

The loopback self-check task and `web.rebindNetwork()` are gone: the server
task is independent of the loop, binds all interfaces, and its own timeouts
and LRU purge replace the heal/restart logic. The loop watchdog
(`esp_task_wdt`, 60 s) stays on the loop; the server task is not subscribed
(IDF default).

Memory: +8 KB server stack +8 KB xfer stack; −8 KB live task, −4 KB speed
task, −4 KB self-check, −4 KB link-tune, −`HTTP_POOL` clients. Net ≈ −4 KB.

### 6.7 API contract (routes unchanged; fields and two transports change)

`/api/live` → `net`: all existing keys kept (`ap, host, cl, ch, rt, sta,
ssid, rssi, ip, conflict:false, mode, rescue`) plus
`state` (`standalone|router-test|router-join|router|router-lost`),
`test` (`null` or `{kind:setup|return, result:testing|ok|wrong-code|not-found|timeout, ms}`),
`routerBack` (bool). `mode` keeps `(WiFi.getMode()&WIFI_MODE_AP)?"ap":"sta"`.

`POST /api/wifi/connect` (ssid, pass): 409 unless `state==STANDALONE` and no
scan/test runs; **does not save**; reply "Testing <ssid> for 20 seconds. Keep
your phone on EngineLink Wi-Fi; the result shows here. If the code is right,
EngineLink moves to the router and its own Wi-Fi goes off."; then
`pendingCmd=CMD_CONNECT`.

`GET /api/wifi/test` (**new**): `{state, test}`.
`POST /api/wifi/return` (**new**, D20): 409 unless `state==STANDALONE` and
`routerBack`; reply "Switching to <ssid>. Move your phone to the router; the
app follows."; `pendingCmd=CMD_RETURN`. The only path on which a joined
phone loses the own Wi-Fi (D21).
`POST /api/wifi/forget`: unchanged reply; `pendingCmd=CMD_FORGET`.
`GET /api/wifi/scan?start=1`: unchanged JSON (legacy progress keys kept);
allowed only in `STANDALONE` and not during a look or test; a due look is
skipped while a user scan runs.
`POST /api/options`: `network=="router"` goes through the router test too
(sets `pendingCmd=CMD_CONNECT`, writes no router credentials); AP
name/password/channel changes apply through `radioApply()`.
`POST /api/update`: raw body (6.6). `POST /api/speedlog`: unchanged minus the
radio-test trigger. `GET /api/live`, `/report`, `/api/speedtest`, `/api/rtt`:
port 80 only.

### 6.8 Report fields (the only ones that exist afterwards)

Text: header (version, uptime, **state**, radio mode, AP channel/bandwidth/
PMF/inactive time read back from the driver, router name or "Not
configured", IP, RSSI, negotiated security, WPA3 switch active, TX power
target/driver/reapplied, power save, PHY mode); **joined phones** (per
station: MAC prefix, RSSI, b/g/n/LR flags, from `esp_wifi_ap_get_sta_list()`);
**timeline since boot** (state changes, ≤ 32); counters (`apJoins, apLeaves
(lastReason), apDhcpLeases, apStarts, staAssociations, staIps,
staDisconnects (lastReason), lostIps, boots3, wifiEventOverflow`); router
test (last kind/result/ms); looks (`looks, looksSeen, returns,
returnsFailed, nextLookS`); router as seen by EngineLink (unchanged block,
`netreport.ino:164-178`); speed tests; system; HTTP (`httpdRequests,
httpdAsync, httpdBusy503, httpdSessionsPurged, httpdSendTimeouts`); latest
Wi-Fi events; JSON.

Deleted: every line in F6; the radio test/pings/survey block; "Security
switches"; "Failure stage"; "Time before rescue"; "restarted by the
watchdog"; "HTTP rebinds"; "Web server self-check". JSON keys removed:
`associationFailures, retryFails, routerProbes, routerProbeSeen,
driverNudges, failedReason, failedPhase, failedAfterMs, unexpectedReason,
unexpectedDrops, linkTune, secMode, secSwitches, saeConfigured,
saeSetupError, lastFailureDescription, routerFallbacks, routerAutoReturns,
routerNextTryS, routerAttempts, httpRebinds, httpSelfFails, httpHeals,
httpSelfRestarts, httpBoundEpoch`. `/api/diag` `wifi` block keeps the keys the
app reads (`routerFailureReason, routerUnexpectedReason, staReason,
staIpEvents, routerUnexpectedDrops, routerNextTryS, routerAuthMode, bw, ch`)
and drops the rest.

---

## 7. Acceptance criteria (measured; a release needs all of them)

| Area | Criterion | Measured by |
|---|---|---|
| Standalone stability | 24 h with an iPhone and a PC joined, app open on one of them: 0 `left … code 2` series; ≤ 1 unexplained AP leave (reason 8/3) per hour; every DHCP lease followed by a port-80 connection from that phone within 30 s; station RSSI ≥ −60 dBm at 1 m | Wi-Fi report, SD log |
| Router stability | 24 h on the router: ≤ 2 `lostEvents`, each recovered (IP again) within 15 s; 0 Standalone fallbacks with the router up | report |
| HTTP latency | `/api/live` round trip (the app's existing RTT stat) p95 ≤ 150 ms on the own Wi-Fi, ≤ 300 ms on the router; app page (`/`) first load ≤ 2 s on the own Wi-Fi; no non-transfer request > 2 s; `/api/live` p95 ≤ 500 ms while a 1 MB export runs | app speed/RTT stat, `httpdSendTimeouts` = 0 |
| Throughput | own Wi-Fi ≥ 400 kB/s; router ≥ 300 kB/s with `netboost`; the router figure without `netboost` is recorded (P8) | "Test speed" ×5 |
| Scan / look | user scan ≤ 4 s and lists the boat router; a look on the known channel ≤ 0.3 s; a phone stays joined through 60 consecutive looks | report (`looks`, `apLeaves`), stopwatch |
| Recovery | router off 5 min → `standalone` at 120 ± 5 s; router back → `router` ≤ 90 s with no phone joined; boot with router: IP ≤ 5 s median, ≤ 180 s worst, else Standalone; wrong code → result ≤ 25 s with the AP never down (`apStarts` unchanged for a router on channel 6) | 9.9 T2–T6 |
| Phone never cut off | T5: 30 min with a phone joined and the router back: `returns == 0`, phone never dropped; "Switch now" → router ≤ 25 s | D21 |
| Memory | after 24 h: lowest free heap ≥ 40 KB, largest block ≥ 32 KB, free heap not falling more than 2 KB/h; `httpd_accept_conn` errors 0; `wifiEventOverflow` 0 | report |
| Loop / engines | `engineWait max` < 200 ms during every test; `loopPartMaxMs[network]` ≤ 1 500 ms at a state change, ≤ 20 ms otherwise; the former "web" part ≤ 5 ms (the loop no longer serves HTTP); engine report answered-rate and NMEA frame count unchanged during Wi-Fi tests | engine report, Wi-Fi report |
| Radio truth | report shows AP bandwidth 20 MHz, inactive time 600 s, TX "driver reports 19.50 dBm" in both modes, `txReapplied` stable | report |
| Rapid boots, rollback | T7; an OTA image with a deliberately broken `startAP()` (test build) rolls back after 60 s | 9.9 |

---

## 8. Test strategy

1. **Host tests** (CI, `g++ -std=c++17 -Wall -Wextra`): `tools/test_wifistate.cpp`
   with the scenarios in 9.3; `tools/test_tracefold.cpp` (F2 fold, 9.1c).
2. **Source contracts** (CI, Python, `tools/fwsource.py`):
   `tools/test_wifi_contract.py` (9.10) replaces `test_wifi_simple.py`;
   `test_router_throughput_guard.py` keeps its non-radio assertions (the
   `linkTune`/`radioTestWanted` ones go, its "rescue return rule" comment is
   corrected).
3. **Compile** (CI): unchanged command, core pinned (D25), the
   `__wrap_esp_wifi_init` objdump check stays.
4. **Bench** (owner, USB from a charger, iPhone + PC, serial `wifi`): after
   Stages 1, 2 and 5 as listed in 9.
5. **Boat** (owner): protocol 9.9. D24: no merge, no release before it passes.

---

## 9. Stages

Each stage is one or two commits on the Wi-Fi 2.0 branch, compiles, passes
CI, and can be read on its own. Order matters. Stage 0 gates everything;
Stage 1 may be built in parallel with it (its fixes are cause-independent and
produce the diagnostics the gate needs).

### 9.0 Stage 0 — Root-cause gate (section 13.6)

No Stage 2–8 work before the decision table in 13.6 has a row. If the row is
H3 (firmware regression), the owner decides whether Stage 1 ships alone first
(see 13.7). If it is H1/H2/H4, the environment/hardware is fixed first.

### 9.1 Stage 1 — Diagnostics and cause-independent fixes

- **1a `apRadioSetup()`** (6.2; F19). Contract: all four calls in its body.
- **1b TX power truth** (F7): `applyTxPower(check)` uses `!=`; called after
  every interface start and on every IP epoch; the report prints target and
  driver value in quarter-dBm too.
- **1c Trace fold** (F2): a `DISCONNECTED` with reason 36 ≤ 50 ms after a
  `DISCONNECTED` with another reason folds into it; `traceFold()` in
  `wifistate.h`, host-tested (201,36,201,36 at 0,0,1574,1574 ms → 1 entry,
  count 2).
- **1d Link diagnostics** (F20): per-station RSSI/PHY and the AP's bandwidth,
  channel, PMF and inactive time in the report, in the SD log at join/leave,
  and in `/api/diag` (`wifi.bw`, `wifi.ch` stay).
- **1e CI pinning** (D25): `esp32:esp32@3.3.12` and the NeoPixel version in
  `validate.yml`, `build-engine.yml`, `pages.yml`.

Bench acceptance: report shows 20 MHz / 600 s / 19.50 dBm in both modes;
station RSSI printed; a router switched off for 2 min yields < 10 trace
entries. No behaviour change otherwise. (The earlier "bounded writes"
workaround is **dropped**: Stage 2 removes the cause.)

### 9.2 Stage 2 — `esp_http_server` (F1 structurally)

1. `httpd.ino` per 6.6: server start in `initializeWeb()`, the `Web`
   adapter, URL decoding, static page, CORS, async transfer worker, upload.
2. Route table unchanged in `webapi.ino`; handlers that used
   `web.client()`/`web.upload()` adapted (`downloads.ino:34`,
   `webapi.ino:870-`).
3. Delete `webserver.h`, `liveHttpTask`, `speedHttpTask`,
   `httpSelfCheckTask`, `NetworkServer liveServer/speedServer`,
   `netWaitBegin/End`, `lastHttpMs`, `httpBusySince`, `httpHeal*`,
   `httpSelf*`, the `web.rebindNetwork()` call in `serviceNetwork()`.
4. `netboost.cpp` called from `open_fn` behind `#if EL_NETBOOST` (P8).
5. App (two lines + upload): remove `url.port='81'` / `'82'`; `uploadFw()`
   sends the `File` as the body instead of `FormData`. `python3
   tools/build_ui.py`.
6. Contract tests (9.10) for the server.

Bench acceptance (before Stage 3): the HTTP latency, throughput and memory
rows of section 7 on the own Wi-Fi; a phone put in airplane mode mid-download:
`loopPartMaxMs` all ≤ 50 ms, `httpdSendTimeouts ≥ 1`, the other phone's app
keeps updating; 20 rapid page reloads: no `httpd_accept_conn` error; OTA from
a file works.

### 9.3 Stage 3 — `wifistate.h` + host test

Interface:

```cpp
namespace wifistate {
  enum State : uint8_t { STANDALONE, ROUTER_TEST, ROUTER_JOIN, ROUTER, ROUTER_LOST };
  enum TestKind : uint8_t { TEST_NONE, TEST_SETUP, TEST_RETURN };
  enum TestResult : uint8_t { TR_NONE, TR_TESTING, TR_OK, TR_WRONG_CODE, TR_NOT_FOUND, TR_TIMEOUT };
  enum Radio : uint8_t { RADIO_APSTA_IDLE, RADIO_APSTA_JOIN, RADIO_STA };
  enum Look : uint8_t { LOOK_SKIP, LOOK_CHANNEL, LOOK_ALL };
  struct Machine {
    bool routerSaved=false, phoneJoined=false; uint8_t knownChannel=0; bool saeAvailable=true;   // inputs
    State state=STANDALONE; Radio radio=RADIO_APSTA_IDLE; bool radioDirty=false;                  // outputs
    TestKind testKind=TEST_NONE; TestResult testResult=TR_NONE; uint32_t testMs=0;
    bool allowSae=false, saeTried=false, lastJoinUsedSae=false, returnOkFromApp=false, autoReturn=true;
    uint8_t authFails=0; bool wantLook=false; Look lookKind=LOOK_SKIP; bool saveCreds=false, routerBackSeen=false;
    uint32_t looks=0, looksSeen=0, returns=0, returnsFailed=0, lostEvents=0;
    void boot(uint32_t now, bool routerSaved, bool rapidBoots);
    void event(uint32_t now, uint8_t ev, uint8_t reason);
    void scanResult(uint32_t now, bool seen, uint8_t channel);
    void cmdConnect(uint32_t now); void cmdForget(uint32_t now); void cmdReturn(uint32_t now);
    void tick(uint32_t now);
    uint32_t nextLookInMs(uint32_t now) const;
  };
}
```

`tools/test_wifistate.cpp` scenarios (each an assert block naming the rule):
1 boot without router; 2 boot with router, IP at 1.4 s → ROUTER; 3 nothing
for 180 s → STANDALONE at ≥ 180 000, not 120 000; 4 ROUTER → DISCONNECTED(200)
→ ROUTER_LOST → GOT_IP at +50 s → ROUTER, `lostEvents==1`; 5 ROUTER_LOST 120 s
→ STANDALONE; 6 looks at 60/120/180 s, `lookKind` rules ported from
`test_netpolicy.cpp`; 7 look seen, no phone → RETURN → OK → ROUTER; 8 look
seen, phone joined → stays for 24 h of looks, `routerBackSeen`, `returns==0`;
a later unseen look clears it; `cmdReturn()` with it set → RETURN → ROUTER;
without → no change; 9 RETURN → 202, 202 → STANDALONE, `returnsFailed==1`,
next looks at 300 s then 900 s, never more; 10 SETUP: GOT_IP → `saveCreds`;
201 → NOT_FOUND, no save; 202 at 2 s still testing, 202 at 5 s → WRONG_CODE;
one 202 then GOT_IP at 8 s → OK; 11 DISCONNECTED(8) at 1 s ignored; TIMEOUT at
20 s without auth failure → TIMEOUT, with one → WRONG_CODE; 12 rapid boots →
STANDALONE, `autoReturn==false`, looks set `routerBackSeen` but start no test,
`cmdReturn()` works; 13 `cmdForget()` in ROUTER/ROUTER_LOST → STANDALONE, looks
stop; 14 `radioDirty` only on radio-changing transitions (boot → JOIN →
STANDALONE → TEST → ROUTER = 3; looks = 0); 15 millis wrap across
`0xFFFFFF00 → 0x100`; 16 stale GOT_IP in STANDALONE: no phone → ROUTER, phone
→ stays + disconnect output; 17 RETURN without `returnOkFromApp`: AP_STA_JOIN
at 3 s → STANDALONE, no penalty; with it → ignored; 18 210 → `allowSae`,
`radioDirty`, still testing; second 210 → WRONG_CODE; `saeAvailable=false` →
first 210 is WRONG_CODE; 19 look back-off reset after a success; 20
`lastJoinUsedSae` seeds RETURN tests; `cmdForget()` clears it.

### 9.4 Stage 4 — `wifi.ino` and `netsvc.ino` on official APIs

1. `netsvc.ino`: move clock, NTP, mDNS, texts, GitHub report scheduling
   (`state==ROUTER` instead of `online`).
2. `wifi.ino` per 6.2–6.3: `onWiFi()` → `xQueueSend` + `traceWifi()` + the
   `volatile` counters; `serviceNetwork()` drains the queue into
   `machine.event()`, updates inputs, `tick()`, `radioApply()` on
   `radioDirty`, mDNS/TX on the epoch, log lines.
3. `setup()`: `machine.boot()` + `serviceNetwork()`; D9 counter after
   `restartLogBoot()`; `loop()` clears it at 10 s.
4. Delete the F6 globals; drop `#include "netpolicy.h"`, `"routerguard.h"`,
   `"linktune.h"`; add `"wifistate.h"`, `"txpower.h"`; remove `linkTuneTask`
   and `radioTestWanted`.
5. `webapi.ino`: `pendingCmd` in the four handlers; `handleWifiScan` gates on
   `state==STANDALONE && !lookRunning`; `handleCfg` host change →
   `mdnsRefreshPending`.
6. `led.ino` mapping (6.4). `tools/test_wifi_contract.py` in CI; the
   `test_netpolicy`/`test_routerguard` steps replaced by `test_wifistate`.

Bench acceptance: standalone boot, phone joins, app loads, `state
standalone`; heap lowest ≥ 40 KB after 1 h; router saved and present → `router`
within 5 s; router off → `router lost`, `standalone` at +120 s; router back →
seen within 60 s and (no phone) `router` within 25 s.

### 9.5 Stage 5 — Router test, looks and return

1. `startLook(kind)`: `WiFi.scanNetworks(true, true, false, LOOK_DWELL_MS,
   kind==LOOK_CHANNEL ? routerKnownChannel : 0, cfg.ssid.c_str())` — directed
   and `show_hidden`, so a hidden router is found; result → `saveKnownChannel`
   + `machine.scanResult()`; 5 s timeout = not seen; never a mode change (D19).
2. User scan reuses the same code with `channel=0`, `ssid=nullptr`, the
   0.16.5 snapshotting (strongest BSSID per SSID). WPA3-only detection from
   the chosen entry's `auth==WIFI_AUTH_WPA3_PSK`; manual SSID → WPA2 (D10).
3. `ROUTER_TEST` radio per 6.3; AP restarted on its channel after a failed
   test if it drifted.
4. `/api/wifi/test`, `/api/wifi/return`, the `state/test/routerBack` keys.
5. `phoneJoined` read once per `serviceNetwork()` pass; a RETURN test aborts
   on `EV_AP_STA_JOIN` (D21).

Bench acceptance: wrong password → scan page back within 25 s, own Wi-Fi
never down (`apStarts` unchanged for a router on channel 6; +1 and the phone
rejoined within 10 s for another channel), report `test: setup wrong-code`;
right password → `test: setup ok`, AP off, router IP, `cfg.router` saved;
router off 5 min → standalone at +2 min; back within 90 s with no phone;
with a phone joined: stays standalone, `routerBack: true`.

### 9.6 Stage 6 — Rapid boots, rollback, log texts

D9 counter + `boots3` in the report; the rollback line
`EngineLink.ino:601` unchanged (contract); `serialcmd.ino` boot line uses the
state name; `serviceWifiLog()` logs state changes. Acceptance: T7; the
broken-`startAP()` OTA test build rolls back after 60 s.

### 9.7 Stage 7 — Report, cleanup, docs, CI

`netreport.ino` per 6.8; `diagnosis.ino` wifi block; delete `linktune.*`,
`netpolicy.h`, `routerguard.h`, their tests; `test_router_throughput_guard.py`
trimmed and its comment corrected; `HANDOVER.md` Wi-Fi section and code-layout
table (`wifi.ino`, `httpd.ino`, `netsvc.ino`, `wifistate.h`, `txpower.h`;
drop `webserver.h`, `netpolicy.h`, `linktune`, `routerguard.h`);
`docs/STATUS.md` rows "0.17.0 untested" until 9.9; `docs/TODO.md`.
Acceptance: `wc -l wifi.ino < 300`, `httpd.ino < 300`, `netreport.ino < 220`;
`grep -rn "linkTune\|netpolicy\|routerGuard\|staSecMode\|WebServer.h\|NetworkServer" firmware tools .github` empty; CI green.

### 9.8 Stage 8 — App (D20; `firmware/ui/app.html`, then `tools/build_ui.py`)

1. Router page: after `wifiConnect`, poll `/api/wifi/test` every second for
   25 s; show `testing` / `wrong-code` ("Wrong code or the router refused
   the login") / `not-found` ("Router not found") / `timeout` ("No answer
   from the router") / `ok` ("OK, address <ip>. Switch your phone to the
   router; the app follows.").
2. "Router is back": on the rising edge of `lv.net.routerBack` one toast
   "Router <ssid> is back"; while true, a row "Router is back · Switch now"
   on the Diagnostics Wi-Fi card and the router page → confirm "Switch
   EngineLink to <ssid>? Its own Wi-Fi turns off. Move your phone to the
   router afterwards." → `POST /api/wifi/return`, `BASE=''`, `findHost()`.
   No modal that reappears (D21: staying is a valid choice).
3. Texts at `app.html:1703, 1719` describe looks/return.
4. `tools/test_mobile_ui_contract.py` keeps passing; Playwright desktop and
   iPhone viewports render the texts; reduced motion unaffected.

### 9.9 Stage 9 — Hardware validation, release 0.17.0 (D24)

Bench protocol (owner, before the boat): the Stage 2 and Stage 5 bench
acceptance lists, plus 12 h standalone with iPhone + PC.

Boat protocol (owner; send the `wifi` report after each):

| # | Test | Pass (section 7) |
|---|---|---|
| T1 | Standalone, iPhone + PC, 2 h, app open, USB not attached | stability, latency, memory rows |
| T2 | Pick the router with a wrong code | own Wi-Fi stays, `wrong-code` ≤ 25 s, nothing saved |
| T3 | Pick the router with the right code | `ok`, router IP, app at `http://enginelink.local` and the IP ≤ 30 s |
| T4 | Router off 5 min, then on; no phone on the own Wi-Fi | `router-lost` at the drop, `standalone` at +120 ± 5 s, `router` ≤ 90 s after the router is back; trace < 10 entries |
| T5 | As T4, iPhone joins the own Wi-Fi during the outage; app open 15 min after the router is back, then app closed, phone joined, 15 min | standalone the whole 30 min, `returns==0`, phone never dropped; "Switch now" → router ≤ 25 s; repeat with the phone's Wi-Fi off → router ≤ 90 s |
| T6 | Boat power: router and EngineLink on together, 5×, no phone joined | `router` ≤ 3 min, or `standalone` at 3 min and `router` ≤ 2 min after the router is up |
| T7 | Power off/on 3× within 10 s | `standalone`, LED S_ROUTER, `boots3==1`; next normal boot → `router` |
| T8 | "Test speed" 5× in each mode | throughput row |
| T9 | Engines running during T4 | `engineWait max` < 200 ms, engine report unchanged |
| T10 | 24 h on the router | router stability and memory rows |

Then `docs/RELEASING.md`: bump `EL_VERSION`, README badge,
`docs/releases/v0.17.0.md` (what changed, the protocol results, what is
untested), merge, release. `docs/STATUS.md` says "verified on the board" only
for rows T1–T10 passed.

### 9.10 `tools/test_wifi_contract.py` (replaces `test_wifi_simple.py`)

```
w = wifi.ino; h = httpd.ino; fw = firmware_source()
- len(w.splitlines()) < 300 and len(h.splitlines()) < 300
- "delay(" not in w and "delay(" not in h
- body(w,"void onWiFi(") has "xQueueSend" and "traceWifi(" and no "WiFi." / "MDNS." / "esp_wifi_"   (D14, F5)
- count("WiFi.mode(") in fw == count in body(w,"void radioApply(")                                   (one place)
- "httpdSuspend()" precedes every "WiFi.mode(" in radioApply (regex)                                 (driver-buffer lesson)
- body(w,"void startAP(") has 'WiFi.softAPConfig(IPAddress(192,168,4,1)' and
  'WiFi.softAP(cfg.apName.c_str(),cfg.apPassword.c_str(),apChannelNow,false,6)'                     (D2)
- body(w,"void apRadioSetup(") has "esp_wifi_set_bandwidth(WIFI_IF_AP,WIFI_BW_HT20)",
  "esp_wifi_set_inactive_time(WIFI_IF_AP,600)", "esp_wifi_set_country(", "applyTxPower(false)"        (F19, D14)
- body(w,"void startLook(") has "scanNetworks(true,true,false," and "cfg.ssid.c_str()"                (hidden SSIDs)
- "scanNetworks(false" not in fw                                                                     (async only)
- not re.search(r"print[f]?\([^;]*(apPassword|wifiPassword)", fw)                                    (never log passwords)
- "saveSettings()" not in body(webapi,"void handleWifiConnect("); "cfg.ssid=" not in that body nor in handleOptions   (F9/P7)
- "HTTPD_DEFAULT_CONFIG()" in h; "lru_purge_enable=true" in h; "max_uri_handlers=96" in h; "httpd_uri_match_wildcard" in h
- "httpd_req_async_handler_begin(" in h and every path of the transfer list (6.6) appears in the async table in h
- "xSemaphoreGive(engineMx)" precedes "httpd_resp_send" inside the adapter's send functions (regex on body(h,"size_t Web::send"))
- "#include <WebServer.h>" not in fw; "NetworkServer" not in fw; "setTxTimeoutMs(0)" after "Serial.begin" in setup()
- "wifistate.h" has no "#include <Arduino.h>", no "WiFi", no "millis("
- the "esp_ota_mark_app_valid_cancel_rollback" line in loop() unchanged (string compare)              (F12)
- grep: no "netpolicy", "linkTune", "routerGuard", "staSecMode", "radioTestWanted", "lastHttpMs" in firmware/ or .github/
- "esp32:esp32@3.3.12" in .github/workflows/*.yml                                                     (D25)
```

---

## 10. Implementation rules for whoever builds it

- Follow the stages in order; Stage 0 first; do not start Stage 4 before
  `test_wifistate.cpp` passes; do not start Stage 3 before the Stage 2 bench
  acceptance is reported.
- D23: before writing a helper, name the IDF/core API that does it; if there
  is none, say so in the commit message. No new file may include
  `lwip/priv/*` or wrap a core function; the two existing exceptions are the
  only ones.
- Never call `WiFi.*`, `MDNS.*`, `esp_wifi_*` from the event callback, an
  HTTP handler or another task. Handlers set flags; `serviceNetwork()` acts
  ≥ 1.2 s later.
- Every blocking core call (`WiFi.mode`, `WiFi.begin`, `MDNS.begin`) happens
  inside `radioApply()`/`startNameService()` only (`loopPartMaxMs[network]`
  measures them).
- No new settings, no NVS format change beyond `rlog/quick`, no change to
  J1708, trips, fuel, storage or NMEA 2000 code.
- Texts short and factual, English; never print a password; the router name
  is replaced in GitHub reports as today.
- What the hardware shows wrong goes into `docs/TODO.md`/`HANDOVER.md` in the
  same PR; never a silent different fix.
- D24: the PR for 0.17.0 is not merged on green CI; it is merged when 9.9 is
  reported passed.

---

## 11. Owner decisions and open approvals

Answered (history): P1 approved → D19; P2 approved → D20; P3 rejected → D21;
P4 deferred → D22; P5 (20 s test budget), P6 (`apch` honoured), P7
(`/api/options` through the router test): no objection.

Open, for approval:

| # | Proposal | Recommendation | Why |
|---|---|---|---|
| P8 | Keep `netboost.cpp` (fenced exception) until option B | **keep**, with the no-`netboost` router throughput measured and recorded in T8 | it is the only way under A to lift the 5744-byte compile-time window that measured 7× on the router; 54 lines, kill switch, contract test |
| P9 | Two app changes beyond D20: ports 81/82 removed from the app, firmware upload as a raw body | **approve** | both are consequences of using `esp_http_server` instead of three hand-written servers; two lines and one function |
| P10 | Option B (ESP-IDF project with Arduino as a component) as a separate later plan under the triggers in 3.2 | **defer**; decide after T10 | measured need first; it is a full-firmware conversion |
| P11 | The loopback web self-check and its automatic board restart are removed (official server timeouts and LRU purge replace them) | **approve** | the self-check existed to catch the single-threaded server hanging; that server is gone |

Consequence of D21 the owner should keep in mind: a phone that stays
associated to the own Wi-Fi holds EngineLink in Standalone (NTP, GitHub
reports and "Check for updates" wait). The AP drops a silent phone after the
driver's inactive time (600 s, 6.2), so a return can lag a departed phone by
up to 10 min; shortening it is **not** in this plan.

---

## 12. Review log and open risks

Final review 2026-11-05 (history, all folded into section 6): R1 rapid boots
vs automatic return (`autoReturn`); R2 AP left on the router's channel after a
failed test (restart on drift); R3 one 202 is not "wrong code" (two strikes);
R4 the coalesced trace cannot feed the machine (queue); R5 hidden SSIDs
(directed scan); R6 auto-reconnect off before the AP comes up; R7
`lastJoinUsedSae`; R8 T6 wording; R9 a stale test comment.

Third-revision review (2026-11-05, this document): the Stage 1a bounded-write
workaround contradicted D23 and is replaced by Stage 2; the connection pool,
keep-alive rewrite, two raw listeners and the self-check were four hand-made
pieces of one component and are replaced by it; the hand ring for events is
replaced by a FreeRTOS queue; `wifi_provisioning` was evaluated and rejected
with reasons (section 4); the CI core version was unpinned (D25).

Open risks:

- `WIFI_AP_STA` idle STA (D19): RAM and a driver path the board has not run
  long; measured in 9.4 and T1/T10; revert switch exists.
- `esp_http_server` in the prebuilt core uses `CONFIG_LWIP_MAX_SOCKETS=16`;
  the socket budget in 6.6 leaves 5 for mDNS, SNTP, HTTPClient and the
  loopback; measured by `httpd_accept_conn` errors in T1.
- Handlers now wait for `engineMx` released by the loop; a long loop part
  (raw clean-up 461 ms, serial 584 ms with a console attached) delays a
  request by that much. The latency criterion catches it; if it fails, the
  fix is shortening that loop part, not a second lock.
- A phone that never leaves keeps EngineLink off the router (D21).
- iOS behaviour on networks without internet (F15) is outside the firmware.
- The core's immediate reconnect (D3) hammers a refusing router every 1.6 s
  for 2 min; the trace no longer floods.
- The 60 s rollback window confirms on `apStarted`; a build whose AP comes up
  but whose server is dead would be confirmed (the self-check that restarted
  the board in that case is gone, P11). Mitigation: the Stage 2 bench test
  and T1 run on the same build that is released.

---

## 13. Field evidence 2026-11-05 (0.16.5 on the boat) and the root-cause gate

History and the only open investigation. Source: the owner's serial-console
export (16 SD reports 2026-11-04 14:11 UTC → 2026-11-05 10:48 UTC, `/WIFI.LOG`,
one live report at uptime 400 s). Setup: 0.16.5 from factory settings, both
J1708 channels and NMEA 2000 connected (CAN listening off), 13.53 V in,
3.301 V at the LDO (DMM, static). Tests: (1) boat 12 V only — phone joined,
no web page; (2) USB only — page opens, "Waiting for EngineLink…" /
"Reconnecting"; (3) USB + 12 V — worse, then unreachable while the phone
still showed the network. iPhone, PC, Safari/Chrome, home-screen app, IP and
`.local` all tried.

### 13.1 Telling the tests apart

| Reports in `/WIFIREP.TXT` | Firmware | Belongs to |
|---|---|---|
| 2026-11-04 14:11, 14:23, 14:26 UTC | 0.16.3 / 0.16.4 | yesterday, USB (8.5 dBm) — old |
| 2026-11-04 14:47 UTC | 0.16.5 | yesterday, USB — old |
| "up 120s", reset reason 1, no phone | 0.16.5 | first boat 12 V power-on |
| 2026-11-05 08:26 UTC (reason 11) | 0.16.5 | today, USB attached |
| "up 120s" reason 1, phone joined +26 s, **0 non-loopback TCP accepts** | 0.16.5 | today, 12 V only (test 1) |
| 2026-11-05 08:56 UTC, reason 1, speed tests 60–72 kB/s | 0.16.5 | today, 12 V |
| "up 120s" reason 1, web stall 10 022 ms | 0.16.5 | today, 12 V |
| "up 8s", "up 120s" ×2, "up 27s", 09:25, 09:30 UTC (reason 11) | 0.16.5 | today, USB only (test 2) |
| 2026-11-05 10:48 UTC and the live report (reason 1) | 0.16.5 | test 3: the boot after connecting 12 V while on USB |

Reset reason 1 = power-on, 11 = USB/console reset. "reason 1 after 0 s" does
not mean a short session: the `alive` marker is written by `saveTotals()`
only (every 30 s while an engine runs, or at a clean restart).

### 13.2 Documented findings

| # | Finding | Evidence |
|---|---|---|
| L1 | The AP, association and DHCP work in every boot (join within 4–42 s, `apDhcpLeases ≥ 1` in 13 of 14 boots with a phone). | all 5 Nov reports |
| L2 | In four boots the phone has a lease but **no TCP connection from the phone reaches port 80 within 2 min** (`httpExternalAccepts: 0`, `httpRequests: 2` = loopback only): one 12 V boot and three USB boots. | "up 120s" reason 1; "up 120s" reason 11 ×2; 09:25 UTC |
| L3 | In two boots the phone asked DHCP **twice** within 1–2 s of joining. | "up 120s", "up 27s" USB boots |
| L4 | The AP reports the phone **leaving with reason 2 eight to ten times in 4–20 s** for a single join, each series ending with **reason 209** 3–97 s later and a new join. On 0.16.4 at 8.5 dBm and 0.16.5 at 19.5 dBm, USB and 12 V. | `/WIFI.LOG` 14:23:19→14:23:46, 14:26:53→14:26:59, 14:29:08→14:30:45, up 100s→123s, 09:20:21→09:20:24, 09:35:04→09:35:08 UTC |
| L5 | The phone leaves by itself (reason 8) 7–45 s after joining in six boots, once reason 3 at +171 s. | 09:29:14, 09:29:31, 09:25:21, 09:21:08, 09:23:31, 10:49:02 UTC |
| L6 | Every boot with data exchanged shows the loop blocked in the web part 5.6–10.0 s; the engine task waited the same (F1, a consequence of the dead link). | 08:26, 08:56, 09:30, 10:48 UTC and the 10 022 ms boots |
| L7 | The two speed tests on 12 V: **60 and 72 kB/s at 215–307 ms**, against 650 kB/s (0.13.4, 2 Nov) and 265–346 kB/s at 91–99 ms (0.15.0, 4 Nov, USB). | 08:56 UTC |
| L8 | Driver 19.5 dBm, `httpSelfFails ≤ 1`, heap lowest 76–78 KB, 0 parse failures. | all 5 Nov reports |
| L9 | No brownout/watchdog/crash resets today. The 10:46 UTC boot is a **power-on reset of a board that was on USB** when 12 V was connected (test 3), if the sequence is as described. | 10:48 UTC report |
| L10 | 0.16.3/0.16.4 at 8.5 dBm showed the same reason-2/209 series. | 14:23, 14:26 UTC |

### 13.3 What the source says

- The Wi-Fi driver is not stopped by the loop's stalls (driver and lwIP on
  core 0 in their own tasks; the loop on core 1). L4/L5 are not caused by L6;
  L6 is caused by whatever makes the phone stop acknowledging.
- L2 means the **downlink fails while the uplink works**: Auth, association
  and DHCP Discover (phone → EngineLink) succeed; the DHCP Ack, the TCP
  SYN-ACK and the SA Query (EngineLink → phone) do not arrive. Three
  independent symptoms, one direction.
- PMF is always on for the softAP (F18); 209 is the AP dropping a PMF
  association after an unanswered SA Query — a mechanism, not the cause.
- 0.16.2 removed four AP radio settings every working version had (F19;
  table in the second revision, restored by `apRadioSetup()`).
- 0.16.1 (old Wi-Fi code) also failed on 4 Nov; 0.16.0 also added the TWAI
  driver and the drive-test UI; nothing in the log shows what 0.16.1 did.
- 8.5 dBm did not remove the symptom (L10): weak evidence against a pure
  PA-current explanation.
- Not iOS-specific (PC too, owner's statement).
- The firmware cannot see: the phone's RSSI at the AP (Stage 1d adds it), the
  AP's real bandwidth (only `/api/diag`), per-station TX failures, the
  router's channel today, the 3.3 V rail during a TX burst.

### 13.4 Hypotheses (none accepted)

| # | Hypothesis | Fits | Against / unknown | Decided by |
|---|---|---|---|---|
| H1 | RF environment changed (the Zyxel's automatic channel now overlaps channel 6, −39 dBm at the board on 2 Nov; or a new strong neighbour) | L2–L4, L7 (0.15.0 already 3× slower RTT on 4 Nov than 0.13.4 on 2 Nov with the router on 11), "worked before" | test 3 got worse without moving the board (one observation) | 13.6 steps 0 and 3 |
| H2 | Power path: 3.3 V sag/noise during TX bursts (LDO transient response, buck ripple, weak USB 5 V), 12 V back-feeding or dipping the 5 V node (L9), a ground loop via USB + boat negative | L9, worse with 12 V, marine environment | also unstable on USB only; no brownout resets; 8.5 dBm did not help; LDO/buck parts and USB/12 V ORing not documented | 13.6 steps 1 and 4 |
| H3 | Firmware AP-configuration regression in 0.16.2 (F19) | exactly the versions without those settings fail | 0.16.1 also failed; the actual bandwidth not yet read | 13.6 steps 0 and 2 |
| H4 | Board/antenna degradation (moisture, corrosion, decoupling) | "worked before", boat | indistinguishable from H2 in the log | 13.6 steps 1–2 |
| H5 | PMF/SA-Query makes every link loss cost 3–97 s (L4, L5) | timing | consequence, not origin; same core ran 0.13.4 at 650 kB/s | only after H1–H4; `bss_max_idle_cfg` is a B-only option |
| H6 | The loop stalls as root cause | — | ruled out (13.3); aggravator, removed by Stage 2 | — |

### 13.5 What only a measurement can settle

The router's current channel and signal at the mounting place (read from the
app's scan or the router's status page; no setting changed); `wifi.bw`/`wifi.ch`
from `/api/diag` on 0.16.5; the 3.3 V rail and the 5 V node on an oscilloscope
during a speed test and at the instant 12 V is connected while on USB (pass
limits need the LDO/buck part numbers, not in the repository); the phone's
RSSI as the AP sees it (Stage 1d).

### 13.6 Troubleshooting procedure (fewest tests; stop at a decision)

| Step | Where / supply / firmware | Do | Read |
|---|---|---|---|
| 0 | boat, any supply, 0.16.5 | Settings › Router › "Scan for networks" (does not connect): router channel and dBm; `/api/diag` → `wifi.bw`, `wifi.ch` | channel 4–8 ⇒ H1 likely; `bw: 40` ⇒ H3 possible |
| 1 | **home bench**, USB from a charger, 0.16.5, iPhone + PC, 15 min | as test 2 | stable (no `code 2` series, web part < 1 s, ≥ 300 kB/s) ⇒ the boat is the difference; unstable ⇒ board or firmware (step 2) |
| 2 | same place, **0.13.4** from the web installer's version list (0.13.4 is a comparison point, not a reference: its Wi-Fi was also unstable on the router) | same test | stable ⇒ H3 (confirm with 0.15.1 and 0.16.5 again); unstable ⇒ hardware (step 4) |
| 3 | boat, 12 V only, the firmware that was stable in 1–2 | 15 min router on; then router **powered off** 5 min | stable only with the router off ⇒ H1; unstable both ways ⇒ H2/H4 |
| 4 | boat, oscilloscope on 3V3 (module pin) and the 5 V node | (a) USB only + speed test; (b) 12 V only; (c) on USB, connect 12 V; (d) 12 V input ripple. Placeholders until the parts are known: 3V3 never < 3.0 V, ripple < 50 mVpp; 5 V node ≥ LDO dropout + 3.3 V; no dip < 2.8 V in (c) | a fail in (b)/(c)/(d) ⇒ H2 |
| 5 | boat, 12 V, if inconclusive | a second phone or a laptop as the only client, 1 m, 15 min | same series ⇒ board/environment; clean ⇒ the first phone (L5) |

Decision table:

| Step 1 | Step 2 | Step 3 | Conclusion | Next |
|---|---|---|---|---|
| stable | — | router off helps | H1 | Stage 1 (HT20, diagnostics, the router's channel in the report); standalone channel selectable from 1/6/11 at setup is already allowed; no router change (D17) |
| stable | — | no difference | H2/H4 | step 4; hardware fix before Stage 2 |
| unstable | stable | — | H3 | reinstate the 0.15.1 AP settings one at a time in a bench build; Stage 1 ships the result |
| unstable | unstable | — | hardware | step 4; the board, not the boat |

For the record: the owner adds the LDO and buck part numbers, the USB/12 V
ORing and the decoupling at the module to `docs/HARDWARE.md` during the next
authorised change.

### 13.7 If the gate ends in H3

The owner decides whether Stage 1 ships alone first as 0.16.6 (D18 "one
release" was about the rewrite, not about a restored setting). Stages 2–8 then
follow in 0.17.0.
