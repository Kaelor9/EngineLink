<p align="center">
  <img src="docs/images/board-3d.png" width="100%" alt="EngineLink PCB">
</p>

<h1 align="center">EngineLink</h1>

<p align="center">
  Dual-engine marine fuel monitoring on a custom ESP32-S3 board. It listens to two engines over J1708/J1587, shows live fuel flow on your phone and records every engine run, without an app, internet or cloud.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/MCU-ESP32--S3-blue" alt="ESP32-S3">
  <img src="https://img.shields.io/badge/Engines-2%C3%97%20J1708%20isolated-success" alt="2 × isolated J1708">
  <img src="https://img.shields.io/badge/NMEA%202000-listen--only-informational" alt="NMEA 2000 listen-only">
  <img src="https://img.shields.io/badge/Firmware-0.3.0--alpha-orange" alt="Firmware 0.3.0-alpha">
  <img src="https://img.shields.io/badge/J1708%20TX-disabled-lightgrey" alt="J1708 transmit disabled">
</p>

<p align="center">
  <a href="https://kaelor9.github.io/EngineLink/">
    <img src="https://img.shields.io/badge/INSTALL%20FIRMWARE-OPEN%20WEB%20INSTALLER-ffffff?style=for-the-badge&labelColor=0b1420" alt="Open Web Installer">
  </a>
</p>

---

## About

**EngineLink** is a dedicated onboard device for boats with two engines. It reads fuel and engine data from two separate J1708/J1587 engine interfaces, processes it locally and serves a clean web app straight from the device.

The goal is **useful and trustworthy fuel consumption** rather than raw frames on a screen. If the engines do not send a value, or it is stale or invalid, EngineLink shows **—** and explains why. It never shows a made-up 0.

---

## Why EngineLink?

Marine engine displays rarely show combined consumption, economy per nautical mile or a history you can export. Generic ESP32 boards need extra isolation and protection before they can be connected to an engine bus.

EngineLink combines both on one board:

- Two **galvanically isolated** J1708 transceivers, one per engine
- An isolated NMEA 2000 interface for GPS speed and time
- microSD storage for raw data, engine runs and calibration captures
- Its own Wi-Fi network, so it works with no router and no internet
- A phone-friendly web app stored on the device

---

## Key Features

- ⛽ **Live fuel flow:** per engine and combined L/h from the ECU (J1587 PID 183)
- 🧭 **Fuel economy:** L/nm when NMEA 2000 GPS speed is available
- 📈 **One-hour trend** of fuel flow and RPM
- 🧾 **Trip and lifetime totals** with engine hours that survive restarts
- ⏱ **Automatic engine-run log:** no start/stop buttons, runs are never merged
- 🔎 **Bus discovery:** every parameter your ECUs actually send, with raw bytes and standard values
- 🧪 **Guided engine test:** record steady RPM stages with your own reference readings, export CSV
- 📶 **Offline first:** EngineLink Wi-Fi at `192.168.4.1` / `enginelink.local`, optional router
- 🔄 **Wi-Fi firmware updates** from the device's own Settings page
- 🩺 **Health page** that explains in plain words what works and what needs attention
- 🚦 **Fixed status LED** meanings, faults always take priority

---

## Hardware

| Feature | Specification |
|---------|---------------|
| MCU | ESP32-S3-WROOM-1-N8R8 |
| Flash / PSRAM | 8 MB / 8 MB (OPI) |
| Engine inputs | 2 × isolated J1708 (MAX3444E + ADuM), J7 and J6 |
| Marine network | Isolated NMEA 2000 (ISO1042), listen-only |
| Storage | microSD (SPI) with card detect |
| Status | SK6805 addressable LED, level-shifted |
| Digital input | Optoisolated input on GPIO38 |
| Wireless | Wi-Fi 2.4 GHz access point + optional router |
| USB | USB-C, native USB CDC |

### Pinout

| Connection | ESP32-S3 | Notes |
|------------|----------|-------|
| J7 · J1708 channel 1 | RX GPIO21 · TX GPIO47 | TX **not used**; solder jumper SJ1 |
| J6 · J1708 channel 2 | RX GPIO17 · TX GPIO18 | TX **not used**; solder jumper SJ2 |
| NMEA 2000 CAN | TX GPIO15 · RX GPIO16 | Listen-only when enabled |
| microSD | CS 10 · MOSI 11 · SCK 12 · MISO 13 · Detect 14 | Detect LOW = inserted |
| Status LED | GPIO48 | SK6805 |
| Isolated input | GPIO38 | |
| I²C | SDA 8 · SCL 9 | Unused, no RTC fitted |

More detail and safety checks: [docs/HARDWARE.md](docs/HARDWARE.md).

---

## Software

