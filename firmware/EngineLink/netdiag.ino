// What EngineLink measures about the router link by itself, layer by layer
// (docs/WIFI_PLAN_2026-11-09.md section 8). A page that does not open says
// nothing about where it fails, and the report can only be fetched afterwards
// on the own Wi-Fi, so each router session leaves these figures behind:
//   radio:  the router as the driver saw it, signal at the start/lowest/last
//   IP:     one ping to the router every 2 s (esp_ping, lwIP's documented API)
//   HTTP:   connections and requests that came in through the router (httpd.ino)
// and, when the router was given up, one scan: is anything heard at all?
// Diagnostics only: nothing here changes how the station connects.

// The stored RF calibration is discarded once, so this start calibrates in
// full (esp_phy_init.h). It was made while the 3.3 V regulator had a cold
// solder joint (repaired 2026-11-09, docs/HARDWARE.md); the driver otherwise
// reuses it for ever. Runs before the Wi-Fi starts; settings are untouched.
void rfCalibrateOnce(){
  Preferences m;
  if(!m.begin("el_radio",false))return;
  if(m.getUChar("cal",0)<1){rfRecalAtBoot=esp_phy_erase_cal_data_in_nvs()==ESP_OK;m.putUChar("cal",1);}
  m.end();
}

static esp_ping_handle_t linkPing=nullptr;
static void linkPingOk(esp_ping_handle_t h,void *){
  uint32_t ms=0;esp_ping_get_profile(h,ESP_PING_PROF_TIMEGAP,&ms,sizeof(ms));
  rsess.pingSent=rsess.pingSent+1;rsess.pingOk=rsess.pingOk+1;rsess.pingSumMs=rsess.pingSumMs+ms;
  if(ms>rsess.pingMaxMs)rsess.pingMaxMs=ms;
  rsess.pingLostRow=0;
  if(rsess.pingOk%15==0){int r=0;if(esp_wifi_sta_get_rssi(&r)==ESP_OK&&r<0){rsess.rssiLast=r;if(r<rsess.rssiMin)rsess.rssiMin=r;}}   // every 30 s
}
static void linkPingLost(esp_ping_handle_t,void *){
  rsess.pingSent=rsess.pingSent+1;rsess.pingLostRow=rsess.pingLostRow+1;
  if(rsess.pingLostRow>rsess.pingLostRowMax)rsess.pingLostRowMax=rsess.pingLostRow;
}
static const char *phyName(uint8_t p){
  return p==WIFI_PHY_MODE_11B?"802.11b (slow)":p==WIFI_PHY_MODE_11G?"802.11g":p==WIFI_PHY_MODE_HT20?"802.11n 20 MHz":p==WIFI_PHY_MODE_HT40?"802.11n 40 MHz":p==WIFI_PHY_MODE_HE20?"802.11ax":"not known";
}
static const char *cipherName(uint8_t c){
  static const char *n[]={"none","WEP40","WEP104","TKIP","CCMP (AES)","TKIP+CCMP","AES-CMAC","SMS4","GCMP","GCMP-256","AES-GMAC-128","AES-GMAC-256"};
  return c<12?n[c]:"not known";
}
// One line for the SD log and the report.
static void sessionLine(String &s){
  const RouterSession &r=rsess;
  const uint32_t sent=r.pingSent,ok=r.pingOk;
  s+="pings to the router ";s+=ok;s+=" of ";s+=sent;s+=" answered";
  if(ok){s+=", average ";s+=r.pingSumMs/ok;s+=" ms, longest ";s+=r.pingMaxMs;s+=" ms";}
  s+=", most lost in a row ";s+=r.pingLostRowMax;
  s+=" · through the router: ";s+=httpdStats.rtrOpens-r.opens0;s+=" connections, ";s+=httpdStats.rtrReq-r.req0;s+=" requests, ";
  s+=httpdStats.rtrOk-r.ok0;s+=" answered, ";s+=httpdStats.rtrFail-r.fail0;s+=" failed while sending";
}
void routerSessionEnd(){
  if(linkPing){esp_ping_stop(linkPing);esp_ping_delete_session(linkPing);linkPing=nullptr;}
  if(!rsess.active)return;
  rsess.active=false;rsess.endMs=millis();
  rsess.endReason=WiFi.status()==WL_CONNECTED?0:staLastReason;   // 0: EngineLink left by itself (mode change)
  String s="Router session ended after ";s+=(rsess.endMs-rsess.startMs)/1000UL;s+=" s, code ";s+=(unsigned)rsess.endReason;
  s+=", signal ";s+=rsess.rssiStart;s+="/";s+=rsess.rssiMin;s+="/";s+=rsess.rssiLast;s+=" dBm (start/lowest/last): ";
  sessionLine(s);wifiLog(s);
}
// Called from the main loop shortly after the router gave an IP (never from
// the Wi-Fi event callback): one read of the driver, then the link check.
void routerSessionBegin(){
  routerSessionEnd();
  rsess=RouterSession();rsess.active=rsess.seen=true;rsess.startMs=millis();
  wifi_ap_record_t ap;
  if(esp_wifi_sta_get_ap_info(&ap)==ESP_OK){
    rsess.rssiStart=rsess.rssiMin=rsess.rssiLast=ap.rssi;rsess.ch=ap.primary;rsess.second=(uint8_t)ap.second;
    rsess.b=ap.phy_11b;rsess.g=ap.phy_11g;rsess.n=ap.phy_11n;rsess.pair=(uint8_t)ap.pairwise_cipher;rsess.group=(uint8_t)ap.group_cipher;
  }
  wifi_phy_mode_t pm;if(esp_wifi_sta_get_negotiated_phymode(&pm)==ESP_OK)rsess.phy=(uint8_t)pm;
  wifi_ps_type_t ps;if(esp_wifi_get_ps(&ps)==ESP_OK)rsess.ps=(uint8_t)ps;
  rsess.opens0=httpdStats.rtrOpens;rsess.req0=httpdStats.rtrReq;rsess.ok0=httpdStats.rtrOk;rsess.fail0=httpdStats.rtrFail;
  esp_ping_config_t c=ESP_PING_DEFAULT_CONFIG();
  const IPAddress gw=WiFi.gatewayIP();
  IP_ADDR4(&c.target_addr,gw[0],gw[1],gw[2],gw[3]);
  c.count=ESP_PING_COUNT_INFINITE;c.interval_ms=2000;c.timeout_ms=1000;c.data_size=32;c.task_stack_size=4096;
  esp_ping_callbacks_t cb={nullptr,linkPingOk,linkPingLost,nullptr};
  if(esp_ping_new_session(&c,&cb,&linkPing)!=ESP_OK||esp_ping_start(linkPing)!=ESP_OK)linkPing=nullptr;
}
static void surveyLine(String &s){
  if(survey.failed){s+="the scan failed";return;}
  s+=survey.heard;s+=" networks heard";
  if(survey.best>-127){s+=" (strongest other ";s+=survey.best;s+=" dBm)";}
  s+=", the router ";
  if(survey.routerSeen){s+="heard on channel ";s+=survey.routerCh;s+=" at ";s+=survey.routerRssi;s+=" dBm";}else s+="NOT heard";
}
// The scan after the router was given up (wifi.ino starts it, 5 s after the
// own Wi-Fi came on). Neighbours heard but not the router: the router's
// 2.4 GHz radio is away. Nothing heard at all: EngineLink's receiver.
void surveyRecord(){
  survey=Survey();survey.done=true;survey.atMs=millis();survey.failed=scanCount<0;survey.heard=scanRawCount;
  for(uint8_t i=0;i<scanEntriesCount;i++){
    const ScanEntry &x=scanEntries[i];
    if(x.ssid==cfg.ssid){survey.routerSeen=true;survey.routerRssi=x.rssi;survey.routerCh=x.channel;}
    else if(x.rssi>survey.best)survey.best=x.rssi;
  }
  String s="Scan after giving up the router: ";surveyLine(s);wifiLog(s);
}
void appendRouterDiagText(String &s){
  const RouterSession &r=rsess;
  s+="ROUTER LINK AS ENGINELINK MEASURED IT (";s+=r.active?"this router session":"the last router session";s+=")\n";
  if(!r.seen)s+="No router session with an IP address since the start.\n";
  else{
    s+="Session: started at uptime ";s+=r.startMs/1000UL;s+=" s, ";
    if(r.active){s+="running for ";s+=(millis()-r.startMs)/1000UL;s+=" s\n";}
    else{s+="lasted ";s+=(r.endMs-r.startMs)/1000UL;s+=" s, ended with code ";s+=(unsigned)r.endReason;s+=" (";s+=r.endReason?reasonText(r.endReason):"EngineLink changed network itself";s+=")\n";}
    s+="Router: channel ";s+=r.ch;s+=r.second==WIFI_SECOND_CHAN_NONE?" (20 MHz)":" (router uses 40 MHz)";
    s+=", supports ";s+=r.b?"b ":"";s+=r.g?"g ":"";s+=r.n?"n ":"";s+="· negotiated ";s+=phyName(r.phy);
    s+=" · encryption pairwise ";s+=cipherName(r.pair);s+=", group ";s+=cipherName(r.group);s+="\n";
    s+="Signal: ";s+=r.rssiStart;s+=" dBm at the start, lowest ";s+=r.rssiMin;s+=", last ";s+=r.rssiLast;s+=" (read every 30 s)\n";
    s+="Station power save in that session: ";s+=r.ps==WIFI_PS_NONE?"off":r.ps==255?"not read":"ON (modem sleep)";s+="\n";
    s+="Link check, one ping to the router every 2 s: ";sessionLine(s);s+="\n";
  }
  s+="Scan after the router was given up: ";
  if(!survey.done)s+="none yet";else{surveyLine(s);s+=" (at uptime ";s+=survey.atMs/1000UL;s+=" s)";}
  s+="\nRF calibration: ";s+=rfRecalAtBoot?"made in full at this start (once, after the regulator repair)":"stored one used";
  s+="\nReading it: pings lost = the radio link itself. Pings fine but 0 connections = the other device's packets\n"
     "never reached EngineLink (router isolation, the device on another network, name not resolved).\n"
     "Connections but failed sends = the link carries small packets but not a page.\n\n";
}
void appendRouterDiagJson(String &s){
  const RouterSession &r=rsess;
  s+=",\"routerSession\":{\"seen\":";s+=r.seen?"true":"false";s+=",\"active\":";s+=r.active?"true":"false";
  s+=",\"startMs\":";s+=r.startMs;s+=",\"endMs\":";s+=r.endMs;s+=",\"endReason\":";s+=(unsigned)r.endReason;
  s+=",\"ch\":";s+=r.ch;s+=",\"second\":";s+=r.second;s+=",\"b\":";s+=r.b?1:0;s+=",\"g\":";s+=r.g?1:0;s+=",\"n\":";s+=r.n?1:0;
  s+=",\"phy\":";s+=r.phy;s+=",\"pairwise\":";s+=r.pair;s+=",\"group\":";s+=r.group;s+=",\"ps\":";s+=r.ps;
  s+=",\"rssiStart\":";s+=r.rssiStart;s+=",\"rssiMin\":";s+=r.rssiMin;s+=",\"rssiLast\":";s+=r.rssiLast;
  s+=",\"pingSent\":";s+=(uint32_t)r.pingSent;s+=",\"pingOk\":";s+=(uint32_t)r.pingOk;s+=",\"pingSumMs\":";s+=(uint32_t)r.pingSumMs;
  s+=",\"pingMaxMs\":";s+=(uint32_t)r.pingMaxMs;s+=",\"pingLostRowMax\":";s+=(uint32_t)r.pingLostRowMax;
  s+=",\"opens\":";s+=httpdStats.rtrOpens-r.opens0;s+=",\"requests\":";s+=httpdStats.rtrReq-r.req0;
  s+=",\"ok\":";s+=httpdStats.rtrOk-r.ok0;s+=",\"sendFails\":";s+=httpdStats.rtrFail-r.fail0;s+="}";
  s+=",\"survey\":{\"done\":";s+=survey.done?"true":"false";s+=",\"failed\":";s+=survey.failed?"true":"false";s+=",\"atMs\":";s+=survey.atMs;
  s+=",\"heard\":";s+=survey.heard;s+=",\"best\":";s+=survey.best;s+=",\"routerSeen\":";s+=survey.routerSeen?"true":"false";
  s+=",\"routerRssi\":";s+=survey.routerRssi;s+=",\"routerCh\":";s+=survey.routerCh;s+="}";
  s+=",\"rfRecal\":";s+=rfRecalAtBoot?"true":"false";
}
