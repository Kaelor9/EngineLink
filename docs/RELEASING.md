# EngineLink firmware releases and web installer

The GitHub Pages website provides a browser installer for the **EngineLink application alpha** (`firmware/EngineLink/`) after a successful GitHub Actions compilation. It is **not** hardware-verified production firmware. A separate approved release process remains mandatory for actual engine installations. The old Lab sketches under `experiments/` are no longer published.

## Before release

1. Compile for **ESP32-S3-WROOM-1-N8R8** (8 MB flash, 8 MB OPI PSRAM), with tested USB CDC and partition options.
2. Produce a **merged binary** that includes bootloader, partitions and application at their actual offsets. A sketch-only .bin must not be flashed as a merged image at address 0.
3. Test the image on the **actual EngineLink PCB**, including boot, safe defaults, storage and Wi-Fi access.
4. Confirm the unit never enables J1708 TX on boot and that recovery from a failed network configuration works.
5. Verify flash erase and upgrade behavior; flashing can delete device settings.

## Publishing

The Pages workflow (`.github/workflows/pages.yml`) compiles `firmware/EngineLink` and generates these files in the Pages artifact (they are not committed):

- `firmware.bin`: CI-compiled **merged** ESP32-S3 image (bootloader + partitions + app) flashed at address 0.
- `manifest.json`: ESP Web Tools manifest pointing to that file.

```json
{
  "name": "EngineLink App (alpha)",
  "version": "0.2.1-alpha",
  "new_install_prompt_erase": true,
  "builds": [{"chipFamily":"ESP32-S3","parts":[{"path":"firmware.bin","offset":0}]}]
}
```

When the version changes, update **all** of these together: `EL_VERSION` in `firmware/EngineLink/EngineLink.ino`, the footer in `ui.h`, the manifest in `pages.yml`, the version check and button text in `site/app.js`, `site/index.html`, the artifact name in `build-engine.yml`, and the README/docs. `site/app.js` refuses to offer a manifest whose version does not match.

The website checks that its manifest and binary exist before it displays the flashing option, and requires explicit acknowledgment of the safety warning. Successful compilation alone does **not** establish suitability for a real engine network.

Create a GitHub Release with version, compatible hardware revisions, flashing notes, change log, merged binary, checksum and any settings/data migration information.

## GitHub Pages

Select **Settings → Pages → Build and deployment → Source: GitHub Actions**. The `.github/workflows/pages.yml` action then publishes the `site/` folder to `https://kaelor9.github.io/EngineLink/`.

Desktop Chromium browsers supporting Web Serial may flash over USB. iPhone Safari cannot use Web Serial firmware flashing but can access the unit's local SoftAP web UI.
