# To do

Small fixes and wishes, collected here and released together in the next
version instead of one update each. Newest at the bottom of each section.

## App

- **Drive test, animation continuity — fixed in source, awaiting next firmware release.**
  [PR #131](https://github.com/Kaelor9/EngineLink/pull/131) keeps the scene mounted
  while 1 Hz `/api/fast` samples arrive, updates numbers in place, and hides
  the second mooring scene under reduced motion. Confirm smooth playback on
  iPhone and dock safety-step behavior once a new release is installed.
- **Drive test scenes, new look.** The owner is choosing boat, water, fenders,
  lines, quay points and piles on the options page (Køretest-scener
  artifact). Build the chosen combination into `dtAnim` when the choice is in.

## Firmware

## To check on the boat

- Run the drive test at the dock (0.14.0) and check that bit 0x100 in 264
  means neutral and 0x20 the compressor.
