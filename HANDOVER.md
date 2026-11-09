# EngineLink: developer guide

For anyone (person or AI assistant) continuing work on EngineLink. It
describes how the project is built and the rules it follows. Release notes for
every version are in [docs/releases](docs/releases); what has and has not been
tested on hardware is in [docs/STATUS.md](docs/STATUS.md).

**Talk to the owner in Danish**, briefly and concretely. The app itself is in
English. The owner has asked for pull requests to be merged without asking once
CI is green — except changes that touch networking: those are merged and
released only after the owner has run the bench and boat protocol in
[docs/WIFI_PLAN_2026-11-09.md](docs/WIFI_PLAN_2026-11-09.md) section 5 (owner's
rule of 2026-11-05).

Never ask for, repeat or store secrets (GitHub tokens, passwords) in chats,
code or reports. The owner's GitHub token for reports lives only on the board.

## Collect, then release

Small fixes and wishes go into [docs/TODO.md](docs/TODO.md) first. Release
them together when the owner asks for an update, not one version per fix
(every release costs the owner time and tokens). When an update is made,
push and merge it once CI is green so it shows under "Check for updates".

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
| `gearbox.ino` | Gear and clutch: four readings a second (ring, trip and event files), shift timing per engine, compressor, warm-up and charging, `/api/shifts` and `/api/fast` |
| `storage.ino` | SD card, LittleFS, NVS settings and totals, restart log, raw logging |
| `wifi.ino` | Own access point or router, never both (kept under 300 lines by `tools/test_wifi_simple.py`): the AP and station radio profiles, the 2 min recovery fallback and 10 min retry, scans, mDNS, the Wi-Fi event trace |
| `serialcmd.ino` | USB serial commands and the Wi-Fi log/reports on the SD card (0.16.3) |
| `httpd.h`, `httpd.ino` | The web server since 0.16.6: ESP-IDF `esp_http_server` on port 80 in its own task, the adapter that keeps the handlers' `web.arg()`/`web.send()` calls, the own-origin security checks, and the task for long transfers |
| `httputil.h` | URL decoding and the own-address allow-list (host-tested, `tools/test_httputil.cpp`) |
| `netsvc.ino` | Network services; since 0.16.6 the boot self-test that confirms a new firmware or rolls it back |
| `tracefold.h` | Folding of repeated Wi-Fi events in the trace (host-tested by `tools/test_tracefold.cpp`) |
| `webapi.ino` | JSON helpers, the app's API handlers and the route table |
| `netreport.ino` | Network report and health JSON, link diagnostics (joined phones, the AP as the driver runs it, the router as the station sees it) |
| `downloads.ino`, `sdexport.ino` | Streaming downloads (uncompressed since 0.16.6), CSV exports, speed test, SD clean-up, backup |
| `updates.ino` | Firmware updates from GitHub or a file, with rollback; sends the GitHub reports |
| `enginereport.ino` | Engine report after each trip: how well both engines were read, plus the diagnostics JSON |
| `led.ino`, `nmea2000.ino`, `enginetest.ino` | Status LED, NMEA 2000 listener (navigation, bus census, per-trip `.NAV` log, `/api/nmea`), engine test capture |
| `nmea2k.h` | NMEA 2000 decoding, fast packets and census, host-tested (`tools/test_nmea2k.cpp`) |
| `crashinfo.cpp/.h` | The last crash from the core dump, for the report |
| `fuelcal.h`, `diagrules.h`, `shiftrules.h`, `ledpatterns.h`, `otaheader.h`, `httputil.h`, `tracefold.h` | Logic without Arduino, tested on a PC in CI |

Elsewhere:

