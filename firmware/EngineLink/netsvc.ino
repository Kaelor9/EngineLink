// Network services. Wi-Fi 2.0 Stage 4 moves the clock, NTP, mDNS, texts and
// GitHub report scheduling here from wifi.ino; Stage 2 starts the file with
// the boot self-test.

// ---------- Boot self-test and OTA rollback (plan 6.10) ----------
// A new image boots "pending verify" (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE).
// IDF's instruction (esp_ota_ops.h): run the application's self-tests, then
// mark the app valid (the call is in serviceSelfTest). From 60 s after boot, every 20 s:
// the web server runs, a network is up, and a web client really got an
// answer: another device (strong evidence: Wi-Fi, IP, TCP, server and a
// handler all worked) or, when no phone has connected yet, one GET of
// http://127.0.0.1/api/health. Not passed by 600 s: the totals are saved and
// the bootloader goes back to the previous firmware.
static uint32_t selfTestNextAt=60000;
static const char *selfTestEvidence=nullptr;
static bool loopbackHealthOk(){
  netWaitBegin();   // the server task may need the lock meanwhile
  NetworkClient c;bool ok=false;
  if(c.connect(IPAddress(127,0,0,1),80,1000)){
    c.print("GET /api/health HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    ok=c.readStringUntil('\n').startsWith("HTTP/1.1 200");
  }
  c.stop();
  netWaitEnd();
  return ok;
}
void serviceSelfTest(){
  if(!rollbackPending)return;
  const uint32_t now=millis();
  if(now<selfTestNextAt)return;
  selfTestNextAt=now+20000;
  const bool netUp=apStarted||WiFi.status()==WL_CONNECTED;
  const char *ev=nullptr;
  if(web.handle&&netUp){
    if(httpdStats.external2xx)ev="client response";
    else if(loopbackHealthOk())ev="loopback /api/health";
  }
  if(ev){
    esp_ota_mark_app_valid_cancel_rollback();
    rollbackPending=false;selfTestEvidence=ev;
    Serial.printf("Firmware confirmed: %s\n",ev);
    wifiLog(String("Firmware confirmed: ")+ev);
    return;
  }
  if(now<600000UL)return;
  Serial.println("Firmware self-test failed for 10 minutes: going back to the previous firmware");
  wifiLog("Firmware self-test failed for 10 minutes: rollback");
  saveTotals();
  esp_ota_mark_app_invalid_rollback_and_reboot();
  rollbackPending=false;   // only reached when there is no previous firmware to go back to
}
String selfTestText(){
  if(selfTestEvidence)return String("passed (")+selfTestEvidence+")";
  if(!rollbackPending)return "not needed (this firmware was confirmed before)";
  return String("waiting for a web client (rollback in ")+(long)((600000L-(long)millis())/1000)+" s)";
}
