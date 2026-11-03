# To do

Small fixes and wishes, collected here and released together in the next
version instead of one update each. Newest at the bottom of each section.

## App

- **Drive test, animation continuity — fixed in source, awaiting next firmware release.**
  [PR #131](https://github.com/Kaelor9/EngineLink/pull/131) keeps the scene mounted
  while 1 Hz `/api/fast` samples arrive, updates numbers in place, and hides
  the second mooring scene under reduced motion. Confirm smooth playback on
  iPhone and dock safety-step behavior once a new release is installed.
- **Drive test scenes, new look.** The owner has chosen the boat: see
  `tools/boat-studio/` (the page, the choice code and a picture). Keep its
  shape exactly; for the scenes it is being drawn in the app's line style
  (flat colours, a few strong lines, bow to the right; colour set still to be
  chosen on the "Båden i stregstil" page). The swim ladder must match:
  stowed under a hatch on the starboard side of the platform, folded out with
  the lid shut, also in the animations (for example when someone climbs back
  aboard). Water, quay, piles and lines still to be chosen in the same style.

## Firmware

## To check on the boat

- Run the drive test at the dock (0.14.0) and check that bit 0x100 in 264
  means neutral and 0x20 the compressor.
