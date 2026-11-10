// Gear and clutch (0.14.0): both engines four times a second, every shift at
// idle timed (shiftrules.h), the compressor on accelerations, warm-up time and
// charging voltage per engine, and the recordings behind them.
//
// Everything here runs in the engine task (or the loop without it) and in web
// handlers, both under engineMx.

// ---------- Gear from the learned bits ----------
uint8_t gearNow(uint8_t c,uint32_t &ageMs){
  ageMs=0xFFFFFFFF;
  if(c>1||cfg.protocol!=PROTO_KAD)return shift::G_UNKNOWN;
  for(uint8_t i=0;i<KAD_N;i++){
    if(KAD_PARAMS[i].index!=264)continue;
    const uint64_t now=elapsedMs();
    if(!kadRawAt[c][i]||now<kadRawAt[c][i]||now-kadRawAt[c][i]>15000)return shift::G_UNKNOWN;
    ageMs=(uint32_t)(now-kadRawAt[c][i]);
    return shift::gearFromBits(kadRawV[c][i]&0xFFFF,cfg.gearMaskF,cfg.gearValF,cfg.gearMaskR,cfg.gearValR);
  }
  return shift::G_UNKNOWN;
}
// The compressor clutch bit: -1 not learned or not read, 0 off, 1 on.
int8_t compNow(uint8_t c){
  if(!cfg.compMask)return -1;
  float v;
  if(!engineValueAge(c,264,v,3000))return -1;
  return (((uint16_t)v)&cfg.compMask)==cfg.compVal?1:0;
}
bool gearLearned(){return cfg.gearMaskF||cfg.gearMaskR;}
const char *gearCode(uint8_t g){return g==shift::G_NEUTRAL?"N":g==shift::G_FWD?"F":g==shift::G_REV?"R":g==shift::G_IN?"G":"?";}

