// What EngineLink measures about its networks by itself, layer by layer
// (docs/WIFI_PLAN_2026-11-09.md sections 8 and 9). A page that does not open
// says nothing about where it fails, and the report can only be fetched
// afterwards on the own Wi-Fi, so every router session leaves a record:
//   radio:  the router as the driver saw it, the signal over time, and a
//           2 s sample of received frames every 30 s (beacons, repeats,
//           40 MHz, noise floor)
//   IP:     one ping to the router every 2 s (esp_ping, lwIP's documented API)
//   HTTP:   every connection and request by network (httpd.ino, netstat.h)
// plus scans after the router was given up, slow steps of the network part
// and the tasks' CPU time and stack. Documented ESP-IDF APIs only; nothing
// here connects, disconnects or changes a station setting. The two decisions
// taken from the link check (netstat.h: linkPoor, linkDead) are acted on in
// wifi.ino.

const char* reasonText(uint8_t r){
  switch(r){
    case 0:return "No reason reported";
    case 2:return "Router did not answer the authentication";
    case 4:return "Router dropped EngineLink for inactivity";
    case 8:return "Station deliberately left";
    case 15:return "WPA handshake timeout";
    case 36:return "Station left (mode change or a new attempt)";
    case 200:return "Router beacons lost";
    case 201:return "Router not found";
    case 202:return "Authentication failed (does not prove a wrong password)";
    case 203:return "Router association failed";
    case 204:return "WPA handshake failed";
    case 205:return "Router connection failed";
    case 209:return "WPA3 PMF SA Query timeout (router did not answer)";
    case 210:case 211:return "Incompatible Wi-Fi security";
    case netstat::END_REJOIN:return "EngineLink's link check: nothing got through for a minute, joined again";
    case netstat::END_GIVEN_UP:return "EngineLink's link check: dead again after joining again, own Wi-Fi on";
    default:return "Other Wi-Fi disconnect";
  }
}
const char* traceText(uint8_t t){
  switch(t){
    case 1:return "Connect started";
    case 2:return "Associated with router";
    case 3:return "Received router IP";
    case 4:return "Disconnected";
    case 5:return "Lost router IP";
    case 6:return "Rescue AP fallback";
    case 7:return "Phone joined EngineLink Wi-Fi";
    case 8:return "Phone left EngineLink Wi-Fi";
    case 9:return "Phone received an IP from EngineLink DHCP";
    default:return "Network event";
  }
}
const char* stationSecurityName(uint8_t mode){
  switch(mode){
    case WIFI_AUTH_OPEN: return "Open";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA-Personal";
    case WIFI_AUTH_WPA2_PSK: return "WPA2-Personal";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2-Personal";
    case WIFI_AUTH_WPA3_PSK: return "WPA3-SAE";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3 mixed";
    default: return "Not yet negotiated";
  }
}

// The stored RF calibration is discarded once, so this start calibrates in
// full (esp_phy_init.h). It was made while the 3.3 V regulator had a cold
// solder joint (repaired 2026-11-09, docs/HARDWARE.md); the driver otherwise
// reuses it for ever. Runs before the Wi-Fi starts; settings are untouched.
void rfCalibrateOnce(){
  Preferences m;
  if(!m.begin("el_radio",false))return;
  if(m.getUChar("cal",0)<1){rfRecalAtBoot=esp_phy_erase_cal_data_in_nvs()==ESP_OK;m.putUChar("cal",1);}
  m.end();
}

// ---- Driver events and memory: counters only ---------------------------------
static volatile uint32_t allocFails=0,allocFailSize=0,allocFailCaps=0;
static void allocFailed(size_t size,uint32_t caps,const char *){allocFails=allocFails+1;allocFailSize=size;allocFailCaps=caps;}
static void diagWifiEvent(void *,esp_event_base_t,int32_t id,void *){
  if(id==WIFI_EVENT_STA_BEACON_TIMEOUT)staBeaconTimeouts=staBeaconTimeouts+1;
  else if(id==WIFI_EVENT_HOME_CHANNEL_CHANGE)staChannelChanges=staChannelChanges+1;
}
void netdiagInit(){
  heap_caps_register_failed_alloc_callback(allocFailed);
  esp_event_handler_register(WIFI_EVENT,WIFI_EVENT_STA_BEACON_TIMEOUT,diagWifiEvent,nullptr);
  esp_event_handler_register(WIFI_EVENT,WIFI_EVENT_HOME_CHANNEL_CHANGE,diagWifiEvent,nullptr);
}
// A step of the network part that took 100 ms or more (the six newest).
void netStep(const char *what,uint32_t startedMs){
  const uint32_t ms=millis()-startedMs;
  if(ms<100)return;
  netSteps[netStepHead]={what,ms,startedMs};
  netStepHead=(netStepHead+1)%netstat::STEP_CAP;if(netStepSize<netstat::STEP_CAP)++netStepSize;
}

// ---- The web server by network (called from httpd.ino) -----------------------
struct SlotAcc{uint32_t pingSent,pingOk,rttSum,requests,failed,bytes,beacons,beaconsExp,toUs,retries,wide,ht,group,others;int rssi,noise;};
static SlotAcc acc={};
static uint32_t slotAt=0;
// what: 0 a connection closed, 1 opened, 2 a request arrived on it (returns
// its number on that connection).
uint8_t httpConn(int fd,uint8_t what){
  const int slot=fd-LWIP_SOCKET_OFFSET;
  uint8_t n=netstat::NET_OWN,nth=0;
  if(what==1){
    struct sockaddr_storage a={};socklen_t al=sizeof(a);
    if(getsockname(fd,(struct sockaddr *)&a,&al)==0){
      const uint32_t ip=ip4Of(a);
      n=ip==lwip_htonl(0xC0A80401)?netstat::NET_OWN:ip==lwip_htonl(0x7F000001)?netstat::NET_SELF:netstat::NET_ROUTER;
    }
  }
  portENTER_CRITICAL(&httpStatMux);
  if(what==1)httpStat.opened(slot,n);else if(what==2)nth=httpStat.began(slot);else httpStat.closed(slot);
  portEXIT_CRITICAL(&httpStatMux);
  return nth;
}
void httpRecord(WebCtx &c,uint32_t ms){
  const uint8_t n=!c.external?netstat::NET_SELF:c.onAp?netstat::NET_OWN:netstat::NET_ROUTER;
  const uint32_t now=millis();
  portENTER_CRITICAL(&httpStatMux);
  httpStat.finished(n,c.r->uri,(uint16_t)c.code,c.failed,c.bytes,ms,c.firstMs,now,c.nth);
  if(n==netstat::NET_ROUTER){++acc.requests;if(c.failed)++acc.failed;acc.bytes+=c.bytes;}
  portEXIT_CRITICAL(&httpStatMux);
}
static netstat::HttpNet routerHttpNow(){
  portENTER_CRITICAL(&httpStatMux);
  const netstat::HttpNet h=httpStat.net[netstat::NET_ROUTER];
  portEXIT_CRITICAL(&httpStatMux);
  return h;
}