| Path | What |
|---|---|
| `firmware/ui/app.html` | The phone app (one file, plain JavaScript). Edit this, then run `python3 tools/build_ui.py` to regenerate `ui.h` |
| `tools/test_*` | Host tests and source checks, run in CI (`tools/fwsource.py` reads the firmware for the source checks) |
| `tools/build_demo.py`, `tools/demo/` | Browser demo (app + simulated boat) for GitHub Pages |
| `docs/` | Protocol notes, hardware, status, release process |
| `.claude/skills/`, `.claude/references/` | Agent Skills and Ponytail for Claude Code (third-party, MIT). When and how they are used is in `CLAUDE.md` |

## Build and release

```bash
arduino-cli compile \
  --fqbn "esp32:esp32:esp32s3:FlashSize=8M,PartitionScheme=default_8MB,PSRAM=opi,USBMode=hwcdc,CDCOnBoot=cdc" \
  firmware/EngineLink
```

ESP32 Arduino core **3.3.12** (pinned in CI, built on ESP-IDF 5.5.5) and
Adafruit NeoPixel 1.15.5. No linker options: since 2026-11-09 the firmware
uses only documented Arduino ESP32 and ESP-IDF APIs (`tools/test_wifi_contract.py`
refuses `--wrap`, private lwIP headers and the removed hook files).

To release: bump `EL_VERSION` in `EngineLink.ino`, update the README badge and
status table, and add `docs/releases/vX.Y.Z.md`. Merging to `main` builds and
publishes release `vX.Y.Z` with `enginelink-update.bin` (Wi-Fi update) and
`firmware.bin` (full USB image, erases settings and data). The board can update
itself from GitHub; a version that does not run properly for its first minute (since 0.16.6: that no web client got an answer within 10 minutes, plan 6.10)
is rolled back. Details: [docs/RELEASING.md](docs/RELEASING.md).

## How the firmware works

**Tasks.** Core 0: Wi-Fi, the two J1708 receiver tasks (UART to a frame queue
in PSRAM) and the engine task (frames, KAD polling, once-a-second engine, trip
and diagnosis work). Core 1: the main loop, and since 0.16.6 the web server
(`esp_http_server` task `httpd`, port 80 only) and the `xfer` task for long
downloads. A mutex (`engineMx`) guards shared state: the loop holds it while it
works, a web handler takes it (2 s, else 503) and lets go whenever it waits for
the phone. Up to 0.16.5 the web server ran inside the loop, with extra
listeners on ports 81 (live data, report) and 82 (speed test). Since the
2026-11-06 fixes, slow SD card work (directory walk, Wi-Fi log, wipe) runs
with the lock let go (`sdGapBegin/End`, plan 12.1 I10), and nothing unmounts
the card while it does.

**Tasks and cores (review 2026-11-06, as built; FreeRTOS priority in brackets).**

| Core 0 | Core 1 |
|---|---|
| Wi-Fi driver (23), esp_timer (22), event loop (20), lwIP `tcpip` (18), J1708 receivers `J7RX`/`J6RX` (4), `engines` (3), `fw` GitHub worker (1), mDNS (1) | `httpd` web server (5), `xfer` long transfers (4), Arduino network events, main `loopTask` (1) |

What the measurements say and what is deliberately left for later:

- **The waits are lock waits, not CPU.** The engine task needs little CPU;
  its long waits came from `engineMx` held by others (SD 7.3 s, network switch
  1.5–2.5 s). The report now names the holder at the longest wait and the
  engine task's own longest round, so later changes start from numbers.
- **The loop holds `engineMx` almost all the time** and lets go once a pass
  (`delay(1)`), so engine processing waits for the loop's pass (typically
  5–70 ms). Frames are not lost meanwhile: the receivers stamp each frame when
  the UART's hardware timeout ends it and queue it (2048 frames in PSRAM).
- **Priority inheritance crosses cores.** While the engine task (3) waits for
  the loop (1), the loop inherits 3, but `httpd` (5) and `xfer` (4) on core 1
  still run before it.
- **The engine task writes the raw log itself** (`writeRecord()`), so a slow
  card write stalls one engine round while it holds the lock.