// ---------- Storage ----------
void saveShiftLife(){if(fsWrite("/shiftlife.bin",&shiftLife,sizeof(shiftLife)))shiftLifeDirty=false;}
void loadShiftLife(){ShiftLife l;if(fsRead("/shiftlife.bin",&l,sizeof(l),sizeof(l))==sizeof(l)&&l.magic==shiftLife.magic)shiftLife=l;}
void saveShifts(){if(fsWriteRecords("/shifts.bin",0x31544853,shiftLog,sizeof(ShiftRec),shiftCount)){shiftLogDirty=false;shiftLogSavedAt=millis();}}
void loadShifts(){
  size_t n;uint8_t *buf=fsSlurp("/shifts.bin",n,sizeof(FileHead)+sizeof(shiftLog)+64);
  shiftCount=0;
  if(!buf)return;
  FileHead h;memcpy(&h,buf,min(n,sizeof(h)));
  if(n>=sizeof(h)&&h.magic==0x31544853&&h.recSize==sizeof(ShiftRec)&&n==sizeof(h)+(size_t)h.count*sizeof(ShiftRec)){
    shiftCount=min<uint16_t>(h.count,SHIFT_N);memcpy(shiftLog,buf+sizeof(h),sizeof(ShiftRec)*shiftCount);
  }
  elPsFree(buf);
  // Restored engine knowledge, so the first shifts after a start are judged right.
  for(uint8_t c=0;c<2;c++){if(isfinite(shiftLife.idleLph[c]))shiftTr[c].base=shiftLife.idleLph[c];if(isfinite(shiftLife.idleRpm[c]))shiftTr[c].baseRpm=shiftLife.idleRpm[c];}
}
// ---------- One shift ----------
float slowAvg(float avg,float x,float k){return isfinite(avg)?avg+k*(x-avg):x;}
void recordShift(uint8_t c,const shift::Shift &s){
  ShiftRec r;
  r.utc=validWall()?(uint32_t)time(nullptr):0;r.up=(uint32_t)(elapsedMs()/1000ULL);
  r.eng=c;r.kind=s.kind;r.gear=s.gear;r.flags=s.flags;
  r.lagMs=s.lagMs<0?-1:(int16_t)min<int32_t>(s.lagMs,32000);
  r.rpm=isfinite(s.rpm)?(uint16_t)constrain(lroundf(s.rpm),0,9000):0;
  if(shiftCount==SHIFT_N)--shiftCount;
  memmove(&shiftLog[1],&shiftLog[0],sizeof(ShiftRec)*shiftCount);
  shiftLog[0]=r;++shiftCount;shiftLogDirty=true;
  ShiftLife &L=shiftLife;
  if(s.kind==shift::K_ENGAGE){
    ++L.engages[c];
    if(s.flags&shift::F_QUICK)++L.quick[c];
    if(s.lagMs>=0){L.engAvg[c]=slowAvg(L.engAvg[c],s.lagMs,0.1f);if(L.engN[c]<20)L.engFirst[c]=slowAvg(L.engFirst[c],s.lagMs,1.0f/(L.engN[c]+1));if(L.engN[c]<65535)++L.engN[c];}
  }else{
    ++L.disengages[c];
    if(s.lagMs>=0){L.disAvg[c]=slowAvg(L.disAvg[c],s.lagMs,0.1f);if(L.disN[c]<20)L.disFirst[c]=slowAvg(L.disFirst[c],s.lagMs,1.0f/(L.disN[c]+1));if(L.disN[c]<65535)++L.disN[c];}
  }
  shiftLifeDirty=true;
}
void shiftEvent(uint8_t c,const shift::Ev &e,uint32_t up){
  ShiftLife &L=shiftLife;
  diag::Event d;d.eng=c;d.value=e.value;d.ref=e.ref;
  if(e.type==shift::E_PULL_NEUTRAL){
    ++L.pullNeutral[c];shiftLifeDirty=true;
    // Every time: this is the one that matters (at most one a minute per engine).
    if(lastPullEventMs[c]&&(uint32_t)(millis()-lastPullEventMs[c])<60000)return;
    lastPullEventMs[c]=millis();
    d.type=diag::T_PULL_NEUTRAL;d.sev=diag::SEVERITY[diag::T_PULL_NEUTRAL];
  }else{
    ++L.hard[c];if((int)e.ref==2)++L.highRpm[c];shiftLifeDirty=true;
    // Each one is counted; the list gets one every 10 minutes per engine.
    if(lastHardEventMs[c]&&(uint32_t)(millis()-lastHardEventMs[c])<600000UL)return;
    lastHardEventMs[c]=millis();
    d.type=diag::T_HARD_SHIFT;d.sev=diag::SEVERITY[diag::T_HARD_SHIFT];
  }
  addEvent(d,up);
}

// ---------- Warm-up and charging ----------
void warmAndVolt(uint8_t c,uint32_t up){
  ShiftLife &L=shiftLife;
  if(!motor[c].running){warmTiming[c]=false;return;}
  float cool,volt;
  const bool hasCool=engineValueAge(c,259,cool,20000);
  if(!warmTiming[c]&&lifeStartAt[c]&&up-lifeStartAt[c]<=30&&hasCool&&cool<40){warmTiming[c]=true;warmStartUp[c]=lifeStartAt[c];}
  if(warmTiming[c]&&hasCool&&cool>=60){
    warmTiming[c]=false;
    const float s=(float)(up-warmStartUp[c]);
    if(s>30&&s<7200){L.warmLast[c]=s;L.warmAvg[c]=slowAvg(L.warmAvg[c],s,L.warmN[c]<10?1.0f/(L.warmN[c]+1):0.1f);if(L.warmN[c]<65535)++L.warmN[c];shiftLifeDirty=true;}
  }
  // Charging: only after the first minute, when the alternator has taken over.
  if(up-lifeStartAt[c]>60&&engineValueAge(c,293,volt,25000)&&volt>6){
    L.voltRun[c]=slowAvg(L.voltRun[c],volt,0.002f);
    if(!isfinite(L.voltMin[c])||volt<L.voltMin[c])L.voltMin[c]=volt;
    shiftLifeDirty=true;
  }
}