// ---- Radio sample: 2 s of every 30 s in a router session ---------------------
// Promiscuous mode (esp_wifi.h) hands every management and data frame on the
// channel to this callback, in the Wi-Fi task: it only compares addresses and
// counts. Beacons need no answer from EngineLink, so "beacons heard" is the
// router -> EngineLink direction alone; a repeat (retry bit) of a frame for
// EngineLink means the router did not get the acknowledgement of the first.
struct RadioAcc{volatile uint32_t beacons,toUs,retries,wide,ht,ampdu,group,others,noiseN;volatile int32_t noiseSum;volatile int noiseMax;};
static RadioAcc racc={};
static uint8_t radioBssid[6]={},radioMac[6]={};
static bool radioOn=false;
static uint32_t radioStartedAt=0,radioNextAt=0;
static void radioCb(void *buf,wifi_promiscuous_pkt_type_t type){
  if(type!=WIFI_PKT_MGMT&&type!=WIFI_PKT_DATA)return;
  const wifi_promiscuous_pkt_t *p=(const wifi_promiscuous_pkt_t *)buf;
  if(p->rx_ctrl.sig_len<28)return;   // 24-byte header and the checksum
  const uint8_t *f=p->payload;
  if(memcmp(f+10,radioBssid,6)!=0){racc.others=racc.others+1;return;}   // not sent by our router
  const int nf=p->rx_ctrl.noise_floor;
  racc.noiseSum=racc.noiseSum+nf;racc.noiseN=racc.noiseN+1;if(nf>racc.noiseMax)racc.noiseMax=nf;
  if(type==WIFI_PKT_MGMT){if((f[0]&0xFC)==0x80)racc.beacons=racc.beacons+1;return;}
  if(f[4]&1){racc.group=racc.group+1;return;}   // broadcast or multicast
  if(memcmp(f+4,radioMac,6)!=0)return;          // for another device
  racc.toUs=racc.toUs+1;
  if(f[1]&0x08)racc.retries=racc.retries+1;
  if(p->rx_ctrl.sig_mode==1){
    racc.ht=racc.ht+1;
    if(p->rx_ctrl.cwb)racc.wide=racc.wide+1;
    if(p->rx_ctrl.aggregation)racc.ampdu=racc.ampdu+1;
  }
}
static void radioStop(){
  if(!radioOn)return;
  radioOn=false;
  esp_wifi_set_promiscuous(false);
  netstat::RouterSession &r=rsessHist[rsessCur];
  const uint32_t expected=(millis()-radioStartedAt)*1000UL/102400UL;   // a beacon every 102.4 ms
  ++r.rWindows;r.rBeacons+=racc.beacons;r.rBeaconsExp+=expected;r.rToUs+=racc.toUs;r.rRetries+=racc.retries;r.rWide+=racc.wide;
  r.rHt+=racc.ht;r.rAmpdu+=racc.ampdu;r.rGroup+=racc.group;r.rOthers+=racc.others;
  r.rNoiseSum+=racc.noiseSum;r.rNoiseN+=racc.noiseN;if(racc.noiseN&&racc.noiseMax>r.rNoiseMax)r.rNoiseMax=racc.noiseMax;
  acc.beacons+=racc.beacons;acc.beaconsExp+=expected;acc.toUs+=racc.toUs;acc.retries+=racc.retries;acc.wide+=racc.wide;acc.ht+=racc.ht;
  acc.group+=racc.group;acc.others+=racc.others;if(racc.noiseN)acc.noise=racc.noiseSum/(int32_t)racc.noiseN;
}
static void radioStart(uint32_t now){
  racc.beacons=racc.toUs=racc.retries=racc.wide=racc.ht=racc.ampdu=racc.group=racc.others=racc.noiseN=0;racc.noiseSum=0;racc.noiseMax=-127;
  const wifi_promiscuous_filter_t f={WIFI_PROMIS_FILTER_MASK_MGMT|WIFI_PROMIS_FILTER_MASK_DATA};
  radioNextAt=now+30000UL;
  if(esp_wifi_set_promiscuous_filter(&f)!=ESP_OK||esp_wifi_set_promiscuous_rx_cb(radioCb)!=ESP_OK||esp_wifi_set_promiscuous(true)!=ESP_OK)return;
  radioOn=true;radioStartedAt=now;
}

