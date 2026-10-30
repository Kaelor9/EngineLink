# Recover an existing EngineLink by USB WITHOUT wiping fuel history

**Applies only to a previously working ESP32-S3 EngineLink board with the
standard Arduino `default_8MB` partition map.** Disconnect the engine buses
and NMEA 2000 for firmware recovery.

## The important distinction

The **regular USB Install button** writes the complete **8 MB merged firmware**
at address 0. That image covers the NVS, OTA metadata, and LittleFS partitions,
even when the installer does not perform a separate erase-flash operation.
**Do not use it on an existing EngineLink if you want to retain settings,
fuel totals, trips, calibration or internal historical records.**

The `enginelink-update.bin` file is **only the application image**.
If EngineLink's Wi-Fi is inaccessible, use `tools/recover_usb.py` instead.

## Windows recovery

1. Download `enginelink-update.bin` **v0.9.2 or newer** from
   [GitHub releases](https://github.com/Kaelor9/EngineLink/releases),
   and `tools/recover_usb.py` from this repository, into the same directory.
   Do **not** download `firmware.bin` (8 MB) for recovery.
2. Connect the EngineLink USB-C data port and find its COM port in Device Manager.
   Ensure no serial monitor is holding the COM port. If needed, put the ESP32-S3
   into ROM download mode using the documented board procedure.
3. Install Python 3.10+ and esptool 5.x:

   ```powershell
   py -m pip install --upgrade "esptool>=5,<6"
   ```

4. In PowerShell, from the directory containing both files, replace `COM7`
   with your actual device port:

   ```powershell
   py tools/recover_usb.py --port COM7 --firmware enginelink-update.bin
   ```

   If you downloaded `recover_usb.py` into the same directory directly,
   use `py recover_usb.py ...` instead.

5. Type `RECOVER`. The script **first reads back the real partition table**
   and refuses unexpected layouts. It then backs up NVS, OTA metadata and
   LittleFS to a time-stamped local directory, checks backup sizes, and only
   then writes `app0` at `0x10000`. Finally it erases **only**
   `otadata` (`0xE000`–`0xFFFF`) to make the bootloader select app0.
6. Keep all backup files. After restart, `v0.9.2` tries the saved router
   once; if connection fails for 45 seconds it restores standalone Wi-Fi and
   holds it across reboot. Join the saved EngineLink SSID and open
   `http://192.168.4.1`.

This procedure does not change NVS (0x9000–0xDFFF), app1
(0x340000–0x66FFFF) or LittleFS (0x670000–0x7FFFFF). The script does not erase
the whole chip, change the partition table or touch external microSD.

**Important:** Tested as host software, not yet performed on the owner's
actual custom board. If the USB port, flash chip, existing partitions or image
verification behaves unexpectedly, the script stops rather than guessing.
