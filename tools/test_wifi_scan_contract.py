#!/usr/bin/env python3
"""Static regression checks for EngineLink's non-blocking 2.4 GHz discovery.

Actual radio reliability and channel coverage require testing the ESP32-S3 PCB.
"""
from pathlib import Path
import re

import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from fwsource import firmware_source, body as fn

ino = firmware_source()
ui = Path("firmware/ui/app.html").read_text(encoding="utf-8")

def body(name, next_name):
    return ino.split(name, 1)[1].split(next_name, 1)[0]

api = fn(ino, "void handleWifiScan(){")
service = fn(ino, "void serviceWifiScan(){")
capture = fn(ino, "void captureWifiScan(int16_t total){")
assert "WiFi.mode(" not in api and "WiFi.scanNetworks(" not in api, (
    "HTTP scan handler must acknowledge before manipulating the radio"
)
assert "scanNextAt=millis()+600UL" in api, "No response grace before scan starts"
assert "scanEntriesCount=0" in api, "Scan must clear old results at the start"
assert "SCAN_CHANNEL_FIRST=1,SCAN_CHANNEL_LAST=13,SCAN_PASSES=2" in ino
assert "SCAN_PASSIVE_DWELL_MS=300" in ino
assert "WiFi.setScanActiveMinTime(0)" in service, "Use the full active dwell instead of Arduino's default 100 ms"
assert "WiFi.setScanActiveMinTime(100)" in service, "Restore scanner defaults after completion"
assert "SCAN_DWELL_MS=220" in ino, "Avoid tiny per-channel listen windows"
assert "WiFi.scanNetworks(true,false,passive," in service
assert "passive?SCAN_PASSIVE_DWELL_MS:SCAN_DWELL_MS,scanChannel" in service
assert "scanPass==SCAN_PASSES" in service and "advanceScanChannel();" in service
assert "scanPassOneCount=scanEntriesCount" in ino
assert "scanNextAt=millis()+SCAN_GAP_MS" in service
assert "captureWifiScan(n)" in service
assert "scanEntriesCount=0" not in capture, "Channel results must accumulate"
assert "WiFi.scanDelete()" in capture
assert "scanRestoreAt=millis()+15000UL" in service
assert "scanFailures" in api and "scanDurationMs" in api
assert "Scan ${sc.pass||1}/${sc.passes||2}" in ui and "n<45" in ui
assert 'data-i="wifiManualSsid"' in ui and 'data-i="wifiManualPass"' in ui
assert "wifiManualConnect" in ui and "startRouterConnection(ssid,S.wifiManualPass)" in ui
assert "firstPassNets" in api
assert "scanFirstPassNets" in ino
assert not re.search(r"WiFi\.scanNetworks\(false", ino), (
    "A blocking scan has reappeared in the main firmware task"
)
print("Wi-Fi scan regression checks passed")
