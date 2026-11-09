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

**2026-11-09 — state of the Wi-Fi work:** the last owner test (CI [run #261](https://github.com/Kaelor9/EngineLink/actions/runs/42631425052), `75faba4`, 0.16.6 built Nov 9 14:08:09, after the IC5 solder repair) showed Standalone serving the app at 540–746 kB/s while Router mode on the home 4G router associated, got `192.168.0.2` and then served nothing (pages and pings time out), lost the router's beacons after 539 s and did not see the router for 2 min ([evidence](test-evidence/wifi-0166-2026-11-09/README.md)). The review and the new plan are in [WIFI_PLAN_2026-11-09.md](WIFI_PLAN_2026-11-09.md); **0.17.0-rc2 implements it: *built*, not yet run on the board** (rc1 was bench-tested the same afternoon: standalone fine, the home router answered neither auth nor association requests, reasons 2/4/15, so rc2 runs the station on the driver's defaults exactly as the Prism controller, plan 2.2 revision 2). Verification is one bench session (plan section 5) that also separates the router, the place and the board, then the boat.


| Area | Result |
|------|--------|
| J1708 receive | Bench loop: 36,000 frames in 30 minutes without errors |
| Both KAD300 engines | Read on the boat: rpm, fuel rate, temperatures, boost, battery voltage, gear learned |
| Fuel figures | First fill-ups within about ±8 % before calibration |
| microSD, status LED | Bench tested |
| Own Wi-Fi (standalone) | **Not usable on the boat with 0.16.5 (2026-11-05).** Phones join and get a DHCP lease in every boot, but the link EngineLink → phone fails: in four boots no TCP connection from the phone reached the web server at all, the AP logged the phone leaving with code 2 eight to ten times in a row followed by code 209 (SA Query unanswered), the phone re-asked DHCP, and the two speed tests that completed gave 60–72 kB/s at 215–307 ms. Seen on boat 12 V, on USB only, and worst with both (a power-on reset occurred when 12 V was connected while on USB). The main loop blocked 5.6–10 s in the web part each time (the core's write retry, a consequence, not the cause). Earlier: about 650 kB/s on 0.13.4 (2 Nov), 265–346 kB/s at 91–99 ms on 0.15.0 (4 Nov, USB). Cause open: RF environment (router channel), power path, firmware AP settings dropped in 0.16.2, or board. The 2026-11-06/09 bench runs after the IC5 solder repair served the app at 540–1 105 kB/s on the own Wi-Fi; the boat is not re-tested yet. |
| Router mode | Zyxel EX3600 (WPA2/WPA3 mixed): previously 500–630 kB/s on free channel. Field report [#126](https://github.com/Kaelor9/EngineLink/issues/126), v0.13.4: 12 test average 584.6 kB/s (457.8–665.3), round trip 18–20 ms. Still 95 Wi-Fi disconnect events / 48 pre-connection failures and about 50 seconds reconnecting, so long-term stability is NOT verified. 0.16.2 replaced the Wi-Fi with a simple version. 0.16.6 on the bench router (reported 2026-11-08): joins with IP at −42 dBm, small answers work, the app page (110 KB) fails after 20.5 s; 0.16.2 had dropped the station's 20 MHz and beacon-timeout settings, restored in the third 0.16.6 bench build; the owner's test of that build (2026-11-08) failed worse: `/`, `/api/live` and `.local` timed out, 1 of 10 pings answered, no report of the running build. Cause open (plan 13.6.1: EngineLink left the router, or stayed on it with heavy frame loss); the fourth bench build makes the report and SD log record which (*built*); the A/B idea was dropped. 2026-11-09 (`75faba4`, driver defaults with modem sleep on): association and IP, no page, no ping, beacons lost after 539 s. **0.17.0-rc2** (station on the driver's defaults as the Prism controller; the app page as a long transfer; dead Wi-Fi 2.0 code removed) is *built*; rc1's station profile (power save off, country DK, 20 MHz, all-channel scan) was bench-tested on 2026-11-09 and failed the same way (reasons 2/4/15); the one bench session in [WIFI_PLAN_2026-11-09.md](WIFI_PLAN_2026-11-09.md) section 5 decides between firmware, router, place and board. |
| Firmware updates | Over Wi-Fi from GitHub and from a file, with rollback |

## Not yet checked

- Why the own Wi-Fi's downlink to the phone fails on the boat with 0.16.5 (2026-11-05): not yet separated between RF environment, power supply, the AP radio settings 0.16.2 dropped (20 MHz bandwidth, country, inactive time) and the board itself. Superseded by [WIFI_PLAN_2026-11-09.md](WIFI_PLAN_2026-11-09.md): the IC5 repair restored the own Wi-Fi on the bench; the boat is re-tested in plan section 5.5. Wi-Fi 2.0 Stage 4 (state machine, AP+STA) is dropped. The 5–10 s web stall is explained (Arduino core `NetworkClient::write()` retrying 10 × 1 s against a phone that does not acknowledge); the plan removes it by moving HTTP to `esp_http_server` (its own task) instead of patching the write. Firmware is reported as *built* (CI green, untested on hardware), *bench-verified* (owner ran plan section 5.2–5.3) or *boat-verified* (5.5 passed); this table says "verified on the board" only for the last.
- **0.16.6 development build (Wi-Fi 2.0 Stages 1–3): *built*, partially exercised on hardware (not yet fully bench-verified or boat-verified).** CI-equivalent checks pass locally (pinned core 3.3.12 compile, host tests `test_tracefold`, `test_httputil`, `test_wifistate` with plan 9.3's 20 scenarios, `test_wifi_contract.py`). The owner's 2026-11-06 bench tests exercised the access point radio settings (20 MHz, 600 s, country DK), the HTTP server, OTA boot confirmation, and three security responses (200/421/403); the remaining Stage 2 bench acceptance, including upload failure paths, complete export regression and latency checks, is still open. Evidence: [v0.16.6 owner bench report](test-evidence/wifi-0166-2026-11-06/README.md). (`docs/releases/v0.16.6.md`, plan 9.1/9.2). Not merged, not released. The 2026-11-06 fixes (engine lock let go during SD card work, 503 causes) ran on the board on 2026-11-08/09 without new stalls (longest engine wait 79 ms). **Hardware (2026-11-09):** a cold solder joint on IC5 (pins 5–8) was found and repaired; the 3.3 V rail and the own Wi-Fi's throughput were normal afterwards. It does not explain the router failures, which look the same before and after the repair.
- Router mode with the simple Wi-Fi (since 0.16.2) is not verified (see the router row: the app page failed on the bench router, fix *built*); it could not be tested on 2026-11-05 because the standalone link was already unreliable. The owner supplied Rigol DHO804 scope captures of 3.3 V under USB and laboratory input during Wi-Fi activity; no severe voltage collapse was demonstrated in those captures. The isolated maximum ripple reading is uncertain due to probe contact and dissimilar timescales. Combined USB+12 V interaction and full boat power/RF tests remain unmeasured; see [owner bench report](test-evidence/wifi-0166-2026-11-06/README.md).
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
