// Radio test for the network report, on request (after a speed test on the
// router): large and small pings to the router, pings to the internet (a slow
// router CPU versus a slow radio link) and which networks share the router's
// channel. It changes nothing on the radio: the transmit power is fixed
// (until 0.14.1 it tried every power step; that test never completed in the
// field because the app is always polling). Runs in its own task. In its own
// file: the ping API uses C callbacks.
#include <Arduino.h>
#include <WiFi.h>
#include "ping/ping_sock.h"
#include "linktune.h"

// Transmit power steps, lowest to highest (index = level).
const wifi_power_t elTxPower[5] = {WIFI_POWER_8_5dBm, WIFI_POWER_11dBm, WIFI_POWER_13dBm, WIFI_POWER_15dBm, WIFI_POWER_19_5dBm};
const char *const elTxName[5] = {"very low (8.5 dBm)", "low (11 dBm)", "moderate (13 dBm)", "medium (15 dBm)", "high (19.5 dBm)"};

LinkTuneStep linkTuneSteps[5];
PingStat linkSmall = {}, linkNet = {};
volatile uint8_t linkTuneCount = 0, linkTuneChosen = 255;
volatile bool linkTuneRunning = false;
volatile uint32_t linkTuneAt = 0;
// Channel survey from the scan in the test.
volatile bool linkScanDone = false;
volatile uint8_t linkScanTotal = 0, linkScanSame = 0, linkScanOverlap = 0;
volatile int8_t linkScanSameBest = 0;

struct PingAcc { volatile uint8_t got; volatile uint32_t sumMs, minMs, maxMs; volatile bool done; };
static void onOk(esp_ping_handle_t h, void *a) {
  uint32_t t = 0; esp_ping_get_profile(h, ESP_PING_PROF_TIMEGAP, &t, sizeof(t));
  PingAcc *p = (PingAcc *)a; p->got++; p->sumMs += t;
  if (t < p->minMs) p->minMs = t;
  if (t > p->maxMs) p->maxMs = t;
}
static void onEnd(esp_ping_handle_t, void *a) { ((PingAcc *)a)->done = true; }

static bool pingHost(IPAddress ip, uint8_t count, uint16_t size, uint16_t intervalMs, PingStat &out) {
  out = {count, 0, 0, 0, 0};
  if (ip == IPAddress(0, 0, 0, 0)) return false;
  esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
  cfg.target_addr.type = IPADDR_TYPE_V4;
  cfg.target_addr.u_addr.ip4.addr = (uint32_t)ip;
  cfg.count = count; cfg.interval_ms = intervalMs; cfg.timeout_ms = 800; cfg.data_size = size;
  cfg.task_stack_size = 3072;
  PingAcc acc = {0, 0, UINT32_MAX, 0, false};
  esp_ping_callbacks_t cbs = {}; cbs.cb_args = &acc; cbs.on_ping_success = onOk; cbs.on_ping_end = onEnd;
  esp_ping_handle_t h;
  if (esp_ping_new_session(&cfg, &cbs, &h) != ESP_OK) return false;
  esp_ping_start(h);
  const uint32_t t0 = millis();
  while (!acc.done && millis() - t0 < 25000) vTaskDelay(pdMS_TO_TICKS(50));
  esp_ping_stop(h); esp_ping_delete_session(h);
  out.got = acc.got;
  if (acc.got) { out.avgMs = acc.sumMs / acc.got; out.minMs = acc.minMs; out.maxMs = acc.maxMs; }
  return true;
}

