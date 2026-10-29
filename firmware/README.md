# EngineLink firmware

The application firmware is in [`EngineLink/`](EngineLink/):

- `EngineLink.ino`: device logic. J1708 receive tasks, Volvo Penta KAD / EDC I decoding and opt-in polling (see [docs/KAD300_EDC1.md](../docs/KAD300_EDC1.md)), J1587 parsing, fuel integration, totals, engine-run segments, SD logging, NMEA 2000 listen-only, Wi-Fi/HTTP API, OTA update, status LED.
- `ui.h`: the phone app, gzip-compressed and served from flash. **Generated**: edit [`ui/app.html`](ui/app.html) and run `python3 tools/build_ui.py` (CI fails if they differ). No CDN, works offline.
- `icons.h`: the Engine Link app icon (SVG and PNG for the home screen).
- `rollback.cpp`: turns on the bootloader rollback check for Wi-Fi updates.
- `fuelcal.h`: the fuel calibration (weighted ridge regression over fill-ups, leave-one-out check). Plain C++, tested on a PC by `tools/test_fuelcal.cpp` in CI.

Data on the board: settings and lifetime totals in NVS; trips, the fuel log, engine health, diagnostics results and app preferences in LittleFS on the `spiffs` partition (written to a temporary file and renamed, so a power cut never leaves half a file); raw J1708 logs on the SD card, cleaned up after the number of days set in Settings.

Wi-Fi: the EngineLink access point is always on with power saving off, full transmit power and a 10-minute timeout for sleeping phones. With a boat router, the access point uses the router's channel and reconnects are aimed at that channel and router, with a backoff from 15 s to 5 min; a full channel scan only runs when no phone is connected. A watchdog restarts the access point if it stops.

HTTP: `EngineLinkWebServer` replaces the stock one-socket-at-a-time loop. It accepts up to 8 sockets at once, serves whichever has a request, and keeps HTTP/1.1 connections alive between requests (idle sockets close after 5 s). Responses carry `Access-Control-Allow-Origin: *`, so the app loaded from 192.168.4.1 can also reach EngineLink at its router address. `/api/live` reports that address as `net.ip`, and `net.conflict` when the router uses 192.168.4.x.

Build settings: ESP32S3 Dev Module, 8 MB flash, `default_8MB` partitions (two OTA slots), OPI PSRAM, USB CDC on boot. Library: **Adafruit NeoPixel**.

## HTTP API

| Method | Path | Purpose |
|---|---|---|
| GET | `/` | Phone app (gzip, ETag) · `/manifest.webmanifest`, `/icon.svg`, `/apple-touch-icon.png`, `/icon-192.png`, `/icon-512.png` |
| GET | `/api/state` | Live status, engines, totals, segments, recent frames |
| GET | `/api/pids` | Bus discovery: J1587 PIDs and KAD answers (`kad`: index, reply bytes, BE/LE value) per channel |
| POST | `/api/kad/poll` (`ch`, `on`, `confirm=POLL`) | Start/stop KAD read polling on one connector (never persisted) |
| POST | `/api/kad/config` | `protocol` (kad/j1587), `endian` (be/le), `fuelDiv` (128/256), `volt` (0/1), `corr0`/`corr1` |
| GET | `/api/history` | One-hour trend (10 s samples) |
| POST | `/api/setup`, `/api/settings` | Configuration |
| POST | `/api/phone-time` | Set clock from browser |
| POST | `/api/test/start`, `/mark`, `/stop` · GET `/api/test/download` | Passive engine test |
| POST | `/api/sd/start`, `/stop`, `/remount` · GET `/api/log` | Session logging |
| GET | `/api/segments.csv` | Engine-run log |
| POST | `/api/trip/reset` | Reset trip counter |
| POST | `/api/update` | Wi-Fi firmware update (multipart, app image only) |
| POST | `/api/reboot`, `/api/factory-reset` (`confirm=ERASE`, `keep=1` keeps trips and the fuel log) | Maintenance |
| POST | `/api/led/test` · GET `/api/led` | Show every status light pattern for 3 s each · the LED language (patterns, colours, the one on now) |
| GET | `/api/live` | Compact live data for the app (polled every 0.5 s) |
| GET | `/api/trips` · POST `/api/trip/note`, `/combine`, `/split` | Trip list and editing |
| GET | `/api/fuel` · POST `/api/fuel/add` (`q` 0 not full, 1 clicked off, 2 topped up), `/edit` (`i`, `q`, `use` 0 auto/1 use/2 never), `/delete` | Fuel log with calibration state per entry, calibration result (`cal`), tank ± |
| GET | `/api/economy` · POST `/api/season/reset` | RPM/speed statistics, zones, health |
| GET/POST | `/api/prefs` | App preferences shared by all phones |
| POST | `/api/cfg` | Tank, price, trip end, raw log days, NTP, port, scaling, `host` (mDNS name for `<host>.local`, also the DHCP hostname), `gearF`/`gearR` (`mask:value` learned at the dock). `autoPoll=1` needs `confirm=POLL` |
| POST | `/api/wifi/ap`, `/connect`, `/forget` · GET `/api/wifi/scan` | EngineLink network and boat router |
| GET | `/api/diag` · POST `/api/diag/test`, `/api/diag/focus` (`idx`, `secs` ≤ 90) | Diagnostics, 30 s connection test, data quality counters, one value read in every slow slot (gear learning) |
| GET | `/api/sd/info` · POST `/api/sd/wipe` (`confirm=WIPE`) | SD card |
| GET | `/api/events` · POST `/api/events/ack` (`id` or `all`), `/api/mark` | Diagnosis events, marking them seen, "Something's odd" |
| GET | `/api/event/series` (`id`), `/api/trip/series` (`id`), `/api/series/recent` (`secs` 60–3600) | Second-by-second curves (columns `t`, `rpm`, `req`, `lph`, `cool`, `air`, `boost`, `volt`, each `[port..., starboard...]` by channel), at most 600 points |
| GET | `/api/speedtest` (`kb` 16–4096) | Download speed test from memory (no SD card); result and the last export's timing in `/api/diag` → `sp`, `dl` |
| GET | `/api/export/series.csv` (`id`) | A trip's 1 Hz log as CSV |
| GET | `/api/life` | Each engine's life story and its learned normal values per rpm band |
| POST | `/api/kad/discover` (`ch`) | Ask once for each KAD value 256–319 (read requests only; reading must be on). Progress in `/api/diag` → `reading` |
| GET | `/api/export/trips.csv`, `/api/export/raw.csv`, `/api/backup` | Downloads |
| POST | `/api/fw/check`, `/api/fw/install` · GET `/api/fw/status` | Firmware from GitHub Releases |

There is no authentication yet beyond the Wi-Fi password. The Bench Lab sketches are in [`../experiments`](../experiments) and are not the application.
