# Wi-Fi 2.0: the implementation plan

> **Owner override, 2026-11-09 (supersedes the former exceptions in section 4.1, P8 and I16):** Remove `wifiwpa3.cpp/.h` and `netboost.cpp/.h` entirely. No linker interposition on `esp_wifi_init`, no private lwIP PCB edits, and no station-specific radio overrides. Router mode uses Arduino ESP32 3.3.12 / ESP-IDF driver defaults for WPA2/WPA3 and TCP. Preserve the official `esp_http_server`, OTA rollback, AP and engine safeguards. This is a deliberate simplification, **not** proof that router connectivity is repaired. Existing references below describe superseded design history; do not restore the exceptions. The A/B experimental build is retired.

> **Planning reopened by owner, 2026-11-09:** After actual **green build #261** (`75faba4`) and on-board use, the Router mode still fails to serve HTTP despite WPA2 association and a DHCP address, then loses router beacons. Full [2026-11-09 evidence](test-evidence/wifi-0166-2026-11-09/README.md) and [raw report](test-evidence/wifi-0166-2026-11-09/firmware-75faba4-maggan-recovery.txt). Fable must critically review **all** old and new findings and write a **new evidence-based plan**, **not implement it yet**. This explicit instruction supersedes "planning closed" and any implementation steps below that imply continued A/B testing or keeping special solutions. Only two user-selectable modes (Standalone and Router); no custom driver/TCP internals or mandatory router adjustments.\n\nStatus: **historical plan (revision requested 2026-11-09), previously final, planning closed 2026-11-05 (fifth revision). Stages 1–3 *built*
(development build 0.16.6, CI green, untested on hardware); Stage 4 onwards waits for
the Stage 0 gate (9.0). Implementation findings: section 12.1.** This
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

**Instructions to the implementer (Opus):**

1. Build with Espressif's official components and the Arduino core's thin
   wrappers (D23, section 4). Before writing any helper, name the IDF or core
   API that does the job; if none exists, say so in the commit message. The
   two exceptions in 4.1 are the only ones; a third needs the owner's written
   approval first.
2. Follow the stages in section 9 in order. Stage 0 is a hardware gate the
   owner runs; 9.0 says which work may proceed meanwhile.
3. Every stage ends with its acceptance list. Report results with the status
   vocabulary of D28: *built* (compiles, CI green), *bench-verified* (the
   owner ran the stage's bench list and reported it), *boat-verified* (9.9
   passed). Never write "verified", "works" or "fixed" about firmware that
   has not been run on the board; say *built, untested on hardware*.
4. Do not revise the architecture. If implementation reveals a concrete
   critical defect in this plan, record it in section 12 with evidence and
   stop at that stage; do not start a new plan (D29).
