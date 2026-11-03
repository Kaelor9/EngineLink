#!/usr/bin/env python3
"""Publish previous complete USB images as same-origin ESP Web Tools manifests.

Run inside the GitHub Pages workflow after creating /tmp/releases.json:
    python3 tools/build_installer_archive.py /tmp/releases.json --current 0.14.1

GitHub Release download links are not suitable as ESP Web Tools manifest parts
without browser CORS guarantees. Serve the binaries from Pages instead.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

IMAGE_BYTES = 8 * 1024 * 1024
# GitHub Pages has a 1 GB deployed-site limit. 90 full images use 720 MiB.
MAX_OLDER = 90
VERSION_RE = re.compile(r"^v(\d+)\.(\d+)\.(\d+)(?:-([a-z0-9.-]+))?$")


def version_key(tag):
    match = VERSION_RE.fullmatch(tag)
    if not match:
        return None
    a, b, c, pre = match.groups()
    return (int(a), int(b), int(c), 0 if pre else 1, pre or "")


def eligible(releases, current):
    """Select only actual full 8 MB merged ESP32 images, newest first."""
    candidates = []
    seen = set()
    current_key = version_key("v" + current)
    if current_key is None:
        raise ValueError("Invalid current version")
    for rel in releases:
        if rel.get("draft"):
            continue
        tag = rel.get("tag_name", "")
        key = version_key(tag)
        if key is None or key >= current_key or tag in seen:
            continue
        asset = next(
            (a for a in rel.get("assets", [])
             if a.get("name") == "firmware.bin" and
             a.get("state", "uploaded") == "uploaded" and
             a.get("size") == IMAGE_BYTES),
            None,
        )
        if asset is None:
            continue
        seen.add(tag)
        candidates.append({
            "tag": tag,
            "v": tag[1:],
            "date": (rel.get("published_at") or rel.get("created_at") or "")[:10],
            "digest": asset.get("digest") or "",
        })
    candidates.sort(key=lambda r: version_key(r["tag"]), reverse=True)
    return candidates[:MAX_OLDER]


def make_manifest(version):
    return {
        "name": "EngineLink App (alpha)",
        "version": version,
        "new_install_prompt_erase": True,
        "builds": [{
            "chipFamily": "ESP32-S3",
            "parts": [{"path": "firmware.bin", "offset": 0}],
        }],
    }


def verified_binary(path, digest):
    if not path.is_file() or path.stat().st_size != IMAGE_BYTES:
        return False
    with path.open("rb") as fh:
        if fh.read(1) != b"\xe9":  # ESP32 bootloader at flash address zero
            return False
    if digest:
        if not re.fullmatch(r"sha256:[0-9a-fA-F]{64}", digest):
            return False
        expected = digest.partition(":")[2].lower()
        h = hashlib.sha256()
        with path.open("rb") as fh:
            for block in iter(lambda: fh.read(1024 * 1024), b""):
                h.update(block)
        return h.hexdigest() == expected
    return True


def download(tag, destination, digest):
    destination.mkdir(parents=True, exist_ok=True)
    binary = destination / "firmware.bin"
    for attempt in range(3):
        try:
            subprocess.run(
                ["gh", "release", "download", tag,
                 "--pattern", "firmware.bin", "--dir", str(destination),
                 "--clobber"],
                check=True, capture_output=True, text=True, timeout=150,
            )
            if verified_binary(binary, digest):
                return True
            print(f"Invalid image or SHA256 for {tag} (attempt {attempt + 1})", flush=True)
        except (subprocess.SubprocessError, OSError) as exc:
            print(f"Download error for {tag} (attempt {attempt + 1}): {exc}", flush=True)
        binary.unlink(missing_ok=True)
        if attempt < 2:
            time.sleep(3 * (attempt + 1))
    return False


def build(releases, current, out):
    out.mkdir(parents=True, exist_ok=True)
    candidates = eligible(releases, current)
    published = []
    for entry in candidates:
        folder = out / entry["tag"]
        if not download(entry["tag"], folder, entry["digest"]):
            print(f"Warning: skipping unavailable historical image {entry['tag']}", flush=True)
            continue
        (folder / "manifest.json").write_text(
            json.dumps(make_manifest(entry["v"]), indent=2) + "\n", encoding="utf-8"
        )
        published.append({"v": entry["v"], "date": entry["date"]})
    if candidates and not published:
        raise RuntimeError("All historical firmware downloads failed; refusing an empty archive")
    (out / "index.json").write_text(
        json.dumps({"versions": published}, indent=2) + "\n", encoding="utf-8"
    )
    print(f"Archived {len(published)} of {len(candidates)} older USB releases", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("releases_json", type=Path)
    parser.add_argument("--current", required=True)
    parser.add_argument("--out", type=Path, default=Path("site/versions"))
    args = parser.parse_args()
    build(json.loads(args.releases_json.read_text(encoding="utf-8")),
          args.current, args.out)
