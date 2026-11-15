# EngineLink ASYNC LAB v3.1 — OTA recovery and WebSocket reliability

Independent diagnostic firmware for ESP32-S3-WROOM-1-N8R8 (8 MiB flash, 8 MiB OPI PSRAM).
Pinned Arduino ESP32 core 3.3.12, Async TCP 3.5.0 and ESP Async WebServer 3.12.1.

## Reason for v3.1

V2 repeatedly reported successful app-only OTA to v3, but v2 reappeared after restart.
The previous v3 required multiple JSON/status events to approve its first boot
after 60 seconds. Without those events, an OTA-pending image could be rolled
back on reboot or after a health timeout. A separate v3 boot crash remains
possible and needs physical UART logs for definitive diagnosis.

v3.1 accepts a REAL HTTP page/health/status request or WebSocket handshake
after 8 seconds of AP and HTTP initialization. A JSON export is unnecessary.
If no external client connects, it attempts rollback after 180 seconds
where ESP-IDF bootloader OTA rollback is enabled. This protection is not
validated on physical EngineLink hardware.

## Status and service URLs

Wi-Fi AP: EngineLink-ChatGPT, password 12345678, IP 192.168.4.1.

- /lab — dashboard with automatic WebSocket updates every second.
- /lab/health — PLAIN TEXT: running firmware version, AP status, uptime,
  OTA pending/confirmed state, and free heap; no JSON or JavaScript needed.
- /lab/status — optional JSON; dashboard shows an informative error rather
  than merely Unexpected token < if HTML is returned instead of JSON.
- /lab/report — downloadable structured diagnostic report.
- /ws — live telemetry WebSocket with reconnect.
- /api/speedtest?kb=512 — asynchronous streaming benchmark, also 64/2048.
- /api/rtt — lightweight HTTP latency check.
- /original — original UI appearance reference, not the real engine backend.
- /lab/ota — raw app-only OTA using file selection and one confirmation dialog.

## OTA and recovery warning

A successful build does NOT prove successful boot on real hardware.
Do NOT flash a remotely accessible device without a physical USB recovery
path or other independently verified fallback.

A 3.2 MB maximum and ESP32 app header/descriptor compatibility check are
enforced. The generic Arduino application project name is NOT a signature.
Use only trusted images from this repository. Firmware write errors abort.
The ESP32 must reboot after a verified Update.end() succeeds. Bootloader
rollback is a best-effort safeguard, not a guarantee.

The normal EngineLink, v1, v2, and previous v3 variants remain preserved;
no production motor/J1708/CAN pins are ever configured by this test.

## Validation procedure

Keep the same board supply, Windows PC, distance, radio conditions and AP
placement. Compare 30 ICMP pings, five 512 KiB HTTP tests, ten HTTP RTT
requests, Wi-Fi RSSI, WebSocket reconnects and heap. Record bench/OTA
verification in docs/test-evidence/chatgpt-async-lab-v3-1/.

References: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/ota.html
and https://github.com/ESP32Async/ESPAsyncWebServer.
