// Network report and health JSON, the port 81 (live/report) and 82
// (speed test) listeners, the web server self-check and transmit power task.

// Minimal network-only diagnostic, intentionally independent of SD/engine
// data. Usable by direct IP on router Wi-Fi or after rescue AP returns.
String makeNetworkHealthJson(){
  String s;s.reserve(3300);
  s+="{\"version\":";jsonString(s,EL_VERSION);
  s+=",\"uptimeMs\":";s+=(unsigned long)millis();
  s+=",\"mode\":";
  jsonString(s,routerHold?"rescue":routerConfigured()?"router":"standalone");
  s+=",\"wifiMode\":";s+=(unsigned)WiFi.getMode();
  s+=",\"ssid\":";jsonString(s,cfg.ssid);
  s+=",\"status\":";s+=(int)WiFi.status();
  s+=",\"phase\":";
  jsonString(s,routerHold?"rescue":!routerConfigured()?"standalone":
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
  s+=",\"saQueryFastRetries\":";s+=saQueryFastRetries;
  s+=",\"speedTxBytes\":";s+=(unsigned long)speedTxLastBytes;
  s+=",\"speedTxMs\":";s+=(unsigned long)speedTxLastMs;
  s+=",\"speedTxWrites\":";s+=(unsigned long)speedTxLastWrites;
  s+=",\"failedReason\":";s+=(unsigned)routerFailureReason;
  s+=",\"failedPhase\":";s+=(unsigned)routerFailurePhase;
  s+=",\"failedAfterMs\":";s+=(unsigned long)routerFailureAfterMs;
  s+=",\"driverNudges\":";s+=routerForcedReconnects;
  s+=",\"httpRebinds\":";s+=web.listenerRebinds;
  s+=",\"httpSelfFails\":";s+=httpSelfFails;s+=",\"httpHeals\":";s+=httpHeals;s+=",\"httpSelfRestarts\":";s+=httpSelfRestarts;
  s+=",\"httpBoundEpoch\":";s+=httpBoundEpoch;
  s+=",\"networkEpoch\":";s+=(unsigned long)networkIpEpoch;
  s+=",\"httpRequests\":";s+=web.served;
  s+=",\"routerAttempts\":";s+=routerAttempts;
  s+=",\"routerFallbacks\":";s+=routerFallbacks;
  s+=",\"routerAutoReturns\":";s+=routerAutoReturns;s+=",\"routerNextTryS\":";
  if(routerHold&&routerConfigured())s+=(long)((int32_t)(routerProbeAt-millis())/1000);else s+="null";
  s+=",\"disconnectEvents\":";s+=(unsigned long)staDisconnects;
  s+=",\"lastGotIpUptimeMs\":";s+=(unsigned long)staIpAt;
  s+=",\"lastRouterIp\":";jsonString(s,lastRouterIp);
  s+=",\"lastRouterRssi\":";if(lastRouterIp.length())s+=lastRouterRssi;else s+="null";
  s+=",\"lastRouterChannel\":";s+=(unsigned)lastRouterChannel;
  s+=",\"lastSecurity\":";jsonString(s,stationSecurityName(staNegotiatedAuth));
  appendLinkJson(s);
  s+=",\"secMode\":";s+=staSecMode?"\"wpa3\"":"\"wpa2\"";s+=",\"saeControl\":";s+=elSaeControlActive()?"true":"false";s+=",\"secSwitches\":";s+=staSecSwitches;{wifi_ps_type_t ps=WIFI_PS_NONE;esp_wifi_get_ps(&ps);s+=",\"powerSave\":";s+=(int)ps;}
  s+=",\"saeConfigured\":";s+=staSecurityConfigured?"true":"false";
  s+=",\"saeSetupError\":";s+=(int)staSecurityConfigError;
  s+=",\"lastFailureDescription\":";jsonString(s,reasonText(routerFailureReason));
  s+=",\"events\":[";
  RouterTrace trace[ROUTER_TRACE_CAP];const uint8_t n=getWifiTrace(trace);
  for(uint8_t i=0;i<n;i++){
    if(i)s+=',';
    s+="{\"uptimeMs\":";s+=trace[i].ms;
    s+=",\"event\":";jsonString(s,traceText(trace[i].type));
    s+=",\"reason\":";s+=(unsigned)trace[i].reason;
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
  String s;s.reserve(6500);
  s+="ENGINELINK - WI-FI REPORT\n";
  s+="========================\n\n";
  s+="Firmware: ";s+=EL_VERSION;s+="\n";
  s+="Uptime: ";s+=millis()/1000UL;s+=" seconds\n";
  s+="Mode: ";s+=routerHold?"Recovery (EngineLink Wi-Fi)":routerConfigured()?"Router (station)":"Standalone (EngineLink Wi-Fi)";s+="\n";
  s+="Wi-Fi radio mode: ";s+=(unsigned)WiFi.getMode();s+=" (1=station, 2=access point)\n";
  s+="Router: ";s+=cfg.ssid.isEmpty()?"Not configured":cfg.ssid;s+="\n";
  s+="Currently connected: ";s+=WiFi.status()==WL_CONNECTED?"Yes":"No";s+="\n";
  s+="Router IP now: ";s+=WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():"None";s+="\n";
  s+="Last successful router IP: ";s+=lastRouterIp.isEmpty()?"None this boot":lastRouterIp;s+="\n";
  s+="Last router signal: ";if(lastRouterIp.isEmpty())s+="Not measured";else{s+=lastRouterRssi;s+=" dBm";}s+="\n";
  s+="Last router channel: ";s+=lastRouterChannel;s+="\n";
  s+="Last negotiated Wi-Fi security: ";s+=stationSecurityName(staNegotiatedAuth);s+="\n";
  s+="Security mode: ";s+=staSecMode?"WPA3 allowed (router needs WPA3)":"WPA2, like most IoT devices (works on WPA2 and WPA2/WPA3 routers)";s+="\n";
  s+="WPA3 switch in firmware: ";s+=elSaeControlActive()?"active":"NOT active (built without -Wl,--wrap=esp_wifi_init)";s+="\n";
  s+="Security switches: ";s+=staSecSwitches;s+="\n";
  s+="Transmit power: ";s+=elTxName[txLevel<5?txLevel:3];s+=" (chosen by EngineLink)";
  {int8_t q=0;if(esp_wifi_get_max_tx_power(&q)==ESP_OK){s+=" · driver reports ";s+=q/4.0f;s+=" dBm";}}
  s+=" · lowered automatically ";s+=txAutoSteps;s+=" time(s) this start\n";
  {wifi_ps_type_t ps=WIFI_PS_NONE;esp_wifi_get_ps(&ps);
   s+="Power save: ";s+=ps==WIFI_PS_NONE?"off":ps==WIFI_PS_MIN_MODEM?"ON (minimum modem sleep)":"ON (maximum modem sleep)";s+="\n";}
  if(WiFi.getMode()&WIFI_MODE_STA){
    wifi_phy_mode_t pm;
    if(esp_wifi_sta_get_negotiated_phymode(&pm)==ESP_OK){s+="Negotiated Wi-Fi standard: ";s+=pm==WIFI_PHY_MODE_11B?"802.11b (slow)":pm==WIFI_PHY_MODE_11G?"802.11g":pm==WIFI_PHY_MODE_HT20?"802.11n 20 MHz":pm==WIFI_PHY_MODE_HT40?"802.11n 40 MHz":pm==WIFI_PHY_MODE_HE20?"802.11ax":"other";s+="\n";}
  }
  s+="Transmit power test (ping to the router, 20 x 1400 bytes, min/avg/max): ";
  if(linkTuneRunning)s+="running";
  else if(!linkTuneCount)s+="not run yet";
  else{
    static const char *nm[5]={"8.5","11","13","15","19.5"};
    for(uint8_t i=0;i<linkTuneCount;i++){const LinkTuneStep &x=linkTuneSteps[i];s+="\n  ";s+=nm[x.level];s+=" dBm: ";s+=x.p.got;s+="/";s+=x.p.sent;s+=" back, ";s+=x.p.minMs;s+="/";s+=x.p.avgMs;s+="/";s+=x.p.maxMs;s+=" ms";}
    if(linkTuneChosen<5){s+="\n  best by ping: ";s+=nm[linkTuneChosen];s+=" dBm";}
    s+=" (";s+=(millis()-linkTuneAt)/1000UL;s+=" s ago)";
  }
  s+="\n";
  if(linkSmall.sent){s+="Small pings to the router (20 x 64 bytes): ";s+=linkSmall.got;s+="/";s+=linkSmall.sent;s+=" back, ";s+=linkSmall.minMs;s+="/";s+=linkSmall.avgMs;s+="/";s+=linkSmall.maxMs;s+=" ms\n";}
  if(linkNet.sent){s+="Pings to the internet (1.1.1.1, 10 x 64 bytes): ";s+=linkNet.got;s+="/";s+=linkNet.sent;s+=" back";if(linkNet.got){s+=", ";s+=linkNet.minMs;s+="/";s+=linkNet.avgMs;s+="/";s+=linkNet.maxMs;s+=" ms";}s+="\n";}
  s+="Note: if pings to the internet are faster than pings to the router, the router is slow to answer pings itself, not the Wi-Fi.\n";
  s+="Security configuration result: ";s+=esp_err_to_name(staSecurityConfigError);s+="\n\n";
  s+="CONNECTIVITY (COUNTERS SINCE BOOT)\n";
  s+="Successful Wi-Fi associations: ";s+=(unsigned long)staAssociatedEvents;s+="\n";
  s+="Successful router IP assignments: ";s+=(unsigned long)staIpEvents;s+="\n";
  s+="Router IP losses: ";s+=(unsigned long)staIpLostEvents;s+="\n";
  s+="Wi-Fi disconnect events: ";s+=(unsigned long)staDisconnects;s+="\n";
  s+="Dropped active router connections: ";s+=(unsigned long)staUnexpectedDisconnects;s+="\n";
  s+="Pre-connection failures: ";s+=(unsigned long)staAssociationFailures;s+="\n";
  s+="SA Query fast recovery attempts: ";s+=saQueryFastRetries;s+="\n";
  s+="App-initiated router attempts: ";s+=routerAttempts;s+="\n";
  s+="Router recovery fallbacks: ";s+=routerFallbacks;s+="\n";
  s+="Last association time: ";s+=staJoinMs;s+=" ms\n";
  s+="Last DHCP time: ";s+=staDhcpMs;s+=" ms\n";
  s+="Last successful IP at uptime: ";s+=staIpAt/1000UL;s+=" seconds\n";
  s+="Last Wi-Fi event reason: ";s+=(unsigned)staLastReason;s+=" - ";s+=reasonText(staLastReason);s+="\n";
  s+="Last unexpected reason: ";s+=(unsigned)staLastUnexpectedReason;s+=" - ";s+=reasonText(staLastUnexpectedReason);s+="\n";
  s+="Last saved failure: ";s+=(unsigned)routerFailureReason;s+=" - ";s+=reasonText(routerFailureReason);s+="\n";
  s+="Failure stage at timeout: ";s+=routerFailurePhase==2?"Associated but without IP":routerFailurePhase==1?"Not associated":"Not recorded";s+="\n";
  s+="Time before rescue: ";s+=routerFailureAfterMs;s+=" ms\n";
  s+="Note: final failure stage does not erase any earlier successful IP events.\n";
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
    s+="Wi-Fi networks seen in the test: ";
    if(linkScanDone){s+=linkScanTotal;s+=" · others on the router's channel: ";s+=linkScanSame;if(linkScanSame){s+=" (strongest ";s+=(int)linkScanSameBest;s+=" dBm)";}s+=" · on overlapping channels: ";s+=linkScanOverlap;}
    else s+="not scanned yet";
    s+="\n";
    if(linkScanDone){
      if(linkScanCh!=lastRouterChannel){s+="Note: the scan and ping tests were made while the router was on channel ";s+=linkScanCh;s+="; they repeat a minute after reconnecting.\n";}
      s+="Networks on or next to the router's channel: ";s+=linkScanList.length()?linkScanList:String("none");s+="\n";
      s+="Quietest of channels 1, 6 and 11 around EngineLink: ";s+=linkScanQuiet;s+=linkScanQuiet==lastRouterChannel?" (the router already uses it)":" (setting the router to it may help)";s+="\n";
    }
    s+="\n";
  }
  s+="SYSTEM\n";
  s+="Chip: ";s+=ESP.getChipModel();s+=" rev ";s+=ESP.getChipRevision();s+=" · ";s+=ESP.getCpuFreqMHz();s+=" MHz · Arduino core ";s+=ESP_ARDUINO_VERSION_MAJOR;s+=".";s+=ESP_ARDUINO_VERSION_MINOR;s+=".";s+=ESP_ARDUINO_VERSION_PATCH;s+=" · IDF ";s+=esp_get_idf_version();s+="\n";
  s+="Memory: free ";s+=ESP.getFreeHeap()/1024;s+=" KB, lowest ";s+=ESP.getMinFreeHeap()/1024;s+=" KB, largest block ";s+=ESP.getMaxAllocHeap()/1024;s+=" KB · PSRAM free ";s+=ESP.getFreePsram()/1024;s+=" KB\n";
  s+="Main loop: longest pass ";s+=loopMaxMs;s+=" ms · engine reading in own task: ";s+=engineTaskOn?"yes":"no";s+="\n";
  s+="Reset reason: ";s+=(int)esp_reset_reason();s+=" (1 power on, 3 software/update, 4 crash, 5/6/7 watchdog, 9 brownout, 11 reset over USB)\n";
  s+="Recent restarts (newest first): ";
  {bool any=false;for(uint8_t i=0;i<RESTART_LOG_N;i++){const RestartRec &x=restartLog[i];if(!x.ver[0])continue;if(any)s+=" | ";any=true;s+="reason ";s+=x.reason;s+=" after ";s+=x.upBefore;s+=" s on ";s+=x.ver;}if(!any)s+="none recorded";}
  s+="\n";
  s+=elCrashSummary();
  s+="\n";
  s+="HTTP / WEB\n";
  s+="HTTP rebinds: ";s+=web.listenerRebinds;s+="\n";
  s+="Web server self-check misses: ";s+=httpSelfFails;s+=" · restarted web server: ";s+=httpHeals;s+=" · board restarts for it: ";s+=httpSelfRestarts;s+="\n";
  s+="HTTP requests: ";s+=web.served;s+="\n";
  s+="Last 32 KiB server write: ";s+=speedTxLastBytes;s+=" bytes in ";s+=speedTxLastMs;s+=" ms (";
  s+=speedTxLastWrites;s+=" write calls)\n";
  s+="TCP send window: 5744 bytes by default, raised to 17 KB for page/file requests and 23 KB for the speed test (";s+=elBoostedSockets;s+=" connections raised, ";s+=elBoostMisses;s+=" not found)\n";
  s+="Note: server write time is NOT the same as measured radio throughput.\n\n";
  s+="LATEST WI-FI EVENTS (oldest to newest; uptime seconds)\n";
  RouterTrace trace[ROUTER_TRACE_CAP];const uint8_t n=getWifiTrace(trace);
  if(!n)s+="No events recorded this boot.\n";
  for(uint8_t i=0;i<n;i++){
    s+="  +";s+=trace[i].ms/1000UL;s+="s  ";s+=traceText(trace[i].type);
    if(trace[i].reason){s+="  code ";s+=(unsigned)trace[i].reason;s+=" (";s+=reasonText(trace[i].reason);s+=")";}
    s+="\n";
  }
  s+="\nMACHINE-READABLE JSON (copy/send the entire report)\n";
  s+=makeNetworkHealthJson();s+="\n";
  return s;
}
// Port 81/82 tasks each own their listener. They rebind it themselves after
// an interface-ready event, avoiding cross-core close/accept races.
void refreshWorkerListener(NetworkServer &server,uint32_t &boundEpoch){
  const uint32_t epoch=networkIpEpoch;
  if(epoch!=boundEpoch && epoch &&
     (uint32_t)(millis()-networkIpEventAt)>=600UL){
    server.end();
    server.begin();
    boundEpoch=epoch;
  }
}
void speedHttpTask(void *){
  uint32_t boundEpoch=0;
  // Full-sized TCP payloads avoid slow small-packet/Nagle/ACK interaction.
  // The test remains independent of filesystem and engine tasks.
  static const uint8_t sample[1460]={'E','n','g','i','n','e','L','i','n','k'};
  for(;;){
    refreshWorkerListener(speedServer,boundEpoch);
    NetworkClient c=speedServer.accept();
    if(!c){vTaskDelay(pdMS_TO_TICKS(15));continue;}
    c.setNoDelay(true);c.setTimeout(300);
    elBoostSocket(c.fd(),17232);     // 16 packets in flight instead of 4
    const String req=c.readStringUntil('\n');
    if(req.startsWith("GET /api/rtt")){  // round trip of one small request, for the report
      c.print("HTTP/1.1 204 No Content\r\nAccess-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n");
      c.stop();continue;
    }
    if(!req.startsWith("GET /api/speedtest ") && !req.startsWith("GET /api/speedtest?")){
      c.print("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
      c.stop();continue;
    }
    // Size from ?kb= (16-256 KB, default 32): bigger tests even out a jumpy link.
    size_t total=32768;
    {const int q=req.indexOf("kb=");if(q>0){const long kb=strtol(req.c_str()+q+3,nullptr,10);if(kb>=16&&kb<=256)total=(size_t)kb*1024;}}
    c.printf("HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nCache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: %u\r\nConnection: close\r\n\r\n",(unsigned)total);
    const uint32_t begun=millis();
    size_t sent=0;
    uint32_t writes=0;
    uint8_t blocks=0;
    while(sent<total && c.connected() && (uint32_t)(millis()-begun)<20000UL){
      const size_t chunk=min((size_t)sizeof(sample),(size_t)(total-sent));
      const size_t n=c.write(sample,chunk);
      ++writes;
      if(n==0)break;
      sent+=n;
      // Yield periodically, not after every tiny 256-byte packet. This task
      // is priority 1 and remains independent from the main HTTP server.
      if((++blocks&3)==0)vTaskDelay(1);
    }
    // Queue/write timing is distinct from browser-observed total download time.
    // Record even incomplete transfers; never block the engine task waiting.
    speedTxLastBytes=(uint32_t)sent;
    speedTxLastWrites=writes;
    speedTxLastAt=millis();
    speedTxLastMs=millis()-begun;
    // No unbounded flush: incomplete responses fail cleanly in the browser.
    c.stop();
  }
}
void liveHttpTask(void *){
  uint32_t boundEpoch=0;
  for(;;){
    refreshWorkerListener(liveServer,boundEpoch);
    NetworkClient c=liveServer.accept();
    if(!c){vTaskDelay(pdMS_TO_TICKS(10));continue;}
    c.setNoDelay(true);
    c.setTimeout(250);
    const String request=c.readStringUntil('\n');
    const bool pathOk=request.startsWith("GET /api/live ")||request.startsWith("GET /api/live?");
    // The Wi-Fi report here too: this small server works even if the main
    // web server on port 80 has stopped answering.
    if(request.startsWith("GET /report")){
      String rep;
      if(engineMx&&xSemaphoreTake(engineMx,pdMS_TO_TICKS(1000))==pdTRUE){rep=buildNetworkReport();xSemaphoreGive(engineMx);}
      if(rep.isEmpty())rep="EngineLink is busy; try again in a moment.\n";
      c.printf("HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nCache-Control: no-store\r\nContent-Length: %u\r\nConnection: close\r\n\r\n",(unsigned)rep.length());
      c.write((const uint8_t*)rep.c_str(),rep.length());
      c.flush();c.stop();continue;
    }
    if(!pathOk){
      c.print("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
      c.stop();continue;
    }
    String payload;
    if(engineMx&&xSemaphoreTake(engineMx,pdMS_TO_TICKS(200))==pdTRUE){
      payload=makeLiveJson();
      xSemaphoreGive(engineMx);
    }
    if(payload.isEmpty()){
      c.print("HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n");
    }else{
      c.printf("HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nCache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: %u\r\nConnection: close\r\n\r\n",(unsigned)payload.length());
      c.write((const uint8_t*)payload.c_str(),payload.length());
    }
    c.flush();
    c.stop();
  }
}
// Self-check of the main web server, from inside the board: every 20 s ask
// http://127.0.0.1/api/health (loopback, no radio involved). Three misses in
// a row: close every connection and start the web server again. Still
// failing two minutes after that: save the totals and restart. Skipped while
// a handler is legitimately busy (a download, a report). This catches a web
// server that accepts connections but never answers, whatever the cause.
// Tunes the transmit power once per start, 60 s after the router link is up,
// and when asked from the app. Never during a download or an update.
static void appendPingJson(String &s,const char *k,uint8_t sent,uint8_t got,uint16_t mn,uint16_t av,uint16_t mx){
  s+=",\"";s+=k;s+="\":{\"sent\":";s+=sent;s+=",\"got\":";s+=got;s+=",\"min\":";s+=mn;s+=",\"avg\":";s+=av;s+=",\"max\":";s+=mx;s+='}';
}
void appendLinkJson(String &s){
  s+=",\"linkTune\":{\"running\":";s+=linkTuneRunning?"true":"false";s+=",\"chosen\":";s+=linkTuneChosen<5?String(linkTuneChosen):String("null");
  s+=",\"agoS\":";if(linkTuneAt)s+=(unsigned long)((millis()-linkTuneAt)/1000UL);else s+="null";
  s+=",\"steps\":[";
  for(uint8_t i=0;i<linkTuneCount;i++){const LinkTuneStep &x=linkTuneSteps[i];if(i)s+=',';s+="{\"level\":";s+=x.level;s+=",\"got\":";s+=x.p.got;s+=",\"sent\":";s+=x.p.sent;s+=",\"ms\":";s+=x.p.avgMs;s+=",\"min\":";s+=x.p.minMs;s+=",\"max\":";s+=x.p.maxMs;s+='}';}
  s+=']';
  appendPingJson(s,"small",linkSmall.sent,linkSmall.got,linkSmall.minMs,linkSmall.avgMs,linkSmall.maxMs);
  appendPingJson(s,"internet",linkNet.sent,linkNet.got,linkNet.minMs,linkNet.avgMs,linkNet.maxMs);
  s+=",\"channel\":";s+=(unsigned)lastRouterChannel;s+=",\"quiet\":";s+=(unsigned)linkScanQuiet;s+=",\"scanCh\":";s+=(unsigned)linkScanCh;
  if(linkScanDone){s+=",\"scan\":{\"total\":";s+=linkScanTotal;s+=",\"sameChannel\":";s+=linkScanSame;s+=",\"overlapping\":";s+=linkScanOverlap;s+=",\"sameBest\":";s+=(int)linkScanSameBest;s+='}';}
  s+='}';
  s+=",\"txLevel\":";s+=txLevel;
  s+=",\"phoneTests\":[";
  for(uint8_t i=0;i<speedLogCount;i++){const SpeedLogRec &x=speedLog[i];if(i)s+=',';s+="{\"agoS\":";s+=(unsigned long)((millis()-x.at)/1000UL);s+=",\"level\":";s+=x.level;s+=",\"kbs\":";s+=x.kbs10/10.0f;s+=",\"rttMs\":";s+=x.rttMs;s+='}';}
  s+=']';
}
// Transmit power, fully automatic: a ping test 60 s after joining the router
// and again every 30 minutes while nobody is downloading, keeping the
// strength the router answers best. Nothing to set or press.
void linkTuneTask(void *){
  uint32_t lastRun=0,lastIp=0;
  for(;;){
    vTaskDelay(pdMS_TO_TICKS(2000));
    const bool online=routerConfigured()&&!routerHold&&WiFi.status()==WL_CONNECTED&&staIpAt&&(uint32_t)(millis()-staIpAt)>=60000UL;
    if(!online||otaActive||httpBusySince)continue;
    const bool fresh=staIpAt!=lastIp;                       // a new connection
    const bool due=lastRun&&(uint32_t)(millis()-lastRun)>=1800000UL;
    if(!fresh&&!due)continue;
    if(!fresh&&(uint32_t)(millis()-lastHttpMs)<20000UL)continue;  // the app is busy: later
    lastIp=staIpAt;lastRun=millis()?millis():1;
    const uint8_t best=runLinkTune(txLevel);
    if(best!=txLevel){
      txLevel=best;
      Preferences tp;if(tp.begin("enginelink",false)){tp.putUChar("txal",best);tp.end();}
    }
  }
}
void httpSelfCheckTask(void *){
  uint8_t misses=0;uint32_t healedAt=0;
  vTaskDelay(pdMS_TO_TICKS(60000));
  for(;;){
    vTaskDelay(pdMS_TO_TICKS(20000));
    if(httpBusySince||pendingNetwork||otaActive)continue;
    bool ok=false;
    NetworkClient c;
    if(c.connect(IPAddress(127,0,0,1),80,3000)){
      c.setTimeout(4000);
      c.print("GET /api/health HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
      const String line=c.readStringUntil('\n');
      ok=line.startsWith("HTTP/1.1 200");
    }
    c.stop();
    if(ok){misses=0;healedAt=0;continue;}
    ++httpSelfFails;
    if(httpBusySince)continue;  // a long request started meanwhile
    if(++misses<3)continue;
    if(!healedAt){healedAt=millis()?millis():1;httpHealPending=true;continue;}
    if((uint32_t)(millis()-healedAt)>=120000UL){
      ++httpSelfRestarts;
      Serial.println("Web server still not answering: saving and restarting");
      if(engineMx&&xSemaphoreTake(engineMx,pdMS_TO_TICKS(3000))==pdTRUE)saveTotals();
      delay(100);ESP.restart();
    }
  }
}
