#!/usr/bin/env python3
"""Network architecture regression checks for EngineLink.
Static contracts complement hardware tests; they cannot prove RF stability.
"""
from pathlib import Path

ino = Path("firmware/EngineLink/EngineLink.ino").read_text(encoding="utf-8")
ui = Path("firmware/ui/app.html").read_text(encoding="utf-8")

def section(text, begin, end):
    assert text.count(begin) == 1, f"missing/duplicate: {begin}"
    return text.split(begin, 1)[1].split(end, 1)[0]

setup = section(ino, "void setup(){", "\nvoid loop(){")
apply_network = section(ino, "void applyNetwork(){", "\nvoid serviceWifiScan(){")
network = section(ino, "void serviceNetwork(){", "// ---------- Diagnosis")
forget = section(ino, "void handleWifiForget(){", "\nvoid handleSeasonReset(){")
speed_main = section(ino, "void handleSpeedTest(){", "\n// ---------- Backup")
speed_worker = section(ino, "void speedHttpTask(void *){", "\nvoid liveHttpTask(void *){")
network_api = section(ino, "void handleWifiScan(){", "\nvoid handleWifiConnect(){")

# The two user-selected normal operating modes are exclusive.
assert "WiFi.mode(WIFI_AP);" in apply_network
assert "WiFi.mode(WIFI_STA);" in apply_network
assert "WiFi.mode(WIFI_AP_STA)" not in apply_network
assert "routerConfigured()&&!routerHold" in apply_network
assert "WiFi.onEvent(onWiFi);" in setup
assert setup.index("applyNetwork();") < setup.index("LittleFS.begin(")
assert setup.index("applyNetwork();") < setup.index("initSD();")

# The browser receives a valid reply before its network interface goes away.
assert "reply(true," in forget
assert "esp_wifi_disconnect()" not in forget
assert "WiFi.disconnect(" not in forget
assert "pendingNetwork=true" in forget

# Association attempts are event driven; don't repeatedly cancel a live auth.
assert "staDropEvents!=routerSeenDropEvents" in network
assert "ROUTER_AUTH_RETRY_DELAY_MS" in network
assert "routerResetPending" in network
assert "RouterGuard::CONNECT_TIMEOUT_MS" in Path("firmware/EngineLink/routerguard.h").read_text() or "120000UL" in Path("firmware/EngineLink/routerguard.h").read_text()

# A 32KiB network test previously froze the normal HTTP loop for 33s.
assert "ChunkOut" not in speed_main and "beginDownload(" not in speed_main
assert "speedServer.accept()" in speed_worker
assert "Content-Length: 32768" in speed_worker
assert 'url.port=\'82\'' in ui
assert "NetworkServer speedServer(82);" in ino

# The browser must discover hosts in parallel, not one by one.
discovery = section(ui, "async function findHost(){", "\n// Live samples use")
assert "Promise(resolve=>" in discovery
assert "for(const host of hosts)" in discovery
assert "const r=await fetch(host+" in discovery
assert "discoveryPending" in discovery
assert "1800" in discovery

# Access point discovery should remain on-demand only, never at every boot.
assert "scanRequested=true" in network_api
assert "WiFi.scanNetworks(" not in network_api
print("network architecture checks passed")
