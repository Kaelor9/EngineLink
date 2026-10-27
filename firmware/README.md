# EngineLink firmware

The application firmware is in [`EngineLink/`](EngineLink/):

- `EngineLink.ino`: device logic. J1708 receive tasks, Volvo Penta KAD / EDC I decoding and opt-in polling (see [docs/KAD300_EDC1.md](../docs/KAD300_EDC1.md)), J1587 parsing, fuel integration, totals, engine-run segments, SD logging, NMEA 2000 listen-only, Wi-Fi/HTTP API, OTA update, status LED.
- `ui.h`: the complete web app (HTML/CSS/JS) served from flash. No CDN, works offline.

Build settings: ESP32S3 Dev Module, 8 MB flash, `default_8MB` partitions (two OTA slots), OPI PSRAM, USB CDC on boot. Library: **Adafruit NeoPixel**.

## HTTP API

| Method | Path | Purpose |
|---|---|---|
| GET | `/` | Web app |
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
| POST | `/api/reboot`, `/api/factory-reset` (`confirm=ERASE`) | Maintenance |
| POST | `/api/led/test` | 16 s LED demonstration |

There is no authentication yet beyond the Wi-Fi password. The Bench Lab sketches are in [`../experiments`](../experiments) and are not the application.
