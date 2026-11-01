// Wi-Fi: own access point or the boat router (never both), router fallback
// and return, scans, mDNS name and the Wi-Fi event log.

void traceWifi(uint8_t type,uint8_t reason){
  const uint32_t now=millis();
  portENTER_CRITICAL(&traceMux);
  routerTrace[traceHead]={now,type,reason};
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
    case 36:return "Station left during mode change";
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
void onWiFi(WiFiEvent_t e,WiFiEventInfo_t info){
  if(e==ARDUINO_EVENT_WIFI_AP_START){
    const uint32_t now=millis();
    ++apStarts;apLastEvent=1;apLastEventMs=now;
    mdnsNetworkChangedAt=now;mdnsRefreshPending=true;
    ++networkIpEpoch;networkIpEventAt=now;
  }
  if(e==ARDUINO_EVENT_WIFI_AP_STOP){++apStops;apLastEvent=2;apLastEventMs=millis();}
  if(e==ARDUINO_EVENT_WIFI_AP_STACONNECTED)++apJoins;
  if(e==ARDUINO_EVENT_WIFI_AP_STADISCONNECTED){++apLeaves;apReason=info.wifi_ap_stadisconnected.reason;}
  if(e==ARDUINO_EVENT_WIFI_STA_CONNECTED){
    const uint32_t now=millis();
    staAssociated=true;staAssociatedAt=now;++staAssociatedEvents;
    saQueryRetryAt=0; // A native reconnect may already have succeeded.
    staNegotiatedAuth=(uint8_t)info.wifi_sta_connected.authmode;
    traceWifi(2);
    const uint32_t from=staLastDropAt && staLastDropAt>staBeginAt?
      staLastDropAt:staBeginAt;
    if(from)staJoinMs=now-from;
  }
  if(e==ARDUINO_EVENT_WIFI_STA_DISCONNECTED){
    const bool droppedOnline=staHadIp;
    staHadIp=false;
    staAssociated=false;
    ++staDisconnects;
    staLastReason=info.wifi_sta_disconnected.reason;
    staLastDropAt=millis();
    traceWifi(4,staLastReason);
    // Count a lost live connection only after a DHCP IP was obtained.
    // Failures BEFORE first association are connection attempts, not drops.
    const bool unexpected=!routerIntentionalShutdown &&
       staLastReason!=WIFI_REASON_ASSOC_LEAVE && staLastReason!=36;
    if(unexpected){
      staLastUnexpectedReason=staLastReason;
      if(droppedOnline)++staUnexpectedDisconnects;
      else ++staAssociationFailures;
      // The Arduino 3.3.12 auto-reconnect list excludes reason 209.
      // Schedule an early one-shot attempt without resetting WPA/PMF state.
      if(staLastReason==WIFI_REASON_SA_QUERY_TIMEOUT && droppedOnline)
        saQueryRetryAt=millis()+2000UL;
      if(staLastReason==2||staLastReason==15){if(txNoAnswer<65535)++txNoAnswer;}
      // The router offers no WPA2 (WPA3-only): start the driver with WPA3.
      if((staLastReason==210||staLastReason==211)&&staSecMode==0){staSecSwitchTo=1;staSecSwitchPending=true;}

    }
  }
  if(e==ARDUINO_EVENT_WIFI_STA_LOST_IP){
    staIpLostAt=millis();++staIpLostEvents;traceWifi(5);
  }
  if(e==ARDUINO_EVENT_WIFI_STA_GOT_IP){
    const uint32_t now=millis();
    routerIntentionalShutdown=false;
    ++staConnects;staConnectedAt=now;staIpAt=now;++staIpEvents;
    staHadIp=true;
    saQueryRetryAt=0;
    routerGotIpThisSession=true;
    esp_wifi_set_ps(WIFI_PS_NONE); // modem sleep adds up to a beacon (~100 ms) to every reply
    txNoAnswer=0;
    traceWifi(3);
    if(staAssociatedAt)staDhcpMs=now-staAssociatedAt;
    mdnsNetworkChangedAt=now;mdnsRefreshPending=true;
    ++networkIpEpoch;networkIpEventAt=now;
  }
}
void captureWifiScan(int16_t total){
  // Merge a channel's results into the running unique-SSID list.
  // Clear the list once at the beginning of a full scan, not on each channel.
  for(int16_t i=0;i<total;i++){
    const String ssid=WiFi.SSID(i);
    if(!ssid.length())continue;
    const int32_t rssi=WiFi.RSSI(i);
    int slot=-1;
    for(uint8_t k=0;k<scanEntriesCount;k++){
      if(scanEntries[k].ssid==ssid){slot=k;break;}
    }
    if(slot>=0 && rssi<=scanEntries[slot].rssi)continue;
    if(slot<0){
      if(scanEntriesCount<WIFI_SCAN_MAX_RESULTS)slot=scanEntriesCount++;
      else{
        uint8_t weakest=0;
        for(uint8_t k=1;k<scanEntriesCount;k++)
          if(scanEntries[k].rssi<scanEntries[weakest].rssi)weakest=k;
        if(rssi<=scanEntries[weakest].rssi)continue;
        slot=weakest;
      }
    }
    ScanEntry &entry=scanEntries[slot];
    entry.ssid=ssid;
    entry.rssi=rssi;
    entry.channel=(uint8_t)WiFi.channel(i);
    entry.locked=WiFi.encryptionType(i)!=WIFI_AUTH_OPEN;
    entry.auth=(uint8_t)WiFi.encryptionType(i);
    const uint8_t *b=WiFi.BSSID(i);
    if(b)memcpy(entry.bssid,b,sizeof(entry.bssid));
    else memset(entry.bssid,0,sizeof(entry.bssid));
  }
  // Stable strongest-first display regardless of discovery order.
  for(uint8_t i=0;i<scanEntriesCount;i++)
    for(uint8_t j=i+1;j<scanEntriesCount;j++)
      if(scanEntries[j].rssi>scanEntries[i].rssi){
        ScanEntry tmp=scanEntries[i];scanEntries[i]=scanEntries[j];scanEntries[j]=tmp;
      }
  WiFi.scanDelete(); // channel records are copied to our own stable cache
}
// ---------- Wi-Fi: exclusive normal operating modes ----------
// Standalone uses only the EngineLink SoftAP; router operation uses only STA.
// During explicit router discovery only, STA is temporarily enabled alongside
// the AP to scan 2.4 GHz channels and is disabled after the results are served.
// No connection to an external router occurs before the user presses Connect.
bool routerConfigured(){return cfg.router&&cfg.ssid.length();}
void radioTuning(){
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
  // Full power reaches furthest, but its current peaks can upset the radio on
  // a board with a weak supply or a chip antenna; a lower setting can then
  // hold the connection better (Diagnostics → Wi-Fi → Transmit power).
  if(txLevel>4)txLevel=3;
  WiFi.setTxPower(elTxPower[txLevel]);
  // Ride out a few seconds of not hearing the router (default 6 s) instead of
  // dropping and logging in again on a weak or busy link.
  esp_wifi_set_inactive_time(WIFI_IF_STA,20);
  // Setting the country again on a running network makes it start over and
  // drops every phone, so it is only set when it is not already right.
  wifi_country_t cur={};
  if(esp_wifi_get_country(&cur)!=ESP_OK||cur.cc[0]!='E'||cur.cc[1]!='U'||cur.schan!=1||cur.nchan!=13||cur.policy!=WIFI_COUNTRY_POLICY_MANUAL){
    wifi_country_t cc={{'E','U',' '},1,13,20,WIFI_COUNTRY_POLICY_MANUAL};
    esp_wifi_set_country(&cc);
  }
}
void startAP(){
  // Reapplying softAP configuration can interrupt associated phones. Do not
  // touch a healthy AP when its actual desired settings have not changed.
  static String activeName,activePassword;
  if(apStarted&&(WiFi.getMode()&WIFI_MODE_AP)&&apLastEvent!=2
     &&WiFi.softAPIP()==IPAddress(192,168,4,1)
     &&apStartedChannel==apChannelNow
     &&activeName==cfg.apName&&activePassword==cfg.apPassword)return;
  if(WiFi.softAPIP()!=IPAddress(192,168,4,1))WiFi.softAPConfig(IPAddress(192,168,4,1),IPAddress(192,168,4,1),IPAddress(255,255,255,0));
  apStarted=WiFi.softAP(cfg.apName.c_str(),cfg.apPassword.c_str(),apChannelNow,false,6);
  esp_wifi_set_inactive_time(WIFI_IF_AP,600);
  if(apStarted){
    if(apStartedChannel&&apStartedChannel!=apChannelNow)++apChannelChanges;
    apStartedChannel=apChannelNow;
    activeName=cfg.apName;activePassword=cfg.apPassword;
  }
}
void connectRouter(){
  // The standard ESP32 station path: configure the credentials once and let
  // Arduino's built-in reconnect handle router dropouts.
  staBssidKnown=false;
  staBeginAt=millis();
  traceWifi(1);
  routerIntentionalShutdown=false;
  // Arduino's regular begin() connects immediately. Configure the SAE station
  // BEFORE the first handshake, preserving WPA2, WPA3 and transition support.
  // Explicitly set H2E + Hunt-and-Peck and a blank SAE password identifier:
  // old/migrated Wi-Fi configuration can otherwise carry unsuitable SAE defaults.
  // PMF is capable but not required, allowing WPA2 while retaining WPA3 support.
  staSecurityConfigured=false;
  staSecurityConfigError=ESP_OK;
  const char *password=cfg.wifiPassword.length()?cfg.wifiPassword.c_str():nullptr;
  const wl_status_t result=WiFi.begin(cfg.ssid.c_str(),password,0,nullptr,false);
  if(result!=WL_CONNECT_FAILED){
    wifi_config_t staCfg={};
    esp_err_t err=esp_wifi_get_config(WIFI_IF_STA,&staCfg);
    if(err==ESP_OK){
      staCfg.sta.pmf_cfg.capable=true;
      staCfg.sta.pmf_cfg.required=false;
      staCfg.sta.sae_pwe_h2e=WPA3_SAE_PWE_BOTH;
      memset(staCfg.sta.sae_h2e_identifier,0,sizeof(staCfg.sta.sae_h2e_identifier));
      // A threshold is a MINIMUM, NOT a forced WPA2 downgrade. WPA3 still wins
      // automatically whenever the router and station negotiate it successfully.
      staCfg.sta.threshold.authmode=password?WIFI_AUTH_WPA2_PSK:WIFI_AUTH_OPEN;
      err=esp_wifi_set_config(WIFI_IF_STA,&staCfg);
    }
    staSecurityConfigError=err;
    if(err==ESP_OK){
      staSecurityConfigured=true;
      err=esp_wifi_connect();
      if(err!=ESP_OK)staSecurityConfigError=err;
    }
    if(err!=ESP_OK){
      // Safe fallback to Arduino's known-good default path; never strand the
      // user in STA without a live connection attempt.
      Serial.printf("Router explicit SAE setup failed (%s); using default Wi-Fi connect\n",
                    esp_err_to_name(err));
      WiFi.begin(cfg.ssid.c_str(),password);
    }
  }else{
    staSecurityConfigError=ESP_FAIL;
    Serial.println("Router STA configuration could not start");
  }
  routerAttempts++;
  Serial.printf("Router association started: SSID=%s, SAE H2E+HnP=%u, setup=%s\n",
    cfg.ssid.c_str(),(unsigned)staSecurityConfigured,esp_err_to_name(staSecurityConfigError));
}
// A name for http://<name>.local: letters, digits and hyphens, as DNS allows.
bool validHostName(const String &n){
  if(n.length()<1||n.length()>32||n[0]=='-'||n[n.length()-1]=='-')return false;
  for(size_t i=0;i<n.length();i++){const char c=n[i];if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'))return false;}
  return true;
}
// mDNS answers <name>.local on the EngineLink network and on the router; the
// router also learns the name from DHCP.
void startNameService(){
  // Initialize and announce only from the main loop, never the Wi-Fi callback.
  // The ESP-IDF responder registers both AP and STA interfaces. A successful
  // begin alone is not enough: verify that the HTTP service was registered.
  mdnsLastAttemptAt=millis();
  mdnsRefreshPending=false;
  if(mdnsReady){MDNS.end();mdnsReady=false;}
  if(MDNS.begin(cfg.hostName.c_str())){
    if(MDNS.addService("http","tcp",80)){
      MDNS.setInstanceName(cfg.apName.c_str());
      mdnsReady=true;
      ++mdnsRestarts;
      Serial.printf("mDNS ready: http://%s.local (AP %s)\n",
                    cfg.hostName.c_str(),WiFi.softAPIP().toString().c_str());
      return;
    }
  }
  // A partial init/service registration must not be left behind: retry later.
  MDNS.end();
  ++mdnsFailures;
  Serial.println("mDNS setup failed; will retry without restarting Wi-Fi");
}
// Fail open to the device's own Wi-Fi if the boat router cannot be reached.
// Store the hold in NVS, not in LittleFS or the user's router credentials;
// subsequent reboots immediately start in AP-only until Connect is selected.
void parkRouter(const char *why){
  if(routerHold)return;
  // Filed once the router is back (no internet in standalone).
  reportWanted=true;reportReason=String("router lost (")+why+")";
  // Snapshot diagnostics BEFORE intentional STA disconnect changes the last
  // Wi-Fi reason to a voluntary leave (reason 8 or 36).
  routerFailureReason=staLastUnexpectedReason?staLastUnexpectedReason:staLastReason;
  routerFailurePhase=staAssociated?2:1;
  routerFailureAfterMs=routerGuard.disconnectedSince?
    (uint32_t)(millis()-routerGuard.disconnectedSince):0;
  traceWifi(6,routerFailureReason);
  routerIntentionalShutdown=true;
  Serial.printf("Router unavailable (%s): last Wi-Fi reason=%u, phase=%s; restoring standalone AP\n",
    why,(unsigned)routerFailureReason,routerFailurePhase==2?"dhcp":"association");
  routerHold=true;
  ++routerFallbacks;
  routerHoldSince=millis()?millis():1;
  // A fallback says nothing about security: the router may simply have been
  // off or restarting. WPA3 is chosen only on clear evidence (WPA3-only in the
  // scan, or codes 210/211); a router that needs WPA2 is never pushed to WPA3.
  if(routerAutoReturning){routerAutoReturning=false;routerProbeEvery=min<uint32_t>(routerProbeEvery*2,7200000UL);}
  routerProbeAt=millis()+routerProbeEvery;
  Preferences p;
  if(p.begin("enginelink",false)){
    p.putBool("rhold",true);
    p.putUChar("rrsn",routerFailureReason);
    p.putUChar("rstage",routerFailurePhase);
    p.putULong("rduration",routerFailureAfterMs);
    p.end();
  }
  routerGuard.reset();
  saQueryRetryAt=0;
  staHadIp=false;
  staDownSince=0; // Do not count rescue time as router downtime.
  staBssidKnown=false;
  if(scanInFlight)esp_wifi_scan_stop();
  scanBusy=false;scanRequested=false;scanInFlight=false;
  scanRestoreAt=0;scanCount=-1;
  WiFi.scanDelete();
  WiFi.setAutoReconnect(false); // stop driver retrying while entering rescue
  WiFi.disconnect(false,false);
  // Recovery uses EngineLink standalone Wi-Fi ONLY.
  WiFi.mode(WIFI_AP);
  // WiFi.mode(WIFI_AP) has already disabled STA and created the AP
  // interface. Do not call softAPdisconnect(false) here: it clears the
  // access point configuration we are trying to restore.
  apStarted=false;apLastEvent=0;
  apChannelNow=(cfg.apChannel>=1&&cfg.apChannel<=11)?cfg.apChannel:6;
  startAP();
  Serial.printf("Rescue AP started=%u IP=%s SSID=%s channel=%u\n",
    (unsigned)apStarted,WiFi.softAPIP().toString().c_str(),
    cfg.apName.c_str(),(unsigned)apChannelNow);
}
void applyNetwork(){
  routerIntentionalShutdown=true; // suppress false disconnect reason on mode change
  // The only NORMAL operating modes:
  //   AP  = EngineLink standalone, local dashboard with no router.
  //   STA = boat router, EngineLink SoftAP is completely disabled.
  // Scan provisioning may temporarily enable AP+STA; no router association
  // occurs until the user explicitly selects it.
  const bool withRouter=routerConfigured()&&!routerHold;
  if(scanInFlight)esp_wifi_scan_stop();
  scanBusy=false;scanRequested=false;scanInFlight=false;
  scanRestoreAt=0;scanStaTemporary=false;
  WiFi.scanDelete();
  if(mdnsReady){MDNS.end();mdnsReady=false;}
  mdnsRefreshPending=false;
  // Hostname must be assigned before Wi-Fi is started (ESP32 requirement).
  // A real mode change may drop old clients, but it must never happen in
  // normal operating loops or between two reconnect attempts.
  web.closeClients();
  WiFi.mode(WIFI_OFF);
  // Read by wifiwpa3.cpp when the driver starts again below.
  elAllowSae=routerConfigured()&&!routerHold&&staSecMode==1;
  WiFi.persistent(false);
  WiFi.setHostname(cfg.hostName.c_str());
  apStarted=false;
  staAssociated=false;
  staHadIp=false;
  saQueryRetryAt=0;
  ntpStarted=false;
  if(withRouter){
    WiFi.mode(WIFI_STA);         // never broadcast EngineLink in router mode
    WiFi.setAutoReconnect(true);
    radioTuning();
    // Stock fast scan and strongest-AP selection; never pin to a possibly
    // stale BSSID or channel.
    wifi_bandwidth_t bw;
    if(esp_wifi_get_bandwidth(WIFI_IF_STA,&bw)==ESP_OK&&bw!=WIFI_BW_HT20)
      esp_wifi_set_bandwidth(WIFI_IF_STA,WIFI_BW_HT20);
    routerGuard.reset();
    routerGuard.attempt(millis());
    routerGotIpThisSession=false;
    staSecSwitchPending=false;
    staDownSince=millis()?millis():1;
    routerLastRecoveryAttempt=millis();
    routerAttempts=0;
    routerForcedReconnects=0;
    staFails=0;
    connectRouter();            // only one begin() per selected network
  }else{
    WiFi.setAutoReconnect(false);
    WiFi.mode(WIFI_AP);         // always pure SoftAP in standalone/rescue
    radioTuning();
    wifi_bandwidth_t bw;
    if(esp_wifi_get_bandwidth(WIFI_IF_AP,&bw)==ESP_OK&&bw!=WIFI_BW_HT20)
      esp_wifi_set_bandwidth(WIFI_IF_AP,WIFI_BW_HT20);
    apChannelNow=(cfg.apChannel>=1&&cfg.apChannel<=11)?cfg.apChannel:6;
    startAP();
    routerGuard.reset();
    staDownSince=0;
    staBssidKnown=false;
    startNameService();
  }
}
// Advance discovery without blocking loop() or monopolizing the shared AP
// radio for a continuous all-channel sweep. Each channel gets a full active
// dwell time, followed by a gap in which normal AP traffic can be serviced.
// A scan is only launched by the user's explicit request on the router page.
// On each completed channel, advance to the next channel or start the second
// (passive) survey. Results stay in the cache until both passes are done.
void advanceScanChannel(){
  if(scanChannel==SCAN_CHANNEL_LAST && scanPass<SCAN_PASSES){
    scanPassOneCount=scanEntriesCount;
    ++scanPass;
    scanChannel=SCAN_CHANNEL_FIRST;
  }else{
    ++scanChannel;
  }
}
void serviceWifiScan(){
  const uint32_t now=millis();
  if(scanRequested && (int32_t)(now-scanNextAt)>=0){
    scanRequested=false;
    scanPass=1;
    scanChannel=SCAN_CHANNEL_FIRST;
    scanStartedMs=now;
    scanInFlight=false;
    scanRestoreAt=0;
    scanStartRetry=0;
    // A second scan during the discovery window must not forget that STA
    // was enabled temporarily by an earlier scan.
    scanStaTemporary=scanStaTemporary || !(WiFi.getMode()&WIFI_MODE_STA);
    if(!(WiFi.getMode()&WIFI_MODE_STA) && !WiFi.mode(WIFI_AP_STA)){
      scanBusy=false;scanCount=WIFI_SCAN_FAILED;
      ++scanFailures;
      Serial.println("Wi-Fi scan: cannot enable temporary station interface");
      return;
    }
    // Arduino's default min active dwell is 100 ms, and the radio can leave
    // a channel after min dwell even when max dwell is set to 220 ms.
    // Zero min requests the full max dwell per Espressif's scan semantics.
    WiFi.setScanActiveMinTime(0);
    WiFi.scanDelete();
    scanNextAt=millis()+SCAN_GAP_MS;
    Serial.println("Wi-Fi discovery: two-pass active and passive 2.4 GHz scan");
  }
  if(scanBusy && !scanRequested && scanChannel>=SCAN_CHANNEL_FIRST){
    if((uint32_t)(now-scanStartedMs)>SCAN_TIMEOUT_MS){
      ++scanFailures;
      if(scanInFlight)esp_wifi_scan_stop();
      scanInFlight=false;
      WiFi.scanDelete();
      scanPass=SCAN_PASSES;
      scanChannel=SCAN_CHANNEL_LAST+1;
    }else if(scanChannel<=SCAN_CHANNEL_LAST){
      if(scanInFlight){
        const int16_t n=WiFi.scanComplete();
        if(n!=WIFI_SCAN_RUNNING){
          scanInFlight=false;
          if(n>=0){
            scanRawCount+=(uint16_t)n;
            captureWifiScan(n);
          }else{
            ++scanFailures;
            WiFi.scanDelete();
          }
          advanceScanChannel();
          scanStartRetry=0;
          scanNextAt=millis()+SCAN_GAP_MS;
        }
      }else if((int32_t)(now-scanNextAt)>=0){
        const bool passive=scanPass==SCAN_PASSES;
        const int16_t start=WiFi.scanNetworks(true,false,passive,
          passive?SCAN_PASSIVE_DWELL_MS:SCAN_DWELL_MS,scanChannel);
        if(start==WIFI_SCAN_RUNNING){
          scanInFlight=true;
        }else if(start>=0){
          scanRawCount+=(uint16_t)start;
          captureWifiScan(start);
          advanceScanChannel();
          scanStartRetry=0;
          scanNextAt=millis()+SCAN_GAP_MS;
        }else if(++scanStartRetry<3){
          ++scanStartFailures;
          scanNextAt=millis()+250;
        }else{
          ++scanFailures;
          advanceScanChannel();
          scanStartRetry=0;
          scanNextAt=millis()+SCAN_GAP_MS;
        }
      }
    }
    if(scanChannel>SCAN_CHANNEL_LAST){
      // Restore Arduino's usual setting for any later scan.
      WiFi.setScanActiveMinTime(100);
      scanBusy=false;
      scanCount=scanEntriesCount>0||scanFailures==0?scanEntriesCount:WIFI_SCAN_FAILED;
      scanCompletedMs=millis();
      scanDurationMs=(uint32_t)(scanCompletedMs-scanStartedMs);
      // Give Safari time to receive the final network list before returning
      // from temporary AP+STA discovery to the normal AP-only mode.
      scanRestoreAt=millis()+15000UL;
      Serial.printf("Wi-Fi discovery: %u SSIDs (%u raw), %u failed channels over %u passes in %lu ms\n",
        (unsigned)scanEntriesCount,(unsigned)scanRawCount,
        (unsigned)scanFailures,(unsigned)SCAN_PASSES,(unsigned long)scanDurationMs);
    }
  }
  if(scanRestoreAt && !scanBusy && !scanRequested &&
     (int32_t)(millis()-scanRestoreAt)>=0){
    scanRestoreAt=0;
    if(scanStaTemporary && (!routerConfigured()||routerHold)){
      WiFi.mode(WIFI_AP);
      scanStaTemporary=false;
    }
  }
}
void serviceNetwork(){
  // Give the HTTP success reply time to reach the phone before turning
  // its current Wi-Fi interface off. All work still runs in the main loop.
  if(pendingNetwork && (int32_t)(millis()-networkSwitchAt)>=0){
    pendingNetwork=false;
    applyNetwork();
  }
  // AP recovery should never cause a reset loop on transient event timing.
  // Only act on a sustained stopped/missing-interface state. Do not call
  // esp_wifi_start() from here: softAP() establishes the interface itself.
  // A requested mode switch has a response grace period. The AP watchdog
  // must not pre-empt it by starting SoftAP as soon as cfg.router changes.
  if(!pendingNetwork&&(!routerConfigured()||routerHold)&&(uint32_t)(millis()-lastApCheck)>=15000){
    lastApCheck=millis();
    const bool modeOk=(WiFi.getMode()&WIFI_MODE_AP)!=0;
    const bool stopped=apLastEvent==2&&(uint32_t)(millis()-apLastEventMs)>=10000;
    const bool missingIp=WiFi.softAPIP()!=IPAddress(192,168,4,1);
    if(!modeOk||stopped||missingIp){
      ++apRestarts;
      apStarted=false;
      if(!modeOk)WiFi.mode(WIFI_AP);
      startAP();
    }
  }
  if(txNoAnswer>=12&&routerConfigured()&&!routerHold&&!pendingNetwork){
    txNoAnswer=0;
    if(txLevel>0){
      --txLevel;++txAutoSteps;
      {Preferences tp;if(tp.begin("enginelink",false)){tp.putUChar("txal",txLevel);tp.end();}}
      WiFi.setTxPower(elTxPower[txLevel]);
      Serial.printf("Router does not answer: transmit power lowered to %s\n",elTxName[txLevel]);
    }
  }
  if(staSecSwitchPending&&routerConfigured()&&!routerHold&&!pendingNetwork){
    staSecSwitchPending=false;
    if(staSecMode!=staSecSwitchTo){
      staSecMode=staSecSwitchTo;++staSecSwitches;saveSecMode();saQueryRetryAt=0;
      Serial.printf("Router security switched to %s\n",staSecMode?"WPA3 allowed":"WPA2");
      // The driver must start again for the change: a normal network re-apply.
      networkSwitchAt=millis();pendingNetwork=true;
    }
  }
  // Standalone after a router fallback: listen for the saved router now and
  // then, with EngineLink's own Wi-Fi kept on, and go back to router mode
  // when it is there. Not while someone is using the app on EngineLink's own
  // Wi-Fi (a request in the last 2 minutes), so nobody is cut off mid-use.
  if(routerProbing){
    const int16_t r=WiFi.scanComplete();
    if(r>=0||r==WIFI_SCAN_FAILED||(uint32_t)(millis()-routerProbeStarted)>10000){
      bool seen=false;
      for(int16_t i=0;i<r;i++)if(WiFi.SSID(i)==cfg.ssid){seen=true;break;}
      WiFi.scanDelete();
      routerProbing=false;
      if(!seen&&!routerProbeHadSta)WiFi.mode(WIFI_AP);
      if(seen&&routerHold){
        Serial.println("Saved router is back: returning to router mode");
        routerHold=false;routerGuard.reset();routerAttempts=0;
        routerAutoReturning=true;++routerAutoReturns;
        {Preferences pr;if(pr.begin("enginelink",false)){pr.putBool("rhold",false);pr.end();}}
        networkSwitchAt=millis();
        pendingNetwork=true;
      }
    }
  }else if(routerHold&&routerConfigured()&&!pendingNetwork&&!scanBusy&&!otaActive
     &&(int32_t)(millis()-routerProbeAt)>=0
     &&!(WiFi.softAPgetStationNum()>0&&(uint32_t)(millis()-lastHttpMs)<120000UL
         &&(!routerHoldSince||(uint32_t)(millis()-routerHoldSince)<1800000UL))){
    routerProbeAt=millis()+routerProbeEvery;
    routerProbeHadSta=(WiFi.getMode()&WIFI_MODE_STA)!=0;
    if(!routerProbeHadSta)WiFi.mode(WIFI_AP_STA);
    // Asynchronous: the loop keeps serving while the radio listens.
    if(WiFi.scanNetworks(true,true,false,300,0,cfg.ssid.c_str())!=WIFI_SCAN_FAILED){routerProbing=true;routerProbeStarted=millis();}
    else if(!routerProbeHadSta)WiFi.mode(WIFI_AP);
  }
  // The driver, not EngineLink, reconnects to the selected router. This
  // avoids resetting in-flight WPA handshakes and DHCP exchanges.
  // EngineLink only guards against indefinite invisibility, and supplies a
  // very infrequent last-resort reconnect if the STA becomes stuck idle.
  if(routerConfigured()&&!routerHold){
    const uint32_t now=millis();
    const bool hasIp=WiFi.status()==WL_CONNECTED &&
                     WiFi.localIP()!=IPAddress(0,0,0,0);
    // Signal, channel and IP are read from the Wi-Fi driver once a second,
    // not on every pass of loop(): each read waits for the driver's own
    // task, and about 1000 reads a second made every packet through the
    // router wait (pings of 40-150 ms, downloads of a few kB/s).
    static uint32_t staInfoAt=0;
    const bool staInfoDue=staDownSince||!staInfoAt||(uint32_t)(now-staInfoAt)>=1000UL;
    if(hasIp&&staInfoDue){
       staInfoAt=now?now:1;
       lastRouterIp=WiFi.localIP().toString();
       lastRouterRssi=WiFi.RSSI();
       lastRouterChannel=WiFi.channel();
       if(staDownSince)Serial.printf("Router ready: %s RSSI=%d dBm\n",
        lastRouterIp.c_str(),lastRouterRssi);
       if(lastRouterChannel){staChannel=(uint8_t)lastRouterChannel;apChannelNow=(uint8_t)lastRouterChannel;}
    }
    if(hasIp){
      routerGuard.reset();
      routerLastSuccessMs=now;
      staDownSince=0;
      if(routerAutoReturning){routerAutoReturning=false;routerProbeEvery=600000UL;}
    }else{
      // A PMF SA Query timeout can leave Arduino's station idle because its
      // native reconnect list omits error 209. Run one retry ~2 s later,
      // OUTSIDE the event callback, without disconnecting a live WPA3 link.
      const uint32_t saRetry=saQueryRetryAt;
      if(saRetry && !staAssociated && (int32_t)(now-saRetry)>=0){
        saQueryRetryAt=0;
        const esp_err_t saErr=esp_wifi_connect();
        if(saErr==ESP_OK){
          ++saQueryFastRetries;
          ++routerAttempts;
          routerLastRecoveryAttempt=now;
        }else if(saErr!=ESP_ERR_WIFI_STATE){
          ++staFails;
        }
        Serial.printf("WPA3 SA Query recovery: %s\n",esp_err_to_name(saErr));
      }
      if(!staDownSince){
        staDownSince=now?now:1;
        // A link that was up for hours gets a fresh grace window. Do not
        // issue a forced reconnect immediately after the driver's first
        // DISCONNECTED event; native auto-reconnect gets the first 30s.
        routerLastRecoveryAttempt=now;
      }
      if(!routerGuard.disconnectedSince)routerGuard.attempt(now);
      // A router that answers but is slow to let EngineLink in (logins timing
      // out, codes 2/4/15/202/204) can need about two minutes and many tries:
      // give it up to 5 minutes. A router that is not there at all (201 and
      // the like) still falls back after 120 s.
      const uint8_t lr=staLastReason;
      const bool slowLogin=lr==2||lr==4||lr==202||lr==204||lr==15;
      if(routerGuard.expired(now)&&(!slowLogin||(uint32_t)(now-routerGuard.disconnectedSince)>=300000UL)){
        // User-visible recovery, not a reboot loop.
        parkRouter(slowLogin?"router did not let EngineLink in within 5 minutes":"no router DHCP address after 120 seconds");
        return;
      }
      // The native auto-reconnect normally handles all recoverable failures.
      // If the radio has remained genuinely unassociated for 30 seconds,
      // nudge it once with reconnect() (never another WiFi.begin()).
      // Only nudge a truly silent driver. Never compete with native retries.
      if(!staAssociated &&
         (!staLastDropAt||(uint32_t)(now-staLastDropAt)>=ROUTER_LAST_RESORT_RETRY_MS) &&
         (uint32_t)(now-routerLastRecoveryAttempt)>=ROUTER_LAST_RESORT_RETRY_MS){
        routerLastRecoveryAttempt=now;
        // esp_wifi_connect() does NOT forcibly disconnect an ongoing WPA
        // handshake. WiFi.reconnect() does, and used to restart attempts at
        // 30-second intervals even when the router was still negotiating.
        const esp_err_t err=esp_wifi_connect();
        if(err==ESP_OK){++routerForcedReconnects;++routerAttempts;}
        else if(err!=ESP_ERR_WIFI_STATE){++staFails;}
        Serial.printf("Router retry nudge: %s (associated=%u, reason=%u)\n",
          esp_err_to_name(err),(unsigned)staAssociated,(unsigned)staLastReason);
      }
    }
  }
  if(routerConfigured()&&WiFi.status()==WL_CONNECTED&&!ntpStarted&&cfg.ntpOn&&allowTime("ntp")){
    configTime(0,0,cfg.ntpServer.c_str(),"pool.ntp.org");
    ntpStarted=true;
  }
  if(ntpNotification){
    ntpNotification=false;
    if(allowTime("ntp")&&validWall()){hasTime=true;timeSource="ntp";backfillTripTime();}
  }
  // Keep progressing the radio scan while ordinary web requests are served.
  serviceWifiScan();
  // Refresh Bonjour after AP recreation or router DHCP; retry failed init.
  // Debounce network events and never reset the Wi-Fi interface to fix mDNS.
  // Avoid restarting mDNS in the middle of a radio scan.
  const uint32_t mdnsNow=millis();
  const bool mdnsInterfaceReady=
    (routerConfigured()&&!routerHold)
      ? (WiFi.status()==WL_CONNECTED && (WiFi.getMode()&WIFI_MODE_STA))
      : ((WiFi.getMode()&WIFI_MODE_AP) &&
         WiFi.softAPIP()==IPAddress(192,168,4,1));
  // A listener opened on the previous interface may retain stale lwIP
  // socket state when we deliberately change AP -> STA or receive a new
  // router DHCP address. Rebind only AFTER the IP interface is ready, from
  // the main loop between request handlers, never from the event callback.
  if(httpHealPending){
    httpHealPending=false;++httpHeals;
    reportWanted=true;reportReason="web server stopped answering and was restarted";
    Serial.println("HTTP self-check failed: closing connections and restarting the web server");
    web.rebindNetwork();
  }
  // Once per start (also after an update): a report when the router link has
  // held for 2 minutes, so it is known that this version connects.
  if(!reportedThisBoot&&cfg.ghToken.length()&&routerConfigured()&&!routerHold&&staIpAt
     &&WiFi.status()==WL_CONNECTED&&(uint32_t)(millis()-staIpAt)>=120000UL){
    reportedThisBoot=true;
    if(!reportWanted){reportWanted=true;reportManual=true;reportReason="connected after start";}
  }
  if(reportWanted&&cfg.ghToken.length()&&fwJob==FW_IDLE&&fwState!=3&&routerConfigured()&&!routerHold
     &&WiFi.status()==WL_CONNECTED&&WiFi.localIP()!=IPAddress(0,0,0,0)
     &&staIpAt&&(uint32_t)(millis()-staIpAt)>=60000UL){  // let the link settle first
    if(!reportDayStart||(uint32_t)(millis()-reportDayStart)>=86400000UL){reportDayStart=millis()?millis():1;reportDayCount=0;}
    if(reportManual||(reportDayCount<10&&(!reportLastAt||(uint32_t)(millis()-reportLastAt)>=1800000UL))){
      String r=buildNetworkReport();
      if(cfg.ssid.length()>=3)r.replace(cfg.ssid,"<router name hidden>");
      reportBody=r;reportWanted=false;reportManual=false;++reportDayCount;
      fwJob=FW_REPORT;
    }
  }
  if(!pendingNetwork && !scanBusy && mdnsInterfaceReady &&
     networkIpEpoch!=httpBoundEpoch &&
     (uint32_t)(mdnsNow-networkIpEventAt)>=600UL){
    web.rebindNetwork();
    httpBoundEpoch=networkIpEpoch;
    httpBoundAt=mdnsNow;
    Serial.printf("HTTP/80 bound to active Wi-Fi interface (epoch %lu)\n",
      (unsigned long)httpBoundEpoch);
  }
  if(!scanBusy && mdnsInterfaceReady){
    const bool retry=!mdnsReady && (uint32_t)(mdnsNow-mdnsLastAttemptAt)>=15000UL;
    const bool refresh=mdnsRefreshPending &&
      (uint32_t)(mdnsNow-mdnsNetworkChangedAt)>=2000UL &&
      (uint32_t)(mdnsNow-mdnsLastAttemptAt)>=5000UL;
    if(retry||refresh)startNameService();
  }
}
