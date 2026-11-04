#!/usr/bin/env python3
"""Contract for the simple Wi-Fi (0.16.2). Static checks; not a radio test."""
import re
from pathlib import Path
w = Path("firmware/EngineLink/wifi.ino").read_text(encoding="utf-8")
def fn(sig):
    i = w.index(sig); return w[i:w.index("\n}\n", i)]
apply, net, ap = fn("void applyNetwork(){"), fn("void serviceNetwork(){"), fn("void startAP(){")
# Own Wi-Fi: fixed address, the saved name and password, up to 6 phones.
assert "IPAddress(192,168,4,1)" in ap
assert "WiFi.softAP(cfg.apName.c_str(),cfg.apPassword.c_str(),apChannelNow,false,6)" in ap
# The two modes are exclusive: AP alone or router alone.
assert "WiFi.mode(WIFI_AP);" in apply and "WiFi.mode(WIFI_STA);" in apply
assert "WiFi.mode(WIFI_AP_STA)" not in apply
# Router not reached in 2 minutes: back to the own Wi-Fi (offline first).
assert "120000UL" in net and "routerHold=true" in net
# Nothing blocks the loop: no delay(), scans are asynchronous.
assert "delay(" not in w
assert "WiFi.scanNetworks(true," in w
# Never logs the Wi-Fi passwords.
assert not re.search(r"printf\([^;]*(apPassword|wifiPassword)", w)
assert len(w.splitlines()) < 300, "keep the Wi-Fi simple"
print("simple Wi-Fi checks passed")
