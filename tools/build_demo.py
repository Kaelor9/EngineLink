#!/usr/bin/env python3
"""Builds the browser demo of the phone app into site/demo/.

    python3 tools/build_demo.py

site/demo/app.html is firmware/ui/app.html with a simulated EngineLink
(tools/demo/mock.js) in front of it and a small remote control
(tools/demo/control.js) after it. site/demo/index.html shows it in a phone
frame (tools/demo/page.html). The Pages workflow runs this, so the demo always
shows the app of the published firmware. The output is not committed.
"""
import pathlib, re

root = pathlib.Path(__file__).resolve().parent.parent
demo = root / "tools/demo"
out = root / "site/demo"

version = re.search(r'^#define EL_VERSION "(.*)"', (root / "firmware/EngineLink/EngineLink.ino").read_text(), re.M).group(1)
app = (root / "firmware/ui/app.html").read_text()
mock = (demo / "mock.js").read_text().replace("__VERSION__", version)
control = (demo / "control.js").read_text()

head = '<meta charset="utf-8">'
assert app.count(head) == 1 and app.count("</body>") == 1, "app.html layout changed"
app = app.replace(head, head + '\n<meta name="robots" content="noindex">\n<script>\n' + mock + "\n</script>", 1)
app = app.replace("</body>", "<script>\n" + control + "\n</script>\n</body>", 1)

out.mkdir(parents=True, exist_ok=True)
(out / "app.html").write_text(app)
(out / "index.html").write_text((demo / "page.html").read_text().replace("__VERSION__", version))
print(f"Wrote site/demo (app {version}, {len(app)} bytes)")
