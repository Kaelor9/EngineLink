<p align="center">
  <img src="site/assets/enginelink-mark.svg" width="88" alt="EngineLink logo">
</p>
<h1 align="center">EngineLink</h1>
<p align="center"><strong>Marine Precision</strong> · ESP32-S3 dual-engine fuel monitoring and diagnostics</p>
<p align="center">J1708 / J1587 &nbsp;•&nbsp; NMEA 2000 &nbsp;•&nbsp; Fuel analytics &nbsp;•&nbsp; Offline web interface</p>
<p align="center"><a href="https://kaelor9.github.io/EngineLink/"><strong>Firmware Downloads &amp; Installer</strong></a> · <a href="https://github.com/Kaelor9/EngineLink/releases">Releases</a> · <a href="docs/PROJECT_PLAN.md">Project plan</a></p>

> [!NOTE]
> **EngineLink application v0.2.1-alpha** is now implemented and CI-compiled. It provides an offline onboard web app, first-time setup, passive dual-J1708 capture, standard PID183 fuel decoding and automatic SD session logging. **Actual engine compatibility and hardware reliability remain unverified.** Disconnect engine/NMEA2000 networks for initial flashing and bring-up. See [Alpha status and safety](docs/PRODUCTION_ALPHA.md).

## Purpose

EngineLink is a dedicated onboard device designed to collect fuel-consumption and engine information from two separate J1708/J1587 engine interfaces, process it locally, and eventually bridge selected data to NMEA 2000. The main objective is **useful and trustworthy fuel consumption**, not just showing raw engine frames.

### Intended capabilities

- Individual and combined fuel rate (L/h), subject to verifying real engine data.
- Fuel economy in L/nm when a valid speed source is available.
- Continuous fuel history across engine stops without requiring users to start/finish trips manually (cross-restart grouping still under development).
- Local storage to microSD, with careful handling of missing data and power loss.
- First-run setup: **EngineLink Wi-Fi** or **Connect to a router**, engine-to-channel mapping, automatic time selection.
- Permanent direct-access SoftAP capability; external Wi-Fi is optional.
- Time sources: browser/phone, NMEA 2000 GPS, optional NTP, monotonic timestamps when no absolute clock is available.
- Advanced passive J1708 packet monitor and read-only terminal; an on-device **Engine Test & Calibration** capture can export raw J1708 evidence with manually supplied reference RPM/fuel readings.

The alpha firmware implements a substantial subset, including fixed automatic LED fault/status indications and a passive motor calibration test with downloadable CSV. [Engine test instructions](docs/ENGINE_TEST.md). Automatic durable multi-day trip grouping, engine-specific proprietary decoders, transmitted NMEA 2000 PGNs, and J1708 transmit commands remain future work. See [application status](docs/PRODUCTION_ALPHA.md).

## Hardware (current board)

| Component | Description |
|---|---|
| MCU | ESP32-S3-WROOM-1-N8R8; 8 MB flash, 8 MB PSRAM |
| J1708 Channel 1 | J7, RX GPIO21 / TX GPIO47, isolated MAX3444E |
| J1708 Channel 2 | J6, RX GPIO17 / TX GPIO18, isolated MAX3444E |
| NMEA 2000 | Isolated ISO1042; CAN/TWAI TX GPIO15 / RX GPIO16 |
| microSD | SPI: CS10, MOSI11, SCK12, MISO13, detect14 |
| Status LED | Addressable SK6805, GPIO48 via level shifter |
| Digital input | Optoisolated GPIO38 |
| USB | ESP32-S3 native USB CDC |

See [hardware documentation](docs/HARDWARE.md) for test status and critical safety notes.

## Download and installation

The [GitHub Pages download page](https://kaelor9.github.io/EngineLink/) includes:

1. A link to the repository's source archive.
2. A link to official GitHub Releases (once releases exist).
3. An **ESP Web Tools** installer for the **EngineLink 0.2.1-alpha application** compiled by GitHub Actions. Flashing requires acknowledgement of the safety warning. Do not treat this as hardware-validated production firmware.

See [release procedure](docs/RELEASING.md). **Do not flash random or example binaries from other ESP32 projects.**

## Development material

The earlier bench dashboard is stored under [`experiments/`](experiments/README.md). It demonstrates SoftAP, LED control, SD and dual UART J1708 bench traffic. It is **not** the planned final product firmware, a validated fuel decoder, or an approved production release.

## Repository layout

```text
EngineLink/
├── .github/workflows/      # Pages publishing and basic checks
├── docs/                   # Design decisions, pinout and release process
├── experiments/            # Development sketches (bench only)
├── firmware/EngineLink/     # Actual alpha application, embedded offline UI
├── hardware/               # Hardware documentation / later public assets
├── site/                   # GitHub Pages download + installer website
├── .gitignore
└── README.md
```

## Development status

| Area | Status |
|---|---|
| J1708 UART bench loop | Tested: 36,000 frames / 30 minutes / no recorded transfer errors |
| microSD read/write | Bench tested |
| Addressable status LED | Bench tested |
| NMEA 2000 bus communication | Not yet validated |
| Fuel-rate decoding on real engines | Not yet verified |
| EngineLink application alpha | CI compiled; no real-engine validation yet |
| Browser flashing | EngineLink application alpha; hardware test pending |

> The J1708 bench result does not establish full bus arbitration/conformance. The NMEA2000 and engine connections require separate validation.

## License

Licensing has not yet been selected for this repository. **Do not assume this repository is open-source merely because the code is visible on GitHub.** An explicit LICENSE file can be added once the intended terms are decided.