// ---------- Four readings a second ----------
void fastPush(const FastSample &f){
  if(!fastRing)return;
  fastRing[fastHead]=f;fastHead=(fastHead+1)%FAST_N;if(fastCount<FAST_N)fastCount++;
}
// Samples between two uptimes (ms), oldest first.
uint16_t fastCopy(uint32_t from,uint32_t to,FastSample *out,uint16_t max){
  uint16_t n=0;
  if(!fastRing)return 0;
  for(uint16_t i=0;i<fastCount&&n<max;i++){
    const FastSample &s=fastRing[(fastHead+FAST_N-fastCount+i)%FAST_N];
    if(s.ms>=from&&s.ms<=to)out[n++]=s;
  }
  return n;
}
void tripFastOpen(uint32_t id){
  if(tripFast)tripFast.close();
  fastBufN=0;tripFastId=0;
  if(!sdReady||!cardInserted()||!ensureDir("/TRIPS"))return;
  char name[32];snprintf(name,sizeof(name),"/TRIPS/T%06lu.FST",(unsigned long)id);
  const bool exists=SD.exists(name);
  tripFast=SD.open(name,exists?FILE_APPEND:FILE_WRITE);
  if(!tripFast)return;
  if(!exists){SeriesHead h={FAST_MAGIC,id,bootId,(uint32_t)elapsedMs(),totals.tripStartUtc,(int8_t)portIndex(),{0,0,0}};tripFast.write((const uint8_t*)&h,sizeof(h));}
  tripFastId=id;
}
void tripFastFlush(){
  if(tripFast&&fastBufN){tripFast.write((const uint8_t*)fastBuf,sizeof(FastSample)*fastBufN);tripFast.flush();}
  fastBufN=0;
}
void tripFastClose(bool keep){
  if(!tripFast){tripFastId=0;return;}
  tripFastFlush();tripFast.close();
  if(!keep&&sdReady&&cardInserted()){char name[32];snprintf(name,sizeof(name),"/TRIPS/T%06lu.FST",(unsigned long)tripFastId);SD.remove(name);}
  tripFastId=0;
}
// The minute before and after an event, four readings a second.
void writeFastBox(uint32_t id,uint32_t atMs){
  if(!fastRing||!sdReady||!cardInserted()||!ensureDir("/EVENTS"))return;
  const uint16_t want=(uint16_t)(2*FAST_BOX_MS/FAST_MS+8);
  FastSample *buf=(FastSample*)elPsAlloc(sizeof(FastSample)*want);
  if(!buf)return;
  const uint16_t n=fastCopy(atMs>FAST_BOX_MS?atMs-FAST_BOX_MS:0,atMs+FAST_BOX_MS,buf,want);
  char name[32];snprintf(name,sizeof(name),"/EVENTS/E%06lu.FST",(unsigned long)id);
  File f=SD.open(name,FILE_WRITE);
  if(f){
    const int64_t utc0=validWall()&&n?(int64_t)time(nullptr)-(int64_t)((elapsedMs()-buf[0].ms)/1000ULL):0;
    SeriesHead h={FAST_MAGIC,id,bootId,n?buf[0].ms:atMs,utc0,(int8_t)portIndex(),{0,0,0}};
    f.write((const uint8_t*)&h,sizeof(h));f.write((const uint8_t*)buf,sizeof(FastSample)*n);f.close();
  }
  elPsFree(buf);
}
// Events wait a minute for the "after" part; called from the fast loop.
void serviceFastBoxes(uint32_t nowMs){
  for(uint8_t k=0;k<pendingFastBoxN;){
    EventRec *e=nullptr;
    for(uint8_t i=0;i<eventCount;i++)if(events[i].id==pendingFastBox[k]){e=&events[i];break;}
    if(!e||e->boot!=bootId){pendingFastBox[k]=pendingFastBox[--pendingFastBoxN];continue;}
    const uint32_t at=e->up*1000UL;
    if(nowMs>=at+FAST_BOX_MS){writeFastBox(e->id,at);pendingFastBox[k]=pendingFastBox[--pendingFastBoxN];continue;}
    k++;
  }
}
void queueFastBox(uint32_t id){if(pendingFastBoxN<8)pendingFastBox[pendingFastBoxN++]=id;}
int16_t fast16(float v,float k){return isfinite(v)?(int16_t)constrain(lroundf(v*k),-32767L,32767L):NA16;}
void serviceFast(){
  const uint32_t ms=(uint32_t)elapsedMs();
  if(lastFastMs&&ms-lastFastMs<FAST_MS)return;
  lastFastMs=lastFastMs&&ms-lastFastMs<2*FAST_MS?lastFastMs+FAST_MS:ms;
  const uint64_t now=elapsedMs();
  const uint32_t up=(uint32_t)(now/1000ULL);
  FastSample f;f.ms=ms;
  for(uint8_t c=0;c<2;c++){
    shift::In in;in.ms=ms;in.running=motor[c].running;
    // Only values younger than 1.5 s: older ones would blur the timing.
    in.rpm=motor[c].rpmEver&&now>=motor[c].rpmAt&&now-motor[c].rpmAt<1500?motor[c].rpm:NAN;
    in.lph=motor[c].fuelEver&&now>=motor[c].fuelAt&&now-motor[c].fuelAt<1500?motor[c].fuelLph:NAN;
    float v;in.req=engineValueAge(c,291,v,6000)?v:NAN;
    in.gear=gearNow(c,in.gearAgeMs);
    shift::Shift sh[2];shift::Ev ev[2];int ns=0,ne=0;
    shiftTr[c].step(in,sh,ns,ev,ne);
    for(int i=0;i<ns;i++)recordShift(c,sh[i]);
    for(int i=0;i<ne;i++)shiftEvent(c,ev[i],up);
    const int8_t comp=compNow(c);
    float boost=NAN;if(engineValueAge(c,265,v,3000))boost=v;
    if(accelTr[c].step(ms,in.rpm,boost,comp)){
      ShiftLife &L=shiftLife;
      ++L.accels[c];if(accelTr[c].compSeen)++L.compOn[c];
      if(isfinite(accelTr[c].boostPeak))L.boostPeak[c]=slowAvg(L.boostPeak[c],accelTr[c].boostPeak,0.2f);
      shiftLifeDirty=true;
    }
    if(isfinite(shiftTr[c].base))shiftLife.idleLph[c]=shiftTr[c].base;
    if(isfinite(shiftTr[c].baseRpm))shiftLife.idleRpm[c]=shiftTr[c].baseRpm;
    f.rpm[c]=fast16(in.rpm,1);f.lph100[c]=fast16(in.lph,100);f.req[c]=fast16(in.req,1);f.boost10[c]=fast16(boost,10);
    f.st264[c]=engineValueAge(c,264,v,3000)?(uint16_t)v:0xFFFF;
    f.gear[c]=in.gearAgeMs<=shift::GEAR_FRESH_MS?in.gear:shift::G_UNKNOWN;
    f.flags[c]=(shiftTr[c].loaded?FF_LOAD:0)|(shiftTr[c].known?FF_LOADKNOWN:0)|(comp>0?FF_COMP:0)|(comp>=0?FF_COMPKNOWN:0);
  }
  fastPush(f);
  if(totals.tripActive&&tripFast){
    fastBuf[fastBufN++]=f;
    if(fastBufN>=sizeof(fastBuf)/sizeof(fastBuf[0]))tripFastFlush();
  }
  serviceFastBoxes(ms);
  if(ms/1000!=(ms-FAST_MS)/1000)for(uint8_t c=0;c<2;c++)warmAndVolt(c,up);
}
// A new trip: the lowest voltage is per trip.
void shiftTripStart(){for(uint8_t c=0;c<2;c++)shiftLife.voltMin[c]=NAN;}

