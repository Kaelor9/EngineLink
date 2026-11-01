// Status LED: picks the state shown with the patterns from ledpatterns.h.

// ---------- Status LED ----------
ledlang::State ledStateNow(){
  using namespace ledlang;
  if(otaActive||fwState==3)return S_UPDATE;
  if(millis()<3000)return S_BOOT;
  if(diagRules.alarm())return S_ALARM;
  if(sdFaultLatched)return S_SD_ERROR;
  if(lastUARTFaultMs&&(uint32_t)(millis()-lastUARTFaultMs)<30000UL)return S_RX_ERROR;
  if(diagRules.warning())return S_WARNING;
  if(!sdReady)return S_NO_SD;
  if(calActive)return S_TEST;
  if(!cfg.completed)return S_SETUP;
  if(routerConfigured()&&WiFi.status()!=WL_CONNECTED
     &&(routerHold||(staDownSince&&(uint32_t)(millis()-staDownSince)>15000)))return S_ROUTER;
  const uint64_t now=elapsedMs();
  for(uint8_t c=0;c<2;c++)if(kad[c].polling&&rpmFresh(c,now))return S_LIVE;
  if(cfg.protocol!=PROTO_KAD&&(rpmFresh(0,now)||rpmFresh(1,now)))return S_LIVE;
  return S_READY;
}
bool ledTestRunning(){return ledTestStartedMs&&(uint32_t)(millis()-ledTestStartedMs)<LED_DEMO_STEP_MS*ledlang::S_COUNT;}
void updateLED(){
  if((uint32_t)(millis()-lastLed)<40)return;
  lastLed=millis();
  const ledlang::Pattern *p;
  uint32_t t=millis();
  if(ledTestRunning()){
    const uint32_t k=(t-ledTestStartedMs)/LED_DEMO_STEP_MS;
    p=&ledlang::PATTERNS[k];t=(t-ledTestStartedMs)%LED_DEMO_STEP_MS;
  }else if((int32_t)(markLedUntil-t)>0){p=&ledlang::MARK;t=t-(markLedUntil-1600);}
  else p=&ledlang::PATTERNS[ledStateNow()];
  uint32_t rgb;uint8_t level;
  ledlang::at(*p,t,rgb,level);
  const uint32_t fingerprint=(rgb<<8)|level;
  if(fingerprint==ledPrev)return;
  led.setBrightness(level);
  led.setPixelColor(0,led.Color((rgb>>16)&255,(rgb>>8)&255,rgb&255));
  led.show();
  ledPrev=fingerprint;
}
// The LED language for the app's legend.
void handleLed(){
  String s;s.reserve(3200);
  s+="{\"now\":\"";s+=ledlang::PATTERNS[ledStateNow()].id;s+="\",\"demo\":";
  if(ledTestRunning()){s+=(millis()-ledTestStartedMs)/LED_DEMO_STEP_MS;}else s+="null";
  s+=",\"stepMs\":";s+=LED_DEMO_STEP_MS;
  s+=",\"colours\":{";
  for(uint8_t c=1;c<7;c++){if(c>1)s+=',';s+='"';s+=ledlang::COLOUR_NAME[c];s+="\":\"#";char b[8];snprintf(b,sizeof(b),"%06lX",(unsigned long)ledlang::RGB[c]);s+=b;s+='"';}
  s+="},\"patterns\":[";
  for(uint8_t i=0;i<=ledlang::S_COUNT;i++){
    const ledlang::Pattern &p=i<ledlang::S_COUNT?ledlang::PATTERNS[i]:ledlang::MARK;
    if(i)s+=',';
    s+="{\"id\":\"";s+=p.id;s+="\",\"name\":";jsonString(s,p.name);s+=",\"meaning\":";jsonString(s,p.meaning);
    s+=",\"breathe\":";s+=p.breathe?"true":"false";s+=",\"steps\":[";
    for(uint8_t k=0;k<p.n;k++){if(k)s+=',';s+="[\"";s+=ledlang::COLOUR_NAME[p.s[k].c];s+="\",";s+=p.s[k].ms;s+=']';}
    s+="]}";
  }
  s+="]}";
  sendJSON(200,s);
}
