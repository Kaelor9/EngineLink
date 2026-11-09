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
assert "WiFi.setAutoReconnect(true)" in body(w, "void applyNetwork(){")
# Router STA must use the Arduino ESP32 Wi-Fi driver defaults.
an = body(w, "void applyNetwork(){")
assert "WiFi.mode(WIFI_STA);" in an and "WiFi.setAutoReconnect(true);" in an
assert "staRadioSetup" not in fw and "EL_STA_DEFAULTS" not in fw
assert "elAllowSae" not in fw and "staSecMode" not in fw
assert "elBoostSocket" not in fw and "wifiwpa3" not in fw

# Router counters and signal the report prints (plan 12.1 I15): written by
# the event callback from its event data only (no Wi-Fi calls there, D14),
# the signal sampled by the loop, so a report after a loss is true.
cb = body(w, "void onWiFi(WiFiEvent_t e,WiFiEventInfo_t info){")
assert "esp_wifi_" not in cb and "WiFi." not in cb, "D14: no Wi-Fi calls from the event callback"
for need in ("staAssociatedEvents=staAssociatedEvents+1", "staIpEvents=staIpEvents+1", "staIpLostEvents=staIpLostEvents+1",
             "staDisconnects=staDisconnects+1", "staUnexpectedDisconnects=staUnexpectedDisconnects+1",
             "staAssociationFailures=staAssociationFailures+1", "lastRouterChannel=info.wifi_sta_connected.channel",
             "staLastReason!=WIFI_REASON_ASSOC_LEAVE", "ARDUINO_EVENT_WIFI_STA_LOST_IP"):
    assert need in cb, f"onWiFi() lacks {need}"
sn = body(w, "void serviceNetwork(){")
assert "esp_wifi_sta_get_ap_info(&ap)" in sn and "lastRouterRssi=ap.rssi" in sn
assert "++routerFallbacks;routerHold=true;" in sn, "the report counts every fallback to the own Wi-Fi"
# 1c: the trace folds through the host-tested function.
assert "wifistate::traceFold(" in body(w, "void traceWifi(uint8_t type,uint8_t reason){")

# --- Stage 2: esp_http_server, security model, self-test, no deflate ----
h = (FW / "httpd.ino").read_text(encoding="utf-8")
assert "#include <WebServer.h>" not in fw and "NetworkServer" not in fw, "one server: esp_http_server"
assert "httpd_stop(" not in fw, "the server is started once and never stopped (6.6)"
assert len(h.splitlines()) < 300, "httpd.ino stays small (6.1)"
d = body(h, "static esp_err_t webDispatch(httpd_req_t *r) {")
for need in ("httputil::hostAllowed(", "httputil::originAllowed(", '"X-EngineLink"', "421", "403", "413",
             "httpd_req_async_handler_begin(", "ctxLock(c, 2000)"):
    assert need in d, f"dispatcher lacks {need}"
assert d.index("hostAllowed(") < d.index("X-EngineLink") < d.index("rt.fn();"), "checks run before any handler"
wk = body(h, "static void xferWorker(void *) {")
assert "httpd_req_async_handler_complete(j.r)" in wk, "async requests always complete (esp_http_server.h)"
cfgs = body(h, "bool Web::begin() {")
for need in ("lru_purge_enable = true", "max_open_sockets = 7", "keep_alive_enable = true",
             "httpd_uri_match_wildcard", "open_fn = webOpen", "core_id = 1"):
    assert need in cfgs, f"server config lacks {need}"
assert "setsockopt(fd, IPPROTO_TCP, TCP_NODELAY" in body(h, "static esp_err_t webOpen(httpd_handle_t, int fd) {")
assert "Access-Control-Allow-Origin\",\"*" not in fw and "Allow-Origin: *" not in fw, "no wildcard CORS (F22)"
api = (FW / "webapi.ino").read_text(encoding="utf-8")
for route in ("/api/export/raw.csv", "/api/export/trips.csv", "/api/export/series.csv", "/api/export/fast.csv",
              "/api/log", "/api/segments.csv", "/api/backup", "/api/nmea/bus.csv", "/api/speedtest"):
    line = next(l for l in api.splitlines() if f'web.on("{route}"' in l)
    assert "R_ASYNC" in line, f"{route} must be a long transfer (6.6)"
assert 'web.on("/api/update",HTTP_POST,handleUpdate,R_RAWBODY)' in api
up = body(api, "void handleUpdate(){")
assert "len>3200000" in up and "len<100000" in up and "web.recv(" in up
# The application check runs on the streamed descriptor, before Update.end()
# makes the image bootable (plan 12.1 I8); never by switching back afterwards.
assert up.index("otaSameProject(head)") < up.index("Update.end(true)")
fwk = body(fw, "void fwWorker(void *){")
assert fwk.index("otaSameProject(imageHeader)") < fwk.index("Update.end(true)")
assert "esp_ota_set_boot_partition" not in fw, "never switch the boot partition back by hand"
# D26: no deflate of downloads
for gone in ("tdefl_", "deflate", "miniz.h", "zComp", "acceptsDeflate"):
    assert gone not in fw, f"D26: {gone} is back"
# 6.10: the only place that confirms a new firmware is the self-test
assert fw.count("esp_ota_mark_app_valid_cancel_rollback") == 1
assert "esp_ota_mark_app_valid_cancel_rollback" in body(fw, "void serviceSelfTest(){")
assert "esp_ota_mark_app_invalid_rollback_and_reboot" in body(fw, "void serviceSelfTest(){")
for gone in ("lastHttpMs", "httpBusySince", "httpSelfCheckTask", "liveHttpTask", "speedHttpTask", "rebindNetwork"):
    assert gone not in fw, f"{gone} was removed in Stage 2"
# Handlers never act on the radio or mDNS (they run in the server task now)
assert "startNameService()" not in body(api, "void handleCfg(){")

# --- 2026-11-09 owner override: no low-level hooks or private lwIP structs ---
for removed in ("netboost.cpp", "netboost.h", "wifiwpa3.cpp", "wifiwpa3.h"):
    assert not (FW / removed).exists(), f"{removed} must not return"
for p in sorted(FW.glob("*")):
    if p.suffix in (".ino", ".h", ".cpp"):
        t = p.read_text(encoding="utf-8")
        assert "lwip/priv" not in t, f"{p.name}: private lwIP internals"
        assert "__wrap_" not in t, f"{p.name}: linker wrapper"
for wf in ("build-engine.yml", "pages.yml"):
    t = (ROOT / ".github" / "workflows" / wf).read_text(encoding="utf-8")
    assert "--wrap=esp_wifi_init" not in t
    assert "AB-test-router-driver-defaults" not in t

# --- Secrets never printed -----------------------------------------------
assert not re.search(r"print[f]?\([^;]*(apPassword|wifiPassword|ghToken)", fw), "never log a secret"
assert "setTxTimeoutMs(0)" in body(fw, "void setup(){")

# --- D25: pinned toolchain -----------------------------------------------
for wf in ("build-engine.yml", "pages.yml"):
    t = (ROOT / ".github" / "workflows" / wf).read_text(encoding="utf-8")
    assert "esp32:esp32@3.3.12" in t, f"{wf}: core not pinned (D25)"
    assert '"Adafruit NeoPixel@' in t, f"{wf}: NeoPixel not pinned (D25)"

# --- Pure headers stay pure ------------------------------------------------
for h in ("wifistate.h", "httputil.h"):
    t = (FW / h).read_text(encoding="utf-8")
    assert "#include <Arduino.h>" not in t and "WiFi" not in t and "millis(" not in t, h

print("Wi-Fi contract checks passed")
