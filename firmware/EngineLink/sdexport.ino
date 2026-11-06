// SD card info, clean-up and CSV exports, and the settings backup.

// ---------- SD card: info, wipe, clean-up, export ----------
bool isRawName(const String &n){return n.startsWith("EL")||n.startsWith("TEST")||(n.startsWith("T")&&n.endsWith(".CSV")&&n!="TRIPS.CSV");}
// ---- One walk through the root directory, a few entries per loop pass ----
// Every open by name searches the whole directory, so on a card with many
// logs each entry costs 0.1-0.4 s: counting in one go held the engine lock
// for 7.3 s (/api/sd/info, plan 13.6.1) and four entries per pass up to
// 485 ms. Each card access now runs with the lock let go (sdGapBegin); the
// results are written with it held. The same walk counts for /api/sd/info
// and, once a day, finds raw logs older than cfg.rawKeepDays.
static File walkRoot;
static bool walkOn=false,walkClean=false,walkRemoving=false,sdCountWanted=false,sdKnown=false;
static SdCounts walkK,sdK;               // being counted; last finished count
static uint64_t sdUsedB=0,sdTotalB=0;
static uint32_t sdCountedAt=0;
static String doomed[16];
static uint8_t nDoomed=0,nRemoved=0;
static time_t walkLimit=0;
// Before any unmount (the loop, or a handler while the loop waits for the lock).
void sdWalkStop(){
  if(walkRoot)walkRoot.close();
  walkOn=walkRemoving=false;nDoomed=nRemoved=0;sdKnown=false;
  for(String &d:doomed)d=String();
}
static bool walkCardGone(){return !sdReady||!cardInserted()||calActive||otaActive;}
void serviceSdWalk(){
  if(walkRemoving){
    if(walkCardGone()){sdWalkStop();return;}
    if(nRemoved>=nDoomed){walkRemoving=false;nDoomed=nRemoved=0;sdCountedAt=0;return;}
    if(webXferBusy())return;   // a download may be reading one of them
    const String path=doomed[nRemoved];doomed[nRemoved++]=String();
    if(path==sessionName||path==calName)return;
    const bool gap1=sdGapBegin();SD.remove(path);sdGapEnd(gap1);
    return;
  }
  if(walkOn){
    if(walkCardGone()){sdWalkStop();return;}
    for(uint8_t k=0;k<4;k++){
      String name;bool dir=false;size_t size=0;time_t w=0;
      const bool gap2=sdGapBegin();
      File f=walkRoot.openNextFile();
      const bool got=(bool)f;
      if(got){name=f.name();dir=f.isDirectory();size=f.size();w=f.getLastWrite();f.close();}
      sdGapEnd(gap2);
      if(!got)break;
      if(name.startsWith("/"))name=name.substring(1);
      ++walkK.files;
      if(!dir&&isRawName(name)){
        walkK.rawBytes+=size;++walkK.rawFiles;
        if(walkClean&&nDoomed<16&&w>1700000000&&w<walkLimit&&("/"+name)!=sessionName&&("/"+name)!=calName)doomed[nDoomed++]="/"+name;
      }else if(!dir&&name.startsWith("EXPORT"))++walkK.exports;
      if(k==3)return;   // more next pass
    }
    // End of the directory (or a read error: a removed card is noticed above).
    walkRoot.close();
    uint64_t used=0,total=0;
    const bool gap3=sdGapBegin();
    if(cardInserted()){total=SD.totalBytes();used=SD.usedBytes();}   // FAT free count: can be slow once after a mount
    sdGapEnd(gap3);
    walkOn=false;
    if(walkCardGone()){sdWalkStop();return;}
    sdK=walkK;sdUsedB=used;sdTotalB=total;sdKnown=true;sdCountedAt=millis()?millis():1;
    walkRemoving=walkClean&&nDoomed>0;nRemoved=0;
    return;
  }
  if(walkCardGone())return;
  bool clean=false;
  if(cfg.rawKeepDays&&validWall()&&(lastRawCleanup?(uint32_t)(millis()-lastRawCleanup)>=86400000UL:millis()>=120000)){
    clean=true;lastRawCleanup=millis();
  }
  if(!clean&&!sdCountWanted)return;
  sdCountWanted=false;
  walkClean=clean;walkLimit=time(nullptr)-(time_t)cfg.rawKeepDays*86400;
  walkK=SdCounts();nDoomed=nRemoved=0;
  const bool gap4=sdGapBegin();
  walkRoot=SD.open("/");
  sdGapEnd(gap4);
  walkOn=(bool)walkRoot;
}
// Never touches the card: the last finished count, and a new one asked from
// the loop when it is older than 10 s. "counting" tells the app to ask again.
void handleSdInfo(){
  const bool fresh=sdKnown&&sdCountedAt&&(uint32_t)(millis()-sdCountedAt)<10000UL;
  if(sdReady&&!fresh&&!walkOn&&!walkRemoving)sdCountWanted=true;
  const bool counting=sdReady&&(walkOn||sdCountWanted)&&!calActive&&!otaActive;
  String s="{\"ok\":";s+=sdReady?"true":"false";
  s+=",\"inserted\":";s+=cardInserted()?"true":"false";
  s+=",\"logging\":";s+=sdLogging?"true":"false";
  s+=",\"known\":";s+=sdReady&&sdKnown?"true":"false";
  s+=",\"counting\":";s+=counting?"true":"false";
  s+=",\"usedMB\":";s+=(unsigned long)(sdUsedB/1048576ULL);s+=",\"totalMB\":";s+=(unsigned long)(sdTotalB/1048576ULL);
  s+=",\"files\":";s+=sdK.files;s+=",\"rawMB\":";jnum(s,sdK.rawBytes/1048576.0,1);s+=",\"rawFiles\":";s+=sdK.rawFiles;
  s+=",\"exports\":";s+=sdK.exports;
  s+=",\"trips\":";s+=tripCount;s+=",\"fuel\":";s+=fuelCount;
  s+=",\"status\":";jsonString(s,sdMessage);
  s+='}';
  sendJSON(200,s);
}
// Wipe and factory reset (a web handler holding the engine lock): every card
// access with the lock let go, and a short sleep every 16 files so the main
// loop on the same core gets its turn and feeds its watchdog.
uint32_t removeTree(const String &path){
  uint32_t n=0;
  const bool gap5=sdGapBegin();
  File dir=SD.open(path);
  const bool isDir=dir&&dir.isDirectory();
  if(dir&&!isDir){dir.close();n=SD.remove(path)?1:0;}
  sdGapEnd(gap5);
  if(!isDir)return n;
  static uint8_t breath=0;
  for(;;){
    const bool gap6=sdGapBegin();
    File f=dir.openNextFile();
    String child;bool sub=false;
    if(f){child=String(f.path());sub=f.isDirectory();f.close();if(!sub&&SD.remove(child))++n;}
    if(!(++breath&15))vTaskDelay(pdMS_TO_TICKS(20));
    sdGapEnd(gap6);
    if(!child.length())break;
    if(sub){n+=removeTree(child);const bool gap7=sdGapBegin();SD.rmdir(child);sdGapEnd(gap7);}
  }
  const bool gap8=sdGapBegin();dir.close();sdGapEnd(gap8);
  return n;
}
void handleSdWipe(){
  if(web.arg("confirm")!="WIPE"){reply(false,"Type WIPE to confirm",400);return;}
  if(!sdReady||!cardInserted()){reply(false,"No SD card",409);return;}
  if(webXferBusy()){reply(false,"A download is running; try again when it has finished",409);return;}
  if(sdOffLock){reply(false,"The SD card is busy; try again in a moment",409);return;}
  sdWalkStop();
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
    while(!done&&!web.sendFailed()){
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
// Download speed test (?kb=16..256, default 32), measured by the phone. It
// runs in the xfer task (httpd.ino) and touches no engine data or SD card,
// so every other request is answered meanwhile.
void handleSpeedTest(){
  size_t total=32768;
  if(web.hasArg("kb")){const long kb=web.arg("kb").toInt();if(kb>=16&&kb<=256)total=(size_t)kb*1024;}
  static const char block[4096]={'E','n','g','i','n','e','L','i','n','k'};   // in flash, not RAM
  web.sendHeader("Cache-Control","no-store");
  web.setContentLength(CONTENT_LENGTH_UNKNOWN);
  web.send(200,"application/octet-stream","");
  const uint32_t begun=millis();size_t sent=0;uint32_t writes=0;
  while(sent<total&&!web.sendFailed()){
    const size_t n=min(sizeof(block),total-sent);
    web.sendContent(block,n);++writes;
    if(!web.sendFailed())sent+=n;
  }
  web.sendContent("");
  // Server write time for the report; the browser measures the real speed.
  speedTxLastBytes=(uint32_t)sent;speedTxLastWrites=writes;speedTxLastAt=millis();speedTxLastMs=millis()-begun;
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
