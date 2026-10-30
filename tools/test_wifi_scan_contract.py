#!/usr/bin/env python3
"""Static regression checks for EngineLink's non-blocking 2.4 GHz discovery.

Actual radio reliability and channel coverage require testing the ESP32-S3 PCB.
"""
from pathlib import Path
import re

ino = Path("firmware/EngineLink/EngineLink.ino").read_text(encoding="utf-8")
ui = Path("firmware/ui/app.html").read_text(encoding="utf-8")

def body(name, next_name):
    return ino.split(name, 1)[1].split(next_name, 1)[0]

api = body("void handleWifiScan(){", "void handleWifiConnect(){")
service = body("void serviceWifiScan(){", "void serviceNetwork(){")
capture = body("void captureWifiScan(int16_t total){", "// ---------- Wi-Fi: exclusive")
assert "WiFi.mode(" not in api and "WiFi.scanNetworks(" not in api, (
    "HTTP scan handler must acknowledge before manipulating the radio"
)
assert "scanNextAt=millis()+600UL" in api, "No response grace before scan starts"
assert "scanEntriesCount=0" in api, "Scan must clear old results at the start"
assert "SCAN_CHANNEL_FIRST=1,SCAN_CHANNEL_LAST=13" in ino
assert "SCAN_DWELL_MS=220" in ino, "Avoid tiny per-channel listen windows"
assert "WiFi.scanNetworks(true,false,false,SCAN_DWELL_MS,scanChannel)" in service
assert "scanNextAt=millis()+SCAN_GAP_MS" in service
assert "captureWifiScan(n)" in service
assert "scanEntriesCount=0" not in capture, "Channel results must accumulate"
assert "WiFi.scanDelete()" in capture
assert "scanRestoreAt=millis()+15000UL" in service
assert "scanFailures" in api and "scanDurationMs" in api
assert "Checking channel " in ui and "n<30" in ui
assert not re.search(r"WiFi\.scanNetworks\(false", ino), (
    "A blocking scan has reappeared in the main firmware task"
)
print("Wi-Fi scan regression checks passed")
