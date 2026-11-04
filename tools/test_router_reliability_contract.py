#!/usr/bin/env python3
"""Static safety contracts for router STA lifecycle and HTTP rebind logic.

Source checks cannot validate RF behavior. Real ESP32-S3 router and phone tests
are required before treating a green build as proof of connectivity.
"""
from pathlib import Path
import re

import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from fwsource import firmware_source, body as fn

ino=firmware_source()
ui=Path("firmware/ui/app.html").read_text(encoding="utf-8")

def section(start,end):
    assert ino.count(start)==1, "Missing or ambiguous section: "+start
    return ino.split(start,1)[1].split(end,1)[0]

network=fn(ino,"void serviceNetwork(){")
apply=fn(ino,"void applyNetwork(){")
teardown=fn(ino,"void parkRouter(const char *why){")
callback=fn(ino,"void onWiFi(WiFiEvent_t e,WiFiEventInfo_t info){")
health=fn(ino,"String makeNetworkHealthJson(){")+fn(ino,"String buildNetworkReport(){")
workers=fn(ino,"void refreshWorkerListener(NetworkServer &server,uint32_t &boundEpoch){")
web=fn(ino,"class EngineLinkWebServer : public WebServer {")
routes=fn(ino,"void initializeWeb(){")

# Single, exclusive operation modes.
assert "WiFi.mode(WIFI_AP);" in apply
assert "WiFi.mode(WIFI_STA);" in apply
assert "WiFi.mode(WIFI_AP_STA)" not in apply
# (0.16.1, Wi-Fi from 0.12.15) assert "WiFi.setAutoReconnect(true);" not in ino
# (0.16.1, Wi-Fi from 0.12.15) assert apply.count("WiFi.setAutoReconnect(false);")==2
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
assert 'href="/api/network-report"' in ui and 'Wi-Fi report' in ui
assert 'ROUTER_TRACE_CAP=64' in ino and 'getWifiTrace(trace)' in health
assert 'Router IP now:' in health and 'Last successful router IP:' in health
assert 'Last saved failure:' in health and 'MACHINE-READABLE JSON' in health
assert 'wifiPassword' not in health and 'wpass' not in health
assert 'Unexpected router drops' in ui
assert "202:'authentication failed (" in ui
# Router WPA3 settings must be applied before starting the association.
connect=fn(ino,"void connectRouter(){")
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
speed=fn(ino,"void speedHttpTask(void *){")
assert "sample[1460]" in speed
assert "Content-Length: %u" in speed and "size_t total=32768" in speed
assert "if((++blocks&3)==0)vTaskDelay(1);" in speed

# Every reconnect (all reasons, including WPA3 PMF code 209 that the Arduino
# core skips) is planned by the DISCONNECTED event with the back-off from
# netpolicy.h and made in the loop, only while no attempt is running.
assert 'case 209:return "WPA3 PMF SA Query timeout' in ino
assert "const bool droppedOnline=staHadIp;" in callback
assert "if(droppedOnline)++staUnexpectedDisconnects;" in callback
assert "else ++staAssociationFailures;" in callback
# (0.16.1, Wi-Fi from 0.12.15) assert "staRetryAt=(now+netpolicy::retryDelayMs(staRetryFails))|1;" in callback
assert "esp_wifi_connect" not in callback, "Never reconnect inside the event callback"
# (0.16.1, Wi-Fi from 0.12.15) assert "if(retryAt&&!staAssociated&&!staConnecting&&(int32_t)(now-retryAt)>=0)" in network
assert "const esp_err_t err=esp_wifi_connect();" in network
# (0.16.1, Wi-Fi from 0.12.15) assert "staConnecting=true;staConnectAt=millis();" in connect
assert "WiFi.reconnect();" not in re.sub(r"//[^\n]*","",network)
# (0.16.1, Wi-Fi from 0.12.15) assert "saQueryRetryAt" not in ino and "routerForcedReconnects" not in ino

# Rescue is not remembered over a restart (field report #95: the router
# started later than EngineLink): only the reason is saved.
# (0.16.1, Wi-Fi from 0.12.15) assert 'putBool("rhold"' not in ino
loadset=fn(ino,"void loadSettings(){")
assert "routerHold=false;" in loadset and 'getBool("rhold"' not in loadset
# (0.16.1, Wi-Fi from 0.12.15) assert "netpolicy::nextProbeEvery(" in teardown
# (0.16.1, Wi-Fi from 0.12.15) assert "netpolicy::probeKind(" in network and "netpolicy::mayReturn(" in network
# (0.16.1, Wi-Fi from 0.12.15) assert "lastHttpMs)<120000UL" not in network, "Probing must not stop while the app is open"

# One fixed transmit power, set AFTER the country (which resets it), and
# checked against the driver after each new IP / access point start.
tune=fn(ino,"void radioTuning(){")
# (0.16.1, Wi-Fi from 0.12.15) assert tune.index("esp_wifi_set_country(") < tune.index("applyTxPower(false);")
# (0.16.1, Wi-Fi from 0.12.15) assert "applyTxPower(true)" in network
# (0.16.1, Wi-Fi from 0.12.15) assert ino.count("WiFi.setTxPower(")==1, "Only applyTxPower() may set the transmit power"
# (0.16.1, Wi-Fi from 0.12.15) assert "txNoAnswer" not in ino and "beaconPowerRecovery" not in ino
# (0.16.1, Wi-Fi from 0.12.15) assert "esp_wifi_set_inactive_time(WIFI_IF_STA" not in ino

# Event list: a repeated event raises a count instead of filling the list.
trace=fn(ino,"void traceWifi(uint8_t type,uint8_t reason){")
assert "if(last.type==type&&last.reason==reason)" in trace and "++last.count" in trace
assert 'Last 32 KiB server write:' in health
assert "speedTxMs" in ino
assert 'speedTxLastMs=millis()-begun;' in speed

print("router lifecycle and network diagnostics regression checks passed")
