# EngineLink application v0.3.0-alpha

This is a **working application foundation**, not a validated production release. The former Lab firmware is retained under \`experiments/\`. The application is in \`firmware/EngineLink/\`.

## Changes in 0.3.0-alpha

- **Safari stability:** the stock ESP32 WebServer waits up to 5 s on a TCP connection that never sends a request, and serves nobody else in the meantime. Safari opens such speculative connections, which is a likely cause of "network connection was lost". EngineLink now drops idle sockets after 600 ms (`EngineLinkWebServer`).
- **Wi-Fi firmware update** (Settings → Firmware update) using the `enginelink-update.bin` application image published on the download page. The image is checked for the ESP32 app descriptor; the merged USB image is rejected. Files and totals are closed/saved first.
- **Persistent totals** per physical channel in NVS: trip fuel, lifetime fuel, engine hours and trip engine time. Counted only from valid ECU samples ≤5 s apart. Saved at most every 5 minutes, at the end of each engine run and before deliberate restarts, so up to 5 minutes can be lost on sudden power loss.
- **Engine-run segments:** fresh RPM ≥300 starts a run; it ends 10 s after RPM stops or goes stale. Runs are listed in History and appended to `SEGMENTS.CSV`. Runs are never merged into trips.
- **Bus discovery:** every page-0 J1587 PID seen per channel/MID with its latest raw bytes and count (`/api/pids`). The UI shows standard J1587 values labelled *unverified*.
- **Engine details** on the Fuel page (coolant, oil pressure, battery, load, boost, oil temperature, ECU engine hours and total fuel) when the ECU sends them, with standard J1587 scaling labelled *unverified*.
- **One-hour trend** of fuel flow and RPM (10 s samples).
- **mDNS:** `http://enginelink.local`. **Selectable Wi-Fi channel** (1/6/11).
- **Maintenance:** restart, re-read SD card, factory reset (typed confirmation).
- **New UI:** bottom tab bar on phones, health overview with plain-language explanations, guided engine test, decoded message details, prioritized alert bar with one-tap actions, confirmation dialogs.
- `ESP.getSketchSize()` (which verifies the whole image) is cached at boot instead of running on every status poll.
- Version defined once (`EL_VERSION`); the installer, manifest and artifacts read it automatically.

## Changes in 0.2.1-alpha

- J1708 frame backlog moved to PSRAM (2048 frames). A CSV download or SD flush blocks the web server loop; the old 72-frame queue could overflow in well under a second of bus traffic, dropping frames and triggering the red J1708 LED alarm.
- Frames up to 64 bytes are captured intact. J1708 limits messages to 21 bytes only while the vehicle moves; longer stationary/diagnostic messages were previously marked malformed.
- microSD hot-plug: card removal is detected in every state (not only while recording) and the card is unmounted; a re-inserted card mounts and resumes session logging automatically.
- A motor test that fails to start no longer leaves ordinary SD logging switched off.
- SD write failures at end of row are detected.
- Settings: a failed NMEA2000 start no longer leaves half-applied settings in memory. The Settings form is now filled correctly right after the setup wizard.
- NTP no longer overrides an active GPS time source in Automatic mode.
- The onboard UI is served with `Cache-Control: no-cache`, so Safari does not keep showing an old UI for an hour after a firmware update.
- Device page: reset reason shown as text (e.g. *Brownout*), UART line-error counter and frame backlog; the LED panel shows the real system state even while the LED demo runs.
- No status-poll SD lookups: test-file availability is cached.
- Version metadata aligned across firmware, installer, workflows and docs; RELEASING.md no longer describes the Lab build.

## What is implemented

