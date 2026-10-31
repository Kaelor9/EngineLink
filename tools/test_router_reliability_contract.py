#!/usr/bin/env python3
"""Static safety contracts for router STA lifecycle and HTTP rebind logic.

Source checks cannot validate RF behavior. Real ESP32-S3 router and phone tests
are required before treating a green build as proof of connectivity.
"""
from pathlib import Path
import re

ino=Path("firmware/EngineLink/EngineLink.ino").read_text(encoding="utf-8")
ui=Path("firmware/ui/app.html").read_text(encoding="utf-8")

def section(start,end):
    assert ino.count(start)==1, "Missing or ambiguous section: "+start
    return ino.split(start,1)[1].split(end,1)[0]

network=section("void serviceNetwork(){","// ---------- Diagnosis")
apply=section("void applyNetwork(){","// On each completed channel")
teardown=section("void parkRouter(const char *why){","void applyNetwork(){")
callback=section("void onWiFi(WiFiEvent_t e,WiFiEventInfo_t info){","bool cardInserted(){")
health=section("String makeNetworkHealthJson(){","// Independent, read-only live")
workers=section("void refreshWorkerListener(NetworkServer &server,uint32_t &boundEpoch){","void speedHttpTask(void *){")
web=section("class EngineLinkWebServer : public WebServer {","struct Frame {")
routes=section("void initializeWeb(){","void serialStatus(){")

# Single, exclusive operation modes.
assert "WiFi.mode(WIFI_AP);" in apply
assert "WiFi.mode(WIFI_STA);" in apply
assert "WiFi.mode(WIFI_AP_STA)" not in apply
assert "WiFi.setAutoReconnect(true);" in apply
assert "WiFi.setAutoReconnect(false);" in apply
assert "routerGuard.expired(now)" in network and "parkRouter(" in network
assert "120000UL" in Path("firmware/EngineLink/routerguard.h").read_text()

# Driver may retry without forcing disconnect; the application may not
# restart Wi-Fi negotiations from inside the 100Hz main service loop.
assert "esp_wifi_connect()" in network
assert "WiFi.reconnect()" not in re.sub(r"//[^\n]*","",network)
assert not re.search(r"(?m)^\s*(WiFi\.begin|WiFi\.disconnect|esp_wifi_disconnect)\(", network)

# Treat DHCP and STA association as separate stages of readiness.
assert "ARDUINO_EVENT_WIFI_STA_CONNECTED" in callback
assert "ARDUINO_EVENT_WIFI_STA_GOT_IP" in callback
assert "ARDUINO_EVENT_WIFI_STA_LOST_IP" in callback
assert "staDhcpMs=now-staAssociatedAt" in callback
assert "staJoinMs=now-from" in callback

# Only rebind after interface is ready and once per IP epoch, in the loop.
assert "web.rebindNetwork();" in network
assert "networkIpEpoch!=httpBoundEpoch" in network
assert "mdnsInterfaceReady" in network
assert "(uint32_t)(mdnsNow-networkIpEventAt)>=600UL" in network
assert "WebServer::begin();" in web
assert "slot.c.stop()" in web
assert "server.end();" in workers and "server.begin();" in workers
assert "refreshWorkerListener(speedServer,boundEpoch)" in ino
assert "refreshWorkerListener(liveServer,boundEpoch)" in ino

# If rescue is used, do not overwrite the true failure reason with a
# voluntary station-disconnect event emitted while stopping the STA.
assert teardown.index("routerFailureReason=")<teardown.index("routerIntentionalShutdown=true")
assert teardown.index("routerFailurePhase=")<teardown.index("WiFi.disconnect(")
assert 'p.putUChar("rrsn",routerFailureReason)' in teardown
assert 'pref.getUChar("rrsn",0)' in ino
assert "staLastUnexpectedReason" in callback
assert "routerIntentionalShutdown" in callback

# Diagnostic endpoint works without accessing engine data or the SD card.
assert 'web.on("/api/network-health",HTTP_GET,handleNetworkHealth);' in routes
for key in ["associationMs","dhcpMs","failedPhase","driverReason","httpRebinds","phase"]:
    assert f'\\\"{key}\\\"' in health or f'\"{key}\"' in health,key
assert "makeLiveJson" not in health
assert "fsSlurp" not in health and "SD." not in health
assert 'web.on("/api/network-report",HTTP_GET,handleNetworkReport);' in routes
assert 'Open readable network report' in ui
assert 'ROUTER_TRACE_CAP=64' in ino and 'getWifiTrace(trace)' in health
assert 'Router IP now:' in health and 'Last successful router IP:' in health
assert 'Last saved failure:' in health and 'MACHINE-READABLE JSON' in health
assert 'wifiPassword' not in health and 'wpass' not in health
assert 'Unexpected router drops' in ui
assert "202:'authentication failed (" in ui
# Router WPA3 settings must be applied before starting the association.
connect=section("void connectRouter(){","// A name for http://")
assert "WiFi.begin(cfg.ssid.c_str(),password,0,nullptr,false)" in connect
assert "WPA3_SAE_PWE_BOTH" in connect
assert "staCfg.sta.pmf_cfg.required=false" in connect
assert "memset(staCfg.sta.sae_h2e_identifier,0" in connect
assert connect.index("esp_wifi_set_config(") < connect.index("esp_wifi_connect()")
assert "WiFi.begin(cfg.ssid.c_str(),password);" in connect, "Failed security setup must not strand STA"
assert "staNegotiatedAuth=(uint8_t)info.wifi_sta_connected.authmode;" in callback
assert '"Last negotiated Wi-Fi security: "' in health
assert "routerAuthMode" in ino
assert "No current connection error" in ui
assert "authentication timeout (router reply not received)" in ui
# The speed test remains 32 KiB but no longer emits tiny 256-byte packets
# with a task delay on every packet.
speed=section("void speedHttpTask(void *){","void liveHttpTask(void *){")
assert "sample[1460]" in speed
assert "Content-Length: 32768" in speed
assert "if((++blocks&3)==0)vTaskDelay(1);" in speed

print("router lifecycle and network diagnostics regression checks passed")
