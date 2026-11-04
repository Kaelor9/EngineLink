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

- **Drive test steps as text — in source, awaiting the next release.** Each step
  shows a PORT LEVER and a STARBOARD LEVER card with the position in words
  (NEUTRAL / FORWARD / REVERSE, idle or rpm) and marks the lever to move.
  Safety:
  - a red warning when an engine is over 1,000 rpm at the dock, or when the
    other engine is above idle in a one-engine step;
  - Start works only when both engines are under 800 rpm.

  The elaborate options page for the rest of the drive test
  (`tools/boat-studio/drive-test-flow.html`) is kept but not used, to save work.

## Firmware

## To check on the boat

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
