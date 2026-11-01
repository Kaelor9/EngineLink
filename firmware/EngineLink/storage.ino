// Storage: SD card, LittleFS files, NVS settings and totals, restart log,
// raw J1708 logging and trip/fuel/event files.

bool cardInserted(){return digitalRead(SD_DETECT)==LOW;}
// ---------- Persistent fuel totals ----------
void loadTotals(){
  pref.begin("totals",true);
  const size_t stored=pref.getBytesLength("t");
  if(stored==sizeof(Totals)){
    Totals t;
    if(pref.getBytes("t",&t,sizeof(t))==sizeof(t)&&t.magic==totals.magic)totals=t;
  }else if(stored==sizeof(TotalsV5)){
    // 0.5.x: same counters; fuel per band starts now, the tank level keeps its reference.
    TotalsV5 *o=new TotalsV5();
    if(pref.getBytes("t",o,sizeof(*o))==sizeof(*o)&&o->magic==0xE1170005){
      for(int c=0;c<2;c++){totals.lifetimeL[c]=o->lifetimeL[c];totals.tripL[c]=o->tripL[c];
        totals.runSeconds[c]=o->runSeconds[c];totals.tripRunSeconds[c]=o->tripRunSeconds[c];
        totals.lifeRawH[c]=o->lifeRawH[c];totals.tripRawH[c]=o->tripRawH[c];
        totals.tankRef.raw[c]=o->tankRef.raw[c];totals.tankRef.lit[c]=o->tankRef.lit[c];
        totals.lastEcuHours[c]=o->lastEcuHours[c];}
      totals.tripStartUtc=o->tripStartUtc;totals.tripResets=o->tripResets;
      totals.tripActive=o->tripActive;totals.tripBoot=o->tripBoot;totals.tripStartSec=o->tripStartSec;
      totals.tripAnySec=o->tripAnySec;totals.tripLastUtc=o->tripLastUtc;totals.tripMaxRpm=o->tripMaxRpm;
      totals.nextTripId=o->nextTripId;totals.tankRefL=o->tankRefL;totals.tankRef.missed=o->tankRef.missed;
      totals.estMissedL=o->estMissedL;totals.missedEvents=o->missedEvents;totals.seasonStartUtc=o->seasonStartUtc;
      memcpy(totals.bucketSec,o->bucketSec,sizeof(o->bucketSec));memcpy(totals.bucketL,o->bucketL,sizeof(o->bucketL));
      memcpy(totals.bucketSogSec,o->bucketSogSec,sizeof(o->bucketSogSec));memcpy(totals.bucketSogL,o->bucketSogL,sizeof(o->bucketSogL));
      memcpy(totals.bucketNm,o->bucketNm,sizeof(o->bucketNm));
      memcpy(totals.zoneL,o->zoneL,sizeof(o->zoneL));memcpy(totals.zoneSec,o->zoneSec,sizeof(o->zoneSec));memcpy(totals.zoneNm,o->zoneNm,sizeof(o->zoneNm));
      totals.tankRefVar=totals.tankRefL>=0?9.0:0; // unknown fill quality: about 3 L
    }
    delete o;
  }else if(stored==sizeof(TotalsV4)){
    // 0.4.x: keep every counter; the new fields start empty.
    TotalsV4 o;
    if(pref.getBytes("t",&o,sizeof(o))==sizeof(o)&&o.magic==0xE1170004){
      for(int c=0;c<2;c++){totals.lifetimeL[c]=o.lifetimeL[c];totals.tripL[c]=o.tripL[c];
        totals.runSeconds[c]=o.runSeconds[c];totals.tripRunSeconds[c]=o.tripRunSeconds[c];
        totals.lifeRawH[c]=o.lifeRawH[c];totals.tripRawH[c]=o.tripRawH[c];}
      totals.tripStartUtc=o.tripStartUtc;totals.tripResets=o.tripResets;
    }
  }else if(stored==sizeof(TotalsV3)){
    TotalsV3 o;
    if(pref.getBytes("t",&o,sizeof(o))==sizeof(o)&&o.magic==0xE1170003){
      for(int c=0;c<2;c++){totals.lifetimeL[c]=o.lifetimeL[c];totals.tripL[c]=o.tripL[c];
        totals.runSeconds[c]=o.runSeconds[c];totals.tripRunSeconds[c]=o.tripRunSeconds[c];}
      totals.tripStartUtc=o.tripStartUtc;totals.tripResets=o.tripResets;
    }
  }
  pref.end();
}
void restartLogBoot(){
  Preferences p;p.begin("rlog",false);
  p.getBytes("r",restartLog,sizeof(restartLog));
  for(int i=RESTART_LOG_N-1;i>0;i--)restartLog[i]=restartLog[i-1];
  RestartRec &r=restartLog[0];r=RestartRec();
  r.reason=(uint8_t)esp_reset_reason();r.upBefore=p.getUInt("alive",0);
  strlcpy(r.ver,EL_VERSION,sizeof(r.ver));
  p.putUInt("alive",0);
  p.putBytes("r",restartLog,sizeof(restartLog));
  p.end();
  restartUtcPending=true;
}
// The clock is set a little after start (phone, GPS or NTP): date the entry then.
void restartLogDate(){
  if(!restartUtcPending||!validWall())return;
  restartUtcPending=false;
  restartLog[0].utc=(uint32_t)(time(nullptr)-(time_t)(millis()/1000));
  Preferences p;p.begin("rlog",false);p.putBytes("r",restartLog,sizeof(restartLog));p.end();
}
void saveTotals(){
  if(lifeDirty)saveLife();
  pref.begin("totals",false);
  pref.putBytes("t",&totals,sizeof(totals));
  pref.end();
  {Preferences p;p.begin("rlog",false);p.putUInt("alive",(uint32_t)(millis()/1000));p.end();}
  totalsDirty=false;lastTotalsSave=millis();
}
void resetTrip(){
  for(int c=0;c<2;c++){totals.tripL[c]=0;totals.tripRawH[c]=0;totals.tripRunSeconds[c]=0;for(uint8_t b=0;b<FUEL_BANDS;b++)totals.tripRawB[c][b]=0;}
  totals.tripStartUtc=validWall()?(int64_t)time(nullptr):0;
  ++totals.tripResets;
  saveTotals();
}
void stopLog(){
  if(logFile){if(cardInserted())logFile.flush();logFile.close();}
  sdLogging=false;
}
void initSD(){
  if(!cardInserted()){sdReady=false;sdMessage="No card";return;}
  if(!spiStarted){SPI.begin(SD_SCK,SD_MISO,SD_MOSI,SD_CS);spiStarted=true;}
  sdReady=SD.begin(SD_CS,SPI,4000000);
  sdMessage=sdReady?"Card mounted":"Mount failed";
  if(!sdReady)sdFaultLatched=true;
  if(!sdReady)return;
  char name[25];
  snprintf(name,sizeof(name),"/EL%08lX.CSV",(unsigned long)bootId);
  sessionName=name;
  logFile=SD.open(sessionName,FILE_APPEND);
  if(!logFile){sdMessage="Cannot open session";sdFaultLatched=true;return;}
  if(logFile.size()==0){
    logFile.println("boot_id,uptime_ms,utc_epoch,channel,mid,checksum,raw_hex,engine,fuel_lph,rpm,direction,kad_index,kad_value");
  }
  sdLogging=true;
  sdMessage="Logging raw J1708";sdFaultLatched=false;
  lastFlush=millis();
}
void refreshTestAvailable(){
  testFileAvailable=sdReady&&cardInserted()&&calName.length()&&SD.exists(calName);
}
uint64_t sdTotalMB(){return sdReady?SD.totalBytes()/(1024ULL*1024ULL):0;}
uint64_t sdUsedMB(){return sdReady?SD.usedBytes()/(1024ULL*1024ULL):0;}
void serviceSD(){
  // Card detect is checked for every state, not only while recording, so a
  // removed card unmounts cleanly and a re-inserted card mounts again.
  if((uint32_t)(millis()-lastCardCheck)>=250){
    lastCardCheck=millis();
    const bool inserted=cardInserted();
    if(inserted!=cardWasInserted){cardWasInserted=inserted;cardChangeAt=millis();}
    if(!inserted&&(sdReady||sdLogging||calActive)){
      const bool wasRecording=sdLogging||calActive;
      if(calActive)finishCalibration(false);
      if(sdLogging)stopLog();
      SD.end();
      sdReady=false;testFileAvailable=false;
      if(wasRecording){sdFaultLatched=true;sdMessage="SD removed - recording stopped";}
      else sdMessage="No card";
      return;
    }
    // Debounced insertion: mount and resume automatic session logging.
    if(inserted&&!sdReady&&cardChangeAt&&(uint32_t)(millis()-cardChangeAt)>=1000&&!otaActive){
      cardChangeAt=0;
      initSD();
      refreshTestAvailable();
    }
  }
  // After a write failure with the card still in, try again: mount anew,
  // resume the raw log and the trip log. Every minute, then every 5 minutes.
  if(sdFaultLatched&&cardInserted()&&!calActive&&!otaActive&&(uint32_t)(millis()-lastSdRetry)>=(sdRetries<3?60000UL:300000UL)){
    lastSdRetry=millis();++sdRetries;
    if(sdLogging)stopLog();
    if(tripSeries)tripSeries.close();
    if(sdReady){SD.end();sdReady=false;}
    initSD();
    if(sdReady){
      tripSeriesReopen();refreshTestAvailable();
      if(!sdFaultLatched){++sdRecoveries;sdRetries=0;}
    }
  }
  if(sdLogging && (uint32_t)(millis()-lastFlush)>=CSV_FLUSH_MS){
    logFile.flush();lastFlush=millis();
  }
  if(calActive && (uint32_t)(millis()-lastTestFlush)>=CSV_FLUSH_MS){
    calFile.flush();lastTestFlush=millis();
  }
}
const char *frameDirection(const Decoded &d){return d.kadEcho?"ECHO":d.kadOwn?"TX":d.kadRequest?"REQ":"RX";}
void writeRecord(const Frame &f,const Decoded &d,bool valid,const char *hex){
  if(!sdLogging)return;
  int engineId = cfg.portChannel==0?-1:((f.channel+1)==cfg.portChannel?0:1);
  time_t utc=validWall()?time(nullptr):0;
  // Single short append; UART receiver tasks buffer independently.
  int result=logFile.printf("%08lX,%llu,%lld,J%u,%u,%u,%s,%s,",
      (unsigned long)bootId,(unsigned long long)f.bootMs,(long long)utc,
      f.channel==0?7:6,(unsigned)d.mid,valid?1:0,hex,
      engineId==0?"port":engineId==1?"starboard":"unassigned");
  if(result<=0){sdMessage="SD write failed";sdFaultLatched=true;++sdErrors;stopLog();return;}
  if(d.gotFuel)logFile.print(d.lph,3);
  logFile.print(',');
  if(d.gotRPM)logFile.print(d.rpm,2);
  logFile.printf(",%s,",frameDirection(d));
  if(d.kadIndex>=0)logFile.print(d.kadIndex);
  logFile.print(',');
  if(d.kadValueOk)logFile.print(d.kadValue);
  if(logFile.println()==0){sdMessage="SD write failed";sdFaultLatched=true;++sdErrors;stopLog();return;}
  ++sdRows;
}
bool fsError(const char *path,const char *reason){
  ++fsWriteErrors;
  snprintf(fsLastFailure,sizeof(fsLastFailure),"%s: %s",path,reason);
  Serial.printf("LittleFS write failed: %s\n",fsLastFailure);
  return false;
}
bool fsWrite(const char *path,const void *data,size_t len){
  if(!fsReady)return fsError(path,"not mounted");
  String tmp=String(path)+".tmp";
  File f=LittleFS.open(tmp,"w");
  if(!f)return fsError(path,"open temporary file");
  const bool ok=f.write((const uint8_t*)data,len)==len;
  f.close();
  if(!ok){LittleFS.remove(tmp);return fsError(path,"short write");}
  // Keep the last good copy until the new file has a durable name. A power
  // failure between the two renames can be recovered on the next boot.
  const String bak=String(path)+".bak";
  const bool hadOld=LittleFS.exists(path);
  if(hadOld){
    LittleFS.remove(bak);
    if(!LittleFS.rename(path,bak)){LittleFS.remove(tmp);return fsError(path,"backup rename");}
  }
  if(!LittleFS.rename(tmp,path)){
    if(hadOld)LittleFS.rename(bak,path);
    return fsError(path,"commit rename");
  }
  if(hadOld)LittleFS.remove(bak);
  return true;
}
void fsRecover(const char *path){
  if(!fsReady)return;
  const String bak=String(path)+".bak",tmp=String(path)+".tmp";
  if(!LittleFS.exists(path)){
    if(LittleFS.exists(bak)){
      if(LittleFS.rename(bak,path))++fsRecoveredFiles;
      else fsError(path,"recover backup");
    }else if(LittleFS.exists(tmp)){
      if(LittleFS.rename(tmp,path))++fsRecoveredFiles;
      else fsError(path,"recover temporary file");
    }
  }
  if(LittleFS.exists(path)){LittleFS.remove(bak);LittleFS.remove(tmp);}
}
// Reads a file of exactly len bytes, or of a whole number of items of size item.
size_t fsRead(const char *path,void *data,size_t maxLen,size_t item){
  if(!fsReady||!LittleFS.exists(path))return 0;
  File f=LittleFS.open(path,"r");
  if(!f)return 0;
  const size_t n=f.size();
  if(n>maxLen||n%item){f.close();return 0;}
  const size_t got=f.read((uint8_t*)data,n);
  f.close();
  return got==n?n:0;
}
bool fsWriteRecords(const char *path,uint32_t magic,const void *recs,uint16_t recSize,uint16_t count){
  const size_t len=sizeof(FileHead)+(size_t)recSize*count;
  uint8_t *buf=(uint8_t*)malloc(len);
  if(!buf)return false;
  FileHead h={magic,recSize,count};
  memcpy(buf,&h,sizeof(h));memcpy(buf+sizeof(h),recs,(size_t)recSize*count);
  const bool ok=fsWrite(path,buf,len);
  free(buf);return ok;
}
// Whole file into a new buffer (caller frees), or nullptr.
uint8_t *fsSlurp(const char *path,size_t &n,size_t maxLen){
  n=0;
  if(!fsReady||!LittleFS.exists(path))return nullptr;
  File f=LittleFS.open(path,"r");
  if(!f)return nullptr;
  n=f.size();
  if(!n||n>maxLen){f.close();n=0;return nullptr;}
  uint8_t *buf=(uint8_t*)malloc(n);
  if(buf&&f.read(buf,n)!=n){free(buf);buf=nullptr;}
  f.close();
  return buf;
}
void snapFromV1(FuelSnap &d,const FuelSnapV1 &s){d=FuelSnap();for(uint8_t c=0;c<2;c++){d.raw[c]=s.raw[c];d.lit[c]=s.lit[c];}d.missed=s.missed;}
void saveTrips(){fsWriteRecords("/trips.bin",TRIPS_MAGIC,trips,sizeof(TripRec),tripCount);}
void loadTrips(){
  size_t n;uint8_t *buf=fsSlurp("/trips.bin",n,sizeof(FileHead)+sizeof(trips)+4096);
  tripCount=0;
  if(!buf)return;
  FileHead h;memcpy(&h,buf,min(n,sizeof(h)));
  if(n>=sizeof(h)&&h.magic==TRIPS_MAGIC&&h.recSize==sizeof(TripRec)&&n==sizeof(h)+(size_t)h.count*sizeof(TripRec)){
    tripCount=min<uint16_t>(h.count,MAX_TRIPS);memcpy(trips,buf+sizeof(h),sizeof(TripRec)*tripCount);
  }else if(n%sizeof(TripRecV1)==0){
    const uint16_t c=min<size_t>(n/sizeof(TripRecV1),MAX_TRIPS);
    for(uint16_t i=0;i<c;i++){
      TripRecV1 o;memcpy(&o,buf+i*sizeof(o),sizeof(o));
      TripRec &r=trips[i];r=TripRec();
      r.id=o.id;r.group=o.group;r.startUtc=o.startUtc;r.endUtc=o.endUtc;r.anySec=o.anySec;
      r.bootId=o.bootId;r.startSec=o.startSec;r.endSec=o.endSec;
      for(uint8_t k=0;k<2;k++){r.rawH[k]=o.rawH[k];r.litJ[k]=o.litJ[k];}
      r.maxRpm=o.maxRpm;r.people=o.people;r.sea=o.sea;r.wind=o.wind;r.extras=o.extras;memcpy(r.note,o.note,sizeof(r.note));
    }
    tripCount=c;saveTrips();
  }
  free(buf);
}
void saveFuel(){fsWriteRecords("/fuel.bin",FUEL_MAGIC,fuelLog,sizeof(FuelRec),fuelCount);}
void loadFuel(){
  size_t n;uint8_t *buf=fsSlurp("/fuel.bin",n,sizeof(FileHead)+sizeof(fuelLog)+4096);
  fuelCount=0;
  if(!buf)return;
  FileHead h;memcpy(&h,buf,min(n,sizeof(h)));
  if(n>=sizeof(h)&&h.magic==FUEL_MAGIC&&h.recSize==sizeof(FuelRec)&&n==sizeof(h)+(size_t)h.count*sizeof(FuelRec)){
    fuelCount=min<uint16_t>(h.count,MAX_FUEL);memcpy(fuelLog,buf+sizeof(h),sizeof(FuelRec)*fuelCount);
  }else if(n%sizeof(FuelRecV1)==0){
    const uint16_t c=min<size_t>(n/sizeof(FuelRecV1),MAX_FUEL);
    for(uint16_t i=0;i<c;i++){
      FuelRecV1 o;memcpy(&o,buf+i*sizeof(o),sizeof(o));
      FuelRec &r=fuelLog[i];r=FuelRec();
      r.utc=o.utc;r.litres=o.litres;r.price=o.price;r.full=o.full;r.counted=o.counted;r.rawDelta=o.rawDelta;
      snapFromV1(r.at,o.at);r.prevRefL=o.prevRefL;snapFromV1(r.prevRef,o.prevRef);
      r.quality=o.full?1:0; // unknown: treated as "nozzle clicked off" until the owner says otherwise
    }
    fuelCount=c;saveFuel();
  }
  free(buf);
}
void saveHealth(){if(fsWrite("/health.bin",&health,sizeof(health)))healthDirty=false;}
void loadHealth(){
  Health h;
  if(fsRead("/health.bin",&h,sizeof(h),sizeof(h))==sizeof(h)&&h.magic==health.magic)health=h;
}
void saveDiag(){fsWrite("/diag.bin",diagTests,sizeof(diagTests));}
void loadDiag(){fsRead("/diag.bin",diagTests,sizeof(diagTests),sizeof(diagTests));}
void saveSettings(){
  pref.begin("enginelink",false);
  pref.putBool("done",cfg.completed);
  pref.putBool("router",cfg.router);
  pref.putBool("rhold",routerHold);
  pref.putBool("can",cfg.canListen);
  pref.putUChar("port",cfg.portChannel);
  pref.putUChar("apch",cfg.apChannel);
  pref.putString("appass",cfg.apPassword);
  pref.putString("ssid",cfg.ssid);
  pref.putString("wpass",cfg.wifiPassword);
  pref.putString("time",cfg.timePreference);
  pref.putUChar("proto",cfg.protocol);
  pref.putBool("kbe",cfg.kadBigEndian);
  pref.putUShort("kdiv",cfg.kadFuelDiv);
  pref.putUChar("kvolt",cfg.kadVolt);
  pref.putFloat("corr0",cfg.fuelCorr[0]);
  pref.putFloat("corr1",cfg.fuelCorr[1]);
  pref.putBytes("badj",cfg.bandAdj,sizeof(cfg.bandAdj));
  pref.putFloat("calsig",cfg.calSig);pref.putUChar("calfills",cfg.calFills);pref.putFloat("call",cfg.calLitres);
  {uint16_t g[4]={cfg.gearMaskF,cfg.gearValF,cfg.gearMaskR,cfg.gearValR};pref.putBytes("gear",g,sizeof(g));}
  pref.putString("apname",cfg.apName);
  pref.putString("host",cfg.hostName);
  pref.putFloat("tank",cfg.tankL);
  pref.putUChar("reserve",cfg.reserve);
  pref.putFloat("price",cfg.defaultPrice);
  pref.putUChar("tripend",cfg.tripEndMin);
  pref.putBool("autopoll",cfg.autoPoll);
  pref.putUShort("rawkeep",cfg.rawKeepDays);
  pref.putBool("ntp",cfg.ntpOn);
  pref.putString("ntpsrv",cfg.ntpServer);
  pref.putString("ghtok",cfg.ghToken);
  pref.putBool("erep",cfg.engReports);
  pref.putBool("scaleok",cfg.fuelScaleOk);
  pref.putBool("voltok",cfg.voltOk);
  pref.putUChar("tunes",cfg.fineTunes);
  pref.putFloat("tunedev",cfg.lastTuneDev);
  pref.end();
}
void savePrefs(){fsWrite("/prefs.json",cfg.prefs.c_str(),cfg.prefs.length());}
void loadPrefs(){
  if(!fsReady||!LittleFS.exists("/prefs.json"))return;
  File f=LittleFS.open("/prefs.json","r");
  if(f&&f.size()<=4000){String p=f.readString();if(p.length()>=2&&p[0]=='{')cfg.prefs=p;}
  if(f)f.close();
}
void loadSettings(){
  pref.begin("enginelink",true);
  cfg.completed=pref.getBool("done",false);
  cfg.router=pref.getBool("router",false);
  routerHold=pref.getBool("rhold",false);
  {Preferences sp;if(sp.begin("enginelink",true)){staSecMode=sp.getUChar("smode",0)?1:0;sp.end();}}
  if(routerHold)routerHoldSince=1;
  routerProbeAt=120000UL;  // after a start in standalone, first look after 2 minutes
  routerFailureReason=pref.getUChar("rrsn",0);
  routerFailurePhase=pref.getUChar("rstage",0);
  routerFailureAfterMs=pref.getULong("rduration",0);
  cfg.canListen=pref.getBool("can",false);
  cfg.portChannel=pref.getUChar("port",1);
  cfg.apChannel=pref.getUChar("apch",6);
  txLevel=pref.getUChar("txal",3);
  // Settings from earlier Wi-Fi experiments that no longer exist.
  {Preferences old;if(old.begin("enginelink",false)){for(const char *k:{"txp","txp2","txp3","aggr","aggr2","agrx","agrxt"})if(old.isKey(k))old.remove(k);old.end();}}
  cfg.apPassword=pref.getString("appass","12345678");
  cfg.ssid=pref.getString("ssid","");
  cfg.wifiPassword=pref.getString("wpass","");
  cfg.timePreference=pref.getString("time","auto");
  cfg.protocol=pref.getUChar("proto",PROTO_KAD);
  cfg.kadBigEndian=pref.getBool("kbe",true);
  cfg.kadFuelDiv=pref.getUShort("kdiv",128);
  cfg.kadVolt=pref.getUChar("kvolt",0);
  cfg.fuelCorr[0]=pref.getFloat("corr0",1.0f);
  if(pref.getBytesLength("badj")==sizeof(cfg.bandAdj))pref.getBytes("badj",cfg.bandAdj,sizeof(cfg.bandAdj));
  for(uint8_t b=0;b<3;b++)if(!(cfg.bandAdj[b]>=0.7f&&cfg.bandAdj[b]<=1.4f))cfg.bandAdj[b]=1.0f;
  cfg.calSig=pref.getFloat("calsig",0);cfg.calFills=pref.getUChar("calfills",0);cfg.calLitres=pref.getFloat("call",0);
  {uint16_t g[4]={0,0,0,0};if(pref.getBytesLength("gear")==sizeof(g))pref.getBytes("gear",g,sizeof(g));
   cfg.gearMaskF=g[0];cfg.gearValF=g[1];cfg.gearMaskR=g[2];cfg.gearValR=g[3];}
  cfg.fuelCorr[1]=pref.getFloat("corr1",1.0f);
  cfg.apName=pref.getString("apname","EngineLink");
  cfg.hostName=pref.getString("host","enginelink");
  if(!validHostName(cfg.hostName))cfg.hostName="enginelink";
  cfg.tankL=pref.getFloat("tank",0);
  cfg.reserve=pref.getUChar("reserve",10);
  cfg.defaultPrice=pref.getFloat("price",0);
  cfg.tripEndMin=pref.getUChar("tripend",2);
  cfg.autoPoll=pref.getBool("autopoll",false);
  cfg.rawKeepDays=pref.getUShort("rawkeep",90);
  cfg.ntpOn=pref.getBool("ntp",true);
  cfg.ntpServer=pref.getString("ntpsrv","pool.ntp.org");
  cfg.ghToken=pref.getString("ghtok","");
  cfg.engReports=pref.getBool("erep",true);
  engineReportTrip=pref.getUInt("erpend",0);
  cfg.fuelScaleOk=pref.getBool("scaleok",false);
  cfg.voltOk=pref.getBool("voltok",false);
  cfg.fineTunes=pref.getUChar("tunes",0);
  cfg.lastTuneDev=pref.getFloat("tunedev",0);
  pref.end();
  if(!cfg.apName.length()||cfg.apName.length()>31)cfg.apName="EngineLink";
  if(!(cfg.tankL>=0&&cfg.tankL<=10000))cfg.tankL=0;
  if(cfg.reserve>50)cfg.reserve=10;
  if(!(cfg.defaultPrice>=0&&cfg.defaultPrice<1000))cfg.defaultPrice=0;
  if(cfg.tripEndMin<1||cfg.tripEndMin>60)cfg.tripEndMin=2;
  if(!cfg.ntpServer.length())cfg.ntpServer="pool.ntp.org";
  if(cfg.protocol>1)cfg.protocol=PROTO_KAD;
  if(cfg.kadFuelDiv!=128&&cfg.kadFuelDiv!=256)cfg.kadFuelDiv=128;
  for(int c=0;c<2;c++)if(!(cfg.fuelCorr[c]>=0.5f&&cfg.fuelCorr[c]<=2.0f))cfg.fuelCorr[c]=1.0f;
  if(cfg.apPassword.length()<8)cfg.apPassword="12345678";
  if(cfg.portChannel>2)cfg.portChannel=0;
  if(cfg.apChannel<1||cfg.apChannel>13)cfg.apChannel=6;
  if(txLevel>4)txLevel=3;
}
