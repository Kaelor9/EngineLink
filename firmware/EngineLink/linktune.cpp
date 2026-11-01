// Finds the transmit power that the router hears best.
//
// Field data: EngineLink hears the router strongly while frames from
// EngineLink get lost or need retries (slow logins at full power, 32 KB taking
// ~2 s through the router against 50 ms on EngineLink's own Wi-Fi). Here the
// board pings its router (ICMP, 1400-byte packets, the router answers by
// itself) at each power step and keeps the one with the fewest losses and the
// shortest round trip. Runs in its own task, so engine reading and the web
// server carry on. In its own file: the ping API uses C callbacks.
#include <Arduino.h>
#include <WiFi.h>
#include "ping/ping_sock.h"

struct LinkTuneStep { uint8_t level; uint8_t sent, got; uint16_t avgMs; };
LinkTuneStep linkTuneSteps[4];
volatile uint8_t linkTuneCount = 0, linkTuneChosen = 255;
volatile bool linkTuneRunning = false;
volatile uint32_t linkTuneAt = 0;

static const wifi_power_t kPower[4] = {WIFI_POWER_19_5dBm, WIFI_POWER_15dBm, WIFI_POWER_11dBm, WIFI_POWER_8_5dBm};

struct PingAcc { volatile uint8_t got; volatile uint32_t sumMs; volatile bool done; };
static void onOk(esp_ping_handle_t h, void *a) {
  uint32_t t = 0; esp_ping_get_profile(h, ESP_PING_PROF_TIMEGAP, &t, sizeof(t));
  PingAcc *p = (PingAcc *)a; p->got++; p->sumMs += t;
}
static void onEnd(esp_ping_handle_t, void *a) { ((PingAcc *)a)->done = true; }

// Pings the gateway `count` times; false if the session could not start.
static bool pingGateway(uint8_t count, uint8_t &got, uint16_t &avgMs) {
  IPAddress gw = WiFi.gatewayIP();
  if (gw == IPAddress(0, 0, 0, 0)) return false;
  esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
  cfg.target_addr.type = IPADDR_TYPE_V4;
  cfg.target_addr.u_addr.ip4.addr = (uint32_t)gw;
  cfg.count = count; cfg.interval_ms = 60; cfg.timeout_ms = 600; cfg.data_size = 1400;
  cfg.task_stack_size = 3072;
  PingAcc acc = {0, 0, false};
  esp_ping_callbacks_t cbs = {}; cbs.cb_args = &acc; cbs.on_ping_success = onOk; cbs.on_ping_end = onEnd;
  esp_ping_handle_t h;
  if (esp_ping_new_session(&cfg, &cbs, &h) != ESP_OK) return false;
  esp_ping_start(h);
  const uint32_t t0 = millis();
  while (!acc.done && millis() - t0 < 20000) vTaskDelay(pdMS_TO_TICKS(50));
  esp_ping_stop(h); esp_ping_delete_session(h);
  got = acc.got; avgMs = acc.got ? (uint16_t)(acc.sumMs / acc.got) : 0;
  return true;
}

// Called from a task. `current` is the level in use; returns the best level
// (or `current` if the test could not run or the link dropped).
uint8_t runLinkTune(uint8_t current) {
  linkTuneRunning = true; linkTuneCount = 0;
  uint8_t best = current; int32_t bestScore = INT32_MAX;
  for (uint8_t lv = 0; lv < 4; lv++) {
    if (WiFi.status() != WL_CONNECTED) break;
    WiFi.setTxPower(kPower[lv]);
    vTaskDelay(pdMS_TO_TICKS(1500));       // let rate control settle
    uint8_t got = 0; uint16_t avg = 0;
    if (!pingGateway(20, got, avg)) break;
    linkTuneSteps[linkTuneCount++] = {lv, 20, got, avg};
    // Every lost packet costs as much as 100 ms of extra round trip.
    const int32_t score = (int32_t)(20 - got) * 100 + avg;
    if (got >= 10 && score < bestScore) { bestScore = score; best = lv; }
    if (WiFi.status() != WL_CONNECTED) break;
  }
  WiFi.setTxPower(kPower[best]);
  linkTuneChosen = best; linkTuneAt = millis() ? millis() : 1;
  linkTuneRunning = false;
  return best;
}
