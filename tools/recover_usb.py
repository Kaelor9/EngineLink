#!/usr/bin/env python3
"""Preserve EngineLink NVS/history when recovering a Wi-Fi-locked ESP32-S3.

Requires Python 3.10+, esptool 5.x, and enginelink-update.bin for the exact
EngineLink release. Intended ONLY for an existing EngineLink with the stock
Arduino default_8MB partition layout. Never writes the 8 MB merged image.
"""
import argparse
import datetime
import hashlib
import json
import struct
import subprocess
import sys
from pathlib import Path

PARTS = {
    "nvs": (0x9000, 0x5000),
    "otadata": (0xE000, 0x2000),
    "app0": (0x10000, 0x330000),
    "app1": (0x340000, 0x330000),
    "spiffs": (0x670000, 0x190000),
}
BACKUP = ("nvs", "otadata", "spiffs")


def partition_entries(raw: bytes):
    """Parse the real on-device 0x8000 partition table, not an assumed layout."""
    found = {}
    for i in range(0, len(raw) - 31, 32):
        magic, typ, sub, offset, size, label, flags = struct.unpack_from(
            "<HBBII16sI", raw, i
        )
        if magic == 0xFFFF:
            break
        if magic != 0x50AA:
            continue
        name = label.split(b"\0", 1)[0].decode("ascii", "replace")
        found[name] = (offset, size)
    return found


def verify_layout(raw: bytes):
    actual = partition_entries(raw)
    for name, expected in PARTS.items():
        if actual.get(name) != expected:
            raise RuntimeError(
                f"Partition {name}: expected {expected}, found {actual.get(name)}. "
                "Nothing has been written. This script is not compatible "
                "with this board layout."
            )


def verify_app(app: Path):
    if not app.is_file():
        raise RuntimeError(f"Firmware file not found: {app}")
    length = app.stat().st_size
    if length < 100_000 or length > PARTS["app0"][1]:
        raise RuntimeError(
            "Only the ~1-3 MB enginelink-update.bin is supported. "
            "Do NOT use the 8 MB merged firmware.bin."
        )
    with app.open("rb") as f:
        header = f.read(0x30)
    if len(header) < 0x30 or header[0] != 0xE9 or header[0x20:0x24] != b"\x32\x54\xCD\xAB":
        raise RuntimeError("Not an ESP32 application image; refusing to flash.")


def sha256(path: Path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for part in iter(lambda: f.read(65536), b""):
            h.update(part)
    return h.hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--port", required=True, help="ESP32-S3 serial port, e.g. COM7")
    p.add_argument("--firmware", required=True, type=Path,
                   help="Downloaded enginelink-update.bin, NOT firmware.bin")
    p.add_argument("--backup-dir", type=Path, help="Folder for full backups")
    p.add_argument("--dry-run", action="store_true", help="Verify local file only")
    args = p.parse_args()
    verify_app(args.firmware)
    folder = args.backup_dir or Path("EngineLink-rescue-backup-" +
                                    datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))
    print("Existing EngineLink default_8MB layout is REQUIRED.")
    print(f"Image: {args.firmware}  ({args.firmware.stat().st_size} bytes)")
    print("Plan: backup partition table, NVS, OTA metadata and LittleFS; "
          "flash ONLY OTA app0 at 0x10000; reset ONLY OTA metadata at 0xe000.")
    print("NVS 0x9000 and LittleFS 0x670000 will never be written.")
    if args.dry_run:
        print("Dry run complete; no device was accessed.")
        return

    answer = input("Type RECOVER to confirm non-erasing recovery: ").strip()
    if answer != "RECOVER":
        raise RuntimeError("Cancelled without changes.")
    folder.mkdir(parents=True, exist_ok=False)
    tool = [sys.executable, "-m", "esptool", "--chip", "esp32s3",
            "--port", args.port, "--baud", "115200"]

    def run(*cmd):
        print("Running:", " ".join(str(c) for c in cmd))
        subprocess.run(tool + list(map(str, cmd)), check=True)

    table = folder / "partition-table.bin"
    run("read-flash", "0x8000", "0x1000", table)
    verify_layout(table.read_bytes())
    print("Device partition layout verified.")
    hashes = {"partition-table": sha256(table)}
    for name in BACKUP:
        offset, size = PARTS[name]
        output = folder / f"{name}.bin"
        run("read-flash", hex(offset), hex(size), output)
        if output.stat().st_size != size:
            raise RuntimeError(f"Backup of {name} incomplete. Nothing written.")
        hashes[name] = sha256(output)
    (folder / "backup-checksums.json").write_text(
        json.dumps(hashes, indent=2) + "\n", encoding="utf-8"
    )
    print(f"BACKUPS COMPLETE: {folder.resolve()}")
    print("Writing application only. Never using erase-flash or full merged image.")
    run("write-flash", "0x10000", args.firmware.resolve())
    # The selected OTA image might currently be app1. Reset OTA selection
    # ONLY AFTER app0 was written successfully, so the bootloader selects app0.
    run("erase-region", "0xe000", "0x2000")
    print("Recovery complete. Keep the backup folder.")
    print("EngineLink should start its AP; if the boat router is still unreachable,")
    print("v0.9.2 returns to AP-only and remembers that rescue mode after 45 s.")
    print("Join the EngineLink SSID and open http://192.168.4.1")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, subprocess.CalledProcessError, OSError) as exc:
        print("RECOVERY STOPPED:", exc, file=sys.stderr)
        sys.exit(1)
