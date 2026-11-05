#!/usr/bin/env python3
"""Wi-Fi 2.0 structural contracts (docs/WIFI_REWRITE_PLAN.md 9.10).

Only what a host test cannot execute: lessons a build or a code review can
break. Items are added stage by stage; the Stage 4 items (state machine in
wifi.ino) follow when that stage is built."""
import re
from pathlib import Path

from fwsource import FW, body, firmware_source

ROOT = FW.parent.parent
fw = firmware_source()
w = (FW / "wifi.ino").read_text(encoding="utf-8")

# --- Stage 1 -------------------------------------------------------------
# 1a: the AP radio settings every version before 0.16.2 had (F19), D27 country.
ap = body(w, "void apRadioSetup(){")
for call in ("esp_wifi_set_bandwidth(WIFI_IF_AP,WIFI_BW_HT20)",
             "esp_wifi_set_inactive_time(WIFI_IF_AP,600)",
             'esp_wifi_set_country_code("DK",true)',
             "WiFi.setSleep(false)", "applyTxPower(false)"):
    assert call in ap, f"apRadioSetup() lacks {call}"
assert ap.index('set_country_code("DK"') < ap.index("applyTxPower(false)"), "TX power after the country (0.15.0 lesson)"
assert '"EU"' not in fw, "D27: a real ISO country code, never EU"
# startAP keeps the 0.16.5 line exactly (D2) and runs apRadioSetup.
sa = body(w, "void startAP(){")
assert "WiFi.softAPConfig(IPAddress(192,168,4,1),IPAddress(192,168,4,1),IPAddress(255,255,255,0))" in sa
assert "WiFi.softAP(cfg.apName.c_str(),cfg.apPassword.c_str(),apChannelNow,false,6)" in sa
assert "apRadioSetup()" in sa
# 1b: TX power truth (F7): exact compare, set in router mode too.
tx = body(w, "void applyTxPower(bool check){")
assert "abs(" not in tx and "!=ESP_OK" in tx and "==(int8_t)elTxPower[txLevel]" in tx
assert "staRadioSetup()" in body(w, "void applyNetwork(){")
# 1c: the trace folds through the host-tested function.
assert "wifistate::traceFold(" in body(w, "void traceWifi(uint8_t type,uint8_t reason){")

# --- Section 4: no third exception ---------------------------------------
for p in sorted(FW.glob("*")):
    if p.suffix not in (".ino", ".h", ".cpp"):
        continue
    t = p.read_text(encoding="utf-8")
    if "lwip/priv" in t:
        assert p.name == "netboost.cpp", f"lwip/priv used in {p.name}"
    if "__wrap_" in t:
        assert p.name == "wifiwpa3.cpp", f"__wrap_ used in {p.name}"

# --- Secrets never printed -----------------------------------------------
assert not re.search(r"print[f]?\([^;]*(apPassword|wifiPassword|ghToken)", fw), "never log a secret"
assert "setTxTimeoutMs(0)" in body(fw, "void setup(){")

# --- D25: pinned toolchain -----------------------------------------------
for wf in ("build-engine.yml", "pages.yml"):
    t = (ROOT / ".github" / "workflows" / wf).read_text(encoding="utf-8")
    assert "esp32:esp32@3.3.12" in t, f"{wf}: core not pinned (D25)"
    assert '"Adafruit NeoPixel@' in t, f"{wf}: NeoPixel not pinned (D25)"

# --- Pure headers stay pure ------------------------------------------------
for h in ("wifistate.h",):
    t = (FW / h).read_text(encoding="utf-8")
    assert "#include <Arduino.h>" not in t and "WiFi" not in t and "millis(" not in t, h

print("Wi-Fi contract checks passed")
