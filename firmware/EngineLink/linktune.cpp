// Transmit power test and link measurements for the network report.
//
// The board pings its router (ICMP, the router answers by itself) at each
// power step and keeps the one with the fewest losses and the shortest round
// trip. In "Auto" the phone's own download test across the steps has the last
// word (it measures what matters: data from EngineLink to the phone); this
// ping test is the start-up choice until then. It also measures small pings,
// pings to the internet (1.1.1.1: tells a slow router CPU from a slow radio
// link) and counts the Wi-Fi networks on the router's channel. Runs in its own
// task, so engine reading and the web server carry on. In its own file: the
// ping API uses C callbacks.
#include <Arduino.h>
#include <WiFi.h>
#include "ping/ping_sock.h"

// Transmit power steps, lowest to highest (index = level).
extern const wifi_power_t elTxPower[5];
const wifi_power_t elTxPower[5] = {WIFI_POWER_8_5dBm, WIFI_POWER_11dBm, WIFI_POWER_13dBm, WIFI_POWER_15dBm, WIFI_POWER_19_5dBm};
extern const char *const elTxName[5];
const char *const elTxName[5] = {"very low (8.5 dBm)", "low (11 dBm)", "moderate (13 dBm)", "medium (15 dBm)", "high (19.5 dBm)"};

struct PingStat { uint8_t sent, got; uint16_t minMs, avgMs, maxMs; };
struct LinkTuneStep { uint8_t level; PingStat p; };
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

bool pingHost(IPAddress ip, uint8_t count, uint16_t size, uint16_t intervalMs, PingStat &out) {
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

static void surveyChannel() {
  const int ch = WiFi.channel();
  if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) return;  // someone else is scanning
  if (WiFi.scanNetworks(true, true, false, 120) == WIFI_SCAN_FAILED) return;
  const uint32_t t0 = millis();
  int16_t n;
  while ((n = WiFi.scanComplete()) == WIFI_SCAN_RUNNING && millis() - t0 < 8000) vTaskDelay(pdMS_TO_TICKS(100));
  if (n < 0) { WiFi.scanDelete(); return; }
  uint8_t same = 0, overlap = 0; int8_t best = -127;
  const String own = WiFi.SSID();
  for (int16_t i = 0; i < n; i++) {
    const int c = WiFi.channel(i);
    if (c == ch) {
      if (WiFi.SSID(i) == own) continue;  // the router itself
      ++same; if (WiFi.RSSI(i) > best) best = WiFi.RSSI(i);
    } else if (abs(c - ch) <= 4) ++overlap;
  }
  linkScanTotal = n > 255 ? 255 : n; linkScanSame = same; linkScanOverlap = overlap;
  linkScanSameBest = same ? best : 0; linkScanDone = true;
  WiFi.scanDelete();
}

// Called from a task. `current` is the level in use; returns the best level
// (or `current` if the test could not run). With apply=false the level in use
// is restored afterwards (a fixed setting chosen by the owner).
uint8_t runLinkTune(uint8_t current, bool apply) {
  linkTuneRunning = true; linkTuneCount = 0;
  uint8_t best = current; int32_t bestScore = INT32_MAX, curScore = INT32_MAX;
  for (int8_t lv = 4; lv >= 0; lv--) {
    if (WiFi.status() != WL_CONNECTED) break;
    WiFi.setTxPower(elTxPower[lv]);
    vTaskDelay(pdMS_TO_TICKS(1500));       // let rate control settle
    PingStat p;
    if (!pingHost(WiFi.gatewayIP(), 20, 1400, 60, p)) break;
    linkTuneSteps[linkTuneCount++] = {(uint8_t)lv, p};
    // Every lost packet costs as much as 100 ms of extra round trip.
    const int32_t score = (int32_t)(20 - p.got) * 100 + p.avgMs;
    if (p.got >= 10 && score < bestScore) { bestScore = score; best = lv; }
    if (lv == current) curScore = p.got >= 10 ? score : INT32_MAX;
  }
  // Stay on the current step unless another one is clearly (20 %) better:
  // a busy channel makes single measurements jump around.
  if (curScore != INT32_MAX && (int64_t)curScore * 100 <= (int64_t)bestScore * 120) best = current;
  const uint8_t use = apply ? best : current;
  WiFi.setTxPower(elTxPower[use]);
  if (WiFi.status() == WL_CONNECTED) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    pingHost(WiFi.gatewayIP(), 20, 64, 100, linkSmall);
    pingHost(IPAddress(1, 1, 1, 1), 10, 64, 200, linkNet);
    surveyChannel();
  }
  linkTuneChosen = best; linkTuneAt = millis() ? millis() : 1;
  linkTuneRunning = false;
  return best;
}

// Pings the router; used by the frame bundling test in the sketch.
bool linkPingRouter(uint8_t count, uint16_t size, uint16_t intervalMs, PingStat &out) {
  return pingHost(WiFi.gatewayIP(), count, size, intervalMs, out);
}
