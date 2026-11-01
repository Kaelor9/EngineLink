// Web API: JSON helpers and the handlers behind the app (state, live,
// settings, trips, fuel, Wi-Fi setup), and the route table.

void jsonString(String &out,const String &v){
  out+='"';
  for(size_t i=0;i<v.length();i++){
    char c=v[i];
    if(c=='"'||c=='\\'){out+='\\';out+=c;}
    else if((uint8_t)c<32){out+=' ';}
    else out+=c;
  }
  out+='"';
}
void sendJSON(int code,const String &s){
  web.sendHeader("Cache-Control","no-store");
  web.send(code,"application/json; charset=utf-8",s);
}
void reply(bool ok,const String &message,int code){
  String s="{\"ok\":";
  s+=ok?"true":"false";
  s+=",\"message\":";jsonString(s,message);s+='}';
  sendJSON(code,s);
}
// KAD: latest reply per DataINDEX with both byte orders; the UI applies the
// VODIA scale and shows the chosen order.
void appendKadParams(String &s,uint8_t c,uint64_t now){
  s+=",\"kad\":{";
  bool first=true;
  for(uint8_t i=0;i<kadSeenCount;i++){
    const KadSeen &k=kadSeen[i];
    if(k.ch!=c||now<k.at||now-k.at>PARAM_FRESH_MS)continue;
    const KadParam *p=kadParam(k.index);
    if(!first)s+=',';
    first=false;
    s+='"';s+=k.index;s+="\":{\"v\":";s+=kadSigned(cfg.kadBigEndian?k.be:k.le,p);
    s+=",\"be\":";s+=k.be;s+=",\"le\":";s+=k.le;
    s+=",\"age\":";s+=(unsigned long)(now-k.at);
    s+='}';
  }
  s+='}';
}
void appendParams(String &s,uint8_t c,uint64_t now){
  if(cfg.protocol==PROTO_KAD){appendKadParams(s,c,now);return;}
  s+=",\"params\":{";
  bool first=true;
  for(uint8_t pid:OVERVIEW_PIDS){
    const PidSeen *best=nullptr;
    for(uint8_t i=0;i<pidCount;i++){
      if(pids[i].ch==c&&pids[i].pid==pid&&(!best||pids[i].at>best->at))best=&pids[i];
    }
    if(!best||now<best->at||now-best->at>PARAM_FRESH_MS)continue;
    uint32_t raw=0;
    const uint8_t n=min(best->len,(uint8_t)4);
    for(uint8_t b=0;b<n;b++)raw|=(uint32_t)best->data[b]<<(8*b);
    if(!first)s+=',';
    first=false;
    s+='"';s+=pid;s+="\":{\"raw\":";s+=raw;
    s+=",\"len\":";s+=best->len;
    s+=",\"mid\":";s+=best->mid;
    s+=",\"age\":";s+=(unsigned long)(now-best->at);
    s+='}';
  }
  s+='}';
}
void handleState(){
  const uint64_t now=elapsedMs();
  bool freshFuel[2]={fuelFresh(0,now),fuelFresh(1,now)};
  bool freshRPM[2]={rpmFresh(0,now),rpmFresh(1,now)};
  bool mapped=cfg.portChannel==1||cfg.portChannel==2;
  int port=mapped?(cfg.portChannel-1):-1,starboard=mapped?(1-port):-1;
  bool totalAvailable=mapped&&freshFuel[0]&&freshFuel[1];
  float total=totalAvailable?motor[0].fuelLph+motor[1].fuelLph:0;
  bool speedAvailable=isfinite(sogKnots)&&sogAt&&now-sogAt<7000 && sogKnots>0.5;
  String s;s.reserve(14000);
  s+="{\"version\":\"" EL_VERSION "\",\"build\":\"" __DATE__ " " __TIME__ "\",\"setup\":";
  s+=cfg.completed?"true":"false";
  s+=",\"mode\":\"PASSIVE\",\"uptime\":";
  s+=(unsigned long)(now/1000ULL);
  s+=",\"bootId\":";s+=bootId;
  s+=",\"timeKnown\":";s+=validWall()?"true":"false";
  s+=",\"timeSource\":";jsonString(s,timeSource);
  s+=",\"utc\":";s+=validWall()?(long long)time(nullptr):0;
  s+=",\"network\":{\"apSsid\":\"EngineLink\",\"apClients\":";
  s+=WiFi.softAPgetStationNum();
  s+=",\"apIP\":\"192.168.4.1\",\"apChannel\":";s+=cfg.apChannel;
  s+=",\"mdns\":";s+=mdnsReady?"true":"false";
  s+=",\"router\":";s+=cfg.router?"true":"false";
  s+=",\"staConnected\":";s+=WiFi.status()==WL_CONNECTED?"true":"false";
  s+=",\"staIP\":";jsonString(s,WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():String(""));
  s+=",\"rssi\":";if(WiFi.status()==WL_CONNECTED)s+=WiFi.RSSI();else s+="null";
  s+=",\"ssid\":";jsonString(s,cfg.ssid);
  s+=",\"apJoins\":";s+=(unsigned long)apJoins;
  s+=",\"apLeaves\":";s+=(unsigned long)apLeaves;
  s+=",\"apReason\":";s+=(unsigned int)apReason;
  s+=",\"apStarts\":";s+=(unsigned long)apStarts;
  s+=",\"apStops\":";s+=(unsigned long)apStops;
  s+=",\"staDisconnects\":";s+=(unsigned long)staDisconnects;
  s+=",\"apStarted\":";s+=apStarted?"true":"false";
  s+="},\"config\":{\"router\":";
  s+=cfg.router?"true":"false";
  s+=",\"portChannel\":";s+=cfg.portChannel;
  s+=",\"apChannel\":";s+=cfg.apChannel;
  s+=",\"timePref\":";jsonString(s,cfg.timePreference);
  s+=",\"canListen\":";s+=cfg.canListen?"true":"false";
  s+=",\"defaultPassword\":";s+=cfg.apPassword=="12345678"?"true":"false";
  s+=",\"protocol\":";s+=cfg.protocol==PROTO_KAD?"\"kad\"":"\"j1587\"";
  s+=",\"kadBigEndian\":";s+=cfg.kadBigEndian?"true":"false";
  s+=",\"kadFuelDiv\":";s+=cfg.kadFuelDiv;
  s+=",\"kadVolt\":";s+=cfg.kadVolt;s+=",\"txLevel\":";s+=txLevel;s+=",\"txAutoSteps\":";s+=txAutoSteps;
  s+=",\"fuelCorr\":[";s+=String(cfg.fuelCorr[0],4);s+=',';s+=String(cfg.fuelCorr[1],4);s+=']';
  s+=",\"apName\":";jsonString(s,cfg.apName);
  s+=",\"host\":";jsonString(s,cfg.hostName);
  s+=",\"gear\":[";s+=cfg.gearMaskF;s+=',';s+=cfg.gearValF;s+=',';s+=cfg.gearMaskR;s+=',';s+=cfg.gearValR;s+=']';
  s+=",\"ssid\":";jsonString(s,cfg.ssid);
  s+=",\"tankL\":";s+=String(cfg.tankL,0);
  s+=",\"reserve\":";s+=cfg.reserve;
  s+=",\"price\":";s+=String(cfg.defaultPrice,2);
  s+=",\"tripEnd\":";s+=cfg.tripEndMin;
  s+=",\"autoPoll\":";s+=cfg.autoPoll?"true":"false";
  s+=",\"rawKeep\":";s+=cfg.rawKeepDays;
  s+=",\"ntp\":";s+=cfg.ntpOn?"true":"false";
  s+=",\"ntpServer\":";jsonString(s,cfg.ntpServer);
  s+=",\"ghToken\":";s+=cfg.ghToken.length()?"true":"false";
  s+=",\"report\":{\"sent\":";s+=reportSent;s+=",\"lastIssue\":";s+=reportLastIssue;s+=",\"lastAgo\":";if(reportLastAt)s+=(unsigned long)((millis()-reportLastAt)/1000);else s+="null";s+=",\"error\":";jsonString(s,reportError);s+=",\"pending\":";s+=reportWanted?"true":"false";s+='}';
  s+=",\"scaleOk\":";s+=cfg.fuelScaleOk?"true":"false";
  s+=",\"voltOk\":";s+=cfg.voltOk?"true":"false";
  s+=",\"fineTunes\":";s+=cfg.fineTunes;
  s+="},\"sd\":{\"ready\":";
  s+=sdReady?"true":"false";
  s+=",\"inserted\":";s+=cardInserted()?"true":"false";
  s+=",\"logging\":";s+=sdLogging?"true":"false";
  s+=",\"fault\":";s+=sdFaultLatched?"true":"false";
  s+=",\"rows\":";s+=sdRows;
  s+=",\"errors\":";s+=sdErrors;
  s+=",\"filename\":";jsonString(s,sessionName);
  s+=",\"status\":";jsonString(s,sdMessage);
  s+="},\"test\":{\"running\":";s+=calActive?"true":"false";
  s+=",\"rows\":";s+=calRows;
  s+=",\"invalid\":";s+=calInvalid;
  s+=",\"step\":";s+=calSteps;
  s+=",\"phase\":";jsonString(s,calPhase);
  s+=",\"channel\":";s+=calChannel;
  s+=",\"referenceRPM\":";if(calReferenceRPM>=0)s+=calReferenceRPM;else s+="null";
  s+=",\"referenceFuel\":";if(calReferenceFuel>=0)s+=String(calReferenceFuel,3);else s+="null";
  s+=",\"file\":";jsonString(s,calName);
  s+=",\"seconds\":";s+=calActive?((millis()-calStartMs)/1000UL):0;
  s+=",\"stepSeconds\":";s+=calActive?((millis()-calStepStartMs)/1000UL):0;
  // Cached: avoid an SD directory lookup on every status poll.
  s+=",\"available\":";s+=(sdReady&&!calActive&&testFileAvailable)?"true":"false";
  // Report the real system state even while the LED demo sequence runs, so a
  // test never hides an active fault in the UI.
  s+="},\"led\":{\"name\":";jsonString(s,ledlang::PATTERNS[ledStateNow()].name);
  s+=",\"status\":";s+=(unsigned)ledStateNow();
  s+=",\"testing\":";s+=ledTestRunning()?"true":"false";
  s+="},\"hardware\":{\"heap\":";
  s+=ESP.getFreeHeap();
  s+=",\"minHeap\":";s+=ESP.getMinFreeHeap();
  s+=",\"psram\":";s+=psramFound()?ESP.getPsramSize():0;
  s+=",\"freePsram\":";s+=psramFound()?ESP.getFreePsram():0;
  s+=",\"opto\":";s+=digitalRead(OPTO_PIN);
  s+=",\"canReady\":";s+=canReady?"true":"false";
  s+=",\"nmeaRx\":";s+=ctr.nmeaFrames;
  s+=",\"resetReason\":";s+=(unsigned int)esp_reset_reason();
  s+=",\"sketchKB\":";s+=sketchKB;
  s+=",\"uartReady\":[";s+=uartReady[0]?"true":"false";s+=",";s+=uartReady[1]?"true":"false";s+="]";
  s+="},\"channels\":[";
  for(int c=0;c<2;c++){
    if(c)s+=',';
    s+="{\"name\":\"J";s+=c==0?"7":"6";s+="\",\"frames\":";
    s+=ctr.frames[c];
    s+=",\"invalid\":";s+=ctr.invalid[c];
    s+=",\"drops\":";s+=(unsigned long)ctr.rxDropped[c];
    s+=",\"uartFaults\":";s+=(unsigned long)ctr.uartFaults[c];
    s+=",\"lineErrors\":";s+=(unsigned long)ctr.lineErrors[c];
    s+=",\"fuel\":";
    if(freshFuel[c])s+=String(motor[c].fuelLph,3);else s+="null";
    s+=",\"fuelAge\":";if(motor[c].fuelEver)s+=(unsigned long)(now-motor[c].fuelAt);else s+="null";
    s+=",\"rpm\":";
    if(freshRPM[c])s+=String(motor[c].rpm,1);else s+="null";
    s+=",\"running\":";s+=motor[c].running?"true":"false";
    s+=",\"runSeconds\":";s+=motor[c].running?(unsigned long)((now-motor[c].runStartMs)/1000ULL):0UL;
    s+=",\"observedLiters\":";s+=String(motor[c].observedLiters,4);
    s+=",\"fuelSamples\":";s+=motor[c].fuelSamples;
    s+=",\"rpmSamples\":";s+=motor[c].rpmSamples;
    s+=",\"gaps\":";s+=motor[c].gaps;
    s+=",\"lastMID\":";s+=motor[c].lastMID;
    s+=",\"tripLiters\":";s+=String(tripLitersFor(c),3);
    s+=",\"lifetimeLiters\":";s+=String(lifetimeLitersFor(c),2);
    // Uncorrected candidates for the tank-to-tank scale decision.
    s+=",\"trip128\":";s+=String(totals.tripRawH[c]/128.0,3);
    s+=",\"trip256\":";s+=String(totals.tripRawH[c]/256.0,3);
    const KadLink &k=kad[c];
    s+=",\"link\":{\"polling\":";s+=k.polling?"true":"false";
    s+=",\"requests\":";s+=k.requests;
    s+=",\"replies\":";s+=k.replies;
    s+=",\"foreign\":";s+=k.foreignRequests;
    s+=",\"foreignAge\":";if(k.foreignRequests)s+=(unsigned long)(now-k.lastForeignAt);else s+="null";
    s+=",\"unanswered\":";s+=k.unanswered;
    s+=",\"orphans\":";s+=k.orphanReplies;
    s+=",\"replyAge\":";if(k.lastReplyAt)s+=(unsigned long)(now-k.lastReplyAt);else s+="null";
    s+=",\"pollSeconds\":";s+=k.polling?(unsigned long)((millis()-k.startedMs)/1000UL):0UL;
    s+=",\"stopReason\":";jsonString(s,k.stopReason);
    s+='}';
    s+=",\"engineSeconds\":";s+=totals.runSeconds[c];
    s+=",\"tripSeconds\":";s+=totals.tripRunSeconds[c];
    appendParams(s,c,now);
    s+="}";
  }
  s+="],\"trip\":{\"startUtc\":";s+=(long long)totals.tripStartUtc;
  s+=",\"resets\":";s+=totals.tripResets;
  s+="},\"segments\":[";
  for(uint8_t i=0;i<segmentCount;i++){
    const Segment &g=segments[(segmentIndex+MAX_SEGMENTS-1-i)%MAX_SEGMENTS];
    if(i)s+=',';
    s+="{\"ch\":";s+=g.ch;
    s+=",\"start\":";s+=(unsigned long)(g.startMs/1000ULL);
    s+=",\"end\":";s+=(unsigned long)(g.endMs/1000ULL);
    s+=",\"utc\":";s+=(long long)g.startUtc;
    s+=",\"liters\":";s+=String(g.liters,3);
    s+=",\"maxRpm\":";s+=String(g.maxRpm,0);
    s+='}';
  }
  s+="],\"queue\":{\"capacity\":";s+=(unsigned long)framesQueueLength;
  s+=",\"waiting\":";s+=framesQueue?(unsigned long)uxQueueMessagesWaiting(framesQueue):0UL;
  s+="},\"fuel\":{\"total\":";
  if(totalAvailable)s+=String(total,3);else s+="null";
  s+=",\"portChannel\":";s+=port;
  s+=",\"starboardChannel\":";s+=starboard;
  s+=",\"sogKnots\":";
  if(speedAvailable)s+=String(sogKnots,2);else s+="null";
  s+=",\"litersPerNm\":";
  if(speedAvailable&&totalAvailable)s+=String(total/sogKnots,3);else s+="null";
  s+="},\"recent\":[";
  uint8_t n=recentCount; // all 24, so the per-channel filter is not starved
  for(int i=0;i<n;i++){
    const Recent &e=recent[(recentIndex+24-1-i)%24];
    if(i)s+=',';
    s+="{\"ch\":";s+=e.channel;
    s+=",\"mid\":";s+=e.mid;
    s+=",\"ms\":";s+=(unsigned long)(e.bootMs%4294967295ULL);
    s+=",\"ok\":";s+=e.checksum?"true":"false";
    s+=",\"dir\":\"";s+=e.dir;s+='"';
    s+=",\"kad\":";s+=e.kadIndex;
    s+=",\"hex\":";jsonString(s,e.hex);
    s+="}";
  }
  s+="]}";
  sendJSON(200,s);
}
void handlePids(){
  const uint64_t now=elapsedMs();
  String s;s.reserve(9000);
  s+="{\"full\":";s+=ctr.pidTableFull;
  s+=",\"pids\":[";
  for(uint8_t i=0;i<pidCount;i++){
    const PidSeen &p=pids[i];
    if(i)s+=',';
    s+="{\"ch\":";s+=p.ch;
    s+=",\"mid\":";s+=p.mid;
    s+=",\"pid\":";s+=p.pid;
    s+=",\"len\":";s+=p.len;
    s+=",\"count\":";s+=p.count;
    s+=",\"age\":";s+=(unsigned long)(now>=p.at?now-p.at:0);
    s+=",\"hex\":\"";
    const uint8_t n=min(p.len,(uint8_t)sizeof(p.data));
    for(uint8_t b=0;b<n;b++){char h[4];snprintf(h,sizeof(h),b?" %02X":"%02X",p.data[b]);s+=h;}
    if(p.len>sizeof(p.data))s+=" …";
    s+="\"}";
  }
  s+="],\"kad\":[";
  for(uint8_t i=0;i<kadSeenCount;i++){
    const KadSeen &k=kadSeen[i];
    if(i)s+=',';
    s+="{\"ch\":";s+=k.ch;
    s+=",\"index\":";s+=k.index;
    s+=",\"len\":";s+=k.len;
    s+=",\"count\":";s+=k.count;
    s+=",\"age\":";s+=(unsigned long)(now>=k.at?now-k.at:0);
    s+=",\"be\":";s+=k.be;s+=",\"le\":";s+=k.le;
    s+=",\"hex\":\"";
    const uint8_t n=min(k.len,(uint8_t)sizeof(k.reply));
    for(uint8_t b=0;b<n;b++){char h[4];snprintf(h,sizeof(h),b?" %02X":"%02X",k.reply[b]);s+=h;}
    if(k.len>sizeof(k.reply))s+=" …";
    s+="\"}";
  }
  s+="]}";
  sendJSON(200,s);
}
void handleHistory(){
  String s;s.reserve(HISTORY_LEN*24+200);
  s+="{\"step\":";s+=HISTORY_STEP_MS/1000;
  s+=",\"count\":";s+=histCount;
  const char *names[]={"fuel0","fuel1","rpm0","rpm1","sog"};
  for(uint8_t k=0;k<5;k++){
    s+=",\"";s+=names[k];s+="\":[";
    for(uint16_t i=0;i<histCount;i++){
      const uint16_t idx=(histIndex+HISTORY_LEN-histCount+i)%HISTORY_LEN;
      if(i)s+=',';
      if(k<2){int16_t v=histFuel[k][idx];if(v<0)s+="null";else s+=String(v/100.0f,2);}
      else if(k<4){uint16_t v=histRpm[k-2][idx];if(v==0xFFFF)s+="null";else s+=v;}
      else{int16_t v=histSog[idx];if(v<0)s+="null";else s+=String(v/100.0f,2);}
    }
    s+=']';
  }
  s+='}';
  sendJSON(200,s);
}
void handleOptions(){
  if(!web.hasArg("port")||!web.hasArg("network")){reply(false,"Missing fields",400);return;}
  String network=web.arg("network");
  if(network!="ap"&&network!="router"){reply(false,"Invalid network mode",400);return;}
  String apPass=web.arg("apPass");
  if(apPass.length() && (apPass.length()<8||apPass.length()>63)){reply(false,"EngineLink Wi-Fi password must be 8 to 63 characters",400);return;}
  String ssid=web.arg("ssid");
  if(network=="router"&&ssid.isEmpty()){reply(false,"Enter router Wi-Fi network name",400);return;}
  String preference=web.arg("time");
  if(preference!="auto"&&preference!="phone"&&preference!="gps"&&preference!="ntp"&&preference!="manual"){
    reply(false,"Invalid time source",400);return;
  }
  int port=web.arg("port").toInt();
  if(port<0||port>2){reply(false,"Invalid engine assignment",400);return;}
  int apch=web.hasArg("apch")?web.arg("apch").toInt():cfg.apChannel;
  if(apch!=1&&apch!=6&&apch!=11){reply(false,"Wi-Fi channel must be 1, 6 or 11",400);return;}
  // Start/stop CAN first: a failure must not leave half-applied settings in RAM.
  bool newCAN=web.arg("can")=="1";
  if(newCAN!=cfg.canListen){
    if(!newCAN)stopCAN();
    else if(!startCAN()){reply(false,"NMEA2000 listen-only interface could not start",409);return;}
    cfg.canListen=newCAN;
  }
  bool changeNetwork=cfg.router!=(network=="router")||cfg.ssid!=ssid||cfg.apChannel!=(uint8_t)apch;
  String newRouterPass=web.arg("pass");
  if(newRouterPass.length()){cfg.wifiPassword=newRouterPass;changeNetwork=true;}
  if(apPass.length()&&apPass!=cfg.apPassword){cfg.apPassword=apPass;changeNetwork=true;}
  cfg.router=network=="router";
  cfg.ssid=ssid;
  cfg.apChannel=(uint8_t)apch;
  cfg.timePreference=preference;
  cfg.portChannel=(uint8_t)port;
  cfg.completed=true;
  saveSettings();
  reply(true,changeNetwork?"Saved. Wi-Fi restarts now; reconnect to EngineLink if your phone drops.":"Settings saved.");
  if(changeNetwork)pendingNetwork=true;
}
void handleTime(){
  if(!allowTime("phone")){reply(false,"Phone is not the selected time source",409);return;}
  String v=web.arg("ms");
  if(v.length()<13||v.length()>14){reply(false,"Invalid time",400);return;}
  uint64_t epochMs=strtoull(v.c_str(),nullptr,10);
  time_t sec=(time_t)(epochMs/1000ULL);
  if(sec<1700000000||sec>4102444800LL){reply(false,"Phone time out of range",400);return;}
  setClockFromUtc(sec,"phone");
  if(!totals.tripStartUtc&&totals.tripResets==0){totals.tripStartUtc=sec;totalsDirty=true;}
  reply(true,"Clock synchronized from phone");
}
void handleLEDTest(){
  ledTestStartedMs=millis();ledPrev=0xFFFFFFFF;
  reply(true,"Showing every LED pattern, 3 seconds each");
}
void handleDownload(){
  if(!sdReady||!sessionName.length()||!SD.exists(sessionName)){web.send(404,"text/plain","No session file");return;}
  if(sdLogging)logFile.flush();
  File f=SD.open(sessionName,FILE_READ);
  if(!f){web.send(500,"text/plain","Cannot open log file");return;}
  streamFileOut(f,"raw log","EngineLink-session.csv");
  f.close();
}
void handleSegmentsDownload(){
  if(!sdReady||!SD.exists("/SEGMENTS.CSV")){web.send(404,"text/plain","No engine-run segments recorded on this SD card yet");return;}
  File f=SD.open("/SEGMENTS.CSV",FILE_READ);
  if(!f){web.send(500,"text/plain","Cannot open segments file");return;}
  streamFileOut(f,"engine runs","EngineLink-engine-runs.csv");
  f.close();
}
// Polling is never persisted: after any restart EngineLink only listens.
void handleKadPoll(){
  const int ch=web.arg("ch").toInt();
  if(ch<0||ch>1){reply(false,"Invalid connector",400);return;}
  if(web.arg("on")!="1"){
    if(kad[ch].polling)stopPolling(ch,"Stopped by user");
    reply(true,"Polling stopped. EngineLink only listens on this connector.");return;
  }
  if(web.arg("confirm")!="POLL"){reply(false,"Confirmation missing",400);return;}
  String why;
  if(!kad[ch].polling&&!startPolling(ch,why)){reply(false,why,409);return;}
  kad[ch].byAuto=false;kad[ch].byTest=false;
  reply(true,String("Polling ")+(ch==0?"J7":"J6")+": reading as fast as the engine answers. It stops after a restart.");
}
void applyProtocol(){
  for(uint8_t c=0;c<2;c++){
    if(kad[c].polling)stopPolling(c,"Protocol changed");
    kad[c].pending=false;
    kad[c].lastAutoTry=0;  // automatic reading starts again right away
    if(uartReady[c])uart_set_rx_timeout(portFor(c),rxTimeoutSymbols());
  }
}
void handleKadConfig(){
  if(web.hasArg("protocol")){
    const String v=web.arg("protocol");
    if(v!="kad"&&v!="j1587"){reply(false,"Invalid protocol",400);return;}
    const uint8_t np=v=="kad"?PROTO_KAD:PROTO_J1587;
    if(np!=cfg.protocol){cfg.protocol=np;applyProtocol();}
  }
  if(web.hasArg("endian"))cfg.kadBigEndian=web.arg("endian")!="le";
  if(web.hasArg("fuelDiv")){
    const int d=web.arg("fuelDiv").toInt();
    if(d!=128&&d!=256){reply(false,"Fuel scale must be 1/128 or 1/256",400);return;}
    cfg.kadFuelDiv=(uint16_t)d;
    recalibrate();
  }
  if(web.hasArg("volt")){
    const int v=web.arg("volt").toInt();
    if(v!=0&&v!=1){reply(false,"Invalid voltage scale",400);return;}
    cfg.kadVolt=(uint8_t)v;
  }
  for(uint8_t c=0;c<2;c++){
    const String key=String("corr")+c;
    if(!web.hasArg(key))continue;
    const float f=web.arg(key).toFloat();
    if(!(f>=0.5f&&f<=2.0f)){reply(false,"Correction must be between 0.5 and 2.0",400);return;}
    cfg.fuelCorr[c]=f;
  }
  saveSettings();
  reply(true,"Engine data settings saved. Totals are recalculated from the raw record.");
}
// ---------- App API ----------
bool parseIds(const String &ids,uint32_t *out,uint8_t &n){
  n=0;int from=0;
  while(from<(int)ids.length()&&n<MAX_TRIPS){
    int comma=ids.indexOf(',',from);if(comma<0)comma=ids.length();
    const uint32_t id=ids.substring(from,comma).toInt();
    if(id)out[n++]=id;
    from=comma+1;
  }
  return n>0;
}
void jnum(String &s,double v,uint8_t dec){if(isfinite(v))s+=String(v,(unsigned int)dec);else s+="null";}
void jkey(String &s,const char *k){s+='"';s+=k;s+="\":";}
int portIndex(){return cfg.portChannel==1?0:cfg.portChannel==2?1:-1;}
// Small, fast summary for the live screens. Polled about twice a second.
String makeLiveJson(){
  const uint64_t now=elapsedMs();
  String s;s.reserve(2600);
  s+="{\"v\":\"" EL_VERSION "\",\"up\":";s+=(unsigned long)(now/1000ULL);
  s+=",\"utc\":";s+=validWall()?(long long)time(nullptr):0;
  s+=",\"ts\":";jsonString(s,timeSource);
  s+=",\"proto\":";s+=cfg.protocol==PROTO_KAD?"\"kad\"":"\"j1587\"";
  s+=",\"port\":";s+=portIndex();
  s+=",\"e\":[";
  for(uint8_t c=0;c<2;c++){
    if(c)s+=',';
    const KadLink &k=kad[c];
    s+="{\"run\":";s+=motor[c].running?"true":"false";
    s+=",\"rpm\":";if(rpmFresh(c,now))jnum(s,motor[c].rpm,0);else s+="null";
    s+=",\"lph\":";if(fuelFresh(c,now))jnum(s,motor[c].fuelLph,2);else s+="null";
    s+=",\"seen\":";
    const uint64_t last=max(motor[c].rpmAt,motor[c].fuelAt);
    if(last&&now>=last)s+=(unsigned long)((now-last)/1000ULL);else s+="null";
    s+=",\"poll\":";s+=k.polling?"true":"false";
    s+=",\"why\":";jsonString(s,k.stopReason);
    s+=",\"hrs\":";jnum(s,totals.lastEcuHours[c],1);
    s+=",\"x\":{";
    bool first=true;
    for(uint8_t i=0;i<KAD_N;i++){
      float v;
      if(!engineValueAge(c,KAD_PARAMS[i].index,v,15000))continue;
      if(!first)s+=',';
      first=false;
      s+='"';s+=KAD_PARAMS[i].index;s+="\":";jnum(s,v,2);
    }
    s+="}}";
  }
  s+="],\"trip\":{\"act\":";s+=totals.tripActive?"true":"false";
  s+=",\"start\":";s+=(long long)totals.tripStartUtc;
  s+=",\"run\":";s+=totals.tripAnySec;
  s+=",\"l\":[";jnum(s,tripLitersFor(0),2);s+=',';jnum(s,tripLitersFor(1),2);s+=']';
  s+=",\"max\":";s+=totals.tripMaxRpm;
  s+="},\"tank\":{\"cap\":";jnum(s,cfg.tankL,0);
  const double ab=tankAboard();
  s+=",\"aboard\":";if(ab>=0)jnum(s,ab,1);else s+="null";
  s+=",\"pm\":";jnum(s,tankPlusMinus(),0);
  s+=",\"reserve\":";s+=cfg.reserve;
  s+="},\"price\":";jnum(s,latestPrice(),2);
  s+=",\"sog\":";if(sogFresh())jnum(s,sogKnots,1);else s+="null";
  s+=",\"net\":{\"ap\":";jsonString(s,cfg.apName);
  s+=",\"host\":";jsonString(s,cfg.hostName);
  s+=",\"cl\":";s+=WiFi.softAPgetStationNum();
  s+=",\"ch\":";s+=apChannelNow;
  s+=",\"rt\":";s+=routerConfigured()?"true":"false";
  s+=",\"sta\":";s+=WiFi.status()==WL_CONNECTED?"true":"false";
  s+=",\"ssid\":";jsonString(s,cfg.ssid);
  s+=",\"rssi\":";if(WiFi.status()==WL_CONNECTED)s+=WiFi.RSSI();else s+="null";
  s+=",\"ip\":";if(WiFi.status()==WL_CONNECTED)jsonString(s,WiFi.localIP().toString());else s+="null";
  s+=",\"conflict\":";s+=staConflict?"true":"false";
  s+=",\"mode\":";jsonString(s,(WiFi.getMode()&WIFI_MODE_AP)?"ap":"sta");
  s+=",\"rescue\":";s+=routerHold?"true":"false";
  s+="},\"sd\":{\"ok\":";s+=sdReady?"true":"false";
  s+=",\"log\":";s+=sdLogging?"true":"false";
  s+="},\"test\":";s+=calActive?"true":"false";
  s+=",\"rb\":";s+=rollbackPending?"true":"false";
  s+=",\"fw\":";s+=(unsigned)fwState;
  s+=",\"led\":\"";s+=ledlang::PATTERNS[ledStateNow()].id;s+='"';
  s+=",\"ev\":{\"n\":";s+=unackedEvents();s+=",\"alarm\":";s+=diagRules.alarm()?"true":"false";s+=",\"warn\":";s+=diagRules.warning()?"true":"false";
  s+=",\"last\":";
  if(eventCount){const EventRec &e=events[0];s+="{\"id\":";s+=e.id;s+=",\"t\":\"";s+=diag::TYPE_ID[e.type<diag::T_COUNT?e.type:0];s+="\",\"eng\":";s+=e.eng;s+=",\"sev\":";s+=e.sev;
    s+=",\"v\":";jnum(s,e.value,2);s+=",\"ref\":";jnum(s,e.ref,2);s+=",\"utc\":";s+=(long long)e.utc;s+=",\"ack\":";s+=e.ack;s+='}';}
  else s+="null";
  s+='}';
  s+='}';
  return s;
}
void handleLive(){sendJSON(200,makeLiveJson());}
void appendTrip(String &s,const TripRec &r){
  s+="{\"id\":";s+=r.id;s+=",\"g\":";s+=r.group;
  s+=",\"s\":";s+=(long long)r.startUtc;s+=",\"e\":";s+=(long long)r.endUtc;
  s+=",\"run\":";s+=r.anySec;
  s+=",\"l\":[";jnum(s,tripLitres(r,0),2);s+=',';jnum(s,tripLitres(r,1),2);s+=']';
  s+=",\"max\":";s+=r.maxRpm;
  s+=",\"p\":";s+=r.people;s+=",\"sea\":";s+=r.sea;s+=",\"w\":";s+=r.wind;s+=",\"x\":";s+=r.extras;
  s+=",\"n\":";jsonString(s,String(r.note));
  s+=",\"b\":";s+=r.bootId;
  s+='}';
}
void handleTrips(){
  String s;s.reserve(220*tripCount+200);
  s+="{\"trips\":[";
  for(uint8_t i=0;i<tripCount;i++){if(i)s+=',';appendTrip(s,trips[i]);}
  s+="],\"port\":";s+=portIndex();
  s+='}';
  sendJSON(200,s);
}
void handleTripNote(){
  TripRec *r=tripById(web.arg("id").toInt());
  if(!r){reply(false,"Trip not found",404);return;}
  const int p=web.arg("people").toInt(),sea=web.arg("sea").toInt(),w=web.arg("wind").toInt(),x=web.arg("extras").toInt();
  if(p<0||p>30||sea<0||sea>3||w<0||w>3||x<0||x>7){reply(false,"Invalid note",400);return;}
  r->people=p;r->sea=sea;r->wind=w;r->extras=x;
  String n=web.arg("note");n.trim();
  memset(r->note,0,sizeof(r->note));
  strncpy(r->note,n.c_str(),sizeof(r->note)-1);
  saveTrips();
  reply(true,"Trip notes saved");
}
// Combines the chosen trips (and the groups they already belong to) into
// one group, named after the oldest trip.
void handleTripCombine(){
  uint32_t sel[MAX_TRIPS];uint8_t n=0;
  parseIds(web.arg("ids"),sel,n);
  if(n<2){reply(false,"Select two or more trips",400);return;}
  bool hit[MAX_TRIPS]={};
  for(uint8_t j=0;j<n;j++){
    const TripRec *r=tripById(sel[j]);
    if(!r){reply(false,"Trip not found",404);return;}
    for(uint8_t i=0;i<tripCount;i++)if(trips[i].id==r->id||(r->group&&trips[i].group==r->group))hit[i]=true;
  }
  uint32_t groupId=0xFFFFFFFF;
  for(uint8_t i=0;i<tripCount;i++)if(hit[i]&&trips[i].id<groupId)groupId=trips[i].id;
  for(uint8_t i=0;i<tripCount;i++)if(hit[i])trips[i].group=groupId;
  saveTrips();
  reply(true,"Trips combined");
}
void handleTripSplit(){
  const uint32_t g=web.arg("group").toInt();
  if(!g){reply(false,"Invalid group",400);return;}
  bool any=false;
  for(uint8_t i=0;i<tripCount;i++)if(trips[i].group==g){trips[i].group=0;any=true;}
  if(!any){reply(false,"Group not found",404);return;}
  saveTrips();
  reply(true,"Split into separate trips");
}
void handleFuelList(){
  String s;s.reserve(120*fuelCount+400);
  s+="{\"log\":[";
  for(uint8_t i=0;i<fuelCount;i++){
    const FuelRec &r=fuelLog[i];
    if(i)s+=',';
    s+="{\"utc\":";s+=(long long)r.utc;
    s+=",\"l\":";jnum(s,r.litres,1);
    s+=",\"p\":";jnum(s,r.price,2);
    s+=",\"full\":";s+=r.full?"true":"false";
    s+=",\"c\":";if(r.counted>=0)jnum(s,r.counted,1);else s+="null";
    s+=",\"r128\":";jnum(s,r.rawDelta/128.0,1);
    s+=",\"r256\":";jnum(s,r.rawDelta/256.0,1);
    s+=",\"q\":";s+=r.quality;s+=",\"use\":";s+=r.calUse;
    s+=",\"cs\":";s+=calState[i];s+=",\"z\":";jnum(s,calZ[i],1);
    s+=",\"pump\":";jnum(s,calPump[i],1);
    s+=",\"now\":";if(calCounted[i]>=0)jnum(s,calCounted[i],1);else s+="null";
    s+=",\"base\":";if(calBase[i]>=0)jnum(s,calBase[i],1);else s+="null";
    s+='}';
  }
  s+="],\"cal\":{\"f\":";jnum(s,cfg.fuelCorr[0],4);
  s+=",\"b\":[";for(uint8_t b=0;b<FUEL_BANDS;b++){if(b)s+=',';jnum(s,cfg.bandAdj[b],4);}
  s+="],\"sig\":";jnum(s,cfg.calSig,4);s+=",\"n\":";s+=cfg.calFills;s+=",\"l\":";jnum(s,cfg.calLitres,1);
  s+=",\"bands\":[";
  for(uint8_t b=0;b<FUEL_BANDS;b++){
    double l=0;for(uint8_t c=0;c<2;c++)l+=totals.lifeRawB[c][b]/(double)cfg.kadFuelDiv;
    if(b)s+=',';jnum(s,l,1);
  }
  s+="],\"pm\":";jnum(s,tankPlusMinus(),1);
  s+=",\"gaps\":";s+=(unsigned long)(totals.gapFillSec[0]+totals.gapFillSec[1]);
  s+="},\"tunes\":";s+=cfg.fineTunes;
  s+=",\"tuneDev\":";jnum(s,cfg.lastTuneDev,4);
  s+=",\"since\":";if(tankKnown())jnum(s,usedSince(totals.tankRef),1);else s+="null";
  s+=",\"missed\":{\"n\":";s+=totals.missedEvents;s+=",\"l\":";jnum(s,totals.estMissedL,1);
  s+=",\"last\":";s+=(long long)lastMissedUtc;s+=",\"lastL\":";jnum(s,lastMissedL,1);
  s+="}}";
  sendJSON(200,s);
}
void handleFuelAdd(){
  String msg;
  const float litres=web.arg("litres").toFloat(),price=web.arg("price").toFloat();
  int q=web.hasArg("q")?web.arg("q").toInt():(web.arg("full")=="1"?1:0);
  if(q<0||q>2)q=1;
  const int64_t utc=strtoll(web.arg("utc").c_str(),nullptr,10);
  if(q&&cfg.tankL<=0){reply(false,"Set the fuel tank size in Settings first",409);return;}
  const bool ok=addFuel(litres,price,(uint8_t)q,utc,msg);
  reply(ok,msg,ok?200:400);
}
void handleFuelEdit(){
  const int i=web.arg("i").toInt();
  const int q=web.hasArg("q")?web.arg("q").toInt():-1,use=web.hasArg("use")?web.arg("use").toInt():-1;
  if(i<0||!editFuel((uint8_t)i,q,use)){reply(false,"Entry not found",404);return;}
  reply(true,cfg.calFills?"Fuel figures now within about "+String(cfg.calSig*200.0f,1)+" %":"Saved");
}
void handleFuelDelete(){
  const int i=web.arg("i").toInt();
  if(i<0||!deleteFuel((uint8_t)i)){reply(false,"Entry not found",404);return;}
  reply(true,"Entry deleted");
}
void handleEconomy(){
  String s;s.reserve(3200);
  s+="{\"season\":";s+=(long long)totals.seasonStartUtc;
  s+=",\"b\":[";
  for(uint8_t b=0;b<RPM_BUCKETS;b++){
    if(b)s+=',';
    s+="[";s+=1000+b*250;s+=',';jnum(s,totals.bucketSec[b],0);s+=',';jnum(s,totals.bucketL[b],2);
    s+=',';jnum(s,totals.bucketSogSec[b],0);s+=',';jnum(s,totals.bucketSogL[b],2);s+=',';jnum(s,totals.bucketNm[b],2);s+=']';
  }
  s+="],\"z\":[";
  for(uint8_t z=0;z<ZONES;z++){
    if(z)s+=',';
    s+='[';jnum(s,totals.zoneL[z],2);s+=',';jnum(s,totals.zoneSec[z],0);s+=',';jnum(s,totals.zoneNm[z],2);s+=']';
  }
  s+="],\"trips\":";s+=tripCount;
  s+=",\"h\":{\"dev\":[";
  for(uint8_t c=0;c<2;c++){
    if(c)s+=',';
    uint16_t maxTrips=0;
    for(uint8_t b=0;b<RPM_BUCKETS;b++)if(health.baseTrips[c][b]>maxTrips)maxTrips=health.baseTrips[c][b];
    s+="{\"trips\":";s+=maxTrips;s+=",\"n\":";s+=health.devSamples[c];s+=",\"rpm\":";s+=health.devRpm[c];
    s+=",\"d\":[";for(uint8_t m=0;m<HEALTH_METRICS;m++){if(m)s+=',';jnum(s,health.dev[c][m],4);}s+="]";
    // Normal values at the rpm step of the latest comparison.
    s+=",\"base\":[";
    const int bb=health.devRpm[c]>=1000?bucketFor(health.devRpm[c]):-1;
    for(uint8_t m=0;m<HEALTH_METRICS;m++){if(m)s+=',';if(bb>=0)jnum(s,health.base[c][bb][m],2);else s+="null";}
    s+="]}";
  }
  s+="],\"bal\":";jnum(s,health.balance,4);
  s+=",\"upd\":";s+=(long long)health.updatedUtc;
  s+="}}";
  sendJSON(200,s);
}
void handlePrefsGet(){
  web.sendHeader("Cache-Control","no-store");
  web.send(200,"application/json; charset=utf-8",cfg.prefs);
}
void handlePrefsSet(){
  String p=web.arg("p");
  if(p.length()<2||p.length()>4000||p[0]!='{'){reply(false,"Invalid preferences",400);return;}
  cfg.prefs=p;savePrefs();
  reply(true,"Saved");
}
// Device settings the firmware itself uses. Every field is optional.
void handleCfg(){
  if(web.hasArg("tank")){
    const float t=web.arg("tank").toFloat();
    if(!(t>=50&&t<=10000)){reply(false,"Tank size must be 50 to 10000 litres",400);return;}
    // A new size invalidates the estimate until the next full tank.
    if(fabsf(t-cfg.tankL)>0.5f){cfg.tankL=t;totals.tankRefL=-1;saveTotals();}
  }
  if(web.hasArg("reserve")){const int r=web.arg("reserve").toInt();if(r<0||r>50){reply(false,"Invalid reserve",400);return;}cfg.reserve=r;}
  if(web.hasArg("price")){const float p=web.arg("price").toFloat();if(!(p>=0&&p<1000)){reply(false,"Invalid price",400);return;}cfg.defaultPrice=p;}
  if(web.hasArg("tripEnd")){const int m=web.arg("tripEnd").toInt();if(m<1||m>60){reply(false,"Invalid trip end time",400);return;}cfg.tripEndMin=m;}
  if(web.hasArg("autoPoll")){
    const bool on=web.arg("autoPoll")=="1";
    // Switching on sends read requests to the engines: it needs the typed confirmation.
    if(on&&!cfg.autoPoll&&web.arg("confirm")!="POLL"){reply(false,"Confirmation missing",400);return;}
    cfg.autoPoll=on;
    if(!cfg.autoPoll)for(uint8_t c=0;c<2;c++)if(kad[c].polling&&kad[c].byAuto)stopPolling(c,"Automatic reading switched off");
  }
  if(web.hasArg("rawKeep")){const int d=web.arg("rawKeep").toInt();if(d<0||d>3650){reply(false,"Invalid value",400);return;}cfg.rawKeepDays=d;}
  if(web.hasArg("ntp")){cfg.ntpOn=web.arg("ntp")=="1";ntpStarted=false;}
  // Gear bits learned at the dock: "mask:value" for forward and reverse, "" to forget.
  for(uint8_t g=0;g<2;g++){
    const char *key=g?"gearR":"gearF";
    if(!web.hasArg(key))continue;
    const String v=web.arg(key);
    uint16_t m=0,val=0;
    if(v.length()){
      const int c=v.indexOf(':');
      if(c<1){reply(false,"Invalid gear pattern",400);return;}
      m=(uint16_t)strtoul(v.substring(0,c).c_str(),nullptr,0);val=(uint16_t)strtoul(v.substring(c+1).c_str(),nullptr,0)&m;
    }
    if(g){cfg.gearMaskR=m;cfg.gearValR=val;}else{cfg.gearMaskF=m;cfg.gearValF=val;}
  }
  if(web.hasArg("host")){
    String v=web.arg("host");v.trim();v.toLowerCase();v.replace(' ','-');
    if(v.endsWith(".local"))v.remove(v.length()-6);
    if(!validHostName(v)){reply(false,"Use 1 to 32 letters, digits or hyphens, for example enginelink",400);return;}
    if(v!=cfg.hostName){cfg.hostName=v;WiFi.setHostname(v.c_str());startNameService();}
  }
  if(web.hasArg("ghtoken")){String v=web.arg("ghtoken");v.trim();if(v.length()>200){reply(false,"Token too long",400);return;}cfg.ghToken=v;reportError="";}
  if(web.hasArg("ntpServer")){String v=web.arg("ntpServer");v.trim();if(v.length()<3||v.length()>63){reply(false,"Invalid server",400);return;}cfg.ntpServer=v;ntpStarted=false;}
  if(web.hasArg("port")){const int p=web.arg("port").toInt();if(p<0||p>2){reply(false,"Invalid socket choice",400);return;}cfg.portChannel=p;}
  if(web.hasArg("can")){
    const bool on=web.arg("can")=="1";
    if(on!=cfg.canListen){if(!on)stopCAN();else if(!startCAN()){reply(false,"NMEA 2000 listen-only interface could not start",409);return;}cfg.canListen=on;}
  }
  if(web.hasArg("fuelDiv")){
    const int d=web.arg("fuelDiv").toInt();
    if(d!=128&&d!=256){reply(false,"Fuel scale must be 1/128 or 1/256",400);return;}
    cfg.kadFuelDiv=d;cfg.fuelScaleOk=true;
    recalibrate();
  }
  if(web.hasArg("volt")){const int v=web.arg("volt").toInt();if(v!=0&&v!=1){reply(false,"Invalid voltage factor",400);return;}cfg.kadVolt=v;cfg.voltOk=true;}
  if(web.hasArg("protocol")){
    const String v=web.arg("protocol");
    if(v!="kad"&&v!="j1587"){reply(false,"Invalid protocol",400);return;}
    const uint8_t np=v=="kad"?PROTO_KAD:PROTO_J1587;
    if(np!=cfg.protocol){cfg.protocol=np;applyProtocol();}
  }
  cfg.completed=true;
  saveSettings();
  reply(true,"Saved");
}
// EngineLink's own network: name and password.
void handleApSave(){
  String name=web.arg("name");name.trim();
  String pass=web.arg("pass");
  if(pass=="__keep__")pass=cfg.apPassword; // rename only
  if(name.length()<1||name.length()>31){reply(false,"Network name must be 1 to 31 characters",400);return;}
  if(pass.length()<8||pass.length()>63){reply(false,"Password must be 8 to 63 characters",400);return;}
  const bool change=name!=cfg.apName||pass!=cfg.apPassword;
  cfg.apName=name;cfg.apPassword=pass;cfg.completed=true;saveSettings();
  reply(true,change?"Saved. The EngineLink network restarts with the new name and password; connect your phone to it again.":"Nothing changed");
  if(change)pendingNetwork=true;
}
void handleWifiScan(){
  if(web.arg("start")=="1"){
    if(!(WiFi.getMode()&WIFI_MODE_AP)||pendingNetwork||routerConfigured()&&!routerHold){
      reply(false,"Connect to EngineLink Wi-Fi before scanning for a router",409);return;
    }
    if(routerProbing){reply(false,"EngineLink is looking for the saved router; try again in a few seconds",409);return;}
    if(!scanBusy&&!scanRequested){
      // Acknowledge the HTTP request BEFORE changing radio modes. This
      // prevents the phone losing the response that initiated the scan.
      scanBusy=true;scanRequested=true;scanInFlight=false;
      scanCount=-1;scanChannel=0;scanPass=1;scanPassOneCount=0;scanEntriesCount=0;
      scanFailures=0;scanStartFailures=0;scanRawCount=0;
      scanStartedMs=0;scanCompletedMs=0;scanDurationMs=0;
      scanRestoreAt=0;scanNextAt=millis()+600UL;
    }
  }
  String s="{\"busy\":";s+=scanBusy?"true":"false";
  s+=",\"done\":";s+=scanCompletedMs>0?"true":"false";
  s+=",\"channel\":";s+=scanBusy?(unsigned)scanChannel:0;
  s+=",\"pass\":";s+=scanPass;
  s+=",\"firstPassNets\":";s+=scanPassOneCount;
  s+=",\"passes\":";s+=SCAN_PASSES;
  s+=",\"channels\":";s+=SCAN_CHANNEL_LAST;
  s+=",\"raw\":";s+=scanRawCount;
  s+=",\"failures\":";s+=scanFailures;
  s+=",\"durationMs\":";s+=scanDurationMs;
  s+=",\"error\":";
  String err="";
  if(!scanBusy){
    if(scanCount==WIFI_SCAN_FAILED)err="Wi-Fi scan could not complete. Try again.";
    else if(scanFailures>0)err="Some channels could not be scanned; refresh to try again.";
  }
  jsonString(s,err);
  s+=",\"nets\":[";
  for(uint8_t i=0;i<scanEntriesCount;i++){
    const ScanEntry &entry=scanEntries[i];
    if(i)s+=',';
    s+="{\"s\":";jsonString(s,entry.ssid);
    s+=",\"r\":";s+=entry.rssi;
    s+=",\"lock\":";s+=entry.locked?"true":"false";
    s+=",\"ch\":";s+=entry.channel;
    s+='}';
  }
  s+="]}";
  sendJSON(200,s);
}
void handleWifiConnect(){
  String ssid=web.arg("ssid");ssid.trim();
  if(!ssid.length()||ssid.length()>32){reply(false,"Choose a network",400);return;}
  const String pass=web.arg("pass");
  if(pass.length()>63){reply(false,"Password too long",400);return;}
  // The user explicitly requested another router attempt; release rescue
  // mode, including the persistent latch, without modifying AP credentials.
  if(scanBusy){reply(false,"Wait for the network scan to finish",409);return;}
  routerHold=false;routerGuard.reset();
  routerAttempts=0;
  cfg.router=true;cfg.ssid=ssid;cfg.wifiPassword=pass;cfg.completed=true;
  saveSettings();
  // WPA2 unless the scan shows a WPA3-only router.
  {uint8_t want=0;for(uint8_t k=0;k<scanEntriesCount;k++)if(scanEntries[k].ssid==ssid){want=scanEntries[k].auth==WIFI_AUTH_WPA3_PSK?1:0;break;}
   if(want!=staSecMode){staSecMode=want;saveSecMode();}}
  // The displayed scan is only for choosing the SSID. Never lock to its
  // cached BSSID/channel: it can change before the connection begins.
  staBssidKnown=false;
  String m="Connecting to "+ssid+". EngineLink will stop its own Wi-Fi and try the router for up to two minutes. Connect your phone to the router and open http://enginelink.local. If all attempts fail, EngineLink restores its own Wi-Fi automatically.";
  reply(true,m);
  networkSwitchAt=millis()+1200UL;
  pendingNetwork=true;
}
void handleWifiForget(){
  routerHold=false;routerGuard.reset();
  cfg.router=false;cfg.ssid="";cfg.wifiPassword="";saveSettings();
  // Reply on the current router connection BEFORE disabling STA. Otherwise
  // Safari sees a failed HTTP request even when the mode change succeeded.
  reply(true,"Disconnected from router. EngineLink is switching to its own Wi-Fi at http://192.168.4.1.");
  networkSwitchAt=millis()+1200UL;
  pendingNetwork=true;
}
void handleSeasonReset(){
  totals.seasonStartUtc=validWall()?(int64_t)time(nullptr):0;
  memset(totals.bucketSec,0,sizeof(totals.bucketSec));memset(totals.bucketL,0,sizeof(totals.bucketL));
  memset(totals.bucketSogSec,0,sizeof(totals.bucketSogSec));memset(totals.bucketSogL,0,sizeof(totals.bucketSogL));
  memset(totals.bucketNm,0,sizeof(totals.bucketNm));
  memset(totals.zoneL,0,sizeof(totals.zoneL));memset(totals.zoneSec,0,sizeof(totals.zoneSec));memset(totals.zoneNm,0,sizeof(totals.zoneNm));
  saveTotals();
  reply(true,"A new season has started");
}
// Sends a file from flash with a long cache time (icons, manifest).
void sendStatic(const char *type,const uint8_t *data,size_t len){
  web.sendHeader("Cache-Control","public, max-age=604800");
  web.send_P(200,type,(const char*)data,len);
}
// Closes files and stores totals before any deliberate restart.
void prepareRestart(){
  for(uint8_t c=0;c<2;c++)if(kad[c].polling)stopPolling(c,"Restart");
  if(calActive)finishCalibration(false);
  for(uint8_t c=0;c<2;c++)if(motor[c].running)closeSegment(c);
  if(sdLogging)stopLog();
  saveTotals();
}
// ---------- Wi-Fi firmware update (application image only) ----------
void handleUpdateUpload(){
  HTTPUpload &u=web.upload();
  static OtaImageHeader uploadHeader;
  if(u.status==UPLOAD_FILE_START){
    uploadHeader.reset();
    otaError="";otaBytes=0;otaChecked=false;
    if(calActive){otaError="Stop the motor test first";return;}
    for(uint8_t c=0;c<2;c++)if(kad[c].polling)stopPolling(c,"Firmware update");
    if(u.filename.indexOf("merged")>=0){otaError="This is the USB (merged) image. Use the Wi-Fi update file enginelink-update.bin";return;}
    otaResumeLogging=sdLogging;
    if(sdLogging)stopLog();
    otaActive=true;
    if(!Update.begin(UPDATE_SIZE_UNKNOWN,U_FLASH)){otaError=Update.errorString();otaActive=false;}
  }else if(u.status==UPLOAD_FILE_WRITE){
    if(otaError.length()||!otaActive)return;
    // An ESP32 application image carries its app descriptor magic at 0x20.
    if(!otaChecked){
      uploadHeader.feed(u.buf,u.currentSize);
      if(uploadHeader.complete()){
        otaChecked=true;
        if(!uploadHeader.valid()){
          otaError="Not an ESP32 application image";Update.abort();otaActive=false;return;
        }
      }
    }
    feedLoopWDT();
    if(Update.write(u.buf,u.currentSize)!=u.currentSize){otaError=Update.errorString();Update.abort();otaActive=false;return;}
    otaBytes+=u.currentSize;
  }else if(u.status==UPLOAD_FILE_END){
    if(otaError.length()||!otaActive)return;
    if(!otaChecked){otaError="Firmware header incomplete";Update.abort();otaActive=false;return;}
    if(!Update.end(true)){otaError=Update.errorString();otaActive=false;}
  }else if(u.status==UPLOAD_FILE_ABORTED){
    if(otaActive)Update.abort();
    otaError="Upload interrupted";
  }
}
void handleUpdateDone(){
  const bool ok=otaActive&&!otaError.length()&&!Update.hasError()&&otaBytes>0;
  otaActive=false;
  if(!ok){
    if(otaResumeLogging&&!sdLogging)initSD();
    otaResumeLogging=false;
    reply(false,"Update failed: "+(otaError.length()?otaError:String("no data received")),400);
    return;
  }
  prepareRestart();
  reply(true,"Update installed. EngineLink restarts now; reconnect in about 20 seconds.");
  restartAt=millis()+1500;
}
void initializeWeb(){
  web.on("/",HTTP_GET,[](){
    // no-cache + ETag: the phone checks for a newer app every time, but a
    // reload after a dropped connection is answered with a tiny 304.
    web.sendHeader("Cache-Control","no-cache");
    web.sendHeader("ETag","\"" EL_VERSION "-" __DATE__ " " __TIME__ "\"");
    if(web.header("If-None-Match")=="\"" EL_VERSION "-" __DATE__ " " __TIME__ "\""){web.send(304,"text/plain","");return;}
    web.sendHeader("Content-Encoding","gzip");
    web.send_P(200,"text/html; charset=utf-8",(const char*)ENGINE_UI_GZ,ENGINE_UI_GZ_LEN);
  });
  web.on("/favicon.ico",HTTP_GET,[](){web.send(204,"text/plain","");});
  web.on("/api/state",HTTP_GET,handleState);
  web.on("/api/pids",HTTP_GET,handlePids);
  web.on("/api/history",HTTP_GET,handleHistory);
  web.on("/api/setup",HTTP_POST,handleOptions);
  web.on("/api/settings",HTTP_POST,handleOptions);
  web.on("/api/phone-time",HTTP_POST,handleTime);
  web.on("/api/led/test",HTTP_POST,handleLEDTest);
  web.on("/api/test/start",HTTP_POST,handleCalibrationStart);
  web.on("/api/test/mark",HTTP_POST,handleCalibrationMark);
  web.on("/api/test/stop",HTTP_POST,handleCalibrationStop);
  web.on("/api/test/download",HTTP_GET,handleCalibrationDownload);
  web.on("/api/sd/remount",HTTP_POST,[](){
    if(sdLogging||calActive){reply(false,"Stop recording before remount",409);return;}
    SD.end();sdReady=false;initSD();refreshTestAvailable();reply(sdReady,sdMessage);
  });
  web.on("/api/sd/stop",HTTP_POST,[](){if(calActive){reply(false,"Stop motor test first",409);return;}stopLog();reply(true,"Session recording stopped");});
  web.on("/api/sd/start",HTTP_POST,[](){if(calActive){reply(false,"Stop motor test first",409);return;}if(sdLogging){reply(true,"Already recording");return;}initSD();reply(sdLogging,sdMessage);});
  web.on("/api/log",HTTP_GET,handleDownload);
  web.on("/api/segments.csv",HTTP_GET,handleSegmentsDownload);
  web.on("/api/trip/reset",HTTP_POST,[](){resetTrip();reply(true,"Trip counter reset. Lifetime totals are kept.");});
  web.on("/api/reboot",HTTP_POST,[](){prepareRestart();reply(true,"EngineLink restarts now.");restartAt=millis()+1000;});
  web.on("/api/factory-reset",HTTP_POST,[](){
    if(web.arg("confirm")!="ERASE"){reply(false,"Confirmation missing",400);return;}
    const bool keep=web.arg("keep")=="1";
    prepareRestart();
    pref.begin("enginelink",false);pref.clear();pref.end();
    for(const char *f:{"/prefs.json","/health.bin","/diag.bin"})LittleFS.remove(f);
    if(!keep){
      pref.begin("totals",false);pref.clear();pref.end();
      for(const char *f:{"/trips.bin","/fuel.bin"})LittleFS.remove(f);
      if(sdReady&&cardInserted()){if(sdLogging)stopLog();removeTree("/");}
    }
    reply(true,keep?"Factory settings restored. Trips and the fuel log were kept. EngineLink restarts with Wi-Fi EngineLink, password 12345678.":"Everything erased. EngineLink restarts with Wi-Fi EngineLink, password 12345678.");
    restartAt=millis()+1200;
  });
  web.on("/api/update",HTTP_POST,handleUpdateDone,handleUpdateUpload);
  web.on("/api/kad/poll",HTTP_POST,handleKadPoll);
  web.on("/api/kad/config",HTTP_POST,handleKadConfig);
  web.on("/api/health",HTTP_GET,[](){web.send(200,"text/plain","OK");});
  web.on("/api/network-health",HTTP_GET,handleNetworkHealth);
  web.on("/api/network-report",HTTP_GET,handleNetworkReport);
  // A download test measured by the phone, kept for the network report.
  web.on("/api/speedlog",HTTP_POST,[](){
    if(web.hasArg("kbs")){
      if(speedLogCount==SPEED_LOG_N){memmove(speedLog,speedLog+1,sizeof(SpeedLogRec)*(SPEED_LOG_N-1));--speedLogCount;}
      SpeedLogRec &x=speedLog[speedLogCount++];
      x.at=millis();x.level=web.hasArg("l")?(uint8_t)web.arg("l").toInt():txLevel;
      x.kbs10=(uint16_t)constrain(web.arg("kbs").toFloat()*10.0f,0.0f,65000.0f);
      x.rttMs=(uint16_t)constrain(web.arg("rtt").toInt(),0,65000);
    }
    reply(true,"ok");
  });
  web.on("/api/report/send",HTTP_POST,[](){
    if(!cfg.ghToken.length()){reply(false,"Add a GitHub token first",409);return;}
    reportWanted=true;reportManual=true;reportReason="sent from the app";
    reply(true,routerConfigured()&&!routerHold?"The report is sent within a minute":"The report is sent once EngineLink is on the router with internet");
  });
  web.on("/api/live",HTTP_GET,handleLive);
  web.on("/api/trips",HTTP_GET,handleTrips);
  web.on("/api/trip/note",HTTP_POST,handleTripNote);
  web.on("/api/trip/combine",HTTP_POST,handleTripCombine);
  web.on("/api/trip/split",HTTP_POST,handleTripSplit);
  web.on("/api/fuel",HTTP_GET,handleFuelList);
  web.on("/api/fuel/add",HTTP_POST,handleFuelAdd);
  web.on("/api/fuel/delete",HTTP_POST,handleFuelDelete);
  web.on("/api/fuel/edit",HTTP_POST,handleFuelEdit);
  web.on("/api/economy",HTTP_GET,handleEconomy);
  web.on("/api/season/reset",HTTP_POST,handleSeasonReset);
  web.on("/api/prefs",HTTP_GET,handlePrefsGet);
  web.on("/api/prefs",HTTP_POST,handlePrefsSet);
  web.on("/api/cfg",HTTP_POST,handleCfg);
  web.on("/api/wifi/ap",HTTP_POST,handleApSave);
  web.on("/api/wifi/scan",HTTP_GET,handleWifiScan);
  web.on("/api/wifi/connect",HTTP_POST,handleWifiConnect);
  web.on("/api/wifi/forget",HTTP_POST,handleWifiForget);
  web.on("/api/diag",HTTP_GET,handleDiag);
  web.on("/api/diag/test",HTTP_POST,handleDiagTest);
  web.on("/api/diag/focus",HTTP_POST,handleDiagFocus);
  web.on("/api/kad/discover",HTTP_POST,handleKadDiscover);
  web.on("/api/events",HTTP_GET,handleEvents);
  web.on("/api/events/ack",HTTP_POST,handleEventsAck);
  web.on("/api/mark",HTTP_POST,handleMark);
  web.on("/api/event/series",HTTP_GET,handleEventSeries);
  web.on("/api/trip/series",HTTP_GET,handleTripSeries);
  web.on("/api/series/recent",HTTP_GET,handleRecentSeries);
  web.on("/api/export/series.csv",HTTP_GET,handleExportSeries);
  web.on("/api/life",HTTP_GET,handleLife);
  web.on("/api/led",HTTP_GET,handleLed);
  web.on("/api/sd/info",HTTP_GET,handleSdInfo);
  web.on("/api/sd/wipe",HTTP_POST,handleSdWipe);
  web.on("/api/export/trips.csv",HTTP_GET,handleExportTrips);
  web.on("/api/export/raw.csv",HTTP_GET,handleExportRaw);
  web.on("/api/speedtest",HTTP_GET,handleSpeedTest);
  web.on("/api/backup",HTTP_GET,handleBackup);
  web.on("/api/fw/check",HTTP_POST,handleFwCheck);
  web.on("/api/fw/install",HTTP_POST,handleFwInstall);
  web.on("/api/fw/status",HTTP_GET,handleFwStatus);
  web.on("/icon.svg",HTTP_GET,[](){sendStatic("image/svg+xml",(const uint8_t*)ICON_SVG,strlen_P(ICON_SVG));});
  web.on("/apple-touch-icon.png",HTTP_GET,[](){sendStatic("image/png",ICON_PNG_180,ICON_PNG_180_LEN);});
  web.on("/icon-192.png",HTTP_GET,[](){sendStatic("image/png",ICON_PNG_192,ICON_PNG_192_LEN);});
  web.on("/icon-512.png",HTTP_GET,[](){sendStatic("image/png",ICON_PNG_512,ICON_PNG_512_LEN);});
  web.on("/manifest.webmanifest",HTTP_GET,[](){
    web.sendHeader("Cache-Control","no-cache");
    web.send(200,"application/manifest+json","{\"name\":\"Engine Link\",\"short_name\":\"Engine Link\",\"start_url\":\"/\",\"scope\":\"/\",\"display\":\"standalone\",\"background_color\":\"#0a1420\",\"theme_color\":\"#0a1420\",\"icons\":[{\"src\":\"/icon-192.png\",\"sizes\":\"192x192\",\"type\":\"image/png\"},{\"src\":\"/icon-512.png\",\"sizes\":\"512x512\",\"type\":\"image/png\",\"purpose\":\"any maskable\"}]}");
  });
  web.onNotFound([](){web.send(404,"text/plain","Not found");});
  const char *keep[]={"If-None-Match","Connection","Accept-Encoding"};
  web.collectHeaders(keep,3);
  web.enableCORS(true); // the app can also reach EngineLink through the boat router
  web.begin();
}
