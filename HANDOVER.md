# EngineLink — Project Handover for Claude

> **Read this first.** This is a working handover of the product requirements, hardware, design decisions, implementation status, outstanding validation and priorities as of **26 October 2026**. It is for continuing development of the real EngineLink application, **not** the old laboratory simulator.
>
> **Communicate with the project owner in Danish.** The current on-device UI is predominantly English. Keep explanations straightforward, concrete and actionable. Do not quietly reinterpret or override decisions below.
>
> **Repository:** https://github.com/Kaelor9/EngineLink  
> **Firmware installer:** https://kaelor9.github.io/EngineLink/  
> **Current main firmware:** firmware/EngineLink/EngineLink.ino + firmware/EngineLink/ui.h  
> **Current app version:** 0.2.0-alpha (CI-compiled and published, **not validated end-to-end on the actual PCB**).

## 1. Mission — why this project exists

**EngineLink is primarily a dual-engine marine fuel-consumption monitoring system.** The main goal is to reliably acquire engine fuel information, decode it, show genuinely useful consumption and efficiency metrics, record them over time, and optionally make selected information available on the boat's NMEA 2000 network.

It is **not primarily** a J1708 gadget, a generic ESP32 demo, or a visually busy dashboard. Those are supporting capabilities. The most valuable outputs are:

