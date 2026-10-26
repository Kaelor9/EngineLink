/*
 * EngineLink 0.1.0-alpha — actual application firmware
 * ESP32-S3-WROOM-1-N8R8, 8 MB flash / 8 MB PSRAM.
 *
 * Captures actual J1708 / J1587 RX bytes on J7 and J6. No J1708 TX pins
 * are configured. CAN is listen-only if user enables it.
 * Never claim a simulated or missing value is measured.
 *
 * Read docs/PRODUCTION_ALPHA.md before attaching to any engine or CAN bus.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_NeoPixel.h>
#include <driver/uart.h>
#include <driver/twai.h>
#include <esp_timer.h>
#include <sys/time.h>
#include <time.h>
#include <esp_sntp.h>
#include <math.h>
#include "ui.h"

constexpr uint8_t RX_J7=21, RX_J6=17; // TX GPIO47/GPIO18 deliberately NOT assigned
constexpr uint8_t LED_PIN=48;
constexpr uint8_t SD_CS=10, SD_MOSI=11, SD_SCK=12, SD_MISO=13, SD_DETECT=14;
constexpr uint8_t OPTO_PIN=38, CAN_TX=15, CAN_RX=16;
constexpr uint8_t MAX_FRAME=21;
constexpr uint32_t FRAME_FRESH_MS=6000;
constexpr uint32_t RPM_FRESH_MS=12000;
constexpr uint32_t CSV_FLUSH_MS=2000;

struct Frame {
  uint8_t channel=0;
  uint8_t length=0;
  uint8_t data[MAX_FRAME]={};
  uint64_t bootMs=0;
  bool overflow=false;
};
struct Channel {
  uart_port_t port;
  uint8_t id;
  QueueHandle_t uartEvents=nullptr;
  uint8_t assembling[MAX_FRAME]={};
  uint8_t count=0;
  bool overflow=false;
};
struct Recent {
  uint8_t channel=0, mid=0, len=0;
  bool checksum=false, malformed=false;
  uint64_t bootMs=0;
  char hex[3*MAX_FRAME+1]={};
};
struct Motor {
  bool fuelEver=false, rpmEver=false;
  float fuelLph=0, rpm=0;
  uint64_t fuelAt=0,rpmAt=0,prevFuelAt=0;
  float previousFuelLph=0;
  double observedLiters=0;
  uint32_t fuelSamples=0, rpmSamples=0, gaps=0;
  uint8_t lastMID=0;
};
struct Settings {
  bool completed=false;
  bool router=false;
  bool canListen=false;
  uint8_t portChannel=1; // 1=J7 port, 2=J6 port, 0=unassigned
  String apPassword="12345678";
  String ssid="";
  String wifiPassword="";
  String timePreference="auto";
};
struct Counters {
  uint32_t frames[2]={0,0};
  uint32_t invalid[2]={0,0};
  uint32_t overflows[2]={0,0};
  volatile uint32_t rxDropped[2]={0,0};
  volatile uint32_t uartFaults[2]={0,0};
  uint32_t nmeaFrames=0;
  uint32_t canErrors=0;
};

WebServer web(80);
Preferences pref;
Adafruit_NeoPixel led(1, LED_PIN, NEO_GRB+NEO_KHZ800);
Settings cfg;
Counters ctr;
Motor motor[2];
Channel channels[2]={{UART_NUM_1,0},{UART_NUM_2,1}};
QueueHandle_t framesQueue=nullptr;
Recent recent[24];
uint8_t recentIndex=0,recentCount=0;
uint32_t bootId=0,lastFlush=0,lastWifiRetry=0,lastLed=0,ledPrev=0xFFFFFFFF;
uint8_t ledBrightness=34;
uint32_t manualLedColor=0x26B8AE;
bool manualLED=false;
bool sdReady=false,sdLogging=false,canReady=false,pendingNetwork=false;
bool hasTime=false,ntpStarted=false;
String timeSource="none",sdMessage="No card",sessionName;
File logFile;
uint32_t sdRows=0,sdErrors=0;
uint32_t lastAPCheck=0;
volatile uint32_t apJoins=0,apLeaves=0;
volatile uint8_t apReason=0;
float sogKnots=NAN;
uint64_t sogAt=0;
volatile bool ntpNotification=false;

uint64_t elapsedMs(){return (uint64_t)esp_timer_get_time()/1000ULL;}
bool validWall(){return time(nullptr)>1700000000;}
void setClockFromUtc(time_t sec, const String &source){
  if(sec<1700000000 || sec>4102444800LL)return;
  struct timeval tv={sec,0};
  settimeofday(&tv,nullptr);
  hasTime=true;timeSource=source;
}
bool allowTime(const String &source){
  if(cfg.timePreference=="auto") {
    if(source=="gps")return true;
    if(source=="ntp")return timeSource!="gps";
    if(source=="phone")return timeSource!="gps"&&timeSource!="ntp";
    return false;
  }
  return cfg.timePreference==source;
}
void ntpCallback(struct timeval *tv){(void)tv;ntpNotification=true;}
void onWiFi(WiFiEvent_t e,WiFiEventInfo_t info){
  if(e==ARDUINO_EVENT_WIFI_AP_STACONNECTED)++apJoins;
  if(e==ARDUINO_EVENT_WIFI_AP_STADISCONNECTED){
    ++apLeaves;
    apReason=info.wifi_ap_stadisconnected.reason;
  }
}
bool cardInserted(){return digitalRead(SD_DETECT)==LOW;}

void stopLog(){
  if(logFile){logFile.flush();logFile.close();}
  sdLogging=false;
}
void initSD(){
  if(!cardInserted()){sdReady=false;sdMessage="No card";return;}
  SPI.begin(SD_SCK,SD_MISO,SD_MOSI,SD_CS);
  sdReady=SD.begin(SD_CS,SPI,4000000);
  sdMessage=sdReady?"Card mounted":"Mount failed";
  if(!sdReady)return;
  char name[25];
  snprintf(name,sizeof(name),"/EL%08lX.CSV",(unsigned long)bootId);
  sessionName=name;
  logFile=SD.open(sessionName,FILE_APPEND);
  if(!logFile){sdMessage="Cannot open session";return;}
  if(logFile.size()==0){
    logFile.println("boot_id,uptime_ms,utc_epoch,channel,mid,checksum,raw_hex,engine,fuel_lph,rpm");
  }
  sdLogging=true;
  sdMessage="Logging raw J1708";
  lastFlush=millis();
}
void serviceSD(){
  if(sdLogging&&!cardInserted()){
    stopLog();sdReady=false;sdMessage="Card removed - logging stopped";
    return;
  }
  if(sdLogging && (uint32_t)(millis()-lastFlush)>=CSV_FLUSH_MS){
    logFile.flush();
    lastFlush=millis();
  }
}
void writeRecord(const Frame &f,uint8_t mid,bool valid,const char *hex,bool gotFuel,float lph,bool gotRPM,float rpm){
  if(!sdLogging)return;
  int engineId = cfg.portChannel==0?-1:((f.channel+1)==cfg.portChannel?0:1);
  time_t utc=validWall()?time(nullptr):0;
  // Single short append; UART receiver tasks buffer independently.
  int result=logFile.printf("%08lX,%llu,%lld,J%u,%u,%u,%s,%s,",
      (unsigned long)bootId,(unsigned long long)f.bootMs,(long long)utc,
      f.channel==0?7:6,(unsigned)mid,valid?1:0,hex,
      engineId==0?"port":engineId==1?"starboard":"unassigned");
  if(result<=0){sdMessage="SD write failed";++sdErrors;stopLog();return;}
  if(gotFuel)logFile.print(lph,3);
  logFile.print(',');
  if(gotRPM)logFile.print(rpm,2);
  logFile.println();
  ++sdRows;
}
void queueFrame(Channel *c){
  if(c->count<2 && !c->overflow){c->count=0;return;}
  Frame f;
  f.channel=c->id;
  f.length=c->count;
  memcpy(f.data,c->assembling,c->count);
  f.bootMs=elapsedMs();
  f.overflow=c->overflow;
  if(xQueueSend(framesQueue,&f,0)!=pdTRUE)++ctr.rxDropped[c->id];
  c->count=0;c->overflow=false;
}
void receiverTask(void *arg){
  Channel *c=(Channel*)arg;
  uart_event_t evt;
  uint8_t buf[128];
  for(;;){
    if(xQueueReceive(c->uartEvents,&evt,pdMS_TO_TICKS(250))!=pdTRUE)continue;
    if(evt.type==UART_DATA){
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
    }else if(evt.type==UART_FIFO_OVF||evt.type==UART_BUFFER_FULL){
      ++ctr.uartFaults[c->id];
      uart_flush_input(c->port);
      xQueueReset(c->uartEvents);
      c->count=0;c->overflow=false;
    }
  }
}
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
  if(uart_set_rx_timeout(c.port,1)!=ESP_OK)return false;
  return xTaskCreatePinnedToCore(receiverTask,c.id==0?"J7RX":"J6RX",4096,&c,4,nullptr,0)==pdPASS;
}
bool validChecksum(const Frame &f){
  if(f.overflow||f.length<3||f.length>MAX_FRAME)return false;
  uint8_t sum=0;
  for(uint8_t i=0;i<f.length;i++)sum=(uint8_t)(sum+f.data[i]);
  return sum==0;
}
void updateFuel(Motor &m,float newRate,uint64_t timestamp){
  if(m.fuelEver && timestamp>m.prevFuelAt) {
    const uint64_t dt=timestamp-m.prevFuelAt;
    if(dt<=5000) {
      m.observedLiters+=((double)m.previousFuelLph+(double)newRate)*0.5*(double)dt/3600000.0;
    }else ++m.gaps;
  }
  m.fuelLph=newRate;m.fuelAt=timestamp;m.prevFuelAt=timestamp;
  m.previousFuelLph=newRate;m.fuelEver=true;++m.fuelSamples;
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
void processFrame(const Frame &f){
  if(f.channel>1)return;
  ++ctr.frames[f.channel];
  const bool ok=validChecksum(f);
  if(!ok)++ctr.invalid[f.channel];
  if(f.overflow)++ctr.overflows[f.channel];
  Recent &r=recent[recentIndex];
  r=Recent();
  r.channel=f.channel;
  r.mid=f.length?f.data[0]:0;
  r.len=f.length;r.checksum=ok;r.malformed=f.overflow;r.bootMs=f.bootMs;
  size_t n=0;
  for(uint8_t i=0;i<f.length&&i<MAX_FRAME;i++){
    n+=(size_t)snprintf(r.hex+n,sizeof(r.hex)-n,"%02X%s",f.data[i],i+1<f.length?" ":"");
  }
  recentIndex=(recentIndex+1)%24;
  if(recentCount<24)recentCount++;
  uint8_t mid=0;
  bool gotFuel=false,gotRPM=false;
  float lph=0,rpm=0;
  if(ok){
    decodeJ1587(f,mid,gotFuel,lph,gotRPM,rpm);
    Motor &m=motor[f.channel];
    m.lastMID=mid;
    if(gotFuel)updateFuel(m,lph,f.bootMs);
    if(gotRPM){m.rpm=rpm;m.rpmAt=f.bootMs;m.rpmEver=true;++m.rpmSamples;}
  }
  writeRecord(f,mid,ok,r.hex,gotFuel,lph,gotRPM,rpm);
}
uint32_t nmeaPGN(uint32_t canid){
  uint32_t pgn=(canid>>8)&0x3FFFF;
  if(((canid>>16)&0xFF)<240)pgn&=0x3FF00;
  return pgn;
}
bool startCAN(){
  if(canReady)return true;
  twai_general_config_t gen=TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)CAN_TX,(gpio_num_t)CAN_RX,TWAI_MODE_LISTEN_ONLY);
  gen.rx_queue_len=32;
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
  for(int k=0;k<12;k++){
    if(twai_receive(&m,0)!=ESP_OK)break;
    ++ctr.nmeaFrames;
    if(!m.extd||m.data_length_code!=8)continue;
    uint32_t pgn=nmeaPGN(m.identifier);
    if(pgn==129026){ // COG & SOG rapid update. SOG in 0.01 m/s units.
      uint16_t raw=(uint16_t)m.data[4]|((uint16_t)m.data[5]<<8);
      if(raw!=0xFFFF){sogKnots=((float)raw*0.01f)*1.94384449f;sogAt=elapsedMs();}
    }else if(pgn==126992){ // NMEA2000 System Time: days since 1970, 0.0001 sec/day
      uint16_t days=(uint16_t)m.data[2]|((uint16_t)m.data[3]<<8);
      uint32_t sec10000=(uint32_t)m.data[4]|((uint32_t)m.data[5]<<8)|((uint32_t)m.data[6]<<16)|((uint32_t)m.data[7]<<24);
      if(days!=0xFFFF&&sec10000!=0xFFFFFFFFUL&&sec10000<864000000UL&&allowTime("gps")){
        time_t epoch=(time_t)((uint32_t)days*86400UL+(sec10000/10000UL));
        if(epoch>1700000000) setClockFromUtc(epoch,"gps");
      }
    }
  }
}
void applyNetwork(){
  WiFi.mode(cfg.router?WIFI_AP_STA:WIFI_AP);
  WiFi.setSleep(false);
  WiFi.softAPConfig(IPAddress(192,168,4,1),IPAddress(192,168,4,1),IPAddress(255,255,255,0));
  WiFi.softAP("EngineLink",cfg.apPassword.c_str(),6,false,4);
  if(cfg.router && cfg.ssid.length()){
    WiFi.setAutoReconnect(true);
    WiFi.begin(cfg.ssid.c_str(),cfg.wifiPassword.c_str());
    lastWifiRetry=millis();
  }else{WiFi.disconnect(false,false);ntpStarted=false;}
}
void serviceNetwork(){
  if(pendingNetwork){pendingNetwork=false;applyNetwork();}
  if(cfg.router && WiFi.status()!=WL_CONNECTED&&(uint32_t)(millis()-lastWifiRetry)>30000UL){
    WiFi.reconnect();lastWifiRetry=millis();
  }
  // AP remains available regardless of router connectivity.
  if((uint32_t)(millis()-lastAPCheck)>15000UL){
    lastAPCheck=millis();
    if(WiFi.softAPIP()==IPAddress(0,0,0,0))applyNetwork();
  }
  if(cfg.router&&WiFi.status()==WL_CONNECTED&&!ntpStarted && (cfg.timePreference=="auto"||cfg.timePreference=="ntp")){
    configTime(0,0,"pool.ntp.org","time.google.com");
    ntpStarted=true;
  }
  if(ntpNotification){
    ntpNotification=false;
    if(allowTime("ntp")&&validWall()){hasTime=true;timeSource="ntp";}
  }
}
void updateLED(){
  if((uint32_t)(millis()-lastLed)<100)return;
  lastLed=millis();
  uint32_t c=manualLED?manualLedColor:
     !sdReady?0xCC7A14:(!cfg.completed?0x1858AA:0x169E83);
  c=led.Color((c>>16)&255,(c>>8)&255,c&255);
  if(c==ledPrev)return;
  led.setBrightness(ledBrightness);
  led.setPixelColor(0,c);led.show();ledPrev=c;
}
void saveSettings(){
  pref.begin("enginelink",false);
  pref.putBool("done",cfg.completed);
  pref.putBool("router",cfg.router);
  pref.putBool("can",cfg.canListen);
  pref.putUChar("port",cfg.portChannel);
  pref.putString("appass",cfg.apPassword);
  pref.putString("ssid",cfg.ssid);
  pref.putString("wpass",cfg.wifiPassword);
  pref.putString("time",cfg.timePreference);
  pref.end();
}
void loadSettings(){
  pref.begin("enginelink",true);
  cfg.completed=pref.getBool("done",false);
  cfg.router=pref.getBool("router",false);
  cfg.canListen=pref.getBool("can",false);
  cfg.portChannel=pref.getUChar("port",1);
  cfg.apPassword=pref.getString("appass","12345678");
  cfg.ssid=pref.getString("ssid","");
  cfg.wifiPassword=pref.getString("wpass","");
  cfg.timePreference=pref.getString("time","auto");
  pref.end();
  if(cfg.apPassword.length()<8)cfg.apPassword="12345678";
  if(cfg.portChannel>2)cfg.portChannel=0;
}
void jsonString(String &out,const String &v){
  out+='"';
  for(size_t i=0;i<v.length();i++){
    char c=v[i];
    if(c=='"'||c=='\\'){out+='\\';out+=c;}
    else if((uint8_t)c<32){out+=' ';}
    else out+=c;
  }
  out+='"';
}
void sendJSON(int code,const String &s){
  web.sendHeader("Cache-Control","no-store");
  web.sendHeader("Connection","close");
  web.send(code,"application/json; charset=utf-8",s);
}
void reply(bool ok,const String &message,int code=200){
  String s="{\"ok\":";
  s+=ok?"true":"false";
  s+=",\"message\":";jsonString(s,message);s+='}';
  sendJSON(code,s);
}
void handleState(){
  const uint64_t now=elapsedMs();
  bool freshFuel[2]={false,false},freshRPM[2]={false,false};
  for(int c=0;c<2;c++){
    freshFuel[c]=motor[c].fuelEver&&now>=motor[c].fuelAt&&(now-motor[c].fuelAt)<FRAME_FRESH_MS;
    freshRPM[c]=motor[c].rpmEver&&now>=motor[c].rpmAt&&(now-motor[c].rpmAt)<RPM_FRESH_MS;
  }
  bool mapped=cfg.portChannel==1||cfg.portChannel==2;
  int port=mapped?(cfg.portChannel-1):-1,starboard=mapped?(1-port):-1;
  bool totalAvailable=mapped&&freshFuel[0]&&freshFuel[1];
  float total=totalAvailable?motor[0].fuelLph+motor[1].fuelLph:0;
  bool speedAvailable=isfinite(sogKnots)&&sogAt&&now-sogAt<7000 && sogKnots>0.5;
  String s;s.reserve(7500);
  s+="{\"version\":\"0.1.0-alpha\",\"setup\":";
  s+=cfg.completed?"true":"false";
  s+=",\"mode\":\"PASSIVE\",\"uptime\":";
  s+=(unsigned long)(now/1000ULL);
  s+=",\"bootId\":";s+=bootId;
  s+=",\"timeKnown\":";s+=validWall()?"true":"false";
  s+=",\"timeSource\":";jsonString(s,timeSource);
  s+=",\"utc\":";s+=validWall()?(long long)time(nullptr):0;
  s+=",\"network\":{\"apSsid\":\"EngineLink\",\"apClients\":";
  s+=WiFi.softAPgetStationNum();
  s+=",\"apIP\":\"192.168.4.1\",\"router\":";
  s+=cfg.router?"true":"false";
  s+=",\"staConnected\":";s+=WiFi.status()==WL_CONNECTED?"true":"false";
  s+=",\"staIP\":";jsonString(s,WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():String(""));
  s+=",\"ssid\":";jsonString(s,cfg.ssid);
  s+=",\"apJoins\":";s+=(unsigned long)apJoins;
  s+=",\"apLeaves\":";s+=(unsigned long)apLeaves;
  s+=",\"apReason\":";s+=(unsigned int)apReason;
  s+="},\"config\":{\"router\":";
  s+=cfg.router?"true":"false";
  s+=",\"portChannel\":";s+=cfg.portChannel;
  s+=",\"timePref\":";jsonString(s,cfg.timePreference);
  s+=",\"canListen\":";s+=cfg.canListen?"true":"false";
  s+="},\"sd\":{\"ready\":";
  s+=sdReady?"true":"false";
  s+=",\"logging\":";s+=sdLogging?"true":"false";
  s+=",\"rows\":";s+=sdRows;
  s+=",\"errors\":";s+=sdErrors;
  s+=",\"filename\":";jsonString(s,sessionName);
  s+=",\"status\":";jsonString(s,sdMessage);
  s+="},\"hardware\":{\"heap\":";
  s+=ESP.getFreeHeap();
  s+=",\"minHeap\":";s+=ESP.getMinFreeHeap();
  s+=",\"psram\":";s+=psramFound()?ESP.getPsramSize():0;
  s+=",\"opto\":";s+=digitalRead(OPTO_PIN);
  s+=",\"canReady\":";s+=canReady?"true":"false";
  s+=",\"nmeaRx\":";s+=ctr.nmeaFrames;
  s+="},\"channels\":[";
  for(int c=0;c<2;c++){
    if(c)s+=',';
    s+="{\"name\":\"J";s+=c==0?"7":"6";s+="\",\"frames\":";
    s+=ctr.frames[c];
    s+=",\"invalid\":";s+=ctr.invalid[c];
    s+=",\"drops\":";s+=(unsigned long)ctr.rxDropped[c];
    s+=",\"uartFaults\":";s+=(unsigned long)ctr.uartFaults[c];
    s+=",\"fuel\":";
    if(freshFuel[c])s+=String(motor[c].fuelLph,3);else s+="null";
    s+=",\"rpm\":";
    if(freshRPM[c])s+=String(motor[c].rpm,1);else s+="null";
    s+=",\"observedLiters\":";s+=String(motor[c].observedLiters,4);
    s+=",\"fuelSamples\":";s+=motor[c].fuelSamples;
    s+=",\"gaps\":";s+=motor[c].gaps;
    s+=",\"lastMID\":";s+=motor[c].lastMID;
    s+="}";
  }
  s+="],\"fuel\":{\"total\":";
  if(totalAvailable)s+=String(total,3);else s+="null";
  s+=",\"portChannel\":";s+=port;
  s+=",\"starboardChannel\":";s+=starboard;
  s+=",\"sogKnots\":";
  if(speedAvailable)s+=String(sogKnots,2);else s+="null";
  s+=",\"litersPerNm\":";
  if(speedAvailable&&totalAvailable)s+=String(total/sogKnots,3);else s+="null";
  s+="},\"recent\":[";
  uint8_t n=min((uint8_t)12,recentCount);
  for(int i=0;i<n;i++){
    const Recent &e=recent[(recentIndex+24-1-i)%24];
    if(i)s+=',';
    s+="{\"ch\":";s+=e.channel;
    s+=",\"mid\":";s+=e.mid;
    s+=",\"ms\":";s+=(unsigned long)(e.bootMs%4294967295ULL);
    s+=",\"ok\":";s+=e.checksum?"true":"false";
    s+=",\"hex\":";jsonString(s,e.hex);
    s+="}";
  }
  s+="]}";
  sendJSON(200,s);
}
void handleOptions(){
  if(!web.hasArg("port")||!web.hasArg("network")){reply(false,"Missing fields",400);return;}
  String network=web.arg("network");
  if(network!="ap"&&network!="router"){reply(false,"Invalid network mode",400);return;}
  String apPass=web.arg("apPass");
  if(apPass.length() && (apPass.length()<8||apPass.length()>63)){reply(false,"EngineLink Wi-Fi password must be 8 to 63 characters",400);return;}
  String ssid=web.arg("ssid");
  if(network=="router"&&ssid.isEmpty()){reply(false,"Enter router Wi-Fi network name",400);return;}
  String preference=web.arg("time");
  if(preference!="auto"&&preference!="phone"&&preference!="gps"&&preference!="ntp"&&preference!="manual"){
    reply(false,"Invalid time source",400);return;
  }
  int port=web.arg("port").toInt();
  if(port<0||port>2){reply(false,"Invalid engine assignment",400);return;}
  bool changeNetwork=cfg.router!=(network=="router")||cfg.ssid!=ssid;
  String newRouterPass=web.arg("pass");
  if(newRouterPass.length()){cfg.wifiPassword=newRouterPass;changeNetwork=true;}
  if(apPass.length()&&apPass!=cfg.apPassword){cfg.apPassword=apPass;changeNetwork=true;}
  cfg.router=network=="router";
  cfg.ssid=ssid;
  cfg.timePreference=preference;
  cfg.portChannel=(uint8_t)port;
  bool newCAN=web.arg("can")=="1";
  if(newCAN!=cfg.canListen){
    cfg.canListen=newCAN;
    if(!newCAN)stopCAN();
    else if(!startCAN()){reply(false,"NMEA2000 listen-only interface could not start",409);return;}
  }
  cfg.completed=true;
  saveSettings();
  reply(true,"Settings saved. Direct EngineLink Wi-Fi remains available.");
  if(changeNetwork)pendingNetwork=true;
}
void handleTime(){
  if(!allowTime("phone")){reply(false,"Phone is not the selected time source",409);return;}
  String v=web.arg("ms");
  if(v.length()<13||v.length()>14){reply(false,"Invalid time",400);return;}
  uint64_t epochMs=strtoull(v.c_str(),nullptr,10);
  time_t sec=(time_t)(epochMs/1000ULL);
  if(sec<1700000000||sec>4102444800LL){reply(false,"Phone time out of range",400);return;}
  setClockFromUtc(sec,"phone");
  reply(true,"Clock synchronized from phone");
}
void handleLED(){
  if(web.arg("mode")=="auto"){manualLED=false;}
  else if(web.arg("mode")=="manual"){
    String hex=web.arg("hex");
    if(hex.length()!=6){reply(false,"Invalid RGB color",400);return;}
    manualLedColor=strtoul(hex.c_str(),nullptr,16)&0xFFFFFF;
    manualLED=true;
  }else{reply(false,"Invalid LED mode",400);return;}
  ledBrightness=(uint8_t)constrain(web.arg("brightness").toInt(),1,180);
  ledPrev=0xFFFFFFFF;
  reply(true,"Status LED updated");
}
void handleDownload(){
  if(!sdReady||!sessionName.length()||!SD.exists(sessionName)){web.send(404,"text/plain","No session file");return;}
  if(sdLogging)logFile.flush();
  File f=SD.open(sessionName,FILE_READ);
  if(!f){web.send(500,"text/plain","Cannot open log file");return;}
  web.sendHeader("Content-Disposition","attachment; filename=EngineLink-session.csv");
  web.streamFile(f,"text/csv");
  f.close();
}
void initializeWeb(){
  web.on("/",HTTP_GET,[](){
    web.sendHeader("Cache-Control","no-store");
    web.sendHeader("Connection","close");
    web.send_P(200,"text/html; charset=utf-8",ENGINE_UI);
  });
  web.on("/favicon.ico",HTTP_GET,[](){web.send(204,"text/plain","");});
  web.on("/api/state",HTTP_GET,handleState);
  web.on("/api/setup",HTTP_POST,handleOptions);
  web.on("/api/settings",HTTP_POST,handleOptions);
  web.on("/api/phone-time",HTTP_POST,handleTime);
  web.on("/api/led",HTTP_POST,handleLED);
  web.on("/api/sd/remount",HTTP_POST,[](){
    if(sdLogging){reply(false,"Stop logging before remount",409);return;}
    SD.end();sdReady=false;initSD();reply(sdReady,sdMessage);
  });
  web.on("/api/sd/stop",HTTP_POST,[](){stopLog();reply(true,"Session recording stopped");});
  web.on("/api/sd/start",HTTP_POST,[](){if(sdLogging){reply(true,"Already recording");return;}initSD();reply(sdLogging,sdMessage);});
  web.on("/api/log",HTTP_GET,handleDownload);
  web.on("/api/health",HTTP_GET,[](){web.send(200,"text/plain","OK");});
  web.onNotFound([](){web.send(404,"text/plain","Not found");});
  web.begin();
}
void setup(){
  pinMode(LED_PIN,OUTPUT);digitalWrite(LED_PIN,LOW);
  led.begin();led.setBrightness(ledBrightness);led.clear();led.show();
  Serial.begin(115200);
  delay(1200); // do not block boot for USB Serial Monitor
  Serial.println("\nEngineLink 0.1.0-alpha - passive engine monitoring");
  bootId=esp_random();
  pinMode(SD_DETECT,INPUT);
  pinMode(OPTO_PIN,INPUT);
  loadSettings();
  framesQueue=xQueueCreate(72,sizeof(Frame));
  if(framesQueue){
    if(!initUART(channels[0],RX_J7))Serial.println("J7 UART init failed");
    if(!initUART(channels[1],RX_J6))Serial.println("J6 UART init failed");
  }
  initSD();
  WiFi.onEvent(onWiFi);
  WiFi.setSleep(false);
  applyNetwork();
  initializeWeb();
  if(cfg.canListen && !startCAN())Serial.println("CAN listen-only init failed");
  // NTP time sync callback is non-blocking.
  sntp_set_time_sync_notification_cb(ntpCallback);
  Serial.printf("SoftAP EngineLink at http://%s ; passive J1708 only\n",WiFi.softAPIP().toString().c_str());
}
void loop(){
  // Give the UART data priority over serving the user interface.
  Frame f;
  int handled=0;
  while(framesQueue&&handled<12&&xQueueReceive(framesQueue,&f,0)==pdTRUE){processFrame(f);handled++;}
  serviceCAN();
  web.handleClient();
  serviceSD();
  serviceNetwork();
  updateLED();
  delay(1);
}
