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

## Wi-Fi 2.0 (plan, agreed with the owner 2026-11-04 — not built yet)

A clean rewrite of the Wi-Fi part, built in one release after a short router
test on 0.16.5. Not written from zero: the lessons below become tests.
**The one authoritative implementation plan is
[docs/WIFI_REWRITE_PLAN.md](WIFI_REWRITE_PLAN.md)** (third revision
2026-11-05, plan only). Where this section and the plan differ, the plan
wins: it turns the wishes below into requirements D1–D22 and chooses the
technical realisation (Arduino core 3.3.12 + official ESP-IDF components:
`esp_http_server` for the web server, `esp_wifi`/`WiFi` with a small
host-tested state machine, FreeRTOS queue for events; owner's component
policy D23; hardware validation before merge D24; pinned toolchain D25).
Open approvals for the owner: P8–P13 in the plan's section 11 (keep
`netboost.cpp` fenced; the three small app changes; option B later; the
boot self-test instead of the loopback self-check; the security model of
plan 6.9; an optional `sha256` per release entry).

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
- 10 s main-loop stall in the web part (0.16.5 report): find the cause.
- Only if EngineLink is ever sold: unique SSID (`EngineLink-XXXX`) and code
  per board on a label with a Wi-Fi QR code; a shared default password is
  not allowed for new radio products in the EU since 2025-08-01 (RED / EN 18031).

**Boat test after the release:** standalone; pick router; wrong code; router
off and on; boot with router; power off/on 3 times; speed in both modes.

## To check on the boat

- **Own Wi-Fi root cause first (2026-11-05, blocks Wi-Fi 2.0 stages 2–8).**
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
