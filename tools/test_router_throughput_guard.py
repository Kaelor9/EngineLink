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
assert "if(otaActive||webXferBusy())continue;" in tune
radio=body(ino,"void runRadioTest(uint8_t level) {")
assert "setTxPower" not in radio
assert "radioTestWanted=true" in body(ino,"void initializeWeb(){")
# Downloads are sent uncompressed since Wi-Fi 2.0 Stage 2 (plan D26): the
# 0.13.2 crash backtrace went through the deflate path, which is gone; the
# contract file (test_wifi_contract.py) checks that it stays gone.

# USB serial must never block (#141: 2.2 s per print with a non-reading
# host attached), and the daily SD clean-up runs in small steps (#141: 7.4 s),
# now in the shared card walk with the engine lock let go (test_sd_engine_lock.py).
setup=body(ino,"void setup(){")
assert setup.index("Serial.begin(115200);") < setup.index("Serial.setTxTimeoutMs(0);")
cleanup=body(ino,"void serviceSdWalk(){")
assert "for(uint8_t k=0;k<4;k++)" in cleanup and "static File walkRoot;" in ino
assert "for(File f=walkRoot.openNextFile();" not in cleanup

print("Router throughput and export stability source checks passed")
