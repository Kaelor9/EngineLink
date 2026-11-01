# EngineLink: project handover

> Status as of **30 October 2026**, firmware **0.9.2**. This document is written so that any developer or AI assistant can pick up the work. It replaces the earlier handover from 26 October (version 0.2.0-alpha), which is out of date.
>
> **Talk to the owner in Danish.** Keep answers short and concrete; the owner prefers a brief summary over long explanations. The app itself is in English.
>
> **Repository:** https://github.com/Kaelor9/EngineLink  
> **Installer and live demo:** https://kaelor9.github.io/EngineLink/ and https://kaelor9.github.io/EngineLink/demo/  
> **Releases (firmware files):** https://github.com/Kaelor9/EngineLink/releases

## 1. What EngineLink is

An ESP32-S3 board on a **Fairline Targa 34 with two Volvo Penta KAD300 engines (EDC I control units)**. It plugs into each engine's **diagnostic connector** (J1708), reads the engines, and serves a phone app over its own Wi-Fi. Main purpose: **exact fuel consumption per engine and per trip**, plus engine diagnostics (a little like Volvo's VODIA, but read-only).

Principles that still hold:

- **Never show invented numbers.** Missing or stale data shows as "—" with a reason.
- **Read only.** The board only sends the KAD read request `01 43 21 <index hi> <index lo> <checksum>`. No commands, no clearing of fault codes, never programming the ECU. Reading the engines automatically is **off by default** and needs a confirmation.
- **Offline first.** Everything works on the board's own Wi-Fi without internet or a router.
- **Do not commit Volvo proprietary material** (decoded firmware, datasets, password or substitution tables). Only our own observations.
- Public demo and docs must not contain personal data (router names, home IP addresses).

## 2. Hardware (custom PCB)

ESP32-S3-WROOM-1-N8R8 (8 MB flash, 8 MB PSRAM, PCB antenna). Power: 12 V input → LM5164 buck → 5 V → 3.3 V LDO.

| Function | Pins |
|---|---|
| J1708 J7 | RX 21, TX 47 (TX attached only while reading) |
| J1708 J6 | RX 17, TX 18 |
| SD card (SPI, 4 MHz) | CS 10, MOSI 11, SCK 12, MISO 13, detect 14 (LOW = inserted) |
| Status LED SK6805 | 48 |
| NMEA 2000 CAN (listen only) | TX 15, RX 16 |
| Opto input | 38 |

Which socket is port or starboard is a setting (on this boat: port J6, starboard J7). See `docs/HARDWARE.md`.

## 3. Code layout

| Path | What |
|---|---|
| `firmware/EngineLink/EngineLink.ino` | All firmware (~4,500 lines): J1708 receive, KAD polling, fuel integration, trips, diagnosis, SD, Wi-Fi, web server, OTA |
| `firmware/ui/app.html` | The phone app (single file, plain JS). **Edit this**, then run `python3 tools/build_ui.py` to regenerate `firmware/EngineLink/ui.h` (gzip). `--check` verifies they match |
| `firmware/EngineLink/fuelcal.h` | Fuel calibration maths (weighted ridge fit from fill-ups) |
| `firmware/EngineLink/diagrules.h` | Diagnosis rules (limits, CUSUM drift, port/starboard imbalance, events) |
| `firmware/EngineLink/ledpatterns.h` | Status LED language (colours + rhythms) |
| `tools/test_*.cpp` | Host tests for the three headers (run in CI) |
| `tools/build_demo.py`, `tools/demo/` | Builds the browser demo (app + mock API) for GitHub Pages |
| `docs/PRODUCTION_ALPHA.md` | Change log per version (newest first). `docs/releases/vX.Y.Z.md` = release notes |
| `docs/KAD300_EDC1.md` | What is known about the KAD protocol on this engine |
| `.github/workflows/` | `validate.yml` (host tests, UI check, demo build), `build-engine.yml` (compile), `pages.yml` (release + Pages) |

**Build:** arduino-cli, FQBN `esp32:esp32:esp32s3:FlashSize=8M,PartitionScheme=default_8MB,PSRAM=opi,USBMode=hwcdc,CDCOnBoot=cdc`, core esp32 3.3.x, with `--build-property "compiler.c.elf.extra_flags=-Wl,--wrap=esp_wifi_init"` (needed to switch WPA3 off, see `wifiwpa3.cpp`; CI fails the build without it). Arduino pitfall: types used in function signatures must be declared near the top (the builder inserts prototypes before the first function).

