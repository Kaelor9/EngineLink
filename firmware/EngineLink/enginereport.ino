// Engine report: how well both engines were read on the last trip, sent to
// GitHub with the owner's token after each trip (and on request), the same
// way as the Wi-Fi report. It holds engine data and trip times, never a
// position or names. Called from loop(), which holds the engine lock.

static void appendEngineReading(String &s,uint8_t c,const char *side){
  const KadLink &k=kad[c];
  s+=side;s+=" engine (connector ";s+=c==0?"J7":"J6";s+=")\n";
  s+="  Frames: ";s+=ctr.frames[c];s+=" good, ";s+=ctr.invalid[c];s+=" bad checksum, ";
  s+=ctr.overflows[c];s+=" too long, ";s+=(unsigned long)ctr.rxDropped[c];s+=" dropped, ";
  s+=(unsigned long)ctr.lineErrors[c];s+=" line errors\n";
  if(cfg.protocol==PROTO_KAD){
    s+="  Reading: ";
    if(k.polling){s+="on (";s+=k.byAuto?"automatic":k.byTest?"connection test":"switched on";s+="), a request every ";s+=k.intervalMs;s+=" ms";}
    else if(k.byAuto&&cfg.autoPoll&&!motor[c].running&&k.stopReason.startsWith("No answer"))s+="paused: the engine stopped answering (ignition off), tried again every 30 s";
    else{s+="off";if(k.stopReason.length()){s+=" · ";s+=k.stopReason;}}
    s+="\n  Requests ";s+=k.requests;s+=", replies ";s+=k.replies;
    if(isfinite(k.answered)){s+=" (";s+=(int)(k.answered*100);s+=" % answered lately)";}
    s+=", unanswered in a row ";s+=k.unanswered;
    s+=", average reply ";s+=k.replyCount?(unsigned long)(k.replyMsSum/k.replyCount):0UL;s+=" ms\n";
    s+="  Values held back by the plausibility check: ";s+=kadHeldBack[c];s+=", rejected: ";s+=kadRejected[c];s+="\n";
    s+="  Values the engine does not answer: ";
    bool any=false;
    for(uint8_t i=0;i<KAD_SLOW_N&&i<16;i++)if(k.skip&(1u<<i)){if(any)s+=", ";any=true;s+=KAD_POLL_SLOW[i];}
    s+=any?"\n":"none\n";
    s+="  Other requests seen on the bus (another tool or display): ";s+=k.foreignRequests;s+="\n";
  }
  const uint64_t now=elapsedMs();
  s+="  Last rpm ";if(motor[c].rpmEver){s+=(int)motor[c].rpm;s+=" (";s+=(unsigned long)((now-motor[c].rpmAt)/1000ULL);s+=" s ago)";}else s+="never";
  s+=" · last fuel rate ";if(motor[c].fuelEver){s+=String(motor[c].fuelLph,1);s+=" L/h (";s+=(unsigned long)((now-motor[c].fuelAt)/1000ULL);s+=" s ago)";}else s+="never";
  s+="\n";
}

static const char *c2side(uint8_t c){const int pi=portIndex();return pi<0?(c==0?"J7":"J6"):(c==pi?"port":"starboard");}
// Gear and clutch on this trip: shifts at idle per engine, lever-to-clutch
// times, slow and hard ones, and the engine's own story.
static void appendGearReport(String &s,const TripRec *t){
  s+="GEAR AND CLUTCH\n";
  s+="Gear bits learned: ";s+=gearLearned()?"yes":"no (run the drive test)";s+=" · compressor bit: ";s+=cfg.compMask?"learned":"not learned";s+="\n";
  const int pi=portIndex();
  for(uint8_t k=0;k<2;k++){
    const uint8_t c=pi==1?1-k:k;
    uint16_t in=0,out=0,slow=0,quick=0,nIn=0,nOut=0;long sumIn=0,sumOut=0;int maxOut=-1;
    for(uint8_t i=0;i<shiftCount;i++){
      const ShiftRec &r=shiftLog[i];
      if(r.eng!=c||!r.utc||(int64_t)r.utc<t->startUtc||(int64_t)r.utc>t->endUtc+120)continue;
      if(r.kind==shift::K_ENGAGE){++in;if(r.lagMs>=0){sumIn+=r.lagMs;++nIn;}if(r.flags&shift::F_QUICK)++quick;}
      else{++out;if(r.lagMs>=0){sumOut+=r.lagMs;++nOut;if(r.lagMs>maxOut)maxOut=r.lagMs;}if(r.flags&shift::F_SLOW)++slow;}
    }
    const ShiftLife &L=shiftLife;
    s+="  ";s+=pi<0?(c==0?"J7":"J6"):(k==0?"Port":"Starboard");s+=": ";s+=in;s+=" into gear, ";s+=out;s+=" out of gear";
    if(nIn){s+=" · lever to clutch in ";s+=sumIn/nIn;s+=" ms";}
    if(nOut){s+=" · neutral to clutch out ";s+=sumOut/nOut;s+=" ms (longest ";s+=maxOut;s+=" ms)";}
    if(slow){s+=" · ";s+=slow;s+=" slow to let go";}
    if(quick){s+=" · ";s+=quick;s+=" quick re-engage";}
    s+="\n    Since installed: ";s+=L.engages[c];s+=" shifts, ";s+=L.hard[c];s+=" hard, ";s+=L.pullNeutral[c];s+=" pulled in neutral";
    s+=" · neutral idle ";
    if(isfinite(L.idleLph[c])){s+=String(L.idleLph[c],2);s+=" L/h at ";s+=(int)L.idleRpm[c];s+=" rpm";}else s+="not learned";
    s+="\n    Compressor on ";s+=L.compOn[c];s+=" of ";s+=L.accels[c];s+=" accelerations";
    if(isfinite(L.boostPeak[c])){s+=", boost peak ";s+=String(L.boostPeak[c],0);s+=" kPa";}
    s+=" · warm-up to 60 C ";
    if(isfinite(L.warmLast[c])){s+=(int)(L.warmLast[c]/60);s+=" min last time";}else s+="not timed yet";
    s+=" · charging ";
    if(isfinite(L.voltRun[c])){s+=String(L.voltRun[c],2);s+=" V";}else s+="—";
    if(isfinite(L.voltMin[c])){s+=" (lowest ";s+=String(L.voltMin[c],2);s+=" V this trip)";}
    s+="\n";
  }
  s+="\n";
}

