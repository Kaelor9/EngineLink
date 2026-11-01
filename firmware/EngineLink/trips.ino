// Trips: start/end detection, per-trip logs, data coverage, history and
// the per-second learning (economy, health).

// ---------- Engine-run segments ----------
// A segment is one continuous period with fresh RPM above cranking speed on a
// physical channel. Segments are only recorded, never merged into "trips".
void writeSegmentCSV(const Segment &s){
  if(!sdReady||!cardInserted()||otaActive)return;
  File f=SD.open("/SEGMENTS.CSV",FILE_APPEND);
  if(!f)return;
  if(f.size()==0)f.println("boot_id,channel,engine,start_uptime_ms,end_uptime_ms,start_utc,duration_s,observed_fuel_l,max_rpm");
  const int engineId=cfg.portChannel==0?-1:((s.ch+1)==cfg.portChannel?0:1);
  f.printf("%08lX,J%u,%s,%llu,%llu,%lld,%lu,%.3f,%.0f\n",(unsigned long)s.boot,s.ch==0?7:6,
    engineId==0?"port":engineId==1?"starboard":"unassigned",
    (unsigned long long)s.startMs,(unsigned long long)s.endMs,(long long)s.startUtc,
    (unsigned long)((s.endMs-s.startMs)/1000ULL),s.liters,s.maxRpm);
  f.close();
}
void closeSegment(uint8_t c){
  Motor &m=motor[c];
  Segment &s=segments[segmentIndex];
  s.boot=bootId;s.ch=c;s.startMs=m.runStartMs;s.endMs=m.lastRunMs;
  s.startUtc=m.runStartUtc;s.liters=(float)(m.observedLiters-m.runStartLiters);s.maxRpm=m.runMaxRpm;
  segmentIndex=(segmentIndex+1)%MAX_SEGMENTS;
  if(segmentCount<MAX_SEGMENTS)segmentCount++;
  m.running=false;
  writeSegmentCSV(s);
  saveTotals();
}
void coverageAdd(Coverage &v,uint8_t c,uint32_t sec,float rpm,bool fuel){
  if(v.lastGoodSec[c]&&sec>v.lastGoodSec[c]+1){
    const uint32_t gap=sec-v.lastGoodSec[c]-1;
    if(gap<=600&&v.lastGoodRpm[c]>400&&rpm>400){
      v.missing[c]+=gap;if(gap>v.longest[c])v.longest[c]=(uint16_t)min<uint32_t>(gap,65535);
      if(gap>=3&&v.gaps[c]<65535)++v.gaps[c];
    }
  }
  if(rpm>=ENGINE_RUN_RPM){++v.good[c];if(fuel)++v.fuelGood[c];}
  v.lastGoodSec[c]=sec;v.lastGoodRpm[c]=rpm;
}
void coverageSecond(){
  const uint64_t now=elapsedMs();const uint32_t sec=(uint32_t)(now/1000ULL);
  for(uint8_t c=0;c<2;c++){
    const Motor &m=motor[c];
    if(!m.rpmEver||now-m.rpmAt>=2000)continue;
    const bool fuel=m.fuelEver&&now-m.fuelAt<3000;
    coverageAdd(bootCov,c,sec,m.rpm,fuel);
    if(tripSeriesId)coverageAdd(tripCov,c,sec,m.rpm,fuel);
  }
}
void coverageJson(String &s,const Coverage &v){
  s+="[";
  for(uint8_t c=0;c<2;c++){
    if(c)s+=',';
    s+="{\"good\":";s+=v.good[c];s+=",\"missing\":";s+=v.missing[c];s+=",\"fuel\":";s+=v.fuelGood[c];
    s+=",\"longest\":";s+=v.longest[c];s+=",\"gaps\":";s+=v.gaps[c];s+='}';
  }
  s+="]";
}
void serviceEngines(){
  if((uint32_t)(millis()-lastEngineService)<1000)return;
  lastEngineService=millis();
  const uint64_t now=elapsedMs();
  for(uint8_t c=0;c<2;c++){
    Motor &m=motor[c];
    const bool on=rpmFresh(c,now)&&m.rpm>=ENGINE_RUN_RPM;
    if(on){
      if(!m.running){
        m.running=true;m.runStartMs=now;m.lastTickMs=now;m.runMaxRpm=0;
        m.runStartUtc=validWall()?(int64_t)time(nullptr):0;
        m.runStartLiters=m.observedLiters;
      }
      const uint64_t dt=now-m.lastTickMs;
      if(dt>=1000){
        const uint32_t sec=(uint32_t)(dt/1000ULL);
        totals.runSeconds[c]+=sec;totals.tripRunSeconds[c]+=sec;
        m.lastTickMs+=sec*1000ULL;totalsDirty=true;
      }
      m.lastRunMs=now;
      if(m.rpm>m.runMaxRpm)m.runMaxRpm=m.rpm;
    }else if(m.running){
      m.lastTickMs=now;
      if(now-m.lastRunMs>=ENGINE_STOP_DEBOUNCE_MS)closeSegment(c);
    }
  }
  coverageSecond();
  serviceTripSecond();
  serviceDiagnosis();
  // While an engine runs the totals are saved every 30 s: power often goes with
  // the ignition, before an engine is seen to stop, and everything since the
  // last save would be lost. NVS spreads the writes; this is years of use.
  const uint32_t every=(motor[0].running||motor[1].running)?TOTALS_SAVE_RUNNING_MS:TOTALS_SAVE_MS;
  if(totalsDirty&&(uint32_t)(millis()-lastTotalsSave)>=every)saveTotals();
}
// ---------- One-hour trend ----------
void serviceHistory(){
  if((uint32_t)(millis()-lastHistory)<HISTORY_STEP_MS)return;
  lastHistory=millis();
  const uint64_t now=elapsedMs();
  for(uint8_t c=0;c<2;c++){
    histFuel[c][histIndex]=fuelFresh(c,now)?(int16_t)lroundf(min(motor[c].fuelLph,320.0f)*100.0f):-1;
    histRpm[c][histIndex]=rpmFresh(c,now)?(uint16_t)lroundf(min(motor[c].rpm,8000.0f)):0xFFFF;
  }
  const bool sog=isfinite(sogKnots)&&sogAt&&now-sogAt<7000;
  histSog[histIndex]=sog?(int16_t)lroundf(min(sogKnots,320.0f)*100.0f):-1;
  histIndex=(histIndex+1)%HISTORY_LEN;
  if(histCount<HISTORY_LEN)histCount++;
}
double tripLitres(const TripRec &r,uint8_t ch){
  return cfg.protocol==PROTO_KAD?rawToLitres(ch,r.rawH[ch],r.rawB[ch][0],r.rawB[ch][1],r.rawB[ch][2]):(double)r.litJ[ch];
}
void writeTripCSV(const TripRec &r){
  if(!sdReady||!cardInserted()||otaActive)return;
  File f=SD.open("/TRIPS.CSV",FILE_APPEND);
  if(!f)return;
  if(f.size()==0)f.println("trip_id,start_utc,end_utc,running_s,j7_l,j6_l,total_l,max_rpm,boot_id,start_uptime_s,end_uptime_s");
  const double a=tripLitres(r,0),b=tripLitres(r,1);
  f.printf("%lu,%lld,%lld,%lu,%.2f,%.2f,%.2f,%u,%08lX,%lu,%lu\n",(unsigned long)r.id,(long long)r.startUtc,(long long)r.endUtc,
    (unsigned long)r.anySec,a,b,a+b,(unsigned)r.maxRpm,(unsigned long)r.bootId,(unsigned long)r.startSec,(unsigned long)r.endSec);
  f.close();
}
void resetTripHealth(){memset(tripHealthSum,0,sizeof(tripHealthSum));memset(tripHealthN,0,sizeof(tripHealthN));}
void startTrip(){
  for(uint8_t c=0;c<2;c++){totals.tripL[c]=0;totals.tripRawH[c]=0;totals.tripRunSeconds[c]=0;for(uint8_t b=0;b<FUEL_BANDS;b++)totals.tripRawB[c][b]=0;}
  totals.tripActive=true;totals.tripBoot=bootId;
  totals.tripStartSec=(uint32_t)(elapsedMs()/1000ULL);
  totals.tripStartUtc=validWall()?(int64_t)time(nullptr):0;
  totals.tripLastUtc=totals.tripStartUtc;
  totals.tripAnySec=0;totals.tripMaxRpm=0;++totals.tripResets;
  if(!totals.seasonStartUtc&&validWall())totals.seasonStartUtc=time(nullptr);
  resetTripHealth();
  tripSeriesOpen();
  for(uint8_t c=0;c<2;c++)lifeTripMax[c]=0;
  saveTotals();
}
void backfillTripTime(){
  if(!validWall())return;
  const int64_t nowUtc=time(nullptr),nowSec=(int64_t)(elapsedMs()/1000ULL);
  if(totals.tripActive&&totals.tripBoot==bootId){
    if(!totals.tripStartUtc)totals.tripStartUtc=nowUtc-(nowSec-(int64_t)totals.tripStartSec);
    if(!totals.tripLastUtc&&lastEngineOnSec)totals.tripLastUtc=nowUtc-(nowSec-(int64_t)lastEngineOnSec);
  }
  if(!totals.seasonStartUtc&&totals.tripActive)totals.seasonStartUtc=totals.tripStartUtc;
  // Trips that ended earlier in this boot before the clock was known.
  bool changed=false;
  for(uint8_t i=0;i<tripCount;i++){
    TripRec &r=trips[i];
    if(r.bootId!=bootId)continue;
    if(!r.startUtc){r.startUtc=nowUtc-(nowSec-(int64_t)r.startSec);changed=true;}
    if(!r.endUtc){r.endUtc=nowUtc-(nowSec-(int64_t)r.endSec);changed=true;}
  }
  if(changed)saveTrips();
}
void endTrip(){
  totals.tripActive=false;
  // Very short runs (an engine started for a few seconds) are not trips.
  if(totals.tripAnySec>=20){
    TripRec r;
    r.id=totals.nextTripId++;
    r.startUtc=totals.tripStartUtc;
    r.endUtc=totals.tripLastUtc;
    r.anySec=totals.tripAnySec;
    r.bootId=totals.tripBoot;r.startSec=totals.tripStartSec;
    r.endSec=totals.tripBoot==bootId&&lastEngineOnSec?lastEngineOnSec:totals.tripStartSec+totals.tripAnySec;
    for(uint8_t c=0;c<2;c++){r.rawH[c]=(float)totals.tripRawH[c];r.litJ[c]=(float)totals.tripL[c];for(uint8_t b=0;b<FUEL_BANDS;b++)r.rawB[c][b]=(float)totals.tripRawB[c][b];}
    r.maxRpm=totals.tripMaxRpm;
    if(tripCount==MAX_TRIPS)--tripCount;
    memmove(&trips[1],&trips[0],sizeof(TripRec)*tripCount);
    trips[0]=r;++tripCount;
    saveTrips();
    writeTripCSV(r);
    if(totals.tripBoot==bootId)finishTripHealth();
    if(cfg.engReports&&cfg.ghToken.length()){
      engineReportTrip=r.id;
      Preferences p;if(p.begin("enginelink",false)){p.putUInt("erpend",r.id);p.end();}
    }
    tripSeriesClose(true);
    for(uint8_t c=0;c<2;c++)if(lifeTripMax[c]>0){life.wotRpm[c]=lifeTripMax[c];if(lifeTripMax[c]>life.wotRpmBest[c])life.wotRpmBest[c]=lifeTripMax[c];}
    lifeDirty=true;
  }else tripSeriesClose(false);
  saveTotals();
}
TripRec *tripById(uint32_t id){for(uint8_t i=0;i<tripCount;i++)if(trips[i].id==id)return &trips[i];return nullptr;}
// ---------- Learning: economy and engine health ----------
uint8_t zoneFor(float rpm){return rpm<1000?0:rpm<2000?1:rpm<2500?2:rpm<3000?3:4;}
int bucketFor(float rpm){if(rpm<1000)return -1;int b=(int)((rpm-1000.0f)/250.0f);return b>=RPM_BUCKETS?RPM_BUCKETS-1:b;}
bool sogFresh(){return isfinite(sogKnots)&&sogAt&&elapsedMs()-sogAt<7000;}
void learnSecond(){
  const uint64_t now=elapsedMs();
  float rpmSum=0,lph=0;uint8_t n=0;bool lphOk=true;
  for(uint8_t c=0;c<2;c++){
    if(!motor[c].running)continue;
    if(rpmFresh(c,now)){rpmSum+=motor[c].rpm;++n;}
    if(fuelFresh(c,now))lph+=motor[c].fuelLph;else lphOk=false;
  }
  if(!n||!lphOk)return;
  const float rpm=rpmSum/n,L=lph/3600.0f;
  const bool sog=sogFresh();
  const uint8_t z=zoneFor(rpm);
  totals.zoneL[z]+=L;totals.zoneSec[z]+=1;
  if(sog)totals.zoneNm[z]+=sogKnots/3600.0f;
  const int b=bucketFor(rpm);
  if(b>=0){
    totals.bucketSec[b]+=1;totals.bucketL[b]+=L;
    if(sog){totals.bucketSogSec[b]+=1;totals.bucketSogL[b]+=L;totals.bucketNm[b]+=sogKnots/3600.0f;}
  }
  // Health: only a warm engine at a steady rpm step says something.
  for(uint8_t c=0;c<2;c++){
    if(!motor[c].running||!rpmFresh(c,now)||!fuelFresh(c,now))continue;
    const int eb=bucketFor(motor[c].rpm);
    if(eb<0)continue;
    float cool,boost,air;
    if(!engineValue(c,259,cool)||!engineValue(c,265,boost)||!engineValue(c,261,air))continue;
    if(cool<70)continue;
    float *sum=tripHealthSum[c][eb];
    sum[0]+=cool;sum[1]+=boost;sum[2]+=air;sum[3]+=motor[c].fuelLph;
    if(tripHealthN[c][eb]<65000)++tripHealthN[c][eb];
  }
}
void finishTripHealth(){
  constexpr uint16_t MIN_N=120; // two minutes at one rpm step
  int bestB[2]={-1,-1};
  for(uint8_t c=0;c<2;c++){
    uint16_t best=0;
    for(uint8_t b=0;b<RPM_BUCKETS;b++)if(tripHealthN[c][b]>=MIN_N&&tripHealthN[c][b]>best){best=tripHealthN[c][b];bestB[c]=b;}
    for(uint8_t b=0;b<RPM_BUCKETS;b++){
      const uint16_t n=tripHealthN[c][b];
      if(n<MIN_N)continue;
      float avg[HEALTH_METRICS];
      for(uint8_t m=0;m<HEALTH_METRICS;m++)avg[m]=tripHealthSum[c][b][m]/n;
      uint16_t &bt=health.baseTrips[c][b];
      float *base=health.base[c][b];
      if((int)b==bestB[c]&&bt>=3){
        health.dev[c][0]=avg[0]-base[0];
        for(uint8_t m=1;m<HEALTH_METRICS;m++)health.dev[c][m]=fabsf(base[m])>0.01f?(avg[m]-base[m])/base[m]:0;
        health.devSamples[c]=n;health.devRpm[c]=1000+b*250+125;
      }
      // Running mean for the first trips, then a slow average.
      const float k=bt<4?1.0f/(bt+1):0.2f;
      for(uint8_t m=0;m<HEALTH_METRICS;m++)base[m]+=(avg[m]-base[m])*k;
      if(bt<60000)++bt;
    }
  }
  // Fuel balance: both engines at the same rpm step.
  for(uint8_t b=0;b<RPM_BUCKETS;b++){
    if(tripHealthN[0][b]>=MIN_N&&tripHealthN[1][b]>=MIN_N){
      const float a=tripHealthSum[0][b][3]/tripHealthN[0][b],d=tripHealthSum[1][b][3]/tripHealthN[1][b];
      if(a>0.5f)health.balance=(d-a)/a;
      break;
    }
  }
  health.updatedUtc=validWall()?(int64_t)time(nullptr):0;
  saveHealth();
}
// The engine's own hour meter shows running that EngineLink did not see (it
// was off or restarting). The missing fuel is estimated from the average use.
void checkEcuHours(uint8_t ch,float hours){
  if(ch>1||!(hours>0&&hours<200000))return;
  if(!ecuHoursChecked[ch]){
    ecuHoursChecked[ch]=true;
    const float last=totals.lastEcuHours[ch],gap=hours-last;
    if(last>0&&gap>0.1f&&gap<500){
      const double runH=totals.runSeconds[ch]/3600.0;
      const double avg=runH>0.5?lifetimeLitersFor(ch)/runH:10.0;
      const double est=gap*constrain(avg,1.0,80.0);
      totals.estMissedL+=est;++totals.missedEvents;
      lastMissedUtc=validWall()?(int64_t)time(nullptr):0;lastMissedL=(float)est;
      totalsDirty=true;
    }
  }
  if(hours>=totals.lastEcuHours[ch]){totals.lastEcuHours[ch]=hours;}
}
// Called once a second from serviceEngines().
void serviceTripSecond(){
  const bool anyOn=motor[0].running||motor[1].running;
  if(anyOn){
    if(!totals.tripActive)startTrip();
    ++totals.tripAnySec;
    lastEngineOnMs=millis();lastEngineOnSec=(uint32_t)(elapsedMs()/1000ULL);
    if(validWall())totals.tripLastUtc=time(nullptr);
    const uint64_t now=elapsedMs();
    for(uint8_t c=0;c<2;c++)if(rpmFresh(c,now)&&motor[c].rpm>totals.tripMaxRpm)totals.tripMaxRpm=(uint16_t)motor[c].rpm;
    learnSecond();
    totalsDirty=true;
  }else if(totals.tripActive&&(uint32_t)(millis()-lastEngineOnMs)>=(uint32_t)cfg.tripEndMin*60000UL){
    endTrip();
  }
}