**Release:** bump `#define EL_VERSION` in the .ino, README badge, add `docs/releases/vX.Y.Z.md` and a section in `docs/PRODUCTION_ALPHA.md`. On merge to `main` the workflow builds and publishes release `vX.Y.Z` with `enginelink-update.bin` (OTA / "Update from file") and `firmware.bin` (full USB image). The board can update itself from GitHub (Settings → Firmware) when it is on a router with internet; a new version that does not run properly for a minute is rolled back.

The owner has said **merge PRs without asking** once CI is green.

## 4. How the firmware works (0.9.2)

**Tasks and cores.** Core 0: Wi-Fi stack, the two J1708 receiver tasks (UART → frame queue in PSRAM, 2048 frames), the **engine task** (`engineTask`: frames, KAD polling, once-a-second engine/trip/diagnosis work), the GitHub update worker. Core 1: Arduino `loop()` (web server, SD, network management, LED). Engine data is shared under one mutex (`engineMx`); the loop releases it whenever it waits on the network (request parsing, every socket write, router scans, its 1 ms pause). Watchdogs: engine task (10 s without a round → save and restart) and loop task (60 s).

**KAD reading.** Request `01 43 21 hi lo cs`; the echo of our own request is seen on the bus ~8 ms later; the ECU answers `83 C1 <width nibble>1 <value…> <cs>` (value big-endian). Fast values every round (256 rpm, 289 fuel), slow values in turn. Pace adapts to answers (80–220 ms). Echoes are recognised against the last four requests; a reply only counts after its echo; the frame queue is drained before judging a timeout. "Another device polls this engine" only after 3 foreign requests in 10 s.

**Fuel.** Raw fuel counts per engine and rpm band are stored; litres = raw × calibration from fill-ups (`fuelcal.h`), recomputed whenever the fuel log changes. Totals saved to NVS every 30 s while an engine runs, every 5 min otherwise.

**Trips.** A trip starts with the first engine and ends when both have been off for N minutes (setting). Each trip gets a 1 Hz log on SD (`/TRIPS/Txxxxxx.BIN`, 40-byte samples) and a data-coverage file (`.COV`). Raw J1708 of the whole session is logged to `/ELxxxxxxxx.CSV`.

**Diagnosis.** Events (with a ±2 min black box on SD), life story per engine, port vs starboard. Rules are host-tested on simulated trips.

**App.** Tabs Trip, Economy, Tank, History, Engines; Settings and Diagnostics pages. Export sheet: printable trip/engine reports (the app opened as `?report=trips&ids=…` / `?report=engines`) and CSV files (downloads are deflate-compressed with the ESP32 ROM compressor when the browser accepts it).

## 0.9.2 — rescue AP after failed router association

- User report: long green + short blue LED (Router unreachable), while EngineLink SSID invisible and router access unavailable after switching from standalone.
- P0 change: AP+STA unavailable for 45 seconds -> shut down STA, recreate standalone AP, and save `enginelink/rhold=true` in NVS. The board starts AP-only on subsequent boots until the user explicitly presses Connect to a router through existing Settings. No added UI or passwords. Retains all router credentials/data.
- Diagnostic API exposes routerHold, routerFallbacks, apMode, apDriverSsid and apDriverIp; `tools/test_routerguard.cpp` tests the deadline.
- Hardware proof is STILL outstanding. User must use [docs/USB_RECOVERY.md](docs/USB_RECOVERY.md) with partition backups if already locked out; the full 8 MB merged web installer overwrites NVS/LittleFS, even without a separate erase option, then validate AP broadcast and access on iPhone.
- Tradeoff: no automatic background reconnect after 45-second timeout; user must retry deliberately to avoid recurrent lockouts.

## 0.9.1 AP-only stability

- Pure AP no longer performs initial Wi-Fi channel scan or temporarily enables STA; saved AP channel stays fixed. AP watchdog is less aggressive and avoids manual `esp_wifi_start()`.
- Hardware AP-only testing on the owner's custom PCB **is still pending**. If phone Wi-Fi itself disconnects without a router, review hardware RF, power rail and AP event reason codes.

## 0.9.0 stabilization pass (software-only)

- Wi-Fi: defer STA reconnect while user-requested async scan is active.
- OTA: shared packet-fragmented ESP32 header check (`otaheader.h`), exercised by `tools/test_otaheader.cpp` in CI, for both GitHub and file upload.
- Persistence: record LittleFS write failures and recoveries in the existing diagnostic API; do not mark health/life state clean if a write failed.
- The UI and motor-command whitelist are unchanged. Still verify motor task lock waits, frame drops, Wi-Fi, downloads, OTA and sudden power loss on hardware before describing the release as production-ready.