// ---- Link check and the session record ---------------------------------------
static esp_ping_handle_t linkPing=nullptr;
static bool endPending=false;   // a lost session waits for the driver's reason
static void linkPingDone(esp_ping_handle_t h,bool ok){
  netstat::RouterSession &r=rsessHist[rsessCur];
  if(!r.active)return;
  if(ok){
    uint32_t ms=0;esp_ping_get_profile(h,ESP_PING_PROF_TIMEGAP,&ms,sizeof(ms));
    r.ping.answered(ms);routerAnswersPings=true;++acc.pingOk;acc.rttSum+=ms;
  }else r.ping.lost();
  ++acc.pingSent;
  if(r.ping.sent%5==0){   // the signal every 10 s
    int v=0;
    if(esp_wifi_sta_get_rssi(&v)==ESP_OK&&v<0){
      r.rssiLast=v;r.rssiSum+=v;++r.rssiN;if(v<r.rssiMin)r.rssiMin=v;if(v>r.rssiMax)r.rssiMax=v;acc.rssi=v;
    }
  }
}
static void linkPingOk(esp_ping_handle_t h,void *){linkPingDone(h,true);}
static void linkPingLost(esp_ping_handle_t h,void *){linkPingDone(h,false);}
static void slotRoll(uint32_t now){
  if(!acc.pingSent&&!acc.requests&&!acc.beaconsExp){slotAt=now;return;}
  netstat::Slot s;
  auto u16=[](uint32_t v){return (uint16_t)(v>65535?65535:v);};
  s.startS=slotAt/1000UL;s.pingSent=u16(acc.pingSent);s.pingOk=u16(acc.pingOk);s.rttAvg=u16(acc.pingOk?acc.rttSum/acc.pingOk:0);
  s.requests=u16(acc.requests);s.failed=u16(acc.failed);s.bytes=acc.bytes;s.rssi=(int8_t)acc.rssi;s.noise=(int8_t)acc.noise;
  s.beacons=u16(acc.beacons);s.beaconsExpected=u16(acc.beaconsExp);s.toUs=u16(acc.toUs);s.retries=u16(acc.retries);
  s.wide=u16(acc.wide);s.ht=u16(acc.ht);s.group=u16(acc.group);s.others=u16(acc.others);
  slots[slotHead]=s;slotHead=(slotHead+1)%netstat::SLOT_CAP;if(slotSize<netstat::SLOT_CAP)++slotSize;
  portENTER_CRITICAL(&httpStatMux);acc={};portEXIT_CRITICAL(&httpStatMux);
  slotAt=now;
}
static netstat::HttpDelta sessionHttp(const netstat::RouterSession &r){return r.active?netstat::httpSince(routerHttpNow(),r.http0):r.http;}
// One line for the SD log.
static void sessionLine(String &s,const netstat::RouterSession &r){
  s+="pings ";s+=r.ping.ok;s+=" of ";s+=r.ping.sent;s+=" answered";
  if(r.ping.ok){s+=", average ";s+=r.ping.avgMs();s+=" ms, longest ";s+=r.ping.maxMs;s+=" ms";}
  s+=", most lost in a row ";s+=r.ping.lostRowMax;
  const netstat::HttpDelta d=sessionHttp(r);
  s+=" · through the router: ";s+=d.conns;s+=" connections (";s+=d.empty;s+=" without a request), ";s+=d.requests;s+=" requests, ";
  s+=d.failed;s+=" failed while sending, ";s+=(uint32_t)d.bytes;s+=" bytes";
  if(r.rWindows){s+=" · beacons heard ";s+=r.rBeacons;s+=" of ";s+=r.rBeaconsExp;s+=", frames to EngineLink ";s+=r.rToUs;s+=" (";s+=r.rRetries;s+=" repeats, ";s+=r.rWide;s+=" at 40 MHz)";}
}
// The reason is known: finish the record, choose the next radio profile, log.
static void sessionClose(uint8_t reason){
  netstat::RouterSession &r=rsessHist[rsessCur];
  r.endReason=reason;
  if(reason&&reason<netstat::END_REJOIN)r.rssiEnd=staDropRssi;
  const bool poor=netstat::linkPoor(r.ping);
  if(poor)staPlainG=!r.plainG;   // the other profile for the next session (plan 9.4)
  String s="Router session ended after ";s+=(r.endMs-r.startMs)/1000UL;s+=" s, code ";s+=(unsigned)reason;
  s+=r.plainG?" (802.11g)":" (802.11n 20 MHz)";
  s+=", signal ";s+=r.rssiStart;s+="/";s+=r.rssiMin;s+="/";s+=r.rssiLast;s+=" dBm (start/lowest/last): ";
  sessionLine(s,r);
  if(poor){s+=" · link poor: the next session uses ";s+=staPlainG?"802.11g":"802.11n 20 MHz";}
  wifiLog(s);
}
// why: netstat::END_OWN (EngineLink changed network itself), END_REJOIN,
// END_GIVEN_UP, or 255 = the link was lost and the driver's reason follows
// in its event (the event may arrive a moment after the loop saw the loss).
void routerSessionEnd(uint8_t why){
  netstat::RouterSession &r=rsessHist[rsessCur];
  if(!r.active)return;
  radioStop();
  if(linkPing){esp_ping_stop(linkPing);esp_ping_delete_session(linkPing);linkPing=nullptr;}
  r.http=netstat::httpSince(routerHttpNow(),r.http0);
  r.active=false;r.endMs=millis();
  slotRoll(r.endMs);
  if(why==255){endPending=true;return;}
  sessionClose(why);
}
// Called from the main loop shortly after the router gave an IP (never from
// the Wi-Fi event callback): one read of the driver, then the link check.
void routerSessionBegin(){
  routerSessionEnd(netstat::END_OWN);
  if(endPending){endPending=false;sessionClose(staSessionEndReason?(uint8_t)staSessionEndReason:(uint8_t)staLastReason);}
  if(rsessCount)rsessCur=(rsessCur+1)%netstat::SESSION_CAP;
  if(rsessCount<netstat::SESSION_CAP)++rsessCount;
  netstat::RouterSession &r=rsessHist[rsessCur];
  r=netstat::RouterSession();r.active=r.seen=true;r.startMs=millis();r.joinMs=staJoinMs;r.dhcpMs=staDhcpMs;
  staSessionEndReason=0;staBeaconTimeouts=0;staChannelChanges=0;
  wifi_ap_record_t ap;
  if(esp_wifi_sta_get_ap_info(&ap)==ESP_OK){
    r.rssiStart=r.rssiMin=r.rssiMax=r.rssiLast=ap.rssi;r.rssiSum=ap.rssi;r.rssiN=1;r.ch=ap.primary;r.second=(uint8_t)ap.second;
    r.b=ap.phy_11b;r.g=ap.phy_11g;r.n=ap.phy_11n;r.pair=(uint8_t)ap.pairwise_cipher;r.group=(uint8_t)ap.group_cipher;
    memcpy(radioBssid,ap.bssid,6);
  }
  esp_wifi_get_mac(WIFI_IF_STA,radioMac);
  wifi_phy_mode_t pm;if(esp_wifi_sta_get_negotiated_phymode(&pm)==ESP_OK)r.phy=(uint8_t)pm;
  wifi_ps_type_t ps;if(esp_wifi_get_ps(&ps)==ESP_OK)r.ps=(uint8_t)ps;
  wifi_bandwidth_t bw;if(esp_wifi_get_bandwidth(WIFI_IF_STA,&bw)==ESP_OK)r.bw=bw==WIFI_BW_HT40?40:20;
  uint8_t proto=0;if(esp_wifi_get_protocol(WIFI_IF_STA,&proto)==ESP_OK){r.proto=proto;r.plainG=!(proto&WIFI_PROTOCOL_11N);}
  int8_t q=0;if(esp_wifi_get_max_tx_power(&q)==ESP_OK)r.txQdbm=q;
  r.ip=(uint32_t)WiFi.localIP();r.mask=(uint32_t)WiFi.subnetMask();r.gw=(uint32_t)WiFi.gatewayIP();r.dns=(uint32_t)WiFi.dnsIP();
  r.http0=routerHttpNow();
  slotHead=slotSize=0;slotAt=r.startMs;radioNextAt=r.startMs+10000UL;
  portENTER_CRITICAL(&httpStatMux);acc={};portEXIT_CRITICAL(&httpStatMux);
  acc.rssi=r.rssiStart;
  esp_ping_config_t c=ESP_PING_DEFAULT_CONFIG();
  const IPAddress gw=WiFi.gatewayIP();
  IP_ADDR4(&c.target_addr,gw[0],gw[1],gw[2],gw[3]);
  c.count=ESP_PING_COUNT_INFINITE;c.interval_ms=2000;c.timeout_ms=1900;c.data_size=32;c.task_stack_size=4096;
  esp_ping_callbacks_t cb={nullptr,linkPingOk,linkPingLost,nullptr};
  if(esp_ping_new_session(&c,&cb,&linkPing)!=ESP_OK||esp_ping_start(linkPing)!=ESP_OK)linkPing=nullptr;
}
// "Connected" but nothing gets through (netstat.h: linkDead).
bool routerLinkDead(uint32_t now){
  const netstat::RouterSession &r=rsessHist[rsessCur];
  if(!r.active)return false;
  const uint32_t last=httpStat.net[netstat::NET_ROUTER].lastMs;
  const uint32_t since=(last&&(int32_t)(last-r.startMs)>=0)?now-last:now-r.startMs;
  return netstat::linkDead(r.ping,routerAnswersPings,since);
}

