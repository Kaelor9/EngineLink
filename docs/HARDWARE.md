# EngineLink hardware: current board

![EngineLink PCB](images/board-3d.png)

## Pinout

| Module | ESP32-S3 pins | Notes |
|---|---|---|
| J1708 Channel 1 / J7 | RX GPIO21, TX GPIO47 | Isolated MAX3444E / ADuM path; SJ1 TX jumper |
| J1708 Channel 2 / J6 | RX GPIO17, TX GPIO18 | Isolated MAX3444E / ADuM path; SJ2 TX jumper |
| Isolated NMEA 2000 CAN | TX GPIO15, RX GPIO16 | ISO1042 transceiver; physical bus not fully verified |
| microSD SPI | CS10, MOSI11, SCK12, MISO13 | Detect GPIO14, LOW when card inserted |
| Addressable status LED | GPIO48 | SK6805, level-shifted to 5V |
| Optoisolated input | GPIO38 | Digital input |
| I2C | SDA8, SCL9 | Exposed internally; no battery RTC installed |
| USB CDC | D− GPIO19, D+ GPIO20 | Native USB serial |

ESP32-S3-WROOM-1-N8R8: 8 MB flash and 8 MB OPI PSRAM.

## Verified on bench

USB serial, status LED, microSD detection and read/write were tested. Two-channel physical bench UART traffic completed 36,000 frames over 30 minutes without recorded corruption in that specific test. **This does not validate arbitration or all SAE J1708 electrical rules.**

## Safety before connecting engines

- The J1708 transceivers have galvanically isolated supply domains. Before transmitting, measure *both* supplies relative to **their own** isolated grounds: nominal 4.75–5.25 V.
- A prior diagnostic reading of roughly **0.98 V** on one isolated domain is not confirmed resolved. Verify again.
- Bench interconnection: J7 pin 1 (B) ↔ J6 pin 1 (B); J7 pin 2 (A) ↔ J6 pin 2 (A). **Do not bridge isolated grounds.**
- SJ1/SJ2 should be closed **only for the controlled bench TX test**; open before connecting real engine buses.
- Do not enable TX on real engines using this experimental firmware.
- Physical NMEA 2000 CAN communication, actual engine reception, fuel decoders and J1708 arbitration require separate validation.
