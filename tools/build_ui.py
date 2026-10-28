#!/usr/bin/env python3
"""Builds firmware/EngineLink/ui.h from firmware/ui/app.html.

The phone app is one HTML file (markup, styles and script). It is stored
gzip-compressed in flash and sent with Content-Encoding: gzip, which makes
it about four times smaller over Wi-Fi.

    python3 tools/build_ui.py          # write ui.h
    python3 tools/build_ui.py --check  # fail if ui.h is out of date (CI)
"""
import gzip, sys, pathlib

root = pathlib.Path(__file__).resolve().parent.parent
src = root / "firmware/ui/app.html"
dst = root / "firmware/EngineLink/ui.h"

html = src.read_bytes()
# mtime=0 keeps the output identical for identical input.
gz = gzip.compress(html, compresslevel=9, mtime=0)
rows = [", ".join("0x%02x" % b for b in gz[i:i + 24]) for i in range(0, len(gz), 24)]
text = (
    "#pragma once\n"
    "// Engine Link phone app, built from firmware/ui/app.html by tools/build_ui.py.\n"
    "// Do not edit by hand: change app.html and run the script.\n"
    "#include <pgmspace.h>\n"
    f"// {len(html)} bytes of HTML, {len(gz)} bytes compressed.\n"
    "static const uint8_t ENGINE_UI_GZ[] PROGMEM = {\n" + ",\n".join(rows) + "\n};\n"
    "static const size_t ENGINE_UI_GZ_LEN = sizeof(ENGINE_UI_GZ);\n"
)
if "--check" in sys.argv:
    # Compare the unpacked bytes, so a different zlib version cannot fail it.
    import re
    old = dst.read_text() if dst.exists() else ""
    body = old.split("{", 1)[1].split("}", 1)[0] if "{" in old else ""
    packed = bytes(int(h, 16) for h in re.findall(r"0x([0-9a-f]{2})", body))
    try:
        same = gzip.decompress(packed) == html
    except Exception:
        same = False
    if not same:
        sys.exit("ui.h is out of date: run python3 tools/build_ui.py")
    print("ui.h is up to date")
else:
    dst.write_text(text)
    print(f"Wrote {dst.relative_to(root)}: {len(html)} -> {len(gz)} bytes")