// ---------- Web ----------
// Fast samples as JSON columns.
void sendFast(const FastSample *buf,uint16_t n,uint32_t ms0,int64_t utc0,uint32_t nowMs){
  web.setContentLength(CONTENT_LENGTH_UNKNOWN);
  web.send(200,"application/json","");
  String s;s.reserve(4400);
  s="{\"ms0\":";s+=ms0;s+=",\"utc0\":";s+=(long long)utc0;s+=",\"now\":";s+=nowMs;s+=",\"port\":";s+=portIndex();
  s+=",\"gl\":";s+=gearLearned()?"true":"false";s+=",\"cl\":";s+=cfg.compMask?"true":"false";
  s+=",\"t\":[";
  for(uint16_t i=0;i<n;i++){if(i)s+=',';s+=(long)(buf[i].ms-ms0);if(s.length()>4000){web.sendContent(s);s="";}}
  s+="]";
  auto col=[&](const char *name,int k){
    s+=",\"";s+=name;s+="\":[";
    for(uint8_t c=0;c<2;c++){
      s+=c?",[":"[";
      for(uint16_t i=0;i<n;i++){
        if(i)s+=',';
        const FastSample &x=buf[i];
        switch(k){
          case 0:if(x.rpm[c]==NA16)s+="null";else s+=x.rpm[c];break;
          case 1:if(x.lph100[c]==NA16)s+="null";else s+=String(x.lph100[c]/100.0f,2);break;
          case 2:if(x.req[c]==NA16)s+="null";else s+=x.req[c];break;
          case 3:if(x.boost10[c]==NA16)s+="null";else s+=String(x.boost10[c]/10.0f,1);break;
          case 4:if(x.st264[c]==0xFFFF)s+="null";else s+=x.st264[c];break;
          case 5:s+=x.gear[c];break;
          case 6:s+=x.flags[c];break;
        }
        if(s.length()>4000){web.sendContent(s);s="";}
      }
      s+="]";
    }
    s+="]";
  };
  col("rpm",0);col("lph",1);col("req",2);col("boost",3);col("st",4);col("gear",5);col("fl",6);
  s+="}";
  web.sendContent(s);web.sendContent("");
}
// The last seconds (?secs=, at most 120) or everything after ?since= (uptime ms).
void handleFastRecent(){
  if(!fastRing){reply(false,"No recording",404);return;}
  const uint32_t nowMs=(uint32_t)elapsedMs();
  uint32_t from;
  if(web.hasArg("since"))from=(uint32_t)strtoul(web.arg("since").c_str(),nullptr,10)+1;
  else{const uint32_t secs=constrain(web.arg("secs").toInt(),5,120);from=nowMs>secs*1000UL?nowMs-secs*1000UL:0;}
  if(from>nowMs)from=nowMs;
  if(nowMs-from>120000UL)from=nowMs-120000UL;
  const uint16_t want=480;
  FastSample *buf=(FastSample*)elPsAlloc(sizeof(FastSample)*want);
  if(!buf){reply(false,"Out of memory",500);return;}
  const uint16_t n=fastCopy(from,nowMs,buf,want);
  const int64_t utc0=validWall()&&n?(int64_t)time(nullptr)-(int64_t)((nowMs-buf[0].ms)/1000UL):0;
  sendFast(buf,n,n?buf[0].ms:nowMs,utc0,nowMs);
  elPsFree(buf);
}
// Reads a fast file; ?from=&to= seconds from its start pick a window, at most
// 1200 samples evenly spread.
void sendFastFile(const char *path){
  if(!sdReady||!cardInserted()||!SD.exists(path)){reply(false,"No recording for this",404);return;}
  if(tripFast&&tripFastId)tripFastFlush();
  File f=SD.open(path,FILE_READ);
  if(!f){reply(false,"Cannot open the recording",500);return;}
  SeriesHead h;
  if(f.read((uint8_t*)&h,sizeof(h))!=sizeof(h)||h.magic!=FAST_MAGIC){f.close();reply(false,"Unknown recording",500);return;}
  const uint32_t total=(f.size()-sizeof(h))/sizeof(FastSample);
  uint32_t a=0,b=total;
  if(web.hasArg("from")){a=min<uint32_t>(total,(uint32_t)max(0L,web.arg("from").toInt())*1000UL/FAST_MS);}
  if(web.hasArg("to")){b=min<uint32_t>(total,(uint32_t)max(0L,web.arg("to").toInt())*1000UL/FAST_MS);}
  if(b<a)b=a;
  const uint32_t span=b-a,step=span>1200?(span+1199)/1200:1,n=(span+step-1)/step;
  FastSample *buf=(FastSample*)elPsAlloc(sizeof(FastSample)*(n?n:1));
  if(!buf){f.close();reply(false,"Out of memory",500);return;}
  uint16_t got=0;
  for(uint32_t i=a;i<b&&got<n;i+=step){
    f.seek(sizeof(h)+i*sizeof(FastSample));
    if(f.read((uint8_t*)&buf[got],sizeof(FastSample))!=sizeof(FastSample))break;
    ++got;
  }
  f.close();
  const uint32_t ms0=got?buf[0].ms:h.startUp;
  const int64_t utc0=h.startUtc?h.startUtc+(int64_t)((ms0-h.startUp)/1000UL):0;
  sendFast(buf,got,ms0,utc0,0);
  elPsFree(buf);
}
void handleEventFast(){
  const uint32_t id=web.arg("id").toInt();
  EventRec *e=nullptr;
  for(uint8_t i=0;i<eventCount;i++)if(events[i].id==id){e=&events[i];break;}
  if(!e){reply(false,"Event not found",404);return;}
  char name[32];snprintf(name,sizeof(name),"/EVENTS/E%06lu.FST",(unsigned long)id);
  if(sdReady&&cardInserted()&&SD.exists(name)){sendFastFile(name);return;}
  // Not on the card (yet): from the last ten minutes, if it is still there.
  if(!fastRing||e->boot!=bootId){reply(false,"This recording was not saved",404);return;}
  const uint32_t at=e->up*1000UL;
  const uint16_t want=(uint16_t)(2*FAST_BOX_MS/FAST_MS+8);
  FastSample *buf=(FastSample*)elPsAlloc(sizeof(FastSample)*want);
  if(!buf){reply(false,"Out of memory",500);return;}
  const uint16_t n=fastCopy(at>FAST_BOX_MS?at-FAST_BOX_MS:0,at+FAST_BOX_MS,buf,want);
  const uint32_t nowMs=(uint32_t)elapsedMs();
  const int64_t utc0=validWall()&&n?(int64_t)time(nullptr)-(int64_t)((nowMs-buf[0].ms)/1000UL):0;
  sendFast(buf,n,n?buf[0].ms:at,utc0,0);
  elPsFree(buf);
}
void handleTripFast(){
  const uint32_t id=web.arg("id").toInt();
  char name[32];snprintf(name,sizeof(name),"/TRIPS/T%06lu.FST",(unsigned long)id);
  sendFastFile(name);
}
// Shifts and the gear story per engine.
void shiftLifeJson(String &s,uint8_t c){
  const ShiftLife &L=shiftLife;
  s+="{\"eng\":";s+=L.engages[c];s+=",\"dis\":";s+=L.disengages[c];s+=",\"hard\":";s+=L.hard[c];s+=",\"quick\":";s+=L.quick[c];s+=",\"high\":";s+=L.highRpm[c];
  s+=",\"pull\":";s+=L.pullNeutral[c];
  s+=",\"engAvg\":";jnum(s,L.engAvg[c],0);s+=",\"disAvg\":";jnum(s,L.disAvg[c],0);
  s+=",\"engFirst\":";jnum(s,L.engFirst[c],0);s+=",\"disFirst\":";jnum(s,L.disFirst[c],0);
  s+=",\"engN\":";s+=L.engN[c];s+=",\"disN\":";s+=L.disN[c];
  s+=",\"idleLph\":";jnum(s,L.idleLph[c],2);s+=",\"idleRpm\":";jnum(s,L.idleRpm[c],0);
  s+=",\"accels\":";s+=L.accels[c];s+=",\"compOn\":";s+=L.compOn[c];s+=",\"boostPeak\":";jnum(s,L.boostPeak[c],1);
  s+=",\"warmLast\":";jnum(s,L.warmLast[c],0);s+=",\"warmAvg\":";jnum(s,L.warmAvg[c],0);s+=",\"warmN\":";s+=L.warmN[c];
  s+=",\"voltRun\":";jnum(s,L.voltRun[c],2);s+=",\"voltMin\":";jnum(s,L.voltMin[c],2);
  uint32_t age;const uint8_t g=gearNow(c,age);
  s+=",\"gear\":\"";s+=age<=shift::GEAR_FRESH_MS*2?gearCode(g):"?";s+="\",\"load\":";s+=shiftTr[c].known?(shiftTr[c].loaded?"true":"false"):"null";
  s+='}';
}
void handleShifts(){
  String s;s.reserve(1200+shiftCount*90);
  s+="{\"port\":";s+=portIndex();s+=",\"gl\":";s+=gearLearned()?"true":"false";s+=",\"cl\":";s+=cfg.compMask?"true":"false";
  s+=",\"e\":[";shiftLifeJson(s,0);s+=',';shiftLifeJson(s,1);s+="],\"list\":[";
  const uint8_t n=min<uint8_t>(shiftCount,(uint8_t)constrain(web.hasArg("n")?web.arg("n").toInt():120,1,SHIFT_N));
  for(uint8_t i=0;i<n;i++){
    const ShiftRec &r=shiftLog[i];
    if(i)s+=',';
    s+='[';s+=r.utc;s+=',';s+=r.up;s+=',';s+=r.eng;s+=',';s+=r.kind;s+=',';s+=r.gear;s+=',';s+=r.flags;s+=',';s+=r.lagMs;s+=',';s+=r.rpm;s+=']';
  }
  s+="]}";
  sendJSON(200,s);
}
// Trips four times a second as one CSV for a spreadsheet.
void handleExportFast(){
  uint32_t ids[MAX_TRIPS];uint8_t n;
  if(!parseIds(web.arg("ids"),ids,n)){web.send(400,"text/plain","No trips selected");return;}
  if(!sdReady||!cardInserted()){web.send(409,"text/plain","No SD card");return;}
  if(tripFast)tripFastFlush();
  ChunkOut &out=beginDownload("four a second","text/csv","EngineLink-trip-fast.csv");
  const int pi=portIndex();
  const char *A=pi==1?"stbd":"port",*B=pi==1?"port":"stbd";
  if(pi<0){A="j7";B="j6";}
  String s="trip_id,time_utc,seconds";
  const char *cols[]={"rpm","requested_rpm","fuel_lph","boost_kpa","status_264","gear","clutch_pulls","compressor"};
  for(const char *side:{A,B})for(const char *c:cols){s+=',';s+=side;s+='_';s+=c;}
  s+="\n";out.add(s);s="";
  for(uint8_t j=0;j<n;j++){
    char name[32];snprintf(name,sizeof(name),"/TRIPS/T%06lu.FST",(unsigned long)ids[j]);
    File f=SD.open(name,FILE_READ);
    if(!f)continue;
    SeriesHead h;
    if(f.read((uint8_t*)&h,sizeof(h))!=sizeof(h)||h.magic!=FAST_MAGIC){f.close();continue;}
    FastSample x;
    while(f.read((uint8_t*)&x,sizeof(x))==sizeof(x)){
      const uint32_t dms=x.ms-h.startUp;
      s+=ids[j];s+=',';
      if(h.startUtc){char b[24];time_t u=(time_t)(h.startUtc+dms/1000);struct tm tmv;gmtime_r(&u,&tmv);strftime(b,sizeof(b),"%Y-%m-%d %H:%M:%S",&tmv);s+=b;}
      s+=',';s+=String(dms/1000.0f,2);
      for(uint8_t c:{(uint8_t)0,(uint8_t)1}){
        s+=',';if(x.rpm[c]!=NA16)s+=x.rpm[c];
        s+=',';if(x.req[c]!=NA16)s+=x.req[c];
        s+=',';if(x.lph100[c]!=NA16)s+=String(x.lph100[c]/100.0f,2);
        s+=',';if(x.boost10[c]!=NA16)s+=String(x.boost10[c]/10.0f,1);
        s+=',';if(x.st264[c]!=0xFFFF){char hx[8];snprintf(hx,sizeof(hx),"0x%04X",x.st264[c]);s+=hx;}
        s+=',';s+=gearCode(x.gear[c]);
        s+=',';if(x.flags[c]&FF_LOADKNOWN)s+=(x.flags[c]&FF_LOAD)?'1':'0';
        s+=',';if(x.flags[c]&FF_COMPKNOWN)s+=(x.flags[c]&FF_COMP)?'1':'0';
      }
      s+="\n";
      if(s.length()>3000){out.add(s);s="";}
    }
    f.close();
  }
  out.add(s);
  endDownload(out);
}
