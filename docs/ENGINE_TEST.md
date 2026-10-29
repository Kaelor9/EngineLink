# EngineLink — Passive engine calibration and J1708 capture

Applies to **EngineLink 0.4.0-alpha**. The test collects *actual* J1708 data and independent reference observations. It never commands or operates an engine.

> **Volvo Penta KAD300 / EDC I:** these engines answer only when asked. Follow the step-by-step procedure in [KAD300_EDC1.md](KAD300_EDC1.md#test-procedure-on-the-boat) first: listen, voltage check, RPM check. Then start polling on the Data page *before* you start an engine test recording, so the requests and answers end up in the test CSV.

## Safety prerequisites

Before connecting a real engine bus, confirm both isolated 5 V supply rails are within their expected range **relative to each rail's own isolated ground**. One earlier diagnostic reading of approximately **0.98 V** on an isolated domain has not been documented as resolved.

Keep the **SJ1 and SJ2** J1708 transmit jumpers open for pure listening. For KAD polling, close only the jumper of the connector you poll (SJ1 = J7, SJ2 = J6). Firmware attaches a TX pin only while polling runs on that connector, and polling is off after every restart. Do not connect anything to the NMEA 2000 bus until its isolated transceiver and bus behavior are verified.

Run engines only under the manufacturer's operating procedures, with the vessel safely secured or underway in suitable conditions. Do not attempt to alter engine controls from EngineLink.

## Capture workflow

1. Insert a microSD card; confirm the **SD card** tile on the **Device** page shows it is recording or inserted.
2. Open **Data → Engine Test & Calibration** (step 1 of the guided flow) and select **Both connectors**, **J7**, or **J6**. Before testing, check the **Bus discovery** table: it lists every PID the ECU sends, which tells you whether PID 183 (fuel rate) is present at all.
3. Select **Start recording**. The unit pauses its ordinary CSV stream while the test capture uses the SD card, avoiding two simultaneous high-rate writers.
4. With the engine at idle, let the unit record for 30–60 seconds. The first automatic marker is **unlabelled**, with no reference values. Use **Mark stage** to label stable idle after you have an actual reference. The test may be started before the engine starts if useful.
5. For the next reference point, choose **Low / Medium / High RPM / Cruise / Custom**, enter an independent *observed* RPM and optional fuel-flow reference (L/h), then select **Mark stage**. The stage timer shows how long the current stage has been held. Hold stable for 30–60 seconds.
6. Repeat for as many reference points as needed. Stage markers never change engine RPM; they simply label incoming data.
7. Select **Stop & save**, then **Download CSV**. Safari should offer the CSV through its download/File handling; desktop browsers download it directly.
8. Retain the original file, do not overwrite or edit it before analysis. The last test file name persists in preferences, allowing download after a reboot if the SD card remains inserted.

A Wi-Fi outage does **not** stop on-device capture, but a power failure may lose unflushed data. Test rows are flushed periodically; stage markers are flushed immediately. The SD card must not be removed during recording.

## File contents

The test has a filename like \`TESTxxxxxxxx.CSV\`. Headers:

\`\`\`csv
kind,boot_id,uptime_ms,utc_epoch,step,phase,reference_rpm,reference_fuel_lph,channel,mid,length,checksum,raw_hex,decoded_fuel_lph,decoded_rpm
\`\`\`

- **MARK** records a chosen stage, reference RPM and optional reference fuel rate.
- **FRAME** records each received J1708 message, its physical channel, raw HEX bytes, checksum result, optional decoded standard parameters and current manual references.
- **utc_epoch=0** means the unit did not have confirmed wall-clock time. \`uptime_ms\` remains useful for aligning samples within the boot session.
- **reference_rpm=-1** and **reference_fuel_lph=-1** mean the operator did not supply those reference measurements; they are *not* real negative physical readings.
- **checksum=0** means the captured data failed J1708 checksum validation. Framing still needs verification; a checksum failure does not automatically prove an ECU problem.
- Captures may have gaps if \`uartFaults\`, \`drops\`, or SD write errors occur. Review hardware diagnostics together with raw capture.

## How we identify scaling constants

First confirm J1708 framing and detect the ECU's MIDs and consistent packet layouts. Use multiple engine states (idle + several distinct stable RPMs). Compare candidate raw fields with independently observed RPM values using known unit encodings; confirm endianness, signedness, length, PID identity, and offsets. Repeat with an independent reference for fuel flow if available. Do not infer a fuel-rate scaling factor solely from RPM correlation.

The application currently tries the *standard* J1587 page-0 representations:
- **PID 190** engine speed: unsigned 16-bit little-endian, **0.25 RPM/bit**.
- **PID 183** instantaneous fuel rate: unsigned 16-bit little-endian, **1/64 US gal/h per bit** = **3.785411784 / 64 L/h per bit**.

The existence of a standard PID does not mean your engine broadcasts it; nonstandard Volvo Penta data may require further identification. Raw original bytes are kept for this reason.

## Onboard status light

The light shows the most important thing at the moment (top of the table first). Colour and rhythm both carry the meaning, so it can be read in sunlight and by people who see red and green alike. Two colours that are easy to mix up (red and orange, blue and magenta) never follow each other in one pattern, and red never follows green. The same legend, with the pattern on now, is in the app under Settings → Status light; "Show every pattern on the board" plays each one for 3 seconds. The rules are checked by `tools/test_ledpatterns.cpp`.

| Light | Meaning | What to do |
|---|---|---|
| White, fast blink | Updating firmware | Keep the power on |
| Red, fast blink | Engine alarm (for example coolant too hot) | Look at Events in the app now |
| Long red, then short blue | SD card error: nothing is saved | Take out the card and put it back, or replace it |
| Three short red blinks, pause | Engine data fault (J1708 garbled or overflowing) | Check the engine connectors and cables |
| Two short orange blinks, pause | Engine warning (a value outside what is normal for that engine) | Look at Events in the app |
| Short blue, then long orange | No SD card | Everything works, but trips and raw data are not saved |
| Magenta, slow blink (1 s on, 1 s off) | Recording a motor test | – |
| Blue, breathing | Setup not finished | Join the EngineLink Wi-Fi and open the app |
| Long green, then short blue | Boat router not reached | EngineLink's own Wi-Fi still works |
| Green, dark for a moment every 2 s | Reading the engines, all is well | – |
| Green, steady | Ready, engines off or not being read | – |
| White, steady | Starting | – |
| Three quick magenta flashes | "Something's odd" mark was saved | – |

A J1708 fault clears by itself 30 seconds after the last one; the counters stay in Diagnostics. An engine alarm shows for 60 seconds and a warning for 10 minutes, or until it is marked as seen in the app.

## Wi-Fi troubleshooting

The default SoftAP uses network **EngineLink**, initial password **12345678**, at **http://192.168.4.1**. Internet is not needed, and a SIM router is never a requirement. Change the shared factory password once installed.

If Safari disconnects, use **Device** to compare \`Wi-Fi client disconnects\`, \`AP starts/stops\`, \`router disconnects\`, \`reset reason\`, and \`uptime\`. A reset indicates possible power/watchdog issues; AP station leave codes implicate wireless association; a page API timeout without an AP event suggests HTTP/backpressure instead. No single symptom alone proves the cause.

AP-only mode does not call \`WiFi.disconnect\` or perform recurring router scans. When router mode is configured, automatic router reconnects are suppressed while a client is on EngineLink's SoftAP; AP+STA still shares one 2.4 GHz radio, so a first router association can temporarily change the AP channel.

## Known limitations

This alpha firmware is **not hardware-validated on the target PCB as a complete system**. EngineLink currently does not guarantee zero missed frames, calibrated fuel accounting, automatic journey grouping across power cycles, remote engine commands, or NMEA 2000 bus interoperability. Its engine test is a capture/analysis tool, not an automatic auto-calibrator.
