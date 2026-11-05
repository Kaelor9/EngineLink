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
  Wi-Fi password (or on the boat router's network) can change settings; there
  is no separate admin login. Up to 0.16.5 every response also carries
  `Access-Control-Allow-Origin: *`, so a web page open in your browser could
  read EngineLink data or send it commands; the 0.16.6 development build
  closes that (plan section 6.9) without adding a login (built, not yet
  tested on the board).

## Checked on the board

| Area | Result |
|------|--------|
| J1708 receive | Bench loop: 36,000 frames in 30 minutes without errors |
| Both KAD300 engines | Read on the boat: rpm, fuel rate, temperatures, boost, battery voltage, gear learned |
| Fuel figures | First fill-ups within about ±8 % before calibration |
| microSD, status LED | Bench tested |
| Own Wi-Fi (standalone) | **Not usable on the boat with 0.16.5 (2026-11-05).** Phones join and get a DHCP lease in every boot, but the link EngineLink → phone fails: in four boots no TCP connection from the phone reached the web server at all, the AP logged the phone leaving with code 2 eight to ten times in a row followed by code 209 (SA Query unanswered), the phone re-asked DHCP, and the two speed tests that completed gave 60–72 kB/s at 215–307 ms. Seen on boat 12 V, on USB only, and worst with both (a power-on reset occurred when 12 V was connected while on USB). The main loop blocked 5.6–10 s in the web part each time (the core's write retry, a consequence, not the cause). Earlier: about 650 kB/s on 0.13.4 (2 Nov), 265–346 kB/s at 91–99 ms on 0.15.0 (4 Nov, USB). Cause open: RF environment (router channel), power path, firmware AP settings dropped in 0.16.2, or board. The test procedure that separates them is in [WIFI_REWRITE_PLAN.md](WIFI_REWRITE_PLAN.md) section 13.6. |
| Router mode | Zyxel EX3600 (WPA2/WPA3 mixed): previously 500–630 kB/s on free channel. Field report [#126](https://github.com/Kaelor9/EngineLink/issues/126), v0.13.4: 12 test average 584.6 kB/s (457.8–665.3), round trip 18–20 ms. Still 95 Wi-Fi disconnect events / 48 pre-connection failures and about 50 seconds reconnecting, so long-term stability is NOT verified. 0.16.2 replaced the Wi-Fi with a simple version: router mode not yet tested on the board. |
| Firmware updates | Over Wi-Fi from GitHub and from a file, with rollback |

## Not yet checked

- Why the own Wi-Fi's downlink to the phone fails on the boat with 0.16.5 (2026-11-05): not yet separated between RF environment, power supply, the AP radio settings 0.16.2 dropped (20 MHz bandwidth, country, inactive time) and the board itself. Procedure: [WIFI_REWRITE_PLAN.md](WIFI_REWRITE_PLAN.md) 13.6. Until it is done, Wi-Fi 2.0 Stage 4 onwards is not built; Stages 1–2 may be built and bench-tested meanwhile and Stage 3 is host code (plan 9.0). The 5–10 s web stall is explained (Arduino core `NetworkClient::write()` retrying 10 × 1 s against a phone that does not acknowledge); the plan removes it by moving HTTP to `esp_http_server` (its own task) instead of patching the write. Wi-Fi 2.0 as a whole (plan section 7 acceptance criteria, section 9.9 bench and boat protocol) is untested. From here each stage is reported as *built* (CI green, untested on hardware), *bench-verified* (owner ran the stage's bench list) or *boat-verified* (plan 9.9 passed); this table says "verified on the board" only for the last.
- **0.16.6 development build (Wi-Fi 2.0 Stages 1–3): *built*, untested on hardware.** CI-equivalent checks pass locally (pinned core 3.3.12 compile, host tests `test_tracefold`, `test_httputil`, `test_wifistate` with plan 9.3's 20 scenarios, `test_wifi_contract.py`). Nothing of it has run on the board: the access point settings (20 MHz, 600 s, country DK), the `esp_http_server` web server, the security checks, the raw-body firmware upload, the boot self-test and rollback all wait for the owner's bench list (`docs/releases/v0.16.6.md`, plan 9.1/9.2). Not merged, not released (D24).
- Router mode with the simple Wi-Fi (since 0.16.2) is untested; it could not be tested on 2026-11-05 because the standalone link was already unreliable. The 3.3 V rail under Wi-Fi transmit bursts and the USB/12 V supply interaction have never been measured with an oscilloscope.
- Long-term router stability across automatic router channel changes; #126 shows recovery after an outage but not why it occurred.
- Whether the small Trip summary CSV fix in PR #123 resolves Chrome/iPhone download failures.
- Whether retained crash history reflects any new crashes during extended v0.13.4 operation.

- KAD reply layout for every value: verified for the values above, not for all
  indexes ("Discover more values" should be re-run).
- Status bits in values 263/264 are not decoded yet. The gear and compressor bits are learned per boat by the drive test (0.14.0); their meaning is a hypothesis (see KAD300 / EDC I).
- Gear and clutch timing (0.14.0): checked against two recorded trips (replayed on a PC), not yet on the boat with the faster gear readings.
- Fault codes: no read command found among the PC tool's commands; "Discover more values" now covers 256–447.
- Fuel factor confirmed tank to tank.
- NMEA 2000 interface: isolation, termination and traffic not validated. Do not
  enable it until verified. 0.16 decodes navigation, depth, log, heading,
  temperatures, battery, rudder, wind and tanks, and lists every device on
  Diagnostics › GPS · NMEA 2000 › Devices and data. "Record the bus" saves every raw frame
  (10/30 min, /NMEA/BUS.CSV) to map everything the boat sends. Owner's boat:
  SmartPilot X10 and AIS700 on SeaTalkNG; ST60 is SeaTalk1, no converter seen.
  Not yet tested on the boat.
- Behaviour after a sudden power cut during SD writes.
- No battery-backed clock: without phone, router or GPS time, logs use uptime.

See also [KAD300 / EDC I](KAD300_EDC1.md), [engine test procedure](ENGINE_TEST.md)
and [hardware](HARDWARE.md).
