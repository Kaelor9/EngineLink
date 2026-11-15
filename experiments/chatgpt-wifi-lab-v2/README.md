# EngineLink Wi-Fi LAB v2 — independent ESP32-S3 test

**Status: TEST ONLY, not field validated. Do not connect to a running boat.**

Version: 0.0.2-chatgpt-wifi-lab
Target: **ESP32-S3-WROOM-1-N8R8**, 8 MB flash, 8 MB OPI PSRAM, two OTA app slots.

## Why this experiment

The first ChatGPT firmware on the same EngineLink hardware was observed through
its own Wi-Fi network, after roughly two hours uptime, to provide:
- 512 KiB browser throughput: **16.6 KiB/s** (observed, not a controlled average).
- 4 ICMP ping results: 1287 ms, 1311 ms, 7 ms, 2164 ms; zero packet loss.
- Curl to enginelink-chatgpt.local/lab: HTTP 200, connect 11.13 s, total 12.35 s.
- mDNS resolution worked: enginelink-chatgpt.local -> 192.168.4.1.

Those tests cannot by themselves distinguish link/RF, ESP32 silicon/PCB,
Wi-Fi driver, firmware blocking, Windows network configuration or a sleeping
client. This baseline independently tests the **HTTP server architecture**.

## What has been deliberately changed

- Replaces the prior synchronous Arduino WebServer with Espressif
  esp_http_server in a dedicated FreeRTOS task. Keeps Arduino-ESP32 3.3.12
  core / hardware config unchanged for a more controlled comparison.
- Uses AP+STA with WiFi sleep disabled (WIFI_PS_NONE), 20 MHz AP width.
- Device keeps **EngineLink-ChatGPT** AP, same password **12345678**, same
  address **192.168.4.1** and mDNS name **enginelink-chatgpt.local**.
- Reads and writes router credentials in the SAME cgpt_wifi NVS namespace
  as the already-installed v1 experiment. Never wipes the original
  EngineLink application's enginelink NVS or LittleFS.
- Lightweight offline dashboard styled like normal EngineLink on /lab and /.
  Normal UI gzip is available at /original *cosmetic only*; its endpoints
  are not implemented.
- Rich JSON status (/lab/status), 64/512/2048 KiB HTTP transfer benchmarks
  (/api/speedtest?kb=), RTT endpoint (/api/rtt), report (/lab/report).
- Reports free internal RAM, PSRAM and DMA-capable heap, radio channel,
  AP-client RSSI (when available), router RSSI, connection/disconnect
  counters, reset cause, TX power, CPU and flash information.
- **OTA remains available at /lab/ota and /api/update**: accepts only a raw
  ESP32 application-only binary with explicit X-EngineLink-Test: CONFIRM-OTA
  header. The lab dashboard sends this header and raw binary automatically.
- The previous OTA slot is not marked valid until >=60 s uptime and at least
  two successful /lab/status (or /api/live) calls. If still unverified
  after three minutes, attempts automatic ESP-IDF rollback. Whether rollback
  is possible depends on bootloader/partition state and requires bench proof.
- No UART/CAN pins are initialized at all. This is intentional to isolate
  Wi-Fi from engine work and to avoid accidental transmissions.

## What is *not* implemented

Motor polling, passive J1708 capture, CAN data, trips, fuel calculations,
calibration, SD logging, UI's functional API, GitHub issue reports. These
are *not* silently simulated. The experimental JSON explicitly reports
the absence of engine/NMEA measurements.

## Access

After a successful OTA flash from the v1 test:
- SSID: EngineLink-ChatGPT
- Password: 12345678
- Diagnostics: http://192.168.4.1/lab
- mDNS alternative: http://enginelink-chatgpt.local/lab
- Normal original UI *visual reference*: /original

### Restore over Wi-Fi (important for remote-only access)

The safe path is **application-only OTA**, not the 8 MiB merged USB image.
From v1: open /lab, scroll down to Firmware update and select
enginelink-update-CHATGPT-WIFI-LAB-V2.bin. The v1 WebServer upload form
sends a multipart body and handles the OTA as expected.

From v2: /lab -> Firmware update -> choose an app-only .bin ->
confirm -> Flash. This uses a raw body and the guarded /lab/ota endpoint.
To return to normal firmware use the normal EngineLink
enginelink-update.bin downloaded from a trusted GitHub Release.

Do not upload a *merged* 8 MB USB image into either OTA updater.
The new OTA updater is **source-implemented, compile-checked only**, not
validated on EngineLink PCB. On a remotely accessed board without USB
recovery, flashing it still entails risk. Keep the v1 image saved for fallback.

### Suggested controlled comparison

1. Keep the same supply voltage, board placement, PC, AP client and distance.
2. Before flashing, export v1 /lab/report and run 512 KiB benchmark 3 times.
3. After v2 OTA, confirm /lab/status responds twice and uptime >60 s;
   export a JSON report, note PSRAM and AP-client RSSI, test 64/512 KiB
   three times, and HTTP latency 10 times.
4. Run in PowerShell (on local/remote PC connected to the AP):

       ping 192.168.4.1
       ping enginelink-chatgpt.local
       curl.exe -4 --noproxy "*" -sS -o NUL -w "DNS:%{time_namelookup}s TCP:%{time_connect}s TOTAL:%{time_total}s\n" http://192.168.4.1/lab

5. Repeat with router STA *only if AP accessibility is stable* and logs
   show a strong signal; AP+STA share a radio/channel and this introduces
   a new variable.
6. Compare results, retaining them in docs/test-evidence/chatgpt-wifi-lab-v2/.

## Isolation and deletion

Code is isolated in experiments/chatgpt-wifi-lab-v2/ and its CI workflow.
The published test installer lives at site/chatgpt-wifi-lab/.
Normal EngineLink firmware and normal release selector remain unchanged.
To delete test code, remove its folder/workflow/installer, but **retain
the evidence and conclusions** in docs/test-evidence.

## Research references

Espressif official docs:
- https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/wifi-driver/wifi-performance-and-power-save.html
- https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/protocols/esp_http_server.html
- https://github.com/espressif/esp-idf/tree/master/examples/wifi/iperf

No Espressif 20 Mbit/s lab result should be taken as a speed guarantee for
EngineLink or for a softAP connection on this particular PCB.
