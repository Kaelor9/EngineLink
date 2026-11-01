// Engine reading: J1708 receive tasks, frame checks, SAE J1587 and Volvo
// Penta KAD decoding, read-only KAD polling and the engine task.

void queueFrame(Channel *c){
  if(c->count<2 && !c->overflow){c->count=0;return;}
  Frame f;
  f.channel=c->id;
  f.length=c->count;
  memcpy(f.data,c->assembling,c->count);
  f.bootMs=elapsedMs();
  f.overflow=c->overflow;
  if(xQueueSend(framesQueue,&f,0)!=pdTRUE){++ctr.rxDropped[c->id];lastUARTFaultMs=millis();}
  c->count=0;c->overflow=false;
}
void receiverTask(void *arg){
  Channel *c=(Channel*)arg;
  uart_event_t evt;
  uint8_t buf[128];
  for(;;){
    if(xQueueReceive(c->uartEvents,&evt,pdMS_TO_TICKS(250))!=pdTRUE)continue;
    if(evt.type==UART_DATA){
      lastRxActivityMs[c->id]=millis();
      size_t remaining=evt.size;
      while(remaining){
        const size_t want=min((size_t)sizeof(buf),remaining);
        int n=uart_read_bytes(c->port,buf,want,pdMS_TO_TICKS(20));
        if(n<=0)break;
        remaining-=n;
        for(int i=0;i<n;i++){
          if(c->count<MAX_FRAME)c->assembling[c->count++]=buf[i];
          else c->overflow=true;
        }
      }
      // UART hardware timeout delineates messages, NOT timing of main loop().
      // One UART symbol (~10 bit times); physical engine framing still needs validation.
      if(evt.timeout_flag)queueFrame(c);
    }else if(evt.type==UART_FRAME_ERR||evt.type==UART_PARITY_ERR){
      // Bus noise/collisions: bytes still arrive and fail the checksum; count only.
      ++ctr.lineErrors[c->id];
    }else if(evt.type==UART_FIFO_OVF||evt.type==UART_BUFFER_FULL){
      ++ctr.uartFaults[c->id];lastUARTFaultMs=millis();
      uart_flush_input(c->port);
      xQueueReset(c->uartEvents);
      c->count=0;c->overflow=false;
    }
  }
}
// KAD replies from a 1990s ECU may carry small inter-byte gaps, so frames are
// split on 2 idle symbols there; J1587 uses 1 symbol (its message gap is short).
uint8_t rxTimeoutSymbols(){return cfg.protocol==PROTO_KAD?2:1;}
bool initUART(Channel &c,uint8_t rx){
  uart_config_t conf={};
  conf.baud_rate=9600;
  conf.data_bits=UART_DATA_8_BITS;
  conf.parity=UART_PARITY_DISABLE;
  conf.stop_bits=UART_STOP_BITS_1;
  conf.flow_ctrl=UART_HW_FLOWCTRL_DISABLE;
  conf.source_clk=UART_SCLK_DEFAULT;
  if(uart_param_config(c.port,&conf)!=ESP_OK)return false;
  // TX is deliberately unconnected even if the board has a solder jumper.
  if(uart_set_pin(c.port,UART_PIN_NO_CHANGE,rx,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE)!=ESP_OK)return false;
  if(uart_driver_install(c.port,4096,0,32,&c.uartEvents,0)!=ESP_OK)return false;
  uart_set_rx_full_threshold(c.port,120);
  // 1 UART symbol at 9600 8N1 ~1.04 ms. J1708 message idle gap
  // is >=10 bit times; larger RX timeout merges adjacent messages.
  if(uart_set_rx_timeout(c.port,rxTimeoutSymbols())!=ESP_OK)return false;
  return xTaskCreatePinnedToCore(receiverTask,c.id==0?"J7RX":"J6RX",4096,&c,4,nullptr,0)==pdPASS;
}
bool validChecksum(const Frame &f){
  if(f.overflow||f.length<3||f.length>MAX_FRAME)return false;
  uint8_t sum=0;
  for(uint8_t i=0;i<f.length;i++)sum=(uint8_t)(sum+f.data[i]);
  return sum==0;
}
// Remembers the latest bytes of every page-0 PID seen on each channel, so the
// UI can show what an ECU really transmits before any decoder is trusted.
void rememberPid(uint8_t ch,uint8_t mid,uint8_t pid,const uint8_t *data,uint8_t width,uint64_t at){
  PidSeen *slot=nullptr;
  for(uint8_t i=0;i<pidCount;i++){
    if(pids[i].ch==ch&&pids[i].mid==mid&&pids[i].pid==pid){slot=&pids[i];break;}
  }
  if(!slot){
    if(pidCount>=MAX_PIDS){++ctr.pidTableFull;return;}
    slot=&pids[pidCount++];
    slot->ch=ch;slot->mid=mid;slot->pid=pid;
  }
  slot->len=width;
  memset(slot->data,0,sizeof(slot->data));
  memcpy(slot->data,data,min((uint8_t)sizeof(slot->data),width));
  ++slot->count;slot->at=at;
}
void decodeJ1587(const Frame &f,uint8_t &mid,bool &gotFuel,float &lph,bool &gotRPM,float &rpm){
  // SAE J1587: PID 0..127 has 1 byte, 128..191 has 2 bytes;
  // 192..253 variable-length. Extension pages 254/255 not decoded.
  mid=f.data[0];
  gotFuel=false;gotRPM=false;
  if(!validChecksum(f))return;
  size_t i=1,end=f.length-1;
  while(i<end){
    uint8_t pid=f.data[i++];
    uint8_t width;
    if(pid<=127)width=1;
    else if(pid<=191)width=2;
    else if(pid<=253){
      if(i>=end)return;
      width=f.data[i++];
    }else{
      // An extended page cannot safely be interpreted as page-0 PID.
      return;
    }
    if((size_t)width>end-i)return;
    rememberPid(f.channel,mid,pid,&f.data[i],width,f.bootMs);
    if(pid==183&&width==2){
      uint16_t raw=(uint16_t)f.data[i]|((uint16_t)f.data[i+1]<<8);
      if(raw!=0xFFFF) {
        lph=(float)raw*(3.785411784f/64.0f); // SAE PID183, 1/64 US gal/h per bit
        gotFuel=true;
      }
    }
    if(pid==190&&width==2){
      uint16_t raw=(uint16_t)f.data[i]|((uint16_t)f.data[i+1]<<8);
      if(raw!=0xFFFF){rpm=(float)raw*0.25f;gotRPM=true;}
    }
    i+=width;
  }
}
const KadParam *kadParam(uint16_t index){
  for(const KadParam &p:KAD_PARAMS)if(p.index==index)return &p;
  return nullptr;
}
bool isKadRequest(const uint8_t *d,uint8_t n){
  if(n!=6||d[0]!=0x01||d[1]!=0x43||d[2]!=0x21)return false;
  uint8_t sum=0;for(uint8_t i=0;i<6;i++)sum=(uint8_t)(sum+d[i]);
  return sum==0;
}
void rememberKad(uint8_t ch,uint16_t index,const Frame &f,uint32_t be,uint32_t le){
  KadSeen *slot=nullptr;
  for(uint8_t i=0;i<kadSeenCount;i++)if(kadSeen[i].ch==ch&&kadSeen[i].index==index){slot=&kadSeen[i];break;}
  if(!slot){
    if(kadSeenCount>=MAX_KAD_SEEN)return;
    slot=&kadSeen[kadSeenCount++];slot->ch=ch;slot->index=index;
  }
  slot->len=f.length;
  memset(slot->reply,0,sizeof(slot->reply));
  memcpy(slot->reply,f.data,min((uint8_t)sizeof(slot->reply),f.length));
  slot->be=be;slot->le=le;slot->at=f.bootMs;++slot->count;
}
int32_t kadSigned(uint32_t v,const KadParam *p){
  if(!p||p->type!='S')return (int32_t)v;
  const uint32_t bits=8u*p->len,sign=1u<<(bits-1);
  return (v&sign)?(int32_t)v-(int32_t)(1u<<bits):(int32_t)v;
}
float kadBaseFactor(uint8_t slot){
  const KadParam &p=KAD_PARAMS[slot];
  if(p.index==289)return 1.0f/(float)cfg.kadFuelDiv;
  if(p.index==293)return cfg.kadVolt?0.0125f:0.01308f;
  return p.factor;
}
bool kadPlausible(uint8_t ch,uint8_t slot,int32_t raw,uint64_t at){
  const KadParam &p=KAD_PARAMS[slot];
  const Sanity *s=nullptr;
  for(const Sanity &x:KAD_SANITY)if(x.index==p.index){s=&x;break;}
  if(!s)return true;
  const float k=kadBaseFactor(slot),v=(float)raw*k;
  if(v<s->lo||v>s->hi){++kadRejected[ch];return false;}
  const bool fresh=kadRawAt[ch][slot]&&at>=kadRawAt[ch][slot]&&at-kadRawAt[ch][slot]<10000;
  if(!fresh){kadHeldOk[ch][slot]=false;return true;}
  const float last=(float)kadRawV[ch][slot]*k;
  if(p.index==266&&v<last-0.01f){++kadRejected[ch];return false;} // hour meters only count up
  if(fabsf(v-last)<=s->step){kadHeldOk[ch][slot]=false;return true;}
  if(kadHeldOk[ch][slot]&&fabsf(v-(float)kadHeld[ch][slot]*k)<=s->step){kadHeldOk[ch][slot]=false;return true;}
  kadHeld[ch][slot]=raw;kadHeldOk[ch][slot]=true;++kadHeldBack[ch];
  return false;
}
// Pairs requests (ours or another device's, e.g. an EDC display) with the
// next checksum-valid frame. The value is taken as the parameter's width in
// bytes just before the checksum; both byte orders are kept.
void decodeKad(const Frame &f,Decoded &d){
  KadLink &k=kad[f.channel];
  if(isKadRequest(f.data,f.length)){
    const uint16_t index=((uint16_t)f.data[3]<<8)|f.data[4];
    d.kadRequest=true;d.kadIndex=index;
    // Timestamps come from two tasks, so compare without unsigned wrap-around.
    // Our own echo: any of our last requests with this value, sent just
    // before. Judged by the frame's own time, so a late-handled echo still
    // counts as ours.
    bool own=false;
    for(uint8_t i=0;i<4;i++){
      const int64_t since=(int64_t)f.bootMs-(int64_t)k.sentAt[i];
      if(k.sentAt[i]&&k.sentIdx[i]==index&&since>-50&&since<(int64_t)KAD_REPLY_MS){own=true;break;}
    }
    d.kadOwn=own;d.kadEcho=own;
    if(own){
      ++k.echoes;
      if(k.ownPending&&k.pendingIndex==index)k.echoSeen=true;
      return;
    }
    ++k.foreignRequests;k.lastForeignAt=f.bootMs;
    // Only a device that keeps asking is another device: three requests that
    // are not ours within 10 s. A single odd frame no longer stops reading.
    if(!k.foreignBurst||f.bootMs-k.foreignFirstAt>10000){k.foreignBurst=0;k.foreignFirstAt=f.bootMs;}
    if(++k.foreignBurst>=3&&k.polling)stopPolling(f.channel,"Another device already polls this engine, so EngineLink only listens");
    if(!k.ownPending){k.pending=true;k.pendingIndex=index;k.pendingAt=f.bootMs;}
    return;
  }
  if(!k.pending||f.bootMs<k.pendingAt||f.bootMs-k.pendingAt>KAD_REPLY_MS){++k.orphanReplies;return;}
  // A reply to our request comes after its echo. Without the echo it belongs
  // to an earlier request: drop it rather than store it under this value.
  if(k.ownPending&&k.echoes>4&&!k.echoSeen){++k.orphanReplies;return;}
  const uint16_t index=k.pendingIndex;
  k.pending=false;
  if(k.ownPending){
    k.ownPending=false;++k.replies;k.unanswered=0;++k.winRep;
    for(uint8_t i=0;i<KAD_SLOW_N;i++)if(KAD_POLL_SLOW[i]==k.pendingIndex)k.miss[i]=0;
    if(k.pendingIndex>=KAD_DISC_FIRST&&k.pendingIndex<=KAD_DISC_LAST)k.discFound|=1ULL<<(k.pendingIndex-KAD_DISC_FIRST);
    if(f.bootMs>=k.pendingAt){k.replyMsSum+=f.bootMs-k.pendingAt;++k.replyCount;}
  }
  k.lastReplyAt=f.bootMs;
  const KadParam *p=kadParam(index);
  // Confirmed on a KAD300 (2026-10): answers are 83 C1 <n>1 <value...> <cs>,
  // where the high nibble of the third byte is the value width (0x21 = 2
  // bytes, 0x31 = 3 bytes) and the value is big-endian.
  uint8_t w=p?p->len:2;
  if(f.length>=4&&f.data[0]==0x83&&f.data[1]==0xC1){
    const uint8_t hw=f.data[2]>>4;
    if(hw>=1&&hw<=4&&f.length==hw+4)w=hw;
  }
  if(f.length<w+2)return;
  const uint8_t *v=&f.data[f.length-1-w];
  uint32_t be=0,le=0;
  for(uint8_t i=0;i<w;i++){be=(be<<8)|v[i];le|=(uint32_t)v[i]<<(8*i);}
  rememberKad(f.channel,index,f,be,le);
  const uint32_t raw=cfg.kadBigEndian?be:le;
  const uint32_t invalid=w>=4?0xFFFFFFFFUL:((1UL<<(8*w))-1UL);
  d.kadIndex=index;d.kadValue=kadSigned(raw,p);d.kadValueOk=true;
  if(raw==invalid)return; // all-ones = not available
  for(uint8_t i=0;i<KAD_N;i++)if(KAD_PARAMS[i].index==index){
    if(!kadPlausible(f.channel,i,d.kadValue,f.bootMs))return;
    // Throttle moving: ask for fuel more often for a few seconds.
    if((index==256||index==289)&&kadRawAt[f.channel][i]){
      const float was=(float)kadRawV[f.channel][i],now=(float)d.kadValue;
      const bool moving=index==256?fabsf(now-was)*0.25f>40.0f:fabsf(now-was)>max(0.08f*was,64.0f);
      if(moving)kadTransientUntil[f.channel]=millis()+4000;
    }
    kadRawV[f.channel][i]=d.kadValue;kadRawAt[f.channel][i]=f.bootMs;break;
  }
  Motor &m=motor[f.channel];
  if(index==256){
    d.rpm=(float)raw*0.25f;d.gotRPM=true;
    m.rpm=d.rpm;m.rpmAt=f.bootMs;m.rpmEver=true;++m.rpmSamples;
  }else if(index==289){
    d.lph=(float)((double)raw*kadFuelScale(f.channel)*cfg.bandAdj[fuelBandFor(m.rpm)]);d.gotFuel=true;
    updateFuel(m,f.channel,d.lph,f.bootMs,(double)raw);
  }else if(index==266){
    checkEcuHours(f.channel,(float)raw*0.0027778f);
  }
}
void processFrame(const Frame &f){
  if(f.channel>1)return;
  // A KAD request echo and the ECU reply can arrive as one burst; split it.
  if(cfg.protocol==PROTO_KAD&&!f.overflow&&f.length>6&&isKadRequest(f.data,6)){
    Frame a=f,b=f;
    a.length=6;
    b.length=f.length-6;memmove(b.data,f.data+6,b.length);
    processFrame(a);processFrame(b);
    return;
  }
  ++ctr.frames[f.channel];
  const bool ok=validChecksum(f);
  if(!ok)++ctr.invalid[f.channel];
  if(f.overflow)++ctr.overflows[f.channel];
  Decoded d;
  if(ok){
    if(cfg.protocol==PROTO_KAD){
      d.mid=f.data[0];
      decodeKad(f,d);
    }else{
      decodeJ1587(f,d.mid,d.gotFuel,d.lph,d.gotRPM,d.rpm);
      Motor &m=motor[f.channel];
      m.lastMID=d.mid;
      if(d.gotFuel)updateFuel(m,f.channel,d.lph,f.bootMs,-1);
      if(d.gotRPM){m.rpm=d.rpm;m.rpmAt=f.bootMs;m.rpmEver=true;++m.rpmSamples;}
    }
  }
  Recent &r=recent[recentIndex];
  r=Recent();
  r.channel=f.channel;
  r.mid=f.length?f.data[0]:0;
  r.len=f.length;r.checksum=ok;r.malformed=f.overflow;r.bootMs=f.bootMs;
  r.dir=d.kadOwn?'T':d.kadRequest?'Q':'R';
  r.kadIndex=(int16_t)d.kadIndex;
  size_t n=0;
  for(uint8_t i=0;i<f.length&&i<MAX_FRAME;i++){
    n+=(size_t)snprintf(r.hex+n,sizeof(r.hex)-n,"%02X%s",f.data[i],i+1<f.length?" ":"");
  }
  recentIndex=(recentIndex+1)%24;
  if(recentCount<24)recentCount++;
  writeRecord(f,d,ok,r.hex);
  appendCalibrationFrame(f,d,ok,r.hex);
}
// ---------- KAD polling (opt-in, this session only) ----------
// Sends only the reconstructed read-parameter request 01 43 21 <hi> <lo> <cs>.
// The TX pin is attached when polling starts and released when it stops.
uart_port_t portFor(uint8_t ch){return ch==0?UART_NUM_1:UART_NUM_2;}
uint8_t txPinFor(uint8_t ch){return ch==0?TX_J7:TX_J6;}
void stopPolling(uint8_t ch,const String &why){
  KadLink &k=kad[ch];
  k.polling=false;k.ownPending=false;k.stopReason=why;
  if(k.txAttached){
    uart_wait_tx_done(portFor(ch),pdMS_TO_TICKS(20));
    gpio_reset_pin((gpio_num_t)txPinFor(ch));
    k.txAttached=false;
  }
}
bool startPolling(uint8_t ch,String &why){
  KadLink &k=kad[ch];
  if(cfg.protocol!=PROTO_KAD){why="Select the Volvo Penta KAD / EDC1 protocol first";return false;}
  if(!uartReady[ch]){why="Receiver for this connector did not start";return false;}
  if(k.foreignBurst>=3&&elapsedMs()-k.lastForeignAt<60000){why="Another device is polling this engine. Listening is enough";return false;}
  if(uart_set_pin(portFor(ch),txPinFor(ch),UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE)!=ESP_OK){why="Could not attach the TX pin";return false;}
  k.txAttached=true;k.polling=true;k.stopReason="";k.unanswered=0;k.ownPending=false;
  memset(k.miss,0,sizeof(k.miss));k.skip=0;k.winReq=k.winRep=0;k.answered=NAN;
  k.echoSeen=false;k.foreignBurst=0;
  k.startedMs=millis();k.lastPollMs=0;
  return true;
}
void writeTxRecord(uint8_t ch,const uint8_t *req,uint16_t index){
  if(!sdLogging&&!calActive)return;
  Frame f;f.channel=ch;f.length=6;memcpy(f.data,req,6);f.bootMs=elapsedMs();
  Decoded d;d.mid=req[0];d.kadIndex=index;d.kadRequest=true;d.kadOwn=true;
  char hex[20];snprintf(hex,sizeof(hex),"%02X %02X %02X %02X %02X %02X",req[0],req[1],req[2],req[3],req[4],req[5]);
  writeRecord(f,d,true,hex);
  appendCalibrationFrame(f,d,true,hex);
}
void serviceKad(){
  if(cfg.protocol!=PROTO_KAD)return;
  // Handle what has already arrived first: after a busy moment (a download,
  // a report) the reply is often waiting in the queue, and judging the
  // request unanswered before reading it mixed up requests and replies.
  {Frame f;int k=0;while(framesQueue&&k<128&&xQueueReceive(framesQueue,&f,0)==pdTRUE){processFrame(f);k++;}}
  const uint64_t now=elapsedMs();
  for(uint8_t ch=0;ch<2;ch++){
    KadLink &k=kad[ch];
    if(k.pending&&now-k.pendingAt>KAD_REPLY_MS)k.pending=false;
    if(!k.polling)continue;
    if(k.ownPending){
      if(now-k.pendingAt<=KAD_REPLY_MS)continue;
      k.ownPending=false;++k.unanswered;
      // A value the engine never answers while it answers others is skipped.
      if(k.lastReplyAt&&now-k.lastReplyAt<2000)
        for(uint8_t i=0;i<KAD_SLOW_N;i++)if(KAD_POLL_SLOW[i]==k.pendingIndex&&++k.miss[i]>=5)k.skip|=1u<<i;
      if(k.unanswered>=KAD_MAX_UNANSWERED){
        stopPolling(ch,"No answer to "+String(KAD_MAX_UNANSWERED)+" requests. Check ignition, the J1708 cable and that the TX jumper for this connector is closed");
        continue;
      }
    }
    // Pace: the next request goes as soon as the answer is in and the bus is
    // quiet, no faster than the interval. Every 40 requests the interval
    // shrinks while (almost) everything is answered, and grows at once if
    // answers go missing, so EngineLink settles at what the engine handles.
    if(k.winReq>=40){
      const uint32_t avgReply=k.replyCount?(uint32_t)(k.replyMsSum/k.replyCount):60;
      const uint32_t floorMs=max<uint32_t>(KAD_MIN_POLL_MS,avgReply+40);
      if(k.winRep*100>=k.winReq*97)k.intervalMs=(uint16_t)max<uint32_t>(floorMs,k.intervalMs>20?k.intervalMs-20:floorMs);
      else if(k.winRep*100<k.winReq*90)k.intervalMs=(uint16_t)min<uint32_t>(KAD_POLL_MS,k.intervalMs*3/2);
      k.answered=(float)k.winRep/k.winReq;
      k.winReq=k.winRep=0;
    }
    if((uint32_t)(millis()-k.lastPollMs)<k.intervalMs)continue;
    if((uint32_t)(millis()-lastRxActivityMs[ch])<KAD_BUS_IDLE_MS)continue; // bus busy
    // Steady: rpm, fuel, one slow value. Throttle moving: fuel, rpm, fuel,
    // slow value, so the fuel integral follows the change closely. A value
    // the app is studying (learning the gear bits) takes the slow slot.
    uint16_t index;
    const bool moving=(int32_t)(kadTransientUntil[ch]-millis())>0;
    const uint8_t slot=moving?k.seq%4:k.seq%3;
    const bool slowSlot=moving?slot==3:slot==2;
    if(!slowSlot)index=moving?(slot==1?256:289):KAD_POLL_FAST[slot];
    else if(kadFocusIndex&&(int32_t)(kadFocusUntil-millis())>0)index=kadFocusIndex;
    else if(k.discActive){
      index=KAD_DISC_FIRST+k.discPos;
      if(++k.discPos>KAD_DISC_LAST-KAD_DISC_FIRST){k.discActive=false;k.discDone=true;}
    }else{
      index=KAD_POLL_SLOW[k.slowPos];
      for(uint8_t tries=0;tries<KAD_SLOW_N&&(k.skip>>k.slowPos&1);tries++){k.slowPos=(k.slowPos+1)%KAD_SLOW_N;index=KAD_POLL_SLOW[k.slowPos];}
      k.slowPos=(k.slowPos+1)%KAD_SLOW_N;
    }
    ++k.seq;
    uint8_t req[6]={0x01,0x43,0x21,(uint8_t)(index>>8),(uint8_t)(index&0xFF),0};
    uint8_t sum=0;for(uint8_t i=0;i<5;i++)sum=(uint8_t)(sum+req[i]);
    req[5]=(uint8_t)(0x100-sum);
    k.lastPollMs=millis();
    if(uart_write_bytes(portFor(ch),(const char*)req,6)!=6){
      ++k.txErrors;
      if(k.txErrors>=5)stopPolling(ch,"UART transmit error");
      continue;
    }
    ++k.requests;++k.winReq;
    k.ownPending=true;k.pending=true;k.pendingIndex=index;k.pendingAt=elapsedMs();k.echoSeen=false;
    k.sentIdx[k.sentPos]=index;k.sentAt[k.sentPos]=k.pendingAt;k.sentPos=(k.sentPos+1)%4;
    writeTxRecord(ch,req,index);
  }
}
bool fuelFresh(uint8_t c,uint64_t now){return motor[c].fuelEver&&now>=motor[c].fuelAt&&(now-motor[c].fuelAt)<FRAME_FRESH_MS;}
bool rpmFresh(uint8_t c,uint64_t now){return motor[c].rpmEver&&now>=motor[c].rpmAt&&(now-motor[c].rpmAt)<RPM_FRESH_MS;}
// ---------- Scaled engine values ----------
// Factor for a KAD parameter now; fuel and voltage follow the chosen scale.
float kadFactor(uint8_t ch,uint8_t slot){
  const KadParam &p=KAD_PARAMS[slot];
  if(p.index==289)return (float)(kadFuelScale(ch)*cfg.bandAdj[fuelBandFor(motor[ch].rpm)]);
  if(p.index==293)return cfg.kadVolt?0.0125f:0.01308f;
  return p.factor;
}
// Latest scaled value of a KAD parameter if it is younger than maxAge ms.
bool engineValueAge(uint8_t ch,uint16_t index,float &out,uint32_t maxAge){
  if(ch>1)return false;
  const uint64_t now=elapsedMs();
  if(cfg.protocol!=PROTO_KAD){
    if(index==256&&rpmFresh(ch,now)){out=motor[ch].rpm;return true;}
    if(index==289&&fuelFresh(ch,now)){out=motor[ch].fuelLph;return true;}
    return false;
  }
  for(uint8_t i=0;i<KAD_N;i++){
    if(KAD_PARAMS[i].index!=index)continue;
    if(!kadRawAt[ch][i]||now<kadRawAt[ch][i]||now-kadRawAt[ch][i]>maxAge)return false;
    const float f=kadFactor(ch,i);
    out=f>0?(float)kadRawV[ch][i]*f:(float)kadRawV[ch][i];
    return true;
  }
  return false;
}
bool engineValue(uint8_t ch,uint16_t index,float &out){return engineValueAge(ch,index,out,10000);}
// "Read engines automatically": polls whenever possible and tries again
// every 30 s (for example after the ignition was off).
void serviceAutoPoll(){
  if(!cfg.autoPoll||cfg.protocol!=PROTO_KAD||millis()<5000)return;
  for(uint8_t c=0;c<2;c++){
    KadLink &k=kad[c];
    if(k.polling||!uartReady[c])continue;
    if(k.lastAutoTry&&(uint32_t)(millis()-k.lastAutoTry)<30000)continue;
    k.lastAutoTry=millis();
    String why;
    if(startPolling(c,why)){k.byAuto=true;k.byTest=false;}
  }
}
// Reads the engines: frames from both receivers, our read requests, and the
// once-a-second engine, trip and diagnosis work.
void engineTask(void *){
  for(;;){
    Frame f;
    xQueuePeek(framesQueue,&f,pdMS_TO_TICKS(5));  // wake as soon as a frame is in, or every 5 ms
    const uint32_t w0=millis();engineWaitSince=w0?w0:1;
    xSemaphoreTake(engineMx,portMAX_DELAY);
    engineWaitSince=0;
    const uint32_t waited=millis()-w0;
    if(waited>engineMaxWaitMs)engineMaxWaitMs=waited;
    if(waited>100)++engineSlowWaits;
    int handled=0;
    while(handled<128&&xQueueReceive(framesQueue,&f,0)==pdTRUE){processFrame(f);handled++;}
    serviceKad();
    serviceAutoPoll();
    serviceEngines();
    engineBeat=millis();
    xSemaphoreGive(engineMx);
    vTaskDelay(1);  // leave time for the idle task on this core
  }
}