String buildEngineReport(const TripRec *t){
  String s;s.reserve(12000);
  s+="ENGINELINK - ENGINE REPORT\n==========================\n\n";
  s+="Firmware: " EL_VERSION "\nUptime: ";s+=(unsigned long)(elapsedMs()/1000ULL);s+=" seconds\n";
  s+="Protocol: ";s+=cfg.protocol==PROTO_KAD?"Volvo Penta KAD / EDC I":"SAE J1587";s+="\n";
  s+="Read engines automatically: ";s+=cfg.autoPoll?"on":"off";s+="\n\n";
  if(t){
    s+="TRIP ";s+=t->id;s+="\n";
    if(t->startUtc>0){
      char b[40];time_t st=(time_t)t->startUtc;struct tm g;gmtime_r(&st,&g);
      strftime(b,sizeof(b),"%Y-%m-%d %H:%M UTC",&g);s+="Started: ";s+=b;s+="\n";
    }
    s+="Engines running: ";s+=t->anySec/60;s+=" min · highest rpm ";s+=t->maxRpm;s+="\n";
    const int pi=portIndex();
    for(uint8_t c=0;c<2;c++){
      s+=pi<0?(c==0?"J7":"J6"):(c==pi?"Port":"Starboard");
      s+=" fuel: ";s+=String(tripLitres(*t,c),1);s+=" L\n";
    }
    uint8_t n=0;for(uint8_t i=0;i<eventCount;i++)if(events[i].utc>=t->startUtc&&events[i].utc<=t->endUtc+120)++n;
    s+="Events during the trip: ";s+=n;s+="\n";
    // Oldest first: time, engine, type, measured value and its reference.
    for(int i=eventCount-1;i>=0;i--){
      const EventRec &e=events[i];
      if(e.utc<t->startUtc||e.utc>t->endUtc+120)continue;
      char b[16];time_t u=(time_t)e.utc;struct tm g;gmtime_r(&u,&g);strftime(b,sizeof(b),"%H:%M:%S",&g);
      s+="  ";s+=b;s+=" UTC  ";s+=e.eng>1?"both":c2side(e.eng);s+="  ";s+=e.type<diag::T_COUNT?diag::TYPE_ID[e.type]:"?";
      s+="  value ";s+=String(e.value,2);s+=", ref ";s+=String(e.ref,2);s+="\n";
    }
    s+="\n";
    appendGearReport(s,t);
  }
  s+="READING\n";
  const int pi=portIndex();
  for(uint8_t c=0;c<2;c++)appendEngineReading(s,c,pi<0?(c==0?"First":"Second"):(c==pi?"Port":"Starboard"));
  s+="\nData coverage this trip (seconds with fresh values per engine): ";
  coverageJson(s,tripCov);
  s+="\n\nMACHINE-READABLE JSON (the app's diagnostics data)\n";
  String d=makeDiagJson();
  if(d.length()>40000)d=d.substring(0,40000)+"...(cut)";  // GitHub takes up to 65,536 characters
  s+=d;s+="\n";
  return s;
}