- Engine work on core 0 beside the Wi-Fi driver is fine: the driver preempts
  it for microseconds to milliseconds and the frame timestamps come from the
  receivers.

After the Wi-Fi plan's acceptance (separate task, [issue #161](https://github.com/Kaelor9/EngineLink/issues/161), `docs/TODO.md`), in this order and
only with measurements: the loop takes `engineMx` around shared data instead
of holding it; raw-log and other SD writes go to one writer task with a
bounded queue, so no SD operation runs in the engine task or under its lock;
`/api/live` reads a snapshot the engine task publishes. Core affinity and
priorities stay as they are unless a measurement shows a need.

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

**Gear and clutch.** The KAD300/DP shifts electrically (EDC, shift actuator,
cone clutch in the drive). Four times a second `shiftrules.h` compares the
learned gear bits in 264 with the propeller load, seen as fuel above the
engine's neutral idle at the same rpm (about 2.2 against 3.1 L/h on the boat).
It times each shift, raises `pull-neutral` when the propeller keeps pulling
2.5 s after neutral and `hard-shift` for fast reversals or shifts above
1,000 rpm. While an engine runs below 1,300 rpm, 264 takes every other slow
slot (about 0.7 s) and the other slow values come about every 15 s. The gear
and compressor bits are learned by the guided drive test in the app (page
`dtest`), which reads `/api/fast` while the owner shifts. Fast samples
(`FastSample`) go to `/TRIPS/Tnnnnnn.FST` and `/EVENTS/Ennnnnn.FST`. The
stored life story counts only the first 22 event types (`LIFE_EV_N`); newer
types are counted in `ShiftLife`.

**Wi-Fi** (plan [docs/WIFI_PLAN_2026-11-09.md](docs/WIFI_PLAN_2026-11-09.md),
implemented 2026-11-09, *built*, bench pending). Two modes, never both:
- **Standalone:** access point only, `EngineLink` at 192.168.4.1, channel 6
  (saved `apch`), max 6 phones. `apRadioSetup()`: 20 MHz, stations kept 600 s,
  country DK (802.11d on), no power save, then the fixed 19.5 dBm.
- **Router:** station only, at the router's address and `enginelink.local`.
  `staRadioSetup()` after `WiFi.mode(WIFI_STA)`, before `WiFi.begin()`:
  `WiFi.setSleep(false)` (no modem sleep: a server must receive unsolicited
  frames at once; the driver's default `WIFI_PS_MIN_MODEM` delays reception
  to the DTIM cycle), the same country as the AP (the driver is deinitialised
  at every mode switch, so each mode sets it), 20 MHz, and
  `WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN)` so a mesh joins its strongest
  access point. Everything else is the driver's default: PMF capable, WPA2
  minimum, WPA3-SAE negotiated by the driver, 20 dBm, 6 s beacon timeout, and
  the core's own reconnect (`WiFi.setAutoReconnect(true)`: a new attempt right
  after every failure).
- **Recovery (a mechanism, not a mode):** no router IP for 120 s → the own
  Wi-Fi comes up; the router is tried again every 10 min while no phone is
  joined, or at once when the app's Connect is used. Not saved: every start
  in router mode tries the router first. The report says
  "Router (router not reached; EngineLink Wi-Fi on meanwhile …)" and the JSON
  `recovery:true`.
- All Wi-Fi calls run in `serviceNetwork()` (main loop); the event callback
  only records counters (`tools/test_wifi_contract.py`). The web server
  (`esp_http_server`) binds every interface and is never restarted; a mode
  switch only closes its sessions (`httpdSuspend()`). The 110 KB app page
  streams from the `xfer` task like the exports, so a slow phone never holds
  the server task; one request may wait behind a running transfer, a third
  gets 503.

