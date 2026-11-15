// Wi-Fi log on the SD card, and commands typed in the serial monitor.
// The SD card survives a USB reinstall, so the Wi-Fi history is kept there:
//   /WIFI.LOG      one line per Wi-Fi event (start, own Wi-Fi, router, drops)
//   /WIFIREP.TXT   the full Wi-Fi report, saved every 30 minutes and on request
//   /TRIPS/Tnnnnnn.WIF  the same report every 10 minutes of a trip and at its end
// Both are capped (older text moves to .OLD) so the card never fills up.
// Only the main loop writes; the Wi-Fi callback only adds to the RAM trace.
static const uint32_t WIFI_LOG_MAX=256UL*1024;
static uint32_t wifiLogged=0,wifiReportSavedAt=0;
static bool wifiBootLogged=false;
static File wifiOpen(const char *name){
  if(!sdReady||!cardInserted())return File();
  File f=SD.open(name,FILE_READ);
  const size_t sz=f?f.size():0;if(f)f.close();
  if(sz>WIFI_LOG_MAX){String old=String(name);old.replace(".LOG",".OLD");old.replace(".TXT",".OLD");SD.remove(old);SD.rename(name,old);}
  return SD.open(name,FILE_APPEND);
}
static void wifiStamp(String &s){
  char b[48];const time_t t=time(nullptr);
  if(validWall()){struct tm tm;gmtime_r(&t,&tm);strftime(b,sizeof(b),"%Y-%m-%d %H:%M:%S UTC",&tm);}
  else snprintf(b,sizeof(b),"up %lus",(unsigned long)(millis()/1000));
  s+=b;
}
// Lines wait in RAM and serviceWifiLog() writes them in one go with the
// engine lock let go: opening the file searches the root directory, up to
// ~0.3 s on a card with many logs (plan 13.6.1). Bounded; the count of
// lines dropped while the card was missing or slow is logged with the next.
static const size_t WIFI_PENDING_MAX=3072;
static String wifiPending;
static uint32_t wifiDropped=0;
void wifiLog(const String &what){
  if(!sdReady)return;
  String s;wifiStamp(s);s+="  ";s+=what;s+='\n';
  if(wifiPending.length()+s.length()>WIFI_PENDING_MAX){++wifiDropped;return;}
  wifiPending+=s;
}
static void wifiFlush(){
  if(!wifiPending.length()&&!wifiDropped)return;
  String out(std::move(wifiPending));wifiPending=String();
  if(wifiDropped){String d;wifiStamp(d);d+="  (";d+=wifiDropped;d+=" Wi-Fi log lines dropped)\n";out+=d;wifiDropped=0;}
  const bool gap1=sdGapBegin();
  File f=wifiOpen("/WIFI.LOG");if(f){f.print(out);f.close();}
  sdGapEnd(gap1);
}
void saveWifiReport(){
  if(!sdReady||!cardInserted())return;
  String s="\n\n######## ";wifiStamp(s);s+=" ########\n";
  String r=buildNetworkReport();if(cfg.ssid.length()>=3)r.replace(cfg.ssid,"<router name hidden>");
  const bool gap2=sdGapBegin();
  File f=wifiOpen("/WIFIREP.TXT");
  const bool saved=(bool)f;
  if(saved){f.print(s);f.print(r);f.close();}
  sdGapEnd(gap2);
  if(saved)wifiReportSavedAt=millis()?millis():1;
}
// The Wi-Fi report of a trip (0.17.7): appended every 10 minutes while the trip
// runs and once when it ends, beside the trip's other files. /WIFIREP.TXT keeps
// about 13 hours: the reports of the boat trip of 2026-11-14 were gone the next
// morning. At most 1 MB per trip; kept until the card is wiped, as the trip.
static const uint32_t TRIP_WIFI_MAX=1024UL*1024;
static uint32_t tripWifiAt=0;
static void saveTripWifi(uint32_t id){
  if(!id||!sdReady||!cardInserted())return;
  char name[32];snprintf(name,sizeof(name),"/TRIPS/T%06lu.WIF",(unsigned long)id);
  String s="\n\n######## ";wifiStamp(s);s+=" ########\n";
  String r=buildNetworkReport();if(cfg.ssid.length()>=3)r.replace(cfg.ssid,"<router name hidden>");
  const bool gap=sdGapBegin();
  if(ensureDir("/TRIPS")){File f=SD.open(name,FILE_APPEND);if(f){if(f.size()<TRIP_WIFI_MAX){f.print(s);f.print(r);}f.close();}}
  sdGapEnd(gap);
}
static void serviceTripWifi(){
  const uint32_t now=millis()?millis():1;
  if(tripWifiEnded){const uint32_t id=tripWifiEnded;tripWifiEnded=0;tripWifiAt=0;saveTripWifi(id);return;}
  if(!totals.tripActive||!tripSeriesId){tripWifiAt=0;return;}
  if(!tripWifiAt)tripWifiAt=now;
  else if((uint32_t)(now-tripWifiAt)>=600000UL){tripWifiAt=now;saveTripWifi(tripSeriesId);}
}
// New Wi-Fi events from the RAM trace go to /WIFI.LOG.
void serviceWifiLog(){
  if(!sdReady||!cardInserted())return;
  if(!wifiBootLogged){
    wifiBootLogged=true;
    wifiLog(String("EngineLink ")+EL_VERSION+" started, "+(routerConfigured()&&!routerHold?"router mode":"own Wi-Fi")
      +", own Wi-Fi "+(apStarted?"on":"off")+" ("+cfg.apName+", channel "+apChannelNow+")");
  }
  if(wifiLogged!=traceTotal){
    RouterTrace t[ROUTER_TRACE_CAP];uint32_t total=0;const uint8_t n=getWifiTrace(t,&total);
    const uint32_t fresh=min<uint32_t>(total-wifiLogged,n);
    for(uint8_t i=n-fresh;i<n;i++){
      String s=traceText(t[i].type);
      if(t[i].reason){s+=" (";s+=t[i].reason;s+=' ';s+=reasonText(t[i].reason);s+=')';}
      // Phone joined or left: how every joined phone is heard (plan Stage 1d).
      if(t[i].type==7||t[i].type==8){s+=" · phones now: ";s+=apStationsText(", ");}
      // Router joined, IP, lost: its channel and the last signal before it (I15).
      if(t[i].type>=2&&t[i].type<=5&&staAssociatedEvents){s+=" · router channel ";s+=lastRouterChannel;s+=", last signal ";s+=lastRouterRssi;s+=" dBm";}
      wifiLog(s);
    }
    wifiLogged=total;
  }
  wifiFlush();
  serviceTripWifi();
  if((uint32_t)(millis()-wifiReportSavedAt)>=1800000UL||(!wifiReportSavedAt&&millis()>120000UL))saveWifiReport();
}
// Writes everything while a serial monitor reads. False once it was stuck
// 2 s (no room, or the port took nothing), so a caller sending more stops
// at once instead of waiting 2 s per piece (audit 2026-11-05 item 1). The
// waits let the engine lock go (sdGapBegin lets go only a lock this task holds).
static bool serialSend(const uint8_t *p,size_t n){
  uint32_t stuck=millis();
  while(n){
    const int room=Serial.availableForWrite();
    const size_t k=room>0?Serial.write(p,min<size_t>(n,(size_t)room)):0;
    if(k){p+=k;n-=k;stuck=millis();continue;}
    if((uint32_t)(millis()-stuck)>2000)return false;
    const bool gap=sdGapBegin();delay(1);sdGapEnd(gap);
  }
  return true;
}
static bool serialPrint(const String &s){return serialSend((const uint8_t*)s.c_str(),s.length());}
// A file from the SD card to the serial monitor, all of it with the engine lock let go.
static void serialFile(const char *name){
  if(!sdReady||!cardInserted()){serialPrint("No SD card\n");return;}
  const bool gap=sdGapBegin();
  File f=SD.open(name,FILE_READ);
  const bool opened=(bool)f;
  bool ok=opened;
  if(opened){uint8_t b[256];int got;while(ok&&(got=f.read(b,sizeof(b)))>0)ok=serialSend(b,(size_t)got);f.close();}
  sdGapEnd(gap);
  if(!opened)serialPrint(String(name)+" not found on the SD card\n");
  else if(ok)serialPrint("\n---- end of "+String(name)+" ----\n");
}
static void serialHelp(){
  serialPrint("EngineLink " EL_VERSION " commands:\n"
    "  wifi           Wi-Fi report now\n"
    "  wifi log       Wi-Fi events saved on the SD card\n"
    "  wifi reports   Wi-Fi reports saved on the SD card\n"
    "  wifi save      save the Wi-Fi report to the SD card now\n"
    "  status         one status line\n"
    "  help           this list\n");
}
void serviceSerialCommands(){
  static char line[48];static uint8_t len=0;
  while(Serial.available()>0){
    const int c=Serial.read();
    if(c=='\r'||c=='\n'){
      line[len]=0;String cmd=String(line);cmd.trim();cmd.toLowerCase();len=0;
      if(!cmd.length())continue;
      if(cmd=="wifi"||cmd=="wifi report"||cmd=="report")serialPrint(buildNetworkReport()+"\n");
      else if(cmd=="wifi log"||cmd=="read wifi log")serialFile("/WIFI.LOG");
      else if(cmd=="wifi reports"||cmd=="read wifi report from sd"||cmd=="read wifi reports")serialFile("/WIFIREP.TXT");
      else if(cmd=="wifi save"){saveWifiReport();serialPrint(wifiReportSavedAt?"Saved to /WIFIREP.TXT\n":"No SD card\n");}
      else if(cmd=="status"){lastSerialStatus=millis()-60000UL;serialStatus();}
      else serialHelp();
    }else if(len<sizeof(line)-1)line[len++]=(char)c;
  }
}