- **Instantaneous fuel rate:** each engine and combined, L/h.
- **Fuel economy:** L/nm using reliable GPS speed-over-ground (knots), with safeguards at zero/very low speed.
- **Fuel used over a journey**, per engine and total.
- **Fuel-use and RPM trends**, historical comparisons, and identification of economical operating ranges (the boat's “sweet spot”).
- **Dependable logging** suitable for diagnosing unusual/undocumented ECU messages and calibrating actual readings.
- Eventually: additional engine data and NMEA 2000 output to supported marine displays.

Data integrity comes before appearance. If engine data are missing, stale, invalid, unverifiable or ECU-specific, display **unknown (—)** and explain why; **do not substitute 0 or simulated numbers**. Clearly distinguish *ECU-reported*, *calculated*, *estimated*, *demo* and *unavailable* values. Do not claim accuracy solely because a formula or standard PID exists.

The owner has discussed Volvo Penta engines, but **the exact ECU models, proprietary message layout, scaling constants and presence of standard fuel-rate PID 183 are not yet verified**. Do not fabricate them.

## 2. Product identity and user-experience requirements

- Product name is **EngineLink**. The old “KAELOR” branding has been explicitly removed from the product, UI and firmware.
- Design direction: **Marine Precision**. Professional marine-instrument aesthetics: deep navy / slate blue, restrained teal, crisp typography, consistent spacing, legible live numerical data, minimal visual noise.
- The owner rejected rapid animated packets / hyperactive diagrams. A packet inspector should show stable rows with pause, navigation, selection and details, **not packets flying around the screen**.
- The UI should feel polished from major layout choices down to micro-interactions, responsive for iPhone Safari and desktop, fully usable **offline** with all assets served by the ESP32.
- Use neutral language for options: **never label one setup choice “Recommended”, “Best”, or otherwise push the user toward it**. The factory default SoftAP is a technical bootstrap method, not a visually promoted choice.
- No requirement for the user to perform repetitive maintenance or manual trip management.
- Keep everyday fuel information simple; sophisticated diagnostics can live one level down.

### Main navigation

1. **Fuel Overview** — primary dashboard; both engines' consumption, combined L/h, L/nm and relevant trip metrics, with visible data quality.
2. **Traffic** — two-channel J1708 packet inspector, decoded versus raw HEX, timestamps, filters, pause/history, passive ECU capture tests.
3. **History** — raw captures, engine-run segments, fuel used and eventual journey grouping and analytics.
4. **Device** — LED status, Wi-Fi/UTC/SD/PSRAM/heap/GPIO38/CAN/hardware diagnostics.
5. **Settings** — network, engine mapping, time preferences and Advanced settings.

Under **Settings → Advanced → J1708 Terminal**: ONE channel selector (Channel 1/J7 or Channel 2/J6), with serial-monitor-style RX/TX output on top and HEX composer underneath; **not four separate text boxes**. Include help via **“Having trouble writing? ℹ️”** covering frame format, MID/PID/checksum and only verified examples. Current terminal is deliberately **read-only**. Any future transmit capability must be opt-in bench-only, electrically safe, and conformant to J1708 arbitration/timing.

## 3. Onboarding, networking and recoverability

**Factory state:** create EngineLink's own Wi-Fi access point (SoftAP) automatically.

- SSID: **EngineLink**
- Current development/factory password: **12345678** (WPA2 password length requires ≥8 characters). This shared password is for current development; future products need unique/secure provisioning and settings authorization.
- Direct web address: **http://192.168.4.1**
- **No SIM router, internet, phone application or cellular coverage is required for onboard operation.**

On the first browser visit, show a short onboarding wizard, *only until the device is configured*:

1. **EngineLink Wi-Fi** (its own AP) **or** **Connect to a router** (STA). Display these as equally valid choices.
2. **Engine mapping:** assign physical J7/Channel 1 and J6/Channel 2 to port/starboard or leave unassigned. Provide an easy “swap” capability and optional passive identify-channel activity view. Never assume J7=port on every installation.
3. **Time source:** automatic or user-selected source (phone/browser, NMEA 2000 GPS, NTP, etc.).
4. Save and open Fuel Overview. All settings must be changeable later.

Router connectivity is optional and must not replace offline autonomy. EngineLink may run AP and STA together, but ESP32-S3 shares a **single 2.4 GHz Wi-Fi radio** and AP/STA channel. Preserve recovery access through SoftAP even if the router disappears. Network configuration changes should be safely committed and recoverable; do not strand the user on a disconnected network.

### Important real-world issue: Safari disconnects

The user repeatedly reported Safari showing **“Safari can't open the page because the network connection was lost”**, particularly while navigating and changing controls. This is **not known to be conclusively fixed**.

Current 0.2.0 changes include serializing browser polls, timeouts/backoff, fewer requests, avoiding unnecessary AP restarts and avoiding repeated STA reconnection while an AP client is present. Status exposes AP joins/leaves, AP start/stop, STA disconnects, reset reason and uptime. **CI success does not prove RF stability.**

Prioritize actual iPhone testing and investigate separately:
- Was the ESP32 rebooting (uptime/reset reason)?
- Was SoftAP shutting down or reconfiguring?
- Did the iPhone disassociate due to an AP with no internet, channel changes, weak signal or captive-network behavior?
- Were synchronous WebServer requests starved by SD writes, large JSON/HTML, or concurrent HTTP requests?
- Are there memory fragmentation, stack, UART event or power-supply issues?

Fix underlying causes; do not merely hide a lost connection with an optimistic “Connected” label. Do not let Wi-Fi failures interrupt engine data capture.

## 4. Time and date with no RTC

The current custom PCB has **no RTC** and no convenient external 3.3 V / 5 V headers; do **not** assume an RTC retrofit will be installed. The device must continue measuring and logging fuel without internet, GPS or a valid wall-clock.

Support combined time sources, user-selectable or automatic:
- **Phone browser** transfers its existing clock using Date.now() over the locally hosted EngineLink webpage, also when the phone has no internet.
- **NMEA 2000 / GPS time**, when actually available.
- **NTP via a connected router**, only where connectivity/internet exists.
- Internal **monotonic uptime** within each boot, with a unique boot ID, if UTC is unavailable.

Store UTC when known, source/quality metadata, and boot-relative timestamps. A future synchronization can timestamp older records **within the same continuous boot** if sufficient timing evidence exists. Across a complete power loss, with no RTC/external timestamp, elapsed off-time and absolute date cannot be reconstructed reliably. **Do not invent dates**. Durations and volume integration should use monotonic timers, not a wall clock that can jump during synchronization.

Use a sensible source priority and allow the user to override the chosen source. GPS/NMEA time must not be overwritten blindly by a dubious phone clock.

## 5. Journey, engine-run and fuel-history semantics

This is a major product requirement and the user is **not yet completely satisfied** with previous trip-grouping proposals.

Real boat usage: engines can be turned off several times during **one** outing while anchored/swimming; conversely two different outings can happen close together on the same day.

**Absolutely do not treat each engine shutdown as end-of-trip.**
**Do not automatically merge two separate trips just because they are near each other in time.**
**Do not require users to remember to press Start, Stop, Continue or Finish on each trip.**

Preferred direction:
- Always log raw fuel measurements and **individual engine-running segments** automatically and independently.
- Keep **boat journey groupings** separate and ideally infer them later from sufficient evidence (GPS positions/home port, known journey context, etc.), without corrupting original segments.
- If evidence is missing, keep sessions separate or ungrouped, not confidently misclassified.
- Offer optional, effortless editing/grouping *after the fact*; do not nag during normal boating.
- Preserve historical channel-to-engine association even if mapping changes later.
- Handle power loss with bounded write loss, crash-safe/session-safe records and recovery.
- Never count “no communication” as zero fuel. Separately record data gaps, missingness and estimated segments.

**Current v0.2 only integrates between sufficiently recent valid fuel-rate readings since the current boot; it does not yet implement a durable, multi-boot journey engine.** This is a future architectural milestone, not a finished feature.

## 6. Custom PCB and verified hardware facts

Main MCU: **ESP32-S3-WROOM-1-N8R8**, 8 MB flash + 8 MB OPI PSRAM. Native USB CDC (GPIO19 D−, GPIO20 D+). Arduino IDE development uses ESP32S3 Dev Module / matching 8 MB flash, OPI PSRAM, USB CDC on boot and 115200 baud serial.

| Module | Physical / ESP32 pin(s) | Important detail |
| --- | --- | --- |
| J1708 Channel 1 | J7, RX GPIO21, TX GPIO47 | Isolated MAX3444E + ADuM; physical TX jumper **SJ1** |
| J1708 Channel 2 | J6, RX GPIO17, TX GPIO18 | Isolated MAX3444E + ADuM; physical TX jumper **SJ2** |
| Isolated NMEA 2000 CAN | TX GPIO15, RX GPIO16 | ISO1042; physical bus not fully validated |
| microSD SPI | CS GPIO10, MOSI GPIO11, SCK GPIO12, MISO GPIO13 | Card detect GPIO14; **LOW = inserted** |
| Addressable LED | GPIO48 | SK6805-EC15, 5 V supplied through logic-level shifter |
| Optoisolated input | GPIO38 | Digital input status |
| I²C (unused currently) | SDA GPIO8, SCL GPIO9 | No onboard RTC |
| Native USB | D− GPIO19, D+ GPIO20 | USB CDC |

Previous isolated bench observations:
- LED operation verified.
- microSD detection, read, write and readback verified.
- J1708 isolated channels exchanged **36,000 frames in 30 minutes without recorded UART test errors**, but that proves only the tested bench path, **not** full SAE J1708 multi-node arbitration or actual live ECU compatibility.
- Both isolated supply domains must be measured against **their respective isolated grounds**. A historical result on one was **about 0.98 V**, while another was near 4.975 V. **The low reading is not explicitly confirmed resolved.** Expect nominal **4.75–5.25 V**. Treat verification as a safety gate before connecting engines.
- Bench loopback uses J7 pin 1/B ↔ J6 pin 1/B and J7 pin 2/A ↔ J6 pin 2/A. **Never bridge the two isolated grounds.**
- SJ1/SJ2 may be closed only for controlled bench transmission; **OPEN before connecting live engine networks**.
- Do **not** enable J1708 transmission or live CAN communication before their physical and protocol requirements are independently validated.

Refer to docs/HARDWARE.md and the original schematic for actual electrical details. Do not infer accessible power breakout pads merely from an unused I²C pair.

## 7. The actual firmware today — v0.2.0-alpha

**Source of truth is current code**, not earlier Lab screenshots or outdated descriptions.

- firmware/EngineLink/EngineLink.ino — device logic, AP/STA, UART reception, J1587 parsing, time, microSD, HTTP API, LED status and passive calibration capture.
- firmware/EngineLink/ui.h — self-contained HTML/CSS/JavaScript delivered from flash (no CDN; responsive SPA-like UI).
- experiments/ — *historical hardware Lab* sketches. Do not use as the primary application or suggest flashing a Lab binary when asked for the real app.
- site/ — separate GitHub Pages firmware installer, not the ESP32-hosted dashboard.
- .github/workflows/build-engine.yml — Arduino ESP32-S3 CI compile + embedded JS syntax check.
- .github/workflows/pages.yml — builds ESP32-S3 merged binary and publishes installer from site/ to GitHub Pages.
- docs/ENGINE_TEST.md — current capture procedure.
- docs/PRODUCTION_ALPHA.md — what works versus what is unverified.
- docs/PROJECT_PLAN.md — product requirements (some *development status* descriptions are stale).
- docs/RELEASING.md — contains **older Lab-specific release text/manifest** and needs correction. Check current site/app.js and Pages workflow to determine actual published version.
- Build CI may use older artifact names even after updating firmware; align naming and version metadata.

### Current application behavior

- SoftAP **EngineLink**, initial password **12345678**, **192.168.4.1**. Optional router mode; client/server share one radio.
- First-run setup wizard with network choice, physical engine mapping and time preference; persistent Preferences.
- Dual J1708 receive-only UART capture, 9600 8N1, packet framing based on UART receive timeout, 8-bit additive checksum verification, recent-traffic inspector.
- Basic standard J1587 parsing: **PID 190** engine speed = 0.25 RPM per bit; **PID 183** instantaneous fuel rate = 1/64 US gallon per hour per bit (convert to L/h). **ECU support and implementation specifics require verification on actual motor captures**. No production-level proprietary decoding.
- Dual engine L/h and combined fuel rate only if data are valid and fresh. Unknown values stay unknown.
- Session CSV raw logging to microSD with boot ID, monotonic timestamp, optional UTC, physical channel, MID, checksum and raw bytes.
- Passive NMEA 2000 controller path for PGN 126992 (time) and PGN 129026 (SOG), disabled unless opted in; isolated CAN hardware is not yet validated. No CAN transmission.
- Phone/browser-based UTC synchronization and optional router NTP.
- Device diagnostics: heap/PSRAM, GPIO38, Wi-Fi events, UART errors, clock/SD/CAN.
- Read-only Advanced J1708 Terminal and HEX inspection/help.
- **Fixed semantic status LED (no manual color picker)**.
- **Engine Test & Calibration** records raw J1708 for chosen J7, J6 or both, accepts operator reference RPM / optional reference L/h and stage markers, saves a separate CSV to microSD and exports it via browser. An RF disconnect should not stop recording while the device stays powered. See docs/ENGINE_TEST.md.
- Hardware tests / data integrity / Safari reliability still need physical verification.

### LED state definitions (do not reintroduce arbitrary color selection)

| Meaning | Fixed signal |
| --- | --- |
| Normal | Steady green |
| Initial setup | Blue breathing |
| Router reconnecting | Amber pulse |
| SD unavailable | Amber double flash |
| SD write/mount error | Red rapid flash |
| J1708 queue/UART fault | Red double flash (time-limited; counters persist) |
| Motor calibration capture | Cyan pulse |

There is a **Test all LED signals** action that briefly cycles patterns and returns to automatic operation. Error/warning states must be clearly prioritized; a test must never conceal a serious safety-critical alarm unnoticed.

### Engine test capture semantics

The test records both **MARK** and **FRAME** records, including a step label (idle, low, medium, high, cruise or custom), reference RPM and optional independent fuel-rate reference. The CSV preserves original bytes, checksums, channel and timing. Unknown reference values are **-1 sentinels in current CSV**; do not interpret them as real RPM/fuel.

A proper calibration study needs several known, stable engine states; check MID, PID, offset, endianess, signedness and scale. Fuel-flow calibration needs a *real independent fuel reference*; RPM correlation alone is not proof of fuel flow.

The alpha recording path still uses synchronous SD operations; check buffering and worst-case timing when receiving real traffic. A Wi-Fi interruption must not discard or silently stop a test.

### HTTP interface currently in code

- GET / : onboard HTML UI
- GET /api/state : live status, channels, raw recent packets, test state, LED status
- GET /api/health : basic HTTP health
- POST /api/setup, /api/settings : onboarding/configuration
- POST /api/phone-time : receive browser UTC clock
- POST /api/led/test : run system LED demonstration
- POST /api/test/start, /api/test/mark, /api/test/stop : passive motor calibration capture
- GET /api/test/download : export last saved test CSV
- POST /api/sd/start, /api/sd/stop, /api/sd/remount : ordinary raw logging
- GET /api/log : export session CSV

Check code before adding/changing endpoint semantics; current authentication is inadequate for production use.

## 8. Safety and engineering principles

1. **J1708 is not an ASCII command shell.** It is a binary datalink; J1587 defines message identifiers and parameters. TX requires correct SAE J1708 timing, contention/backoff and electrical behavior. A UART loopback success does not mean transmission is safe.
2. **Default is RX-only.** Do not enable J1708 TX GPIOs, SJ1/SJ2 or CAN TX automatically. Treat Advanced terminal transmit as a future bench-only feature behind intentional safety steps.
3. **Verify real scaling.** Raw-frame captures take precedence over an assumed Volvo decoder. Any calibration must show evidence and uncertainty.
4. **No phantom fuel figures**; never turn missing data into zero, silently interpolate long gaps, or claim precise voyage totals from incomplete data.
5. **Offline-first.** Fuel capture, clocks relative to boot, microSD logging and diagnostics must operate with no connected phone/router.
6. **No undocumented auto-merging of journeys** or annoying manual trip start/stop prompts.
7. **Keep historical identity.** Log the physical J1708 channel and configuration version/engine mapping at time of capture.
8. **Zero internet dependency in the device UI.** The GitHub Pages installer may load ESP Web Tools online, but the onboard UI must be bundled.
9. **Observability and recovery matter.** Wi-Fi/AP disconnect counters, reset reason, UART queue/drop metrics, SD flush errors and last-good timestamps should be inspectable.
10. **Differentiate validated, implemented and planned.** Passing GitHub CI only proves a successful build and static JS syntax; it is not end-to-end hardware qualification.

## 9. Development priorities for Claude

### P0 — verify and stabilize current alpha BEFORE expanding features

- Read current firmware and UI end to end; inspect memory, service loop, UART event framing and WebServer/SD interactions.
- Run GitHub Actions and fix actual compile warnings/errors; ensure latest build is the same version installed by site/manifest.json.
- Diagnose and reproduce Safari / SoftAP disconnects on the real board; collect AP event counters, resets, SD activity and heap. Fix root cause; test AP-only and AP+STA separately.
- Validate file capture/export **end to end**: start → sustained incoming frames → label stages → stop → downloaded CSV; inspect number of columns, timestamps, checksum fields, no truncation/duplication, error handling and power failure.
- Test LED priorities, reset/boot state and status patterns on the real addressable LED.
- Verify both isolated supplies before any live ECU connection. Keep UART TX disabled.
- Add deterministic tests or fixtures for J1587 parsing and any inferred scale. Do not make bold fuel-calibration claims without evidence.
- Make logs resilient: buffering, bounded memory, nonblocking where possible, coordinated SD writes and explicit loss indicators.

### P1 — deliver genuine fuel-monitoring value

- Run a safe passive motor test and obtain representative actual ECU capture logs.
- Identify reliable RPM and fuel-rate messages, including manufacturer-specific encodings when needed; validate against independent instruments.
- Add explicit per-engine data quality, freshness, source, decoder version and missingness on Fuel Overview.
- Improve fuel integration and robust storage across boots, with replay/checkpoint recovery.
- Design engine-on segments and optional journey grouping without mandatory user intervention and without wrongful merging.
- Implement speed-based L/nm when reliable SOG exists and avoid low-speed division artifacts.

### P2 — expand toward product-ready functionality

- Validate isolated NMEA 2000 transceiver and listen-only operation, then consider correct PGN broadcasting, address claim and compliant engine-instance mapping.
- Improve settings/security: individual AP credentials, authorization for sensitive routes, rollback/recovery from failed router changes.
- Advanced (bench-only) J1708 transmit terminal if there is an actual use case and validated SAE arbitration/electrical safety.
- Structured historical graphs, data export/analysis and polished desktop/mobile UX.
- Clean up stale docs/build artifact labels, implement releases, changelog, automated tests and a safe update strategy.

## 10. Definition of done / quality bar

The owner values a solution that **actually works**, not a screenshot that appears finished. Before calling a feature reliable:

- The real ESP32 board has been tested (not merely CI-compiled).
- Phone Safari can navigate repeatedly, leave/rejoin the AP and recover cleanly without interrupting logging.
- Real test captures survive screen closure / Wi-Fi loss, and can be downloaded and parsed into valid CSV.
- Missing/stale data and errors appear explicitly, rather than silently disappearing.
- No unexpected transmission occurs on either engine J1708 bus.
- The installer, README and firmware version agree.
- The UI remains quiet, streamlined, touch-friendly and consistent with **Marine Precision**.

## 11. How Claude should continue

1. Clone/open https://github.com/Kaelor9/EngineLink and review **firmware/EngineLink/EngineLink.ino**, **firmware/EngineLink/ui.h**, **docs/HARDWARE.md**, **docs/ENGINE_TEST.md**, and CI workflow files.
2. Treat this handover as **requirements and context**, not proof that all existing code works. Check current source and test outcomes.
3. Keep changes versioned, documented, reproducible and focused on user-observed problems first.
4. Do not revert to the old Lab firmware or re-add features the user rejected (KAELOR branding, “Recommended” setup badges, fast-moving graphics, arbitrary LED color picker, forced trip management).
5. When unsure about engine-specific data, request a **real recorded J1708 CSV and independent reference values**, rather than inventing PID definitions or scaling.
6. Reply to the owner **in Danish**, explaining concrete edits, testing evidence and any remaining limitations.

---

### One-sentence brief

**Build EngineLink into a robust, beautiful, self-contained ESP32-S3 marine fuel-monitoring appliance for two J1708 engines, with verifiable ECU fuel measurements, trustworthy offline logging/history, optional NMEA 2000 integration, stable SoftAP-first connectivity, automatic time acquisition, a fixed meaningful status LED and advanced passive diagnostic tools.**
