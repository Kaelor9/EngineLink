<p align="center">
  <img src="docs/images/board-3d.png" width="100%" alt="EngineLink PCB">
</p>

<h1 align="center">EngineLink</h1>

<p align="center">
  Dual-engine marine fuel monitoring on a custom ESP32-S3 board. It reads two engines over J1708, either Volvo Penta KAD / EDC I or SAE J1587. It shows live fuel flow on your phone and records every engine run, without an app, internet or cloud.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/MCU-ESP32--S3-blue" alt="ESP32-S3">
  <img src="https://img.shields.io/badge/Engines-2%C3%97%20J1708%20isolated-success" alt="2 × isolated J1708">
  <img src="https://img.shields.io/badge/Volvo%20Penta-KAD300%20%2F%20EDC%20I-0b5394" alt="Volvo Penta KAD300 / EDC I">
  <img src="https://img.shields.io/badge/NMEA%202000-listen--only-informational" alt="NMEA 2000 listen-only">
  <img src="https://img.shields.io/badge/Firmware-0.13.0-orange" alt="Firmware 0.13.0">
</p>

<p align="center">
  <a href="https://kaelor9.github.io/EngineLink/demo/">
    <img src="https://img.shields.io/badge/TRY%20THE%20APP-LIVE%20DEMO-46cabc?style=for-the-badge&labelColor=0b1420" alt="Try the app: live demo">
  </a>
  <a href="https://kaelor9.github.io/EngineLink/">
    <img src="https://img.shields.io/badge/INSTALL%20FIRMWARE-OPEN%20WEB%20INSTALLER-ffffff?style=for-the-badge&labelColor=0b1420" alt="Open Web Installer">
  </a>
</p>

<p align="center">
  <a href="https://kaelor9.github.io/EngineLink/demo/">
    <img src="docs/images/app-screens.png" width="100%" alt="The EngineLink app: Trip, Tank, Engines and Diagnostics">
  </a>
  <br>
  <sub>The phone app served by the board. <a href="https://kaelor9.github.io/EngineLink/demo/">Open the live demo</a> to use it in your browser against a simulated boat.</sub>
</p>

---

## About

**EngineLink** is a dedicated onboard device for boats with two engines. It reads fuel and engine data from two separate J1708 engine interfaces, processes it locally and serves a clean web app straight from the device.

The goal is **useful and trustworthy fuel consumption** rather than raw frames on a screen. If the engines do not send a value, or it is stale or invalid, EngineLink shows **—** and explains why. It never shows a made-up 0.

It is developed for a Fairline Targa 34 with 2 × **Volvo Penta KAD300 (EDC I)**. These engines use Volvo's request/response protocol. Scalings come from the owner's VODIA analysis. See [docs/KAD300_EDC1.md](docs/KAD300_EDC1.md).

---

## Why EngineLink?

Marine engine displays rarely show combined consumption, economy per nautical mile or a history you can export. Generic ESP32 boards need extra isolation and protection before they can be connected to an engine bus.

EngineLink combines both on one board:

- **galvanically isolated** J1708 transceivers, one per engine
- An isolated NMEA 2000 interface for GPS speed and time
- microSD storage for raw data, engine runs and calibration captures
- Its own Wi-Fi network, so it works with no router and no internet
- A phone-friendly web app stored on the device

---

## Key Features

- **Phone app "Engine Link":** Trip, Economy, Tank, History and Engines, add it to the home screen. Light, dark and night themes, metric or imperial, any currency
- **Live fuel flow:** per engine and combined L/h (KAD index 289 or J1587 PID 183)
- **Automatic trips** with notes (people, sea, wind), combine/split and CSV export
- **Fuel log and tank:** fuel aboard with an honest ±, range at the current speed
- **Self-calibrating fuel figures:** weighted fit over all fill-ups (topped up / clicked off), separate correction for idle, part load and cruise, odd fill-ups caught, every trip recalculated
- **Economy:** best cruising speed, trip planner, season statistics per RPM band, engine health trend
- **Volvo Penta KAD / EDC I:** VODIA scaling, passive decoding of an EDC display, or opt-in read-only polling
- **Calibration tools:** voltage check against a multimeter, fuel scale check, gear learned at the dock
- **Checked engine data:** physical limits and jump confirmation on every answer, short gaps bridged, faster reading while the throttle moves
- **Fuel economy:** L/nm when NMEA 2000 GPS speed is available
- **Lifetime totals** with engine hours that survive restarts and recalculate after calibration
- **Bus discovery:** every parameter your ECUs actually answer or send, with raw bytes
- **Guided engine test:** record steady RPM stages with your own reference readings, export CSV
- **Two Wi-Fi modes:** EngineLink's own network at `192.168.4.1` (factory default), or on the boat router at the address the router gives it. If the router is lost, EngineLink brings its own network back within two minutes and returns to the router by itself. Transmit power is tuned automatically.
- **Firmware updates from GitHub** over the boat router, with automatic rollback, or from a file
- **Diagnostics** with a setup check, connection tests and calibration

