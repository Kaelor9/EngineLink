# To do

Small fixes and wishes, collected here and released together in the next
version instead of one update each. Newest at the bottom of each section.

## App

- **Drive test, "At the dock" with animations turned off.** When the phone
  or PC limits animations (reduced motion), both mooring scenes show at once
  on top of each other, with the captions overlapping. Show only the first
  scene then (hide the alongside group under `prefers-reduced-motion`).
- **Drive test scenes, new look.** The owner is choosing boat, water, fenders,
  lines, quay points and piles on the options page (Køretest-scener
  artifact). Build the chosen combination into `dtAnim` when the choice is in.

## Firmware

## To check on the boat

- Run the drive test at the dock (0.14.0) and check that bit 0x100 in 264
  means neutral and 0x20 the compressor.
