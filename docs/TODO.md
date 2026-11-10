# To do

**NOW — Wi-Fi (2026-11-10, after rc3):** rc3 on the home router: 802.11n 40 MHz negotiated, 3 of 152 pings answered at −47 dBm, beacons lost after 302 s ([evidence](test-evidence/wifi-0170rc3-2026-11-10/README.md)). **0.17.0-rc4** ([plan section 9](WIFI_PLAN_2026-11-09.md)) fixes three rc3 faults (own Wi-Fi at once on a loss, engine lock held 1645 ms in a mode change, wrong end code), sets the station to 20 MHz, tries 802.11g after a poor session, leaves a dead link after a minute and records sessions, requests and radio samples. *Built* (local compile, host tests), not run on the board. Next (owner): install rc4, router mode on the home router for 15 minutes, open the page from the PC a few times, send the Wi-Fi report; read it with plan 9.6. Optional without firmware: plan 8.6 (phone hotspot as router; own Wi-Fi on channel 1). No merge or release before the bench and the boat (5.5) pass.

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

## Wi-Fi (plan revised 2026-11-09, sections 8–9 added 2026-11-10: 0.17.0-rc4)

The authoritative plan is [docs/WIFI_PLAN_2026-11-09.md](WIFI_PLAN_2026-11-09.md);
`docs/WIFI_REWRITE_PLAN.md` is history. Two modes (Standalone, Router) plus
the recovery mechanism; the station runs on the driver's defaults as the
Prism controller (rc2; rc1's profile of power save off, country DK, 20 MHz
and all-channel scan failed the bench); the Wi-Fi 2.0 state machine (Stage 4)
and the 0.15.0 leftovers are removed. Open items from the old section that are **not**
dropped: Apple's captive-portal probe (deferred by the owner), "Router is
back, switch now?" (withdrawn with Stage 4: the app's Connect does it).

## Deferred architecture work — after the Wi-Fi plan's acceptance

- **High priority, BLOCKED until Wi-Fi 2.0 is accepted — decouple engine telemetry from Wi-Fi/web/SD/USB Serial** ([issue #161](https://github.com/Kaelor9/EngineLink/issues/161), [code audit](FIRMWARE_BLOCKING_AUDIT_2026-11-05.md)). The current two-core design uses a broad shared `engineMx` mutex; slow SD, Serial or HTTP operations can delay engine handling. After the Wi-Fi plan's bench + boat acceptance (0.17.0), evaluate a dedicated real-time motor task, bounded FreeRTOS queues, read-only snapshots and separate SD/Serial workers. Choose core pinning based on measurement, not simply swapping tasks. Preserve fuel/trip correctness and record lost frames/queue depth. **Do not change the Wi-Fi plan or its firmware for this item now.** Implement later in a separate branch/PR, with regression tests and owner approval. Starting point: the task and core table, measured couplings and proposed order in HANDOVER "Tasks and cores" (2026-11-06). The targeted fixes of 2026-11-06 (plan 12.1 I10–I11: SD card work, Wi-Fi log and serial export without the engine lock; lock holder and engine round in the report) removed the measured stalls the owner asked to fix now, without changing lock ownership or tasks.

## To check on the boat

- **Wi-Fi 0.17.0-rc4 first: the 15-minute router session of plan 9.6** (no
  PC scripts needed; the report's "ROUTER SESSIONS AS ENGINELINK MEASURED
  THEM" block and the request log are the result). The longer session below (plan section 5) follows
  only if 8.5 passes.
- **Wi-Fi, one bench session then the boat (plan section 5).**
  Bench (home 4G router, default settings, about 40 min): PC `netsh wlan show
  interfaces` first (which band the PC uses), `ping -t` and the `netsh wlan
  show networks mode=bssid` loop logging while EngineLink is in router mode,
  20 × `/api/live`, 10 × `/`, 3 × `enginelink.local`, 30 min, report and
  `/WIFI.LOG`; the Prism controller on the same router and spot as control.
  Pass: ≥ 99/100 pings, 20/20 `/api/live` < 2 s, 10/10 `/` < 5 s, 0 reason
  200, 0 fallbacks, report says power save off, country DK, 20 MHz. Read a
  failure with the table in plan 5.4 (router / place / board); no new
  firmware build for a diagnosis. Then the boat (5.5): one trip, the same
  checks from the iPhone, one router power-cycle, standalone once.
- **Hardware documentation needed for the power tests:** LDO and buck part
  numbers, how USB 5 V and the buck output are combined, decoupling at the
  module (`docs/HARDWARE.md` has none of it).
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