The firmware is written for the ESP32-S3 with the Arduino framework. The complete web app (HTML, CSS and JavaScript) is embedded in flash, so it works without any internet connection.

| Page | What it shows |
|------|---------------|
| **Fuel** | Total and per-engine L/h, RPM, L/nm, engine details, one-hour trend, trip and lifetime totals |
| **Data** | Live J1708 messages with decoding, bus discovery table, guided engine test and calibration |
| **History** | Engine runs, raw data log status and CSV downloads |
| **Device** | Health overview, status LED, network and time, technical counters |
| **Settings** | Wi-Fi, engine mapping, time source, NMEA 2000, firmware update, J1708 terminal, maintenance |

---

## Getting Started

1. Open the **[web installer](https://kaelor9.github.io/EngineLink/)** in Chrome or Edge on a computer.
2. Connect the board with a USB-C data cable, tick the safety confirmation and click **Install**.
3. On your phone, join the Wi-Fi **EngineLink** (password `12345678`) and open **http://192.168.4.1**.
4. Complete the three-step setup: connection, port/starboard connector and time source.

### Updating over Wi-Fi

You only need the cable once. For later versions:

1. Download **`enginelink-update.bin`** from the [download page](https://kaelor9.github.io/EngineLink/#downloads).
2. Join the EngineLink Wi-Fi and open **Settings → Firmware update**.
3. Select the file. EngineLink installs it and restarts. Settings and fuel totals are kept.

---

## Important Information

> [!IMPORTANT]
>
> EngineLink is **alpha firmware** and has not been validated on real engines yet.
>
> - Disconnect the engine networks and NMEA 2000 during first flashing and bring-up
> - Measure **both** isolated 5 V supplies against **their own** isolated grounds (4.75–5.25 V) before connecting an engine
> - Keep solder jumpers **SJ1/SJ2 open**. The firmware never configures the J1708 TX pins
> - Fuel scaling follows SAE J1587 and must be verified against your actual ECUs
> - Change the factory Wi-Fi password `12345678` in Settings

See [alpha status and safety](docs/PRODUCTION_ALPHA.md) and the [engine test procedure](docs/ENGINE_TEST.md).

---

## Project Status

| Hardware | Firmware | Engine validation |
|----------|----------|-------------------|
| ✅ Revision 1 assembled | 🚧 0.3.0-alpha | ⏳ Pending real-engine captures |

| Area | Status |
|------|--------|
| J1708 UART bench loop | Tested: 36,000 frames / 30 min / no recorded errors |
| microSD read/write | Bench tested |
| Status LED | Bench tested |
| Firmware build | CI compiled on every change |
| NMEA 2000 bus | Not yet validated |
| Fuel-rate decoding on real engines | Not yet verified |

---

## Roadmap

### Firmware

- [x] Offline web app with first-time setup
- [x] Passive dual-channel J1708 capture and SD logging
- [x] Guided engine test and calibration capture
- [x] Trip / lifetime totals and engine hours
- [x] Automatic engine-run log
- [x] Bus discovery
- [x] Wi-Fi firmware updates
- [ ] Verified decoders for the actual engines
- [ ] Journey grouping from GPS evidence (never automatic merging)
- [ ] NMEA 2000 output of engine and fuel data
- [ ] Settings authorization and unique device credentials

### Hardware

- [x] Revision 1 PCB
- [x] Isolated J1708 and NMEA 2000 interfaces
- [ ] Isolated supply verification on assembled boards
- [ ] Field test on board

---

## Repository Structure

```text
.
├── .github/workflows/     # CI compile, validation and Pages publishing
├── docs/                  # Hardware, alpha status, engine test, releasing
│   └── images/
├── experiments/           # Earlier bench Lab sketches (not the application)
├── firmware/
│   └── EngineLink/        # Application firmware + embedded web UI (ui.h)
├── hardware/              # Hardware notes
├── site/                  # GitHub Pages installer
├── HANDOVER.md            # Product requirements and design decisions
└── README.md
```

### Building locally

Arduino IDE or `arduino-cli` with the ESP32 core and the **Adafruit NeoPixel** library:

```bash
arduino-cli compile \
  --fqbn "esp32:esp32:esp32s3:FlashSize=8M,PartitionScheme=default_8MB,PSRAM=opi,USBMode=hwcdc,CDCOnBoot=cdc" \
  firmware/EngineLink
```

The version is defined once as `EL_VERSION` in `firmware/EngineLink/EngineLink.ino`. The installer and manifests take it from there. See [docs/RELEASING.md](docs/RELEASING.md).

---

## License

Licensing has not been selected yet. **Do not assume this repository is open source just because the code is visible on GitHub.**

---

<p align="center">
  Designed and developed by <strong>Kaelor9</strong>
  <br>
  ESP32-S3 • Marine Electronics • Dual-Engine Fuel Monitoring
</p>
