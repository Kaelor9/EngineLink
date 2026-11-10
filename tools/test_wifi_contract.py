#!/usr/bin/env python3
"""Wi-Fi structural contracts (docs/WIFI_PLAN_2026-11-09.md section 3, P7).

Only what a host test cannot execute: lessons a build or a code review can
break. Two modes (AP only, STA only), the documented station profile, the
esp_http_server adapter, the OTA self-test, no low-level hooks."""
import re
from pathlib import Path

from fwsource import FW, body, firmware_source

ROOT = FW.parent.parent
fw = firmware_source()
w = (FW / "wifi.ino").read_text(encoding="utf-8")

# --- Radio: the AP settings every version before 0.16.2 had, D27 country ---
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
# --- The station (plan section 9.4): power save off and 20 MHz, set right
# after WiFi.mode(WIFI_STA); 802.11n or 802.11g by the last link check.
# Nothing else: no country, scan method, TX power, security or address call.
an = body(w, "void applyNetwork(){")
sta = an[an.index("WiFi.mode(WIFI_STA);"):an.index("connectRouter();")]
assert sta.count("setSleep") == 1 and "WiFi.setSleep(false);" in sta, "station power save off, set once after WiFi.mode(WIFI_STA)"
assert "staRadioSetup();" in sta and sta.index("WiFi.setSleep(false);") < sta.index("staRadioSetup();")
for gone in ("esp_wifi_", "setScanMethod", "setTxPower", "setMinSecurity", "setSortMethod", "WiFi.config("):
    assert gone not in sta, f"no other station setting between WiFi.mode(WIFI_STA) and connectRouter(): {gone}"
assert "WiFi.setAutoReconnect(true);" in sta, "the core's own reconnect"
srs = body(w, "void staRadioSetup(){")
assert "esp_wifi_set_bandwidth(WIFI_IF_STA,WIFI_BW_HT20);" in srs, "20 MHz on the station (rc3: 40 MHz negotiated, 2 % of pings)"
assert "staPlainG?(WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G):(WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N)" in srs
assert srs.count("esp_wifi_") == 2, "staRadioSetup() sets the protocol and the bandwidth, nothing else"
for gone in ("esp_wifi_set_inactive_time(WIFI_IF_STA", "WIFI_BW_HT40)", "esp_wifi_set_ps(", "WiFi.setSleep(true)", "WiFi.setScanMethod(",
             "esp_wifi_set_country(", "WIFI_PROTOCOL_LR)"):
    assert gone not in fw, f"{gone}: not a station setting of this firmware"
assert fw.count("esp_wifi_set_protocol(") == 1 and fw.count("esp_wifi_set_bandwidth(WIFI_IF_STA") == 1
cr = body(w, "void connectRouter(){")
assert "WiFi.begin(cfg.ssid.c_str()," in cr and "esp_wifi_" not in cr
assert "WiFi.mode(WIFI_AP_STA)" not in an, "two exclusive modes"
assert "EL_STA_DEFAULTS" not in fw and "elAllowSae" not in fw and "staSecMode" not in fw
assert "elBoostSocket" not in fw and "wifiwpa3" not in fw
assert "char cc[3]" not in fw, "the country code is 3 bytes without a terminator (F3)"
# The recovery is a mechanism, not a mode: 120 s fallback, 10 min retries, named so.
assert "routerRetryAt=now+600000UL" in body(w, "void serviceNetwork(){")
assert '"rescue"' not in fw and "Recovery (" not in fw, "no third mode in the report (F6)"
# Wi-Fi 2.0 Stage 4 is not built: no state machine, no looks, no AP+STA idle station.
for gone in ("wifistate", "RouterGuard", "netpolicy::", "linkTune", "runRadioTest", "routerProbe", "routerKnownChannel"):
    assert gone not in fw, f"{gone} was removed (plan P3)"

# Router counters the report prints: written by the event callback from its
# event data only (no Wi-Fi calls there, D14). The signal is read only when a
# report or the app asks (routerSignalSample), never polled while connecting.
cb = body(w, "void onWiFi(WiFiEvent_t e,WiFiEventInfo_t info){")
assert "esp_wifi_" not in cb and "WiFi." not in cb, "D14: no Wi-Fi calls from the event callback"
for need in ("staAssociatedEvents=staAssociatedEvents+1", "staIpEvents=staIpEvents+1", "staIpLostEvents=staIpLostEvents+1",
             "staDisconnects=staDisconnects+1", "staUnexpectedDisconnects=staUnexpectedDisconnects+1",
             "staAssociationFailures=staAssociationFailures+1", "lastRouterChannel=info.wifi_sta_connected.channel",
             "staLastReason!=WIFI_REASON_ASSOC_LEAVE", "ARDUINO_EVENT_WIFI_STA_LOST_IP"):
    assert need in cb, f"onWiFi() lacks {need}"
sn = body(w, "void serviceNetwork(){")
assert "esp_wifi_sta_get_ap_info" not in sn and "WiFi.RSSI()" not in sn, "nothing polls the driver from the loop (as Prism)"
# Plan sections 8-9: the link is measured per router session (netdiag.ino),
# read once after the IP and ended on every network change. It measures; the
# only driver settings it may touch are the promiscuous sample's.
nd = (FW / "netdiag.ino").read_text(encoding="utf-8")
assert "if(online)routerSessionBegin();" in sn and "routerSessionEnd(netstat::END_OWN);" in an
assert an.index("routerSessionEnd(") < an.index("WiFi.mode(WIFI_OFF);"), "the ping session and the radio sample stop before the driver does"
for banned in ("WiFi.begin", "WiFi.mode", "WiFi.disconnect", "WiFi.reconnect", "esp_wifi_connect", "esp_wifi_disconnect", "applyNetwork(",
               "lwip/priv", "esp_private", "etharp_", "tcp_pcb"):
    assert banned not in nd, f"netdiag.ino only measures: {banned}"
