#!/usr/bin/env python3
"""Regressions for the iPhone home-screen tab bar and router text fields."""
from pathlib import Path

app = Path("firmware/ui/app.html").read_text(encoding="utf-8")

# The navigation must grow with its safe-area padding. A fixed 72px total
# height and 34px home-indicator padding left too little room for 26px icons
# and text, making the outer tabs appear cropped.
assert "min-height:calc(60px + max(10px,env(safe-area-inset-bottom),var(--home-bar,0px)))" in app
assert "nav button{" in app and "min-height:56px" in app
assert "html.fill #app" not in app
assert "--app-h" not in app

# Password fields need to shrink inside the two-column router picker.
assert ".field{min-height:46px;min-width:0;" in app
assert "grid-template-columns:minmax(0,1fr) auto;gap:8px;padding:0 0 12px" in app

# Live API responses and Wi-Fi scan polling can force render(true). Neither
# should replace a focused router text field (Safari loses cursor and keyboard).
assert "if(S.page==='router'&&['wifiPass','wifiManualSsid','wifiManualPass'].includes(a.dataset.i))return;" in app

for name in ("wifiPass", "wifiManualPass"):
    field = next(line for line in app.splitlines() if 'data-i="' + name + '"' in line)
    for flag in ('autocapitalize="off"', 'autocorrect="off"', 'spellcheck="false"', 'autocomplete="off"'):
        assert flag in field, (name, flag)

print("iPhone navigation and router input source contracts OK")
