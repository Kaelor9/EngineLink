// EngineLink Wi-Fi bench sketch (docs/WIFI_PLAN_2026-11-09.md section 12).
// The owner's working test sketch, plus the two things it did not test:
// a large answer (/big, 110 KB like the app page) and time (a ping to the
// router every second, counted). Nothing of EngineLink runs here, so the
// result tells whether the fault is in EngineLink's code or below it.
//
// 1. Type the router name and password below (never commit them).
// 2. Tools: same settings as before (ESP32S3 Dev Module, OPI PSRAM or Disabled).
// 3. Open http://enginelink.local  -> the numbers, refreshed every 5 s.
//    Click "big page" a few times. Leave it for 10 minutes.
// 4. Then set RADIO_PROFILE to 1, upload again, and do the same.
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <esp_wifi.h>
#include <ping/ping_sock.h>

const char *ssid = "ROUTER-NAME-HERE";
const char *password = "ROUTER-PASSWORD-HERE";

// 0 = exactly the sketch that worked (driver defaults)
// 1 = the same, but 802.11g only: no 802.11n, no 40 MHz, no frame aggregation
// 2 = driver defaults, but low transmit power (8.5 dBm instead of 20 dBm)
#define RADIO_PROFILE 2

WebServer server(80);
volatile uint32_t pingSent = 0, pingOk = 0, lostRow = 0, lostRowMax = 0, bucket[5] = {0, 0, 0, 0, 0}, pingMax = 0;
uint32_t bigTries = 0, bigMsLast = 0, bigMsMax = 0, drops = 0;

static void onPingOk(esp_ping_handle_t h, void *) {
  uint32_t ms = 0;
  esp_ping_get_profile(h, ESP_PING_PROF_TIMEGAP, &ms, sizeof(ms));
  pingSent = pingSent + 1; pingOk = pingOk + 1; lostRow = 0;
  if (ms > pingMax) pingMax = ms;
  const int b = ms < 10 ? 0 : ms < 50 ? 1 : ms < 200 ? 2 : ms < 1000 ? 3 : 4;
  bucket[b] = bucket[b] + 1;
}
static void onPingLost(esp_ping_handle_t, void *) {
  pingSent = pingSent + 1; lostRow = lostRow + 1;
  if (lostRow > lostRowMax) lostRowMax = lostRow;
}

String numbers() {
  String s = "profile " + String(RADIO_PROFILE) + (RADIO_PROFILE == 1 ? " (802.11g only)" : RADIO_PROFILE == 2 ? " (low transmit power)" : " (driver defaults)");
  int8_t q = 0;
  if (esp_wifi_get_max_tx_power(&q) == ESP_OK) s += " | transmit " + String(q / 4.0f, 1) + " dBm";
  s += " | up " + String(millis() / 1000) + " s | signal " + String(WiFi.RSSI()) + " dBm | channel " + String(WiFi.channel());
  wifi_phy_mode_t pm;
  if (esp_wifi_sta_get_negotiated_phymode(&pm) == ESP_OK)
    s += pm == WIFI_PHY_MODE_11G ? " | 802.11g" : pm == WIFI_PHY_MODE_HT20 ? " | 802.11n 20 MHz" : pm == WIFI_PHY_MODE_HT40 ? " | 802.11n 40 MHz" : pm == WIFI_PHY_MODE_11B ? " | 802.11b" : " | other";
  s += "\npings answered " + String(pingOk) + " of " + String(pingSent) + " | longest " + String(pingMax) + " ms | most lost in a row " + String(lostRowMax);
  s += "\nanswered in <10 / <50 / <200 / <1000 / 1000+ ms: ";
  for (int i = 0; i < 5; i++) s += String(bucket[i]) + (i < 4 ? " / " : "");
  s += "\nbig page sent " + String(bigTries) + " times | last " + String(bigMsLast) + " ms | slowest " + String(bigMsMax) + " ms";
  s += "\nrouter lost and found again " + String(drops) + " times | free memory " + String(ESP.getFreeHeap() / 1024) + " KB";
  return s;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\nENGINELINK WIFI BENCH");
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
#if RADIO_PROFILE == 1
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G);
#endif
  WiFi.begin(ssid, password);
#if RADIO_PROFILE == 2
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
#endif
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nConnected, IP " + WiFi.localIP().toString());
  if (MDNS.begin("enginelink")) MDNS.addService("http", "tcp", 80);

  server.on("/", HTTP_GET, []() {
    String h = "<!DOCTYPE html><html><head><meta http-equiv='refresh' content='5'>"
               "<meta name='viewport' content='width=device-width,initial-scale=1'></head><body>"
               "<h1>ENGINELINK WIFI BENCH</h1><pre>" + numbers() + "</pre>"
               "<p><a href='/big' target='_blank'>big page (110 KB)</a></p></body></html>";
    server.send(200, "text/html", h);
  });
  server.on("/big", HTTP_GET, []() {
    static char block[1025];
    if (!block[0]) { memset(block, 'x', 1024); block[1023] = '\n'; }
    const uint32_t t0 = millis();
    server.setContentLength(110 * 1024);
    server.send(200, "text/plain", "");
    for (int i = 0; i < 110; i++) server.sendContent(block, 1024);
    bigMsLast = millis() - t0; bigTries++;
    if (bigMsLast > bigMsMax) bigMsMax = bigMsLast;
  });
  server.begin();

  esp_ping_config_t c = ESP_PING_DEFAULT_CONFIG();
  const IPAddress gw = WiFi.gatewayIP();
  IP_ADDR4(&c.target_addr, gw[0], gw[1], gw[2], gw[3]);
  c.count = ESP_PING_COUNT_INFINITE; c.interval_ms = 1000; c.timeout_ms = 950; c.data_size = 32;
  esp_ping_callbacks_t cb = {nullptr, onPingOk, onPingLost, nullptr};
  esp_ping_handle_t h;
  if (esp_ping_new_session(&c, &cb, &h) == ESP_OK) esp_ping_start(h);
}

void loop() {
  server.handleClient();
  static bool was = true;
  const bool on = WiFi.status() == WL_CONNECTED;
  if (on && !was) drops++;
  was = on;
  static uint32_t last = 0;
  if (millis() - last >= 10000) { last = millis(); Serial.println(numbers()); Serial.println(); }
  delay(2);
}
