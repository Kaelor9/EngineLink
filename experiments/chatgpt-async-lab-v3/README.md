# EngineLink ASYNC LAB v3 — isolated benchmark

TEST ONLY, not production. Target ESP32-S3-WROOM-1-N8R8, 8 MB flash and 8 MB OPI PSRAM.

Pinned toolchain: Arduino ESP32 core 3.3.12, ESPAsyncWebServer 3.12.1 and AsyncTCP 3.5.0.

## Scope

- True asynchronous AsyncTCP and ESPAsyncWebServer instead of Arduino WebServer or the prior ESP-IDF server adapter.
- WebSocket /ws automatically pushes telemetry every second; browser reconnects automatically. Sparse HTTP fallback after a stalled socket.
- 64/512/2048 KiB streaming benchmarks, 10-request HTTP RTT tests and JSON report.
- AP signal strength, channel, connection counters, DHCP events, free RAM and PSRAM, CPU and flash diagnostics.
- EngineLink-inspired dashboard at /lab; original gzip UI at /original is visual reference only.
- Keeps EngineLink-ChatGPT AP, password 12345678, 192.168.4.1 and enginelink-chatgpt.local; reuses cgpt_wifi NVS settings.
- OTA raw binary upload via /lab/ota using explicit consent header and app-image descriptor verification. No 8 MB merged image accepted.
- Bootloader rollback remains pending until HTTP health checks succeed after 60 seconds; rollback is attempted after 3 minutes of failed confirmation if enabled.
- J1708 and CAN pins are never configured: Wi-Fi-only isolated test, no engine readings.

## Risk and safe flashing

A successful CI build DOES NOT validate automatic OTA rollback on real hardware. Remote-only flashing without USB is hazardous. Make sure the remote PC has a separate Internet access path and preferably arrange physical access for rescue. Download an authentic app-only backup image and export the current report before flashing.

Previous v1/v2 and normal releases are preserved. The V3 web UI supports raw app-only OTA to those images. The default normal EngineLink installer and firmware are not modified.

The project name in the ESP-IDF application descriptor can be the generic Arduino name arduino-lib-builder; matching it only confirms compatible packaging, not cryptographic provenance. Use only firmware binaries from this repository.

## Compare

Record bench voltage, PC and distance, same channel, AP client signal, 5 repetitions of the 512 KiB test, 10-request median HTTP RTT, 30 pings, uptime, and WebSocket reconnects. Log observations at docs/test-evidence/chatgpt-async-lab-v3/. Do not overwrite prior evidence.

The test UI is functional for network measurements only. EngineLink motor/trip features are intentionally not implemented.

## Source and deletion

experiments/chatgpt-async-lab-v3/
.github/workflows/async-lab-v3.yml
site/chatgpt-async-lab-v3/

Remove those files if discarded, but retain docs/test-evidence/chatgpt-async-lab-v3/ permanently.

References: https://github.com/ESP32Async/ESPAsyncWebServer and https://github.com/ESP32Async/AsyncTCP
