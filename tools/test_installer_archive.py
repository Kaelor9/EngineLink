#!/usr/bin/env python3
"""Safety and UI contracts for choosing older full USB images."""
import hashlib
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from build_installer_archive import IMAGE_BYTES, eligible, make_manifest, verified_binary


def release(tag, size=IMAGE_BYTES, state="uploaded"):
    return {
        "tag_name": tag, "published_at": "2026-11-02T12:00:00Z",
        "assets": [{"name": "firmware.bin", "size": size, "state": state}],
    }


class InstallerArchiveTests(unittest.TestCase):
    def test_only_prior_full_images_and_semver_order(self):
        releases = [
            release("v0.13.9"), release("v0.13.11"), release("v0.14.1"),
            release("v0.13.2", size=1000), release("v0.12.1"),
            release("../invalid"), release("v0.14.9"),
        ]
        actual = [x["v"] for x in eligible(releases, "0.14.1")]
        self.assertEqual(actual, ["0.13.11", "0.13.9", "0.12.1"])

    def test_manifest_installs_merged_image_at_zero(self):
        manifest = make_manifest("0.13.4")
        self.assertEqual(manifest["version"], "0.13.4")
        self.assertTrue(manifest["new_install_prompt_erase"])
        self.assertEqual(manifest["builds"][0]["chipFamily"], "ESP32-S3")
        self.assertEqual(manifest["builds"][0]["parts"],
                         [{"path": "firmware.bin", "offset": 0}])

    def test_image_size_and_digest(self):
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "firmware.bin"
            path.write_bytes(b"\xe9" + b"\x00" * (IMAGE_BYTES - 1))
            digest = "sha256:" + hashlib.sha256(path.read_bytes()).hexdigest()
            self.assertTrue(verified_binary(path, digest))
            self.assertFalse(verified_binary(path, "sha256:" + "0" * 64))
            self.assertFalse(verified_binary(path, "invalid:digest"))
            path.write_bytes(b"\x00" + path.read_bytes()[1:])
            self.assertFalse(verified_binary(path, ""))

    def test_site_picker_and_no_direct_release_binaries(self):
        html = Path("site/index.html").read_text(encoding="utf-8")
        js = Path("site/app.js").read_text(encoding="utf-8")
        workflow = Path(".github/workflows/pages.yml").read_text(encoding="utf-8")
        self.assertIn('id="firmware-version"', html)
        self.assertIn("versions/index.json", js)
        self.assertIn("versions/v' + version + '/manifest.json", js)
        self.assertIn("selectedReady && consent.checked", js)
        self.assertIn("binary.origin !== location.origin", js)
        self.assertIn("new URL(part.path, url)", js)
        self.assertIn("build_installer_archive.py", workflow)


if __name__ == "__main__":
    unittest.main()
