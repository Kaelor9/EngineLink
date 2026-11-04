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
# (0.16.1, Wi-Fi from 0.12.15) assert "radioCheckedEpoch!=networkIpEpoch" in network
# (0.16.1, Wi-Fi from 0.12.15) assert "(uint32_t)(radioNow-radioCheckedAt)>=1000UL" in network
# (0.16.1, Wi-Fi from 0.12.15) assert "const wifi_mode_t mode=WiFi.getMode();" in network
# (0.16.1, Wi-Fi from 0.12.15) assert "routerIpReady=staInterfaceReady&&WiFi.status()==WL_CONNECTED" in network
# (0.16.1, Wi-Fi from 0.12.15) assert "routerIp=routerIpReady?WiFi.localIP()" in network
# (0.16.1, Wi-Fi from 0.12.15) assert "const bool hasIp=routerIpReady;" in network
# (0.16.1, Wi-Fi from 0.12.15) assert "const bool mdnsInterfaceReady=(routerConfigured()&&!routerHold)?routerIpReady:apIpReady;" in network
# (0.16.1, Wi-Fi from 0.12.15) assert "&&routerIpReady&&staIpAt" in network

# No radio tests in the background: the ping/channel test runs only after a
# speed test on the router, and never changes the transmit power.
assert "if(!radioTestWanted)continue;" in tune
assert "if(otaActive||httpBusySince)continue;" in tune
radio=body(ino,"void runRadioTest(uint8_t level) {")
assert "setTxPower" not in radio
assert "radioTestWanted=true" in body(ino,"void initializeWeb(){")
# All HTTP listeners, including the live API on port 81 and speed test on
# port 82, mark actual app traffic (used by the rescue return rule).
assert "lastHttpMs=millis();  // live API calls" in ino
assert "if(!parsed||uri()!=\"/api/health\")lastHttpMs=t0;" in ino, "The loopback self-check is not app traffic"
assert "lastHttpMs=millis();  // speed downloads" in ino

# The 0.13.2 crash backtrace repeatedly reached ChunkOut::flush via
# handleExportRaw's compressed transfer on a slow link. Keep this particular
# export uncompressed until the compressor path is repaired and tested.
export=body(ino,"void handleExportRaw(){")
assert 'beginDownload("raw engine data","text/csv","EngineLink-raw-trips.csv",false)' in export

# USB serial must never block (#141: 2.2 s per print with a non-reading
# host attached), and the daily SD clean-up runs in small steps (#141: 7.4 s).
setup=body(ino,"void setup(){")
assert setup.index("Serial.begin(115200);") < setup.index("Serial.setTxTimeoutMs(0);")
cleanup=body(ino,"void serviceRawCleanup(){")
assert "for(uint8_t k=0;k<4;k++)" in cleanup and "static File root;" in cleanup
assert "for(File f=root.openNextFile();" not in cleanup

print("Router throughput and export stability source checks passed")