// ---- Tasks: CPU time over the last half minute and stack never used ---------
struct TaskSnap{UBaseType_t num;uint32_t rt;};
static TaskSnap taskPrev[40];
static uint8_t taskPrevN=0;
static uint32_t taskPrevTotal=0,taskSnapAt=0;
static void taskSnapshot(){
  TaskStatus_t *st=(TaskStatus_t *)malloc(40*sizeof(TaskStatus_t));
  if(!st)return;
  uint32_t total=0;const UBaseType_t n=uxTaskGetSystemState(st,40,&total);
  taskPrevN=(uint8_t)n;taskPrevTotal=total;
  for(UBaseType_t i=0;i<n;i++)taskPrev[i]={st[i].xTaskNumber,(uint32_t)st[i].ulRunTimeCounter};
  free(st);
}
static void appendTasksText(String &s){
  TaskStatus_t *st=(TaskStatus_t *)malloc(40*sizeof(TaskStatus_t));
  if(!st){s+="Tasks: no memory for the list\n";return;}
  uint32_t total=0;const UBaseType_t n=uxTaskGetSystemState(st,40,&total);
  const uint32_t span=total-taskPrevTotal;
  s+="Tasks, CPU share of one core since the last snapshot (at most 30 s ago) / stack never used (bytes):\n ";
  for(UBaseType_t i=0;i<n;i++){
    s+=' ';s+=st[i].pcTaskName;s+=' ';
    bool found=false;
    for(uint8_t k=0;k<taskPrevN&&span;k++)if(taskPrev[k].num==st[i].xTaskNumber){
      s+=(uint32_t)(((uint64_t)((uint32_t)st[i].ulRunTimeCounter-taskPrev[k].rt)*100)/span);found=true;break;
    }
    if(!found)s+='?';
    s+="%/";s+=(uint32_t)st[i].usStackHighWaterMark;s+=i+1<n?" ·":"";
  }
  s+="\n";
  free(st);
}

// Every pass of the main loop (from serviceNetwork).
void netdiagService(uint32_t now,bool online){
  netstat::RouterSession &r=rsessHist[rsessCur];
  if(endPending&&(uint32_t)(now-r.endMs)>=300UL){endPending=false;sessionClose(staSessionEndReason?(uint8_t)staSessionEndReason:(uint8_t)staLastReason);}
  if(r.active&&online){
    r.beaconTimeouts=staBeaconTimeouts;r.channelChanges=staChannelChanges;
    if(radioOn){if((uint32_t)(now-radioStartedAt)>=2000UL)radioStop();}
    else if((int32_t)(now-radioNextAt)>=0)radioStart(now);
    if((uint32_t)(now-slotAt)>=30000UL){
      slotRoll(now);
      // Five good minutes after a rejoin: the link check may act again.
      if(linkRejoined&&r.ping.sent>=150&&r.ping.lossPct()<20)linkRejoined=false;
    }
  }
  if((uint32_t)(now-taskSnapAt)>=30000UL){taskSnapAt=now;taskSnapshot();}
}

