# EngineLink — Passive engine calibration and J1708 capture

Applies to **EngineLink 0.2.1-alpha**. The test is designed to collect *actual* J1708/J1587 data and independent reference observations. It does not send commands or operate an engine.

## Safety prerequisites

Before connecting a real engine bus, confirm both isolated 5 V supply rails are within their expected range **relative to each rail's own isolated ground**. One earlier diagnostic reading of approximately **0.98 V** on an isolated domain has not been documented as resolved.

Open the **SJ1 and SJ2** J1708 transmit jumpers. EngineLink 0.2 has no J1708 TX implementation, but physical jumpers should still be opened. Do not connect anything to the NMEA 2000 bus until its isolated transceiver and bus behavior are verified.

Run engines only under the manufacturer's operating procedures, with the vessel safely secured or underway in suitable conditions. Do not attempt to alter engine controls from EngineLink.

## Capture workflow

1. Insert a microSD card; confirm **Device → SD status** indicates that it is mounted.
2. In **Traffic → Engine Test & Calibration**, select **Both channels**, **J7**, or **J6**.
3. Select **Start recording**. The unit pauses its ordinary CSV stream while the test capture uses the SD card, avoiding two simultaneous high-rate writers.
4. With the engine at idle, let the unit record for 30–60 seconds. The first automatic marker is **unlabelled**, with no reference values. Use **Mark current stage** to label stable idle after you have an actual reference. The test may be started before the engine starts if useful.
5. For the next reference point, choose **Low / Medium / High RPM / Cruise / Custom**, enter an independent *observed* RPM and optional fuel-flow reference (L/h), then select **Mark current stage**. Hold stable for 30–60 seconds.
6. Repeat for as many reference points as needed. Stage markers never change engine RPM; they simply label incoming data.
7. Select **Stop & save**, then **Download test CSV**. Safari should offer the CSV through its download/File handling; desktop browsers download it directly.
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

## Fixed onboard LED behavior

The previous free color picker has been removed; the LED is controlled by actual system state.

| State | Fixed LED |
|---|---|
| First setup | Blue breathing |
| Working, no higher-priority issues | Steady green |
| Router reconnecting, if router mode selected | Amber single pulse |
| SD card unavailable | Amber double flash |
| SD mount/write failure | Rapid red flash |
| J1708 UART queue overflow | Red double flash |
| Motor test recording | Cyan pulse |

The **Test all LED signals** button shows a 16-second diagnostic sequence, then returns to actual system indication. A J1708 overflow indication automatically clears after 30 seconds without a new overflow; historical counters remain visible.

## Wi-Fi troubleshooting

The default SoftAP uses network **EngineLink**, initial password **12345678**, at **http://192.168.4.1**. Internet is not needed, and a SIM router is never a requirement. Change the shared factory password once installed.

If Safari disconnects, use **Device** to compare \`Wi-Fi client disconnects\`, \`AP starts/stops\`, \`router disconnects\`, \`reset reason\`, and \`uptime\`. A reset indicates possible power/watchdog issues; AP station leave codes implicate wireless association; a page API timeout without an AP event suggests HTTP/backpressure instead. No single symptom alone proves the cause.

AP-only mode does not call \`WiFi.disconnect\` or perform recurring router scans. When router mode is configured, automatic router reconnects are suppressed while a client is on EngineLink's SoftAP; AP+STA still shares one 2.4 GHz radio, so a first router association can temporarily change the AP channel.

## Known limitations

This alpha firmware is **not hardware-validated on the target PCB as a complete system**. EngineLink currently does not guarantee zero missed frames, calibrated fuel accounting, automatic journey grouping across power cycles, remote engine commands, or NMEA 2000 bus interoperability. Its engine test is a capture/analysis tool, not an automatic auto-calibrator.
