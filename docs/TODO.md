# To do

Small fixes and wishes, collected here and released together in the next
version instead of one update each. Newest at the bottom of each section.

## App

- **Drive test, animation continuity — fixed in source, awaiting next firmware release.**
  [PR #131](https://github.com/Kaelor9/EngineLink/pull/131) keeps the scene mounted
  while 1 Hz `/api/fast` samples arrive, updates numbers in place, and hides
  the second mooring scene under reduced motion. Confirm smooth playback on
  iPhone and dock safety-step behavior once a new release is installed.
- **Drive test scenes, new look.** The owner has chosen the boat and its
  look: see `tools/boat-studio/`. `line-style.html` is the one to use in the
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

- Run the drive test at the dock (0.14.0) and check that bit 0x100 in 264
  means neutral and 0x20 the compressor.
