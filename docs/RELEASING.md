# EngineLink firmware releases and web installer

The GitHub Pages website is staged. Browser flashing is **intentionally disabled** until a real ESP32-S3 firmware image has been verified.

## Before release

1. Compile for **ESP32-S3-WROOM-1-N8R8** (8 MB flash, 8 MB OPI PSRAM), with tested USB CDC and partition options.
2. Produce a **merged binary** that includes bootloader, partitions and application at their actual offsets. A sketch-only .bin must not be flashed as a merged image at address 0.
3. Test the image on the **actual EngineLink PCB**, including boot, safe defaults, storage and Wi-Fi access.
4. Confirm the unit never enables J1708 TX on boot and that recovery from a failed network configuration works.
5. Verify flash erase and upgrade behavior; flashing can delete device settings.

## Publishing

After hardware validation, put these in `site/`:

- `firmware.bin`: verified **merged** ESP32-S3 binary intended for flashing at address 0.
- `manifest.json`: ESP Web Tools manifest pointing to that file, such as:

```json
{
  "name": "EngineLink",
  "version": "v1.0.0",
  "new_install_prompt_erase": true,
  "builds": [{"chipFamily":"ESP32-S3","parts":[{"path":"firmware.bin","offset":0}]}]
}
```

The version shown is **illustrative, not released**. Publish only after test. The installer page automatically checks both files and enables the ESP Web Tools component if valid resources exist. Asset checks alone do **not** prove a binary is safe; bench validation remains mandatory.

Create a GitHub Release with version, compatible hardware revisions, flashing notes, change log, merged binary, checksum and any settings/data migration information.

## GitHub Pages

Select **Settings → Pages → Build and deployment → Source: GitHub Actions**. The `.github/workflows/pages.yml` action then publishes the `site/` folder to `https://kaelor9.github.io/EngineLink/`.

Desktop Chromium browsers supporting Web Serial may flash over USB. iPhone Safari cannot use Web Serial firmware flashing but can access the unit's local SoftAP web UI.
