# EngineLink — project plan

## Product direction

**EngineLink** — Marine Precision. The goal is accurate, trustworthy fuel consumption for two separate engines. J1708 diagnostics, logging, optional NMEA 2000 conversion and hardware monitoring serve this primary purpose.

## First-time setup

1. Factory state: EngineLink creates SoftAP named `EngineLink`; initial development password `12345678` and local address `http://192.168.4.1`.
2. Connection choice: **EngineLink Wi-Fi** or **Connect to a router** — visually equal choices, no recommendation badge. A router and internet must never be necessary for core operation.
3. Engine assignment: physically independent **J7 / Channel 1** and **J6 / Channel 2** mapped to Port/Starboard or unassigned; editable later without changing past logs.
4. Date/time: automatic or chosen source (phone browser time, NMEA 2000 GPS time, NTP if available). No RTC is installed on the current board.
5. Review and finish. Store settings, then open **Fuel Overview**. Settings remain changeable.

Network changes should keep a safe direct-access fallback; the ESP32-S3's AP and STA share a radio and channel. Use authentication for settings and avoid predictable credentials on deployed products.

## User interface

- **Fuel Overview**: total fuel flow (L/h), per-engine fuel, L/nm (only with valid SOG), and fuel history. Distinguish zero, unknown, simulated and measured data.
- **Traffic**: calm packet inspector for J1708/J1587, history, time stamps, RX/TX and decoded data; no fast animated packets.
- **History**: logged motor-running intervals and optional journey groups; never auto-merge two trips just because engine stops are close together.
- **Device**: LED, SD, CAN, PSRAM, optoisolated input, Wi-Fi, time status and engine mapping.
- **Settings → Advanced → J1708 Terminal**: one channel selector (J7 or J6); RX/TX monitor above HEX composer; inline **Having trouble writing? ℹ️** help. Physical TX must be disabled by default and bench-only.

## Fuel and history safety

- Verify fuel-related messages and scaling on actual engines. A standard PID's existence does not prove the ECU transmits it.
- Keep original bytes, physical channel and channel-to-engine mapping as captured.
- Distinguish reported, calculated and estimated fuel values, including source and accuracy.
- Missing ECU data must never silently become 0 L/h.
- Save each period of confirmed engine operation independently; motor off is not the end of a boat journey.
- Allow later correction of groups without changing raw evidence.
- Use monotonically increasing timers for durations and synchronization-derived UTC for wall time. Unknown dates stay unknown.

## Development gates

1. Freeze UX design and product architecture.
2. Implement passive J1708 receipt with buffered microSD log.
3. Identify and validate real ECU fuel consumption bytes.
4. Validate NMEA 2000/CAN hardware and optional speed input independently.
5. Make firmware builds reproducible and verify startup defaults: no bus TX, stable LED, safe SD, network recovery.
6. Only then publish the **merged** ESP32-S3 firmware and activate the browser installer.

**Status: specification/design. None of the above new product functionality should be represented as shipping today.**
