# EngineLink — ChatGPT independent firmware experiment

**Status: experimental, not hardware-validated, not feature-complete.**

This folder contains a new Arduino firmware implementation, distinct from
the existing production firmware at firmware/EngineLink/. It intentionally
does not include or copy the original backend. The original generated phone
UI header is the **only shared application source**, copied to the build
folder by the isolated GitHub Actions job (not committed twice).

## Scope delivered in the initial diagnostic baseline

- Dedicated access point EngineLink-ChatGPT at 192.168.4.1, default test
  password 12345678; independent AP+STA mode so the AP remains available
  during router connection.
- mDNS enginelink-chatgpt.local where supported.
- Local read-only test dashboard at /lab.
- Wi-Fi counters, heap, IP addresses, disconnect reason, status JSON export,
  and a 512 KiB local download benchmark.
- Both J1708 UART receive channels (GPIO21 / GPIO17, 9600 8N1) with
  checksums, dropped-frame/error statistics and passive J1587 / KAD decoding.
- Optional NMEA 2000 reception (GPIO15/16, TWAI listen-only).
- App-only OTA update endpoint (/api/update), compatible with ordinary
  EngineLink firmware and the normal firmware's project-name check.
- Original visual UI served at / **for appearance comparison only**:
  the full original API is not implemented and some screens will not work.

## Not yet implemented

Normal UI API feature parity; opt-in engine polling; calibrated fuel totals;
trips, history and fuel logs; comprehensive diagnostics; microSD logging;
NMEA fast-packet decoding; reporting to GitHub; and selecting GitHub releases
from the test app. Readings are null unless captured from real valid frames.

The first version is a **network and read-only hardware comparison baseline**.
It is not a replacement of EngineLink 0.17.7 and must not be used to evaluate
the absent business-logic features.

## Flashing and recovery

Arduino sketch path:
experiments/chatgpt-independent/EngineLink/EngineLink.ino

The directory is intentionally named EngineLink, so that the ESP-IDF app
descriptor's project_name matches the normal EngineLink sketch. The original
app's OTA endpoint checks that value and should accept this test app image.
This expectation must be checked on a bench PCB before relying on it.

Arduino FQBN:
esp32:esp32:esp32s3:FlashSize=8M,PartitionScheme=default_8MB,PSRAM=opi,USBMode=hwcdc,CDCOnBoot=cdc

The dedicated workflow produces:
- enginelink-update-CHATGPT-TEST.bin (application-only image for Wi-Fi OTA)
- firmware.bin (merged 8 MiB USB image)
- manifest.json (for a **separate** USB installer)
- sha256.txt

**Do not flash the merged 8 MiB USB image at offset zero on a board with
data to preserve.** The current normal USB installer erases NVS and LittleFS
as part of writing the full image. The OTA application image is the preferred
reversible test method.

To restore, use the lab dashboard /lab to upload the normal release
enginelink-update.bin. Test on a disconnected board first, then verify
return to the normal firmware and retained settings. No UART TX is
configured by this firmware; NMEA operates in listen-only mode.

## Cleanup

The experiment is confined to:
- experiments/chatgpt-independent/
- .github/workflows/chatgpt-lab.yml
- docs/test-evidence/chatgpt-independent/

Delete these paths and the experiment branch to remove the test source.
The normal firmware and normal web installer remain untouched.

All test sessions should record their commit SHA, firmware version, input
voltage, router/channel, test device, distance, result and failures in the
evidence folder. Do not rewrite or erase prior field reports.
