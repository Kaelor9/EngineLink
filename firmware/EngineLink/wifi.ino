// Wi-Fi: own access point or the boat router (never both), router fallback
// and return, scans, mDNS name and the Wi-Fi event log.

void traceWifi(uint8_t type,uint8_t reason){
  const uint32_t now=millis();
  portENTER_CRITICAL(&traceMux);
  if(traceSize){
    RouterTrace &last=routerTrace[(traceHead+ROUTER_TRACE_CAP-1)%ROUTER_TRACE_CAP];
    if(last.type==type&&last.reason==reason){
      last.lastMs=now;if(last.count<65535)++last.count;
      portEXIT_CRITICAL(&traceMux);
      return;
    }
  }
  routerTrace[traceHead]={now,now,1,type,reason};
  traceHead=(traceHead+1)%ROUTER_TRACE_CAP;
  if(traceSize<ROUTER_TRACE_CAP)++traceSize;
  portEXIT_CRITICAL(&traceMux);
}
uint8_t getWifiTrace(RouterTrace *out){
  portENTER_CRITICAL(&traceMux);
  const uint8_t n=traceSize;
  for(uint8_t i=0;i<n;i++)out[i]=routerTrace[(traceHead+ROUTER_TRACE_CAP-n+i)%ROUTER_TRACE_CAP];
  portEXIT_CRITICAL(&traceMux);
  return n;
}
const char* reasonText(uint8_t r){
  switch(r){
    case 0:return "No reason reported";
    case 8:return "Station deliberately left";
    case 15:return "WPA handshake timeout";
    case 36:return "Station left (mode change or a new attempt)";
    case 200:return "Router beacons lost";
    case 201:return "Router not found";
    case 202:return "Authentication failed (does not prove a wrong password)";
    case 203:return "Router association failed";
    case 204:return "WPA handshake failed";
    case 205:return "Router connection failed";
    case 209:return "WPA3 PMF SA Query timeout (router did not answer)";
    case 210:case 211:return "Incompatible Wi-Fi security";
    default:return "Other Wi-Fi disconnect";
  }
}
const char* traceText(uint8_t t){
  switch(t){
    case 1:return "Connect started";
    case 2:return "Associated with router";
    case 3:return "Received router IP";
    case 4:return "Disconnected";
    case 5:return "Lost router IP";
    case 6:return "Rescue AP fallback";
    default:return "Network event";
  }
}
const char* stationSecurityName(uint8_t mode){
  switch(mode){
    case WIFI_AUTH_OPEN: return "Open";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA-Personal";
    case WIFI_AUTH_WPA2_PSK: return "WPA2-Personal";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2-Personal";
    case WIFI_AUTH_WPA3_PSK: return "WPA3-SAE";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3 mixed";
    default: return "Not yet negotiated";
  }
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
// Router channel for the rescue look (one channel instead of all); kept in
// NVS, written only when the router really moved.
void saveKnownChannel(uint8_t ch){
  if(ch<1||ch>13||ch==routerKnownChannel)return;
  routerKnownChannel=ch;
  Preferences p;if(p.begin("enginelink",false)){p.putUChar("rch",ch);p.end();}
}
// The fixed transmit power (txLevel). Setting the country resets the power
// to 20 dBm, so this runs after it; with `check` it only sets the power when
// the driver reports another one (counted for the report).
void applyTxPower(bool check){
  if(check){
    int8_t q=0;
    if(esp_wifi_get_max_tx_power(&q)!=ESP_OK)return;   // Wi-Fi not running
    if(abs((int)q-(int)elTxPower[txLevel])<=2)return;   // the driver rounds to its steps
    ++txReapplied;
  }
  WiFi.setTxPower(elTxPower[txLevel]);
}
// ---- Wi-Fi, kept simple (0.16.2) ----
// Standalone: EngineLink's own Wi-Fi at 192.168.4.1.
// Router: join the boat router with the Arduino core's own reconnect. No IP
// within 2 minutes: back to the own Wi-Fi, and try the router again every
// 10 minutes while no phone is connected to it.
static uint32_t routerRetryAt=0;
void onWiFi(WiFiEvent_t e,WiFiEventInfo_t info){
  const uint32_t now=millis();
  switch(e){
    case ARDUINO_EVENT_WIFI_AP_START:apStarted=true;apLastEvent=1;apLastEventMs=now;++apStarts;++networkIpEpoch;networkIpEventAt=now;mdnsRefreshPending=true;mdnsNetworkChangedAt=now;break;
    case ARDUINO_EVENT_WIFI_AP_STOP:apLastEvent=2;apLastEventMs=now;break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:staAssociated=true;traceWifi(2);break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:staHadIp=true;staIpAt=now?now:1;staDownSince=0;++networkIpEpoch;networkIpEventAt=now;mdnsRefreshPending=true;mdnsNetworkChangedAt=now;traceWifi(3);break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      staAssociated=false;staLastReason=info.wifi_sta_disconnected.reason;staLastDropAt=now;
      if(!staDownSince)staDownSince=now?now:1;
      traceWifi(4,staLastReason);break;
    default:break;
  }
}
bool routerConfigured(){return cfg.router&&cfg.ssid.length();}
void radioTuning(){WiFi.setSleep(false);}
void startAP(){
  WiFi.softAPConfig(IPAddress(192,168,4,1),IPAddress(192,168,4,1),IPAddress(255,255,255,0));
  apChannelNow=(cfg.apChannel>=1&&cfg.apChannel<=11)?cfg.apChannel:6;
  apStarted=WiFi.softAP(cfg.apName.c_str(),cfg.apPassword.c_str(),apChannelNow,false,6);
  apStartedChannel=apChannelNow;
  Serial.printf("EngineLink Wi-Fi %s: %s, channel %u, %s\n",apStarted?"on":"FAILED",cfg.apName.c_str(),(unsigned)apChannelNow,WiFi.softAPIP().toString().c_str());
}
void connectRouter(){
  traceWifi(1);staBeginAt=millis();++routerAttempts;
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
  if(mdnsReady){MDNS.end();mdnsReady=false;}
  web.closeClients();
  WiFi.mode(WIFI_OFF);
  elAllowSae=withRouter&&staSecMode==1;   // WPA3 only for a WPA3-only router
  WiFi.persistent(false);
  WiFi.setHostname(cfg.hostName.c_str());
  apStarted=false;staAssociated=false;staHadIp=false;ntpStarted=false;
  if(withRouter){
    WiFi.mode(WIFI_STA);
    radioTuning();
    WiFi.setAutoReconnect(true);
    staDownSince=millis()?millis():1;
    connectRouter();
  }else{
    WiFi.mode(WIFI_AP);
    radioTuning();
    startAP();
    staDownSince=0;
    startNameService();
  }
}
// Router setup: one asynchronous scan, the loop keeps serving meanwhile.
void serviceWifiScan(){
  if(!scanBusy)return;
  if(!scanInFlight){
    if((int32_t)(millis()-scanNextAt)<0)return;
    scanStaTemporary=!(WiFi.getMode()&WIFI_MODE_STA);
    if(scanStaTemporary)WiFi.mode(WIFI_AP_STA);
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
  if(scanStaTemporary){scanStaTemporary=false;WiFi.mode(WIFI_AP);}
}
void serviceNetwork(){
  const uint32_t now=millis();
  if(pendingNetwork&&(int32_t)(now-networkSwitchAt)>=0){pendingNetwork=false;routerHold=false;applyNetwork();return;}
  const bool online=routerConfigured()&&!routerHold&&WiFi.status()==WL_CONNECTED;
  if(online)lastRouterIp=WiFi.localIP().toString();
  // Router not reached in 2 minutes: own Wi-Fi, and try again later.
  if(routerConfigured()&&!routerHold&&!online&&staDownSince&&(uint32_t)(now-staDownSince)>=120000UL){
    Serial.println("Router not reached in 2 minutes: EngineLink Wi-Fi on");
    routerHold=true;routerHoldSince=now;routerRetryAt=now+600000UL;applyNetwork();return;
  }
  if(routerHold&&routerConfigured()&&(int32_t)(now-routerRetryAt)>=0){
    if(WiFi.softAPgetStationNum()==0){routerHold=false;applyNetwork();return;}
    routerRetryAt=now+600000UL;
  }
  routerProbeAt=routerRetryAt;
  if(online&&!ntpStarted&&cfg.ntpOn&&allowTime("ntp")){configTime(0,0,cfg.ntpServer.c_str(),"pool.ntp.org");ntpStarted=true;}
  if(ntpNotification){ntpNotification=false;if(allowTime("ntp")&&validWall()){hasTime=true;timeSource="ntp";backfillTripTime();}}
  serviceWifiScan();
  if(mdnsRefreshPending&&!scanBusy&&(uint32_t)(now-mdnsNetworkChangedAt)>=2000UL)startNameService();
  if(httpHealPending){httpHealPending=false;++httpHeals;web.rebindNetwork();}
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
