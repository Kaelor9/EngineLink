// Wi-Fi log on the SD card, and commands typed in the serial monitor.
// The SD card survives a USB reinstall, so the Wi-Fi history is kept there:
//   /WIFI.LOG      one line per Wi-Fi event (start, own Wi-Fi, router, drops)
//   /WIFIREP.TXT   the full Wi-Fi report, saved every 30 minutes and on request
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
void wifiLog(const String &what){
  File f=wifiOpen("/WIFI.LOG");if(!f)return;
  String s;wifiStamp(s);s+="  ";s+=what;s+='\n';f.print(s);f.close();
}
void saveWifiReport(){
  File f=wifiOpen("/WIFIREP.TXT");if(!f)return;
  String s="\n\n######## ";wifiStamp(s);s+=" ########\n";
  String r=buildNetworkReport();if(cfg.ssid.length()>=3)r.replace(cfg.ssid,"<router name hidden>");
  f.print(s);f.print(r);f.close();wifiReportSavedAt=millis()?millis():1;
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
    RouterTrace t[ROUTER_TRACE_CAP];const uint8_t n=getWifiTrace(t);
    const uint32_t fresh=min<uint32_t>(traceTotal-wifiLogged,n);
    for(uint8_t i=n-fresh;i<n;i++){
      String s=traceText(t[i].type);
      if(t[i].reason){s+=" (";s+=t[i].reason;s+=' ';s+=reasonText(t[i].reason);s+=')';}
      wifiLog(s);
    }
    wifiLogged=traceTotal;
  }
  if((uint32_t)(millis()-wifiReportSavedAt)>=1800000UL||(!wifiReportSavedAt&&millis()>120000UL))saveWifiReport();
}
// Writes everything when a serial monitor reads; gives up after 2 s stuck.
static void serialSend(const uint8_t *p,size_t n){
  uint32_t stuck=millis();
  while(n){
    const int room=Serial.availableForWrite();
    if(room>0){const size_t k=Serial.write(p,min<size_t>(n,(size_t)room));p+=k;n-=k;if(k)stuck=millis();}
    else{if((uint32_t)(millis()-stuck)>2000)return;delay(1);}
  }
}
static void serialPrint(const String &s){serialSend((const uint8_t*)s.c_str(),s.length());}
static void serialFile(const char *name){
  if(!sdReady||!cardInserted()){serialPrint("No SD card\n");return;}
  File f=SD.open(name,FILE_READ);if(!f){serialPrint(String(name)+" not found on the SD card\n");return;}
  uint8_t b[256];int got;
  while((got=f.read(b,sizeof(b)))>0)serialSend(b,(size_t)got);
  f.close();serialPrint("\n---- end of "+String(name)+" ----\n");
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