// ---- Scans after the router was given up (wifi.ino starts them) -------------
// Neighbours heard but not the router: the router's 2.4 GHz radio is away.
// Nothing heard at all: EngineLink's receiver. The channels tell whether the
// router's own channel is the silent one.
static void surveyLine(String &s,const netstat::Survey &v){
  if(v.failed){s+="the scan failed";return;}
  s+=v.heard;s+=" networks heard, the router ";
  if(v.routerSeen){s+="heard on channel ";s+=v.routerCh;s+=" at ";s+=v.routerRssi;s+=" dBm";}else s+="NOT heard";
  s+=" · others by channel (count, strongest dBm):";
  bool any=false;
  for(uint8_t c=1;c<15;c++)if(v.chCount[c]){any=true;s+=" ch";s+=c;s+=": ";s+=v.chCount[c];s+=" (";s+=(int)v.chBest[c];s+=")";}
  if(!any)s+=" none";
}
void surveyRecord(){
  netstat::Survey v;
  v.atMs=millis();v.failed=scanCount<0;v.heard=scanRawCount;
  for(uint8_t i=0;i<scanEntriesCount;i++)netstat::surveyAdd(v,scanEntries[i].ssid==cfg.ssid,scanEntries[i].channel,scanEntries[i].rssi);
  surveys[surveyHead]=v;surveyHead=(surveyHead+1)%netstat::SURVEY_CAP;if(surveySize<netstat::SURVEY_CAP)++surveySize;
  if(++surveyRound<3)surveyAt=millis()+(surveyRound==1?55000UL:120000UL);
  String s="Scan after giving up the router: ";surveyLine(s,v);wifiLog(s);
}

