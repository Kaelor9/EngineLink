# ChatGPT standalone EngineLink firmware — test evidence

This folder is append-only test documentation. Compare results against
the unchanged normal firmware, not against assumptions.

## Initial baseline

- Implementation branch: experiment/chatgpt-independent-firmware
- Test sketch: experiments/chatgpt-independent/EngineLink/EngineLink.ino
- Version: 0.0.1-chatgpt-test
- Baseline normal version at experiment start: 0.17.7
- Hardware target: ESP32-S3-WROOM-1-N8R8, 8 MB flash + 8 MB OPI PSRAM
- Major design difference: independent AP+STA with AP always available;
  normal firmware selects AP-only or STA-only.
- Safety defaults: no J1708 TX configuration; CAN listen-only and off at boot
- Bench results: none submitted yet; no claims of successful compilation or
  validated OTA can be made before CI/hardware evidence.

## Tests to record

| Date | Commit | Input power | AP association | Router STA | DHCP/TCP | 512 KiB rate | Recovery | Notes |
|---|---|---|---|---|---|---|---|---|
| Pending | — | — | not tested | not tested | not tested | not tested | not tested | Starting baseline |

## Required checkpoints

- [ ] Experimental Arduino CI compile succeeds
- [ ] App descriptor is named EngineLink and image is not a merged 8 MB file
- [ ] Normal EngineLink updater accepts experimental OTA on isolated bench board
- [ ] A normal app-only image restores the original firmware and preserves data
- [ ] Separate test USB installer is available without replacing normal latest
- [ ] Wi-Fi AP+STA, phone connection, router, and web throughput compared
- [ ] UART/CAN safety and read-only observations verified on hardware
- [ ] Full original UI API compatibility addressed separately

Record future compiler failures, observations and engineering decisions here.
Preserve normal docs and previously collected Wi-Fi/boat evidence.

## Finding: OTA identity validation (2026-11-15)

The Arduino-ESP32 3.3.12 application descriptor stores the generic project
name **arduino-lib-builder** in the experimental image. Production's
`otaSameProject()` only checks that the uploaded image has the same project
name. This alone cannot prove that a binary was authored for EngineLink;
a different Arduino-ESP32 sketch may have the same project_name. The test
and regular firmware should eventually authenticate the image using an
explicit application identity/version header and cryptographic digest or
signature, not solely `esp_app_desc_t.project_name`.

Experimental CI compile succeeded for this baseline; staging initially failed
due to mistakenly assuming the descriptor would be `EngineLink`. The CI
validation was updated to accept and explicitly log the actual generic name.
Hardware update and rollback testing remain **not verified**.

## Initial CI results (2026-11-15)

- Source merged as test-only PR #191, main commit `d4720ae258ce672f0cfd2213577eaf86457c25d3`.
- Verified independent build: GitHub Actions run [43732333214](https://github.com/Kaelor9/EngineLink/actions/runs/43732333214) with ESP32 core 3.3.12, target ESP32-S3 8MB / OPI PSRAM.
- Arduino compilation succeeded: 1,092,199 bytes program (32% of app allowance), 50,508 bytes global RAM (15%).
- The separate OTA and 8 MiB USB image staging steps, and test-artifact upload, passed. This is a **build/packaging verification only**.
- First staging attempts failed because the checker incorrectly expected ESP application project name `EngineLink`; actual core compiled `arduino-lib-builder`. Checker and documentation were corrected.
- No hardware run, AP/STA benchmark, NMEA/J1708 verification, or bidirectional OTA/rollback has been recorded yet.
- Production UI and runtime sources were not changed; the hidden test-only Pages publishing step is isolated and optional.

### Readiness labels

| Component | Current status |
|---|---|
| Compiles on GitHub | Confirmed |
| Valid OTA app-only binary generated | Header/size validated, not flashed |
| 8 MiB USB image generated | Image size validated, not flashed |
| Dedicated Pages installer deployed | Awaiting Pages publishing verification |
| Wi-Fi association/HTTP speed on board | Not tested |
| Normal ↔ experimental OTA transition | Not tested |
| Engine/CAN safe operation | Not hardware validated |
