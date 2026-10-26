<p align="center">
  <img src="site/assets/enginelink-mark.svg" width="88" alt="EngineLink logo">
</p>
<h1 align="center">EngineLink</h1>
<p align="center"><strong>Marine Precision</strong> · ESP32-S3 dual-engine fuel monitoring and diagnostics</p>
<p align="center">J1708 / J1587 &nbsp;•&nbsp; NMEA 2000 &nbsp;•&nbsp; Fuel analytics &nbsp;•&nbsp; Offline web interface</p>
<p align="center"><a href="https://kaelor9.github.io/EngineLink/"><strong>Firmware Downloads &amp; Installer</strong></a> · <a href="https://github.com/Kaelor9/EngineLink/releases">Releases</a> · <a href="docs/PROJECT_PLAN.md">Project plan</a></p>

> [!NOTE]
> **Early development.** This is a project foundation, **not a released fuel-monitoring product**. An experimental Lab v1.1 build can be flashed through the browser for controlled bench use. It compiles in CI, but has **not** been validated as a full release on the actual PCB. Do not connect real engines or the NMEA 2000 bus.

## Purpose

EngineLink is a dedicated onboard device designed to collect fuel-consumption and engine information from two separate J1708/J1587 engine interfaces, process it locally, and eventually bridge selected data to NMEA 2000. The main objective is **useful and trustworthy fuel consumption**, not just showing raw engine frames.

### Intended capabilities

- Individual and combined fuel rate (L/h), subject to verifying real engine data.
- Fuel economy in L/nm when a valid speed source is available.
- Continuous fuel history across engine stops without requiring users to start/finish trips manually.
- Local storage to microSD, with careful handling of missing data and power loss.
- First-run setup: **EngineLink Wi-Fi** or **Connect to a router**, engine-to-channel mapping, automatic time selection.
- Permanent direct-access SoftAP capability; external Wi-Fi is optional.
- Time sources: browser/phone, NMEA 2000 GPS, optional NTP, monotonic timestamps when no absolute clock is available.
- Advanced J1708 packet monitor and **bench-only** transmission terminal.

**The features above are planned, not claims of current functionality.** See [project status](docs/PROJECT_PLAN.md).

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
3. An **ESP Web Tools** installer for the **experimental Lab v1.1** built by GitHub Actions. Flashing requires acknowledgement of the bench-only warning; this is not approved production firmware.

See [release procedure](docs/RELEASING.md). **Do not flash random or example binaries from other ESP32 projects.**

## Development material

The previous bench dashboard is stored under [`experiments/`](experiments/README.md). It demonstrates SoftAP, LED control, SD and dual UART J1708 bench traffic. It is **not** the planned final product firmware, a validated fuel decoder, or an approved production release.

## Repository layout

```text
EngineLink/
├── .github/workflows/      # Pages publishing and basic checks
├── docs/                   # Design decisions, pinout and release process
├── experiments/            # Development sketches (bench only)
├── firmware/               # Future production firmware source
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
| Production firmware | In design |
| Browser flashing | Experimental Lab only; production firmware unavailable |

> The J1708 bench result does not establish full bus arbitration/conformance. The NMEA2000 and engine connections require separate validation.

## License

Licensing has not yet been selected for this repository. **Do not assume this repository is open-source merely because the code is visible on GitHub.** An explicit LICENSE file can be added once the intended terms are decided.