---

## Hardware

| Feature | Specification |
|---------|---------------|
| MCU | ESP32-S3-WROOM-1-N8R8 |
| Flash / PSRAM | 8 MB / 8 MB (OPI) |
| Engine inputs | 2 × isolated J1708 (MAX3444E + ADuM1201), J7 and J6 |
| Marine network | Isolated NMEA 2000 (ISO1042), listen-only |
| Storage | microSD (SPI) with card detect |
| Status | SK6805 addressable LED, level-shifted |
| Digital input | Optoisolated input on GPIO38 |
| Wireless | Wi-Fi 2.4 GHz access point + optional router |
| USB | USB-C, native USB CDC |
| Power | Polyfuse + TVS (SMCJ33CA) + reverse protection → LM5164 buck → 5 V → 3.3 V LDO |

### Pinout

| Connection | ESP32-S3 | Notes |
|------------|----------|-------|
| J7 · J1708 channel 1 | RX GPIO21 · TX GPIO47 | TX only while KAD polling runs; solder jumper SJ1 |
| J6 · J1708 channel 2 | RX GPIO17 · TX GPIO18 | TX only while KAD polling runs; solder jumper SJ2 |
| NMEA 2000 CAN | TX GPIO15 · RX GPIO16 | Listen-only when enabled |
| microSD | CS 10 · MOSI 11 · SCK 12 · MISO 13 · Detect 14 | Detect LOW = inserted |
| Status LED | GPIO48 | SK6805 |
| Isolated input | GPIO38 | |
| I²C | SDA 8 · SCL 9 | Unused, no RTC fitted |

On the schematic, channel 1 (J7) sits on the isolated domain named ISO2 and channel 2 (J6) on ISO1. This is cosmetic only. More detail and safety checks: [docs/HARDWARE.md](docs/HARDWARE.md).

---

## Software

The firmware is written for the ESP32-S3 with the Arduino framework. The complete web app (HTML, CSS and JavaScript) is embedded in flash, so it works without any internet connection.

| Page | What it shows |
|------|---------------|
| **Trip** | Fuel this trip, burn now against your average, cost, L/nm and range |
| **Economy** | Best cruising speed, trip planner, season statistics, insights |
| **Tank** | Fuel aboard, fuel log, how the estimate is made |
| **History** | All trips with notes, combine/split, CSV export |
| **Engines** | Every value per engine, engine sync, health trend, service |
| **Settings** | Units, currency, EngineLink Wi-Fi and boat router, firmware, diagnostics, SD card, backup and reset |

The app source is [`firmware/ui/app.html`](firmware/ui/app.html); `tools/build_ui.py` packs it gzip-compressed into `firmware/EngineLink/ui.h`.

---

## Getting Started

