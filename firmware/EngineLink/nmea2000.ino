// NMEA 2000, listen only: GPS, depth, log, heading, temperatures, battery,
// rudder, wind and tanks from the boat's instruments (nmea2k.h decodes), a
// census of every device and message on the bus, and a per-trip navigation
// log beside the engine log. The CAN controller runs in listen-only mode:
// EngineLink never sends anything on NMEA 2000.

uint32_t nmeaPGN(uint32_t canid){return n2k::pgnOf(canid);}
bool startCAN(){
  if(canReady)return true;
  twai_general_config_t gen=TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)CAN_TX,(gpio_num_t)CAN_RX,TWAI_MODE_LISTEN_ONLY);
  gen.rx_queue_len=64;
  twai_timing_config_t timing=TWAI_TIMING_CONFIG_250KBITS();
  twai_filter_config_t filter=TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if(twai_driver_install(&gen,&timing,&filter)!=ESP_OK)return false;
  if(twai_start()!=ESP_OK){twai_driver_uninstall();return false;}
  canReady=true;return true;
}
void stopCAN(){if(canReady){twai_stop();twai_driver_uninstall();canReady=false;}}
void serviceCAN(){
  if(!canReady)return;
  twai_message_t m;
  const uint32_t now=millis();
  // Bounded per loop pass: a busy bus can never hold the loop up.
  for(int k=0;k<24;k++){
    if(twai_receive(&m,0)!=ESP_OK)break;
    ++ctr.nmeaFrames;
    if(!m.extd||m.rtr){++navErrFrames;continue;}
    const uint32_t pgn=n2k::pgnOf(m.identifier);const uint8_t src=n2k::srcOf(m.identifier);
    navSeen.note(pgn,src,now);
    if(pgn==60928){navSeen.claim(src,m.data,m.data_length_code);continue;}
    bool ok;
    if(n2k::isFast(pgn)){const n2k::Fast *f=navFast.feed(pgn,src,m.data,m.data_length_code,now);if(!f)continue;ok=n2k::decode(pgn,f->buf,f->got,now,nav);}
    else ok=n2k::decode(pgn,m.data,m.data_length_code,now,nav);
    if(!ok)continue;
    ++navFrames;
    if(pgn==129026&&n2k::fresh(nav.sog,now)){sogKnots=nav.sog.v;sogAt=elapsedMs();}  // fuel per nautical mile
    if((pgn==126992||pgn==129029)&&nav.utc>1700000000&&allowTime("gps"))setClockFromUtc((time_t)nav.utc,"gps");
  }
}
// ---- one navigation record per second of a trip (/TRIPS/Tnnnnnn.NAV) ----
// Beside the engine log, not inside it, so trips recorded before 0.16 keep
// their format. Matched to the engine log by uptime in the export.
static uint16_t nv16(const n2k::Val &x,float k,uint32_t now){return n2k::fresh(x,now)&&x.v>=0&&x.v*k<65535?(uint16_t)lroundf(x.v*k):0xFFFF;}
static int16_t ns16(const n2k::Val &x,float k,uint32_t now){return n2k::fresh(x,now)&&fabsf(x.v*k)<32767?(int16_t)lroundf(x.v*k):INT16_MIN;}
void tripNavWrite(uint32_t up){
  if(!canReady||!navFrames||!sdReady||!cardInserted()||!totals.tripActive)return;
  if(!tripNav||tripNavId!=totals.nextTripId){
    if(tripNav)tripNav.close();
    if(!ensureDir("/TRIPS"))return;
    char name[32];snprintf(name,sizeof(name),"/TRIPS/T%06lu.NAV",(unsigned long)totals.nextTripId);
    const bool fresh=!SD.exists(name);tripNav=SD.open(name,FILE_APPEND);if(!tripNav)return;
    if(fresh){const uint32_t mg=NAV_MAGIC;tripNav.write((const uint8_t*)&mg,sizeof(mg));}
    tripNavId=totals.nextTripId;
  }
  const uint32_t now=millis();NavRec r;
  r.up=up;
  const bool pos=n2k::fresh(nav.lat,now)&&n2k::fresh(nav.lon,now);
  r.lat=pos?(int32_t)lround(nav.lat.v*1e7):INT32_MIN;r.lon=pos?(int32_t)lround(nav.lon.v*1e7):INT32_MIN;
  r.sog=nv16(nav.sog,100,now);r.cog=nv16(nav.cog,100,now);r.hdg=nv16(nav.hdg,100,now);r.stw=nv16(nav.stw,100,now);
  r.depth=n2k::fresh(nav.depth,now)?(uint32_t)lroundf(nav.depth.v*100):0xFFFFFFFF;
  r.water=ns16(nav.waterC,100,now);r.rudder=ns16(nav.rudder,100,now);r.batt=nv16(nav.battV[0],100,now);
  r.wind=nv16(nav.windKn,100,now);r.windAng=nv16(nav.windDeg,10,now);
  for(uint8_t i=0;i<2;i++)r.tank[i]=n2k::fresh(nav.tankPct[i],now)?(uint8_t)lroundf(constrain(nav.tankPct[i].v,0.0f,100.0f)*2):0xFF;
  r.sats=n2k::fresh(nav.sats,now)?(uint8_t)nav.sats.v:0xFF;r.flags=(nav.hdgMag?1:0)|(nav.windTrue?2:0);
  tripNav.write((const uint8_t*)&r,sizeof(r));
  if((uint32_t)(millis()-tripNavFlush)>10000){tripNav.flush();tripNavFlush=millis();}
}
void tripNavClose(){if(tripNav)tripNav.close();tripNavId=0;}
// Navigation columns for the second-by-second export.
void navCsv(String &s,const NavRec *r){
  if(!r){s+=",,,,,,,,,,,,,,,";return;}
  auto u=[&](uint16_t v,float k,int dec){s+=',';if(v!=0xFFFF)s+=String(v/k,dec);};
  auto i16=[&](int16_t v,float k,int dec){s+=',';if(v!=INT16_MIN)s+=String(v/k,dec);};
  s+=',';if(r->lat!=INT32_MIN)s+=String(r->lat/1e7,6);s+=',';if(r->lon!=INT32_MIN)s+=String(r->lon/1e7,6);
  u(r->sog,100,2);u(r->cog,100,1);u(r->hdg,100,1);u(r->stw,100,2);
  s+=',';if(r->depth!=0xFFFFFFFF)s+=String(r->depth/100.0,2);
  i16(r->water,100,1);i16(r->rudder,100,1);u(r->batt,100,2);u(r->wind,100,1);u(r->windAng,10,0);
  for(uint8_t i=0;i<2;i++){s+=',';if(r->tank[i]!=0xFF)s+=String(r->tank[i]/2.0f,1);}
  s+=',';if(r->sats!=0xFF)s+=r->sats;
}
// ---- JSON: live values (in /api/live) and the bus census (/api/nmea) ----
void navJson(String &s){
  const uint32_t now=millis();
  auto v=[&](const char *k,const n2k::Val &x,int d){if(n2k::fresh(x,now)){s+=",\"";s+=k;s+="\":";jnum(s,x.v,d);}};
  s+="{\"on\":";s+=canReady?"true":"false";s+=",\"frames\":";s+=ctr.nmeaFrames;
  v("lat",nav.lat,6);v("lon",nav.lon,6);v("sog",nav.sog,1);v("cog",nav.cog,0);v("hdg",nav.hdg,0);v("stw",nav.stw,1);v("depth",nav.depth,1);
  v("water",nav.waterC,1);v("air",nav.airC,1);v("rudder",nav.rudder,1);v("batt",nav.battV[0],2);v("batt2",nav.battV[1],2);v("amps",nav.battA[0],1);
  v("wind",nav.windKn,1);v("windDeg",nav.windDeg,0);v("tank0",nav.tankPct[0],0);v("tank1",nav.tankPct[1],0);v("log",nav.logNm,1);v("trip",nav.tripNm,1);v("sats",nav.sats,0);v("hdop",nav.hdop,1);
  v("rpm0",nav.engRpm[0],0);v("rpm1",nav.engRpm[1],0);
  if(n2k::fresh(nav.hdg,now)){s+=",\"hdgMag\":";s+=nav.hdgMag?"true":"false";}
  s+='}';
}
void handleNmea(){
  String s;s.reserve(4000);const uint32_t now=millis();
  s+="{\"nav\":";navJson(s);
  twai_status_info_t st;const bool got=canReady&&twai_get_status_info(&st)==ESP_OK;
  s+=",\"bus\":{\"state\":";jsonString(s,!canReady?"off":!got?"unknown":st.state==TWAI_STATE_RUNNING?"running":st.state==TWAI_STATE_BUS_OFF?"bus off":st.state==TWAI_STATE_RECOVERING?"recovering":"stopped");
  if(got){s+=",\"rxErr\":";s+=st.rx_error_counter;s+=",\"missed\":";s+=st.rx_missed_count;s+=",\"overrun\":";s+=st.rx_overrun_count;s+=",\"busErr\":";s+=st.bus_error_count;s+=",\"queued\":";s+=st.msgs_to_rx;}
  s+=",\"frames\":";s+=ctr.nmeaFrames;s+=",\"used\":";s+=navFrames;s+=",\"bad\":";s+=navErrFrames;s+=",\"dropped\":";s+=navSeen.dropped;s+='}';
  s+=",\"seen\":[";
  for(uint8_t i=0;i<navSeen.n;i++){const n2k::Seen &e=navSeen.e[i];if(i)s+=',';
    const uint32_t span=e.lastAt-e.firstAt;
    s+="{\"src\":";s+=e.src;s+=",\"pgn\":";s+=e.pgn;s+=",\"n\":";s+=e.count;s+=",\"ago\":";s+=(now-e.lastAt)/1000;
    s+=",\"hz\":";jnum(s,span>1000&&e.count>1?(e.count-1)*1000.0f/span:NAN,1);
    const char *nm=n2k::pgnName(e.pgn);s+=",\"name\":";jsonString(s,nm?nm:"");
    const uint16_t mk=navSeen.maker[e.src];s+=",\"maker\":";if(mk!=0xFFFF){const char *m=n2k::makerName(mk);if(m)jsonString(s,m);else{s+='"';s+="maker ";s+=mk;s+='"';}}else s+="null";
    s+='}';}
  s+="]}";
  sendJSON(200,s);
}
