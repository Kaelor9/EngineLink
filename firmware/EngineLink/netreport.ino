// Network report and health JSON and the access point link diagnostics.

// Link diagnostics (plan Stage 1d, F20): what the driver really runs on the
// access point and how each joined phone is heard. Only the maker prefix of a
// phone's MAC address is shown.
ApLink readApLink(){
  ApLink a;
  if(!(WiFi.getMode()&WIFI_MODE_AP))return a;
  a.up=true;
  uint8_t pri=0;wifi_second_chan_t sec=WIFI_SECOND_CHAN_NONE;if(esp_wifi_get_channel(&pri,&sec)==ESP_OK)a.ch=pri;
  wifi_bandwidth_t b=WIFI_BW_HT20;if(esp_wifi_get_bandwidth(WIFI_IF_AP,&b)==ESP_OK)a.bw=b==WIFI_BW_HT40?40:20;
  wifi_config_t c;if(esp_wifi_get_config(WIFI_IF_AP,&c)==ESP_OK){a.pmfCapable=c.ap.pmf_cfg.capable;a.pmfRequired=c.ap.pmf_cfg.required;}
  uint16_t t=0;if(esp_wifi_get_inactive_time(WIFI_IF_AP,&t)==ESP_OK)a.inactiveS=t;
  esp_wifi_get_country_code(a.cc);a.cc[2]=0;
  return a;
}
// One line per joined phone: maker prefix, signal at EngineLink, b/g/n/LR.
String apStationsText(const char *sep){
  String s;wifi_sta_list_t l;
  if(!(WiFi.getMode()&WIFI_MODE_AP)||esp_wifi_ap_get_sta_list(&l)!=ESP_OK||!l.num)return "none";
  for(int i=0;i<l.num;i++){
    const wifi_sta_info_t &x=l.sta[i];char b[64];
    snprintf(b,sizeof(b),"%02X:%02X:%02X %d dBm %s%s%s%s",x.mac[0],x.mac[1],x.mac[2],(int)x.rssi,
      x.phy_11b?"b":"",x.phy_11g?"g":"",x.phy_11n?"n":"",x.phy_lr?" LR":"");
    if(i)s+=sep;s+=b;
  }
  return s;
}
void appendApLinkJson(String &s){
  const ApLink a=readApLink();
  s+=",\"apLink\":{\"up\":";s+=a.up?"true":"false";s+=",\"ch\":";s+=a.ch;s+=",\"bw\":";s+=a.bw;
  s+=",\"pmfCapable\":";s+=a.pmfCapable?"true":"false";s+=",\"pmfRequired\":";s+=a.pmfRequired?"true":"false";
  s+=",\"inactiveS\":";s+=a.inactiveS;s+=",\"country\":";jsonString(s,String(a.cc));
  s+=",\"stations\":[";
  wifi_sta_list_t l;
  if(a.up&&esp_wifi_ap_get_sta_list(&l)==ESP_OK)for(int i=0;i<l.num;i++){
    const wifi_sta_info_t &x=l.sta[i];char b[12];snprintf(b,sizeof(b),"%02X:%02X:%02X",x.mac[0],x.mac[1],x.mac[2]);
    if(i)s+=',';s+="{\"oui\":\"";s+=b;s+="\",\"rssi\":";s+=(int)x.rssi;
    s+=",\"phy\":\"";s+=x.phy_11b?"b":"";s+=x.phy_11g?"g":"";s+=x.phy_11n?"n":"";s+=x.phy_lr?"L":"";s+="\"}";
  }
  s+="]}";
}
// Minimal network-only diagnostic, intentionally independent of SD/engine
// data. Usable by direct IP on router Wi-Fi or on the own Wi-Fi in recovery.
// The router's signal, read when a report or the app asks for it while the
// station is connected; the last value stays for a report after a loss.
void routerSignalSample(){
  if((WiFi.getMode()&WIFI_MODE_STA)&&WiFi.status()==WL_CONNECTED)lastRouterRssi=WiFi.RSSI();
}
String makeNetworkHealthJson(){
  routerSignalSample();
  String s;s.reserve(4200);
  s+="{\"version\":";jsonString(s,EL_VERSION);s+=",\"build\":\"" __DATE__ " " __TIME__ "\"";
  s+=",\"uptimeMs\":";s+=(unsigned long)millis();
  s+=",\"mode\":";jsonString(s,routerConfigured()?"router":"standalone");
  s+=",\"recovery\":";s+=routerHold?"true":"false";   // router mode, own Wi-Fi on meanwhile (not a mode)
  s+=",\"wifiMode\":";s+=(unsigned)WiFi.getMode();
  s+=",\"ssid\":";jsonString(s,cfg.ssid);
  s+=",\"status\":";s+=(int)WiFi.status();
  s+=",\"phase\":";
  jsonString(s,routerHold?"recovery":!routerConfigured()?"standalone":
    WiFi.status()==WL_CONNECTED?"online":staAssociated?"dhcp":"association");
  s+=",\"associated\":";s+=staAssociated?"true":"false";
  s+=",\"hasRouterIp\":";s+=WiFi.status()==WL_CONNECTED?"true":"false";
  s+=",\"ip\":";jsonString(s,WiFi.status()==WL_CONNECTED?
    WiFi.localIP().toString():String(""));
  s+=",\"gateway\":";jsonString(s,WiFi.status()==WL_CONNECTED?
    WiFi.gatewayIP().toString():String(""));
  s+=",\"rssi\":";if(WiFi.status()==WL_CONNECTED)s+=WiFi.RSSI();else s+="null";
  s+=",\"associationEvents\":";s+=(unsigned long)staAssociatedEvents;
  s+=",\"dhcpIpEvents\":";s+=(unsigned long)staIpEvents;
  s+=",\"lostIpEvents\":";s+=(unsigned long)staIpLostEvents;
  s+=",\"associationMs\":";s+=(unsigned long)staJoinMs;
  s+=",\"dhcpMs\":";s+=(unsigned long)staDhcpMs;
  s+=",\"driverReason\":";s+=(unsigned)staLastReason;
  s+=",\"unexpectedReason\":";s+=(unsigned)staLastUnexpectedReason;
  s+=",\"unexpectedDrops\":";s+=(unsigned long)staUnexpectedDisconnects;
  s+=",\"associationFailures\":";s+=(unsigned long)staAssociationFailures;
  s+=",\"txReapplied\":";s+=txReapplied;
  s+=",\"txTargetQdbm\":";s+=(int)elTxPower[txLevel<5?txLevel:TX_LEVEL_FIXED];
  {int8_t q=0;s+=",\"txDriverQdbm\":";if(esp_wifi_get_max_tx_power(&q)==ESP_OK)s+=(int)q;else s+="null";}
  appendApLinkJson(s);
  s+=",\"loopParts\":[";for(uint8_t i=0;i<LOOP_PARTS;i++){if(i)s+=',';s+=loopPartMaxMs[i];}s+=']';
  s+=",\"engineMaxWaitMs\":";s+=engineMaxWaitMs;s+=",\"engineMaxWaitBy\":";jsonString(s,String(engineMaxWaitBy));s+=",\"engineSlowWaits\":";s+=engineSlowWaits;s+=",\"engineMaxRoundMs\":";s+=engineMaxRoundMs;
  s+=",\"apStations\":";s+=(unsigned)WiFi.softAPgetStationNum();
  s+=",\"apJoins\":";s+=(unsigned long)apJoins;s+=",\"apLeaves\":";s+=(unsigned long)apLeaves;s+=",\"apDhcpLeases\":";s+=(unsigned long)apDhcpLeases;
  s+=",\"apLeaveReason\":";s+=(unsigned)apReason;s+=",\"apRestarts\":";s+=apRestarts;
  s+=",\"speedTxBytes\":";s+=(unsigned long)speedTxLastBytes;
  s+=",\"speedTxMs\":";s+=(unsigned long)speedTxLastMs;
  s+=",\"speedTxWrites\":";s+=(unsigned long)speedTxLastWrites;
  s+=",\"networkEpoch\":";s+=(unsigned long)networkIpEpoch;
  appendHttpdJson(s);
  s+=",\"routerAttempts\":";s+=routerAttempts;
  s+=",\"routerFallbacks\":";s+=routerFallbacks;
  s+=",\"routerNextTryS\":";
  if(routerHold&&routerConfigured())s+=(long)((int32_t)(routerRetryAt-millis())/1000);else s+="null";
  s+=",\"disconnectEvents\":";s+=(unsigned long)staDisconnects;
  s+=",\"lastGotIpUptimeMs\":";s+=(unsigned long)staIpAt;
  s+=",\"lastRouterIp\":";jsonString(s,lastRouterIp);
  s+=",\"lastRouterRssi\":";if(lastRouterIp.length())s+=lastRouterRssi;else s+="null";
  s+=",\"lastRouterChannel\":";s+=(unsigned)lastRouterChannel;
  s+=",\"lastSecurity\":";jsonString(s,stationSecurityName(staNegotiatedAuth));
  appendSpeedJson(s);
  appendRouterDiagJson(s);
  {wifi_ps_type_t ps=WIFI_PS_NONE;esp_wifi_get_ps(&ps);s+=",\"powerSave\":";s+=(int)ps;}
  s+=",\"events\":[";
  RouterTrace trace[ROUTER_TRACE_CAP];const uint8_t n=getWifiTrace(trace,nullptr);
  for(uint8_t i=0;i<n;i++){
    if(i)s+=',';
    s+="{\"uptimeMs\":";s+=trace[i].ms;
    s+=",\"event\":";jsonString(s,traceText(trace[i].type));
    s+=",\"reason\":";s+=(unsigned)trace[i].reason;
    s+=",\"count\":";s+=(unsigned)trace[i].count;
    s+=",\"lastUptimeMs\":";s+=trace[i].lastMs;
    s+='}';
  }
  s+="]}";
  return s;
}
void handleNetworkHealth(){sendJSON(200,makeNetworkHealthJson());}
void handleNetworkReport(){
  web.sendHeader("Cache-Control","no-store");
  web.send(200,"text/plain; charset=utf-8",buildNetworkReport());
}
String buildNetworkReport(){
  // Text is easy to select/copy on iPhone and can be sent to support.
  // Never include the saved Wi-Fi password or engine/boat telemetry.
  routerSignalSample();
  String s;s.reserve(8500);
  s+="ENGINELINK - WI-FI REPORT\n";
  s+="========================\n\n";
  s+="Firmware: ";s+=EL_VERSION;s+=" (built " __DATE__ " " __TIME__ ")";   // tells two bench builds of one version apart
  s+="\n";
  s+="Uptime: ";s+=millis()/1000UL;s+=" seconds\n";
  s+="Mode: ";s+=routerHold?"Router (router not reached; EngineLink Wi-Fi on meanwhile, tried again every 10 minutes)":routerConfigured()?"Router (station)":"Standalone (EngineLink Wi-Fi)";s+="\n";
  s+="Wi-Fi radio mode: ";s+=(unsigned)WiFi.getMode();s+=" (1=station, 2=access point)\n";
  s+="Router: ";s+=cfg.ssid.isEmpty()?"Not configured":cfg.ssid;s+="\n";
  s+="Currently connected: ";s+=WiFi.status()==WL_CONNECTED?"Yes":"No";s+="\n";
  s+="Router IP now: ";s+=WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():"None";s+="\n";
  s+="Last successful router IP: ";s+=lastRouterIp.isEmpty()?"None this boot":lastRouterIp;s+="\n";
  s+="Last router signal: ";if(!staAssociatedEvents)s+="Not measured";else{s+=lastRouterRssi;s+=" dBm";}s+="\n";
  s+="Last router channel: ";s+=lastRouterChannel;s+="\n";
  s+="Last negotiated Wi-Fi security: ";s+=stationSecurityName(staNegotiatedAuth);s+="\n";
  s+="Station: ESP32 driver defaults with power save off (no country, bandwidth, scan or TX-power setting)\n";
  s+="Security: ESP32 core WPA2/WPA3 negotiation (no custom SAE handling)\n";
  s+="Transmit power: ";if(WiFi.getMode()&WIFI_MODE_STA)s+="ESP32 driver default";else{s+=elTxName[txLevel<5?txLevel:TX_LEVEL_FIXED];s+=" (fixed)";}
  if(!(WiFi.getMode()&WIFI_MODE_STA)){s+=" · target ";s+=(int)elTxPower[txLevel<5?txLevel:TX_LEVEL_FIXED];s+=" quarter-dBm";}
  {int8_t q=0;if(esp_wifi_get_max_tx_power(&q)==ESP_OK){s+=" · driver reports ";s+=q/4.0f;s+=" dBm (";s+=(int)q;s+=" quarter-dBm)";}}
  s+=" · set again after the driver changed it: ";s+=txReapplied;s+=" time(s)\n";
  {wifi_ps_type_t ps=WIFI_PS_NONE;esp_wifi_get_ps(&ps);
   s+="Power save: ";s+=(WiFi.getMode()&WIFI_MODE_STA)?(ps==WIFI_PS_NONE?"off":ps==WIFI_PS_MIN_MODEM?"ON (minimum modem sleep)":"ON (maximum modem sleep)"):"not applicable now (access point mode); the router session below has its own line";s+="\n";}
  if(WiFi.getMode()&WIFI_MODE_STA){
    wifi_phy_mode_t pm;
    if(esp_wifi_sta_get_negotiated_phymode(&pm)==ESP_OK){s+="Negotiated Wi-Fi standard: ";s+=pm==WIFI_PHY_MODE_11B?"802.11b (slow)":pm==WIFI_PHY_MODE_11G?"802.11g":pm==WIFI_PHY_MODE_HT20?"802.11n 20 MHz":pm==WIFI_PHY_MODE_HT40?"802.11n 40 MHz":pm==WIFI_PHY_MODE_HE20?"802.11ax":"other";s+="\n";}
  }
  s+="CONNECTIVITY (COUNTERS SINCE BOOT)\n";
  s+="Successful Wi-Fi associations: ";s+=(unsigned long)staAssociatedEvents;s+="\n";
  s+="Successful router IP assignments: ";s+=(unsigned long)staIpEvents;s+="\n";
  s+="Router IP losses: ";s+=(unsigned long)staIpLostEvents;s+="\n";
  s+="Wi-Fi disconnect events: ";s+=(unsigned long)staDisconnects;s+="\n";
  s+="Dropped active router connections: ";s+=(unsigned long)staUnexpectedDisconnects;s+="\n";
  s+="Pre-connection failures: ";s+=(unsigned long)staAssociationFailures;s+="\n";
  s+="Reconnect: the ESP32 core's own (a new attempt right after every failure)\n";
  s+="App-initiated router attempts: ";s+=routerAttempts;s+="\n";
  s+="Router recovery fallbacks: ";s+=routerFallbacks;s+="\n";
  s+="Last association time: ";s+=staJoinMs;s+=" ms\n";
  s+="Last DHCP time: ";s+=staDhcpMs;s+=" ms\n";
  s+="Last successful IP at uptime: ";s+=staIpAt/1000UL;s+=" seconds\n";
  s+="Last Wi-Fi event reason: ";s+=(unsigned)staLastReason;s+=" - ";s+=reasonText(staLastReason);s+="\n";
  s+="Last unexpected reason: ";s+=(unsigned)staLastUnexpectedReason;s+=" - ";s+=reasonText(staLastUnexpectedReason);s+="\n";
  s+="Error 202 means authentication failure; it does not prove the password is wrong.\n\n";
  s+="DOWNLOAD TESTS FROM THE PHONE (newest last)\n";
  if(!speedLogCount)s+="None yet: tap \"Test download speed\" in Diagnostics.\n";
  {static const char *nm[5]={"8.5","11","13","15","19.5"};
   for(uint8_t i=0;i<speedLogCount;i++){const SpeedLogRec &x=speedLog[i];
    s+="  ";s+=(millis()-x.at)/1000UL;s+=" s ago · ";s+=x.level<5?nm[x.level]:"?";s+=" dBm · ";
    if(x.kbs10){s+=x.kbs10/10.0f;s+=" kB/s";}else s+="failed";
    s+=" · request round trip ";s+=x.rttMs;s+=" ms";s+="\n";}
  }
  s+="\n";
  appendRouterDiagText(s);
  if((WiFi.getMode()&WIFI_MODE_STA)&&WiFi.status()==WL_CONNECTED){
    s+="ROUTER (AS SEEN BY ENGINELINK)\n";
    wifi_ap_record_t ap;
    if(esp_wifi_sta_get_ap_info(&ap)==ESP_OK){
      char oui[12];snprintf(oui,sizeof(oui),"%02X:%02X:%02X",ap.bssid[0],ap.bssid[1],ap.bssid[2]);
      s+="Maker prefix (first half of the router's MAC address): ";s+=oui;s+="\n";
      s+="Router supports: ";s+=ap.phy_11b?"b ":"";s+=ap.phy_11g?"g ":"";s+=ap.phy_11n?"n ":"";s+=ap.phy_11ax?"ax ":"";s+=ap.phy_lr?"LR ":"";s+="\n";
      s+="Router channel: ";s+=ap.primary;s+=ap.second==WIFI_SECOND_CHAN_ABOVE?" + upper (router uses 40 MHz)":ap.second==WIFI_SECOND_CHAN_BELOW?" + lower (router uses 40 MHz)":" (20 MHz)";s+="\n";
      s+="Router country: ";s+=String(ap.country.cc).substring(0,2);s+=" (channels ";s+=ap.country.schan;s+="-";s+=ap.country.schan+ap.country.nchan-1;s+=")\n";
      static const char *ciph[]={"none","WEP40","WEP104","TKIP (slow, limits Wi-Fi to 54 Mbit/s)","CCMP (AES)","TKIP+CCMP","AES-CMAC","SMS4","GCMP","GCMP-256","AES-GMAC-128","AES-GMAC-256"};
      s+="Encryption: pairwise ";s+=ap.pairwise_cipher<12?ciph[ap.pairwise_cipher]:"other";s+=", group ";s+=ap.group_cipher<12?ciph[ap.group_cipher]:"other";s+="\n";
      s+="Signal now: ";s+=ap.rssi;s+=" dBm\n";
    }
    uint8_t proto=0;if(esp_wifi_get_protocol(WIFI_IF_STA,&proto)==ESP_OK){s+="EngineLink allows: ";s+=proto&WIFI_PROTOCOL_11B?"b ":"";s+=proto&WIFI_PROTOCOL_11G?"g ":"";s+=proto&WIFI_PROTOCOL_11N?"n ":"";s+=proto&WIFI_PROTOCOL_LR?"LR ":"";s+="\n";}
    wifi_bandwidth_t bw;if(esp_wifi_get_bandwidth(WIFI_IF_STA,&bw)==ESP_OK){s+="EngineLink bandwidth: ";s+=bw==WIFI_BW_HT40?"40 MHz":"20 MHz";s+="\n";}
    {uint16_t it=0;if(esp_wifi_get_inactive_time(WIFI_IF_STA,&it)==ESP_OK){s+="Beacon timeout (driver): ";s+=it;s+=" s\n";}}
    s+="\n";
  }
  s+="ENGINELINK WI-FI (ACCESS POINT, THIS START)\n";
  s+="Phones connected now: ";s+=(unsigned)WiFi.softAPgetStationNum();
  s+=" · joined ";s+=(unsigned long)apJoins;s+=" · DHCP leases assigned ";s+=(unsigned long)apDhcpLeases;s+=" · left ";s+=(unsigned long)apLeaves;
  if(apLeaves){s+=" (last code ";s+=(unsigned)apReason;s+=")";}
  s+=" · started ";s+=(unsigned long)apStarts;s+=" · restarted by the watchdog ";s+=apRestarts;
  s+=" · channel ";s+=(unsigned)apChannelNow;s+="\n";
  {const ApLink a=readApLink();
   if(a.up){s+="Access point as the driver runs it: channel ";s+=a.ch;s+=", ";s+=a.bw;s+=" MHz, PMF ";s+=a.pmfRequired?"required":a.pmfCapable?"capable":"off";
     s+=", stations kept ";s+=a.inactiveS;s+=" s without traffic, country ";s+=a.cc[0]?a.cc:"?";s+="\n";
     s+="Joined phones (maker prefix, signal at EngineLink, Wi-Fi standards): ";s+=apStationsText("; ");s+="\n";}
   else s+="Access point: off (router mode)\n";
   char cc[4]={};if(esp_wifi_get_country_code(cc)==ESP_OK){s+="Wi-Fi country (driver): ";s+=cc[0]?cc:"?";s+="\n";}}
  s+="\n";
  s+="SYSTEM\n";
  s+="Chip: ";s+=ESP.getChipModel();s+=" rev ";s+=ESP.getChipRevision();s+=" · ";s+=ESP.getCpuFreqMHz();s+=" MHz · Arduino core ";s+=ESP_ARDUINO_VERSION_MAJOR;s+=".";s+=ESP_ARDUINO_VERSION_MINOR;s+=".";s+=ESP_ARDUINO_VERSION_PATCH;s+=" · IDF ";s+=esp_get_idf_version();s+="\n";
  s+="Memory: free ";s+=ESP.getFreeHeap()/1024;s+=" KB, lowest ";s+=ESP.getMinFreeHeap()/1024;s+=" KB, largest block ";s+=ESP.getMaxAllocHeap()/1024;s+=" KB · PSRAM free ";s+=ESP.getFreePsram()/1024;s+=" KB\n";
  s+="Main loop: longest pass ";s+=loopMaxMs;s+=" ms · engine reading in own task: ";s+=engineTaskOn?"yes":"no";
  s+=" · engine task longest wait ";s+=engineMaxWaitMs;s+=" ms";
  if(engineMaxWaitBy[0]){s+=" (lock held by ";s+=engineMaxWaitBy;s+=" when it began)";}
  s+=" · waits over 100 ms: ";s+=engineSlowWaits;s+=" · longest round of its own work ";s+=engineMaxRoundMs;s+=" ms\n";
  {static const char *part[LOOP_PARTS]={"(web: own task)","sd","diag","sd scan","history","network","led","serial","other"};
   s+="Longest part of the main loop (ms):";
   for(uint8_t i=0;i<LOOP_PARTS;i++){s+=i?", ":" ";s+=part[i];s+=' ';s+=loopPartMaxMs[i];}s+="\n";}
  s+="Reset reason: ";s+=(int)esp_reset_reason();s+=" (1 power on, 3 software/update, 4 crash, 5/6/7 watchdog, 9 brownout, 11 reset over USB)\n";
  s+="Recent restarts (newest first): ";
  {bool any=false;for(uint8_t i=0;i<RESTART_LOG_N;i++){const RestartRec &x=restartLog[i];if(!x.ver[0])continue;if(any)s+=" | ";any=true;s+="reason ";s+=x.reason;s+=" after ";s+=x.upBefore;s+=" s on ";s+=x.ver;}if(!any)s+="none recorded";}
  s+="\n";
  s+=elCrashSummary();
  s+="\n";
  s+="HTTP / WEB (esp_http_server, port 80)\n";
  {const HttpdStats &h=httpdStats;
   s+="Requests: ";s+=h.requests;s+=" · long transfers: ";s+=h.async;s+=" · answered busy (503): ";s+=h.busy503;
   s+=" · refused by the security checks: ";s+=h.rejected;s+=" · send failures (timeout or phone gone): ";s+=h.sendFails;s+="\n";
   if(h.busy503){
     s+="Busy (503) by cause: engine lock not free in 2 s ";s+=h.busyLock;s+=" · another download running ";s+=h.busyXfer;
     s+=" · download waited 10 s for the engine lock ";s+=h.busyXferLock;s+=" · last: ";s+=h.busyPath;s+=" at uptime ";s+=h.busyAtMs/1000;s+=" s\n";
   }
   s+="Connections opened: ";s+=h.opens;s+=" · closed: ";s+=h.closes;s+=" · open now: ";s+=h.openNow;s+=" · most at once: ";s+=h.openMax;
   s+=" · successful answers to other devices: ";s+=h.external2xx;s+="\n";
   s+="Of these through the router since the start (the rest: own Wi-Fi and the self-test): connections ";s+=h.rtrOpens;s+=" · requests ";s+=h.rtrReq;
   s+=" · answered ";s+=h.rtrOk;s+=" · failed while sending ";s+=h.rtrFail;
   if(h.rtrReq){s+=" · last at uptime ";s+=h.rtrLastMs/1000UL;s+=" s";}s+="\n";
   s+="Slowest request: ";s+=h.slowMs;s+=" ms (";s+=h.slowPath;s+=") · last transfer waited ";s+=h.xferWaitMs;s+=" ms for the engine lock\n";
   s+="Stack left (bytes): web server ";s+=webStackLeft(false);s+=", transfers ";s+=webStackLeft(true);s+="\n";
   s+="Firmware self-test: ";s+=selfTestText();s+="\n";}
  s+="Last 32 KiB server write: ";s+=speedTxLastBytes;s+=" bytes in ";s+=speedTxLastMs;s+=" ms (";
  s+=speedTxLastWrites;s+=" write calls)\n";
  s+="TCP send window: ESP32 core default (not modified)\n";
  s+="Note: server write time is NOT the same as measured radio throughput.\n\n";
  s+="LATEST WI-FI EVENTS (oldest to newest; uptime seconds)\n";
  RouterTrace trace[ROUTER_TRACE_CAP];const uint8_t n=getWifiTrace(trace,nullptr);
  if(!n)s+="No events recorded this boot.\n";
  for(uint8_t i=0;i<n;i++){
    s+="  +";s+=trace[i].ms/1000UL;s+="s  ";s+=traceText(trace[i].type);
    if(trace[i].reason){s+="  code ";s+=(unsigned)trace[i].reason;s+=" (";s+=reasonText(trace[i].reason);s+=")";}
    if(trace[i].count>1){s+="  x";s+=(unsigned)trace[i].count;s+=" until +";s+=trace[i].lastMs/1000UL;s+="s";}
    s+="\n";
  }
  s+="\nMACHINE-READABLE JSON (copy/send the entire report)\n";
  s+=makeNetworkHealthJson();s+="\n";
  return s;
}
// The phone's own download tests (the app posts them) for the report.
void appendSpeedJson(String &s){
  s+=",\"txLevel\":";s+=txLevel;
  s+=",\"phoneTests\":[";
  for(uint8_t i=0;i<speedLogCount;i++){const SpeedLogRec &x=speedLog[i];if(i)s+=',';s+="{\"agoS\":";s+=(unsigned long)((millis()-x.at)/1000UL);s+=",\"level\":";s+=x.level;s+=",\"kbs\":";s+=x.kbs10/10.0f;s+=",\"rttMs\":";s+=x.rttMs;s+='}';}
  s+=']';
}
void appendHttpdJson(String &s){
  const HttpdStats &h=httpdStats;
  s+=",\"httpdRequests\":";s+=h.requests;s+=",\"httpdAsync\":";s+=h.async;s+=",\"httpdBusy503\":";s+=h.busy503;
  s+=",\"httpdBusyLock\":";s+=h.busyLock;s+=",\"httpdBusyXfer\":";s+=h.busyXfer;s+=",\"httpdBusyXferLock\":";s+=h.busyXferLock;
  s+=",\"httpdBusyPath\":";jsonString(s,String(h.busyPath));s+=",\"httpdBusyAtMs\":";s+=h.busyAtMs;
  s+=",\"httpdRejected\":";s+=h.rejected;s+=",\"httpdSendFails\":";s+=h.sendFails;
  s+=",\"httpdOpens\":";s+=h.opens;s+=",\"httpdCloses\":";s+=h.closes;s+=",\"httpdOpenNow\":";s+=h.openNow;s+=",\"httpdOpenMax\":";s+=h.openMax;
  s+=",\"httpdExternalOk\":";s+=h.external2xx;s+=",\"httpdSlowMs\":";s+=h.slowMs;s+=",\"httpdSlowPath\":";jsonString(s,String(h.slowPath));
  s+=",\"httpdRouterOpens\":";s+=h.rtrOpens;s+=",\"httpdRouterRequests\":";s+=h.rtrReq;s+=",\"httpdRouterOk\":";s+=h.rtrOk;s+=",\"httpdRouterSendFails\":";s+=h.rtrFail;s+=",\"httpdRouterLastMs\":";s+=h.rtrLastMs;
  s+=",\"httpdStackFree\":";s+=webStackLeft(false);s+=",\"xferStackFree\":";s+=webStackLeft(true);
}
