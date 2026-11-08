#!/usr/bin/env python3
"""Fetch the pinned ESP32 core's archives into arduino-cli's download cache
before `arduino-cli core install`, so one unreachable GitHub release asset
does not fail CI (2026-11-08: every runner got 504 for the RISC-V toolchain,
which the ESP32-S3 build does not even use, for over an hour).

For each archive the platform needs on this host: keep it if the cache
already holds it with the index's SHA-256; else download it, a github.com
release asset first from Espressif's official mirror
(dl.espressif.com/github_assets/...), then from the index's URL. Every file
is checked against the index's size and SHA-256 before it is kept, so the
source never changes what is installed; arduino-cli checks it again.
Two tries per source, no more: a dead source fails fast.

Usage: prefetch_arduino_tools.py [--version 3.3.12] [--only NAME ...]
"""
import argparse
import hashlib
import json
import os
import sys
import time
import urllib.request
from pathlib import Path

ARDUINO = Path(os.path.expanduser("~/.arduino15"))
HOST = "x86_64-pc-linux-gnu"
MIRROR = "https://dl.espressif.com/github_assets/"


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def sources(url: str):
    # The mirror first: GitHub's release downloads were the part that failed
    # (504 on 2026-11-08); the checksum makes the order irrelevant to content.
    if url.startswith("https://github.com/") and "/releases/download/" in url:
        yield MIRROR + url[len("https://github.com/"):]
    yield url


def fetch(url: str, dest: Path, size: int, digest: str, tries: int = 2) -> bool:
    for src in sources(url):
        for attempt in range(1, tries + 1):
            part = dest.with_suffix(dest.suffix + ".part")
            try:
                with urllib.request.urlopen(src, timeout=60) as r, part.open("wb") as f:
                    while chunk := r.read(1 << 20):
                        f.write(chunk)
                if part.stat().st_size == size and sha256(part) == digest:
                    part.replace(dest)
                    print(f"  ok from {src.split('/')[2]}")
                    return True
                print(f"  {src.split('/')[2]}: wrong size or checksum, discarded")
            except Exception as e:  # network errors: try again, then the next source
                print(f"  {src.split('/')[2]} try {attempt}: {e}")
            part.unlink(missing_ok=True)
            time.sleep(5)
    return False


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", default="3.3.12")
    ap.add_argument("--index", default=str(ARDUINO / "package_esp32_index.json"))
    ap.add_argument("--staging", default=str(ARDUINO / "staging" / "packages"))
    ap.add_argument("--only", nargs="*", help="archive file names to fetch (default: all the platform needs)")
    a = ap.parse_args()

    pkg = next(p for p in json.load(open(a.index))["packages"] if p["name"] == "esp32")
    plat = next(p for p in pkg["platforms"] if p["version"] == a.version)
    wanted = [(plat["archiveFileName"], plat["url"], int(plat["size"]), plat["checksum"])]
    for dep in plat["toolsDependencies"]:
        if dep["packager"] != "esp32":
            continue  # tools from other packagers (dfu-util) come from downloads.arduino.cc
        tool = next(t for t in pkg["tools"] if t["name"] == dep["name"] and t["version"] == dep["version"])
        system = next((s for s in tool["systems"] if s["host"] == HOST), None)
        if system:
            wanted.append((system["archiveFileName"], system["url"], int(system["size"]), system["checksum"]))

    staging = Path(a.staging)
    staging.mkdir(parents=True, exist_ok=True)
    missing = 0
    for name, url, size, checksum in wanted:
        if a.only and name not in a.only:
            continue
        algo, _, digest = checksum.partition(":")
        assert algo == "SHA-256", f"{name}: unexpected checksum {checksum}"
        dest = staging / name
        if dest.exists() and dest.stat().st_size == size and sha256(dest) == digest.lower():
            print(f"{name}: cached")
            continue
        print(f"{name}: {size / 1048576:.0f} MB")
        if not fetch(url, dest, size, digest.lower()):
            print(f"{name}: not available from any source")
            missing += 1
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
