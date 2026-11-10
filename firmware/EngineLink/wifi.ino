// Wi-Fi: own access point or the boat router (never both), router fallback
// and return, scans, mDNS name and the Wi-Fi event log.

void traceWifi(uint8_t type,uint8_t reason){
  const uint32_t now=millis();
  portENTER_CRITICAL(&traceMux);
  // Repeats and the core's own reason-36 disconnect right after a failure
  // are folded into the newest entry (tracefold.h, host-tested).
  if(traceSize&&tracefold::traceFold(routerTrace[(traceHead+ROUTER_TRACE_CAP-1)%ROUTER_TRACE_CAP],type,reason,now)){
    portEXIT_CRITICAL(&traceMux);
    return;
  }
  routerTrace[traceHead]={now,now,1,type,reason};++traceTotal;
  traceHead=(traceHead+1)%ROUTER_TRACE_CAP;
  if(traceSize<ROUTER_TRACE_CAP)++traceSize;
  portEXIT_CRITICAL(&traceMux);
}
// The entries and the running total in one critical section, so the SD log
// never counts an event as written that it did not copy (audit 2026-11-05 item 6).
uint8_t getWifiTrace(RouterTrace *out,uint32_t *total){
  portENTER_CRITICAL(&traceMux);
  const uint8_t n=traceSize;
  for(uint8_t i=0;i<n;i++)out[i]=routerTrace[(traceHead+ROUTER_TRACE_CAP-n+i)%ROUTER_TRACE_CAP];
  if(total)*total=traceTotal;
  portEXIT_CRITICAL(&traceMux);
  return n;
}
uint64_t elapsedMs(){return (uint64_t)esp_timer_get_time()/1000ULL;}
bool validWall(){return time(nullptr)>1700000000;}
void setClockFromUtc(time_t sec, const String &source){
  if(sec<1700000000 || sec>4102444800LL)return;
  struct timeval tv={sec,0};
  settimeofday(&tv,nullptr);
  hasTime=true;timeSource=source;
  backfillTripTime();
}
bool allowTime(const String &source){
  if(cfg.timePreference=="auto") {
    if(source=="gps")return true;
    if(source=="ntp")return timeSource!="gps";
    if(source=="phone")return timeSource!="gps"&&timeSource!="ntp";
    return false;
  }
  return cfg.timePreference==source;
}
void ntpCallback(struct timeval *tv){(void)tv;ntpNotification=true;}
// The fixed transmit power (txLevel). Setting the country resets the power
// to 20 dBm, so this runs after it; with `check` it only sets the power when
// the driver reports another one (counted for the report). The driver keeps
// 19.5 dBm as 78 quarter-dBm exactly; its unset 20 dBm (80) is not accepted
// as "close enough" any more (plan F7).
void applyTxPower(bool check){
  if(WiFi.getMode()==WIFI_MODE_STA)return; // use the STA driver default power setting
  if(check){
    int8_t q=0;
    if(esp_wifi_get_max_tx_power(&q)!=ESP_OK)return;   // Wi-Fi not running
    if(q==(int8_t)elTxPower[txLevel])return;
    ++txReapplied;
  }
  WiFi.setTxPower(elTxPower[txLevel]);
}
// Country per Espressif's API (D27), in both modes: DK, 802.11d on so the
// station follows the router's country element. The driver is deinitialised
// at every mode switch, so each mode sets it again, and only when the driver
// reports another code: setting it restarts the AP and resets the transmit
// power (esp_wifi.h). The code is 3 bytes without a terminator: 4-byte buffer.
static bool countryIsDK(){char cc[4]={};return esp_wifi_get_country_code(cc)==ESP_OK&&cc[0]=='D'&&cc[1]=='K';}
// The AP's radio settings every version before 0.16.2 had (F19, Stage 1a): 20 MHz,
// stations kept 600 s, country DK, no power save, then the fixed TX power.
void apRadioSetup(){
  esp_wifi_set_bandwidth(WIFI_IF_AP,WIFI_BW_HT20);
  esp_wifi_set_inactive_time(WIFI_IF_AP,600);
  if(!countryIsDK())esp_wifi_set_country_code("DK",true);
  WiFi.setSleep(false);
  applyTxPower(false);
}
// ---- Wi-Fi, kept simple (0.16.2) ---- Standalone: own Wi-Fi at 192.168.4.1.
// Router: the plain station of the owner's working test sketch
// (applyNetwork); no IP in 2 minutes: own Wi-Fi (recovery, not a mode), and
// try the router again every 10 minutes while no phone is connected to it,
// or at once when the app asks.
void onWiFi(WiFiEvent_t e,WiFiEventInfo_t info){
  const uint32_t now=millis();
  switch(e){
    case ARDUINO_EVENT_WIFI_AP_START:apStarted=true;apLastEvent=1;apLastEventMs=now;++apStarts;++networkIpEpoch;networkIpEventAt=now;mdnsRefreshPending=true;mdnsNetworkChangedAt=now;break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:++apJoins;traceWifi(7);break;
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:++apDhcpLeases;traceWifi(9);break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:++apLeaves;apReason=info.wifi_ap_stadisconnected.reason;traceWifi(8,apReason);break;
    case ARDUINO_EVENT_WIFI_AP_STOP:apLastEvent=2;apLastEventMs=now;break;
    // Router counters, channel and security from the event data only (D14; the
    // loop samples the signal). Printed since 0.16.2, written again (I15).
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      staAssociated=true;staAssociatedEvents=staAssociatedEvents+1;staAssociatedAt=now;
      if(staBeginAt){staJoinMs=now-staBeginAt;staBeginAt=0;}   // the join EngineLink started, not the core's retries
      lastRouterChannel=info.wifi_sta_connected.channel;staNegotiatedAuth=(uint8_t)info.wifi_sta_connected.authmode;traceWifi(2);break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      staIpEvents=staIpEvents+1;if(staAssociatedAt)staDhcpMs=now-staAssociatedAt;
      staHadIp=true;staIpAt=now?now:1;staDownSince=0;++networkIpEpoch;networkIpEventAt=now;mdnsRefreshPending=true;mdnsNetworkChangedAt=now;traceWifi(3);break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:staIpLostEvents=staIpLostEvents+1;staIpLostAt=now;traceWifi(5);break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:{
      const bool wasUp=staAssociated;   // reason 8 = EngineLink left itself (mode change)
      staAssociated=false;staLastReason=info.wifi_sta_disconnected.reason;staLastDropAt=now;staDisconnects=staDisconnects+1;
      if(wasUp){staSessionEndReason=staLastReason;staDropRssi=info.wifi_sta_disconnected.rssi;}
      if(wasUp&&staLastReason!=WIFI_REASON_ASSOC_LEAVE){staUnexpectedDisconnects=staUnexpectedDisconnects+1;staLastUnexpectedReason=staLastReason;}
      else if(!wasUp)staAssociationFailures=staAssociationFailures+1;
      if(!staDownSince)staDownSince=now?now:1;
      traceWifi(4,staLastReason);break;}
    default:break;
  }
}
bool routerConfigured(){return cfg.router&&cfg.ssid.length();}
void startAP(){
  WiFi.softAPConfig(IPAddress(192,168,4,1),IPAddress(192,168,4,1),IPAddress(255,255,255,0));
  apChannelNow=(cfg.apChannel>=1&&cfg.apChannel<=11)?cfg.apChannel:6;
  apStarted=WiFi.softAP(cfg.apName.c_str(),cfg.apPassword.c_str(),apChannelNow,false,6);
  if(apStarted)apRadioSetup();
  apStartedChannel=apChannelNow;
  wifiLog(String("Own Wi-Fi ")+(apStarted?"on":"FAILED to start")+": "+cfg.apName+", channel "+apChannelNow);
  Serial.printf("EngineLink Wi-Fi %s: %s, channel %u, %s\n",apStarted?"on":"FAILED",cfg.apName.c_str(),(unsigned)apChannelNow,WiFi.softAPIP().toString().c_str());
}
void connectRouter(){
  traceWifi(1);staBeginAt=staTryAt=millis();++routerAttempts;
  WiFi.begin(cfg.ssid.c_str(),cfg.wifiPassword.length()?cfg.wifiPassword.c_str():nullptr);
  Serial.printf("Joining router %s\n",cfg.ssid.c_str());
}
bool validHostName(const String &n){
  if(n.length()<1||n.length()>32||n[0]=='-'||n[n.length()-1]=='-')return false;
  for(size_t i=0;i<n.length();i++){const char c=n[i];if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'))return false;}
  return true;
}
// <name>.local on whichever network is up.
void startNameService(){
  mdnsLastAttemptAt=millis();mdnsRefreshPending=false;
  if(mdnsReady){MDNS.end();mdnsReady=false;}
  if(MDNS.begin(cfg.hostName.c_str())&&MDNS.addService("http","tcp",80)){MDNS.setInstanceName(cfg.apName.c_str());mdnsReady=true;++mdnsRestarts;return;}
  MDNS.end();++mdnsFailures;
}
void applyNetwork(){
  const bool withRouter=routerConfigured()&&!routerHold;
  if(scanInFlight)WiFi.scanDelete();
  scanBusy=false;scanRequested=false;scanInFlight=false;
  routerSessionEnd(netstat::END_OWN);
  if(mdnsReady){MDNS.end();mdnsReady=false;}
  uint32_t t=millis();
  httpdSuspend();   // close web sessions before the driver stops (plan 6.2)
  netStep("closing web sessions",t);
  // The driver's mode changes can take over a second (rc3: 1645 ms with the
  // engine task waiting). They touch no shared data, so the engine lock is
  // let go around each of them (plan 9.2 item 3).
  t=millis();netWaitBegin();
  WiFi.mode(WIFI_OFF);
  netWaitEnd();netStep("Wi-Fi off",t);
  WiFi.setHostname(cfg.hostName.c_str());   // the DHCP and mDNS name; the only call the test sketch does not have
  apStarted=false;staAssociated=false;staHadIp=false;ntpStarted=false;
  t=millis();
  if(withRouter){
    // The station is exactly the owner's test sketch that served a page
    // through the home router on this board (2026-11-10): WiFi.mode(WIFI_STA),
    // WiFi.setSleep(false), WiFi.begin(). No radio setting of our own: no
    // bandwidth, protocol, country, scan method or TX power, and the core's
    // own reconnect as in the sketch (on by default, never switched off).
    netWaitBegin();
    WiFi.mode(WIFI_STA);
    netWaitEnd();
    WiFi.setSleep(false);
    staDownSince=millis()?millis():1;
    connectRouter();
    netStep("station start",t);
  }else{
    netWaitBegin();
    WiFi.mode(WIFI_AP);
    netWaitEnd();
    startAP();
    staDownSince=0;
    netStep("own Wi-Fi start",t);
    t=millis();startNameService();netStep("name service",t);
  }
}
// Router setup: one asynchronous scan, the loop keeps serving meanwhile.
void serviceWifiScan(){
  if(!scanBusy)return;
  if(!scanInFlight){
    if((int32_t)(millis()-scanNextAt)<0)return;
    scanStaTemporary=!(WiFi.getMode()&WIFI_MODE_STA);
    uint32_t t=millis();
    if(scanStaTemporary){netWaitBegin();WiFi.mode(WIFI_AP_STA);netWaitEnd();}
    netStep("scan: station on",t);
    scanStartedMs=millis();
    if(WiFi.scanNetworks(true,false)==WIFI_SCAN_FAILED){
      scanCount=WIFI_SCAN_FAILED;scanBusy=false;scanRequested=false;scanCompletedMs=millis()?millis():1;
      if(scanStaTemporary)WiFi.mode(WIFI_AP);
      return;
    }
    scanInFlight=true;return;
  }
  const int16_t r=WiFi.scanComplete();
  if(r==WIFI_SCAN_RUNNING&&(uint32_t)(millis()-scanStartedMs)<15000UL)return;
  scanEntriesCount=0;scanRawCount=r>0?r:0;
  for(int16_t i=0;i<r;i++){
    const String ss=WiFi.SSID(i);if(!ss.length())continue;
    int k=-1;for(uint8_t j=0;j<scanEntriesCount;j++)if(scanEntries[j].ssid==ss){k=j;break;}
    if(k<0){if(scanEntriesCount>=WIFI_SCAN_MAX_RESULTS)continue;k=scanEntriesCount++;scanEntries[k].ssid=ss;scanEntries[k].rssi=-127;}
    if(WiFi.RSSI(i)>scanEntries[k].rssi){
      ScanEntry &x=scanEntries[k];x.rssi=WiFi.RSSI(i);x.channel=(uint8_t)WiFi.channel(i);
      x.auth=(uint8_t)WiFi.encryptionType(i);x.locked=x.auth!=WIFI_AUTH_OPEN;
      const uint8_t *b=WiFi.BSSID(i);if(b)memcpy(x.bssid,b,6);
    }
  }
  WiFi.scanDelete();
  scanCount=r<0?WIFI_SCAN_FAILED:r;
  scanCompletedMs=millis()?millis():1;scanDurationMs=scanCompletedMs-scanStartedMs;
  scanInFlight=false;scanBusy=false;scanRequested=false;
  if(scanStaTemporary){scanStaTemporary=false;const uint32_t t=millis();netWaitBegin();WiFi.mode(WIFI_AP);netWaitEnd();netStep("scan: station off",t);}
}
void serviceNetwork(){
  const uint32_t now=millis();
  if(pendingNetwork&&(int32_t)(now-networkSwitchAt)>=0){
    pendingNetwork=false;routerHold=false;
    // Router mode chosen while the own Wi-Fi is up: start again and join from a
    // clean start, as the owner's sketch does (0.17.1). Every EngineLink
    // session on the home router that began with a switch from the own Wi-Fi
    // lost its pings within a minute; the sketch, which only ever starts as a
    // station, kept them for minutes. Not during a trip: a restart ends it.
    if(routerConfigured()&&WiFi.getMode()!=WIFI_MODE_NULL&&!totals.tripActive&&!restartAt){
      wifiLog("Router chosen: restarting to join from a clean start");
      prepareRestart();restartAt=millis()+300;return;
    }
    applyNetwork();return;
  }
  const bool online=routerConfigured()&&!routerHold&&WiFi.status()==WL_CONNECTED;
  if(online)lastRouterIp=WiFi.localIP().toString();
  else routerSessionEnd(255);   // lost: the reason follows from the event
  netdiagService(now,online);
  // The router's signal is read only when a report or the app asks for it
  // (routerSignalSample); the channel comes from the connected event.
  // Router not reached in 2 minutes: own Wi-Fi, and try again later.
  // Signed: the event task may set staDownSince a millisecond after `now` was
  // read. Unsigned, that gap counted as 49 days and the own Wi-Fi came on at
  // once instead of after 2 minutes (rc3, plan 9.2 item 1).
  if(routerConfigured()&&!routerHold&&!online&&staDownSince&&(int32_t)(now-staDownSince)>=(int32_t)120000UL){
    Serial.println("Router not reached in 2 minutes: EngineLink Wi-Fi on");
    wifiLog("Router not reached in 2 minutes: own Wi-Fi on");
    ++routerFallbacks;routerHold=true;routerRetryAt=now+600000UL;applyNetwork();return;
  }
  // The core reconnects by itself after most failures, but not after code
  // 202 and a few others (STA.cpp): rc4 then stood idle for 90 s. So: 20 s
  // without a join and without any word from the driver, one plain connect.
  // Signed: the event task may write its time a tick after `now` was read.
  if(routerConfigured()&&!routerHold&&!staAssociated&&(int32_t)(now-staTryAt)>=20000&&(int32_t)(now-staLastDropAt)>=20000){staTryAt=now;WiFi.reconnect();}
  if(routerHold&&routerConfigured()&&(int32_t)(now-routerRetryAt)>=0){
    if(WiFi.softAPgetStationNum()==0){routerHold=false;applyNetwork();return;}
    routerRetryAt=now+600000UL;
  }
  if(online&&!ntpStarted&&cfg.ntpOn&&allowTime("ntp")){configTime(0,0,cfg.ntpServer.c_str(),"pool.ntp.org");ntpStarted=true;}
  if(ntpNotification){ntpNotification=false;if(allowTime("ntp")&&validWall()){hasTime=true;timeSource="ntp";backfillTripTime();}}
  serviceWifiScan();
  if(mdnsRefreshPending&&!scanBusy&&(uint32_t)(now-mdnsNetworkChangedAt)>=2000UL)startNameService();
  // Shortly after the own Wi-Fi started or the router gave an IP. The web
  // server binds every interface, so it needs nothing here (plan 6.6).
  if(networkIpEpoch!=netEpochSeen&&!scanBusy&&(uint32_t)(now-networkIpEventAt)>=600UL){
    // Driver setup may reset the TX power after an interface start; check it
    // on every new network in both modes (plan Stage 1b).
    applyTxPower(true);
    netEpochSeen=networkIpEpoch;
    Serial.printf("Network ready at %s\n",online?WiFi.localIP().toString().c_str():WiFi.softAPIP().toString().c_str());
    wifiLog("Network ready");
    if(online)routerSessionBegin();
  }
  // Device report to GitHub (only with a token and the router up).
  if(!reportedThisBoot&&cfg.ghToken.length()&&online&&staIpAt&&(uint32_t)(now-staIpAt)>=120000UL){
    reportedThisBoot=true;if(!reportWanted){reportWanted=true;reportManual=true;reportReason="connected after start";}
  }
  if(reportWanted&&cfg.ghToken.length()&&fwJob==FW_IDLE&&fwState!=3&&online&&staIpAt&&(uint32_t)(now-staIpAt)>=60000UL){
    if(!reportDayStart||(uint32_t)(now-reportDayStart)>=86400000UL){reportDayStart=now?now:1;reportDayCount=0;}
    if(reportManual||(reportDayCount<10&&(!reportLastAt||(uint32_t)(now-reportLastAt)>=1800000UL))){
      String r=buildNetworkReport();
      if(cfg.ssid.length()>=3)r.replace(cfg.ssid,"<router name hidden>");
      reportBody=r;reportWanted=false;reportManual=false;++reportDayCount;fwJob=FW_REPORT;
    }
  }
}