import re as _re
assert set(_re.findall(r"esp_wifi_set_\w+", nd)) == {"esp_wifi_set_promiscuous", "esp_wifi_set_promiscuous_filter", "esp_wifi_set_promiscuous_rx_cb"}
rs = body(nd, "static void radioStart(uint32_t now){")
assert "WIFI_PROMIS_FILTER_MASK_MGMT|WIFI_PROMIS_FILTER_MASK_DATA" in rs and "radioNextAt=now+30000UL" in rs, "a sample every 30 s"
assert ">=2000UL)radioStop();" in body(nd, "void netdiagService(uint32_t now,bool online){"), "a sample lasts 2 s"
assert "esp_wifi_set_promiscuous(false)" in body(nd, "static void radioStop(){")
end = body(nd, "void routerSessionEnd(uint8_t why){")
assert end.index("radioStop();") < end.index("r.active=false"), "the sample is off before a session ends"
cb = body(nd, "static void radioCb(void *buf,wifi_promiscuous_pkt_type_t type){")
assert "sig_len<28" in cb and "esp_wifi_" not in cb and "String" not in cb and "malloc" not in cb, "the sample callback only counts"
assert "esp_ping_new_session(" in body(nd, "void routerSessionBegin(){") and "esp_ping_delete_session(" in end
assert fw.count("esp_phy_erase_cal_data_in_nvs") == 1 and 'getUChar("cal",0)<1' in body(nd, "void rfCalibrateOnce(){"), "RF calibration discarded once only"
st = body(fw, "void setup(){")
assert st.index("rfCalibrateOnce();") < st.index("applyNetwork();"), "before the Wi-Fi starts"
# rc3's race: the fallback timer compares signed (plan 9.2 item 1).
assert "(int32_t)(now-staDownSince)>=(int32_t)120000UL" in sn and "(uint32_t)(now-staDownSince)" not in sn
# The link check's decisions come from the host-tested header and are acted on in wifi.ino only.
assert "netstat::linkDead(" in body(nd, "bool routerLinkDead(uint32_t now){") and "netstat::linkPoor(" in body(nd, "static void sessionClose(uint8_t reason){")
assert sn.count("routerLinkDead(now)") == 1 and "routerSessionEnd(netstat::END_REJOIN);applyNetwork();return;" in sn
assert "if(!linkRejoined){" in sn and "routerSessionEnd(netstat::END_GIVEN_UP);" in sn
assert "surveyRound=0;surveyAt=millis()+5000UL" in sn and "if(routerHold&&(surveyRound==0||WiFi.softAPgetStationNum()==0)){surveyPending=true" in sn
# The engine lock is let go around every driver mode change (plan 9.2 item 3).
for mode in ("WIFI_OFF", "WIFI_STA", "WIFI_AP"):
    i = an.index(f"WiFi.mode({mode});")
    assert "netWaitBegin();" in an[i-40:i] and an[i:i+60].count("netWaitEnd();") == 1, f"engine lock held over WiFi.mode({mode})"
sc = body(w, "void serviceWifiScan(){")
assert "netWaitBegin();WiFi.mode(WIFI_AP_STA);netWaitEnd();" in sc and "netWaitBegin();WiFi.mode(WIFI_AP);netWaitEnd();" in sc
# The web server tells the networks apart and logs single requests.
h = (FW / "httpd.ino").read_text(encoding="utf-8")
assert "httpConn(fd, 1)" in body(h, "static esp_err_t webOpen(httpd_handle_t, int fd) {") and "httpConn(fd, 0)" in body(h, "static void webClose(httpd_handle_t, int fd) {")
assert "httpRecord(c, ms)" in body(h, "static esp_err_t finish(WebCtx &c, uint32_t t0) {")
assert "if (e == ESP_OK) c.bytes += n;" in body(h, "static esp_err_t io(WebCtx &c, const char *p, size_t n, bool chunk) {")
# The trace folds through the host-tested function.
assert "tracefold::traceFold(" in body(w, "void traceWifi(uint8_t type,uint8_t reason){")

# --- esp_http_server, security model, self-test, no deflate ----------------
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
# The 110 KB app page streams from the transfer task too (plan P5), and one
# request may wait behind a running transfer instead of getting 503.
root = body(api, "void initializeWeb(){")
assert "R_NOLOCK|R_ASYNC);" in root[root.index('web.on("/",HTTP_GET'):root.index('web.on("/favicon.ico"')]
assert "xQueueCreate(2, sizeof(XferJob))" in cfgs and "uxQueueSpacesAvailable(xferQ) == 0" in d
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
for removed in ("netboost.cpp", "netboost.h", "wifiwpa3.cpp", "wifiwpa3.h", "wifistate.h", "netpolicy.h", "routerguard.h", "linktune.cpp", "linktune.h"):
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
for h in ("tracefold.h", "httputil.h", "netstat.h"):
    t = (FW / h).read_text(encoding="utf-8")
    assert "#include <Arduino.h>" not in t and "WiFi" not in t and "millis(" not in t, h

print("Wi-Fi contract checks passed")
