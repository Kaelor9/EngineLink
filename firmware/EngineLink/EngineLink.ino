/*
 * EngineLink 0.2.1-alpha — actual application firmware
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
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <math.h>
#include "ui.h"

#define EL_VERSION "0.2.1-alpha"

constexpr uint8_t RX_J7=21, RX_J6=17; // TX GPIO47/GPIO18 deliberately NOT assigned
constexpr uint8_t LED_PIN=48;
constexpr uint8_t SD_CS=10, SD_MOSI=11, SD_SCK=12, SD_MISO=13, SD_DETECT=14;
constexpr uint8_t OPTO_PIN=38, CAN_TX=15, CAN_RX=16;
// SAE J1708 limits messages to 21 bytes only while the vehicle is moving;
// longer stationary/diagnostic messages must still be captured intact.
constexpr uint8_t MAX_FRAME=64;
// Frame backlog lives in PSRAM so a blocking CSV download or SD flush does
// not drop engine traffic (~2048 frames is tens of seconds of bus load).
constexpr UBaseType_t FRAME_QUEUE_PSRAM=2048, FRAME_QUEUE_FALLBACK=96;
constexpr uint32_t FRAME_FRESH_MS=6000;
constexpr uint32_t RPM_FRESH_MS=12000;
constexpr uint32_t CSV_FLUSH_MS=1200;

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
  volatile uint32_t lineErrors[2]={0,0};
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
StaticQueue_t framesQueueState;
UBaseType_t framesQueueLength=0;
Recent recent[24];
uint8_t recentIndex=0,recentCount=0;
uint32_t bootId=0,lastFlush=0,lastWifiRetry=0,lastLed=0,ledPrev=0xFFFFFFFF;
uint32_t ledTestStartedMs=0;
uint8_t ledBrightness=42;
enum LedState : uint8_t { LS_BOOT, LS_SETUP, LS_READY, LS_ROUTER, LS_NO_SD, LS_WRITE_ERROR, LS_RX_ERROR, LS_TEST };
LedState currentLED=LS_BOOT;
bool sdFaultLatched=false;
bool uartReady[2]={false,false};
bool sdReady=false,sdLogging=false,canReady=false,pendingNetwork=false;
bool hasTime=false,ntpStarted=false;
String timeSource="none",sdMessage="No card",sessionName;
File logFile,calFile;
bool calActive=false,calResumeLogging=false;
int8_t calChannel=-1; // -1 both, 0 J7, 1 J6
uint32_t calRows=0,calInvalid=0,calSteps=0,calStartMs=0;
uint32_t lastTestFlush=0;
volatile uint32_t lastUARTFaultMs=0;
int32_t calReferenceRPM=-1;
float calReferenceFuel=-1.0f;
String calName="",calPhase="idle";
uint32_t sdRows=0,sdErrors=0;
bool apStarted=false;
bool testFileAvailable=false,spiStarted=false,cardWasInserted=false;
uint32_t lastCardCheck=0,cardChangeAt=0;
volatile uint32_t apJoins=0,apLeaves=0,apStarts=0,apStops=0,staDisconnects=0;
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
  if(e==ARDUINO_EVENT_WIFI_AP_START)++apStarts;
  if(e==ARDUINO_EVENT_WIFI_AP_STOP)++apStops;
  if(e==ARDUINO_EVENT_WIFI_AP_STACONNECTED)++apJoins;
  if(e==ARDUINO_EVENT_WIFI_AP_STADISCONNECTED){++apLeaves;apReason=info.wifi_ap_stadisconnected.reason;}
  if(e==ARDUINO_EVENT_WIFI_STA_DISCONNECTED)++staDisconnects;
}
bool cardInserted(){return digitalRead(SD_DETECT)==LOW;}

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
    logFile.println("boot_id,uptime_ms,utc_epoch,channel,mid,checksum,raw_hex,engine,fuel_lph,rpm");
  }
  sdLogging=true;
  sdMessage="Logging raw J1708";sdFaultLatched=false;
  lastFlush=millis();
}
void refreshTestAvailable(){
  testFileAvailable=sdReady&&cardInserted()&&calName.length()&&SD.exists(calName);
}
void reply(bool ok,const String &message,int code=200);
void finishCalibration(bool resume);
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
    if(inserted&&!sdReady&&cardChangeAt&&(uint32_t)(millis()-cardChangeAt)>=1000){
      cardChangeAt=0;
      initSD();
      refreshTestAvailable();
    }
  }
  if(sdLogging && (uint32_t)(millis()-lastFlush)>=CSV_FLUSH_MS){
    logFile.flush();lastFlush=millis();
  }
  if(calActive && (uint32_t)(millis()-lastTestFlush)>=CSV_FLUSH_MS){
    calFile.flush();lastTestFlush=millis();
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
  if(result<=0){sdMessage="SD write failed";sdFaultLatched=true;++sdErrors;stopLog();return;}
  if(gotFuel)logFile.print(lph,3);
  logFile.print(',');
  if(gotRPM)logFile.print(rpm,2);
  if(logFile.println()==0){sdMessage="SD write failed";sdFaultLatched=true;++sdErrors;stopLog();return;}
  ++sdRows;
}

// A calibration capture consists of original RX bytes plus operator reference
// markers. Nothing in this workflow transmits or commands an engine.
void finishCalibration(bool resume){
  if(calFile){if(cardInserted())calFile.flush();calFile.close();}
  calActive=false;
  refreshTestAvailable();
  if(resume && calResumeLogging && sdReady && cardInserted()){
    logFile=SD.open(sessionName,FILE_APPEND);
    sdLogging=(bool)logFile;
    if(!sdLogging){sdFaultLatched=true;sdMessage="Could not resume raw logging";}
  }
  calResumeLogging=false;
}
void calibrationMarker(){
  if(!calActive)return;
  const long long epoch=validWall()?(long long)time(nullptr):0;
  char entry[240];
  int n=snprintf(entry,sizeof(entry),"MARK,%08lX,%llu,%lld,%lu,%s,%ld,%.3f,,,,,,,\n",
    (unsigned long)bootId,(unsigned long long)elapsedMs(),epoch,
    (unsigned long)calSteps,calPhase.c_str(),(long)calReferenceRPM,
    calReferenceFuel);
  if(n<=0 || n >= (int)sizeof(entry) || calFile.write((const uint8_t*)entry,n)!=(size_t)n){
    sdFaultLatched=true;sdMessage="Calibration marker write failed";finishCalibration(false);
    return;
  }
  calFile.flush();
}
bool startCalibration(int channel){
  if(calActive||!sdReady||!cardInserted())return false;
  if(channel<-1||channel>1)return false;
  calChannel=channel;
  calRows=0;calInvalid=0;calSteps=1;calReferenceRPM=-1;calReferenceFuel=-1;
  calPhase="unlabelled";calStartMs=millis();
  char name[28];
  snprintf(name,sizeof(name),"/TEST%08lX.CSV",(unsigned long)bootId);
  calName=name;
  if(SD.exists(calName)){ // Do not overwrite an older test if a boot ID collides.
    for(uint8_t i=1;i<50;i++){
      snprintf(name,sizeof(name),"/T%08lX%02u.CSV",(unsigned long)bootId,(unsigned)i);
      if(!SD.exists(name)){calName=name;break;}
    }
    if(SD.exists(calName))return false;
  }
  calFile=SD.open(calName,FILE_WRITE);
  if(!calFile){sdFaultLatched=true;sdMessage="Could not create motor test file";return false;}
  // Only pause session logging once the test file exists, so a failed start
  // never leaves ordinary logging silently switched off.
  calResumeLogging=sdLogging;
  if(sdLogging)stopLog(); // Do not duplicate high-rate writes to the same SD card.
  testFileAvailable=false;
  calFile.println("kind,boot_id,uptime_ms,utc_epoch,step,phase,reference_rpm,reference_fuel_lph,channel,mid,length,checksum,raw_hex,decoded_fuel_lph,decoded_rpm");
  calActive=true;
  lastTestFlush=millis();
  const bool resumeAfterFailure=calResumeLogging;
  calibrationMarker();
  if(!calActive){
    if(resumeAfterFailure)initSD();
    return false;
  }
  pref.begin("enginelink",false);pref.putString("testfile",calName);pref.end();
  sdMessage="Motor calibration test capturing";
  return true;
}
void appendCalibrationFrame(const Frame &f,uint8_t mid,bool valid,const char *hex,bool gotFuel,float lph,bool gotRPM,float rpm){
  if(!calActive || (calChannel>=0 && f.channel!=(uint8_t)calChannel))return;
  const long long epoch=validWall()?(long long)time(nullptr):0;
  char entry[480];
  int n=snprintf(entry,sizeof(entry),"FRAME,%08lX,%llu,%lld,%lu,%s,%ld,%.3f,J%u,%u,%u,%u,%s,",
    (unsigned long)bootId,(unsigned long long)f.bootMs,epoch,
    (unsigned long)calSteps,calPhase.c_str(),(long)calReferenceRPM,calReferenceFuel,
    f.channel==0?7:6,(unsigned)mid,(unsigned)f.length,valid?1:0,hex);
  if(n<=0||n>(int)sizeof(entry)-48){++sdErrors;sdFaultLatched=true;sdMessage="Motor test row too large";finishCalibration(false);return;}
  if(gotFuel)n+=snprintf(entry+n,sizeof(entry)-n,"%.3f",lph);
  entry[n++]=',';
  if(gotRPM)n+=snprintf(entry+n,sizeof(entry)-n,"%.2f",rpm);
  entry[n++]='\n';
  if(calFile.write((const uint8_t*)entry,n)!=(size_t)n){
    ++sdErrors;sdFaultLatched=true;sdMessage="Motor test SD write error";finishCalibration(false);
  }else{++calRows;if(!valid)++calInvalid;}
}
void handleCalibrationStart(){
  int channel=web.arg("channel").toInt(); // 0=both,1=J7,2=J6
  if(channel<0||channel>2){reply(false,"Invalid channel",400);return;}
  if(!sdReady||!cardInserted()){reply(false,"Insert a microSD card before motor test",409);return;}
  bool ok=startCalibration(channel-1);
  reply(ok,ok?"Passive motor test started. Record idle and RPM steps before export.":"Could not start motor test",ok?200:409);
}
void handleCalibrationMark(){
  if(!calActive){reply(false,"No active motor test",409);return;}
  String phase=web.arg("phase");
  if(phase!="idle"&&phase!="low"&&phase!="medium"&&phase!="high"&&phase!="cruise"&&phase!="custom"){
    reply(false,"Invalid phase",400);return;
  }
  String refRpm=web.arg("rpm");
  for(size_t i=0;i<refRpm.length();i++)if(!isDigit(refRpm[i])){reply(false,"Invalid reference RPM",400);return;}
  int rpm=refRpm.length()?refRpm.toInt():-1;
  if(rpm<-1||rpm>6000){reply(false,"Reference RPM must be 0-6000",400);return;}
  String fuel=web.arg("fuel");
  float flow=-1;
  if(fuel.length()){
    for(size_t i=0;i<fuel.length();i++)if(!isDigit(fuel[i])&&fuel[i]!='.'){reply(false,"Invalid reference fuel rate",400);return;}
    flow=fuel.toFloat();
    if(flow<0||flow>2000){reply(false,"Reference fuel rate out of range",400);return;}
  }
  calPhase=phase;calReferenceRPM=rpm;calReferenceFuel=flow;
  ++calSteps;calibrationMarker();
  reply(calActive,calActive?"Motor test step marked. Continue collecting data.":"SD write error stopped recording",calActive?200:500);
}
void handleCalibrationStop(){
  if(!calActive){reply(false,"No motor test is running",409);return;}
  finishCalibration(true);
  reply(true,"Motor test saved. Download the CSV to your device.");
}
void handleCalibrationDownload(){
  if(calActive){web.send(409,"text/plain","Stop the motor test before downloading its CSV");return;}
  if(!sdReady || !calName.length() || !SD.exists(calName)){
    web.send(404,"text/plain","No saved motor test on the inserted SD card");return;
  }
  File f=SD.open(calName,FILE_READ);
  if(!f){web.send(500,"text/plain","Unable to open test log");return;}
  web.sendHeader("Content-Disposition","attachment; filename=EngineLink-Motor-Test.csv");
  web.streamFile(f,"text/csv");f.close();
}

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
  appendCalibrationFrame(f,mid,ok,r.hex,gotFuel,lph,gotRPM,rpm);
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
  // Only a saved network configuration change may rebuild Wi-Fi interfaces.
  // Never periodically restart the AP just because STA lost its router.
  const bool withRouter=cfg.router&&cfg.ssid.length();
  WiFi.mode(withRouter?WIFI_AP_STA:WIFI_AP);
  WiFi.setSleep(false);
  WiFi.softAPConfig(IPAddress(192,168,4,1),IPAddress(192,168,4,1),IPAddress(255,255,255,0));
  apStarted=WiFi.softAP("EngineLink",cfg.apPassword.c_str(),6,false,4);
  if(withRouter){
    WiFi.setAutoReconnect(false);
    WiFi.begin(cfg.ssid.c_str(),cfg.wifiPassword.c_str());
    lastWifiRetry=millis();
  }else{
    ntpStarted=false;
    // In AP-only mode do not call WiFi.disconnect(): it can disturb AP state.
  }
}
void serviceNetwork(){
  if(pendingNetwork){pendingNetwork=false;applyNetwork();}
  // STA scans can force the AP radio onto another channel. Defer retries while
  // an AP client is using the control page; no router must not break direct Wi-Fi.
  if(cfg.router&&cfg.ssid.length()&&WiFi.status()!=WL_CONNECTED &&
      (uint32_t)(millis()-lastWifiRetry)>120000UL &&
      WiFi.softAPgetStationNum()==0){
    WiFi.reconnect();lastWifiRetry=millis();
  }
  if(cfg.router&&WiFi.status()==WL_CONNECTED&&!ntpStarted&&allowTime("ntp")){
    configTime(0,0,"pool.ntp.org","time.google.com");
    ntpStarted=true;
  }
  if(ntpNotification){
    ntpNotification=false;
    if(allowTime("ntp")&&validWall()){hasTime=true;timeSource="ntp";}
  }
}
const char *ledStateName(LedState st){
  switch(st){
    case LS_BOOT:return "Starting";
    case LS_SETUP:return "Setup: blue pulse";
    case LS_READY:return "Normal: green";
    case LS_ROUTER:return "Router reconnect: amber pulse";
    case LS_NO_SD:return "SD unavailable: amber double flash";
    case LS_WRITE_ERROR:return "SD error: red rapid flash";
    case LS_RX_ERROR:return "J1708 overflow: red double flash";
    case LS_TEST:return "Motor test recording: cyan pulse";
  }
  return "Unknown";
}
bool ledTestRunning(){return ledTestStartedMs && (uint32_t)(millis()-ledTestStartedMs)<16000UL;}
LedState ledFor(bool includeDemo){
  if(includeDemo&&ledTestRunning()){
    constexpr LedState demo[]={LS_SETUP,LS_READY,LS_ROUTER,LS_NO_SD,LS_WRITE_ERROR,LS_RX_ERROR,LS_TEST};
    return demo[((millis()-ledTestStartedMs)/2200UL)%7];
  }
  if(sdFaultLatched)return LS_WRITE_ERROR;
  if(lastUARTFaultMs && (uint32_t)(millis()-lastUARTFaultMs)<30000UL)return LS_RX_ERROR;
  if(calActive)return LS_TEST;
  if(!cfg.completed)return LS_SETUP;
  if(cfg.router&&cfg.ssid.length()&&WiFi.status()!=WL_CONNECTED)return LS_ROUTER;
  if(!sdReady)return LS_NO_SD;
  return LS_READY;
}
LedState selectedLED(){return ledFor(true);}
void updateLED(){
  if((uint32_t)(millis()-lastLed)<75)return;
  lastLed=millis();
  currentLED=selectedLED();
  const uint32_t t=millis();
  const uint32_t beat=t%1800;
  bool on=true;
  uint8_t level=ledBrightness;
  uint32_t color=0x13A880;
  switch(currentLED){
    case LS_SETUP: color=0x1C6EED;level=10+(uint8_t)(32.0f*(0.5f+0.5f*sinf((t%2000)*0.00314159f)));break;
    case LS_ROUTER: color=0xE59A21;on=(t%1200)<450;break;
    case LS_NO_SD: color=0xDF9D30;on=beat<120||((beat>250)&&(beat<380));break;
    case LS_WRITE_ERROR: color=0xE92E35;on=(t%360)<180;break;
    case LS_RX_ERROR: color=0xDF4150;on=beat<160||(beat>320&&beat<480);break;
    case LS_TEST: color=0x16C7CD;on=(t%1100)<200;break;
    case LS_BOOT: color=0x1C6EED;break;
    case LS_READY: color=0x17A77E;level=27;break;
  }
  if(!on)level=0;
  uint32_t rgb=level?color:0;
  uint32_t fingerprint=(rgb<<8)|level;
  if(fingerprint==ledPrev)return;
  led.setBrightness(level);
  led.setPixelColor(0,led.Color((rgb>>16)&255,(rgb>>8)&255,rgb&255));
  led.show();
  ledPrev=fingerprint;
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
  web.send(code,"application/json; charset=utf-8",s);
}
void reply(bool ok,const String &message,int code){
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
  String s;s.reserve(12000);
  s+="{\"version\":\"" EL_VERSION "\",\"setup\":";
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
  s+=",\"apStarts\":";s+=(unsigned long)apStarts;
  s+=",\"apStops\":";s+=(unsigned long)apStops;
  s+=",\"staDisconnects\":";s+=(unsigned long)staDisconnects;
  s+=",\"apStarted\":";s+=apStarted?"true":"false";
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
  s+="},\"test\":{\"running\":";s+=calActive?"true":"false";
  s+=",\"rows\":";s+=calRows;
  s+=",\"invalid\":";s+=calInvalid;
  s+=",\"step\":";s+=calSteps;
  s+=",\"phase\":";jsonString(s,calPhase);
  s+=",\"channel\":";s+=calChannel;
  s+=",\"referenceRPM\":";if(calReferenceRPM>=0)s+=calReferenceRPM;else s+="null";
  s+=",\"referenceFuel\":";if(calReferenceFuel>=0)s+=String(calReferenceFuel,3);else s+="null";
  s+=",\"file\":";jsonString(s,calName);
  s+=",\"seconds\":";s+=calActive?((millis()-calStartMs)/1000UL):0;
  // Cached: avoid an SD directory lookup on every status poll.
  s+=",\"available\":";s+=(sdReady&&!calActive&&testFileAvailable)?"true":"false";
  // Report the real system state even while the LED demo sequence runs, so a
  // test never hides an active fault in the UI.
  s+="},\"led\":{\"name\":";jsonString(s,ledStateName(ledFor(false)));
  s+=",\"status\":";s+=(unsigned)ledFor(false);
  s+=",\"testing\":";s+=ledTestRunning()?"true":"false";
  s+="},\"hardware\":{\"heap\":";
  s+=ESP.getFreeHeap();
  s+=",\"minHeap\":";s+=ESP.getMinFreeHeap();
  s+=",\"psram\":";s+=psramFound()?ESP.getPsramSize():0;
  s+=",\"opto\":";s+=digitalRead(OPTO_PIN);
  s+=",\"canReady\":";s+=canReady?"true":"false";
  s+=",\"nmeaRx\":";s+=ctr.nmeaFrames;
  s+=",\"resetReason\":";s+=(unsigned int)esp_reset_reason();
  s+=",\"uartReady\":[";s+=uartReady[0]?"true":"false";s+=",";s+=uartReady[1]?"true":"false";s+="]";
  s+="},\"channels\":[";
  for(int c=0;c<2;c++){
    if(c)s+=',';
    s+="{\"name\":\"J";s+=c==0?"7":"6";s+="\",\"frames\":";
    s+=ctr.frames[c];
    s+=",\"invalid\":";s+=ctr.invalid[c];
    s+=",\"drops\":";s+=(unsigned long)ctr.rxDropped[c];
    s+=",\"uartFaults\":";s+=(unsigned long)ctr.uartFaults[c];
    s+=",\"lineErrors\":";s+=(unsigned long)ctr.lineErrors[c];
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
  s+="],\"queue\":{\"capacity\":";s+=(unsigned long)framesQueueLength;
  s+=",\"waiting\":";s+=framesQueue?(unsigned long)uxQueueMessagesWaiting(framesQueue):0UL;
  s+="},\"fuel\":{\"total\":";
  if(totalAvailable)s+=String(total,3);else s+="null";
  s+=",\"portChannel\":";s+=port;
  s+=",\"starboardChannel\":";s+=starboard;
  s+=",\"sogKnots\":";
  if(speedAvailable)s+=String(sogKnots,2);else s+="null";
  s+=",\"litersPerNm\":";
  if(speedAvailable&&totalAvailable)s+=String(total/sogKnots,3);else s+="null";
  s+="},\"recent\":[";
  uint8_t n=recentCount; // all 24, so the per-channel filter is not starved
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
  // Start/stop CAN first: a failure must not leave half-applied settings in RAM.
  bool newCAN=web.arg("can")=="1";
  if(newCAN!=cfg.canListen){
    if(!newCAN)stopCAN();
    else if(!startCAN()){reply(false,"NMEA2000 listen-only interface could not start",409);return;}
    cfg.canListen=newCAN;
  }
  bool changeNetwork=cfg.router!=(network=="router")||cfg.ssid!=ssid;
  String newRouterPass=web.arg("pass");
  if(newRouterPass.length()){cfg.wifiPassword=newRouterPass;changeNetwork=true;}
  if(apPass.length()&&apPass!=cfg.apPassword){cfg.apPassword=apPass;changeNetwork=true;}
  cfg.router=network=="router";
  cfg.ssid=ssid;
  cfg.timePreference=preference;
  cfg.portChannel=(uint8_t)port;
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
void handleLEDTest(){
  ledTestStartedMs=millis();ledPrev=0xFFFFFFFF;
  reply(true,"LED status sequence started (16 seconds).");
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
    // no-cache: after a firmware update Safari must not keep an old UI.
    web.sendHeader("Cache-Control","no-cache");
    web.send_P(200,"text/html; charset=utf-8",ENGINE_UI);
  });
  web.on("/favicon.ico",HTTP_GET,[](){web.send(204,"text/plain","");});
  web.on("/api/state",HTTP_GET,handleState);
  web.on("/api/setup",HTTP_POST,handleOptions);
  web.on("/api/settings",HTTP_POST,handleOptions);
  web.on("/api/phone-time",HTTP_POST,handleTime);
  web.on("/api/led/test",HTTP_POST,handleLEDTest);
  web.on("/api/test/start",HTTP_POST,handleCalibrationStart);
  web.on("/api/test/mark",HTTP_POST,handleCalibrationMark);
  web.on("/api/test/stop",HTTP_POST,handleCalibrationStop);
  web.on("/api/test/download",HTTP_GET,handleCalibrationDownload);
  web.on("/api/sd/remount",HTTP_POST,[](){
    if(sdLogging||calActive){reply(false,"Stop recording before remount",409);return;}
    SD.end();sdReady=false;initSD();refreshTestAvailable();reply(sdReady,sdMessage);
  });
  web.on("/api/sd/stop",HTTP_POST,[](){if(calActive){reply(false,"Stop motor test first",409);return;}stopLog();reply(true,"Session recording stopped");});
  web.on("/api/sd/start",HTTP_POST,[](){if(calActive){reply(false,"Stop motor test first",409);return;}if(sdLogging){reply(true,"Already recording");return;}initSD();reply(sdLogging,sdMessage);});
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
  Serial.println("\nEngineLink " EL_VERSION " - passive engine monitoring");
  bootId=esp_random();
  pinMode(SD_DETECT,INPUT);
  pinMode(OPTO_PIN,INPUT);
  loadSettings();
  pref.begin("enginelink",true);calName=pref.getString("testfile","");pref.end();
  uint8_t *queueStorage=(uint8_t*)heap_caps_malloc(FRAME_QUEUE_PSRAM*sizeof(Frame),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(queueStorage){
    framesQueue=xQueueCreateStatic(FRAME_QUEUE_PSRAM,sizeof(Frame),queueStorage,&framesQueueState);
    framesQueueLength=FRAME_QUEUE_PSRAM;
  }
  if(!framesQueue){
    framesQueue=xQueueCreate(FRAME_QUEUE_FALLBACK,sizeof(Frame));
    framesQueueLength=framesQueue?FRAME_QUEUE_FALLBACK:0;
    Serial.println("PSRAM frame queue unavailable; using small internal queue");
  }
  if(framesQueue){
    uartReady[0]=initUART(channels[0],RX_J7);
    uartReady[1]=initUART(channels[1],RX_J6);
    if(!uartReady[0])Serial.println("J7 UART init failed");
    if(!uartReady[1])Serial.println("J6 UART init failed");
  }
  cardWasInserted=cardInserted();
  initSD();
  refreshTestAvailable();
  WiFi.onEvent(onWiFi);
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
  while(framesQueue&&handled<32&&xQueueReceive(framesQueue,&f,0)==pdTRUE){processFrame(f);handled++;}
  serviceCAN();
  web.handleClient();
  serviceSD();
  serviceNetwork();
  updateLED();
  delay(1);
}