What the 2026-11-09 review found and why (short; the plan has the evidence):
router mode worked for hours on the boat router (0.13.4, 0.15.0) but never
served the page on the home 4G router, with power save on or off, 20 or
40 MHz, with or without the WPA3 wrapper and the TCP window patch. There the
station stops receiving (ARP, ping, TCP from the PC lost, beacons lost,
reason 200) while its own outbound traffic works (a report reached GitHub over
HTTPS), and 49 full scans in 2 min do not see the router at all. That is below
the firmware's logic: the router's 2.4 GHz radio, the place, or the board's
receive path; plan section 5 tells them apart in one bench session. The
firmware defects found (modem sleep on, country only in AP mode, the unwired
Wi-Fi 2.0 state machine and 0.15.0 leftovers, the "Recovery" mode wording, the
app page in the server task) are fixed in 0.17.0-rc1.

The network report
(`/api/network-report`, also on port 81 at `/report`) holds everything needed
to diagnose a connection; with a GitHub token saved, the board files it as an
issue by itself after problems. After each trip it also files an engine report
("Engine report: trip N"): frames, KAD requests and replies, values held back or
skipped, data coverage and the diagnostics JSON. A trip that ends away from the
router is reported once the board is back on it.

Lessons from the router work (late 2026): the prebuilt libraries' 4-packet TCP
send window capped downloads through a router; the remaining slowness was a
crowded 2.4 GHz channel on the router (channel 1), fixed by moving the router to
channel 11. A-MPDU on/off and power save made no measurable difference.
EngineLink must not require such router changes: as a station it follows the
router's channel, so a busy channel lowers speed but must not break the link.

Wi-Fi review (2026-11-04, before 0.15.0), from reports #95, #126, #130 and the
engine reports #104/#107:
- The automatic transmit power (0.12.2–0.14.1) never took effect at start.
  The driver reported 20 dBm while 11 or 15 dBm was "chosen", because
  `esp_wifi_set_country()` ran after `setTxPower()`. Its ping test never
  completed in the field, since the app polls every second. The theory that
  a lower power helped was therefore never tested.
- The Arduino core (3.3.12, `STA.cpp`) retries with `disconnect()` +
  `connect()` at once on every failure. Each try is logged as code 201 plus
  code 36 in the same millisecond, every 1.57 s. That filled the 64-entry
  event list and hid the first cause of the outage in #126.
- #95: after power-on the router was not found for 120 s; Connect at 311 s
  worked in 1 s. The remembered rescue latch would have kept later starts on
  the access point.
- #126: the router (−39 dBm) was not found on any channel for about 50 s,
  probably while it restarted its radio (automatic channel). EngineLink got
  back by itself.
