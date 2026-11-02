# To do

Small fixes and wishes, collected here and released together in the next
version instead of one update each. Newest at the bottom of each section.

## App

- **"Discover more values" while the engines are off.** The search only runs
  while EngineLink reads the engines. With the ignition off it shows "Looking
  for more values · at 265 of 256–447" next to "Not reading", which looks
  stuck. Say that it is paused and continues by itself when the ignition is
  on. The explanation below still says "256 to 319"; it is 256–447 since
  0.14.0.
- **Trip data four times a second has no button.** It can only be fetched at
  `/api/export/fast.csv?ids=<trip>`. Add it to the export next to the
  second-by-second CSV.

## Firmware

## To check on the boat

- Run the drive test at the dock (0.14.0) and check that bit 0x100 in 264
  means neutral and 0x20 the compressor.