1. **New boards only:** Open the **[web installer](https://kaelor9.github.io/EngineLink/)** in Chrome or Edge on a computer.
2. Connect the board with a USB-C data cable, tick the safety confirmation and click **Install**.
3. On your phone, join the Wi-Fi **EngineLink** (password `12345678`) and open **http://192.168.4.1**.
4. Complete the setup, then add **Engine Link** to the home screen (Settings shows how on iPhone and Android).
5. For Volvo Penta KAD300, follow the **[test procedure](docs/KAD300_EDC1.md#test-procedure-on-the-boat)**: listen, voltage check, RPM check, then tank to tank.

### Updating over Wi-Fi

You only need the cable once. For later versions:

- **With a boat router:** connect EngineLink to it under Settings → Boat router, then open **Settings → Firmware** and install the version you want. EngineLink downloads it from GitHub Releases. If the new version does not run properly for its first minute, EngineLink goes back to the previous one by itself.
- **Without a router:** download **`enginelink-update.bin`** from the [download page](https://kaelor9.github.io/EngineLink/#downloads), join the EngineLink Wi-Fi and choose the file under **Settings → Firmware**.

Settings, trips, the fuel log and fuel totals are kept with Wi-Fi application updates. **The full 8 MB USB installer overwrites NVS and LittleFS**, including stored data, even without a separate Erase Flash selection. If an existing board is locked out of Wi-Fi, use [USB recovery with backups](docs/USB_RECOVERY.md), not the full installer.

---

## Important Information

> [!IMPORTANT]
>
> EngineLink is **alpha firmware** and has not been validated on real engines yet.
>
> - Disconnect the engine networks and NMEA 2000 during first flashing and bring-up
> - Measure **both** isolated 5 V supplies against **their own** isolated grounds (4.75–5.25 V) before connecting an engine
> - EngineLink only listens by default. **Reading the engines** (KAD polling) is a deliberate step: close the connector's jumper (SJ1/SJ2) and switch it on in Settings → Diagnostics with a confirmation. "Read engines automatically" is off until you turn it on. Only the read request `01 43 21` can be sent.
> - The KAD request format is reconstructed from Volvo's PC tool, and the reply layout is not yet wire-verified. Do the voltage check first.
> - Do not poll while a Volvo diagnostic tool (VODIA) is connected
> - Change the factory Wi-Fi password `12345678` in Settings

See [KAD300 / EDC I](docs/KAD300_EDC1.md), [status and safety](docs/STATUS.md) and the [engine test procedure](docs/ENGINE_TEST.md).

---

## Project Status

| Hardware | Firmware | Engine validation |
|----------|----------|-------------------|
| ✅ Revision 1 assembled | 🚧 0.13.0 | ⏳ Pending first KAD300 test |

| Area | Status |
|------|--------|
| J1708 UART bench loop | Tested: 36,000 frames / 30 min / no recorded errors |
| microSD read/write | Bench tested |
| Status LED | Bench tested |
| Firmware build | CI compiled on every change |
| Router Wi-Fi | Tested on a Zyxel EX3600 (WPA2/WPA3 mixed): 500–630 kB/s, about 20 ms round trip on a free 2.4 GHz channel |
| KAD300 scaling (VODIA, M_902) | Confirmed from the database, fuel ×1/128 confirmed three ways |
| KAD request/reply on the wire | Reconstructed, verify with the voltage check |
| Fuel factor on the boat | Pending tank-to-tank |
| NMEA 2000 bus | Not yet validated |

---

## Roadmap

### Firmware

- [x] Offline web app with first-time setup
- [x] Dual-channel J1708 capture and SD logging
- [x] Guided engine test and calibration capture
- [x] Trip / lifetime totals and engine hours
- [x] Automatic engine-run log
- [x] Bus discovery
- [x] Wi-Fi firmware updates, from GitHub with rollback
- [x] Phone app with trips, fuel log, economy and engine health
- [x] Own Wi-Fi or boat router, with automatic fallback and return
- [x] Router mode validated on real hardware (WPA2, WPA3-capable routers)
- [x] Volvo Penta KAD / EDC I decoding and opt-in polling
- [ ] KAD reply layout confirmed on the wire
- [ ] Fuel factor and corrections confirmed tank to tank
- [ ] NMEA 2000 output of engine and fuel data
- [ ] Settings authorization and unique device credentials

### Hardware

- [x] Revision 1 PCB
- [x] Isolated J1708 and NMEA 2000 interfaces
- [ ] Isolated supply verification on assembled boards
- [ ] NMEA 2000 isolated ground review (`GND_N2K_ISO`)
- [ ] Field test on board

---

## Repository Structure

```text
.
├── .github/workflows/     # CI compile, validation and Pages publishing
├── docs/                  # KAD300/EDC I, hardware, alpha status, engine test, releasing
│   └── images/
├── experiments/           # Earlier bench Lab sketches (not the application)
├── firmware/
│   └── EngineLink/        # Application firmware (one file per area) + embedded web UI (ui.h)
├── hardware/              # Hardware notes
├── site/                  # GitHub Pages installer
├── HANDOVER.md            # Design decisions and constraints for developers
└── README.md
```

### Building locally

Arduino IDE or `arduino-cli` with the ESP32 core and the **Adafruit NeoPixel** library:

```bash
arduino-cli compile \
  --fqbn "esp32:esp32:esp32s3:FlashSize=8M,PartitionScheme=default_8MB,PSRAM=opi,USBMode=hwcdc,CDCOnBoot=cdc" \
  --build-property "compiler.c.elf.extra_flags=-Wl,--wrap=esp_wifi_init" \
  firmware/EngineLink
```

The version is defined once as `EL_VERSION` in `firmware/EngineLink/EngineLink.ino`. The installer and manifests take it from there. See [docs/RELEASING.md](docs/RELEASING.md). Release notes for every version are in [docs/releases](docs/releases).

---

## License

Licensing has not been selected yet. **Do not assume this repository is open source just because the code is visible on GitHub.**

---

<p align="center">
  Designed and developed by <strong>Kaelor9</strong>
  <br>
  ESP32-S3 • Marine Electronics • Dual-Engine Fuel Monitoring
</p>
