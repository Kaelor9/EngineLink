#!/usr/bin/env python3
"""Guard against Wi-Fi driver polling and automatic channel scans during live app use.

These are source/architecture contracts, not evidence of RF performance.
"""
from pathlib import Path
from fwsource import firmware_source, body

ino=firmware_source()
network=body(ino,"void serviceNetwork(){")
tune=body(ino,"void linkTuneTask(void *){")

# All fast main-loop reads must share one timer/epoch guarded snapshot,
# not execute once per loop (the loop may run hundreds of times each second).
assert "radioCheckedEpoch!=networkIpEpoch" in network
assert "(uint32_t)(radioNow-radioCheckedAt)>=1000UL" in network
assert "const wifi_mode_t mode=WiFi.getMode();" in network
assert "routerIpReady=staInterfaceReady&&WiFi.status()==WL_CONNECTED" in network
assert "routerIp=routerIpReady?WiFi.localIP()" in network
assert "const bool hasIp=routerIpReady;" in network
assert "const bool mdnsInterfaceReady=(routerConfigured()&&!routerHold)?routerIpReady:apIpReady;" in network
assert "&&routerIpReady&&staIpAt" in network

# The initial calibration is just as disruptive as repeat calibration.
# It must not start during active app traffic.
assert "if(lastHttpMs&&(uint32_t)(millis()-lastHttpMs)<20000UL)continue;" in tune
assert "if(!fresh&&(uint32_t)(millis()-lastHttpMs)<20000UL)continue;" not in tune
# The 0.13.2 crash backtrace repeatedly reached ChunkOut::flush via
# handleExportRaw's compressed transfer on a slow link. Keep this particular
# export uncompressed until the compressor path is repaired and tested.
export=body(ino,"void handleExportRaw(){")
assert 'beginDownload("raw engine data","text/csv","EngineLink-raw-trips.csv",false)' in export

print("Router throughput and export stability source checks passed")
