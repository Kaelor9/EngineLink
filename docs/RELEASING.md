# EngineLink firmware releases and web installer

The GitHub Pages website now provides an **experimental Lab v1.1** browser installer after a successful GitHub Actions compilation. This firmware is **bench only**, not hardware-verified production firmware. A separate approved release process remains mandatory for actual engine installations.

## Before release

1. Compile for **ESP32-S3-WROOM-1-N8R8** (8 MB flash, 8 MB OPI PSRAM), with tested USB CDC and partition options.
2. Produce a **merged binary** that includes bootloader, partitions and application at their actual offsets. A sketch-only .bin must not be flashed as a merged image at address 0.
3. Test the image on the **actual EngineLink PCB**, including boot, safe defaults, storage and Wi-Fi access.
4. Confirm the unit never enables J1708 TX on boot and that recovery from a failed network configuration works.
5. Verify flash erase and upgrade behavior; flashing can delete device settings.

## Publishing

For a future approved production firmware release, publish separately from the experimental Lab build. The current build workflow generates the following files in the GitHub Pages artifact (they are not committed to the repository):

- `firmware.bin`: CI-compiled **merged** ESP32-S3 experimental binary intended for flashing at address 0, only for bench use.
- `manifest.json`: ESP Web Tools manifest pointing to that file. The current CI build uses `EngineLink Lab (experimental)` version `1.1.0-lab`.

```json
{
  "name": "EngineLink Lab (experimental)",
  "version": "1.1.0-lab",
  "new_install_prompt_erase": true,
  "builds": [{"chipFamily":"ESP32-S3","parts":[{"path":"firmware.bin","offset":0}]}]
}
```

The example above describes the experimental build. The website checks that its manifest and binary exist before it displays the flashing option, and requires explicit acknowledgment of the bench safety warning. Successful compilation alone does **not** establish suitability for a real engine network.

Create a GitHub Release with version, compatible hardware revisions, flashing notes, change log, merged binary, checksum and any settings/data migration information.

## GitHub Pages

Select **Settings → Pages → Build and deployment → Source: GitHub Actions**. The `.github/workflows/pages.yml` action then publishes the `site/` folder to `https://kaelor9.github.io/EngineLink/`.

Desktop Chromium browsers supporting Web Serial may flash over USB. iPhone Safari cannot use Web Serial firmware flashing but can access the unit's local SoftAP web UI.
