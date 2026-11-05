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

## Deferred architecture work — after Wi-Fi 2.0

- **High priority, BLOCKED until Wi-Fi 2.0 is accepted — decouple engine telemetry from Wi-Fi/web/SD/USB Serial** ([issue #161](https://github.com/Kaelor9/EngineLink/issues/161), [code audit](FIRMWARE_BLOCKING_AUDIT_2026-11-05.md)). The current two-core design uses a broad shared `engineMx` mutex; slow SD, Serial or HTTP operations can delay engine handling. After Fable's Wi-Fi 2.0 implementation and its bench + boat acceptance tests (planned 0.17.0), evaluate a dedicated real-time motor task, bounded FreeRTOS queues, read-only snapshots and separate SD/Serial workers. Choose core pinning based on measurement, not simply swapping tasks. Preserve fuel/trip correctness and record lost frames/queue depth. **Do not change the Wi-Fi 2.0 plan, PR #160 or firmware for this item now.** Implement later in a separate branch/PR, with regression tests and owner approval.

## To check on the boat

- **Standalone Wi-Fi 0.16.5 — verify on the EngineLink PCB before calling this fixed.**
  Evidence from the 0.16.4 USB report at 50 s: one phone joined at +23 s,
  left at +40 s with reason 8 (station left), zero port-80 HTTP requests;
  AP mode on channel 6, no watchdog restart. The driver actually reported
  8.5 dBm despite the firmware claiming its fixed 19.5 dBm setting.
  0.16.5 removes that AP-only override and records AP DHCP leases, port-80
  TCP accepts and HTTP parse failures separately. With iPhone joined to
  `EngineLink`, turn off mobile data for the controlled test and open
  `http://192.168.4.1/api/health` directly in Safari, then the app at
  `http://192.168.4.1/`. In the USB console check for
  `HTTP/80 listening on 192.168.4.1`, and type `wifi` / `wifi log` to get the
  report. Repeat once at normal distance and send the new
  USB Wi-Fi report *before and after*. If it still fails, use the three
  counters to locate whether failure is association/DHCP, TCP, or HTTP.
  Do not attribute reason 8 to a radio fault without further evidence.

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
