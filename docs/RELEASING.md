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
  "version": "0.5.0",
  "new_install_prompt_erase": true,
  "builds": [{"chipFamily":"ESP32-S3","parts":[{"path":"firmware.bin","offset":0}]}]
}
```

The version is defined **once**, as `EL_VERSION` in `firmware/EngineLink/EngineLink.ino`. The Pages workflow reads it from there to write `manifest.json` and `release.json`, and to cache-bust `index.html` (`__VERSION__`). `site/app.js` shows whatever version the manifest contains. Only the README badge and the docs mention the version by hand.

The workflow also publishes **`enginelink-update.bin`**, the application-only image for Wi-Fi updates. It is checked for the ESP32 app descriptor before publishing. Never upload `firmware.bin` (the merged USB image) as a Wi-Fi update; the device rejects it.

### Updates from the app (0.5.0 and later)

When the workflow runs for a version that has no GitHub Release yet, it creates release `v<EL_VERSION>` with `enginelink-update.bin` and `firmware.bin` attached. A version with a hyphen (for example `0.5.1-beta`) is marked as a pre-release and only shows under **Test versions** in the app. Release notes are taken from `docs/releases/v<EL_VERSION>.md` when that file exists. An existing release is never replaced: bump `EL_VERSION` to publish new firmware.

After that, the workflow writes `releases.json` to the Pages site from all releases that have `enginelink-update.bin`, newest first (at most 5):

```json
{"releases":[{"v":"0.5.0","date":"2026-10-28","pre":false,"notes":"…","url":"https://github.com/Kaelor9/EngineLink/releases/download/v0.5.0/enginelink-update.bin","size":1471387}]}
```

The board fetches this list over its router connection (Settings → Firmware → Check for updates). It only installs URLs under `https://github.com/Kaelor9/EngineLink/releases/download/`, verifies TLS with the built-in certificate bundle and writes the image to the inactive OTA slot. The new firmware must stay healthy for 60 seconds before it marks itself valid; if it crashes or is reset before then, the bootloader rolls back to the previous version. Uploading a `.bin` file from the phone (Settings → Firmware → Install from a file) still works without a router.

The website checks that its manifest and binary exist before it displays the flashing option, and requires explicit acknowledgment of the safety warning. Successful compilation alone does **not** establish suitability for a real engine network.

Before bumping the version, write `docs/releases/v<version>.md` with compatible hardware revisions, flashing notes, the change log and any settings/data migration information. The workflow attaches both binaries; `release.json` on Pages carries the update image checksum.

## GitHub Pages

Select **Settings → Pages → Build and deployment → Source: GitHub Actions**. The `.github/workflows/pages.yml` action then publishes the `site/` folder to `https://kaelor9.github.io/EngineLink/`.

Desktop Chromium browsers supporting Web Serial may flash over USB. iPhone Safari cannot use Web Serial firmware flashing but can access the unit's local SoftAP web UI.

## Important: full merged USB image overwrites user data

The 8 MB \`firmware.bin\` manifest entry at offset 0 covers NVS and LittleFS. It is for **new installations** only; leaving a separate Erase Flash option unchecked does **not** preserve those sectors when the entire image is written. For a previously configured EngineLink that has lost network access, use [partition-verified USB recovery](USB_RECOVERY.md) with the \`enginelink-update.bin\` application image and the backup-first \`tools/recover_usb.py\` script.