- The main loop holds `engineMx`. Its longest passes were 3.3–9.1 s, and the
  engine task waited up to 4.25 s (#104/#107). The cause was not measured,
  so 0.15.0 logs the longest time per loop part in the report.
- The loopback web self-check counted as app use, so a phone joined to the
  access point held back the return from rescue.

First 0.15.0 reports (#141–#143, 2026-11-04, board started over USB):
- Router join took 1.4 s and DHCP 1.1 s; pings to the router were 1–3 ms
  with 0 % loss; no disconnects. Router speed tests reached 490–600 kB/s at
  an 18 ms round trip.
- The main loop stood still for up to 9.4 s (engine wait 9.4 s). Per part:
  - serial 2.2 s: the Arduino core's HWCDC waits up to 20 × 100 ms per write
    when a USB host is attached but not reading. Fixed with
    `Serial.setTxTimeoutMs(0)`.
  - network 6 s: three prints in a mode switch, the same cause.
  - raw cleanup 7.4 s: a one-pass SD directory scan, now incremental.
- Standalone speed tests on the phone came in at 265–346 kB/s with a 91–99 ms
  round trip, against 18 ms on the router with the same phone. Not explained
  yet; possibly the phone's power save on a network without internet. Measure
  again without USB attached before changing anything.

**CURRENT Wi-Fi handoff (2026-11-09; read before dated history below):** [Full current case and failure timeline](docs/test-evidence/wifi-0166-2026-11-09/README.md), including the [unabridged 0.16.6 report](docs/test-evidence/wifi-0166-2026-11-09/firmware-75faba4-maggan-recovery.txt). Branch `claude/wizardly-lovelace-v6tlqz`, draft PR #160, normal build **#261** at `75faba4` passed CI and was run on the repaired PCB. Own Wi-Fi served the UI at 540–746 kB/s; router Maggan associated at +1068 s and obtained 192.168.0.2 at +1069 s, **yet both / and /api/health timed out over LAN**, then disconnect reason 200 (beacons lost) at +1608 s and 49 reason-201 retries. Earlier firmware also failed, and router failure is **not fixed**. IC5 cold joint repaired; no proven 3.3 V transient correlation. The old exceptions `wifiwpa3` and `netboost` were removed. Fable is authorized to revise the Wi-Fi plan from scratch on evidence, **planning only**; implementation/merge/release require approval. All below is historical unless consistent with this case and the new owner directive.\n\nWi-Fi 0.16.0–0.16.5 (2026-11-04, standalone on the boat with an iPhone):
- 0.16.0 (0.15 Wi-Fi): the phone saw `EngineLink` but could not join with
  `12345678`, then the network disappeared. 0.16.1 put back the 0.12.15 Wi-Fi
  code; it also failed.
- Owner's decision: Wi-Fi code must stay dead simple, like ordinary ESP32
  projects, never back to ~1000 lines. Don't touch the app UI for Wi-Fi work.
  0.16.2 rewrote `wifi.ino`: one mode at a time (AP or router), AP
  192.168.4.1 channel 6, max 6 phones, router fallback to AP after 120 s
  without IP, router retried every 10 min while no phone is joined. The old
  contract tests (network architecture, router reliability, scan contract)
  were replaced by `tools/test_wifi_simple.py`.
- 0.16.3 USB log: AP up and "Phones connected now: 1" but "HTTP requests: 0".
  Root cause: 0.16.2 dropped `web.rebindNetwork()` after the AP came up, so
  port 80 never listened on 192.168.4.1. 0.16.4 restored the rebind (serial
  prints `HTTP/80 listening on 192.168.4.1`).
- 0.16.4 USB report: phone joined at +23 s, left at +40 s (reason 8), still 0
  HTTP requests; the driver reported 8.5 dBm. 0.16.5 removed the AP-only
  8.5 dBm override and counts DHCP leases, port-80 accepts and HTTP parse
  failures separately. Not yet verified on the board (see TODO).
- Reset reason 11 in web-installer logs is the console's USB reset, not a crash.
- 0.16.5 USB report: phone joined, DHCP lease, 6 HTTP requests, 19.5 dBm, so
  standalone reaches the web server again. Main loop stalled up to 10 s in
  the web part (unexplained).
- Next was **Wi-Fi 2.0** (plan of 2026-11-04/05, `docs/WIFI_REWRITE_PLAN.md`,
  now history). Of it, Stages 1–3 were built in the 0.16.6 development build
  (`apRadioSetup()`, `esp_http_server` with the security model and the boot
  self-test, trace folding) and stay. Its Stage 4 (four-state machine, AP+STA
  idle station, router tests and looks) and its two exceptions (`wifiwpa3`,
  `netboost`) were dropped on 2026-11-09 after the review in
  [docs/WIFI_PLAN_2026-11-09.md](docs/WIFI_PLAN_2026-11-09.md), which is the
  current plan; the "Wi-Fi" paragraph above describes what 0.17.0-rc1 does.

USB serial commands (0.16.3, 115200 baud, web installer "Logs & console" or any
serial monitor): `wifi` (full Wi-Fi report), `wifi log` / `read wifi log`
(`/WIFI.LOG`), `wifi reports` / `read wifi report from sd` (`/WIFIREP.TXT`),
`wifi save`, `status`, `help`. The SD log gets one line per Wi-Fi event; a full
report is appended 2 min after boot and every 30 min. Both files are capped at
256 KB and rotate to `.OLD`, so they survive reflashing.

## Field data

The board files its reports as GitHub issues in this repository: "Engine report:
trip N" after each trip and "Device report: …" for Wi-Fi problems and after each
start. Read them with `gh issue list` / `gh issue view N`. They are the first
place to look before changing engine reading or Wi-Fi behaviour: change things
because a report shows a problem, not on a guess.

## Owner's preferences

- Danish, short, no walls of text. Say what was wrong, what changed and what is
  not tested.
- It must work on every smartphone, including the iPhone home-screen app.
- The status LED language in `ledpatterns.h` is fixed.
- No "Recommended" labels pushing a choice, no fast animations, no manual trip
  start/stop, nothing to tune by hand that the board can work out itself.
- **Visual work: show options first.** For pictures and animations the owner
  wants to choose before anything goes into the app: build a page of live,
  clickable options (an artifact), let him pick, put the choice in
  `docs/TODO.md`, and only build it into the app with the next update. He
  likes many options and finds plain flat drawings too minimal.
- Slow, calm animations are fine (the drive test uses them); they must also
  look right with reduced motion turned on (show one still scene, nothing
  overlapping).

## USB web installer version archive (2026-11-03)

The website at `site/index.html` / `site/app.js` includes a firmware
version selector. `tools/build_installer_archive.py` runs during
`.github/workflows/pages.yml` after GitHub Release publication, downloads
prior 8 MB `firmware.bin` merged images from the release assets, verifies the
ESP boot image and release SHA-256, and publishes matching per-version
manifests and binaries under `site/versions/` (at most 90 older versions).
The browser still defaults to the newest build; historical choices only
use same-origin GitHub Pages binaries to avoid CORS trouble. They overwrite
internal settings and history just like the existing USB installer. This
does not change the device's separate five-item OTA release feed. See
`docs/RELEASING.md` and `tools/test_installer_archive.py`.

**Installer selector hotfix (2026-11-03).** The previous implementation
left the `#firmware-version` element disabled until the external
`esp-web-tools` JavaScript module had loaded and the latest manifest was
verified. If either failed (browser, CDN, Web Serial, network), older
versions could never be selected. `site/app.js` now enables the selector
before any network calls, loads `versions/index.json` independently, and
loads the USB library only when a selected firmware image is verified.
`tools/test_firmware_selector.js` simulates blocked CDN, failed latest
manifest and normal installation. The Pages build now includes the
Git commit SHA in `app.js` / `styles.css` asset URLs: site-only updates
must not rely on the firmware version for cache invalidation. No firmware
version bump is needed for this website-only hotfix.

**Older USB image verification hotfix (2026-11-03).** The version
selector worked but could display "The USB firmware is not a complete 8 MB
image" after selecting a valid older release. The browser had compared
`HEAD Content-Length` to the original 8 MiB file size. An HTTP proxy/CDN
may compress the response, so this header can describe transferred (encoded)
bytes instead of the actual decoded firmware. The installer now uses HEAD
only to check file availability; `tools/build_installer_archive.py` already
verifies each archived image's uncompressed 8 MiB size, ESP header and
release asset SHA-256 before publishing its manifest. The Pages build also
checks the latest merged image's size. Browser tests exercise a misleading
compressed HEAD size. No new firmware version is needed; browser USB flashing
still requires physical verification.

## Drive test scenes (2026-11-04)

The owner chose the look of the dock and open-water scenes on the options
page `tools/boat-studio/drive-test-scenes.html` (open it locally). The page has
choices for:
- colours and the boat's motion;
- how the lines, fenders and the figure-of-eight on each cleat appear, and in
  which order;
- waves and how they move;
- the quay layout;
- a speed for each animated part.

His choice is recorded in `docs/TODO.md` and in `tools/boat-studio/export_scenes.js`.

How it reaches the app:
- `node tools/boat-studio/export_scenes.js` (Playwright) runs the page's
  `exportApp()` with that choice. It writes the scene markup and timings into
  `firmware/ui/app.html` between `/*DTS*/` and `/*/DTS*/`, with the boat once
  and the waves left out. Then run `python3 tools/build_ui.py`.
- In the app, `dtFrame()` moves the boat and recomputes every line from the
  moved cleat in each frame. The lines therefore stay on the cleats and tighten
  or slacken with the boat. Then the knot fades in and the fenders pop.
- The dock scene alternates stern-to and alongside.
- With reduced motion it shows one still picture of the first scene.
- To change the look, change the page or the choice and export again. Never
  edit the generated data by hand.

The swim, stop, warm-up, lookout and lever scenes still have the older simple
style; the boat studio's notes on the owner's wishes (fenders, knots, ladder,
quays) are in `tools/boat-studio/README.md`.

## Drive test animation fix — 2026-11-03

[PR #131](https://github.com/Kaelor9/EngineLink/pull/131) was merged to
`main` after UI validation and ESP32-S3 compilation passed. During recording,
`dtRecord()` previously called `render(true)` for every ~1 Hz fast-sample
fetch, replacing the SVG and restarting its animation. It now calls
`dtPaint()` to update progress, countdown and live readings in place; the
second dock scene is hidden when reduced motion is enabled. **No version bump
or firmware release was made**. Verify on a real iPhone/board in the next
release. The owner's new visual scene style still needs approval before
replacing the old `scene*` artwork.

## Field observations — 2026-11-02 (v0.13.4)

Source: [#116](https://github.com/Kaelor9/EngineLink/issues/116) (v0.13.3), [#125](https://github.com/Kaelor9/EngineLink/issues/125) and [#126](https://github.com/Kaelor9/EngineLink/issues/126) (v0.13.4). These are actual board measurements.

- **Router configuration should remain default**: Zyxel 2.4 GHz, channel Auto, 20/40 MHz bandwidth, mixed WPA3-SAE/WPA2-PSK. An ESP32 station must follow the router channel, not change it. Channel 6 appears in #125 and channel 11 in #126; their timing/cause is unconfirmed.
- **Throughput improved**: #116's 12 tests averaged 83.0 kB/s (14.8–288.3 kB/s, round trip 44–259 ms). #126's latest 12 tests averaged 584.6 kB/s (457.8–665.3 kB/s, round trip 18–20 ms), about 7x faster in these tests.
- **Still not a fully stable connection**: #126 after 4,833 s uptime had 95 disconnect events, 48 pre-connection failures, one dropped active router connection, and approximately 50 seconds of repeated reason 201 (router not found) and reason 36 (station left during mode change) before reconnecting around uptime 4,753 s. One automatic beacon-loss transmit-power recovery to 15 dBm was logged. Do not attribute this definitively to the router changing channel.
- **Diagnostic gaps**: automatic channel survey/link tuning did not complete in #125/#126, likely because active app use defers tests. #125 shows a mismatch between selected TX level (11 dBm) and driver-reported output (20 dBm); investigate rather than assume the selected level was actually applied. #126 includes a crash history and reset-reason 4; the report does not establish a new crash during its 80-minute session.
- **Export problem**: Chrome intermittently fails to download a ~1 KB Trip summary CSV, while a ~104 KB second-by-second CSV downloaded successfully. [PR #123](https://github.com/Kaelor9/EngineLink/pull/123) proposes a normal known-length response instead of chunked/deflate. Confirm merge, release and field results before claiming fixed.

**Next priorities:** Preserve the v0.13.4 speed gain, investigate the repeated router reconnect attempts with Auto channel, improve safe survey visibility, verify sustained performance and crashes, and validate the tiny-CSV fix in Chrome/iOS. Do not demand router configuration changes from users.
