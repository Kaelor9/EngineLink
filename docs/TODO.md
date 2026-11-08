# To do

Small fixes and wishes, collected here and released together in the next
version instead of one update each. Newest at the bottom of each section.

## App

- **Drive test, animation continuity — released in 0.15.0.**
  [PR #131](https://github.com/Kaelor9/EngineLink/pull/131) keeps the scene mounted
  while 1 Hz `/api/fast` samples arrive, updates numbers in place, and hides
  the second mooring scene under reduced motion. Confirm smooth playback on
  iPhone and dock safety-step behavior once a new release is installed.
- **Drive test scenes, new look — built into the app, awaiting the next
  firmware release.** The owner chose on 2026-11-04 on the options page
  `tools/boat-studio/drive-test-scenes.html`:
  `Farver 1 · Båden 3 (fart 3) · Fortøjninger 1 (fart 4) · Fendere 13 (fart 5) ·
  Rækkefølge 2 · Ottetalsstik 2, vises 1 (fart 3) · Bølger 4 ·
  Bølgernes bevægelse 2 (fart 3) · Mellem pæle 1 · Langs kajen 1 · Åbent vand 1`.
  That is:
  - his boat in the app's dark blue, rocking gently;
  - lines drawn from the cleats to the quay, fast, and fixed to the cleats;
  - a figure-of-eight without locking turn fading in on each cleat;
  - then the fenders popping out one by one, very fast;
  - small ripples fading in and gliding out.

  Only the "Moored properly" and "Open water" scenes use it; the swim, stop,
  warm-up, lookout and lever scenes keep their old look for now. Check on an
  iPhone after the release.

- **Drive test flow, owner's choice — in source, awaiting the next release.**
  Chosen on `tools/boat-studio/drive-test-flow.html` (2026-11-04):
  `Ingen i vandet 1 · Stop 14 · Tomgang 5 · Udkig 1 · Bekræft 8 · Start 1 ·
  Trinbillede 18 · Omdrejningstællere 1/1 · Opbygning 2 · Farve 1 · Nedtælling 8 ·
  Fremskridt 7 · Værdier 8 · Stopknap 6 · Vagter 1-10,12 · Advarsel 1 ·
  Start-skærm 1 · Resultat 1 · Tekst 1`.
  - Pictures come from the page via `node tools/boat-studio/export_flow.js`.
  - Safety cards are swiped away (with a "Done, next" button too). The checks
    are remembered for 15 minutes.
  - Each step shows a big instruction and the Volvo Penta twin-lever picture
    (front and side), gear and rpm per engine, a bar with seconds, and Pause
    and Stop at the top.
  - Guards: over 1,000 rpm at the dock; the other engine above idle; Start
    only at idle; a guard stops the step; boat moving over 2 kn at the dock
    (needs GPS on NMEA 2000); lost connection; engine data missing 3 s; a
    "Put both levers in NEUTRAL" screen when stopped; a lookout reminder
    every 30 s at sea.

  Check on the board and an iPhone after the release.

## Firmware

- **NMEA 2000, listen only — released in 0.16.0, not yet tested on the boat.**
  - "Record the bus" (0.16.0) saves every raw frame for 10 or 30 min to
    `/NMEA/BUS.CSV` (download `/api/nmea/bus.csv`) to map everything the boat
    sends. Next step after a recording: analyse it, then decide on AIS
    target decoding (owner's AIS700 and SmartPilot X10 are on SeaTalkNG).
  - EngineLink now decodes:
    - position, course and speed over ground, GNSS fix, satellites and HDOP;
    - heading, speed through water, depth, distance log;
    - sea and air temperature, rudder, battery, wind, tank levels;
    - other instruments' engine rpm and the time.
  - Logged:
    - every second of a trip to `/TRIPS/Tnnnnnn.NAV`, beside the engine log
      so old trips keep their format;
    - added as columns to the second-by-second export.
  - In the app: a "Navigation" card on the Trip screen, and Settings ›
    Diagnostics › GPS · NMEA 2000 › "Devices and data". That page lists every
    device (maker from its address claim) and every message with its rate,
    plus bus errors.
  - The boat's survey lists Raymarine SeaTalk1 instruments (ST60 Tridata,
    ST6001 autopilot, a Raymarine plotter, Raymarine VHF with DSC). They reach
    NMEA 2000 only through a SeaTalk1-to-SeaTalkNG converter.
  - To check on the boat: the bus page shows the Raymarine devices; depth,
    log and heading match the ST60; the trip export has the navigation
    columns.

## Wi-Fi 2.0 (plan agreed 2026-11-04; Stages 1–3 *built* in the 0.16.6 development build, untested on hardware)

A clean rewrite of the Wi-Fi part, built in one release after a short router
test on 0.16.5. Not written from zero: the lessons below become tests.
**The one authoritative implementation plan is
[docs/WIFI_REWRITE_PLAN.md](WIFI_REWRITE_PLAN.md)** (fifth and final
revision 2026-11-05; implementation findings in its section 12.1). Where this section and the plan differ, the plan
wins: it turns the wishes below into requirements D1–D22 and chooses the
technical realisation (Arduino core 3.3.12 + official ESP-IDF components:
`esp_http_server` for the web server, `esp_wifi`/`WiFi` with a small
host-tested state machine, FreeRTOS queue for events; owner's component
policy D23; hardware validation before merge D24; pinned toolchain D25).
**Planning closed 2026-11-05.** The owner approved P8, P9, P11 and P12 with
their documented limits, deferred P10 and P13, approved removing the deflate
compression of downloads (exports keep routes, content and formats) and
asked for a documented country configuration (`DK`, not `"EU"`). No further
architecture revisions; the work now is building and testing stage by stage
(plan section 9), each reported as built / bench-verified / boat-verified.

**Progress (2026-11-05):** Stage 1 (AP radio settings, TX power truth, trace
fold, link diagnostics, pinned core), Stage 2 (`esp_http_server`, security
model, raw-body upload, boot self-test, no deflate) and Stage 3 (state
machine header + host test, not used by the firmware yet) are *built* in
0.16.6 on branch `claude/wizardly-lovelace-v6tlqz`. Next: the owner's bench
list for 0.16.6 (`docs/releases/v0.16.6.md`) and the Stage 0 gate; Stage 4
(wiring the state machine into `wifi.ino`) starts only after both.

**Why:** the Wi-Fi logic in `wifi.ino` is only ~100 lines; the clutter is the
0.12–0.15 leftovers around it (`netpolicy.h`, `linktune.cpp`, channel survey,
and a 409-line `netreport.ino` full of fields that are always 0). Rewriting
blindly in 0.16.2 lost the web-server rebind and the TX power, which cost
0.16.3 and 0.16.4 on the boat.

**Four states, never two networks** (except the 15 s router test):
1. *Standalone*: own Wi-Fi `EngineLink` / `12345678`, 192.168.4.1, fixed
   channel 6. Must stay the simplest, most robust part: it is the fallback.
   Time from the phone; files in and out as today; no NTP, GitHub update or
   reports. With a router saved, it listens briefly (~0.1 s, the router's
   remembered channel) for the router about every minute, forever.
2. *Router test* (setup only): the owner picks a network from the scan
   (bars, −dBm, open/locked, security) and types the code. The own Wi-Fi
   stays on while EngineLink tries the router for ~15 s, then shows
   "Wrong code" or "OK, address …"; only then the own Wi-Fi goes off. The
   single radio moves the own Wi-Fi to the router's channel during the test,
   so the phone may drop for seconds: the app waits and the result is kept
   until it is back.
3. *Router*: station only, Arduino's own reconnect, fast and stable.
4. *Router lost*: keep retrying for 2 min, then standalone (state 1).

**Rules:**
- Start with a router saved: try the router for up to 3 min (router and
  EngineLink usually get boat power together), else standalone.
- Back to the router: automatically when no phone is on the own Wi-Fi; if
  one is, the app asks "Router is back, switch now?".
- No settings for retry minutes or number of tries (replaces the owner's
  first idea of "every 10 min, 3 times": that could lock him out for 30 min).
- `enginelink.local` everywhere, so one iPhone home-screen app works in both
  modes (the PWA is bound to the address it was saved from).
- Power off/on 3 times within 10 s: start in standalone whatever is saved.
- WPA2 unless the scan shows the router is WPA3-only (decided at connect, no
  stored security mode or switch counter). Keep `wifiwpa3.cpp`: 0.11.x WPA3
  logins failed repeatedly on the Zyxel mixed router, WPA2 since 0.12.0 works.
- Channel: follow the router as a station, never ask for router changes;
  own Wi-Fi fixed on 6; remember the router's channel for the quick look.
- Upgrade keeps the saved router, code and all data.

**Remove:** `netpolicy.h`, `linktune.cpp` (radio test, channel survey),
quiet-channel hints, channel and security counters, the always-0 report
fields; move clock/NTP, mDNS and GitHub reports out of `wifi.ino`. Report
becomes short and readable.
**Keep:** `wifiwpa3.cpp`, `netboost.cpp` (TCP send window, gave the measured
router speed), the USB serial commands and SD Wi-Fi log.

**Tests (lessons that must not get lost):** web server listens on the network
that is up now (0.16.3); TX power set after the country and checked
(0.16.4); power save off; USB printing never blocks (`setTxTimeoutMs(0)`,
0.15 stalls); Wi-Fi calls only from the main loop, never from the event
callback; `wifi.ino` stays under ~300 lines.

**Open:**
- Decided 2026-11-05 (details in `docs/WIFI_REWRITE_PLAN.md`, section 7):
  the small app changes for the router test result and the "Router is back,
  switch now?" prompt are approved; standalone may run the radio as AP+idle
  station; a phone joined to the own Wi-Fi is **never** cut off automatically
  (no idle timer) — only "Switch now" or the phone leaving lets EngineLink
  return to the router; answering Apple's captive-portal probe is deferred.
- 10 s main-loop stall in the web part (0.16.5 report): cause found (the
  core's `NetworkClient::write()` retrying 10 × 1 s, plan F1); 0.16.6 moves
  HTTP to `esp_http_server` in its own task (built, bench test pending).
- Only if EngineLink is ever sold: unique SSID (`EngineLink-XXXX`) and code
  per board on a label with a Wi-Fi QR code; a shared default password is
  not allowed for new radio products in the EU since 2025-08-01 (RED / EN 18031).

**Boat test after the release:** standalone; pick router; wrong code; router
off and on; boot with router; power off/on 3 times; speed in both modes.

## Deferred architecture work — after Wi-Fi 2.0

- **High priority, BLOCKED until Wi-Fi 2.0 is accepted — decouple engine telemetry from Wi-Fi/web/SD/USB Serial** ([issue #161](https://github.com/Kaelor9/EngineLink/issues/161), [code audit](FIRMWARE_BLOCKING_AUDIT_2026-11-05.md)). The current two-core design uses a broad shared `engineMx` mutex; slow SD, Serial or HTTP operations can delay engine handling. After Fable's Wi-Fi 2.0 implementation and its bench + boat acceptance tests (planned 0.17.0), evaluate a dedicated real-time motor task, bounded FreeRTOS queues, read-only snapshots and separate SD/Serial workers. Choose core pinning based on measurement, not simply swapping tasks. Preserve fuel/trip correctness and record lost frames/queue depth. **Do not change the Wi-Fi 2.0 plan, PR #160 or firmware for this item now.** Implement later in a separate branch/PR, with regression tests and owner approval. Starting point: the task and core table, measured couplings and proposed order in HANDOVER "Tasks and cores" (2026-11-06). The targeted fixes of 2026-11-06 (plan 12.1 I10–I11: SD card work, Wi-Fi log and serial export without the engine lock; lock holder and engine round in the report) removed the measured stalls the owner asked to fix now, without changing lock ownership or tasks.

## To check on the boat

- **Own Wi-Fi root cause first (2026-11-05, gates Wi-Fi 2.0 Stage 4 onwards;
  Stages 1–2 are built and bench-tested meanwhile, plan 9.0).**
  The 5 Nov boat test of 0.16.5 showed the phone joining and getting DHCP
  but EngineLink's frames not reaching the phone (0 TCP connections, code 2
  ×8–10 then 209, DHCP asked twice, 60–72 kB/s). Follow
  `docs/WIFI_REWRITE_PLAN.md` section 13.6 in order:
  0. On the boat, 0.16.5: "Scan for networks" → note the boat router's
     channel and dBm (read only); `/api/diag` → `wifi.bw`, `wifi.ch`.
  1. At home, USB from a charger, 0.16.5, iPhone + PC, 15 min.
  2. At home, 0.13.4 from the web installer's version list, same test.
  3. On the boat, 12 V, the stable firmware from 1–2; 15 min with the
     router on, then 5 min with the router powered off (no settings change).
  4. Oscilloscope on 3V3 and the 5 V node: USB only, 12 V only, 12 V
     connected while on USB (a power-on reset happened then on 5 Nov).
  Send the `wifi` report after each step. The decision table in 13.6 says
  what each outcome means. Step 2 uses 0.13.4 as a comparison point only; it
  is not a stable reference (its router mode had 95 disconnects in #126).
- **Hardware documentation needed for the power tests:** LDO and buck part
  numbers, how USB 5 V and the buck output are combined, decoupling at the
  module (`docs/HARDWARE.md` has none of it).
- **Router mode on 0.16.5 (~15 min):** only after the standalone link is
  reliable again: set up the router, check it joins, "Test speed", restart the
  router once; send the USB `wifi` report. Baseline for the rewrite.

- **Bench test of 0.16.6 (Wi-Fi 2.0 Stages 1–2):** the list in
  `docs/releases/v0.16.6.md` (self-test passes, AP 20 MHz / 600 s / DK and
  19.50 dBm in the report, 30 min iPhone + PC, the `curl` security list,
  export `diff` against 0.16.5, upload a file once). Do the "before" export
  downloads on 0.16.5 first.

- **After the 2026-11-06 bench reports (plan 13.6.1):** the Stage 0 gate is
  still unclassified. Next, in this order:
  1. Boat, 12 V only, **0.16.6 as installed now**, iPhone + PC on the own
     Wi-Fi: 15 min router on, then 5 min router powered off (plan 13.6 step
     3). This decides the gate. Conditions (13.6.1): no router saved on
     EngineLink; scan for networks before and after only (note the router's
     channel and dBm); same mounting place; J1708 and NMEA 2000 connected;
     send the Wi-Fi report. Unstable both ways: 10 min on a USB power bank at
     the same place before any scope work.
  2. During it, "Test speed" ×5 alternating iPhone and PC (phone power save vs
     channel; not a pass criterion).
  3. ~~Say which device the bench router is~~: a dedicated 4G router (owner);
     its losses go to Stage 5 diagnostics.
  Done 2026-11-06 (owner's go): SD card work without the engine lock
  (`/api/sd/info`, clean-up, Wi-Fi log, wipe; plan I10), 503 causes and the
  engine lock holder in the report (I11), router name hidden in the test
  evidence. *Built*; bench check after the boat test: install the new
  `enginelink-update.bin` from a file, open Settings (SD card line shows
  "Reading the card…" then the size), send the Wi-Fi report: engine task
  longest wait and the lock holder it names; then the remaining Stage 2 rows
  of `docs/releases/v0.16.6.md`.
- **Router check of bench build 3 (plan 12.1 I14), when convenient:** install
  it from a file, put EngineLink on the bench router again, open
  `http://<its router IP>/` on the PC. Send the Wi-Fi report: it must say
  "EngineLink bandwidth: 20 MHz", and the slowest request and send failures
  show whether `/` now loads. If `/` still fails, tap "Test speed" once on the
  router and send that report too (speed through the transfer task and the
  1400-byte ping test tell the link from the server path).

## Previous boat checks

- **Wi-Fi 0.15.0 test plan** (send a Wi-Fi report after each step; the report
  now has reconnect attempts, rescue looks, real transmit power and the
  longest loop parts):
  1. 10 starts with the router already on: router IP in under 5 s (median).
  2. Router and EngineLink switched on together (boat power), 5 times: ends on
     the router without touching anything.
  3. Router restarted while in use: no rescue for outages under 2 minutes;
     back within about 15 s of the router.
  4. Router off for 5 minutes: EngineLink Wi-Fi after 2 minutes; with the app
     closed it is back on the router within about a minute of the router.
  5. Standalone for 2 hours with an iPhone and an Android phone (mobile data
     on): no stalls over 2 s; check that Android keeps using EngineLink Wi-Fi
     without internet.
  6. "Test speed" 5 times in each mode: router at least 400 kB/s, standalone
     at least 500 kB/s.
  7. With engines running: switch modes and restart the router; the engine
     report's longest wait (engineWait max) stays under 200 ms.
  8. 24 hours on the router: count of drops and reconnect times in the report.
  9. With nothing on the USB port: "Test speed" on EngineLink Wi-Fi (0.15.0
     gave about 300 kB/s at a 95 ms round trip, against 18 ms on the router).
     Check that the report's "Longest part of the main loop" stays under
     about 500 ms.

- Run the drive test at the dock (0.14.0) and check that bit 0x100 in 264
  means neutral and 0x20 the compressor.
