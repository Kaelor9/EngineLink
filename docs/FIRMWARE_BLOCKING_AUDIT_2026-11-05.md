# Firmware blocking, USB and connection audit — 2026-11-05

Read-only code inspection of EngineLink 0.16.6 development branch `claude/wizardly-lovelace-v6tlqz` (PR #160, head `d56680be`), cross-checked against `main` / 0.16.5. Examined all non-generated firmware implementation files and headers under `firmware/EngineLink` (excluding generated `ui.h` and `icons.h`), concentrating on execution flow and blocking behavior. **No code changed and no target board tests were performed.** Findings marked *risk* are not validated field defects.

## Summary

**No application code path deliberately selects a different radio mode, transmit power or engine configuration based on USB versus buck-converter supply.** USB on a PC additionally exposes USB CDC/Serial and host-driven reset/line-state behavior. Different rail voltages/transients from the USB path vs buck converter are hardware questions, not software power-mode switching.

### 1. Confirmed logic flaw: serial export has no aggregate timeout (P1)

- `serialcmd.ino` `serialSend()` lines ~56–63 waits up to 2000 ms **for each call** if `Serial.availableForWrite()==0`.
- `serialFile()` lines ~65–70 reads a file in 256-byte chunks, calling `serialSend()` per chunk regardless of prior timeout/failure. If a PC stops reading after sending `wifi log` or `wifi reports`, the loop can incur repeated 2-second stalls for many chunks, all on the main loop.
- Additionally, if `availableForWrite()>0` but `Serial.write()` returns zero, the `room>0` branch neither checks timeout nor yields. **Potential unbounded busy loop** dependent on USB driver behavior.
- Present on both `main` and 0.16.6 branch.
- Proposed: `serialSend()` returns bytes sent/success, maintains a single deadline even when room>0 and written==0, and `serialFile()` aborts on first timeout or yields/batches asynchronously; cap bytes/time per main-loop pass. Host-test with a simulated stalled serial device.

### 2. Synchronous SD Wi-Fi logging holds main loop (P1/P2 risk)

- `serialcmd.ino` `serviceWifiLog()` lines ~33–53 runs each loop, opens/closes `/WIFI.LOG` for each event, and periodically writes an entire report `/WIFIREP.TXT` with `buildNetworkReport()`.
- `wifi.ino` `serviceNetwork()` writes additional `wifiLog()` events inline.
- Flash/SD-card latency and event storms can delay network service; due to shared `engineMx`, they may also delay engine task / HTTP handler. This does not prove the Wi-Fi radio disconnects.
- Proposed: bounded logger queue, one batch SD write, time-budgeted service, record max SD latency and dropped log records.

### 3. Main-loop/engine-task mutex coupling (P1 architectural risk)

- `EngineLink.ino` `setup()` takes `engineMx` (about line 525); main loop releases/reacquires it only at the tail via `netWaitBegin(); delay(1); netWaitEnd()` (around line 579).
- `engines.ino` `engineTask()` line ~470 waits indefinitely for `engineMx`. Webserver requests also take the same lock (`httpd.ino` `ctxLock`).
- A slow synchronous operation (e.g. SD file I/O, flash Preferences writes, a serial command) in the main loop may affect more than one subsystem despite separate cores/tasks.
- Proposed: bound all long operations; measure `loopParts`, `engineMaxWaitMs`, `engineSlowWaits`, `httpdBusy503` and backlog/drops under SD and Serial stress. Avoid changing lock ownership without a dedicated refactor and host/bench tests.

### 4. Network scans are real radio activity, and may perturb observed throughput (P2)

- `wifi.ino` `serviceWifiScan()` (around 205–237) temporarily enters `WIFI_AP_STA` to scan when the SoftAP is active and switches back. On a single-radio ESP32 scanning can perturb active traffic; not proof of a disconnection.
- `webapi.ino` `/api/speedlog` (around 992–1005) requests a radio test after a speed test in router mode, at most once per five minutes; `netreport.ino` `linkTuneTask()` calls `runRadioTest()`; `linktune.cpp` executes pings and an active multichannel scan.
- If comparing throughput across USB/buck or firmware versions, avoid overlapping scans or compare with the diagnostic task disabled/idle. Include scan started/completed times in report interpretation.

### 5. OTA rollback self-test uses blocking network API in main loop (P2 limited-scope risk)

- `netsvc.ino` `loopbackHealthOk()` lines ~16–25: `NetworkClient.connect(...,1000)`, then `readStringUntil('\n')` without custom read timeout; `serviceSelfTest()` invokes from 60 s after boot and every 20 s only while a new image awaits verification. Mutex is released while this happens, so engine task can continue, but the main loop's network scheduling is delayed.
- If client response arrived already, self-test confirms without loopback. This is limited to OTA pending verification.
- Proposed: explicit short client timeout / asynchronous self-test or record maximum elapsed; test rollback path with nonresponsive local service.

### 6. Trace-to-SD log can skip concurrent events (P2 diagnostic race)

- `wifi.ino` `getWifiTrace()` snapshots events under `traceMux`, but `serialcmd.ino` calculates fresh events from independently read `traceTotal` and sets `wifiLogged=traceTotal` **after** writing the snapshot. A Wi-Fi callback arriving after copying entries but before that assignment may be counted as logged although its record was not copied.
- This affects log completeness rather than RF hardware.
- Proposed: capture trace sequence number and entries atomically under `traceMux`, advance `wifiLogged` only to the captured sequence; add concurrent producer/logger test.

### 7. Intentional router fallback switches the driver fully (functional behavior)

- `wifi.ino` `serviceNetwork()`: 2 min without router IP -> SoftAP, retry router after 10 min if no phone attached. `applyNetwork()` calls `httpdSuspend()` then `WiFi.mode(WIFI_OFF)` before starting the next interface. This **intentionally disconnects clients** during mode changes and is unrelated to USB supply. Stage 3's new state machine is not wired into firmware until Stage 4.
- This is already a known planned design transition. Do not treat as newly discovered defect; test Stage 4 behavior against Fable's plan.

## Fixed/improved in 0.16.5–0.16.6

- `EngineLink.ino` calls `Serial.setTxTimeoutMs(0)` after begin: handles previously reported standard USB serial writes blocking when a host doesn't drain the port (existing in both main and branch). **This does not fix serialSend/serialFile above**.
- 0.16.6 moves web handling out of `main`'s synchronous `web.handleClient()` into `esp_http_server` task, and long transfers to a worker. Directly reduces impact of a slow HTTP peer, but cannot make unrelated synchronous main-loop operations nonblocking.
- `serviceRawCleanup()` was already divided into bounded steps, and engine UART reception is queued; do not report it as a current unbounded full-directory deletion problem. `handleSdWipe()` is a separate explicit user-triggered heavy operation.

## Suggested bench test matrix

1. Same 0.16.6 firmware, SD card inserted: a) PC USB with no serial console, b) PC USB with serial terminal idle, c) PC USB terminal executing `wifi reports` while receiver stops reading, d) USB charger only, e) current-limited buck input at 12 V and 24 V. Save diagnostic JSON / report and compare.
2. USB terminal stalled-export scenario: observe `loopParts[serial]`, `loopMaxMs`, `engineMaxWaitMs`, `engineSlowWaits`, reset reason, HTTP 503/latency; avoid running during active engine tests. Use a lab board, not live engines.
3. With an SD card: compare no-card, healthy card and slow/worn card under synthetic Wi-Fi reconnection events. Observe report and timing counters.
4. During speed test, check whether `linkTuneRunning` / channel survey occurs concurrently with subsequent throughput samples.
5. For any suspected *power source* effect, log 5 V and 3.3 V rails using scope under radio burst and USB/12 V/24 V modes. Program has no explicit supply detection.

**No claims of actual root cause or verified hardware malfunction without reproduction.**
