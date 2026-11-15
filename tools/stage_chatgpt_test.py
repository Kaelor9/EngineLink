#!/usr/bin/env python3
"""Stage isolated ChatGPT lab installer artifacts without touching production."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil

TEST_VERSION = "0.0.1-chatgpt-test"
TEST_NAME = "EngineLink ChatGPT TEST (UNVALIDATED)"


def stage(build: Path, dest: Path, version: str = TEST_VERSION, ota_name: str = 'enginelink-update-CHATGPT-TEST.bin') -> None:
    merged = list(build.glob("*.merged.bin"))
    app = build / "EngineLink.ino.bin"
    if len(merged) != 1 or merged[0].stat().st_size != 8 * 1024 * 1024:
        raise RuntimeError("Expected exactly one complete ESP32-S3 8 MiB image")
    if not app.exists() or not (100_000 < app.stat().st_size < 3 * 1024 * 1024):
        raise RuntimeError("Not an ESP32-S3 application OTA image")
    header = app.open("rb").read(0x70)
    if header[0] != 0xE9 or header[0x20:0x24] != bytes.fromhex("32 54 cd ab"):
        raise RuntimeError("Missing ESP32 application descriptor")
    project = header[0x50:0x70].split(b"\0", 1)[0]
    if project not in (b"arduino-lib-builder", b"EngineLink", b"EngineLink.ino"):
        raise RuntimeError(f"Unexpected ESP application name: {project!r}")
    # Espressif Arduino 3.3.x uses the shared project name arduino-lib-builder.
    # This verifies compatibility, not that an image is genuinely EngineLink.
    print(f"ESP32 project_name: {project!r}")
    dest.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(merged[0], dest / "firmware.bin")
    shutil.copyfile(app, dest / ota_name)
    manifest = {
        "name": TEST_NAME,
        "version": version,
        "new_install_prompt_erase": True,
        "builds": [{"chipFamily": "ESP32-S3",
                    "parts": [{"path": "firmware.bin", "offset": 0}]}],
    }
    (dest / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    digest = hashlib.sha256(app.read_bytes()).hexdigest()
    (dest / "sha256.txt").write_text(
        digest + "  " + ota_name + "\n"
    )
    print(f"Staged TEST ONLY {version} from {app}; SHA256={digest}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--dest", type=Path, required=True)
    parser.add_argument("--version", default=TEST_VERSION)
    parser.add_argument("--ota-name", default="enginelink-update-CHATGPT-TEST.bin")
    args = parser.parse_args()
    stage(args.build, args.dest, args.version, args.ota_name)
