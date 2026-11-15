# EngineLink ASYNC LAB v3 — findings and test record

Date: 2026-11-15.
Target: ESP32-S3-WROOM-1-N8R8, 8MB QSPI flash, 8MB OPI PSRAM.
Build toolchain: Arduino-ESP32 3.3.12; AsyncTCP 3.5.0; ESPAsyncWebServer 3.12.1.
Branch: experiment/enginelink-async-lab-v3.

## Baseline before v3

First ChatGPT v1 firmware used synchronous Arduino WebServer and reported:
- 512 KiB download 16.6 KiB/s; later 23.6 KiB/s.
- ICMP spikes of 1287 ms, 1311 ms, and 2164 ms, with one 7 ms ping.
- mDNS correctly resolved to 192.168.4.1.
- Windows reported strong Wi-Fi RSSI (approx -33 dBm) and negotiated Wi-Fi link rates 65 and 72.2 Mbps; these rates are NOT measured TCP throughput.
- HTTP request returned status 200, with TCP-connect including DNS 11.13 seconds, total 12.35 seconds.
- Disconnections and rollback were reported around test attempts, but exact v2 boot/rollback was not independently verified.

Do not infer that the root cause is solely the synchronous WebServer, hardware or PCB design from these observations.

## Controlled v3 comparison

| Date | Git SHA | Input voltage | AP RSSI | HTTP median ms | 512 KiB/s median | WS uptime/drops | OTA restore |
|---|---|---|---|---|---|---|---|
| Awaiting tests | — | — | — | — | — | — | unverified |

## Verification gates

- [x] CI builds v3 firmware with pinned upstream async libraries
- [x] JavaScript compiles, OTA app descriptor and size verified
- [ ] Normal firmware and v1/v2 Pages paths unchanged
- [ ] OTA v1 -> v3 on disconnected bench board
- [ ] OTA v3 -> v1 and normal EngineLink on disconnected bench board
- [ ] Automatic rollback behavior verified by induced controlled non-response
- [ ] 5 repeated 512 KiB runs and 30 pings compared at same conditions
- [ ] Auto telemetry WebSocket reconnects after AP client disconnect
- [ ] Verified remote connection after reboot

All failures and learning should be appended here; remove experimental code without deleting findings.

## CI findings — 2026-11-15

- Successful isolated async CI build: https://github.com/Kaelor9/EngineLink/actions/runs/43757536536
- Second isolated run also passed: https://github.com/Kaelor9/EngineLink/actions/runs/43757672621
- Pinned AsyncTCP 3.5.0 + ESPAsyncWebServer 3.12.1 install and compile for Arduino-ESP32 core 3.3.12 passed.
- Initial CI library installation failed because the Arduino Library Registry name is "Async TCP", not "AsyncTCP"; fixed by refreshing the index and using "Async TCP@3.5.0".
- Next compile failed because UpdateClass in core 3.3.12 does not expose Update.runAsync; removed the unsupported call while retaining a guarded async request-body upload and app verification.
- JavaScript parse, sketch build, ESP32 application descriptor validation, 8MB USB image staging, app-only OTA staging and Actions artifact upload succeeded.
- Test-only PR #193 merged as main commit 062f35d0f92a704a9733cb5de21baac3f9a414ba.
- **This is CI-only verification:** no physical Wi-Fi speed, OTA transition or rollback outcome is claimed.
