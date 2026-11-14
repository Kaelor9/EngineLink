// Automatic channel for the own Wi-Fi (Standalone and recovery), plan
// section 19. Before the own Wi-Fi starts, EngineLink listens 2 x 250 ms on
// each of channels 1, 6 and 11 with the driver's documented sniffer
// (esp_wifi_set_promiscuous) and starts on the one where other networks use the
// least air time. While it runs, it listens 1 s on its own channel every 10
// minutes when no phone is joined; two busy checks in a row (10 % or more)
// survey again, which restarts the own Wi-Fi only while nobody is connected.
// A phone that is connected is never moved. The choice is in chanpick.h.
#include "chanpick.h"

enum : uint8_t { CA_IDLE = 0, CA_SURVEY = 1, CA_RUN = 2 };
static uint8_t caState = CA_IDLE, caStep = 0, caCurrent = 0;
static bool caListening = false;
static uint32_t caStepAt = 0, caNextCheckAt = 0;
static volatile uint32_t caBusyUs = 0, caFrames = 0;
static chanpick::Survey caSurvey;
static chanpick::Watch caWatch;
uint32_t caSurveys = 0, caChecks = 0;
uint16_t caBusy[3] = {0, 0, 0}, caCheckPermille = 0;
static const uint32_t CA_DWELL_MS = 250, CA_CHECK_MS = 1000, CA_CHECK_EVERY_MS = 600000UL;

bool chanAutoOn() { return cfg.apAuto; }
bool chanSurveying() { return caState == CA_SURVEY; }
// The channel the own Wi-Fi starts on in automatic mode.
uint8_t chanAutoChannel() { return chanpick::indexOf(cfg.apAutoLast) >= 0 ? cfg.apAutoLast : 6; }

// Runs in the Wi-Fi task: only adds to two counters.
static void caRx(void *buf, wifi_promiscuous_pkt_type_t type) {
  (void)type;
  const wifi_pkt_rx_ctrl_t &c = ((const wifi_promiscuous_pkt_t *)buf)->rx_ctrl;
  if (c.rssi < -90) return;
  caBusyUs = caBusyUs + chanpick::airtimeUs(c.sig_mode, c.rate, c.mcs, c.cwb, c.sig_len);
  caFrames = caFrames + 1;
}
static void caSniff(bool on) {
  if (on) {
    wifi_promiscuous_filter_t f = {WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA};
    esp_wifi_set_promiscuous_filter(&f);
    esp_wifi_set_promiscuous_rx_cb(caRx);
  }
  esp_wifi_set_promiscuous(on);
  caListening = on;
}
static void caListenOn(uint8_t ch) {
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  caBusyUs = 0; caFrames = 0; caStepAt = millis();
}
// Stops a survey or check; applyNetwork calls it before the driver changes mode.
void chanAutoStop() {
  if (caListening) caSniff(false);
  caState = CA_IDLE;
}
// applyNetwork calls this instead of starting the own Wi-Fi at once.
void chanSurveyBegin() {
  const uint32_t t = millis();
  caCurrent = apStartedChannel ? apStartedChannel : cfg.apAutoLast;
  netWaitBegin(); WiFi.mode(WIFI_STA); netWaitEnd();   // a station that joins nothing: only listens
  WiFi.setSleep(false);
  caSurvey = chanpick::Survey(); caStep = 0; caState = CA_SURVEY;
  caSniff(true); caListenOn(chanpick::CANDIDATES[0]);
  netStep("channel survey: listening", t);
}
static void caSurveyEnd() {
  caSniff(false);
  for (int i = 0; i < 3; i++) caBusy[i] = caSurvey.busyPermille(chanpick::CANDIDATES[i]);
  const uint8_t ch = caSurvey.pick(caCurrent);
  ++caSurveys;
  if (apStartedChannel && ch != apStartedChannel) ++apChannelChanges;
  if (ch != cfg.apAutoLast) { cfg.apAutoLast = ch; saveApAutoLast(); }   // written only when it changes
  wifiLog(String("Channel survey: air busy ") + caBusy[0] / 10 + "% on 1, " + caBusy[1] / 10 + "% on 6, " +
          caBusy[2] / 10 + "% on 11: channel " + ch);
  const uint32_t t = millis();
  netWaitBegin(); WiFi.mode(WIFI_AP); netWaitEnd();
  startAP();
  staDownSince = 0;
  netStep("own Wi-Fi start", t);
  startNameService();
  caWatch = chanpick::Watch(); caState = CA_RUN; caNextCheckAt = millis() + CA_CHECK_EVERY_MS;
}
// From serviceNetwork, every loop. Never blocks.
void serviceChanAuto(uint32_t now) {
  if (caState == CA_SURVEY) {
    if (now - caStepAt < CA_DWELL_MS) return;
    caSurvey.heard(chanpick::CANDIDATES[caStep % 3], caBusyUs, now - caStepAt, caFrames);
    if (++caStep >= 6) { caSurveyEnd(); return; }
    caListenOn(chanpick::CANDIDATES[caStep % 3]);
    return;
  }
  if (caState != CA_RUN || !apStarted || WiFi.getMode() != WIFI_MODE_AP) return;
  if (caListening) {
    if (now - caStepAt < CA_CHECK_MS) return;
    caSniff(false);
    const uint32_t ms = now - caStepAt;
    caCheckPermille = (uint16_t)((caBusyUs / ms) > 1000 ? 1000 : caBusyUs / ms);
    ++caChecks;
    const bool again = caWatch.check(caCheckPermille);
    caNextCheckAt = now + (caWatch.busyRow ? 60000UL : CA_CHECK_EVERY_MS);   // busy once: look again in a minute
    if (again && WiFi.softAPgetStationNum() == 0) {
      wifiLog(String("Own channel busy (") + caCheckPermille / 10 + "%) with no phone joined: survey again");
      applyNetwork();
    }
    return;
  }
  if ((int32_t)(now - caNextCheckAt) < 0 || scanBusy) return;
  if (WiFi.softAPgetStationNum()) { caNextCheckAt = now + CA_CHECK_EVERY_MS; caWatch = chanpick::Watch(); return; }
  caSniff(true); caBusyUs = 0; caFrames = 0; caStepAt = now;   // on the AP's own channel
}
// One line for the network report.
String chanAutoText() {
  if (!chanAutoOn()) return String("fixed channel ") + cfg.apChannel + " (chosen in settings)";
  return String("automatic: last survey air busy ") + caBusy[0] / 10 + "% on 1, " + caBusy[1] / 10 + "% on 6, " +
         caBusy[2] / 10 + "% on 11 · surveys " + caSurveys + " · checks " + caChecks + " (last " +
         caCheckPermille / 10 + "%) · channel moves " + apChannelChanges;
}