// ---- Report text and JSON -------------------------------------------------------
static const char *phyName(uint8_t p){
  return p==WIFI_PHY_MODE_11B?"802.11b (slow)":p==WIFI_PHY_MODE_11G?"802.11g":p==WIFI_PHY_MODE_HT20?"802.11n 20 MHz":p==WIFI_PHY_MODE_HT40?"802.11n 40 MHz":p==WIFI_PHY_MODE_HE20?"802.11ax":"not known";
}
static const char *cipherName(uint8_t c){
  static const char *n[]={"none","WEP40","WEP104","TKIP","CCMP (AES)","TKIP+CCMP","AES-CMAC","SMS4","GCMP","GCMP-256","AES-GMAC-128","AES-GMAC-256"};
  return c<12?n[c]:"not known";
}
static uint32_t pct(uint32_t part,uint32_t whole){return whole?(uint32_t)(((uint64_t)part*100+whole/2)/whole):0;}
static void sessionText(String &s,const netstat::RouterSession &r,uint8_t nr){
  s+="#";s+=nr;s+=r.active?" RUNNING":" ended";s+=": started at uptime ";s+=r.startMs/1000UL;s+=" s (joined in ";s+=r.joinMs;s+=" ms, address after ";s+=r.dhcpMs;s+=" ms), ";
  if(r.active){s+="running for ";s+=(millis()-r.startMs)/1000UL;s+=" s\n";}
  else{
    s+="lasted ";s+=(r.endMs-r.startMs)/1000UL;s+=" s, ended with code ";s+=(unsigned)r.endReason;s+=" (";
    s+=r.endReason?reasonText(r.endReason):"EngineLink changed network itself";s+=")";
    if(r.rssiEnd){s+=", signal at the loss ";s+=r.rssiEnd;s+=" dBm";}
    s+="\n";
  }
  s+="   Radio: EngineLink offered ";s+=r.plainG?"802.11g (no 802.11n)":"802.11n";s+=", ";s+=r.bw;s+=" MHz · negotiated ";s+=phyName(r.phy);
  s+=" · router on channel ";s+=r.ch;s+=r.second==WIFI_SECOND_CHAN_NONE?" (20 MHz)":r.second==WIFI_SECOND_CHAN_ABOVE?" (itself 40 MHz, second channel above)":" (itself 40 MHz, second channel below)";
  s+=", supports ";s+=r.b?"b ":"";s+=r.g?"g ":"";s+=r.n?"n ":"";s+="· encryption pairwise ";s+=cipherName(r.pair);s+=", group ";s+=cipherName(r.group);
  s+=" · power save ";s+=r.ps==WIFI_PS_NONE?"off":r.ps==255?"not read":"ON (modem sleep)";s+=" · transmit power ";s+=r.txQdbm/4.0f;s+=" dBm\n";
  s+="   Addresses: EngineLink ";s+=IPAddress(r.ip).toString();s+=" · mask ";s+=IPAddress(r.mask).toString();s+=" · gateway ";s+=IPAddress(r.gw).toString();s+=" · DNS ";s+=IPAddress(r.dns).toString();s+="\n";
  s+="   Signal (dBm, every 10 s): start ";s+=r.rssiStart;s+=", lowest ";s+=r.rssiMin;s+=", highest ";s+=r.rssiMax;s+=", average ";s+=r.rssiN?(int)(r.rssiSum/(int32_t)r.rssiN):0;s+=", last ";s+=r.rssiLast;s+="\n";
  s+="   Link check (ping to the router every 2 s): ";s+=r.ping.ok;s+=" of ";s+=r.ping.sent;s+=" answered (";s+=r.ping.lossPct();s+=" % lost)";
  if(r.ping.ok){s+=", average ";s+=r.ping.avgMs();s+=" ms, longest ";s+=r.ping.maxMs;s+=" ms";}
  s+=", most lost in a row ";s+=r.ping.lostRowMax;s+=" (";s+=r.ping.lostRowMax*2;s+=" s) · answered in <10/<50/<200/<1000/1000+ ms: ";
  for(uint8_t i=0;i<5;i++){if(i)s+='/';s+=r.ping.bucket[i];}
  s+="\n";
  s+="   Driver: beacon-timeout warnings ";s+=r.beaconTimeouts;s+=" · channel or width changes ";s+=r.channelChanges;s+="\n";
  const netstat::HttpDelta d=sessionHttp(r);
  s+="   Web through the router: connections ";s+=d.conns;s+=" (";s+=d.empty;s+=" closed without a request, ";s+=d.reused;s+=" used for more than one) · requests ";s+=d.requests;
  s+=" · status 2xx ";s+=d.byClass[0];s+=", 3xx ";s+=d.byClass[1];s+=", 4xx ";s+=d.byClass[2];s+=", 5xx ";s+=d.byClass[3];
  s+=" · failed while sending ";s+=d.failed;s+=" · bytes sent ";s+=(uint32_t)d.bytes;s+="\n";
  s+="   Radio samples (2 s of every 30 s, ";s+=r.rWindows;s+=" taken): ";
  if(!r.rWindows)s+="none\n";
  else{
    s+="beacons heard ";s+=r.rBeacons;s+=" of about ";s+=r.rBeaconsExp;s+=" · frames from the router for EngineLink ";s+=r.rToUs;
    s+=", of them repeats ";s+=r.rRetries;s+=" (";s+=pct(r.rRetries,r.rToUs);s+=" %), 802.11n ";s+=r.rHt;s+=", 40 MHz ";s+=r.rWide;s+=", aggregated ";s+=r.rAmpdu;
    s+=" · the router's broadcasts ";s+=r.rGroup;s+=" · frames of other senders on the channel ";s+=r.rOthers;
    s+=" · noise floor average ";s+=r.rNoiseN?(int)(r.rNoiseSum/(int32_t)r.rNoiseN):0;s+=" dBm, highest ";s+=r.rNoiseN?r.rNoiseMax:0;s+="\n";
  }
}
void appendRouterDiagText(String &s){
  s+="ROUTER SESSIONS AS ENGINELINK MEASURED THEM (newest first, up to 4 kept since the start)\n";
  if(!rsessCount)s+="No router session with an IP address since the start.\n";
  for(uint8_t i=0;i<rsessCount;i++)sessionText(s,rsessHist[(rsessCur+netstat::SESSION_CAP-i)%netstat::SESSION_CAP],rsessCount-i);
  s+="Next router session offers: ";s+=staPlainG?"802.11g":"802.11n";s+=", 20 MHz · link check made EngineLink join again ";s+=linkRejoins;s+=" time(s), give up to the own Wi-Fi ";s+=linkGiveUps;
  s+=" · this router answers pings: ";s+=routerAnswersPings?"yes":"not seen yet";s+="\n";
  s+="RF calibration: ";s+=rfRecalAtBoot?"made in full at this start (once, after the regulator repair)":"stored one used";s+="\n";
  s+="Reading it: beacons heard but pings lost = the router's data frames do not arrive or EngineLink's are not heard\n"
     "(repeats tell which: many repeats = the router does not get EngineLink's acknowledgements). Beacons missing =\n"
     "the router's signal itself. Pings fine but connections without a request, or none = the other device's packets\n"
     "do not reach EngineLink. Requests answered but connections never reused = the answers do not reach the device.\n\n";
  if(slotSize){
    s+="NEWEST SESSION IN 30 s PIECES (uptime · pings answered/sent, average ms · signal · web requests, failed, bytes · sample: beacons heard/expected, frames for EngineLink, repeats, 40 MHz, 802.11n, broadcasts, other senders, noise)\n";
    for(uint8_t i=0;i<slotSize;i++){
      const netstat::Slot &x=slots[(slotHead+netstat::SLOT_CAP-slotSize+i)%netstat::SLOT_CAP];
      s+="  +";s+=x.startS;s+="s · ";s+=x.pingOk;s+="/";s+=x.pingSent;s+=", ";s+=x.rttAvg;s+=" ms · ";s+=(int)x.rssi;s+=" dBm · ";
      s+=x.requests;s+=", ";s+=x.failed;s+=", ";s+=x.bytes;s+=" B";
      if(x.beaconsExpected){s+=" · ";s+=x.beacons;s+="/";s+=x.beaconsExpected;s+=", ";s+=x.toUs;s+=", ";s+=x.retries;s+=", ";s+=x.wide;s+=", ";s+=x.ht;s+=", ";s+=x.group;s+=", ";s+=x.others;s+=", ";s+=(int)x.noise;s+=" dBm";}
      s+="\n";
    }
    s+="\n";
  }
  s+="SCANS AFTER THE ROUTER WAS GIVEN UP (5 s after, then about 1 and 3 minutes while no phone is joined; newest first)\n";
  if(!surveySize)s+="None yet.\n";
  for(uint8_t i=0;i<surveySize;i++){
    const netstat::Survey &v=surveys[(surveyHead+netstat::SURVEY_CAP-1-i)%netstat::SURVEY_CAP];
    s+="  at uptime ";s+=v.atMs/1000UL;s+=" s: ";surveyLine(s,v);s+="\n";
  }
  s+="\n";
}
static const char *netName(uint8_t n){return n==netstat::NET_ROUTER?"router":n==netstat::NET_OWN?"own Wi-Fi":"self-test";}
void appendHttpDiagText(String &s){
  static const char *ep[netstat::EP_N]={"/","/api/health","/api/live","other"};
  for(uint8_t n=0;n<netstat::NET_N;n++){
    portENTER_CRITICAL(&httpStatMux);const netstat::HttpNet h=httpStat.net[n];portEXIT_CRITICAL(&httpStatMux);
    s+=n==netstat::NET_ROUTER?"Through the router":n==netstat::NET_OWN?"On the own Wi-Fi":"Own self-test";
    s+=": connections ";s+=h.conns;s+=" (";s+=h.connsEmpty;s+=" closed without a request, ";s+=h.connsReused;s+=" used for more than one) · requests ";s+=h.requests;
    s+=" · 2xx ";s+=h.byClass[0];s+=", 3xx ";s+=h.byClass[1];s+=", 4xx ";s+=h.byClass[2];s+=", 5xx ";s+=h.byClass[3];
    s+=" · failed while sending ";s+=h.failed;s+=" · bytes ";s+=(uint32_t)(h.bytes/1024);s+=" KB · slowest ";s+=h.slowMs;s+=" ms (";s+=h.slowPath;s+=")";
    if(h.requests){s+=" · last at uptime ";s+=h.lastMs/1000UL;s+=" s";}
    s+="\n";
    if(n==netstat::NET_SELF||!h.requests)continue;
    s+="   by address (requests, failed, slowest ms):";
    for(uint8_t e=0;e<netstat::EP_N;e++){s+=' ';s+=ep[e];s+=' ';s+=h.ep[e].n;s+='/';s+=h.ep[e].failed;s+='/';s+=h.ep[e].maxMs;s+=e+1<netstat::EP_N?" ·":"";}
    s+="\n";
  }
  s+="Request log (every request through the router, and failed or slow ones on the own Wi-Fi; newest ";s+=netstat::REQ_CAP;s+=" of ";s+=httpStat.logged;s+="):\n";
  if(!httpStat.size)s+="  none\n";
  for(uint8_t i=0;i<httpStat.size;i++){
    portENTER_CRITICAL(&httpStatMux);const netstat::Req q=httpStat.at(i);portEXIT_CRITICAL(&httpStatMux);
    s+="  +";s+=q.atMs/1000UL;s+='.';s+=(q.atMs%1000UL)/100UL;s+="s ";s+=netName(q.net);s+=' ';s+=q.path;s+=" -> ";s+=q.status;s+=", ";s+=q.bytes;s+=" B, first byte after ";s+=q.firstMs;
    s+=" ms, done in ";s+=q.ms;s+=" ms";s+=q.failed?", SEND FAILED":"";s+=" (request ";s+=q.nthOnConn;s+=" on its connection)\n";
  }
  s+="A request counts as answered when the server handed the whole answer to TCP. Whether it arrived is seen from the\n"
     "next request on the same connection (\"used for more than one\"); the last 5.7 KB of an answer cannot be confirmed.\n";
}
void appendSystemDiagText(String &s){
  s+="Internal memory: free ";s+=heap_caps_get_free_size(MALLOC_CAP_INTERNAL)/1024;s+=" KB, lowest ";s+=heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)/1024;
  s+=" KB, largest block ";s+=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)/1024;s+=" KB · failed allocations since the start: ";s+=(uint32_t)allocFails;
  if(allocFails){s+=" (last ";s+=(uint32_t)allocFailSize;s+=" bytes, caps 0x";s+=String((uint32_t)allocFailCaps,HEX);s+=")";}
  s+="\n";
  s+="Slow steps of the network part (100 ms or more, newest last):";
  if(!netStepSize)s+=" none";
  for(uint8_t i=0;i<netStepSize;i++){
    const netstat::Step &x=netSteps[(netStepHead+netstat::STEP_CAP-netStepSize+i)%netstat::STEP_CAP];
    s+=i?" · ":" ";s+=x.what;s+=' ';s+=x.ms;s+=" ms at +";s+=x.atMs/1000UL;s+="s";
  }
  s+="\n";
  appendTasksText(s);
  s+="Not available in this build of the ESP32 core (not invented here): lwIP's packet and TCP counters (LWIP_STATS off),\n"
     "the driver's transmit retry counters (no public API), the ARP table (no thread-safe public read).\n";
}
static void sessionJson(String &s,const netstat::RouterSession &r){
  const netstat::HttpDelta d=sessionHttp(r);
  s+="{\"seen\":";s+=r.seen?"true":"false";s+=",\"active\":";s+=r.active?"true":"false";
  s+=",\"startMs\":";s+=r.startMs;s+=",\"endMs\":";s+=r.endMs;s+=",\"joinMs\":";s+=r.joinMs;s+=",\"dhcpMs\":";s+=r.dhcpMs;s+=",\"endReason\":";s+=(unsigned)r.endReason;s+=",\"rssiEnd\":";s+=r.rssiEnd;
  s+=",\"plainG\":";s+=r.plainG?1:0;s+=",\"bw\":";s+=r.bw;s+=",\"proto\":";s+=r.proto;s+=",\"txQdbm\":";s+=(int)r.txQdbm;
  s+=",\"ch\":";s+=r.ch;s+=",\"second\":";s+=r.second;s+=",\"b\":";s+=r.b?1:0;s+=",\"g\":";s+=r.g?1:0;s+=",\"n\":";s+=r.n?1:0;
  s+=",\"phy\":";s+=r.phy;s+=",\"pairwise\":";s+=r.pair;s+=",\"group\":";s+=r.group;s+=",\"ps\":";s+=r.ps;
  s+=",\"ip\":\"";s+=IPAddress(r.ip).toString();s+="\",\"mask\":\"";s+=IPAddress(r.mask).toString();s+="\",\"gw\":\"";s+=IPAddress(r.gw).toString();s+="\",\"dns\":\"";s+=IPAddress(r.dns).toString();s+="\"";
  s+=",\"rssiStart\":";s+=r.rssiStart;s+=",\"rssiMin\":";s+=r.rssiMin;s+=",\"rssiMax\":";s+=r.rssiMax;s+=",\"rssiAvg\":";s+=r.rssiN?(int)(r.rssiSum/(int32_t)r.rssiN):0;s+=",\"rssiLast\":";s+=r.rssiLast;
  s+=",\"pingSent\":";s+=r.ping.sent;s+=",\"pingOk\":";s+=r.ping.ok;s+=",\"pingSumMs\":";s+=r.ping.sumMs;s+=",\"pingMaxMs\":";s+=r.ping.maxMs;s+=",\"pingLostRowMax\":";s+=r.ping.lostRowMax;
  s+=",\"pingBuckets\":[";for(uint8_t i=0;i<5;i++){if(i)s+=',';s+=r.ping.bucket[i];}s+="]";
  s+=",\"beaconTimeouts\":";s+=r.beaconTimeouts;s+=",\"channelChanges\":";s+=r.channelChanges;
  s+=",\"opens\":";s+=d.conns;s+=",\"opensEmpty\":";s+=d.empty;s+=",\"opensReused\":";s+=d.reused;s+=",\"requests\":";s+=d.requests;s+=",\"ok\":";s+=d.byClass[0];
  s+=",\"status\":[";for(uint8_t i=0;i<4;i++){if(i)s+=',';s+=d.byClass[i];}s+="],\"sendFails\":";s+=d.failed;s+=",\"bytes\":";s+=(uint32_t)d.bytes;
  s+=",\"radio\":{\"samples\":";s+=r.rWindows;s+=",\"beacons\":";s+=r.rBeacons;s+=",\"beaconsExpected\":";s+=r.rBeaconsExp;s+=",\"toUs\":";s+=r.rToUs;s+=",\"retries\":";s+=r.rRetries;
  s+=",\"ht\":";s+=r.rHt;s+=",\"wide\":";s+=r.rWide;s+=",\"ampdu\":";s+=r.rAmpdu;s+=",\"group\":";s+=r.rGroup;s+=",\"others\":";s+=r.rOthers;
  s+=",\"noiseAvg\":";s+=r.rNoiseN?(int)(r.rNoiseSum/(int32_t)r.rNoiseN):0;s+=",\"noiseMax\":";s+=r.rNoiseN?r.rNoiseMax:0;s+="}}";
}
// `full` (the report): every session, the 30 s pieces, the scans and the
// request log. Without it (the app's status polls): the newest session only.
void appendRouterDiagJson(String &s,bool full){
  s+=",\"routerSession\":";sessionJson(s,rsessHist[rsessCur]);
  s+=",\"rfRecal\":";s+=rfRecalAtBoot?"true":"false";s+=",\"nextPlainG\":";s+=staPlainG?"true":"false";
  s+=",\"linkRejoins\":";s+=linkRejoins;s+=",\"linkGiveUps\":";s+=linkGiveUps;s+=",\"routerAnswersPings\":";s+=routerAnswersPings?"true":"false";
  s+=",\"allocFails\":";s+=(uint32_t)allocFails;
  if(!full)return;
  s+=",\"routerSessions\":[";
  for(uint8_t i=0;i<rsessCount;i++){if(i)s+=',';sessionJson(s,rsessHist[(rsessCur+netstat::SESSION_CAP-i)%netstat::SESSION_CAP]);}
  s+="],\"slots\":[";
  for(uint8_t i=0;i<slotSize;i++){
    const netstat::Slot &x=slots[(slotHead+netstat::SLOT_CAP-slotSize+i)%netstat::SLOT_CAP];
    if(i)s+=',';
    s+='[';s+=x.startS;s+=',';s+=x.pingSent;s+=',';s+=x.pingOk;s+=',';s+=x.rttAvg;s+=',';s+=(int)x.rssi;s+=',';s+=x.requests;s+=',';s+=x.failed;s+=',';s+=x.bytes;
    s+=',';s+=x.beacons;s+=',';s+=x.beaconsExpected;s+=',';s+=x.toUs;s+=',';s+=x.retries;s+=',';s+=x.wide;s+=',';s+=x.ht;s+=',';s+=x.group;s+=',';s+=x.others;s+=',';s+=(int)x.noise;s+=']';
  }
  s+="],\"slotFields\":\"startS,pingSent,pingOk,rttAvgMs,rssi,requests,failed,bytes,beacons,beaconsExpected,toUs,retries,wide40,ht,group,others,noise\"";
  s+=",\"surveys\":[";
  for(uint8_t i=0;i<surveySize;i++){
    const netstat::Survey &v=surveys[(surveyHead+netstat::SURVEY_CAP-1-i)%netstat::SURVEY_CAP];
    if(i)s+=',';
    s+="{\"atMs\":";s+=v.atMs;s+=",\"failed\":";s+=v.failed?"true":"false";s+=",\"heard\":";s+=v.heard;s+=",\"routerSeen\":";s+=v.routerSeen?"true":"false";
    s+=",\"routerRssi\":";s+=v.routerRssi;s+=",\"routerCh\":";s+=v.routerCh;s+=",\"channels\":[";
    bool any=false;
    for(uint8_t c=1;c<15;c++)if(v.chCount[c]){if(any)s+=',';any=true;s+='[';s+=c;s+=',';s+=v.chCount[c];s+=',';s+=(int)v.chBest[c];s+=']';}
    s+="]}";
  }
  s+="],\"http\":[";
  for(uint8_t n=0;n<netstat::NET_N;n++){
    portENTER_CRITICAL(&httpStatMux);const netstat::HttpNet h=httpStat.net[n];portEXIT_CRITICAL(&httpStatMux);
    if(n)s+=',';
    s+="{\"net\":\"";s+=netName(n);s+="\",\"conns\":";s+=h.conns;s+=",\"connsEmpty\":";s+=h.connsEmpty;s+=",\"connsReused\":";s+=h.connsReused;s+=",\"requests\":";s+=h.requests;
    s+=",\"status\":[";for(uint8_t i=0;i<4;i++){if(i)s+=',';s+=h.byClass[i];}s+="],\"failed\":";s+=h.failed;s+=",\"bytes\":";s+=(uint32_t)h.bytes;
    s+=",\"slowMs\":";s+=h.slowMs;s+=",\"slowPath\":";jsonString(s,String(h.slowPath));s+=",\"lastMs\":";s+=h.lastMs;s+=",\"endpoints\":[";
    for(uint8_t e=0;e<netstat::EP_N;e++){if(e)s+=',';s+='[';s+=h.ep[e].n;s+=',';s+=h.ep[e].failed;s+=',';s+=h.ep[e].maxMs;s+=']';}
    s+="]}";
  }
  s+="],\"requestLog\":[";
  for(uint8_t i=0;i<httpStat.size;i++){
    portENTER_CRITICAL(&httpStatMux);const netstat::Req q=httpStat.at(i);portEXIT_CRITICAL(&httpStatMux);
    if(i)s+=',';
    s+="{\"atMs\":";s+=q.atMs;s+=",\"net\":\"";s+=netName(q.net);s+="\",\"path\":";jsonString(s,String(q.path));s+=",\"status\":";s+=q.status;s+=",\"bytes\":";s+=q.bytes;
    s+=",\"firstMs\":";s+=q.firstMs;s+=",\"ms\":";s+=q.ms;s+=",\"failed\":";s+=q.failed?"true":"false";s+=",\"nth\":";s+=q.nthOnConn;s+='}';
  }
  s+="],\"netSteps\":[";
  for(uint8_t i=0;i<netStepSize;i++){
    const netstat::Step &x=netSteps[(netStepHead+netstat::STEP_CAP-netStepSize+i)%netstat::STEP_CAP];
    if(i)s+=',';
    s+="{\"what\":\"";s+=x.what;s+="\",\"ms\":";s+=x.ms;s+=",\"atMs\":";s+=x.atMs;s+='}';
  }
  s+="]";
}
