# Status and safety

EngineLink is alpha firmware on a revision 1 board. This page lists what has
been checked on real hardware, what has not, and the rules for connecting it
to engines.

## Safety rules

- Disconnect the engine networks and NMEA 2000 during first flashing and bring-up.
- Measure both isolated 5 V supplies against their own isolated grounds
  (4.75–5.25 V) before connecting an engine.
- EngineLink only listens at every boot. Reading the engines (KAD polling) is a
  deliberate step: close the connector's solder jumper (SJ1/SJ2) and switch it
  on under Settings → Diagnostics. "Read engines automatically" is off until
  you turn it on, with a confirmation.
- The only message EngineLink can send is the KAD read request
  `01 43 21 <index hi> <index lo> <checksum>`. There are no commands, no fault
  code clearing and no ECU programming, and there is no free-form transmit
  endpoint.
- Do not poll while a Volvo diagnostic tool (VODIA) is connected.
- Change the factory Wi-Fi password `12345678` in Settings. Anyone with the
  Wi-Fi password can change settings; there is no separate admin login.

## Checked on the board

| Area | Result |
|------|--------|
| J1708 receive | Bench loop: 36,000 frames in 30 minutes without errors |
| Both KAD300 engines | Read on the boat: rpm, fuel rate, temperatures, boost, battery voltage, gear learned |
| Fuel figures | First fill-ups within about ±8 % before calibration |
| microSD, status LED | Bench tested |
| Own Wi-Fi (standalone) | Phones connect at 192.168.4.1; about 650 kB/s |
| Router mode | Zyxel EX3600 (WPA2/WPA3 mixed): 500–630 kB/s, about 20 ms round trip on a free channel; fallback to own Wi-Fi and return tested |
| Firmware updates | Over Wi-Fi from GitHub and from a file, with rollback |

## Not yet checked

- KAD reply layout for every value: verified for the values above, not for all
  indexes ("Discover more values" should be re-run).
- Status bits in values 263/264 are not decoded yet.
- Fault codes: need a VODIA capture.
- Fuel factor confirmed tank to tank.
- NMEA 2000 interface: isolation, termination and traffic not validated. Do not
  enable it until verified.
- Behaviour after a sudden power cut during SD writes.
- No battery-backed clock: without phone, router or GPS time, logs use uptime.

See also [KAD300 / EDC I](KAD300_EDC1.md), [engine test procedure](ENGINE_TEST.md)
and [hardware](HARDWARE.md).
