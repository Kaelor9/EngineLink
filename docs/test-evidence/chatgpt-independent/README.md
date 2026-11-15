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
