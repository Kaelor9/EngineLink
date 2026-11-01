# EngineLink: developer guide

For anyone (person or AI assistant) continuing work on EngineLink. It
describes how the project is built and the rules it follows. Release notes for
every version are in [docs/releases](docs/releases); what has and has not been
tested on hardware is in [docs/STATUS.md](docs/STATUS.md).

**Talk to the owner in Danish**, briefly and concretely. The app itself is in
English. The owner has asked for pull requests to be merged without asking once
CI is green.

## What EngineLink is

A custom ESP32-S3 board on a Fairline Targa 34 with two Volvo Penta KAD300
engines (EDC I). It connects to each engine's J1708 diagnostic connector,
reads the engines and serves a phone app from the board, over its own Wi-Fi or
the boat router. No cloud, no app store, no internet needed.

## Rules

- **Never show invented numbers.** Missing or stale data shows as "—" with a reason.
- **Read only.** The only frame EngineLink transmits is the KAD read request
  `01 43 21 <index hi> <index lo> <checksum>`. No commands, no clearing fault
  codes, never programming the ECU. Reading the engines automatically is off by
  default and needs an explicit confirmation.
- **Offline first.** Everything works on EngineLink's own Wi-Fi without a router.
- **No Volvo proprietary material** in the repository (decoded firmware,
  datasets, passwords, substitution tables). Only our own observations.
- **No personal data** in the public demo, docs or device reports (router
  names, home addresses). Device reports replace the router name.

## Code layout

Firmware (`firmware/EngineLink/`). Arduino joins the `.ino` files into one
program, so they share the globals in `EngineLink.ino`.

| File | What |
|---|---|
| `EngineLink.ino` | Libraries, global state, `setup()` and `loop()` |
| `model.h` | All data types and constants (in a header so Arduino's generated prototypes always see them) |
| `engines.ino` | J1708 receive tasks, frame checks, J1587 and KAD decoding, read-only KAD polling, engine task |
| `fuel.ino` | Fuel integration per rpm band, calibration from fill-ups, fuel log, tank estimate |
| `trips.ino` | Trip start/end, trip logs, data coverage, history, per-second learning |
| `diagnosis.ino` | Sample ring, rules, events and black boxes, engine life story, series downloads |
| `storage.ino` | SD card, LittleFS, NVS settings and totals, restart log, raw logging |
| `wifi.ino` | Own access point or router, fallback and return, scans, mDNS |
| `webserver.h` | The port 80 web server (connection pool) |
| `webapi.ino` | JSON helpers, the app's API handlers and the route table |
| `netreport.ino` | Network report, ports 81 and 82, web server self-check, transmit power task |
| `downloads.ino`, `sdexport.ino` | Streaming (compressed) downloads, CSV exports, SD clean-up, backup |
| `updates.ino` | Firmware updates from GitHub or a file, with rollback |
| `led.ino`, `nmea2000.ino`, `enginetest.ino` | Status LED, NMEA 2000 listener, engine test capture |
| `wifiwpa3.cpp/.h` | Wraps `esp_wifi_init` to switch WPA3-SAE off unless the router needs it |
| `linktune.cpp/.h` | Automatic transmit power, link measurements, channel scan |
| `netboost.cpp/.h` | Raises the TCP send window per connection |
| `crashinfo.cpp/.h` | The last crash from the core dump, for the report |
| `fuelcal.h`, `diagrules.h`, `ledpatterns.h`, `routerguard.h`, `otaheader.h` | Logic without Arduino, tested on a PC in CI |

Elsewhere:

| Path | What |
|---|---|
| `firmware/ui/app.html` | The phone app (one file, plain JavaScript). Edit this, then run `python3 tools/build_ui.py` to regenerate `ui.h` |
| `tools/test_*` | Host tests and source checks, run in CI (`tools/fwsource.py` reads the firmware for the source checks) |
| `tools/build_demo.py`, `tools/demo/` | Browser demo (app + simulated boat) for GitHub Pages |
| `docs/` | Protocol notes, hardware, status, release process |

## Build and release

```bash
arduino-cli compile \
  --fqbn "esp32:esp32:esp32s3:FlashSize=8M,PartitionScheme=default_8MB,PSRAM=opi,USBMode=hwcdc,CDCOnBoot=cdc" \
  --build-property "compiler.c.elf.extra_flags=-Wl,--wrap=esp_wifi_init" \
  firmware/EngineLink
```

ESP32 Arduino core 3.3.x and the Adafruit NeoPixel library. The link option is
required: CI checks that the WPA3 switch is linked in.

To release: bump `EL_VERSION` in `EngineLink.ino`, update the README badge and
status table, and add `docs/releases/vX.Y.Z.md`. Merging to `main` builds and
publishes release `vX.Y.Z` with `enginelink-update.bin` (Wi-Fi update) and
`firmware.bin` (full USB image, erases settings and data). The board can update
itself from GitHub; a version that does not run properly for its first minute
is rolled back. Details: [docs/RELEASING.md](docs/RELEASING.md).

## How the firmware works

**Tasks.** Core 0: Wi-Fi, the two J1708 receiver tasks (UART to a frame queue
in PSRAM) and the engine task (frames, KAD polling, once-a-second engine, trip
and diagnosis work). Core 1: the main loop with the web server on port 80, and
two small listeners: port 81 for live data and the network report, port 82 for
the speed test. A mutex (`engineMx`) guards engine state between the engine
task and the web server.

**KAD reading.** Request `01 43 21 hi lo cs`; our own request echoes on the bus
about 8 ms later; the ECU answers `83 C1 <width>1 <value…> <cs>` (big-endian).
Fast values (rpm, fuel rate) every round, slow ones in turn. Every answer passes
physical limits and jump confirmation. See [docs/KAD300_EDC1.md](docs/KAD300_EDC1.md).

**Fuel.** Raw fuel counts are stored per engine and rpm band; litres are raw ×
the calibration fitted from fill-ups (`fuelcal.h`) and recomputed when the fuel
log changes. Totals are saved to NVS every 30 s while an engine runs.

**Trips.** A trip starts with the first engine and ends when both have been off
for a set number of minutes. Each trip has a 1 Hz log on the SD card.

**Diagnosis.** Limits, drift (CUSUM) and port/starboard imbalance rules in
`diagrules.h`; events keep a ±2 minute black box on the SD card.

**Wi-Fi.** Two exclusive modes. Standalone: access point only, `EngineLink` at
192.168.4.1. Router: station only, reachable at the router's address. If the
router connection is lost, the access point returns after 120 s (300 s while a
login is still in progress) and EngineLink looks for the router again, at
growing intervals, and goes back by itself. WPA2 is used unless the router is
WPA3-only. Transmit power is chosen by `linktune.cpp`. The network report
(`/api/network-report`, also on port 81 at `/report`) holds everything needed
to diagnose a connection; with a GitHub token saved, the board files it as an
issue by itself after problems.

Lessons from the router work (late 2026): the prebuilt libraries' 4-packet TCP
send window capped downloads through a router; the remaining slowness was a
crowded 2.4 GHz channel on the router (channel 1), fixed by moving the router to
channel 11. A-MPDU on/off and power save made no measurable difference.

## Owner's preferences

- Danish, short, no walls of text. Say what was wrong, what changed and what is
  not tested.
- It must work on every smartphone, including the iPhone home-screen app.
- The status LED language in `ledpatterns.h` is fixed.
- No "Recommended" labels pushing a choice, no fast animations, no manual trip
  start/stop, nothing to tune by hand that the board can work out itself.