// Networks per channel and the ones on or near the router's channel (maker
// prefix and signal, no names), for the report and the channel advice.
String linkScanList;
volatile uint32_t linkScanAt = 0, linkScanDurationMs = 0;
volatile uint16_t linkScanFailures = 0;
volatile uint8_t linkChannelCounts[14] = {}, linkChannelStrong[14] = {};
volatile uint8_t linkScanQuiet = 0, linkScanCh = 0;  // ... and the router's channel at the scan   // the 1/6/11 channel with the least around it
static void surveyChannel() {
  const int ch = WiFi.channel();
  if (ch < 1 || ch > 13 || WiFi.status() != WL_CONNECTED) return;
  if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) return;
  const uint32_t scanStart = millis();
  if (WiFi.scanNetworks(true, true, false, 120) == WIFI_SCAN_FAILED) { ++linkScanFailures; return; }
  const uint32_t t0 = millis();
  int16_t n;
  while ((n = WiFi.scanComplete()) == WIFI_SCAN_RUNNING && millis() - t0 < 8000) vTaskDelay(pdMS_TO_TICKS(100));
  if (n < 0 || WiFi.status() != WL_CONNECTED || WiFi.channel() != ch) { ++linkScanFailures; WiFi.scanDelete(); return; }
  uint8_t same = 0, overlap = 0; int8_t best = -127;
  const uint8_t *own = WiFi.BSSID();
  uint8_t ownMac[6] = {0}; if (own) memcpy(ownMac, own, 6);
  int load[14] = {0};   // signal-weighted use per channel
  uint8_t counts[14] = {}, strong[14] = {};
  String list;
  for (int16_t i = 0; i < n; i++) {
    const int c = WiFi.channel(i); const int r = WiFi.RSSI(i);
    const uint8_t *b = WiFi.BSSID(i);
    const bool router = b && memcmp(b, ownMac, 6) == 0;
    const bool sameMaker = b && memcmp(b, ownMac, 3) == 0;
    if (c >= 1 && c <= 13 && !router) {
      if (counts[c] < 255) ++counts[c];
      if (r >= -65 && strong[c] < 255) ++strong[c];
      const int w = r > -50 ? 4 : r > -65 ? 2 : 1;
      for (int k = c - 4; k <= c + 4; k++) if (k >= 1 && k <= 13) load[k] += w;
    }
    if (router) continue;
    if (c == ch) { ++same; if (r > best) best = r; }
    else if (abs(c - ch) <= 4) ++overlap;
    else continue;
    char e[64];
    snprintf(e, sizeof(e), "%s channel %d, %d dBm, maker %02X:%02X:%02X%s%s",
             list.length() ? " | " : "", c, r, b ? b[0] : 0, b ? b[1] : 0, b ? b[2] : 0,
             sameMaker ? " (same maker as the router: probably its own guest or mesh network)" : "",
             WiFi.SSID(i).length() ? "" : ", hidden");
    if (list.length() < 600) list += e;
  }
  const int cand[3] = {1, 6, 11}; int q = 1;
  for (int k = 0; k < 3; k++) if (load[cand[k]] < load[q]) q = cand[k];
  for (int k = 1; k <= 13; ++k) { linkChannelCounts[k] = counts[k]; linkChannelStrong[k] = strong[k]; }
  linkScanDurationMs = millis() - scanStart;
  linkScanAt = millis() ? millis() : 1;
  linkScanQuiet = q; linkScanCh = ch;
  linkScanList = list;
  linkScanTotal = n > 255 ? 255 : n; linkScanSame = same; linkScanOverlap = overlap;
  linkScanSameBest = same ? best : 0; linkScanDone = true;
  WiFi.scanDelete();
}

// Called from a task; blocks for about 15 s. `level` is the transmit power
// step in use, recorded with the large pings.
void runRadioTest(uint8_t level) {
  linkTuneRunning = true; linkTuneCount = 0;
  PingStat p;
  if (WiFi.status() == WL_CONNECTED && pingHost(WiFi.gatewayIP(), 20, 1400, 60, p))
    linkTuneSteps[linkTuneCount++] = {level, p};
  if (WiFi.status() == WL_CONNECTED) {
    pingHost(WiFi.gatewayIP(), 20, 64, 100, linkSmall);
    pingHost(IPAddress(1, 1, 1, 1), 10, 64, 200, linkNet);
    surveyChannel();
  }
  linkTuneChosen = level; linkTuneAt = millis() ? millis() : 1;
  linkTuneRunning = false;
}