## 5. Wi-Fi: the main open problem

**0.8.3 software changes:** unknown-channel STA association now scans freely (channel 0) instead of incorrectly restricting attempts to the SoftAP channel; stale BSSID pins are dropped after repeated failures. The app uses an independent read-only live endpoint on TCP port 81 to keep `/api/live` available while large port-80 downloads are running. LittleFS now retains recovery copies during writes. OTA distinguishes no Wi-Fi from no download bytes. These changes are **not verified on real hardware**. Build CI regenerates `ui.h` from the app source, but local developers must run `python3 tools/build_ui.py` after editing `app.html`.

The owner reports the Wi-Fi as **very unreliable**: the board barely finds routers when scanning, and the router connection keeps dropping even with a good signal (around −63 dBm). On the boat the board also runs its own access point (SSID EngineLink, 192.168.4.1). **This is not solved and has not been verified on the board.**

What the code does now (see `radioTuning`, `startAP`, `applyNetwork`, `findRouter`, `connectRouter`, `serviceNetwork`, `handleWifiScan`):

- AP + STA on one radio, so the AP must use the router's channel. Power saving off, 20 MHz channels, country EU channels 1–13.
- Without a router: at start-up picks the quietest of channels 1/6/11.
- Router: connects by BSSID and channel; on failure retries every 20–30 s (each attempt gets ≥20 s); re-scans for the router every third failure (at most every 5 min while phones are joined). No connect attempts during a user's network scan. Scans listen 300 ms per channel. STA tolerates 20 s without beacons. UDP keep-alive to the gateway every 25 s.
- Transmit power setting (Diagnostics → Wi-Fi): maximum 19.5 / medium 15 / low 11 dBm.
- Diagnostics → Wi-Fi shows the last router drop reason (Wi-Fi reason code in words), drops, channel changes, a download speed test, and Diagnostics → Restarts shows the restart log (brownouts etc.).

History of what was wrong (fixed, in case it helps): reconnect backoff up to 5 min; no re-scan while a phone was joined (router changing channel was never found again); connect attempts cut user scans short; attempts restarted after 8 s.

Suspects not yet ruled out, in rough order:

1. **Hardware RF / power**: PCB antenna placement near metal, or the 3.3 V supply sagging under Wi-Fi TX peaks (try medium/low TX power; check the restart log for brownouts).
2. **Router settings**: band steering (same SSID on 2.4/5 GHz kicking the ESP32 off), automatic channel changes, WPA3-only or PMF required.
3. **Single-threaded web server**: while one client downloads or a report is built, other clients get no live data and show "Reconnecting". Looks like a Wi-Fi drop but is a queue.
4. The ESP32 STA + AP sharing one channel: any router channel change also drops phones on the EngineLink network.

Useful next step: get the owner's **Diagnostics → Wi-Fi** screen (last router drop reason, drops, channel changes) and **Restarts** after running 0.8.2 for a while, and test with only the AP (no router) to separate AP issues from router issues.

## 6. What is verified and what is not

Verified: host tests (fuel calibration, LED patterns, diagnosis rules); every version compiles in CI; the app and reports work against the mock in a browser; the owner has read both engines on the boat (rpm, fuel, temperatures, boost, gear learned, battery confirmed), done fill-ups (fuel figures ±8 %), and exported reports.

Not verified on the board: compressed downloads in real browsers, the engine task split and watchdogs, data-coverage figures, SD auto-recovery, power-cut behaviour, all Wi-Fi changes from 0.7.6 onwards, "Discover more values" (an earlier result listing every index 256–319 came from the echo bug fixed in 0.7.7 and should be re-run).

Owner's observations still open: a "status bits changed" event appears often (bits in values 263/264 not decoded yet); a port rpm-hunting warning at idle once; fault codes need a VODIA capture (owner has not confirmed access to VODIA).

## 7. Owner's preferences

- Danish, short, no walls of text. Explain what was wrong, what changed, and what is not tested.
- Things must work on all smartphones; the iPhone home-screen app has had layout issues (menu under the home bar: fixed in 0.7.2 by always reserving 34 pt).
- Status LED language is fixed (`ledpatterns.h`): distinct colours and rhythms, no two similar colours next to each other.
- No "Recommended" labels pushing a choice; no fast animated graphics; no manual trip start/stop.
