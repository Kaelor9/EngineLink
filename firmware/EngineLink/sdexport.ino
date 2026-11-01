// SD card info, clean-up and CSV exports, and the settings backup.

// ---------- SD card: info, wipe, clean-up, export ----------
bool isRawName(const String &n){return n.startsWith("EL")||n.startsWith("TEST")||(n.startsWith("T")&&n.endsWith(".CSV")&&n!="TRIPS.CSV");}
SdCounts sdCounts(){
  SdCounts k;
  if(!sdReady)return k;
  File root=SD.open("/");
  if(!root)return k;
  for(File f=root.openNextFile();f;f=root.openNextFile()){
    String n=f.name();
    if(n.startsWith("/"))n=n.substring(1);
    ++k.files;
    if(!f.isDirectory()&&isRawName(n)){k.rawBytes+=f.size();++k.rawFiles;}
    else if(!f.isDirectory()&&n.startsWith("EXPORT"))++k.exports;
    f.close();
  }
  root.close();
  return k;
}
void handleSdInfo(){
  const SdCounts k=sdCounts();
  String s="{\"ok\":";s+=sdReady?"true":"false";
  s+=",\"inserted\":";s+=cardInserted()?"true":"false";
  s+=",\"logging\":";s+=sdLogging?"true":"false";
  s+=",\"usedMB\":";s+=(unsigned long)sdUsedMB();s+=",\"totalMB\":";s+=(unsigned long)sdTotalMB();
  s+=",\"files\":";s+=k.files;s+=",\"rawMB\":";jnum(s,k.rawBytes/1048576.0,1);s+=",\"rawFiles\":";s+=k.rawFiles;
  s+=",\"exports\":";s+=k.exports;
  s+=",\"trips\":";s+=tripCount;s+=",\"fuel\":";s+=fuelCount;
  s+=",\"status\":";jsonString(s,sdMessage);
  s+='}';
  sendJSON(200,s);
}
uint32_t removeTree(const String &path){
  uint32_t n=0;
  File dir=SD.open(path);
  if(!dir)return 0;
  if(!dir.isDirectory()){dir.close();return SD.remove(path)?1:0;}
  for(File f=dir.openNextFile();f;f=dir.openNextFile()){
    feedLoopWDT();
    String child=String(f.path());
    const bool sub=f.isDirectory();
    f.close();
    if(sub){n+=removeTree(child);SD.rmdir(child);}else if(SD.remove(child))++n;
  }
  dir.close();
  return n;
}
void handleSdWipe(){
  if(web.arg("confirm")!="WIPE"){reply(false,"Type WIPE to confirm",400);return;}
  if(!sdReady||!cardInserted()){reply(false,"No SD card",409);return;}
  if(calActive)finishCalibration(false);
  const bool wasLogging=sdLogging;
  if(sdLogging)stopLog();
  if(tripSeries)tripSeries.close();
  const uint32_t n=removeTree("/");
  if(totals.tripActive)tripSeriesOpen();
  refreshTestAvailable();
  if(wasLogging)initSD();
  reply(true,String("SD card wiped: ")+n+" files deleted");
}
// Deletes raw logs older than the chosen number of days (needs the clock).
void serviceRawCleanup(){
  if(!cfg.rawKeepDays||!sdReady||!validWall()||calActive||otaActive)return;
  if(lastRawCleanup&&(uint32_t)(millis()-lastRawCleanup)<86400000UL)return;
  if(!lastRawCleanup&&millis()<120000)return;
  lastRawCleanup=millis();
  const time_t limit=time(nullptr)-(time_t)cfg.rawKeepDays*86400;
  File root=SD.open("/");
  if(!root)return;
  String doomed[16];uint8_t n=0;
  for(File f=root.openNextFile();f&&n<16;f=root.openNextFile()){
    String name=f.name();if(name.startsWith("/"))name=name.substring(1);
    const time_t w=f.getLastWrite();
    if(!f.isDirectory()&&isRawName(name)&&w>1700000000&&w<limit&&("/"+name)!=sessionName&&("/"+name)!=calName)doomed[n++]="/"+name;
    f.close();
  }
  root.close();
  for(uint8_t i=0;i<n;i++)SD.remove(doomed[i]);
}
void handleExportTrips(){
  uint32_t ids[MAX_TRIPS];uint8_t n;
  if(!parseIds(web.arg("ids"),ids,n)){web.send(400,"text/plain","No trips selected");return;}
  const int pi=portIndex();
  String s="trip_id,group,start_utc,end_utc,running_s,port_l,starboard_l,total_l,max_rpm,people,sea,wind,extras,note\n";
  for(uint8_t j=0;j<n;j++){
    const TripRec *r=tripById(ids[j]);
    if(!r)continue;
    const double a=tripLitres(*r,pi==1?1:0),b=tripLitres(*r,pi==1?0:1);
    char line[200];
    snprintf(line,sizeof(line),"%lu,%lu,%lld,%lld,%lu,%.2f,%.2f,%.2f,%u,%u,%u,%u,%u,",(unsigned long)r->id,(unsigned long)r->group,
      (long long)r->startUtc,(long long)r->endUtc,(unsigned long)r->anySec,a,b,a+b,(unsigned)r->maxRpm,
      (unsigned)r->people,(unsigned)r->sea,(unsigned)r->wind,(unsigned)r->extras);
    s+=line;
    String note=String(r->note);note.replace("\"","'");
    s+='"';s+=note;s+="\"\n";
  }
  ChunkOut &out=beginDownload("trip summary","text/csv","EngineLink-trips.csv");
  out.add(s);
  endDownload(out);
}
// Uptime (ms) of the raw row that starts at or after pos; false at the end.
bool rowAt(File &f,uint32_t pos,uint64_t &ms,uint32_t &lineStart){
  f.seek(pos);
  if(pos){int c;do{c=f.read();}while(c>=0&&c!='\n');}
  lineStart=f.position();
  char buf[64];uint8_t i=0;int c;uint8_t field=0;
  while((c=f.read())>=0&&c!='\n'){
    if(c==','){if(++field==2)break;i=0;continue;}
    if(field==1&&i<sizeof(buf)-1)buf[i++]=(char)c;
  }
  if(c<0&&field<2)return false;
  buf[i]=0;ms=strtoull(buf,nullptr,10);
  return true;
}
// Raw engine rows of the chosen trips, streamed from the session files.
void handleExportRaw(){
  uint32_t ids[MAX_TRIPS];uint8_t n;
  if(!parseIds(web.arg("ids"),ids,n)){web.send(400,"text/plain","No trips selected");return;}
  if(!sdReady){web.send(409,"text/plain","No SD card");return;}
  if(sdLogging)logFile.flush();
  ChunkOut &out=beginDownload("raw engine data","text/csv","EngineLink-raw-trips.csv");
  out.add("trip_id,boot_id,uptime_ms,utc_epoch,channel,mid,checksum,raw_hex,engine,fuel_lph,rpm,direction,kad_index,kad_value\n");
  char line[400];
  for(uint8_t j=0;j<n;j++){
    const TripRec *r=tripById(ids[j]);
    if(!r)continue;
    char name[25];snprintf(name,sizeof(name),"/EL%08lX.CSV",(unsigned long)r->bootId);
    File f=SD.open(name,FILE_READ);
    if(!f)continue;
    const uint64_t from=(uint64_t)r->startSec*1000ULL,to=(uint64_t)r->endSec*1000ULL+15000ULL;
    // Rows are in time order: binary search for the first row of the trip.
    const uint32_t tf=millis();
    uint32_t lo=0,hi=f.size();
    while(hi-lo>8192){
      const uint32_t mid=lo+(hi-lo)/2;uint64_t ms;uint32_t ls;
      if(!rowAt(f,mid,ms,ls)||ms>=from)hi=mid;else lo=mid;
    }
    f.seek(lo);
    {int c;if(lo==0){do{c=f.read();}while(c>=0&&c!='\n');}else{do{c=f.read();}while(c>=0&&c!='\n');}}
    dlStat.findMs+=millis()-tf;
    char prefix[16];const int pl=snprintf(prefix,sizeof(prefix),"%lu,",(unsigned long)r->id);
    // Read the card in blocks and send in large pieces: one small piece per
    // row made a chunk and a network packet each, about 1 kB/s.
    size_t ll=0;bool done=false;
    while(!done){
      const uint32_t tr=millis();
      const int got=f.read(rawIn,sizeof(rawIn));
      dlStat.sdMs+=millis()-tr;
      if(got<=0)break;
      for(int i=0;i<got&&!done;i++){
        const char ch=(char)rawIn[i];
        if(ch!='\n'){if(ll<sizeof(line)-1)line[ll++]=ch;continue;}
        if(ll&&line[ll-1]=='\r')--ll;
        line[ll]=0;
        const char *c1=ll?strchr(line,','):nullptr;
        if(c1){
          const uint64_t ms=strtoull(c1+1,nullptr,10);
          if(ms>to)done=true;
          else if(ms>=from)out.add(prefix,pl).add(line,ll).add("\n",1);
        }
        ll=0;
      }
    }
    f.close();
  }
  endDownload(out);
}
// The ordinary WebServer is single-threaded. Never send a speed-test payload
// from its request handler: on poor Wi-Fi the old 32 KiB request blocked every
// other API endpoint for 30+ seconds. Speed tests use port 82 independently.
void handleSpeedTest(){
  reply(false,"Speed test moved to HTTP port 82; refresh EngineLink's page",409);
}
// ---------- Backup ----------
void handleBackup(){
  String s="{\"app\":\"EngineLink\",\"version\":\"" EL_VERSION "\",\"settings\":{";
  s+="\"tank\":";jnum(s,cfg.tankL,0);s+=",\"reserve\":";s+=cfg.reserve;s+=",\"price\":";jnum(s,cfg.defaultPrice,2);
  s+=",\"tripEnd\":";s+=cfg.tripEndMin;s+=",\"autoPoll\":";s+=cfg.autoPoll?1:0;s+=",\"rawKeep\":";s+=cfg.rawKeepDays;
  s+=",\"ntp\":";s+=cfg.ntpOn?1:0;s+=",\"ntpServer\":";jsonString(s,cfg.ntpServer);
  s+=",\"host\":";jsonString(s,cfg.hostName);
  s+=",\"port\":";s+=cfg.portChannel;s+=",\"protocol\":";s+=cfg.protocol==PROTO_KAD?"\"kad\"":"\"j1587\"";
  s+=",\"fuelDiv\":";s+=cfg.kadFuelDiv;s+=",\"volt\":";s+=cfg.kadVolt;
  s+=",\"corr0\":";jnum(s,cfg.fuelCorr[0],4);s+=",\"corr1\":";jnum(s,cfg.fuelCorr[1],4);
  s+="},\"prefs\":";s+=cfg.prefs;s+='}';
  web.sendHeader("Content-Disposition","attachment; filename=EngineLink-settings.json");
  sendJSON(200,s);
}