5. No merge, no release before 9.9 is reported passed (D24).

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
| D26 | **No deflate compression of downloads.** The miniz streaming compressor (`ChunkOut.z`, `zComp`, `acceptsDeflate()`, `Content-Encoding: deflate`) is removed in Stage 2; every export keeps its route, content, columns and file format and is sent uncompressed in chunks. Known crash area (0.13.2) gone; the app page keeps its pre-built gzip. (owner, 2026-11-05) |
| D27 | **Country configuration per Espressif's API.** `esp_wifi_set_country_code("DK", true)` (ISO code from the documented list, `esp_wifi.h` attention 6; 802.11d on so the station follows the router's country IE, attention 3/8). Never `"EU"`. (owner, 2026-11-05) |
| D28 | **Status vocabulary.** *built* = compiles and CI is green; *bench-verified* = the owner ran the stage's bench list and reported it; *boat-verified* = 9.9 passed. Firmware is never called verified, working or fixed before the owner's report. |
| D29 | **Planning closed 2026-11-05.** No further architecture revisions unless implementation reveals a concrete critical defect, recorded in section 12 with evidence. |
| D30 | **Router independence (owner, 2026-11-06).** Any router or hotspot that offers a compatible 2.4 GHz network works: home, 4G and 5G routers, iPhone and Android hotspots; WPA2, WPA3 and mixed WPA2/WPA3 as far as the ESP32-S3 supports them; automatic channel selection and channel changes; networks without internet; outages, reconnects and recovery. Never a router setting, a manual channel or code for one router, phone or brand (the owner's 4G router and Zyxel are test cases, not targets). Standalone works with no router at all. Accepted by 9.9 T11–T14; implementation notes I12–I13 (12.1). |

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
| Larger TCP send window per connection | `netboost.cpp` pokes `tcp_pcb->snd_buf` under `LOCK_TCPIP_CORE()` | **exception** (A only; a `CONFIG_LWIP_TCP_SND_BUF_DEFAULT` line under B) | fenced, justified in 4.1; `EL_NETBOOST` switch; approval P8 |
| WPA2 on WPA2/WPA3 routers, WPA3 only when required | `wifiwpa3.cpp` + `-Wl,--wrap=esp_wifi_init` clears `CONFIG_FEATURE_WPA3_SAE_BIT` in `wifi_init_config_t.feature_caps` (a documented field) | **exception** (A only; under B it is three lines before the application's own `esp_wifi_init()`) | fenced, justified in 4.1; CI objdump check; D10 |
| Radio test, channel survey, pings, back-off tables, router guard | `linktune.cpp/.h`, `netpolicy.h`, `routerguard.h` | **removed** (D13) | — |
| Diagnostics: report, SD log, serial commands | `netreport.ino`, `serialcmd.ino` | kept, slimmed to fields that exist; per-station RSSI/PHY from `esp_wifi_ap_get_sta_list()` added | product |

### 4.1 The two exceptions: why, alternatives, isolation, removal

Verified against the sources in the header table.

**`netboost.cpp` — a larger TCP send window per connection.**

- *Why it exists:* lwIP's per-connection send buffer is the compile-time
  `TCP_SND_BUF = CONFIG_LWIP_TCP_SND_BUF_DEFAULT` (`lwipopts.h:627`); the
  prebuilt Arduino core sets **5744 bytes** (lib-builder `defconfig.common`).
  Throughput over a path with round-trip *t* is bounded by window / *t*:
  5744 B / 70 ms ≈ 80 kB/s, which is what the router measured before the
  boost (#116: 83 kB/s average) and 7× less than after it (#126: 585 kB/s).
  On the own Wi-Fi the round trip is short, so the window hardly matters
  there.
- *Supported alternatives checked:* `setsockopt(SO_SNDBUF)` — not available:
  IDF's `lwipopts.h` enables `LWIP_SO_RCVBUF` only, there is no
  `LWIP_SO_SNDBUF` (and lwIP's `SO_SNDBUF` is read-only even where it exists);
  `esp_http_server` has no buffer-size option; window scaling does not change
  the send buffer; the only supported knobs are `sdkconfig`
  (`CONFIG_LWIP_TCP_SND_BUF_DEFAULT`, needs option B) or rebuilding the core
  with the Arduino Static Library Builder (official but a second toolchain
  and a self-hosted core package — rejected as heavier than B).
- *Decision:* keep, as the only code in the firmware that touches a private
  structure (`lwip/priv/tcp_priv.h`, `tcp_active_pcbs`, under
  `LOCK_TCPIP_CORE()`; a struct change breaks the build, not the device).
- *Isolation and test:* compile flag `EL_NETBOOST` (default 1) around the
  call in the HTTP open hook; `elBoostedSockets`/`elBoostMisses` in the
  report; T8 measures "Test speed" on the router with and without it, and
  the plan records both numbers; `tools/test_wifi_contract.py` asserts that
  `lwip/priv` is included from `netboost.cpp` only.
- *Removal:* option B (one `sdkconfig` line), or the owner accepting the
  no-boost router throughput measured in T8.

**`wifiwpa3.cpp` — WPA2 on WPA2/WPA3 routers, WPA3 only when required.**

- *Why it exists:* the Zyxel EX3600 in WPA2/WPA3 mixed mode failed the WPA3
  login repeatedly (0.11.x, reports #53 and HANDOVER); WPA2 since 0.12.0
  works. The driver chooses WPA3 whenever the AP offers it and the driver
  was initialised with the SAE capability.
- *Supported alternatives checked:* `wifi_sta_config_t.threshold.authmode`
  is a *minimum*, not a maximum; there is no runtime API to disable SAE for
  a connection; the capability comes from `wifi_init_config_t.feature_caps`,
  which `WIFI_INIT_CONFIG_DEFAULT()` fills from the **compile-time macro**
  `WIFI_FEATURE_CAPS` (`esp_wifi.h:325-358`, no runtime variable); the
  Arduino core builds that config inside `WiFiGenericClass`
  (`WiFiGeneric.cpp:272-289`) with no hook; `CONFIG_ESP_WIFI_ENABLE_WPA3_SAE`
  is `sdkconfig` (option B) and would remove WPA3 for all routers (D10 wants
  it for WPA3-only ones).
- *Decision:* keep. It changes one documented public field
  (`feature_caps`, `CONFIG_FEATURE_WPA3_SAE_BIT`) and touches no private
  data; the intercept is the GNU ld `--wrap` the build already uses.
- *Isolation and test:* own file; CI's objdump check proves the wrap is
  linked; the report prints "WPA3 switch in firmware: active/NOT active" and
  the negotiated security of the live link (runtime evidence); building
  without `-Wl,--wrap=esp_wifi_init` disables it cleanly (driver default,
  `elSaeControlActive()` false).
- *Removal:* option B (three lines before the application's own
  `esp_wifi_init()`), or dropping WPA2 forcing if the router's WPA3 is
  shown to work in a later test.

No third exception is allowed (rule in section 10).

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
| F12 | Rollback confirmation needs only `apStarted` or a router IP (`EngineLink.ino:601`). | | superseded by the boot self-test, 6.10 (see F23). |
| F13 | Scans move the radio off channel 6; a one-channel look is ~0.15 s. | | Stage 5 rules (ported from `netpolicy.h` tests). |
| F14 | Lowest free heap 55–78 KB; an idle STA costs RAM. | #126, #146, 5 Nov | criterion in section 7. |
| F15 | iPhone leaves the own Wi-Fi by itself (reason 8). | 0.16.4, 5 Nov | measured in 9.9; D22. |
| F16 | Contracts others depend on (`networkIpEpoch`, `apStarted`, `routerConfigured()`, `routerHold`, `lastHttpMs`, …). | | 6.5. |
| F17 | Uplink works, downlink to the phone fails (0 TCP accepts after a lease, DHCP asked twice, SA Query unanswered). Cause open. | section 13 | Stage 0 gate. |
| F18 | The softAP always runs PMF on this core (IDF 5.5: `wifi_pmf_config_t.capable` "Deprecated … will always connect in PMF mode if other device also advertises PMF capability"); a phone re-joining a link it considers dead costs 3–97 s (reason 2 series ending in 209). | `esp_wifi_types_generic.h:505`, 5 Nov log | mechanism, not cause; `bss_max_idle_cfg` is `CONFIG_WIFI_BSS_MAX_IDLE_SUPPORT`-gated (off in the prebuilt core) — a B-only option. |
| F19 | 0.16.2 dropped `esp_wifi_set_bandwidth(AP, HT20)`, the explicit country and `esp_wifi_set_inactive_time(AP, 600)` that every working version had. | `d82911b:wifi.ino:225-262,452-470` | Stage 1a `apRadioSetup()`. |
| F20 | The report lacks per-station RSSI/PHY and the AP's real bandwidth/channel/PMF. | | Stage 1d. |
| F21 | A power-on reset occurred when the boat 12 V was connected while the board ran on USB (hardware, to be confirmed). | 5 Nov 10:48 UTC report | Stage 0 step 4. |
| F22 | No access control and `Access-Control-Allow-Origin: *` on every response: a web page in the owner's browser, or a DNS-rebinding page, can read EngineLink data and POST to `/api/factory-reset`, `/api/update`, `/api/wifi/*`. | `webapi.ino:1053`, `netreport.ino:261,271,329` | 6.9, Stage 2 (P12). |
| F23 | A new firmware confirms itself after 60 s whenever the radio is up, without proving the web server works; the loopback self-check task compensated with a heal/restart machine. | `EngineLink.ino:601`, `netreport.ino:382-409` | 6.10, Stage 2 (P11). |
| F24 | Downloads are deflate-compressed on the fly with the ROM's miniz through a hand-written `ChunkOut`; the 0.13.2 crash backtrace went through it and one export is kept uncompressed as a guard. | `downloads.ino:1-45`, `EngineLink.ino:110-141` | removed, D26, Stage 2. |

---

## 6. Target design

### 6.1 Files after the rewrite

| File | Purpose | Size target |
|---|---|---|
| `wifistate.h` (**new**, no Arduino includes) | the product state machine: states, timers, inputs, actions; host-tested by `tools/test_wifistate.cpp` | ≤ 220 lines |
| `httputil.h` (**new**, no Arduino includes) | URL decoding and the own-origin/host allow-list (6.9); host-tested by `tools/test_httputil.cpp` | ≤ 80 lines |
| `wifi.ino` (**rewritten**) | `onWiFi()` → queue + trace; `radioApply()` (the only place that calls `WiFi.mode/softAP/begin/disconnect`); `apRadioSetup()`; scan and look; `serviceNetwork()` feeds `wifistate` and performs its actions; TX power | ≤ 300 lines (D14) |
| `httpd.ino` (**new**) | `esp_http_server` start, the `Web` adapter (6.6, ≤ 200 lines of logic), the async transfer worker, static page with ETag/gzip, the security checks of 6.9 | ≤ 300 lines |
| `netsvc.ino` (**new**) | clock helpers and NTP, mDNS (`startNameService`, `validHostName`), GitHub device-report scheduling, reason/trace texts — moved verbatim from `wifi.ino`/`serviceNetwork()` — and the boot self-test `serviceSelfTest()` (6.10) | ~150 lines |
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
| ROUTER_TEST | `WIFI_AP_STA` | up (the driver moves it to the router's channel when the STA associates: *"the channel of the external AP … has higher priority over the ESP AP channel"*, `wifi.rst` §station/AP coexistence; it announces the move to joined phones with a Channel Switch Announcement, `wifi_ap_config_t.csa_count`, default 3 beacons) | `begin()`, auto-reconnect **off** |
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
`esp_wifi_set_country_code("DK", true)` (D27) only when `esp_wifi_get_country()`
does not already report `DK` (setting it restarts the AP and switches the PHY
init data, `esp_wifi.h` attention 5); `WiFi.setSleep(false)`; then
`applyTxPower(false)` (the country resets the power — the 0.15.0 lesson).

`staRadioSetup()` (router and the router test; restores I14): after the
station interface starts and before the join,
`esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20)` and
`esp_wifi_set_inactive_time(WIFI_IF_STA, 20)`, then the same country,
power-save and TX-power steps. Stage 4's `radioApply()` calls it for every
radio that has the station interface.

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
| `LOOK_FULL_EVERY` | 5 | **no phone joined:** every fifth look scans all channels (the router may have moved), always all channels when none is known. **A phone joined:** only the known channel (~0.15 s off channel 6), never a full scan; no look at all when no channel is known |
| `LOOK_DWELL_MS` | 120 | active dwell per channel (the driver's own default, `wifi.rst` s2.1) |
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

Result texts: 201 → "Router not
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
| `bool apStarted` | the boot self-test (6.10), serialcmd, netreport | true on `EV_AP_START`, false on `EV_AP_STOP` or `RADIO_STA` |
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

`esp_http_server` (IDF 5.5) in its own task; the adapter only maps the
handlers' existing calls onto the server's API and must stay under 200 lines
of logic. Anything the server already does (sessions, keep-alive, timeouts,
purge, chunking, wildcard routes, query parsing) is used, not re-done.

```
httpd_config_t c = HTTPD_DEFAULT_CONFIG();   // IDF 5.5 defaults verified in esp_http_server.h:53-79
c.server_port = 80; c.core_id = 1;           // the loop also runs on core 1; Wi-Fi/lwIP stay on core 0
c.task_priority = tskIDLE_PRIORITY + 5;      // above the loop (1)
c.stack_size = 8192;                         // same as the Arduino loop task that ran these handlers until now
c.max_uri_handlers = 96;                     // 83 routes today (default 8)
c.max_open_sockets = 7;                      // default; + 3 internal = 10 of CONFIG_LWIP_MAX_SOCKETS=16,
                                             // leaving 6 for mDNS, SNTP, HTTPClient/TLS and the boot self-test
c.lru_purge_enable = true;                   // Safari keeps up to 6 idle connections per host
c.recv_wait_timeout = 5; c.send_wait_timeout = 5;
c.keep_alive_enable = true; c.keep_alive_idle = 5; c.keep_alive_interval = 5; c.keep_alive_count = 3;
                                             // TCP keepalive: a phone that vanished frees its socket in ~20 s
c.uri_match_fn = httpd_uri_match_wildcard;
c.open_fn = elHttpdOpen;                     // TCP_NODELAY; netboost under #if EL_NETBOOST (4.1)
```

The server is started once in `initializeWeb()` and never stopped;
`httpdSuspend()` (6.2) only closes sessions with `httpd_sess_trigger_close()`.

**Adapter (`class Web`)** — the handlers keep calling `web.arg()`,
`web.hasArg()`, `web.header()`, `web.uri()`, `web.send()`, `web.send_P()`,
`web.sendHeader()`, `web.sendContent()`, `web.clientIsOnAp()`. Each maps to
one server call: `httpd_req_get_url_query_str` + `httpd_query_key_value`,
`httpd_req_get_hdr_value_str`, `httpd_resp_set_status/type/hdr`,
`httpd_resp_send`, `httpd_resp_send_chunk`. POST bodies
(`application/x-www-form-urlencoded`, `URLSearchParams` in the app) are
read with `httpd_req_recv()` into a 2 KB buffer (413 above it) and parsed with
the same `httpd_query_key_value()`; the only EngineLink code is a URL decoder
(`+`, `%xx`) and the origin/host allow-list of 6.9, both in a pure header
`httputil.h` with host tests. The current request is a static pointer valid
only during the handler (single server task); it is never stored.

**Locking.** The adapter takes `engineMx` (timeout 2 s → 503 "EngineLink is
busy") before calling a handler and releases it around every
`httpd_resp_send*` call, so a slow phone never holds the engine task. The
request body is read *before* the lock is taken. The loop keeps holding the
lock while it works and releasing it in `delay(1)`; the server task has the
higher priority and gets it at the next release. No task ever waits for the
server task while holding the lock, so there is no cycle: loop → (lock) →
engine task; server → (lock, bounded) → nothing.

**Long transfers** (`/api/speedtest`, `/api/rtt`, `/api/export/*.csv`,
`/api/log`, `/api/segments.csv`, `/api/backup`, `/api/nmea/bus.csv`,
`/api/trip/series`, `/api/event/series`, `/api/series/recent`,
`/api/export/fast.csv`) use the documented async pattern
(`examples/async_requests`): the handler calls
`httpd_req_async_handler_begin()` and hands the copy to one "xfer" task
(8 KB stack, core 1, priority idle+4, queue depth 1; a second transfer gets
503 "Another download is running"). The worker takes `engineMx` only while it
reads a chunk of data and releases it before `httpd_resp_send_chunk()`. The
worker calls `httpd_req_async_handler_complete()` on **every** exit path (done,
send error, session closed by `httpdSuspend()`); the header warns that
otherwise the server stops accepting connections. `ChunkOut` keeps only its
8 KB send buffer; the miniz deflate path is removed (D26), so every export is
plain chunked `text/csv` (or `application/json` for the backup) with no
`Content-Encoding`. `ChunkOut` stays single-instance; the queue depth
guarantees it.

**Uploads** (`/api/update`): the app sends the file as the raw body
(`application/octet-stream`, `esp_http_server` has no multipart parser);
the handler takes `Content-Length` (reject < 100 kB or > 3.2 MB, as the
GitHub path does), `Update.begin(len)`, reads 2 KB blocks with
`httpd_req_recv()` without holding `engineMx`, checks the image header as
today, and `Update.end()`; a truncated upload now fails by length instead of
flashing a short image.

**Limits:** URI ≤ 512 B and headers ≤ 1024 B (server config), bodies ≤ 2 KB
except uploads, query values validated per handler as today, one async
transfer at a time, 7 client sockets.

**Memory:** +8 KB server stack +8 KB xfer stack; −8 KB live task, −4 KB speed
task, −4 KB self-check task, −4 KB link-tune task, −`HTTP_POOL` clients. The
report prints the server and xfer tasks' stack high-water marks
(`uxTaskGetStackHighWaterMark`).

Ports 81 and 82 are gone; the app's `url.port='81'`/`'82'` lines
(`app.html:325, 2198`) are removed (P9).

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

### 6.9 Security model (local, offline-first, one owner)

Threats that apply: a device on the boat router's LAN, and a web page open in
the owner's browser while that browser can reach EngineLink (cross-site
requests, DNS rebinding). Threats that do not apply: the internet (EngineLink
is never port-forwarded; it only makes outbound TLS calls to GitHub).

What 0.16.5 does: no authentication; `Access-Control-Allow-Origin: *` on
every response; 40 state-changing POST routes incl. `/api/factory-reset`,
`/api/update`, `/api/wifi/*`; OTA from GitHub over TLS with the built-in CA
bundle and a URL pinned to this repository's releases; the Arduino `Update`
library checks the image magic and IDF verifies the whole image (checksum
and appended SHA-256) in `esp_ota_set_boot_partition()`
(`esp_ota_ops.c:623`, called from `Update.end()`); rollback if the new image
does not confirm itself. Passwords and the GitHub token are stored in NVS
(plain; flash encryption is not available in the prebuilt core), never
returned by any API or report, and the router name is hidden in GitHub
reports.

The model for Wi-Fi 2.0 — the simplest one routers and other LAN devices use;
no login, no users (owner's requirement):

1. **Same-origin only, by allow-list.** The adapter answers with
   `Access-Control-Allow-Origin: <origin>` only when the request's `Origin`
   host is one of EngineLink's own addresses (`192.168.4.1`,
   `<hostName>.local`, `enginelink.local`, the current router IP); otherwise
   no CORS header, so a foreign page cannot read responses. The app still
   works when it hops between those origins (`findHost()`, `preferIp()`).
2. **State changes need a custom header.** Every POST must carry
   `X-EngineLink: 1`; without it the adapter answers 403 before any handler
   runs. Browsers never add a custom header to a cross-site request without
   a CORS preflight, and the preflight fails for foreign origins (1), so
   cross-site POSTs are blocked. The adapter answers `OPTIONS` for allowed
   origins with `Access-Control-Allow-Headers: X-EngineLink, Content-Type`.
   App change: one header in `post()` and in `uploadFw()`.
3. **Host header check.** Requests whose `Host` is not one of the addresses
   in (1) get 421; this defeats DNS rebinding (a foreign name resolving to
   EngineLink's address would otherwise look same-origin to the browser).
4. **Resource limits** as in 6.6 (bodies, URI, headers, sockets, one
   transfer, upload size, 5 s socket timeouts, TCP keepalive).
5. **Firmware images:** TLS + CA bundle + repository-pinned URL (as today),
   IDF's image verification at `set_boot_partition` (as today, now documented
   as the integrity check), `Content-Length` enforced on uploads (6.6), and
   after `Update.end()` the new partition's `esp_app_desc_t`
   (`esp_ota_get_partition_description()`) must have the same `project_name`
   as the running image (`esp_app_get_description()`), which rejects a
   foreign ESP32 application. **Signed images need secure boot / signed-app
   support in the bootloader — not available in the prebuilt core (option B,
   and a key-management decision).**
6. **What stays open, by design:** any device that is on the boat router's
   LAN (knows the router password) can use the app, change settings and
   update the firmware, exactly like the owner's phone. Traffic is plain
   HTTP on the LAN. The own Wi-Fi's factory password `12345678` must be
   changed (STATUS; the app flags it). Serial console = physical access.

The functional compromise: none for the owner's app; a browser tab from
another site can no longer read EngineLink data or trigger actions, and a
tool that scripts EngineLink's API must send the `X-EngineLink` header and a
correct `Host`. Approval P12.

### 6.10 Boot self-test and OTA rollback (official mechanism, no watchdog)

The bootloader's rollback is already on (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`
in the prebuilt core; `Update` returns `verifyRollbackLater()=true`, so a new
image boots in `ESP_OTA_IMG_PENDING_VERIFY`). IDF's instruction
(`esp_ota_ops.h:81-83`): run the application's self-tests, then
`esp_ota_mark_app_valid_cancel_rollback()`. 0.16.5 marks valid after 60 s if
the radio is up — the owner rightly calls that insufficient.

Self-test for Wi-Fi 2.0 (replaces the loopback self-check task and its
heal/restart logic, P11):

```
from 60 s after boot, every 20 s, until 600 s, while rollbackPending:
  pass = httpd handle != NULL
      && (apStarted || STA has an IP)
      && (a non-loopback client received a 2xx response from the server since boot
          || one GET http://127.0.0.1/api/health answered 200 just now)
  pass → esp_ota_mark_app_valid_cancel_rollback(); log "firmware confirmed: <which evidence>"
at 600 s without pass → log, saveTotals(), esp_ota_mark_app_invalid_rollback_and_reboot()
```

The loopback GET is one `NetworkClient` request (10 lines), used only as the
fallback evidence when no phone has connected yet (a file upload from a
laptop that is then closed must not roll back a good image). A real client
response is the strong evidence: Wi-Fi, IP, TCP, server and a handler all
worked. There is no runtime HTTP watchdog: the server task is the official
component, the loop keeps its task watchdog (60 s, panic → core dump →
`crashinfo`), and a hung server would show in the field as "no answer" with
the counters intact for the next report. If that ever happens, the next step
is the official `esp_task_wdt_add()` for the server task, not a custom
monitor.

---

## 7. Acceptance criteria (measured; a release needs all of them)

| Area | Criterion | Measured by |
|---|---|---|
| Standalone stability | 24 h with an iPhone and a PC joined, app open on one of them: 0 `left … code 2` series; ≤ 1 unexplained AP leave (reason 8/3) per hour; every DHCP lease followed by a port-80 connection from that phone within 30 s; station RSSI ≥ −60 dBm at 1 m | Wi-Fi report, SD log |
| Router stability | 24 h on the router: ≤ 2 `lostEvents`, each recovered (IP again) within 15 s; 0 Standalone fallbacks with the router up | report |
| HTTP latency | `/api/live` round trip (the app's RTT stat) p95 ≤ 150 ms on the own Wi-Fi, ≤ 300 ms on the router; app page first load ≤ 2 s on the own Wi-Fi; no non-transfer request > 2 s; `/api/live` p95 ≤ 500 ms while a 1 MB export runs | app stat; `httpdSendTimeouts` = 0 |
| Dead client | a phone put in airplane mode mid-download and mid-polling: the other client's `/api/live` p95 ≤ 500 ms; its socket freed within 30 s (keepalive); `loopPartMaxMs` all ≤ 50 ms | bench 9.2 |
| Throughput | own Wi-Fi ≥ 400 kB/s; router ≥ 300 kB/s with `netboost`; the router figure without it recorded (P8) | "Test speed" ×5 |
| Sockets | 20 parallel `curl` connections held open: the 21st gets served after LRU purge, no `httpd_accept_conn` error, no reboot; sockets back to idle count within 60 s | bench 9.2 |
| Security | from a foreign origin: GET answered without `Access-Control-Allow-Origin`; POST without `X-EngineLink` → 403; `Host: evil.example` → 421; a 3 KB form body → 413; a 4 MB or truncated upload → rejected, no flash write started or `Update` aborted; passwords never appear in `/api/state`, `/api/diag`, `/api/backup`, the report or the SD files | bench 9.2 `curl` list |
| Scan / look | user scan ≤ 4 s and lists the boat router; a look on the known channel ≤ 0.3 s; a phone stays joined through 60 consecutive looks | report (`looks`, `apLeaves`), stopwatch |
| Recovery | router off 5 min → `standalone` at 120 ± 5 s; router back → `router` ≤ 90 s with no phone joined; boot with router: IP ≤ 5 s median, ≤ 180 s worst, else Standalone; wrong code → result ≤ 25 s with the AP never down | 9.9 T2–T6 |
| Phone never cut off | T5: 30 min with a phone joined and the router back: `returns == 0`, phone never dropped; "Switch now" → router ≤ 25 s | D21 |
| Self-test / rollback | a new image confirms within 2 min of the app reconnecting (log says which evidence); the broken-`startAP()` test build rolls back at 600 s by itself; a build with a broken route table rolls back | 9.6 |
| Memory | after 24 h: lowest free heap ≥ 40 KB, largest block ≥ 32 KB, free heap not falling more than 2 KB/h; server and xfer stack high-water marks ≥ 1 KB; `wifiEventOverflow` 0 | report |
| Loop / engines | `engineWait max` < 200 ms during every test; `loopPartMaxMs[network]` ≤ 1 500 ms at a state change, ≤ 20 ms otherwise; engine report answered-rate and NMEA frame count unchanged during Wi-Fi tests | engine report, Wi-Fi report |
| Radio truth | report shows AP bandwidth 20 MHz, inactive time 600 s, country `DK`, TX "driver reports 19.50 dBm" in both modes, `txReapplied` stable | report |
| Exports | every export route delivers byte-identical content to 0.16.5 on the same data (bench diff, 9.2), no `Content-Encoding` on CSV, a 1 MB export completes on the own Wi-Fi in ≤ 10 s | bench 9.2 |

---

## 8. Test strategy

Tests prove behaviour; a few structural contracts guard the lessons that only
a build can break.

1. **Host tests** (CI, `g++ -std=c++17 -Wall -Wextra`), pure headers without
   Arduino: `tools/test_wifistate.cpp` (9.3 scenarios);
   `tools/test_tracefold.cpp`; `tools/test_httputil.cpp` for `httputil.h`
   (URL decoding incl. `+`, `%2B`, bad `%` sequences, truncation at 2 KB;
   `originAllowed()`/`hostAllowed()` with the four own addresses, a foreign
   name, a foreign IP, a port suffix, upper-case, an empty header).
2. **Structural contracts** (CI, `tools/test_wifi_contract.py`, 9.10): only
   what cannot be executed on a host.
3. **Compile** (CI): unchanged command, core pinned (D25), the
   `__wrap_esp_wifi_init` objdump check.
4. **Bench** (owner; USB from a charger; iPhone + PC; a laptop with `curl`):
   the functional lists in 9.1, 9.2, 9.5, 9.6 — each item is a command or an
   action with an expected observable result.
5. **Boat** (owner): 9.9. D24: no merge, no release before it passes.

---

## 9. Stages

Each stage is one or two commits on the Wi-Fi 2.0 branch, compiles, passes
CI, and can be read on its own. Order matters. Stage 0 gates everything;
Stage 1 may be built in parallel with it (its fixes are cause-independent and
produce the diagnostics the gate needs).

### 9.0 Stage 0 — Root-cause gate (section 13.6)

The gate (13.6) is the owner's work and splits into **physical measurements
on the boat** (steps 0, 3 and 4: router channel/signal, router-off test,
oscilloscope on 3V3/5 V) and **bench work at home** (steps 1 and 2 with USB
from a charger and the version archive).

What proceeds without waiting: Stage 1 and Stage 2 (built and bench-verified
on the owner's bench, they are cause-independent and produce the diagnostics
the gate needs), Stage 3 (host tests only). What waits for a row in the 13.6
decision table: Stage 4 onwards, because their bench and boat acceptance
needs a standalone link that is known to be reliable. If the row is H3
(firmware regression), the owner decides whether Stage 1 ships alone first
(13.7); if H1/H2/H4, the environment/hardware is fixed first.

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

Bench acceptance: report shows 20 MHz / 600 s / country `DK` / 19.50 dBm in both modes;
station RSSI printed; a router switched off for 2 min yields < 10 trace
entries. No behaviour change otherwise. (The earlier "bounded writes"
workaround is **dropped**: Stage 2 removes the cause.)

### 9.2 Stage 2 — `esp_http_server`, security model, boot self-test

1. `httpd.ino` per 6.6; `httputil.h` (URL decoder, allow-lists) with
   `tools/test_httputil.cpp`.
2. Route table unchanged in `webapi.ino`; handlers that used
   `web.client()`/`web.upload()` adapted (`downloads.ino:34`,
   `webapi.ino:870-`); the OPTIONS preflight route; the `X-EngineLink` and
   `Host` checks in the adapter's dispatch (6.9).
3. Delete `webserver.h`, `liveHttpTask`, `speedHttpTask`,
   `httpSelfCheckTask`, `NetworkServer liveServer/speedServer`,
   `netWaitBegin/End`, `lastHttpMs`, `httpBusySince`, `httpHeal*`,
   `httpSelf*`, the `web.rebindNetwork()` call.
4. `netboost.cpp` behind `#if EL_NETBOOST` in `open_fn` (4.1, P8).
5. Boot self-test (6.10) replaces the 60 s radio check at
   `EngineLink.ino:601`; the `esp_app_desc_t` project-name check after
   `Update.end()` in both OTA paths.
6. App: remove `url.port='81'`/`'82'`; `post()` and `uploadFw()` send
   `X-EngineLink: 1`; `uploadFw()` sends the `File` as the body. `python3
   tools/build_ui.py`.
7. Remove the deflate path (D26): `ChunkOut.z`, `zComp`, `acceptsDeflate()`,
   the `compress` parameter of `beginDownload()` and its seven call sites, the
   `Content-Encoding: deflate`/`Vary` headers, `dlStat.z`, `#include "miniz.h"`;
   `test_router_throughput_guard.py` loses its `handleExportRaw … false`
   assertion. Routes, column headers, row content and file names stay.
8. `tools/test_wifi_contract.py` server items (9.10).

Export regression check (do it **before** flashing Stage 2 on the bench
board): on 0.16.5 download every export route — `/api/export/trips.csv`,
`/api/export/raw.csv`, `/api/export/series.csv`, `/api/export/fast.csv`,
`/api/segments.csv`, `/api/log`, `/api/backup`, `/api/nmea/bus.csv`,
`/api/trip/series`, `/api/event/series`, `/api/series/recent` — with
`curl --compressed -o before/<name>`; after flashing Stage 2 on the same
board and data, `curl -o after/<name>` and `diff -r before after` (only
fields that are time-of-download by design may differ; list them in the
report). Also `curl -s -D- -o /dev/null http://192.168.4.1/api/export/trips.csv?ids=<id> | grep -i content-encoding`
must print nothing.

Bench acceptance (before Stage 3), each with the expected result:

| Action | Expect |
|---|---|
| iPhone + PC, app open on both, 30 min | section 7 latency row; `httpdSendTimeouts` 0 |
| PC: `curl -s -D- -o /dev/null http://192.168.4.1/api/live` | 200, `Content-Type: application/json`, no `Access-Control-Allow-Origin` |
| `curl -s -D- -o /dev/null -H 'Origin: http://enginelink.local' http://192.168.4.1/api/live` | `Access-Control-Allow-Origin: http://enginelink.local` |
| `curl -s -D- -o /dev/null -H 'Origin: http://evil.example' http://192.168.4.1/api/live` | no `Access-Control-Allow-Origin` |
| `curl -s -D- -X POST -d 'kbs=1&rtt=1' http://192.168.4.1/api/speedlog` | 403 |
| same with `-H 'X-EngineLink: 1'` | 200 |
| `curl -s -D- -H 'Host: evil.example' http://192.168.4.1/api/live` | 421 |
| `curl -s -D- -X POST -H 'X-EngineLink: 1' --data-binary @3k.txt http://192.168.4.1/api/cfg` | 413 |
| `curl -s -D- -X POST -H 'X-EngineLink: 1' --data-binary @4mb.bin http://192.168.4.1/api/update` | 4xx, `otaBytes` 0, no restart |
| `head -c 500000 enginelink-update.bin` uploaded as a 1.5 MB `Content-Length` (`curl --header 'Content-Length: 1500000'`, then cut) | `Update` aborted, no restart, app still answers |
| `for i in $(seq 20); do curl -s http://192.168.4.1/api/live >/dev/null & done; wait` | all 200; `curl` #21 after that: 200; `httpd_accept_conn` not in the serial log |
| phone in airplane mode during a 1 MB export; other phone polling | other phone's live p95 ≤ 500 ms; export ends with `httpd_req_async_handler_complete` logged; socket freed ≤ 30 s |
| 20 page reloads in a row on Safari | all complete; `max_open_sockets` never exceeded (serial) |
| OTA from a file (valid image) | restart, "firmware confirmed: client response" in the log within 2 min |
| grep of `/api/state`, `/api/diag`, `/api/backup`, the report and `/WIFIREP.TXT` for the AP and router passwords | nothing |
| the export regression check above | `diff` clean; no `Content-Encoding`; a 1 MB raw export ≤ 10 s on the own Wi-Fi |

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
→ STANDALONE; 6 looks at 60/120/180 s; `lookKind`: phone joined → `LOOK_CHANNEL` when known, `LOOK_SKIP` when unknown (never `LOOK_ALL`); no phone → `LOOK_ALL` every fifth and whenever unknown, else `LOOK_CHANNEL`; 7 look seen, no phone → RETURN → OK → ROUTER; 8 look
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

Lock rule (13.6.1; owner 2026-11-06): `radioApply()`, the station start,
`httpdSuspend()`'s wait and every other network switch run with `engineMx`
let go. The machine's state stays loop-owned; handlers only set `pendingCmd`
and read values the loop published (the `Host` check uses the loop's copy of
the router IP, not `WiFi.localIP()` mid-switch). 0.16.6 measured 1.5–2.5 s
engine waits at a switch made with the lock held.

Bench acceptance: standalone boot, phone joins, app loads, `state
standalone`; heap lowest ≥ 40 KB after 1 h; router saved and present → `router`
within 5 s; router off → `router lost`, `standalone` at +120 s; router back →
seen within 60 s and (no phone) `router` within 25 s; across at least five
switches (boot with router, router lost → standalone, return, forget, own
Wi-Fi renamed) the report's engine task longest wait < 200 ms, with the lock
holder it names.

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

### 9.6 Stage 6 — Rapid boots, rollback test builds, log texts

D9 counter + `boots3` in the report; `serialcmd.ino` boot line uses the state
name; `serviceWifiLog()` logs state changes. Rollback acceptance with two
throw-away test builds (never released): (a) `startAP()` returns false and
no router saved → the self-test never passes → rollback at 600 s, the old
image boots; (b) `httpd_start()` fails (route table too large on purpose) →
same. Also T7.

### 9.7 Stage 7 — Report, cleanup, docs, CI

`netreport.ino` per 6.8; `diagnosis.ino` wifi block; delete `linktune.*`,
`netpolicy.h`, `routerguard.h`, their tests; `test_router_throughput_guard.py`
trimmed and its comment corrected; `HANDOVER.md` Wi-Fi section and code-layout
table (`wifi.ino`, `httpd.ino`, `netsvc.ino`, `wifistate.h`, `txpower.h`;
drop `webserver.h`, `netpolicy.h`, `linktune`, `routerguard.h`);
`docs/STATUS.md` rows "0.17.0 untested" until 9.9; `docs/TODO.md`.
Acceptance: `wc -l wifi.ino < 300`, `httpd.ino < 300`, `netreport.ino < 220`;
`grep -rn "linkTune\|netpolicy\|routerGuard\|staSecMode\|WebServer.h\|NetworkServer" firmware tools .github` empty; CI green.

### 9.8 Stage 8 — App (D20 and P9; `firmware/ui/app.html`, then `tools/build_ui.py`)

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
4. The Stage 2 app items (ports, upload body, `X-EngineLink` header) are
   already in; verify the home-screen app on iPhone still loads from both
   origins and that `preferIp()` still switches to the router IP.
5. `tools/test_mobile_ui_contract.py` keeps passing; Playwright desktop and
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
| T11 | D30, on the bench: a phone hotspot (iPhone with "Maximize Compatibility", and an Android phone) as the router, each T3 + T4 | `ok`, IP, recovery as T4; nothing changed on the phone beyond turning the hotspot on |
| T12 | D30: the router's channel changed (its automatic choice or by hand) while EngineLink is on it | back on the router without a fallback (CSA, else the core's full-scan reconnect within the 2 min of D4) |
| T13 | D30: a router with no internet (WAN unplugged) | `router` state, app works by IP and `.local`; NTP and GitHub only report "no internet" |
| T14 | D30: WPA2/WPA3 mixed and, where available, WPA3-only | joins (WPA2 path for mixed, SAE for WPA3-only, D10) |

Then `docs/RELEASING.md`: bump `EL_VERSION`, README badge,
`docs/releases/v0.17.0.md` (what changed, the protocol results, what is
untested), merge, release. `docs/STATUS.md` says "verified on the board" only
for rows T1–T10 passed.

### 9.10 `tools/test_wifi_contract.py` (structural contracts only)

Everything executable is in the host tests (section 8, item 1) or the bench lists; this file
keeps the lessons that only a build or a code review can break:

```
w = wifi.ino; h = httpd.ino; fw = firmware_source()
- len(w.splitlines()) < 300                                                  (D14)
- body(w,"void onWiFi(") contains no "WiFi." / "MDNS." / "esp_wifi_"          (D14, F5)
- count("WiFi.mode(") in fw == count in body(w,"void radioApply(")            (one place changes the mode)
- "httpdSuspend()" precedes every "WiFi.mode(" in radioApply                  (driver-buffer lesson)
- body(w,"void startAP(") has the exact softAPConfig/softAP lines of 6.2      (D2)
- "scanNetworks(false" not in fw                                              (async only)
- 'esp_wifi_set_country_code("DK"' in body(w,"void apRadioSetup("); '"EU"' not in fw   (D27)
- "tdefl_" not in fw; "deflate" not in fw; "miniz.h" not in fw                   (D26)
- not re.search(r"print[f]?\([^;]*(apPassword|wifiPassword|ghToken)", fw)     (never log secrets)
- "saveSettings()" not in body(webapi,"void handleWifiConnect("); "cfg.ssid=" not in it nor in handleOptions   (F9/P7)
- "#include <WebServer.h>" not in fw; "NetworkServer" not in fw; "httpd_stop(" not in fw
- "lwip/priv" appears only in netboost.cpp; "__wrap_" appears only in wifiwpa3.cpp   (section 4: no third exception)
- "esp_ota_mark_app_valid_cancel_rollback" appears exactly once, inside body(fw,"void serviceSelfTest(")   (6.10)
- "setTxTimeoutMs(0)" after "Serial.begin" in setup()
- "wifistate.h", "httputil.h" have no "#include <Arduino.h>", no "WiFi", no "millis("
- grep: no "netpolicy", "linkTune", "routerGuard", "staSecMode", "radioTestWanted", "lastHttpMs" in firmware/ or .github/
- "esp32:esp32@3.3.12" in .github/workflows/*.yml                              (D25)
```

---

## 10. Implementation rules for whoever builds it

- Follow the stages in order; Stage 0 first; do not start Stage 4 before
  `test_wifistate.cpp` passes. Stage 3 is host code only (`wifistate.h` and its
  test) and may be written while the Stage 2 bench list is pending (9.0);
  integrating it into `wifi.ino` is Stage 4, which waits for the Stage 0 gate
  and the Stage 2 bench report. (Corrected 2026-11-05: the earlier wording
  here contradicted 9.0.)
- D23: before writing a helper, name the IDF/core API that does it; if there
  is none, say so in the commit message. No new file may include
  `lwip/priv/*`, wrap a core function or touch driver initialisation; the two
  exceptions in 4.1 are the only ones, and a third needs the owner's written
  approval and its own 4.1 entry first.
- Never call `WiFi.*`, `MDNS.*`, `esp_wifi_*` from the event callback, an
  HTTP handler or another task. Handlers set flags; `serviceNetwork()` acts
  ≥ 1.2 s later. Handlers never keep an `httpd_req_t*` beyond their return
  except through `httpd_req_async_handler_begin()`, and never call
  `httpd_stop()`.
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
- D28: commit messages, PR text, `docs/STATUS.md` and release notes use the
  status vocabulary (*built* / *bench-verified* / *boat-verified*); firmware
  that has not run on the board is "built, untested on hardware", never
  "verified".
- D29: no architecture revision; a concrete critical defect found while
  building goes into section 12 with evidence, and work stops at that stage
  until the owner decides.

---

## 11. Owner decisions and open approvals

Answered (history): P1 approved → D19; P2 approved → D20; P3 rejected → D21;
P4 deferred → D22; P5 (20 s test budget), P6 (`apch` honoured), P7
(`/api/options` through the router test): no objection.

Final decisions (owner, 2026-11-05 — planning closed, D29):

| # | Proposal | Decision |
|---|---|---|
| P8 | Keep `netboost.cpp` fenced until option B | **approved** with the conditions in 4.1 |
| P9 | Ports 81/82 removed from the app, upload as a raw body, `X-EngineLink` header | **approved** |
| P10 | Option B as a later plan | **deferred** |
| P11 | Boot self-test and automatic rollback instead of the loopback self-check | **approved** |
| P12 | Security model 6.9 | **approved** with its documented limits |
| P13 | `sha256` per release entry | **deferred** |
| — | Remove deflate compression of downloads | **approved** → D26 |
| — | Country configuration | use Espressif's API with a real ISO code → D27 |

Original proposals, kept for the record:

| # | Proposal | Recommendation | Why |
|---|---|---|---|
| P8 | Keep `netboost.cpp` as the one private-structure exception until option B, behind `EL_NETBOOST`, with the no-boost router throughput measured in T8 | **keep** | no supported alternative under A (4.1); 54 lines; a struct change fails the build, not the boat |
| P9 | App changes beyond D20: ports 81/82 removed, upload as a raw body, the `X-EngineLink` header on POSTs | **approve** | consequences of `esp_http_server` and of the security model; three small edits |
| P10 | Option B as a separate later plan under the triggers in 3.2 | **defer**; decide after T10 | measured need first |
| P11 | Replace the loopback self-check task and its heal/restart logic by the boot self-test of 6.10 (official rollback API; a real client response or one loopback GET as evidence; rollback by itself at 600 s) and no runtime HTTP watchdog | **approve** | the thing the old task guarded (a single-threaded server hanging the loop) no longer exists; the self-test is what IDF's OTA documentation asks for |
| P12 | Security model 6.9: no login; CORS by own-origin allow-list, `X-EngineLink` header on POSTs, `Host` check, limits, OTA project-name check; LAN devices keep full access | **approve** | the simplest model LAN appliances use; blocks cross-site and rebinding attacks without users or passwords |
| P13 | Add `sha256` per entry to `releases.json` (workflow) and verify it in the GitHub OTA path with mbedtls | **optional, later** | IDF already verifies the image's own checksum and SHA-256 at `set_boot_partition`; this would only add a check against the published list |

Consequence of D21 the owner should keep in mind: a phone that stays
associated to the own Wi-Fi holds EngineLink in Standalone (NTP, GitHub
reports and "Check for updates" wait). The AP drops a silent phone after the
driver's inactive time (600 s, 6.2), so a return can lag a departed phone by
up to 10 min; shortening it is **not** in this plan.

---

## 12. Review log and open risks

Reviews (history, all folded into sections 4–9): the 2026-11-05 final review
R1–R9 (rapid boots vs automatic return; AP channel after a failed test; two
auth failures before "wrong code"; FreeRTOS queue instead of a hand ring;
hidden SSIDs; auto-reconnect off before the AP comes up; `lastJoinUsedSae`;
T6 wording; a stale comment); the third revision (bounded-write workaround
dropped; four hand-made server pieces replaced by `esp_http_server`;
`wifi_provisioning` rejected with reasons; CI core version pinned); the
**fourth revision (quality pass, 2026-11-05)**: the two exceptions justified
against the headers with supported alternatives checked (4.1); the adapter
bounded (≤ 200 lines of logic, every call mapped to one server API, lifetimes
and cleanup stated); TCP keepalive and the socket budget made explicit;
looks with a phone joined restricted to the known channel (no full scans
while anyone is connected); the SETUP-test channel move explained through
the driver's CSA; the rollback confirmation upgraded from "radio up" to a
real self-test with automatic rollback (6.10); a security model added (6.9);
tests moved from line-presence checks to host tests of pure functions and
`curl`-level bench checks (7, 8, 9.2, 9.10); the **fifth revision (closing,
2026-11-05)**: the owner's final decisions recorded (section 11, D26–D29),
deflate removal placed in Stage 2 with an export regression check, the
country configuration set to the documented `esp_wifi_set_country_code("DK",
true)`, Stage 0 split into physical and bench work, the status vocabulary and
the implementer instructions added.

Open risks:

- `WIFI_AP_STA` idle STA (D19): RAM and a driver path the board has not run
  long; measured in 9.4 and T1/T10; revert switch exists.
- Whether iPhones follow the softAP's Channel Switch Announcement during a
  SETUP test without dropping: documented driver behaviour, not yet observed
  on this boat (T2/T3 measure it; the app recovers either way).
- Handlers now wait for `engineMx` released by the loop; a long loop part
  (raw clean-up 461 ms, serial 584 ms with a console attached) delays a
  request by that much. The latency criterion catches it; the fix is
  shortening that loop part, not a second lock. The SD parts of it let the
  lock go since I10 (12.1).
- Pre-existing: `String` fields written by `fwWorker` (`fwList`, `fwError`,
  `reportError`) are read by handlers without a lock; set-then-flag order
  makes it benign today; noted, not changed.
- A phone that never leaves keeps EngineLink off the router (D21).
- iOS behaviour on networks without internet (F15) is outside the firmware.
- The core's immediate reconnect (D3) hammers a refusing router every 1.6 s
  for 2 min; the trace no longer floods.
- No signed firmware images under option A (6.9 item 5); the trust anchor is
  TLS to GitHub and the pinned repository URL.
- The root cause of the 2026-11-05 downlink failure is still open (section
  13); nothing above fixes a hardware or RF fault.

### 12.1 Implementation findings (2026-11-05, while building Stages 1–3)

None of these changes the architecture (D29); each is a parameter or a
route assignment the plan left too tight, with the evidence that forced it.

| # | Finding | Evidence | Resolution in 0.16.6 |
|---|---|---|---|
| I1 | A flat 2 KB form-body limit (6.6) breaks saving app preferences and restoring a backup. | `handlePrefsSet()` accepts up to 4000 characters of JSON; the app posts it URL-encoded (`p=%7B%22…`), up to ~12 KB; the default preferences alone are ~1 KB encoded (`app.html` `const P`). | `/api/prefs` POST has a 12 KB limit (`R_BIGBODY`); every other route keeps 2 KB. Larger bodies get 413. |
| I2 | Making the series JSON routes (`/api/trip/series`, `/api/event/series`, `/api/series/recent`) one-at-a-time async transfers breaks the app. | `app.html` loads several series at once (`Promise.all` over trip parts, and event series beside event fast data); the second would get 503. | These bounded JSON replies (≤ 600 points) stay in the server task; the engine lock is released while they are sent. All CSV exports, `/api/log`, `/api/segments.csv`, `/api/backup`, `/api/nmea/bus.csv`, the motor-test CSV and `/api/speedtest` are async as planned. `/api/rtt` (an empty 204) is answered in the server task so it measures a request round trip, not the transfer queue. |
| I3 | The OTA project-name check (6.9 item 5) is weaker than written. | Every Arduino-built image carries the core's `esp_app_desc_t.project_name` `arduino-lib-builder` (read from the 0.16.6 image at offset 0x50). | Implemented (see I8 for where); it rejects a non-Arduino ESP32 application, not another Arduino sketch. A stronger check needs option B (own `project_name`) or signed images; not changed. |
| I4 | A refused large upload would be drained by the server before the next request. | `httpd_req_delete()` reads any unread body in 32-byte pieces (`httpd_parse.c`, IDF v5.5). | A refused body over 16 KB closes the connection (the handler returns `ESP_FAIL`, which skips the drain, `httpd_sess.c`); smaller ones are drained and the connection is kept. |
| I5 | `curl -sI` sends HEAD, which no route answers (405), so the Content-Encoding check in 9.2 proved nothing. | `esp_http_server` dispatches by method. | 9.2 now uses `curl -s -D- -o /dev/null`. |
| I6 | Handlers now run in the server task, so `handleCfg()` restarting mDNS inside a request would race the loop's own restart. | `webapi.ino` `handleCfg()` called `startNameService()` (F10). | The Stage 4 rule (set `mdnsRefreshPending`, the loop acts) applied in Stage 2. `removeTree()` gives the loop a turn every 16 files. |
| I7 | Without the engine task the loop did not hold `engineMx`, so a handler in the server task could run beside the loop's engine work. | `setup()` gave the lock back when the task failed to start. | The loop holds `engineMx` whenever it exists; `netWaitBegin/End` test `engineMx`. |
| I8 | Checking the project name *after* `Update.end()` (6.9 item 5 as written) and switching back on a mismatch makes the rejected image the rollback target. | `esp_ota_set_boot_partition()` rewrites the OTA data with the new entry in state NEW (`esp_ota_ops.c` `esp_rewrite_ota_data()`, `set_new_state_otadata()`, IDF v5.5); switching back writes a second NEW entry, so the running firmware is pending again and the other slot holds the rejected image. Found by the Stage 2 review. | The same `esp_app_desc_t.project_name` is read from the streamed image (first 0x70 bytes, `otaheader.h`, host-tested) and compared before `Update.end()`; a mismatch aborts the update. The boot partition is never switched by hand. |
| I10 | SD card work held `engineMx` for seconds (13.6.1): `/api/sd/info` counted the root directory and free space in the request (7.3 s), the daily raw clean-up walked 4 entries a pass (485 ms), the Wi-Fi log opened its file inside the loop (~300 ms), wipe gave the lock back only every 16 files. | 0.16.6 bench reports (engine waits 7.3–7.5 s, loop parts `raw cleanup` 485 ms, `serial` 227–306 ms); every Arduino `openNextFile()`/open by name searches the whole directory (`vfs_api.cpp`, FatFs). | One root-directory walk in the loop (`serviceSdWalk`, 4 entries a pass) counts for `/api/sd/info` and does the daily clean-up; every card access in it, in `removeTree()`, the Wi-Fi log and the saved report runs with the lock let go (`sdGapBegin/End`, which only lets go a lock the caller holds). `/api/sd/info` answers from the last count at once (`known`, `counting`; the app asks again). From the 2026-11-05 blocking audit (`docs/FIRMWARE_BLOCKING_AUDIT_2026-11-05.md`): the serial `wifi log`/`wifi reports` export stops at its first 2 s timeout and runs without the lock (item 1), and the SD log takes the trace and its total in one critical section (item 6). No unmount while card work or a web reply runs without the lock (`sdUnmountBlocked`: a removed card stops a download and unmounts on a later check); remount, wipe and factory reset answer 409 meanwhile. The engine task's own raw-log writes are unchanged (see 13.6.1 residual risk). `tools/test_sd_engine_lock.py`. |
| I11 | A 503 could not be attributed (13.6.1), nor an engine wait to its cause. | One counter for three causes. | 503s counted by cause (engine lock 2 s, another download, download lock 10 s) with the last path; the report names the task holding `engineMx` when the engine task's longest wait began, counts waits over 100 ms and the engine task's longest round of its own work; the report's firmware line carries the build time. |
| I12 | D30: the station join uses the core's default `WIFI_FAST_SCAN`, which takes the first matching access point; the "strongest" sort applies only to an all-channel scan (`esp_wifi_types.h`). With a mesh or repeater (one SSID, several BSSIDs) it can join a weak one. | `STA.cpp` 3.3.12: `_scanMethod(WIFI_FAST_SCAN)`, `_sortMethod(WIFI_CONNECT_AP_BY_SIGNAL)`. | Stage 4: `WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN)` before `begin()` (official core API; about 1–2 s more per join). Minimum security stays the core's WPA2 (open and WPA-only networks are not joined). |
| I13 | D30: with a phone joined, looks use the known channel only (fourth revision, to keep the phone connected). A router that changed channel meanwhile is not seen, so "Router is back" waits until no phone is joined (then every fifth look is all channels). | 6.3 look rules; T12. | Kept (the phone's link wins, D21). Recorded so T12 is read correctly; the station itself follows a channel change through CSA or its full-scan reconnect. iPhone hotspots on iPhone 12 and later need "Maximize Compatibility" for 2.4 GHz: the ESP32-S3 has no 5 GHz radio. |
| I14 | F19 was incomplete: 0.16.2 also dropped the **station** settings every version up to 0.16.1 had, `esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20)` and `esp_wifi_set_inactive_time(WIFI_IF_STA, 20)`; Stage 1 restored the AP half only. Without them the station joins a 20/40 MHz router at the driver's default 40 MHz and drops after 6 s without beacons. | `git grep` on 0.13.3 (`a292909` `wifi.ino:198,396`), 0.15.0 and 0.16.1 shows both; 0.16.2 (`7ade549`) and later none. 0.13.4 measured 584 kB/s on the router with them (#126); every 0.16.x router run since failed bulk replies: 2026-11-06 `/` 10.9 s with 14 send failures, reported 2026-11-08 (owner, lab supply, router at −42 dBm) `/api/live` (~2.5 KB) answered every time while `/` (110 KB) took 20.5 s and failed, 5 send failures, standalone 1048 kB/s. | `staRadioSetup()` sets both before the join (6.2), contract in `test_wifi_contract.py`. The most likely cause of the router bulk failures by elimination: power save is off, the TCP window boost and the server path are the same as in standalone, and a reply bigger than one packet gets through. **Not proven** until the router run is repeated (13.6.1). |
| I15 | The router block of the report and `/api/diag` printed counters and "last router signal/channel" that nothing wrote since 0.16.2 (F6 leftovers): "Successful associations: 0", "0 dBm / channel 0", "Router recovery fallbacks: 0" while the router worked. A report after a router failure could not say whether EngineLink had left the router. | 2026-11-06 `ROUTER.txt`; build-3 evidence (2026-11-08) has no report at all. | The event callback writes associations, IP gains/losses, disconnects (drops of a live link apart from failed attempts; reason 8 is EngineLink's own leave), the router's channel and the negotiated security, from the event data only (D14); the loop samples the router's signal every 2 s, so the last value before a loss stays; fallbacks are counted. The report also reads back the station's beacon timeout. The SD Wi-Fi log adds the router's channel and last signal to every router join, IP and loss line. `test_wifi_contract.py`. Stage 4/7 replace these fields by 6.8 as planned. |
| I16 | Every router run on this board shows one pattern: small answers work, then the router's beacons are lost and 49 full scans in 2 min do not find it, while the own Wi-Fi started right after hears the phone at once and the router still lists EngineLink. That is a station that keeps transmitting but stops receiving until the driver restarts (build 3's unanswered ARP fits). The Prism controller (`Kaelor9/Marine-RGB-Light-Controller`, WiFiManager, Arduino `WebServer`) works on the owner's routers with the driver's defaults: it sets no bandwidth, beacon timeout, country, power save or TX power and has no WPA3 wrapper. Router mode has been unstable on EngineLink since before Wi-Fi 2.0 (#126: 95 disconnects on 0.13.4; A-MPDU experiments in 0.12.5–0.12.8 rolled back without a result). | `ROUTER.txt`, `LAB-corrected-password.txt` (2026-11-06), build-3 evidence (2026-11-08), Prism `Marine_RGB_Controller.ino` `connectWiFi()`. | A/B test build, never released: `EL_STA_DEFAULTS=1` (CI artifact `EngineLink-AB-test-router-driver-defaults-<sha>`) skips `staRadioSetup()`'s settings and the router-mode TX power and turns modem sleep on, as Prism; everything else (WPA3 wrapper, server, data) is the same. It separates EngineLink's station settings from the board and the place: B passes the router criteria ⇒ one of those settings causes it (then halve them); B fails like build 3 ⇒ not the firmware's radio settings (then Prism on the same router and spot, and the I15 log: a steady last signal before the loss means the receiver stopped, a falling one the RF path). Normal builds are unchanged (`EL_STA_DEFAULTS 0`, contract). |
| I9 | Review of the Stage 2 server (2026-11-05) found further lifecycle gaps. | Code review against the IDF sources. | Every early reply (421, 403, 503) closes a connection whose unread body is over 16 KB instead of draining it in the server task; a refused upload up to 3.2 MB is read to its end so the app shows the reason (a reset would hide it); a broken transfer is `shutdown()` before `httpd_req_async_handler_complete()`; `httpdSuspend()` also `shutdown()`s every client socket (trigger-close skips sockets that sent nothing yet and its queue holds 6 messages); remount, wipe and factory reset answer 409 while a download keeps an SD file open; a file upload answers 409 during a GitHub install and the GitHub install during an upload; `removeTree()` sleeps 20 ms every 16 files so the loop can feed its watchdog during a long wipe. Left open: the 110 KB app page is sent from the server task (a slow phone delays other requests while it loads); GET routes with side effects (e.g. starting a Wi-Fi scan) can still be triggered cross-site, without reading the answer. |

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

### 13.6.1 Gate status after the owner's bench reports of 2026-11-06

Evidence: `docs/test-evidence/wifi-0166-2026-11-06/` (eight 0.16.6 reports,
oscilloscope notes). Assessment by the implementer, 2026-11-06, no code changed.

**Gate: still unclassified.** No row of the decision table applies: step 1
ran on 0.16.6 instead of 0.16.5, steps 2 and 3 were not run. What the
evidence does support: the boat's failure (L2–L4: lease without TCP, code
2/209 series) did **not** recur on the bench with 0.16.6 (30 min, two clients,
0 AP leaves, 6658 successful answers, 0 send failures); the bench code 2/209
entries are EngineLink's own mode changes (router attempt after 10 min,
scan, password correction), not spontaneous series; H2 weakened on the bench
(3.3 V never below 3.25 V) but not excluded on the boat (12 V and USB+12 V
never measured); H4 (a degraded board) weakened, the same board delivers
700–1100 kB/s; H1 versus H3 can only be told apart on the boat.

**Minimal discriminating test: 13.6 step 3 with 0.16.6** (boat, 12 V only,
iPhone + PC on the own Wi-Fi; 15 min router on, then 5 min router powered
off). Stable both ways ⇒ H3 (the 0.16.2 AP settings, restored by Stage 1) and
the gate closes; stable only with the router off ⇒ H1; unstable both ways ⇒
step 4 on the 5 V and 12 V nodes while 12 V is connected. No further scope
work otherwise.

Findings from the reports (for Stages 4–7 and the owner):

- **Throughput follows the request round trip.** Slow runs show `/api/rtt`
  at ~100 ms (101–105, ~204 ms): one and two beacon intervals of 102.4 ms;
  fast runs ~10 ms. Both phones were fast and slow at the same RSSI and
  supply, so phone power save or channel-6 noise is more likely than the
  supply or the firmware. Cheap test: "Test speed" ×5 alternating iPhone and
  a mains-powered PC at the same spot; a steady ~10 ms PC and a varying phone
  ⇒ phone power save; both varying ⇒ the channel.
- **The router losses look like the router going away**: both correct-code
  runs got an IP, lost beacons after 199 s and 1105 s, then 201 on every
  channel for 2 min, while the phone was heard at −40 dBm right after. Next
  time: check on the phone/PC at once whether the network is still visible
  and run "Scan for networks". If the router is a phone hotspot, that is the
  explanation.
- **Report fields showing 0 without meaning** ("0 dBm / channel 0",
  "Successful associations: 0") are F6 leftovers no longer written since
  0.16.2; Stage 4/7 removes or fills them and records the router RSSI just
  before a loss.
- **Engine task waited 7.5 s** because `/api/sd/info` (7.3 s: counts files
  and used space) holds `engineMx`; pre-existing (0.16.5 too), breaks the
  < 200 ms criterion; fix: release the lock while counting, as `removeTree`
  does.
- **Router → own Wi-Fi switch held the lock 1.5–2.5 s** (engine wait
  1.57 s): Stage 4's `radioApply()` must run without the lock.
- **"/" is sent from the server task** (10.9 s when the router link died,
  2.5 s on a slow link; everything else waits meanwhile): open item I9.
  Proposal for the owner's approval: send the page and the series JSON
  through the xfer task with a queue of 2–3 instead of 1.
- **The one 503 and the 2.6 s `/api/series/recent`** cannot be attributed:
  the counter merges "lock timed out" and "transfer busy" (loop never over
  71 ms, so almost certainly the latter). Report both separately.
- **Privacy:** the router name appears in the committed reports and README
  (CLAUDE.md asks for router names to be hidden); replace on the owner's
  word, noting it stays in the git history. *Done 2026-11-06: replaced by
  `<router name hidden>` in the current files; history not rewritten
  (squash merges keep it out of `main`).*

Review of this assessment (second implementer, 2026-11-06; the owner then
gave the go for the fixes listed after it):

- "Stable both ways ⇒ H3" shows that the failure does **not reproduce with
  0.16.6** (what 9.0 needs before Stage 4); it does not exclude a changed RF
  environment. Write the result that way unless 0.16.5 fails in the same
  session.
- Existing data already settles two things: the speed spread is **not** the
  supply (USB slow, lab slow, USB fast; one phone slow and fast at −47/−48
  dBm), and round trips in whole beacon intervals (≈102 ms) point at phone
  power save. Throughput is therefore no pass criterion for step 3.
- The bench router is a **dedicated 4G router** (owner), not a phone hotspot,
  so that explanation is out. Its losses (beacons lost, then 49 full scans
  without it) stay unexplained and belong to Stage 5 diagnostics, not to the
  gate: in router mode the own Wi-Fi is off, so they say nothing about the
  standalone downlink.
- Step 3 conditions: **no router saved** (else the boot joins the router with
  the own Wi-Fi off); no scan or setting change during the 20 min (scan before
  and after, note the router's channel and dBm); same mounting place, 12 V
  only, J1708 and NMEA 2000 connected as on 2026-11-05; pass = TCP
  connections from both devices, no code-2/209 leaves, 0 send failures, no
  "Reconnecting". Unstable both ways ⇒ first 10 min on a USB power bank at the
  same place (separates the 12 V path from the place without a scope), then
  step 4 only if that points at the supply. Optional when stable: 10 min of
  0.16.5 at the same place, last (if it fails, 0.16.6 can only be put back at
  home).

Power parts for step 4's limits are now known (`hardware/PCB_GERBER_REVIEW_2026-11-05.md`:
LM5164 buck at about 5.24 V, TLV76733 LDO, USB through a 0.5 A PTC); the
antenna keepout and cutout were found adequate there.

Router mode, reported 2026-11-08 (owner, lab supply, 0.16.6 bench build 2): joined at
−42 dBm with IP; `/api/live` answered repeatedly from a PC on the same
router, `/` timed out in the browser (20.5 s in the server, 5 send failures);
standalone loads the page at 1048 kB/s. Analysis in 12.1 I14 (the station's
20 MHz and beacon timeout lost in 0.16.2), restored in bench build 3;
the next router run shows whether that was the cause. Side
effect seen: while `/` waits for the router link, the single server task
serves nothing else (I9, open). This changes neither the gate nor its test:
the boat test of step 3 is standalone only. It also reopens the reading of
the 2026-11-06 router losses above: beacons lost after the driver's 6 s
default, not necessarily the router going away.

Router mode, bench build 3 (owner, 2026-11-08,
`docs/test-evidence/wifi-0166-2026-11-08/router-link-failure-build3.md`):
`/`, `/api/live` and `enginelink.local` time out; 1 of 10 pings answered; the
PC's own "Destination host unreachable" means its ARP request got no answer.
No report of the running build exists. Analysis:

- The router's client table keeps a DHCP lease after a station leaves, so it
  does not show that EngineLink was still on the router. Two readings are
  left: **(A)** EngineLink left the router (disconnects, then the 2 min
  fallback to its own Wi-Fi; the browser's `ERR_NETWORK_CHANGED` fits a PC
  that joined the known EngineLink network), or **(B)** it stayed associated
  but most frames were lost (a link problem). Build 2's pattern (small
  answers through, 110 KB not) fits (B) on a lighter day.
- The code has no router-mode path that takes the radio off its channel or
  into power save: no scan except the radio test after a speed test, power
  save off and read back, nothing sets a channel. A single server task
  amplifies (B): while `/` stalls on a lossy link nothing else is accepted, so
  `/api/live` times out as well (I9, open, owner's decision).
- (A) and (B) are told apart by the board's own `/WIFI.LOG` on the SD card,
  which already covers the build-3 run (written at every Wi-Fi event):
  disconnect lines and "Router not reached in 2 minutes" mean (A); none mean
  (B). Bench build 4 (I15) adds the router's channel and last signal to
  those lines and makes the report's router counters true, so the one
  router check in `docs/TODO.md` documents itself even when HTTP is
  unreachable (USB serial `wifi`, or the SD files).

Fixes made after this review, before the gate (Stages 1–2 scope, 9.0; no
Wi-Fi state machine, no network switch changed): I10 (SD work without the
engine lock) and I11 (503 causes, lock holder, engine round, build time).
Still open: the network switch holding the lock (Stage 4 lock rule, 9.4);
"/" sent from the server task (I9, owner's choice later); the report fields
that read 0 (Stage 4/7). Residual risk: the engine task writes the raw log
itself (`writeRecord()` → `logFile`), so a slow card write or a card access
running beside it can still stall one engine round; the report's new
"longest round of its own work" measures it. Moving those writes to their own
writer is part of the task/core review after Wi-Fi 2.0 (HANDOVER "Tasks").

Stage status after these reports: Stage 1 bench items seen (20 MHz / 600 s /
DK / 19.50 dBm, per-phone RSSI, 2 trace lines for a 2 min router loss);
missing: TX power and country in router mode. Stage 2 seen: self-test
passed by a client response, 200/421/403, heap ≥ 68 KB, stacks ≥ 3980 /
5856 bytes; missing: Origin, 413, 4 MB and truncated upload, 20 parallel
requests, airplane mode, 20 Safari reloads, export diff; the "no request
> 2 s" row failed (2.6 s). 0.16.6 is partially bench-tested, **not
bench-verified**.

### 13.7 If the gate ends in H3

The owner decides whether Stage 1 ships alone first as 0.16.6 (D18 "one
release" was about the rewrite, not about a restored setting). Stages 2–8 then
follow in 0.17.0.