- Native ESP32-S3 web app served **offline** at \`http://192.168.4.1\` on the EngineLink SoftAP.
- First-time wizard: direct EngineLink Wi-Fi vs. router, J7/J6 → port assignment, time-source preference. Both connection choices are presented neutrally.
- AP SSID \`EngineLink\`, initial development password \`12345678\`. A new password can be set in the wizard. **These shared default credentials are not suitable for deployment without changing them.**
- Router credentials saved using ESP32 Preferences; SoftAP remains on as a fallback. ESP32 AP+STA uses a shared 2.4 GHz radio and router connection may briefly change SoftAP channel.
- **RX-only** collection from J7 GPIO21 and J6 GPIO17, 9600 8N1. Firmware does **not configure** J7 TX GPIO47 or J6 TX GPIO18 and contains no J1708 transmit endpoint.
- UART RX timeout frame separation and 8-bit J1708 additive checksum checking. Physical framing and actual engine compatibility still need testing.
- SAE J1587 PID 190 (engine RPM, 0.25 rpm/bit), PID 183 (instantaneous fuel rate, 1/64 US gallon/hour per bit), only on valid full frames with supported page-0 PID layout. Unrecognized proprietary traffic remains raw. **Do not assume Volvo Penta ECUs send PID 183.**
- Display values as **unknown** (—) when missing or stale, not fake zeroes. Combined L/h requires fresh fuel values from both assigned channels.
- Fuel volume since current boot is integrated only over consecutive valid samples (gaps >5 seconds excluded); incomplete estimates are explicitly identified. It is **not** a permanent all-trip fuel total yet.
- microSD session CSV logging of raw J1708 bytes, physical channel, checksum result, MID, uptime and UTC if time is known. Auto-generated per-boot file IDs. Logging stops on card removal. Writes are periodically flushed; sudden power loss can lose unflushed records.
- Browser/phone UTC synchronization and optional NTP after connecting a router.
- Optional NMEA 2000 listen-only TWAI input at 250 kbit/s: PGN 126992 UTC and PGN 129026 speed over ground. **Physical CAN interface has not been validated.** Do not enable until verified. No NMEA transmission/ACK.
- Fixed LED GPIO48 patterns reflect actual status (setup, normal, Wi-Fi/router reconnect, missing SD, write fault, J1708 RX fault, passive motor test). The user color selector was intentionally removed. GPIO38, PSRAM/heap/network diagnostics, raw J1708 Packet Inspector, read-only advanced terminal with ℹ️ help.
- **Engine Test & Calibration**: passive J1708 capture with stable-RPM phase markers, optional independent reference RPM and fuel flow, SD CSV, download to phone or computer. [Test procedure](ENGINE_TEST.md).
- SoftAP stability changes: avoid AP-mode Wi-Fi.disconnect(), avoid frequent router reconnection scans with an AP client present, and serialize Safari polling to prevent overlapping status requests.
- Arduino build and embedded browser JavaScript syntax validation in GitHub Actions.

## Known gaps that must be resolved before any "production" claim

1. **Confirm both isolated 5 V power domains with a multimeter** against their respective isolated grounds. Earlier testing recorded ~0.98 V in one domain; resolution has not been confirmed.
2. Bench-validate UART framing across real J1708 traffic; J1708 protocol has inter-character and inter-message timing requirements. Receiving UART bytes does not prove full protocol conformance.
3. Verify actual Volvo Penta engine MID/PID identities and fuel-rate scaling; proprietary or extended formats must be decoded from real captures and validated independently.
4. Reliable boot-to-boot journey records, configurable grouping and preserved trip fuel accounting are not implemented. SD sessions are separate by design; no auto-merge.
5. Implement robust queue overflow/SD loss reporting under high-rate traffic; verify actual SD write performance and power-loss recovery.
6. Current advanced terminal is **read-only**. Bench-only transmit requires a separate electrical and SAE timing/arbitration safety review.
7. User/admin authorization has not been implemented. Anyone with the Wi-Fi password can currently change settings. Change the AP password from the shared factory value.
8. Test actual iPhone Safari connectivity, AP-only mode, AP+STA fallback and memory stability on the assembled PCB. An automated build cannot prove RF stability; review AP disconnect counters and reset reason under Device.
9. The optional NMEA2000 listener requires verifying ISO1042 power isolation, 250 kbit/s traffic, bus termination, and source correctness before connection.
10. A battery RTC is not fitted. The clock cannot know elapsed power-off time without a new time source; logs store uptime and boot ID when UTC is unavailable.

## Hardware connections

| Connector | Signal | Pin |
|---|---|---|
| J7 | J1708 RX | GPIO21 |
| J7 | J1708 TX | GPIO47 **not used** |
| J6 | J1708 RX | GPIO17 |
| J6 | J1708 TX | GPIO18 **not used** |
| CAN | CAN RX/TX | GPIO16/GPIO15; optional **listen-only** |
| microSD | CS/MOSI/SCK/MISO/Detect | GPIO10/11/12/13/14 |
| LED | SK6805 | GPIO48 |
| Optoisolated input | Digital | GPIO38 |

Set Arduino ESP32-S3 board to **8 MB flash, OPI PSRAM, USB CDC On Boot enabled**. Requires **Adafruit NeoPixel** library. The repo's GitHub Actions build exports a merged ESP32-S3 image for test flashing.

## How to try the application

**Do not connect to the real engine network until the physical power domains and passive electrical inputs are verified.** Test the AP, first-time wizard, LED and SD with the engine connections removed. Confirm firmware boot with USB Serial Monitor at 115200 baud and check \`http://192.168.4.1/api/health\`.

There are no synthetic motor values: Fuel Overview deliberately displays dashes until the UARTs receive valid compatible engine messages. That is expected, not a software error.

No manual trip start/finish is required to create raw sessions, but boat-journey grouping and durable fleet accounting are still pending.
