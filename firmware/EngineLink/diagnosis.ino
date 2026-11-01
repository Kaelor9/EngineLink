// Diagnosis: the 1 Hz sample ring, rules from diagrules.h, events with
// black boxes, engine life story and the series downloads.

// ---------- Diagnosis: rules, events, black box, trip log, life story ----------
int16_t to16(float v,float k){if(isnan(v))return NA16;const float x=v*k;return (int16_t)constrain(lroundf(x),-32767L,32767L);}
float from16(int16_t v,float k){return v==NA16?NAN:(float)v/k;}
float valueOr(uint8_t ch,uint16_t idx,uint32_t age){float v;return engineValueAge(ch,idx,v,age)?v:NAN;}
void saveEvents(){fsWriteRecords("/events.bin",0x31545645,events,sizeof(EventRec),eventCount);}
void loadEvents(){
  size_t n;uint8_t *buf=fsSlurp("/events.bin",n,sizeof(FileHead)+sizeof(events)+64);
  eventCount=0;
  if(!buf)return;
  FileHead h;memcpy(&h,buf,min(n,sizeof(h)));
  if(n>=sizeof(h)&&h.magic==0x31545645&&h.recSize==sizeof(EventRec)&&n==sizeof(h)+(size_t)h.count*sizeof(EventRec)){
    eventCount=min<uint16_t>(h.count,MAX_EVENTS);memcpy(events,buf+sizeof(h),sizeof(EventRec)*eventCount);
  }
  free(buf);
  for(uint8_t i=0;i<eventCount;i++){
    if(events[i].id>=nextEventId)nextEventId=events[i].id+1;
    if(events[i].box==1)events[i].box=3; // EngineLink restarted before the black box was written
  }
}
void saveLife(){if(fsWrite("/life.bin",&life,sizeof(life)))lifeDirty=false;}
void loadLife(){diag::Life l;if(fsRead("/life.bin",&l,sizeof(l),sizeof(l))==sizeof(l)&&l.magic==life.magic)life=l;}
uint8_t unackedEvents(){uint8_t n=0;for(uint8_t i=0;i<eventCount;i++)if(!events[i].ack&&events[i].sev>=diag::WARN)n++;return n;}
// What both engines report this second.
void sampleNow(Sample &s,diag::Input &in){
  const uint64_t now=elapsedMs();
  s=Sample();s.up=(uint32_t)(now/1000ULL);
  in=diag::Input();in.t=s.up;in.gearMask=cfg.gearMaskF|cfg.gearMaskR;
  for(uint8_t c=0;c<2;c++){
    diag::Eng &e=in.e[c];
    e.running=motor[c].running;e.polled=kad[c].polling;
    e.rpm=rpmFresh(c,now)?motor[c].rpm:NAN;
    e.lph=fuelFresh(c,now)?motor[c].fuelLph:NAN;
    e.req=valueOr(c,291,12000);e.cool=valueOr(c,259,12000);e.air=valueOr(c,261,12000);
    e.boost=valueOr(c,265,12000);e.volt=valueOr(c,293,15000);
    const float a=valueOr(c,263,15000),b=valueOr(c,264,15000);
    e.st263=isnan(a)?-1:(int32_t)a&0xFFFF;e.st264=isnan(b)?-1:(int32_t)b&0xFFFF;
    const int bk=isnan(e.rpm)?-1:bucketFor(e.rpm);
    if(bk>=0&&health.baseTrips[c][bk]>=3){
      const float *base=health.base[c][bk];
      e.baseCool=base[0];e.baseBoost=base[1];e.baseAir=base[2];e.baseLph=base[3];
    }
    s.rpm[c]=to16(e.rpm,1);s.req[c]=to16(e.req,1);s.lph10[c]=to16(e.lph,10);
    s.cool10[c]=to16(e.cool,10);s.air10[c]=to16(e.air,10);s.boost10[c]=to16(e.boost,10);s.volt100[c]=to16(e.volt,100);
    s.st263[c]=e.st263<0?0xFFFF:(uint16_t)e.st263;s.st264[c]=e.st264<0?0xFFFF:(uint16_t)e.st264;
  }
}
void ringPush(const Sample &s){
  if(!ring)return;
  ring[ringHead]=s;ringHead=(ringHead+1)%RING_N;if(ringCount<RING_N)ringCount++;
}
// Samples from the last hour between two uptimes, oldest first.
uint16_t ringCopy(uint32_t from,uint32_t to,Sample *out,uint16_t max){
  uint16_t n=0;
  for(uint16_t i=0;i<ringCount&&n<max;i++){
    const Sample &s=ring[(ringHead+RING_N-ringCount+i)%RING_N];
    if(s.up>=from&&s.up<=to)out[n++]=s;
  }
  return n;
}
bool ensureDir(const char *d){return SD.exists(d)||SD.mkdir(d);}
void tripSeriesOpen(){
  if(tripSeries)tripSeries.close();
  if(!sdReady||!cardInserted()||!ensureDir("/TRIPS"))return;
  char name[32];snprintf(name,sizeof(name),"/TRIPS/T%06lu.BIN",(unsigned long)totals.nextTripId);
  SD.remove(name);
  tripSeries=SD.open(name,FILE_WRITE);
  if(!tripSeries)return;
  SeriesHead h={SERIES_MAGIC,totals.nextTripId,bootId,(uint32_t)(elapsedMs()/1000ULL),totals.tripStartUtc,(int8_t)portIndex(),{0,0,0}};
  tripSeries.write((const uint8_t*)&h,sizeof(h));
  tripSeriesId=totals.nextTripId;
  tripCov=Coverage();
}
// After the card was mounted again: carry on with the trip that is running.
void tripSeriesReopen(){
  if(!totals.tripActive||tripSeries||!sdReady)return;
  char name[32];snprintf(name,sizeof(name),"/TRIPS/T%06lu.BIN",(unsigned long)totals.nextTripId);
  if(SD.exists(name)){tripSeries=SD.open(name,FILE_APPEND);if(tripSeries)tripSeriesId=totals.nextTripId;return;}
  const Coverage keep=tripCov;tripSeriesOpen();tripCov=keep;
}
void tripSeriesClose(bool keep){
  if(!tripSeries)return;
  tripSeries.close();
  if(keep&&sdReady&&cardInserted()){
    char name[32];snprintf(name,sizeof(name),"/TRIPS/T%06lu.COV",(unsigned long)tripSeriesId);
    File f=SD.open(name,FILE_WRITE);if(f){f.write((const uint8_t*)&tripCov,sizeof(tripCov));f.close();}
  }
  if(!keep&&sdReady&&cardInserted()){char name[32];snprintf(name,sizeof(name),"/TRIPS/T%06lu.BIN",(unsigned long)tripSeriesId);SD.remove(name);}
  tripSeriesId=0;
}
// The black box of an event: two minutes before and after it, from the ring.
void writeBlackBox(EventRec &e){
  if(!ring||!sdReady||!cardInserted()||!ensureDir("/EVENTS")){e.box=3;return;}
  Sample *buf=(Sample*)ps_malloc(sizeof(Sample)*250);
  if(!buf){e.box=3;return;}
  const uint16_t n=ringCopy(e.up>120?e.up-120:0,e.up+120,buf,250);
  char name[32];snprintf(name,sizeof(name),"/EVENTS/E%06lu.BIN",(unsigned long)e.id);
  File f=SD.open(name,FILE_WRITE);
  if(f){
    SeriesHead h={SERIES_MAGIC,e.id,e.boot,n?buf[0].up:e.up,e.utc?e.utc-(int64_t)(e.up-(n?buf[0].up:e.up)):0,(int8_t)portIndex(),{0,0,0}};
    f.write((const uint8_t*)&h,sizeof(h));f.write((const uint8_t*)buf,sizeof(Sample)*n);f.close();
    e.box=2;
  }else e.box=3;
  free(buf);
}
void addEvent(const diag::Event &d,uint32_t up){
  EventRec r;
  r.id=nextEventId++;r.utc=validWall()?(int64_t)time(nullptr):0;r.boot=bootId;r.up=up;
  r.type=d.type;r.eng=d.eng;r.sev=d.sev;r.value=d.value;r.ref=d.ref;r.box=ring?1:3;
  if(eventCount==MAX_EVENTS)--eventCount;
  memmove(&events[1],&events[0],sizeof(EventRec)*eventCount);
  events[0]=r;++eventCount;
  if(r.box==1&&pendingBoxN<8)pendingBox[pendingBoxN++]=r.id;else if(r.box==1)events[0].box=3;
  if(d.type<diag::T_COUNT)++life.eventsBy[d.type];
  if(d.type==diag::T_LONG_IDLE&&d.eng<2)++life.longIdles[d.eng];
  lifeDirty=true;
  saveEvents();
}
void serviceBlackBoxes(uint32_t up){
  for(uint8_t k=0;k<pendingBoxN;){
    EventRec *e=nullptr;
    for(uint8_t i=0;i<eventCount;i++)if(events[i].id==pendingBox[k]){e=&events[i];break;}
    if(!e){pendingBox[k]=pendingBox[--pendingBoxN];continue;}
    if(up>=e->up+120){writeBlackBox(*e);saveEvents();pendingBox[k]=pendingBox[--pendingBoxN];continue;}
    k++;
  }
}
void updateLife(const diag::Input &in,uint32_t up){
  for(uint8_t c=0;c<2;c++){
    const diag::Eng &e=in.e[c];
    if(e.running&&!lifeWasRunning[c]){++life.starts[c];lifeStartAt[c]=up;lifeColdChecked[c]=false;lifeStartMinV[c]=99;}
    lifeWasRunning[c]=e.running;
    if(!e.running)continue;
    const float rpm=isnan(e.rpm)?0:e.rpm;
    ++life.sec[c][diag::lifeBucket(rpm)];
    if(!isnan(e.cool)){
      if(e.cool>95)++life.hotSec[c];
      if(rpm>2800&&e.cool<50)++life.coldLoadSec[c];
      if(!lifeColdChecked[c]&&up-lifeStartAt[c]<=20){lifeColdChecked[c]=true;if(e.cool<30)++life.coldStarts[c];}
    }
    if(rpm>4000)++life.overSec[c];
    if(rpm>lifeTripMax[c])lifeTripMax[c]=rpm;
    if(up-lifeStartAt[c]<=20&&!isnan(e.volt))lifeStartMinV[c]=min(lifeStartMinV[c],e.volt);
    if(up-lifeStartAt[c]==21&&lifeStartMinV[c]<90)life.lowStartVolt[c]=lifeStartMinV[c];
    lifeDirty=true;
  }
}
void serviceDiagnosis(){
  Sample s;diag::Input in;
  sampleNow(s,in);
  ringPush(s);
  diag::Event ev[6];
  const int n=diagRules.step(in,ev,6);
  for(int i=0;i<n;i++)addEvent(ev[i],s.up);
  updateLife(in,s.up);
  if(totals.tripActive&&tripSeries){
    tripSeries.write((const uint8_t*)&s,sizeof(s));
    if((uint32_t)(millis()-tripSeriesFlush)>10000){tripSeries.flush();tripSeriesFlush=millis();}
  }
  serviceBlackBoxes(s.up);
}
// Series as JSON columns, at most maxPts points evenly spread.
void sendSeries(const Sample *buf,uint32_t n,const SeriesHead &h,uint16_t step){
  web.setContentLength(CONTENT_LENGTH_UNKNOWN);
  web.send(200,"application/json","");
  String s;s.reserve(4200);
  s="{\"id\":";s+=h.id;s+=",\"start\":";s+=(long long)h.startUtc;s+=",\"up0\":";s+=h.startUp;s+=",\"port\":";s+=h.port;s+=",\"step\":";s+=step;
  s+=",\"t\":[";
  for(uint32_t i=0;i<n;i++){if(i)s+=',';s+=(long)buf[i].up-(long)h.startUp;if(s.length()>4000){web.sendContent(s);s="";}}
  s+="]";
  auto col=[&](const char *name,int k){
    s+=",\"";s+=name;s+="\":[";
    for(uint8_t c=0;c<2;c++){
      s+=c?",[":"[";
      for(uint32_t i=0;i<n;i++){
        if(i)s+=',';
        const Sample &x=buf[i];
        float v=NAN;
        switch(k){case 0:v=from16(x.rpm[c],1);break;case 1:v=from16(x.req[c],1);break;case 2:v=from16(x.lph10[c],10);break;
          case 3:v=from16(x.cool10[c],10);break;case 4:v=from16(x.air10[c],10);break;case 5:v=from16(x.boost10[c],10);break;case 6:v=from16(x.volt100[c],100);break;}
        if(isnan(v))s+="null";else s+=String(v,k==6?2:k==2?1:k>=3?1:0);
        if(s.length()>4000){web.sendContent(s);s="";}
      }
      s+="]";
    }
    s+="]";
  };
  col("rpm",0);col("req",1);col("lph",2);col("cool",3);col("air",4);col("boost",5);col("volt",6);
  if(seriesCov&&seriesCov->magic==0xC0E10001){s+=",\"cov\":";coverageJson(s,*seriesCov);}
  s+="}";
  web.sendContent(s);web.sendContent("");
}
// Reads a series file, picking at most 600 evenly spaced seconds.
void sendSeriesFile(const char *path){
  if(!sdReady||!cardInserted()||!SD.exists(path)){reply(false,"No recording for this",404);return;}
  if(tripSeries&&tripSeriesId)tripSeries.flush();
  File f=SD.open(path,FILE_READ);
  if(!f){reply(false,"Cannot open the recording",500);return;}
  SeriesHead h;
  if(f.read((uint8_t*)&h,sizeof(h))!=sizeof(h)||h.magic!=SERIES_MAGIC){f.close();reply(false,"Unknown recording",500);return;}
  const uint32_t total=(f.size()-sizeof(h))/sizeof(Sample);
  const uint16_t step=total>600?(uint16_t)((total+599)/600):1;
  const uint32_t n=(total+step-1)/step;
  Sample *buf=(Sample*)ps_malloc(sizeof(Sample)*(n?n:1));
  if(!buf){f.close();reply(false,"Out of memory",500);return;}
  uint32_t got=0;
  for(uint32_t i=0;i<total&&got<n;i+=step){
    f.seek(sizeof(h)+i*sizeof(Sample));
    if(f.read((uint8_t*)&buf[got],sizeof(Sample))!=sizeof(Sample))break;
    ++got;
  }
  f.close();
  sendSeries(buf,got,h,step);
  free(buf);
}
void handleEvents(){
  String s;s.reserve(160*eventCount+200);
  s+="{\"n\":";s+=unackedEvents();s+=",\"alarm\":";s+=diagRules.alarm()?"true":"false";s+=",\"warn\":";s+=diagRules.warning()?"true":"false";
  s+=",\"boot\":";s+=bootId;s+=",\"up\":";s+=(unsigned long)(elapsedMs()/1000ULL);
  s+=",\"list\":[";
  for(uint8_t i=0;i<eventCount;i++){
    const EventRec &e=events[i];
    if(i)s+=',';
    s+="{\"id\":";s+=e.id;s+=",\"utc\":";s+=(long long)e.utc;s+=",\"boot\":";s+=e.boot;s+=",\"up\":";s+=e.up;
    s+=",\"t\":\"";s+=e.type<diag::T_COUNT?diag::TYPE_ID[e.type]:"";s+="\",\"eng\":";s+=e.eng;s+=",\"sev\":";s+=e.sev;
    s+=",\"v\":";jnum(s,e.value,2);s+=",\"ref\":";jnum(s,e.ref,2);s+=",\"box\":";s+=e.box;s+=",\"ack\":";s+=e.ack;s+='}';
  }
  s+="]}";
  sendJSON(200,s);
}
void handleEventsAck(){
  const String v=web.arg("id");
  for(uint8_t i=0;i<eventCount;i++)if(v=="all"||(uint32_t)v.toInt()==events[i].id)events[i].ack=1;
  diagRules.acknowledge();
  saveEvents();
  reply(true,"Marked as seen");
}
void handleMark(){
  diagRules.mark();
  markLedUntil=millis()+1600;
  reply(true,"Marked. The two minutes before and after are saved");
}
void handleEventSeries(){
  const uint32_t id=web.arg("id").toInt();
  EventRec *e=nullptr;
  for(uint8_t i=0;i<eventCount;i++)if(events[i].id==id){e=&events[i];break;}
  if(!e){reply(false,"Event not found",404);return;}
  if(e->box==2){char name[32];snprintf(name,sizeof(name),"/EVENTS/E%06lu.BIN",(unsigned long)id);sendSeriesFile(name);return;}
  // Not on the card (yet): from the last hour, if it is still there.
  if(!ring||e->boot!=bootId){reply(false,"This recording was not saved",404);return;}
  Sample *buf=(Sample*)ps_malloc(sizeof(Sample)*250);
  if(!buf){reply(false,"Out of memory",500);return;}
  const uint16_t n=ringCopy(e->up>120?e->up-120:0,e->up+120,buf,250);
  SeriesHead h={SERIES_MAGIC,id,bootId,n?buf[0].up:e->up,e->utc?e->utc-(int64_t)(e->up-(n?buf[0].up:e->up)):0,(int8_t)portIndex(),{0,0,0}};
  sendSeries(buf,n,h,1);
  free(buf);
}
void handleTripSeries(){
  const uint32_t id=web.arg("id").toInt();
  char name[32];snprintf(name,sizeof(name),"/TRIPS/T%06lu.BIN",(unsigned long)id);
  static Coverage saved;
  seriesCov=nullptr;
  if(id&&id==tripSeriesId)seriesCov=&tripCov;  // the trip running now
  else if(sdReady&&cardInserted()){
    char cn[32];snprintf(cn,sizeof(cn),"/TRIPS/T%06lu.COV",(unsigned long)id);
    File f=SD.open(cn,FILE_READ);
    if(f){if(f.read((uint8_t*)&saved,sizeof(saved))==sizeof(saved)&&saved.magic==0xC0E10001)seriesCov=&saved;f.close();}
  }
  sendSeriesFile(name);
  seriesCov=nullptr;
}
// The last minutes from the ring, for the live charts.
void handleRecentSeries(){
  if(!ring){reply(false,"No recording",404);return;}
  const uint32_t secs=constrain(web.arg("secs").toInt(),60,3600),up=(uint32_t)(elapsedMs()/1000ULL);
  const uint16_t step=secs>600?(uint16_t)(secs/600):1,want=secs/step+1;
  Sample *all=(Sample*)ps_malloc(sizeof(Sample)*min<uint32_t>(secs+1,RING_N));
  if(!all){reply(false,"Out of memory",500);return;}
  const uint16_t got=ringCopy(up>secs?up-secs:0,up,all,min<uint32_t>(secs+1,RING_N));
  uint16_t n=0;for(uint16_t i=0;i<got&&n<want;i+=step)all[n++]=all[i];
  const int64_t nowUtc=validWall()?(int64_t)time(nullptr):0;
  SeriesHead h={SERIES_MAGIC,0,bootId,n?all[0].up:up,nowUtc&&n?nowUtc-(int64_t)(up-all[0].up):0,(int8_t)portIndex(),{0,0,0}};
  sendSeries(all,n,h,step);
  free(all);
}
// Trip logs as one CSV for a spreadsheet: one row per second.
void handleExportSeries(){
  uint32_t ids[MAX_TRIPS];uint8_t n;
  if(!parseIds(web.arg("ids"),ids,n)){web.send(400,"text/plain","No trips selected");return;}
  if(!sdReady||!cardInserted()){web.send(409,"text/plain","No SD card");return;}
  if(tripSeries)tripSeries.flush();
  ChunkOut &out=beginDownload("second by second","text/csv","EngineLink-trip-log.csv");
  const int pi=portIndex();
  const char *A=pi==1?"stbd":"port",*B=pi==1?"port":"stbd";
  if(pi<0){A="j7";B="j6";}
  String s="trip_id,time_utc,seconds";
  const char *cols[]={"rpm","requested_rpm","fuel_lph","coolant_c","charge_air_c","boost_kpa","battery_v"};
  for(const char *side:{A,B})for(const char *c:cols){s+=',';s+=side;s+='_';s+=c;}
  s+="\n";out.add(s);s="";
  for(uint8_t j=0;j<n;j++){
    char name[32];snprintf(name,sizeof(name),"/TRIPS/T%06lu.BIN",(unsigned long)ids[j]);
    File f=SD.open(name,FILE_READ);
    if(!f)continue;
    SeriesHead h;
    if(f.read((uint8_t*)&h,sizeof(h))!=sizeof(h)||h.magic!=SERIES_MAGIC){f.close();continue;}
    Sample x;
    while(f.read((uint8_t*)&x,sizeof(x))==sizeof(x)){
      const long sec=(long)x.up-(long)h.startUp;
      s+=ids[j];s+=',';
      if(h.startUtc){char b[24];time_t u=(time_t)(h.startUtc+sec);struct tm tmv;gmtime_r(&u,&tmv);strftime(b,sizeof(b),"%Y-%m-%d %H:%M:%S",&tmv);s+=b;}
      s+=',';s+=sec;
      for(uint8_t c:{(uint8_t)0,(uint8_t)1}){
        auto put=[&](int16_t v,float k,int d){s+=',';if(v!=NA16)s+=String((float)v/k,d);};
        put(x.rpm[c],1,0);put(x.req[c],1,0);put(x.lph10[c],10,1);put(x.cool10[c],10,1);put(x.air10[c],10,1);put(x.boost10[c],10,1);put(x.volt100[c],100,2);
      }
      s+="\n";
      if(s.length()>3000){out.add(s);s="";}
    }
    f.close();
  }
  out.add(s);
  endDownload(out);
}
void handleLife(){
  String s;s.reserve(2400);
  s+="{\"port\":";s+=portIndex();
  for(uint8_t c=0;c<2;c++){
    s+=c?",":",\"e\":[";
    s+="{\"sec\":[";for(uint8_t b=0;b<diag::LIFE_BUCKETS;b++){if(b)s+=',';s+=life.sec[c][b];}
    s+="],\"starts\":";s+=life.starts[c];s+=",\"coldStarts\":";s+=life.coldStarts[c];
    s+=",\"hotSec\":";s+=life.hotSec[c];s+=",\"coldLoadSec\":";s+=life.coldLoadSec[c];s+=",\"overSec\":";s+=life.overSec[c];
    s+=",\"longIdles\":";s+=life.longIdles[c];
    s+=",\"startVolt\":";if(life.lowStartVolt[c]>0)jnum(s,life.lowStartVolt[c],2);else s+="null";
    s+=",\"wot\":";jnum(s,life.wotRpm[c],0);s+=",\"wotBest\":";jnum(s,life.wotRpmBest[c],0);
    s+=",\"hours\":";jnum(s,totals.lastEcuHours[c],1);
    s+=",\"base\":[";
    for(uint8_t b=0;b<RPM_BUCKETS;b++){
      if(b)s+=',';
      if(health.baseTrips[c][b]<3){s+="null";continue;}
      s+='[';for(uint8_t m=0;m<HEALTH_METRICS;m++){if(m)s+=',';jnum(s,health.base[c][b][m],2);}s+=']';
    }
    s+="]}";
  }
  s+="],\"events\":{";
  bool first=true;
  for(uint8_t k=1;k<diag::T_COUNT;k++)if(life.eventsBy[k]){if(!first)s+=',';first=false;s+='"';s+=diag::TYPE_ID[k];s+="\":";s+=life.eventsBy[k];}
  s+="}}";
  sendJSON(200,s);
}
// ---------- Diagnostics ----------
String makeDiagJson(){
  const uint64_t now=elapsedMs();
  String s;s.reserve(3000);
  s+="{\"t\":[";
  for(uint8_t c=0;c<2;c++){
    if(c)s+=',';
    const DiagTest &t=diagTests[c];
    const KadLink &k=kad[c];
    s+="{\"utc\":";s+=(long long)t.utc;s+=",\"req\":";s+=t.requests;s+=",\"rep\":";s+=t.replies;
    s+=",\"err\":";s+=t.checksumErrors;s+=",\"ms\":";s+=t.replyMs;
    s+=",\"running\":";s+=diagRun[c].on?"true":"false";
    s+=",\"prog\":";s+=diagRun[c].on?min(100UL,(unsigned long)((millis()-diagRun[c].startMs)/300UL)):0UL;
    s+=",\"poll\":";s+=k.polling?"true":"false";
    s+=",\"frames\":";s+=ctr.frames[c];s+=",\"invalid\":";s+=ctr.invalid[c];
    s+=",\"replyAge\":";if(k.lastReplyAt)s+=(unsigned long)(now-k.lastReplyAt);else s+="null";
    s+=",\"raw\":{";
    bool first=true;
    for(uint8_t i=0;i<KAD_N;i++){
      if(!kadRawAt[c][i]||now-kadRawAt[c][i]>PARAM_FRESH_MS)continue;
      if(!first)s+=',';first=false;
      s+='"';s+=KAD_PARAMS[i].index;s+="\":[";s+=kadRawV[c][i];s+=',';
      const float f=kadFactor(c,i);
      jnum(s,f>0?kadRawV[c][i]*f:(float)kadRawV[c][i],3);s+=',';s+=(unsigned long)(now-kadRawAt[c][i]);s+=']';
    }
    s+="}}";
  }
  s+="],\"nmea\":";s+=ctr.nmeaFrames;
  s+=",\"can\":";s+=cfg.canListen?"true":"false";
  s+=",\"sog\":";if(sogFresh())jnum(s,sogKnots,1);else s+="null";
  s+=",\"fuelDiv\":";s+=cfg.kadFuelDiv;s+=",\"scaleOk\":";s+=cfg.fuelScaleOk?"true":"false";
  s+=",\"volt\":";s+=cfg.kadVolt;s+=",\"voltOk\":";s+=cfg.voltOk?"true":"false";
  s+=",\"hrs\":[";jnum(s,totals.lastEcuHours[0],1);s+=',';jnum(s,totals.lastEcuHours[1],1);s+=']';
  s+=",\"missed\":";s+=totals.missedEvents;
  s+=",\"autoPoll\":";s+=cfg.autoPoll?"true":"false";
  s+=",\"proto\":";s+=cfg.protocol==PROTO_KAD?"\"kad\"":"\"j1587\"";
  s+=",\"quality\":{\"rejected\":[";s+=kadRejected[0];s+=',';s+=kadRejected[1];
  s+="],\"held\":[";s+=kadHeldBack[0];s+=',';s+=kadHeldBack[1];
  s+="],\"bridged\":[";s+=gapFills[0];s+=',';s+=gapFills[1];
  s+="],\"bridgedSec\":[";s+=totals.gapFillSec[0];s+=',';s+=totals.gapFillSec[1];
  s+="],\"gaps\":[";s+=motor[0].gaps;s+=',';s+=motor[1].gaps;
  s+="],\"focus\":";s+=(kadFocusIndex&&(int32_t)(kadFocusUntil-millis())>0)?kadFocusIndex:0;
  s+="}";
  for(uint8_t k=0;k<2;k++){
    const DlStat &x=k?spStat:dlStat;
    if(!x.what[0])continue;
    s+=k?",\"sp\":":",\"dl\":";
    s+="{\"what\":";jsonString(s,x.what);s+=",\"bytes\":";s+=x.bytes;s+=",\"raw\":";s+=x.raw;s+=",\"z\":";s+=x.z?"true":"false";s+=",\"ms\":";s+=x.ms;
    s+=",\"findMs\":";s+=x.findMs;s+=",\"sdMs\":";s+=x.sdMs;s+=",\"netMs\":";s+=x.netMs;
    s+=",\"ap\":";s+=x.ap?"true":"false";s+=",\"rssi\":";s+=x.rssi;s+='}';
  }
  s+=",\"cov\":";coverageJson(s,bootCov);
  s+=",\"restarts\":[";
  {bool f=true;for(uint8_t i=0;i<RESTART_LOG_N;i++){const RestartRec &r=restartLog[i];if(!r.ver[0])continue;if(!f)s+=',';f=false;
    s+="{\"r\":";s+=r.reason;s+=",\"up\":";s+=r.upBefore;s+=",\"utc\":";s+=r.utc;s+=",\"v\":";jsonString(s,r.ver);s+='}';}}
  s+="],\"sdRecoveries\":";s+=sdRecoveries;
  s+=",\"fsWriteErrors\":";s+=fsWriteErrors;
  s+=",\"fsRecoveredFiles\":";s+=fsRecoveredFiles;
  s+=",\"fsLastFailure\":";jsonString(s,fsLastFailure);
  appendLinkJson(s);
  s+=",\"engineTask\":";s+=engineTaskOn?"true":"false";
  s+=",\"engineWait\":{\"max\":";s+=engineMaxWaitMs;s+=",\"slow\":";s+=engineSlowWaits;s+='}';
  s+=",\"reading\":[";
  for(uint8_t c=0;c<2;c++){
    const KadLink &k=kad[c];
    if(c)s+=',';
    s+="{\"polling\":";s+=k.polling?"true":"false";
    s+=",\"interval\":";s+=k.intervalMs;s+=",\"rate\":";jnum(s,k.polling?1000.0f/k.intervalMs:0,1);
    s+=",\"answered\":";jnum(s,k.answered,2);
    s+=",\"skip\":[";bool f=true;for(uint8_t i=0;i<KAD_SLOW_N;i++)if(k.skip>>i&1){if(!f)s+=',';f=false;s+=KAD_POLL_SLOW[i];}
    s+="],\"disc\":{\"active\":";s+=k.discActive?"true":"false";s+=",\"done\":";s+=k.discDone?"true":"false";
    s+=",\"pos\":";s+=k.discPos;s+=",\"found\":[";f=true;
    for(uint8_t i=0;i<64;i++)if(k.discFound>>i&1){if(!f)s+=',';f=false;s+=KAD_DISC_FIRST+i;}
    s+="]}}";
  }
  s+="]";
  s+=",\"wifi\":{\"apRestarts\":";s+=apRestarts;s+=",\"joins\":";s+=(unsigned long)apJoins;s+=",\"leaves\":";s+=(unsigned long)apLeaves;
  s+=",\"reason\":";s+=(unsigned)apReason;s+=",\"staFails\":";s+=staFails;s+=",\"staDrops\":";s+=(unsigned long)staDisconnects;
  s+=",\"staReason\":";s+=(unsigned)staLastReason;s+=",\"staDropAgo\":";if(staLastDropAt)s+=(unsigned long)((millis()-staLastDropAt)/1000);else s+="null";
  s+=",\"staDown\":";if(staDownSince)s+=(unsigned long)((millis()-staDownSince)/1000);else s+="null";
  s+=",\"staConnects\":";s+=staConnects;
  s+=",\"routerAttempts\":";s+=routerAttempts;
  s+=",\"routerNativeAutoReconnect\":";s+=(WiFi.getAutoReconnect()?"true":"false");
  s+=",\"routerDriverNudges\":";s+=routerForcedReconnects;
  s+=",\"routerAssociated\":";s+=staAssociated?"true":"false";
  s+=",\"staAssocEvents\":";s+=(unsigned long)staAssociatedEvents;
  s+=",\"routerAuthMode\":";jsonString(s,stationSecurityName(staNegotiatedAuth));
  s+=",\"staIpEvents\":";s+=(unsigned long)staIpEvents;
  s+=",\"staLostIpEvents\":";s+=(unsigned long)staIpLostEvents;
  s+=",\"staAssocMs\":";s+=(unsigned long)staJoinMs;
  s+=",\"staDhcpMs\":";s+=(unsigned long)staDhcpMs;
  s+=",\"routerPhase\":";
  jsonString(s,routerHold?"rescue":!routerConfigured()?"standalone":
    WiFi.status()==WL_CONNECTED?"online":staAssociated?"dhcp":"associating");
  s+=",\"routerLastReason\":";s+=(unsigned)staLastReason;
  s+=",\"routerUnexpectedReason\":";s+=(unsigned)staLastUnexpectedReason;
  s+=",\"routerUnexpectedDrops\":";s+=(unsigned long)staUnexpectedDisconnects;
  s+=",\"routerAssociationFailures\":";s+=(unsigned long)staAssociationFailures;
  s+=",\"routerSaFastRetries\":";s+=saQueryFastRetries;
  s+=",\"speedTxBytes\":";s+=(unsigned long)speedTxLastBytes;
  s+=",\"speedTxMs\":";s+=(unsigned long)speedTxLastMs;
  s+=",\"speedTxWrites\":";s+=(unsigned long)speedTxLastWrites;
  s+=",\"routerFailureReason\":";s+=(unsigned)routerFailureReason;
  s+=",\"routerFailurePhase\":";s+=(unsigned)routerFailurePhase;
  s+=",\"routerFailureAfterMs\":";s+=(unsigned long)routerFailureAfterMs;
  s+=",\"routerWaitSec\":";s+=routerGuard.disconnectedSince?(unsigned long)((millis()-routerGuard.disconnectedSince)/1000UL):0;
  s+=",\"ch\":";s+=apChannelNow;
  s+=",\"conflict\":";s+=staConflict?"true":"false";
  s+=",\"routerHold\":";s+=routerHold?"true":"false";
  s+=",\"routerFallbacks\":";s+=routerFallbacks;
  s+=",\"routerAutoReturns\":";s+=routerAutoReturns;s+=",\"routerNextTryS\":";
  if(routerHold&&routerConfigured())s+=(long)((int32_t)(routerProbeAt-millis())/1000);else s+="null";
  s+=",\"apMode\":";s+=(unsigned)WiFi.getMode();
  s+=",\"apDriverSsid\":";jsonString(s,WiFi.softAPSSID());
  s+=",\"apDriverIp\":";jsonString(s,WiFi.softAPIP().toString());
  s+=",\"apStops\":";s+=(unsigned long)apStops;s+=",\"chMoves\":";s+=apChannelChanges;s+=",\"bw\":";{wifi_bandwidth_t b=WIFI_BW_HT20;esp_wifi_get_bandwidth(WIFI_IF_AP,&b);s+=b==WIFI_BW_HT40?40:20;}
  s+=",\"httpRebinds\":";s+=web.listenerRebinds;
  s+=",\"httpSelfFails\":";s+=httpSelfFails;s+=",\"httpHeals\":";s+=httpHeals;s+=",\"httpSelfRestarts\":";s+=httpSelfRestarts;
  s+=",\"httpRebindAgoMs\":";s+=httpBoundAt?(unsigned long)(millis()-httpBoundAt):0;
  s+=",\"httpBoundEpoch\":";s+=httpBoundEpoch;
  s+=",\"wifiIpEpoch\":";s+=(unsigned long)networkIpEpoch;
  s+=",\"httpServed\":";s+=web.served;s+=",\"httpReused\":";s+=web.reused;
  s+=",\"httpEvicted\":";s+=web.evicted;s+=",\"httpSlowMs\":";s+=web.slowestMs;
  s+=",\"httpParseSlowMs\":";s+=web.worstParseMs;
  s+=",\"httpHandleSlowMs\":";s+=web.worstHandlerMs;
  s+=",\"httpSlowPath\":";jsonString(s,web.slowPath);
  s+=",\"httpInteractiveSlowMs\":";s+=web.slowestInteractiveMs;
  s+=",\"httpTransferSlowMs\":";s+=web.slowestTransferMs;
  s+=",\"httpOpen\":";s+=web.openSockets();s+=",\"loopMaxMs\":";s+=loopMaxMs;
  s+=",\"mdns\":";s+=mdnsReady?"true":"false";
  s+=",\"mdnsStarts\":";s+=mdnsRestarts;
  s+=",\"mdnsFailures\":";s+=mdnsFailures;
  s+=",\"mdnsHost\":";jsonString(s,cfg.hostName);
  s+=",\"scanBusy\":";s+=scanBusy?"true":"false";
  s+=",\"scanChannel\":";s+=scanBusy?(unsigned)scanChannel:0;
  s+=",\"scanPass\":";s+=scanPass;
  s+=",\"scanFirstPassNets\":";s+=scanPassOneCount;
  s+=",\"scanFailures\":";s+=scanFailures;
  s+=",\"scanStartFailures\":";s+=scanStartFailures;
  s+=",\"scanRawCount\":";s+=scanRawCount;
  s+=",\"scanDurationMs\":";s+=scanDurationMs;
  s+=",\"scanNets\":";s+=scanEntriesCount;
  s+=",\"scanAge\":";if(scanCompletedMs)s+=(unsigned long)((millis()-scanCompletedMs)/1000);else s+="null";
  s+="},\"heap\":";s+=ESP.getFreeHeap();s+=",\"minHeap\":";s+=ESP.getMinFreeHeap();
  s+=",\"reset\":";s+=(unsigned)esp_reset_reason();
  s+=",\"up\":";s+=(unsigned long)(now/1000ULL);
  s+='}';
  return s;
}
void handleDiag(){sendJSON(200,makeDiagJson());}
// 30 s read test on one connector. Polls (read requests only) unless the
// connector is already being polled, then counts the answers.
// "Discover more values": asks once for every index from 256 to 319 with the
// same read request, in the slow slot, and lists the ones that are answered.
void handleKadDiscover(){
  const int ch=web.arg("ch").toInt();
  if(ch<0||ch>1){reply(false,"Choose an engine",400);return;}
  KadLink &k=kad[ch];
  if(!k.polling){reply(false,"Start reading this engine first",409);return;}
  k.discActive=true;k.discPos=0;k.discFound=0;k.discDone=false;
  reply(true,"Asking for 64 values, about a minute");
}
// The app studies one value closely (learning the gear bits): ask for it in
// every slow slot for up to 90 seconds.
void handleDiagFocus(){
  const int idx=web.arg("idx").toInt(),secs=constrain(web.arg("secs").toInt(),0,90);
  if(!kadParam((uint16_t)idx)){reply(false,"Unknown value",400);return;}
  kadFocusIndex=(uint16_t)idx;kadFocusUntil=millis()+(uint32_t)secs*1000UL;
  reply(true,"OK");
}
void handleDiagTest(){
  const int ch=web.arg("ch").toInt();
  if(ch<0||ch>1){reply(false,"Invalid connector",400);return;}
  if(web.arg("stop")=="1"){
    diagRun[ch].on=false;
    if(kad[ch].polling&&kad[ch].byTest)stopPolling(ch,"Test stopped");
    reply(true,"Test stopped");return;
  }
  if(cfg.protocol!=PROTO_KAD){reply(false,"The connection test needs the Volvo Penta KAD protocol",409);return;}
  KadLink &k=kad[ch];
  if(!k.polling){
    String why;
    if(!startPolling(ch,why)){reply(false,why,409);return;}
    k.byTest=true;k.byAuto=false;
  }
  DiagRun &d=diagRun[ch];
  d.on=true;d.startMs=millis();d.req0=k.requests;d.rep0=k.replies;d.inv0=ctr.invalid[ch];d.ms0=k.replyMsSum;d.cnt0=k.replyCount;
  reply(true,"Testing for 30 seconds");
}
void serviceDiag(){
  for(uint8_t c=0;c<2;c++){
    DiagRun &d=diagRun[c];
    if(!d.on||(uint32_t)(millis()-d.startMs)<30000)continue;
    d.on=false;
    KadLink &k=kad[c];
    DiagTest &t=diagTests[c];
    t.utc=validWall()?(int64_t)time(nullptr):0;
    t.requests=(uint16_t)min<uint32_t>(65535,k.requests-d.req0);
    t.replies=(uint16_t)min<uint32_t>(65535,k.replies-d.rep0);
    t.checksumErrors=(uint16_t)min<uint32_t>(65535,ctr.invalid[c]-d.inv0);
    const uint32_t n=k.replyCount-d.cnt0;
    t.replyMs=n?(uint16_t)min<uint64_t>(65535,(k.replyMsSum-d.ms0)/n):0;
    saveDiag();
    if(k.polling&&k.byTest)stopPolling(c,"Connection test finished");
  }
}
