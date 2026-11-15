# EngineLink independent Wi-Fi LAB v2 — evidence

Build branch: `experiment/enginelink-wifi-lab-v2`  
Target: ESP32-S3-WROOM-1-N8R8, 8 MB flash, 8 MB OPI PSRAM  
Version: `0.0.2-chatgpt-wifi-lab`

## Reference observations (v1, 2026-11-15)

| Test | Observed |
|---|---|
| 512 KiB browser download | 16.6 KiB/s |
| ICMP latency (4 pings) | 1287, 1311, 7, 2164 ms |
| ICMP loss | 0/4 |
| HTTP via mDNS | 200 OK, TCP connect + name resolution 11.13 s, total 12.35 s |
| mDNS resolution | enginelink-chatgpt.local -> 192.168.4.1 |
| v1 API free heap | 218,416 bytes (shown in browser screenshot) |

These are single-session findings, not controlled averages. No root cause
inferred from a single low-throughput session.

## V2 design differences

- Espressif `esp_http_server` (separate HTTP task), not the original v1
  synchronous Arduino `WebServer`.
- AP+STA, no modem sleep, lightweight offline mobile dashboard.
- Same SSID, password and IP as v1; router settings retained in `cgpt_wifi`.
- 64 / 512 / 2048 KiB HTTP throughput, RTT and detailed hardware telemetry.
- OTA uses a raw app-only image with explicit confirmation and delayed rollback
  validation; requires hardware verification.
- UART and NMEA pins are never initialized: deliberate Wi-Fi isolation.

## Test log

| Date | Commit | Input voltage | Distance | AP RSSI | Median HTTP RTT | 512 KiB/s | OTA recovery | Notes |
|---|---|---|---|---|---|---|---|---|
| Pending | — | — | — | — | — | — | Not tested | Initial v2 code |

## Gates

- [ ] CI Arduino core 3.3.12 compilation succeeds
- [ ] App-only OTA image validates, bytes and descriptor tested
- [ ] Remote flash succeeds without USB
- [ ] HTTP works at 192.168.4.1/lab
- [ ] Two status requests confirm firmware after 60 s
- [ ] HTTP benchmark results captured in report
- [ ] OTA back to v1 or normal release confirmed on bench
- [ ] Fast/reliable Wi-Fi confirmed in repeated tests

**Do not remove this report when removing experimental firmware code.**

## CI verification (2026-11-15)

- **Independent build passed:** [run 43750982656](https://github.com/Kaelor9/EngineLink/actions/runs/43750982656), including Arduino ESP32 core 3.3.12 compile, offline UI JavaScript parse, app descriptor / merged-image validation and OTA/USB artifact staging.
- **Merged separately:** PR [#192](https://github.com/Kaelor9/EngineLink/pull/192), commit `d205b4a992b0469ee44b471c28d4dd61cfa8ce3a`.
- Source is isolated at `experiments/chatgpt-wifi-lab-v2/`; the normal `firmware/EngineLink/` and default release channel are untouched.
- Bench and boat tests, OTA after flashing, and automatic rollback remain **unverified on hardware**.
- The OTA upload endpoint accepts raw app-only images by file selection from `/lab`, not a normal multi-part form upload. The old v1 image remains separately downloadable for recovery.
