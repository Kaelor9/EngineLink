# To do

Small fixes and wishes, collected here and released together in the next
version instead of one update each. Newest at the bottom of each section.

## App

- **Drive test, animation continuity — released in 0.15.0.**
  [PR #131](https://github.com/Kaelor9/EngineLink/pull/131) keeps the scene mounted
  while 1 Hz `/api/fast` samples arrive, updates numbers in place, and hides
  the second mooring scene under reduced motion. Confirm smooth playback on
  iPhone and dock safety-step behavior once a new release is installed.
- **Drive test scenes, new look.** The owner has chosen the boat and its
  look: see `tools/boat-studio/`. `line-style.html` is the one to use in the
  `tools/boat-studio/drive-test-scenes.html` is the owner's exported, interactive
  scene preview (three groups of six choices); it is a **draft to refine**, not
  an approved design. Future visual iterations must start from this preview.
  scenes (`lineSvg()`, colour set "Som appen", bow to the right); `studio.html`
  is the full drawing with the same shape (keep the shape exactly). Fenders:
  three a side (aft, amidships, forward), round, a little off the hull. Swim
  ladder: stowed under the hatch on the starboard side of the platform (up)
  in every scene, except the one where the boat is seen from the side in the
  water with a person swimming: there it is folded out, down into the water,
  with the lid shut. Water, quay, piles and lines still to be chosen in the
  same style.

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
