/*
 * EngineLink 0.5.0 — application firmware
 * ESP32-S3-WROOM-1-N8R8, 8 MB flash / 8 MB PSRAM.
 *
 * Captures actual J1708 RX bytes on J7 and J6. Engines are decoded either as
 * Volvo Penta KAD/EDC1 (request/response, VODIA scaling) or SAE J1587.
 * J1708 TX pins are attached ONLY while KAD polling is on for a channel
 * (switched on by the user, by "Read engines automatically", which is off by
 * default, or for a 30 s connection test), and only the read-parameter
 * request can be sent. CAN is listen-only if user enables it.
 * Never claim a simulated or missing value is measured.
 *
 * On top of the capture core this firmware keeps trips, a fuel log with
 * tank estimate, learned economy and engine-health figures, and serves the
 * phone app in ui.h.
 *
 * Read docs/PRODUCTION_ALPHA.md before attaching to any engine or CAN bus.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <Preferences.h>
#include <SPI.h>
#include <SD.h>
#include <LittleFS.h>
#include <Adafruit_NeoPixel.h>
#include <driver/uart.h>
#include <driver/twai.h>
#include <driver/gpio.h>
#include <esp_timer.h>
#include <sys/time.h>
#include <time.h>
#include <esp_sntp.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <esp_wifi.h>
#include <esp_ota_ops.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include "ui.h"
#include "icons.h"
#include "fuelcal.h"
#include "ledpatterns.h"
#include "diagrules.h"

#define EL_VERSION "0.7.3"
// Releases list published by the Pages workflow; firmware files come from
// GitHub Releases of the same repository. Nothing else is downloaded.
#define EL_RELEASES_URL "https://kaelor9.github.io/EngineLink/releases.json"
#define EL_RELEASE_PREFIX "https://github.com/Kaelor9/EngineLink/releases/download/"

constexpr uint8_t RX_J7=21, RX_J6=17;
constexpr uint8_t TX_J7=47, TX_J6=18; // attached only while KAD polling is on
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
constexpr uint32_t PARAM_FRESH_MS=30000;
constexpr uint32_t CSV_FLUSH_MS=1200;
constexpr float ENGINE_RUN_RPM=300.0f;        // above cranking speed
constexpr uint32_t ENGINE_STOP_DEBOUNCE_MS=10000;
constexpr uint32_t TOTALS_SAVE_MS=300000;      // NVS wear: at most every 5 min
constexpr uint16_t HISTORY_LEN=360;            // 1 hour at 10 s
constexpr uint32_t HISTORY_STEP_MS=10000;
constexpr uint8_t MAX_SEGMENTS=12;
constexpr uint8_t MAX_PIDS=96;
// The stock WebServer serves one socket at a time. A socket that Safari opens
// ahead of time and leaves silent, or one that stalls during a Wi-Fi blip,
// blocks every other request for up to 5 s: the app then shows "Reconnecting".
// This server accepts every connection at once, serves whichever socket has a
// request waiting, and keeps sockets open between requests (HTTP keep-alive),
// so the app's twice-a-second poll reuses one connection instead of setting up
// a new one each time, which is where a weak link loses most requests.
constexpr uint8_t HTTP_POOL=8;
constexpr uint32_t HTTP_IDLE_MS=5000;      // silent sockets are closed after this
constexpr uint32_t HTTP_IO_TIMEOUT_MS=2000;

class EngineLinkWebServer : public WebServer {
public:
  explicit EngineLinkWebServer(int port):WebServer(port){}
  uint32_t served=0,reused=0,evicted=0,slowestMs=0;
  uint8_t openSockets()const{uint8_t n=0;for(const Slot &s:pool)if(s.used)n++;return n;}
  void handleClient() override {
    const uint32_t now=millis();
    for(;;){
      NetworkClient c=_server.accept();
      if(!c)break;
      c.setNoDelay(true);
      int slot=-1,oldest=0;
      for(int i=0;i<HTTP_POOL;i++){
        if(!pool[i].used){slot=i;break;}
        if((int32_t)(pool[oldest].since-pool[i].since)>0)oldest=i;
      }
      if(slot<0){slot=oldest;pool[slot].c.stop();++evicted;}
      pool[slot].c=c;pool[slot].since=now;pool[slot].used=true;pool[slot].requests=0;
    }
    for(Slot &s:pool){
      if(!s.used||s.c.available())continue;
      if(!s.c.connected()||now-s.since>HTTP_IDLE_MS){s.c.stop();s.used=false;}
    }
    for(int k=0;k<HTTP_POOL;k++){
      const int i=(nextSlot+k)%HTTP_POOL;
      if(pool[i].used&&pool[i].c.available()){nextSlot=(i+1)%HTTP_POOL;serve(pool[i]);break;}
    }
  }
private:
  struct Slot{NetworkClient c;uint32_t since=0;uint16_t requests=0;bool used=false;};
  Slot pool[HTTP_POOL];
  int nextSlot=0;
  bool wrote=false,keepAlive=false,rewriteHeader=false;
  void serve(Slot &s){
    const uint32_t t0=millis();
    _currentClient=s.c;
    _currentClient.setTimeout(HTTP_IO_TIMEOUT_MS);
    _currentStatus=HC_WAIT_READ;_statusChange=t0;
    wrote=false;keepAlive=false;
    if(_parseRequest(_currentClient)){
      _contentLength=CONTENT_LENGTH_NOT_SET;_responseCode=0;_clearResponseHeaders();
      // Only HTTP/1.1 frames every response (length or chunks) for reuse.
      keepAlive=_currentVersion==1&&!header("Connection").equalsIgnoreCase("close");
      rewriteHeader=keepAlive;
      _handleRequest();
      ++served;if(s.requests)++reused;
      if(s.requests<65535)s.requests++;
    }
    rewriteHeader=false;
    const bool keep=keepAlive&&wrote&&_currentClient.connected();
    _currentClient=NetworkClient();
    _currentStatus=HC_NONE;_currentUpload.reset();_currentRaw.reset();
    if(keep)s.since=millis();else{s.c.stop();s.used=false;}
    const uint32_t ms=millis()-t0;if(ms>slowestMs)slowestMs=ms;
  }
protected:
  // The response header is written in one piece. WebServer always says
  // "Connection: close"; for a keep-alive request that line is swapped.
  size_t _currentClientWrite(const char *b,size_t l) override {
    wrote=true;
    if(rewriteHeader&&l>8&&memcmp(b,"HTTP/1.",7)==0){
      rewriteHeader=false;
      String h(b,(unsigned int)l);
      if(h.indexOf("\r\nConnection: close\r\n")<0){keepAlive=false;return _currentClient.write(b,l);}
      h.replace("\r\nConnection: close\r\n","\r\nConnection: keep-alive\r\nKeep-Alive: timeout=4\r\n");
      return _currentClient.write(h.c_str(),h.length())==h.length()?l:0;
    }
    return _currentClient.write(b,l);
  }
  size_t _currentClientWrite_P(PGM_P b,size_t l) override {wrote=true;return _currentClient.write_P(b,l);}
};

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
  char dir='R';          // R = received, Q = request seen on bus, T = our own request
  int16_t kadIndex=-1;
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
  uint32_t rpmSamplesAtFuel=0; // rpm answers so far when the last fuel answer came
  uint8_t lastMID=0;
  // Engine-run segment tracking (physical channel based)
  bool running=false;
  uint64_t runStartMs=0,lastRunMs=0,lastTickMs=0;
  int64_t runStartUtc=0;
  double runStartLiters=0;
  float runMaxRpm=0;
};
// Persistent per physical channel. Only fuel integrated between valid,
// recent ECU samples is counted; gaps are never filled in.
struct TotalsV3 {
  uint32_t magic=0xE1170003;
  double lifetimeL[2]={0,0};
  double tripL[2]={0,0};
  uint32_t runSeconds[2]={0,0};
  uint32_t tripRunSeconds[2]={0,0};
  int64_t tripStartUtc=0;
  uint32_t tripResets=0;
};
struct TotalsV4 {
  uint32_t magic=0xE1170004;
  double lifetimeL[2]={0,0};
  double tripL[2]={0,0};
  uint32_t runSeconds[2]={0,0};
  uint32_t tripRunSeconds[2]={0,0};
  int64_t tripStartUtc=0;
  uint32_t tripResets=0;
  double lifeRawH[2]={0,0};
  double tripRawH[2]={0,0};
};
// Fuel counters at one moment. Fuel used between two snapshots is computed
// with today's scale and correction, so calibration also fixes the past.
// Fuel is also counted per engine-speed band (idle, part load, cruise), so
// calibration can correct each band and still fix everything recorded.
// Raw counted before 0.6.0 has no band ("legacy" = total minus the bands).
constexpr uint8_t FUEL_BANDS=3;
constexpr float BAND_PART_RPM=1100.0f, BAND_CRUISE_RPM=2500.0f;
struct FuelSnapV1 { double raw[2]={0,0}, lit[2]={0,0}, missed=0; };
struct FuelSnap { double raw[2]={0,0}, lit[2]={0,0}, missed=0; double rawB[2][FUEL_BANDS]={}; };
// Learned figures per 250 rpm step from 1000 rpm (index 0) to 3750+ (11).
constexpr uint8_t RPM_BUCKETS=12;
constexpr uint8_t ZONES=5; // idle <1000, low <2000, rising <2500, cruise <3000, fast
struct TotalsV5 {
  uint32_t magic=0xE1170005;
  double lifetimeL[2]={0,0};   // J1587 path: liters as decoded
  double tripL[2]={0,0};
  uint32_t runSeconds[2]={0,0};
  uint32_t tripRunSeconds[2]={0,0};
  int64_t tripStartUtc=0;
  uint32_t tripResets=0;
  // KAD path: integral of the RAW fuel-rate count over hours. Liters are
  // raw x scale x correction, so a later scale/correction choice applies to
  // everything already recorded.
  double lifeRawH[2]={0,0};
  double tripRawH[2]={0,0};
  // ---- 0.5.0 ----
  // The trip that is running now (first engine on -> both engines off).
  bool tripActive=false;
  uint32_t tripBoot=0;          // boot the trip started in (for raw export)
  uint32_t tripStartSec=0;      // uptime seconds at trip start
  uint32_t tripAnySec=0;        // seconds with at least one engine running
  int64_t tripLastUtc=0;        // last time an engine ran, wall clock
  uint16_t tripMaxRpm=0;
  uint32_t nextTripId=1;
  // Tank: fuel aboard = tankRefL - fuel used since tankRef.
  double tankRefL=-1;           // -1 = never set
  FuelSnapV1 tankRef;
  double estMissedL=0;          // estimated from engine hour meters
  uint32_t missedEvents=0;
  float lastEcuHours[2]={0,0};
  // Economy, all trips since the season started.
  int64_t seasonStartUtc=0;
  float bucketSec[RPM_BUCKETS]={}, bucketL[RPM_BUCKETS]={};
  float bucketSogSec[RPM_BUCKETS]={}, bucketSogL[RPM_BUCKETS]={}, bucketNm[RPM_BUCKETS]={};
  float zoneL[ZONES]={}, zoneSec[ZONES]={}, zoneNm[ZONES]={};
};
struct Totals {
  uint32_t magic=0xE1170006;
  double lifetimeL[2]={0,0};   // J1587 path: liters as decoded
  double tripL[2]={0,0};
  uint32_t runSeconds[2]={0,0};
  uint32_t tripRunSeconds[2]={0,0};
  int64_t tripStartUtc=0;
  uint32_t tripResets=0;
  // KAD path: integral of the RAW fuel-rate count over hours. Liters are
  // raw x scale x correction, so a later scale/correction choice applies to
  // everything already recorded.
  double lifeRawH[2]={0,0};
  double tripRawH[2]={0,0};
  // ---- 0.5.0 ----
  // The trip that is running now (first engine on -> both engines off).
  bool tripActive=false;
  uint32_t tripBoot=0;          // boot the trip started in (for raw export)
  uint32_t tripStartSec=0;      // uptime seconds at trip start
  uint32_t tripAnySec=0;        // seconds with at least one engine running
  int64_t tripLastUtc=0;        // last time an engine ran, wall clock
  uint16_t tripMaxRpm=0;
  uint32_t nextTripId=1;
  // Tank: fuel aboard = tankRefL - fuel used since tankRef.
  double tankRefL=-1;           // -1 = never set
  FuelSnap tankRef;
  double estMissedL=0;          // estimated from engine hour meters
  uint32_t missedEvents=0;
  float lastEcuHours[2]={0,0};
  // Economy, all trips since the season started.
  int64_t seasonStartUtc=0;
  float bucketSec[RPM_BUCKETS]={}, bucketL[RPM_BUCKETS]={};
  float bucketSogSec[RPM_BUCKETS]={}, bucketSogL[RPM_BUCKETS]={}, bucketNm[RPM_BUCKETS]={};
  float zoneL[ZONES]={}, zoneSec[ZONES]={}, zoneNm[ZONES]={};
  // ---- 0.6.0 ----
  double lifeRawB[2][FUEL_BANDS]={}, tripRawB[2][FUEL_BANDS]={};
  double tankRefVar=0;          // variance (L^2) of the tank level at tankRef
  uint32_t gapFillSec[2]={0,0}; // seconds of fuel interpolated over short gaps
};
// One finished trip. Liters are kept as the raw KAD integral (or J1587
// liters) so later calibration also corrects old trips.
struct TripRecV1 {
  uint32_t id=0;
  uint32_t group=0;             // id of the first trip of a combined group, 0 = alone
  int64_t startUtc=0,endUtc=0;
  uint32_t anySec=0;            // running time (at least one engine)
  uint32_t bootId=0,startSec=0,endSec=0;
  float rawH[2]={0,0};
  float litJ[2]={0,0};
  uint16_t maxRpm=0;
  uint8_t people=0,sea=0,wind=0,extras=0; // notes, 0 = not set; extras bits
  char note[40]={};
};
struct TripRec {
  uint32_t id=0;
  uint32_t group=0;             // id of the first trip of a combined group, 0 = alone
  int64_t startUtc=0,endUtc=0;
  uint32_t anySec=0;            // running time (at least one engine)
  uint32_t bootId=0,startSec=0,endSec=0;
  float rawH[2]={0,0};
  float litJ[2]={0,0};
  uint16_t maxRpm=0;
  uint8_t people=0,sea=0,wind=0,extras=0; // notes, 0 = not set; extras bits
  char note[40]={};
  float rawB[2][FUEL_BANDS]={};  // 0.6.0: raw per engine-speed band
};
constexpr uint8_t MAX_TRIPS=60;
struct FuelRecV1 {
  int64_t utc=0;
  float litres=0;               // 0 allowed for "tank full" without pump litres
  float price=0;                // per litre, 0 = not given
  uint8_t full=0;
  float counted=-1;             // fuel EngineLink counted since the previous full tank, -1 = unknown
  float rawDelta=0;             // uncorrected raw integral since that full tank (scale decision)
  FuelSnapV1 at;                  // counters when this was logged
  double prevRefL=-1;           // tank reference before this entry (undo of the newest entry)
  FuelSnapV1 prevRef;
};
struct FuelRec {
  int64_t utc=0;
  float litres=0;               // 0 allowed for "tank full" without pump litres
  float price=0;                // per litre, 0 = not given
  uint8_t full=0;
  float counted=-1;             // fuel EngineLink counted since the previous full tank, -1 = unknown
  float rawDelta=0;             // uncorrected raw integral since that full tank (scale decision)
  FuelSnap at;                  // counters when this was logged
  double prevRefL=-1;           // tank reference before this entry (undo of the newest entry)
  FuelSnap prevRef;
  // ---- 0.6.0 ----
  uint8_t quality=0;            // 0 not full, 1 nozzle clicked off, 2 topped up to the filler pipe
  uint8_t calUse=0;             // 0 automatic, 1 use although it looks odd, 2 never use
};
constexpr uint8_t MAX_FUEL=30;
// Engine-health baselines: per engine and rpm step, a slow average of what is
// normal; compared with the latest trip at the same rpm.
constexpr uint8_t HEALTH_METRICS=4; // coolant, boost, charge air, fuel L/h
struct Health {
  uint32_t magic=0xE11A0001;
  float base[2][RPM_BUCKETS][HEALTH_METRICS]={};
  uint16_t baseTrips[2][RPM_BUCKETS]={};
  float dev[2][HEALTH_METRICS]={};     // latest trip vs normal (fraction; coolant in °C)
  uint16_t devSamples[2]={0,0};
  uint16_t devRpm[2]={0,0};
  float balance=0;                      // starboard vs port fuel at same rpm (fraction)
  int64_t updatedUtc=0;
};
// ---- 0.7.0: diagnosis, black box, trip log ----
// One second of both engines (physical channels J7, J6). NA16 = not known.
constexpr int16_t NA16=INT16_MIN;
struct Sample {
  uint32_t up=0;                 // seconds since EngineLink started
  int16_t rpm[2]={NA16,NA16},req[2]={NA16,NA16},lph10[2]={NA16,NA16};
  int16_t cool10[2]={NA16,NA16},air10[2]={NA16,NA16},boost10[2]={NA16,NA16},volt100[2]={NA16,NA16};
  uint16_t st263[2]={0xFFFF,0xFFFF},st264[2]={0xFFFF,0xFFFF};
};
constexpr uint16_t RING_N=3600;   // the last hour, in PSRAM
struct EventRec {
  uint32_t id=0;
  int64_t utc=0;
  uint32_t boot=0,up=0;
  uint8_t type=0,eng=0,sev=0,box=0; // box: 0 none, 1 saving, 2 saved on the SD card, 3 not saved
  float value=0,ref=0;
  uint8_t ack=0,pad[3]={};
};
constexpr uint8_t MAX_EVENTS=100;
// Header of a trip log or black box file: Sample records follow.
struct SeriesHead { uint32_t magic; uint32_t id,boot,startUp; int64_t startUtc; int8_t port; uint8_t pad[3]; };
constexpr uint32_t SERIES_MAGIC=0x31535245; // "ERS1"
struct Segment {
  uint32_t boot=0;
  uint64_t startMs=0,endMs=0;
  int64_t startUtc=0;
  float liters=0,maxRpm=0;
  uint8_t ch=0;
};
struct PidSeen {
  uint8_t ch=0,mid=0,pid=0,len=0;
  uint8_t data[8]={};
  uint32_t count=0;
  uint64_t at=0;
};
struct Settings {
  bool completed=false;
  bool router=false;
  bool canListen=false;
  uint8_t portChannel=1; // 1=J7 port, 2=J6 port, 0=unassigned
  uint8_t apChannel=6;
  String apPassword="12345678";
  String ssid="";
  String wifiPassword="";
  String timePreference="auto";
  uint8_t protocol=1;        // 1 = Volvo Penta KAD/EDC1, 0 = SAE J1587
  bool kadBigEndian=true;    // byte order of KAD reply values (unverified)
  uint16_t kadFuelDiv=128;   // PID289 scale 1/128 (M_902) or 1/256
  uint8_t kadVolt=0;         // PID293: 0 = VODIA 0.01308 V, 1 = Yacht Devices 0.0125 V
  float fuelCorr[2]={1.0f,1.0f}; // per physical channel, from tank-to-tank
  // ---- 0.5.0 ----
  String apName="EngineLink";
  String hostName="enginelink"; // http://<hostName>.local and the name the router sees
  float tankL=0;             // 0 = not set yet
  uint8_t reserve=10;        // percent, 0 = off
  float defaultPrice=0;      // per litre, in the currency chosen in the app
  uint8_t tripEndMin=2;      // trip ends when both engines are off this long
  bool autoPoll=false;       // "Read engines automatically" (sends read requests)
  uint16_t rawKeepDays=90;   // 0 = keep forever
  bool ntpOn=true;
  String ntpServer="pool.ntp.org";
  bool fuelScaleOk=false;    // user confirmed 1/128 or 1/256
  bool voltOk=false;         // user checked the voltage factor
  uint8_t fineTunes=0;       // full fill-ups used to fine-tune the fuel figures
  float lastTuneDev=0;       // pump vs counted at the last fine-tune
  String prefs="{}";         // app-only preferences (JSON written by the app)
  // ---- 0.6.0: calibration result, gear learned at the dock ----
  float bandAdj[3]={1.0f,1.0f,1.0f}; // idle, part load, cruise relative to fuelCorr
  float calSig=0;            // relative 1-sigma of the fuel figures, 0 = not calibrated
  uint8_t calFills=0;        // fill-ups used by the calibration
  float calLitres=0;         // pump litres those fill-ups cover
  uint16_t gearMaskF=0,gearValF=0,gearMaskR=0,gearValR=0; // mask 0 = not learned
};
constexpr uint8_t PROTO_J1587=0, PROTO_KAD=1;
struct Counters {
  uint32_t frames[2]={0,0};
  uint32_t invalid[2]={0,0};
  uint32_t overflows[2]={0,0};
  volatile uint32_t rxDropped[2]={0,0};
  volatile uint32_t uartFaults[2]={0,0};
  volatile uint32_t lineErrors[2]={0,0};
  uint32_t nmeaFrames=0;
  uint32_t pidTableFull=0;
};

// ---------- Volvo Penta KAD / EDC1 ("NIRA EDC1") ----------
// Request frame and checksum are RECONSTRUCTED from PentaEDC1.exe; the reply
// layout (header, value position, byte order) is NOT wire-verified. Scalings
// come from the VODIA ParameterList for program type M_902 (KAD300).
// See docs/KAD300_EDC1.md. Raw bytes are always logged.
struct KadParam { uint16_t index; char type; uint8_t len; float factor; };
constexpr KadParam KAD_PARAMS[]={
  {256,'U',2,0.25f},       // engine speed, rpm
  {258,'U',2,0.03125f},    // air inlet pressure, kPa abs
  {259,'S',2,0.25f},       // coolant temperature, C
  {260,'S',2,0.25f},       // fuel temperature, C
  {261,'S',2,0.25f},       // boost temperature, C
  {263,'B',2,0},           // panel buttons / start input (bitfield)
  {264,'B',2,0},           // gear bits 0x18, relays, indicators (bitfield)
  {265,'S',2,0.03125f},    // boost pressure, kPa rel
  {266,'U',3,0.0027778f},  // total engine hours, h
  {267,'U',1,1.0f},        // active MS unit
  {288,'U',2,0.012207f},   // relative load, %
  {289,'U',2,0.0078125f},  // fuel rate, L/h (1/128 M_902; 1/256 alternative)
  {291,'U',2,0.25f},       // engine speed reference, rpm
  {293,'U',2,0.01308f},    // system voltage, V (VODIA; Yacht Devices 0.0125)
  {295,'S',2,0.0061035f},  // rod position, %
  {296,'S',2,0.015625f},   // alpha, deg
};
// RPM and fuel are asked for twice as often as the rest.
constexpr uint16_t KAD_POLL_FAST[]={256,289};
constexpr uint16_t KAD_POLL_SLOW[]={293,259,258,265,260,261,288,291,264,266,295,263,296,267};
constexpr uint8_t KAD_SLOW_N=sizeof(KAD_POLL_SLOW)/sizeof(KAD_POLL_SLOW[0]);
constexpr uint32_t KAD_MIN_POLL_MS=80;    // fastest pace: about 12 requests a second
constexpr uint16_t KAD_DISC_FIRST=256,KAD_DISC_LAST=319;
constexpr uint32_t KAD_POLL_MS=220;       // <= ~4.5 requests/s per channel
constexpr uint32_t KAD_REPLY_MS=300;      // reply must follow its request
constexpr uint32_t KAD_BUS_IDLE_MS=20;    // never start a request on a busy bus
constexpr uint32_t KAD_MAX_UNANSWERED=40; // then stop polling by itself
struct KadSeen {
  uint8_t ch=0,len=0;
  uint16_t index=0;
  uint8_t reply[12]={};
  uint32_t count=0;
  uint64_t at=0;
  uint32_t be=0,le=0;
};
constexpr uint8_t MAX_KAD_SEEN=48;
struct KadLink {
  bool pending=false,ownPending=false,polling=false,txAttached=false;
  uint16_t pendingIndex=0;
  uint64_t pendingAt=0,lastReplyAt=0,lastForeignAt=0;
  uint32_t requests=0,replies=0,foreignRequests=0,unanswered=0,txErrors=0,orphanReplies=0;
  uint32_t lastPollMs=0,startedMs=0;
  uint8_t seq=0,slowPos=0;
  String stopReason="";
  // 0.5.0: reply time and why polling runs (user, automatic or a test).
  uint64_t replyMsSum=0;
  uint32_t replyCount=0;
  bool byAuto=false,byTest=false;
  uint32_t lastAutoTry=0;
  // 0.7.0: pace follows the engine's answers (see serviceKad).
  uint16_t intervalMs=220;
  uint16_t winReq=0,winRep=0;
  float answered=NAN;  // share of requests answered in the last full window
  uint8_t miss[16]={};          // unanswered in a row, per slow value, while others answer
  uint16_t skip=0;              // slow values this engine does not answer (bits)
  bool discActive=false;        // "Discover more values" running
  uint16_t discPos=0;
  uint64_t discFound=0;         // answered indices KAD_DISC_FIRST.. (bits)
  bool discDone=false;
};
struct SdCounts { uint32_t files=0; uint64_t rawBytes=0; uint32_t rawFiles=0; uint32_t exports=0; };
// Result of the 30 s connection test per connector, kept in NVS.
struct DiagTest {
  int64_t utc=0;
  uint16_t requests=0,replies=0,checksumErrors=0;
  uint16_t replyMs=0;
};
// Result of decoding one frame, used for logging.
struct Decoded {
  uint8_t mid=0;
  bool gotFuel=false,gotRPM=false;
  float lph=0,rpm=0;
  int32_t kadIndex=-1;      // DataINDEX of a KAD request or matched reply
  bool kadRequest=false,kadOwn=false,kadEcho=false,kadValueOk=false;
  int32_t kadValue=0;
};

EngineLinkWebServer web(80);
Preferences pref;
Adafruit_NeoPixel led(1, LED_PIN, NEO_GRB+NEO_KHZ800);
Settings cfg;
Counters ctr;
Motor motor[2];
Totals totals;
bool totalsDirty=false;
uint32_t lastTotalsSave=0;
Segment segments[MAX_SEGMENTS];
uint8_t segmentIndex=0,segmentCount=0;
PidSeen pids[MAX_PIDS];
uint8_t pidCount=0;
KadSeen kadSeen[MAX_KAD_SEEN];
uint8_t kadSeenCount=0;
KadLink kad[2];
// Latest decoded value per KAD parameter (position in KAD_PARAMS), signed raw.
constexpr uint8_t KAD_N=sizeof(KAD_PARAMS)/sizeof(KAD_PARAMS[0]);
int32_t kadRawV[2][KAD_N];
uint64_t kadRawAt[2][KAD_N];
volatile uint32_t lastRxActivityMs[2]={0,0};
int16_t histFuel[2][HISTORY_LEN];   // centi-L/h, -1 = unknown
uint16_t histRpm[2][HISTORY_LEN];   // RPM, 0xFFFF = unknown
int16_t histSog[HISTORY_LEN];       // centi-knots, -1 = unknown
uint16_t histIndex=0,histCount=0;
uint32_t lastHistory=0,lastEngineService=0,lastSerialStatus=0;
Channel channels[2]={{UART_NUM_1,0},{UART_NUM_2,1}};
QueueHandle_t framesQueue=nullptr;
StaticQueue_t framesQueueState;
UBaseType_t framesQueueLength=0;
Recent recent[24];
uint8_t recentIndex=0,recentCount=0;
uint32_t bootId=0,lastFlush=0,lastWifiRetry=0,lastLed=0,ledPrev=0xFFFFFFFF;
uint32_t ledTestStartedMs=0;
uint8_t ledBrightness=42;
bool sdFaultLatched=false;
bool uartReady[2]={false,false};
bool sdReady=false,sdLogging=false,canReady=false,pendingNetwork=false,mdnsReady=false;
bool hasTime=false,ntpStarted=false;
String timeSource="none",sdMessage="No card",sessionName;
File logFile,calFile;
bool calActive=false,calResumeLogging=false;
int8_t calChannel=-1; // -1 both, 0 J7, 1 J6
uint32_t calRows=0,calInvalid=0,calSteps=0,calStartMs=0,calStepStartMs=0;
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
uint32_t loopMaxMs=0;
// Wi-Fi firmware update
bool otaActive=false,otaResumeLogging=false,otaChecked=false;
String otaError="";
size_t otaBytes=0;
uint32_t restartAt=0;
uint32_t sketchKB=0; // cached: getSketchSize() verifies the whole image
// ---- 0.5.0 state ----
TripRec trips[MAX_TRIPS];
uint8_t tripCount=0;            // trips[0] is the newest
FuelRec fuelLog[MAX_FUEL];
uint8_t fuelCount=0;            // fuelLog[0] is the newest
diag::Rules diagRules;
diag::Life life;
bool lifeDirty=false;
EventRec events[MAX_EVENTS];
uint8_t eventCount=0;           // events[0] is the newest
uint32_t nextEventId=1;
Sample *ring=nullptr;
uint16_t ringHead=0,ringCount=0;
File tripSeries;
uint32_t tripSeriesId=0,tripSeriesFlush=0;
uint32_t markLedUntil=0;
uint32_t pendingBox[8];
uint8_t pendingBoxN=0;
bool lifeWasRunning[2]={false,false};
uint32_t lifeStartAt[2]={0,0};
bool lifeColdChecked[2]={false,false};
float lifeStartMinV[2]={99,99},lifeTripMax[2]={0,0};
Health health;
bool healthDirty=false;
// Per-trip sums for the health comparison: [engine][bucket][metric] and samples.
float tripHealthSum[2][RPM_BUCKETS][HEALTH_METRICS];
uint16_t tripHealthN[2][RPM_BUCKETS];
DiagTest diagTests[2];
struct DiagRun { bool on=false; uint32_t startMs=0; uint32_t req0=0,rep0=0,inv0=0; uint64_t ms0=0; uint32_t cnt0=0; } diagRun[2];
bool ecuHoursChecked[2]={false,false};
int64_t lastMissedUtc=0; float lastMissedL=0;
// Wi-Fi robustness
uint8_t apChannelNow=6;          // channel the AP actually uses
uint8_t staChannel=0;            // router channel found by the last scan
uint8_t staBssid[6]={};
bool staBssidKnown=false;
volatile bool staGotConflict=false; // the router handed out an address in 192.168.4.x
bool staConflict=false;
uint32_t staNextTry=0,staBackoff=15000,staFails=0,staConnects=0,apRestarts=0,lastApCheck=0;
uint32_t staConnectedAt=0;
// Last access point event. AP_STOP also arrives (late) when the network only
// changed its settings, so the watchdog acts on "stopped and not started again".
volatile uint8_t apLastEvent=0;      // 1 started, 2 stopped
volatile uint32_t apLastEventMs=0;
uint32_t apChannelChanges=0;
uint8_t apStartedChannel=0;
bool scanBusy=false; int16_t scanCount=-1; uint32_t scanStartedMs=0;
// Firmware from GitHub, done by a worker task so the app keeps working.
enum FwJob : uint8_t { FW_IDLE, FW_LIST, FW_INSTALL };
volatile uint8_t fwJob=FW_IDLE;
volatile uint8_t fwState=0;      // 0 idle, 1 checking, 2 list ready, 3 downloading, 4 installed, 9 error
volatile size_t fwBytes=0,fwTotal=0;
String fwList="",fwError="",fwUrl="",fwTarget="";
bool rollbackPending=false;      // running a new image that is not confirmed yet
uint32_t lastRawCleanup=0;

uint64_t elapsedMs(){return (uint64_t)esp_timer_get_time()/1000ULL;}
bool validWall(){return time(nullptr)>1700000000;}
void backfillTripTime();
void setClockFromUtc(time_t sec, const String &source){
  if(sec<1700000000 || sec>4102444800LL)return;
  struct timeval tv={sec,0};
  settimeofday(&tv,nullptr);
  hasTime=true;timeSource=source;
  backfillTripTime();
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
  if(e==ARDUINO_EVENT_WIFI_AP_START){++apStarts;apLastEvent=1;apLastEventMs=millis();}
  if(e==ARDUINO_EVENT_WIFI_AP_STOP){++apStops;apLastEvent=2;apLastEventMs=millis();}
  if(e==ARDUINO_EVENT_WIFI_AP_STACONNECTED)++apJoins;
  if(e==ARDUINO_EVENT_WIFI_AP_STADISCONNECTED){++apLeaves;apReason=info.wifi_ap_stadisconnected.reason;}
  if(e==ARDUINO_EVENT_WIFI_STA_DISCONNECTED)++staDisconnects;
  if(e==ARDUINO_EVENT_WIFI_STA_GOT_IP){
    ++staConnects;staConnectedAt=millis();
    const uint32_t ip=info.got_ip.ip_info.ip.addr; // first octet in the low byte
    if((ip&0xFFFFFF)==((4u<<16)|(168u<<8)|192u))staGotConflict=true;
  }
}
bool cardInserted(){return digitalRead(SD_DETECT)==LOW;}

// ---------- Persistent fuel totals ----------
void loadTotals(){
  pref.begin("totals",true);
  const size_t stored=pref.getBytesLength("t");
  if(stored==sizeof(Totals)){
    Totals t;
    if(pref.getBytes("t",&t,sizeof(t))==sizeof(t)&&t.magic==totals.magic)totals=t;
  }else if(stored==sizeof(TotalsV5)){
    // 0.5.x: same counters; fuel per band starts now, the tank level keeps its reference.
    TotalsV5 *o=new TotalsV5();
    if(pref.getBytes("t",o,sizeof(*o))==sizeof(*o)&&o->magic==0xE1170005){
      for(int c=0;c<2;c++){totals.lifetimeL[c]=o->lifetimeL[c];totals.tripL[c]=o->tripL[c];
        totals.runSeconds[c]=o->runSeconds[c];totals.tripRunSeconds[c]=o->tripRunSeconds[c];
        totals.lifeRawH[c]=o->lifeRawH[c];totals.tripRawH[c]=o->tripRawH[c];
        totals.tankRef.raw[c]=o->tankRef.raw[c];totals.tankRef.lit[c]=o->tankRef.lit[c];
        totals.lastEcuHours[c]=o->lastEcuHours[c];}
      totals.tripStartUtc=o->tripStartUtc;totals.tripResets=o->tripResets;
      totals.tripActive=o->tripActive;totals.tripBoot=o->tripBoot;totals.tripStartSec=o->tripStartSec;
      totals.tripAnySec=o->tripAnySec;totals.tripLastUtc=o->tripLastUtc;totals.tripMaxRpm=o->tripMaxRpm;
      totals.nextTripId=o->nextTripId;totals.tankRefL=o->tankRefL;totals.tankRef.missed=o->tankRef.missed;
      totals.estMissedL=o->estMissedL;totals.missedEvents=o->missedEvents;totals.seasonStartUtc=o->seasonStartUtc;
      memcpy(totals.bucketSec,o->bucketSec,sizeof(o->bucketSec));memcpy(totals.bucketL,o->bucketL,sizeof(o->bucketL));
      memcpy(totals.bucketSogSec,o->bucketSogSec,sizeof(o->bucketSogSec));memcpy(totals.bucketSogL,o->bucketSogL,sizeof(o->bucketSogL));
      memcpy(totals.bucketNm,o->bucketNm,sizeof(o->bucketNm));
      memcpy(totals.zoneL,o->zoneL,sizeof(o->zoneL));memcpy(totals.zoneSec,o->zoneSec,sizeof(o->zoneSec));memcpy(totals.zoneNm,o->zoneNm,sizeof(o->zoneNm));
      totals.tankRefVar=totals.tankRefL>=0?9.0:0; // unknown fill quality: about 3 L
    }
    delete o;
  }else if(stored==sizeof(TotalsV4)){
    // 0.4.x: keep every counter; the new fields start empty.
    TotalsV4 o;
    if(pref.getBytes("t",&o,sizeof(o))==sizeof(o)&&o.magic==0xE1170004){
      for(int c=0;c<2;c++){totals.lifetimeL[c]=o.lifetimeL[c];totals.tripL[c]=o.tripL[c];
        totals.runSeconds[c]=o.runSeconds[c];totals.tripRunSeconds[c]=o.tripRunSeconds[c];
        totals.lifeRawH[c]=o.lifeRawH[c];totals.tripRawH[c]=o.tripRawH[c];}
      totals.tripStartUtc=o.tripStartUtc;totals.tripResets=o.tripResets;
    }
  }else if(stored==sizeof(TotalsV3)){
    TotalsV3 o;
    if(pref.getBytes("t",&o,sizeof(o))==sizeof(o)&&o.magic==0xE1170003){
      for(int c=0;c<2;c++){totals.lifetimeL[c]=o.lifetimeL[c];totals.tripL[c]=o.tripL[c];
        totals.runSeconds[c]=o.runSeconds[c];totals.tripRunSeconds[c]=o.tripRunSeconds[c];}
      totals.tripStartUtc=o.tripStartUtc;totals.tripResets=o.tripResets;
    }
  }
  pref.end();
}
void saveLife();
void saveTotals(){
  if(lifeDirty)saveLife();
  pref.begin("totals",false);
  pref.putBytes("t",&totals,sizeof(totals));
  pref.end();
  totalsDirty=false;lastTotalsSave=millis();
}
void resetTrip(){
  for(int c=0;c<2;c++){totals.tripL[c]=0;totals.tripRawH[c]=0;totals.tripRunSeconds[c]=0;for(uint8_t b=0;b<FUEL_BANDS;b++)totals.tripRawB[c][b]=0;}
  totals.tripStartUtc=validWall()?(int64_t)time(nullptr):0;
  ++totals.tripResets;
  saveTotals();
}

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
    logFile.println("boot_id,uptime_ms,utc_epoch,channel,mid,checksum,raw_hex,engine,fuel_lph,rpm,direction,kad_index,kad_value");
  }
  sdLogging=true;
  sdMessage="Logging raw J1708";sdFaultLatched=false;
  lastFlush=millis();
}
void refreshTestAvailable(){
  testFileAvailable=sdReady&&cardInserted()&&calName.length()&&SD.exists(calName);
}
uint64_t sdTotalMB(){return sdReady?SD.totalBytes()/(1024ULL*1024ULL):0;}
uint64_t sdUsedMB(){return sdReady?SD.usedBytes()/(1024ULL*1024ULL):0;}
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
    if(inserted&&!sdReady&&cardChangeAt&&(uint32_t)(millis()-cardChangeAt)>=1000&&!otaActive){
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
const char *frameDirection(const Decoded &d){return d.kadEcho?"ECHO":d.kadOwn?"TX":d.kadRequest?"REQ":"RX";}
void writeRecord(const Frame &f,const Decoded &d,bool valid,const char *hex){
  if(!sdLogging)return;
  int engineId = cfg.portChannel==0?-1:((f.channel+1)==cfg.portChannel?0:1);
  time_t utc=validWall()?time(nullptr):0;
  // Single short append; UART receiver tasks buffer independently.
  int result=logFile.printf("%08lX,%llu,%lld,J%u,%u,%u,%s,%s,",
      (unsigned long)bootId,(unsigned long long)f.bootMs,(long long)utc,
      f.channel==0?7:6,(unsigned)d.mid,valid?1:0,hex,
      engineId==0?"port":engineId==1?"starboard":"unassigned");
  if(result<=0){sdMessage="SD write failed";sdFaultLatched=true;++sdErrors;stopLog();return;}
  if(d.gotFuel)logFile.print(d.lph,3);
  logFile.print(',');
  if(d.gotRPM)logFile.print(d.rpm,2);
  logFile.printf(",%s,",frameDirection(d));
  if(d.kadIndex>=0)logFile.print(d.kadIndex);
  logFile.print(',');
  if(d.kadValueOk)logFile.print(d.kadValue);
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
  int n=snprintf(entry,sizeof(entry),"MARK,%08lX,%llu,%lld,%lu,%s,%ld,%.3f,,,,,,,,,,\n",
    (unsigned long)bootId,(unsigned long long)elapsedMs(),epoch,
    (unsigned long)calSteps,calPhase.c_str(),(long)calReferenceRPM,
    calReferenceFuel);
  if(n<=0 || n >= (int)sizeof(entry) || calFile.write((const uint8_t*)entry,n)!=(size_t)n){
    sdFaultLatched=true;sdMessage="Calibration marker write failed";finishCalibration(false);
    return;
  }
  calFile.flush();
  calStepStartMs=millis();
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
  calFile.println("kind,boot_id,uptime_ms,utc_epoch,step,phase,reference_rpm,reference_fuel_lph,channel,mid,length,checksum,raw_hex,decoded_fuel_lph,decoded_rpm,direction,kad_index,kad_value");
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
void appendCalibrationFrame(const Frame &f,const Decoded &d,bool valid,const char *hex){
  if(!calActive || (calChannel>=0 && f.channel!=(uint8_t)calChannel))return;
  const long long epoch=validWall()?(long long)time(nullptr):0;
  char entry[520];
  int n=snprintf(entry,sizeof(entry),"FRAME,%08lX,%llu,%lld,%lu,%s,%ld,%.3f,J%u,%u,%u,%u,%s,",
    (unsigned long)bootId,(unsigned long long)f.bootMs,epoch,
    (unsigned long)calSteps,calPhase.c_str(),(long)calReferenceRPM,calReferenceFuel,
    f.channel==0?7:6,(unsigned)d.mid,(unsigned)f.length,valid?1:0,hex);
  if(n<=0||n>(int)sizeof(entry)-80){++sdErrors;sdFaultLatched=true;sdMessage="Motor test row too large";finishCalibration(false);return;}
  if(d.gotFuel)n+=snprintf(entry+n,sizeof(entry)-n,"%.3f",d.lph);
  entry[n++]=',';
  if(d.gotRPM)n+=snprintf(entry+n,sizeof(entry)-n,"%.2f",d.rpm);
  n+=snprintf(entry+n,sizeof(entry)-n,",%s,",frameDirection(d));
  if(d.kadIndex>=0)n+=snprintf(entry+n,sizeof(entry)-n,"%ld",(long)d.kadIndex);
  entry[n++]=',';
  if(d.kadValueOk)n+=snprintf(entry+n,sizeof(entry)-n,"%ld",(long)d.kadValue);
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
  reply(ok,ok?"Recording started. Hold each RPM stage 30-60 s and mark it.":"Could not start motor test",ok?200:409);
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
  reply(calActive,calActive?"Stage marked. Hold it steady while data is recorded.":"SD write error stopped recording",calActive?200:500);
}
void handleCalibrationStop(){
  if(!calActive){reply(false,"No motor test is running",409);return;}
  finishCalibration(true);
  reply(true,"Motor test saved. You can now download the CSV.");
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
double prevFuelRaw[2]={0,0};
uint32_t gapFills[2]={0,0};
uint8_t fuelBandFor(float rpm){return rpm<BAND_PART_RPM?0:rpm<BAND_CRUISE_RPM?1:2;}
// rawRate < 0 means J1587 (liters are stored directly); otherwise the KAD raw
// count is integrated too, so later scale/correction changes stay exact.
// Samples are joined with the trapezoid rule. A gap of up to 2 minutes while
// the engine kept answering for rpm (fuel requests lost, rpm still coming) is
// bridged the same way and counted as interpolated; longer gaps are not.
void updateFuel(Motor &m,uint8_t ch,float newRate,uint64_t timestamp,double rawRate){
  if(m.fuelEver && timestamp>m.prevFuelAt) {
    const uint64_t dt=timestamp-m.prevFuelAt;
    // Only bridge if rpm kept coming during the whole gap (at least one
    // answer per 2 s): then the engine ran and only the fuel answers were lost.
    const bool rpmKept=m.rpmEver&&timestamp>=m.rpmAt&&timestamp-m.rpmAt<3000&&m.rpm>=ENGINE_RUN_RPM&&
                       (uint64_t)(m.rpmSamples-m.rpmSamplesAtFuel)*2000ULL>=dt;
    const bool bridge=dt>5000&&dt<=120000&&rpmKept;
    if(bridge){++gapFills[ch];totals.gapFillSec[ch]+=(uint32_t)(dt/1000);}
    if(dt<=5000||bridge) {
      const double hours=(double)dt/3600000.0;
      const double liters=((double)m.previousFuelLph+(double)newRate)*0.5*hours;
      m.observedLiters+=liters;
      if(rawRate>=0){
        const double rawH=(prevFuelRaw[ch]+rawRate)*0.5*hours;
        totals.lifeRawH[ch]+=rawH;totals.tripRawH[ch]+=rawH;
        const uint8_t b=fuelBandFor(m.rpm);
        totals.lifeRawB[ch][b]+=rawH;totals.tripRawB[ch][b]+=rawH;
      }else{
        totals.lifetimeL[ch]+=liters;
        totals.tripL[ch]+=liters;
      }
      totalsDirty=true;
    }else ++m.gaps;
  }
  if(rawRate>=0)prevFuelRaw[ch]=rawRate;
  m.fuelLph=newRate;m.fuelAt=timestamp;m.prevFuelAt=timestamp;
  m.previousFuelLph=newRate;m.fuelEver=true;++m.fuelSamples;m.rpmSamplesAtFuel=m.rpmSamples;
}
double kadFuelScale(uint8_t ch){return (1.0/(double)cfg.kadFuelDiv)*(double)cfg.fuelCorr[ch];}
// Litres from a raw integral and its part per band, with today's scale,
// correction and band factors. Raw without a band (before 0.6.0) gets the
// common correction only.
double rawToLitres(uint8_t ch,double total,double b0,double b1,double b2){
  const double legacy=max(0.0,total-(b0+b1+b2));
  return (legacy+b0*cfg.bandAdj[0]+b1*cfg.bandAdj[1]+b2*cfg.bandAdj[2])*kadFuelScale(ch);
}
double tripLitersFor(uint8_t ch){
  if(cfg.protocol!=PROTO_KAD)return totals.tripL[ch];
  const double *b=totals.tripRawB[ch];return rawToLitres(ch,totals.tripRawH[ch],b[0],b[1],b[2]);
}
double lifetimeLitersFor(uint8_t ch){
  if(cfg.protocol!=PROTO_KAD)return totals.lifetimeL[ch];
  const double *b=totals.lifeRawB[ch];return rawToLitres(ch,totals.lifeRawH[ch],b[0],b[1],b[2]);
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
void stopPolling(uint8_t ch,const String &why);
void checkEcuHours(uint8_t ch,float hours);
// ---------- Plausibility of engine answers ----------
// Physical limits per parameter (scaled units): the range, and the largest
// change between two answers that is still real. A value outside the range is
// dropped. A bigger jump is held back until the next answer confirms it, so a
// single garbled reply never reaches the display or the fuel totals, while a
// real fast change (full throttle) only arrives one answer later.
struct Sanity{uint16_t index;float lo,hi,step;};
constexpr Sanity KAD_SANITY[]={
  {256,0,4500,1500},{291,0,4500,2500},{289,0,120,45},{259,-30,130,8},{260,-30,110,8},
  {261,-30,130,15},{258,30,450,200},{265,-60,350,200},{293,6,36,4},{266,0,200000,0.3f}};
int32_t kadHeld[2][32];
bool kadHeldOk[2][32];
uint32_t kadRejected[2]={0,0},kadHeldBack[2]={0,0};
uint32_t kadTransientUntil[2]={0,0};
uint16_t kadFocusIndex=0;uint32_t kadFocusUntil=0;
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
    const int64_t since=(int64_t)f.bootMs-(int64_t)k.pendingAt;
    const bool own=k.ownPending&&k.pendingIndex==index&&since>-50&&since<(int64_t)KAD_REPLY_MS;
    d.kadOwn=own;d.kadEcho=own;
    if(!own){
      ++k.foreignRequests;k.lastForeignAt=f.bootMs;
      if(k.polling)stopPolling(f.channel,"Another device already polls this engine, so EngineLink only listens");
      k.pending=true;k.pendingIndex=index;k.pendingAt=f.bootMs;
    }
    return;
  }
  if(!k.pending||f.bootMs<k.pendingAt||f.bootMs-k.pendingAt>KAD_REPLY_MS){++k.orphanReplies;return;}
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
  if(k.foreignRequests&&elapsedMs()-k.lastForeignAt<60000){why="Another device is polling this engine. Listening is enough";return false;}
  if(uart_set_pin(portFor(ch),txPinFor(ch),UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE)!=ESP_OK){why="Could not attach the TX pin";return false;}
  k.txAttached=true;k.polling=true;k.stopReason="";k.unanswered=0;k.ownPending=false;
  memset(k.miss,0,sizeof(k.miss));k.skip=0;k.winReq=k.winRep=0;k.answered=NAN;
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
    k.ownPending=true;k.pending=true;k.pendingIndex=index;k.pendingAt=elapsedMs();
    writeTxRecord(ch,req,index);
  }
}
bool fuelFresh(uint8_t c,uint64_t now){return motor[c].fuelEver&&now>=motor[c].fuelAt&&(now-motor[c].fuelAt)<FRAME_FRESH_MS;}
bool rpmFresh(uint8_t c,uint64_t now){return motor[c].rpmEver&&now>=motor[c].rpmAt&&(now-motor[c].rpmAt)<RPM_FRESH_MS;}

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
  serviceTripSecond();
  serviceDiagnosis();
  if(totalsDirty&&(uint32_t)(millis()-lastTotalsSave)>=TOTALS_SAVE_MS)saveTotals();
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

// ---------- Fuel counters, tank ----------
FuelSnap fuelSnapNow(){
  FuelSnap s;
  for(uint8_t c=0;c<2;c++){s.raw[c]=totals.lifeRawH[c];s.lit[c]=totals.lifetimeL[c];for(uint8_t b=0;b<FUEL_BANDS;b++)s.rawB[c][b]=totals.lifeRawB[c][b];}
  s.missed=totals.estMissedL;
  return s;
}
// Liters used since a snapshot, with today's scale and correction.
double usedSince(const FuelSnap &a){
  double l=0;
  for(uint8_t c=0;c<2;c++){
    if(cfg.protocol==PROTO_KAD){
      const double *n=totals.lifeRawB[c];
      l+=rawToLitres(c,totals.lifeRawH[c]-a.raw[c],n[0]-a.rawB[c][0],n[1]-a.rawB[c][1],n[2]-a.rawB[c][2]);
    }
    else l+=totals.lifetimeL[c]-a.lit[c];
  }
  return max(0.0,l+(totals.estMissedL-a.missed));
}
bool tankKnown(){return cfg.tankL>0&&totals.tankRefL>=0;}
double tankAboard(){
  if(!tankKnown())return -1;
  return constrain(totals.tankRefL-usedSince(totals.tankRef),0.0,(double)cfg.tankL);
}
int lastFullIndex(){for(uint8_t i=0;i<fuelCount;i++)if(fuelLog[i].full)return i;return -1;}
// Uncertainty of the fuel aboard, carried like a Kalman filter: the variance
// of the last reference (how exactly the tank was filled) plus what the
// counting can be off by since then. Uncalibrated engine figures are taken
// as +/-15 %, after calibration their own fitted uncertainty.
double tankVariance(){
  if(!tankKnown())return 0;
  const double since=usedSince(totals.tankRef);
  const double rel=cfg.calFills?max((double)cfg.calSig,0.01):0.15;
  return totals.tankRefVar+(rel*since)*(rel*since)+(0.005*since)*(0.005*since);
}
// About 95 % of the time the fuel aboard is within this many litres.
double tankPlusMinus(){return tankKnown()?2.0*sqrt(tankVariance()):0;}
double latestPrice(){
  for(uint8_t i=0;i<fuelCount;i++)if(fuelLog[i].price>0)return fuelLog[i].price;
  return cfg.defaultPrice;
}

// ---------- Internal storage for trips, fuel log, health, tests, app prefs ----------
// LittleFS on the 1.5 MB data partition: NVS is only 20 KB and would fill up.
// Every file is written to a temporary name first and then renamed, so a
// power cut never leaves a half-written file.
bool fsReady=false;
bool fsWrite(const char *path,const void *data,size_t len){
  if(!fsReady)return false;
  String tmp=String(path)+".tmp";
  File f=LittleFS.open(tmp,"w");
  if(!f)return false;
  const bool ok=f.write((const uint8_t*)data,len)==len;
  f.close();
  if(!ok){LittleFS.remove(tmp);return false;}
  LittleFS.remove(path);
  return LittleFS.rename(tmp,path);
}
// Reads a file of exactly len bytes, or of a whole number of items of size item.
size_t fsRead(const char *path,void *data,size_t maxLen,size_t item){
  if(!fsReady||!LittleFS.exists(path))return 0;
  File f=LittleFS.open(path,"r");
  if(!f)return 0;
  const size_t n=f.size();
  if(n>maxLen||n%item){f.close();return 0;}
  const size_t got=f.read((uint8_t*)data,n);
  f.close();
  return got==n?n:0;
}
// Files from 0.6.0 on start with a small header (magic, record size), so a
// later layout can still read them. 0.5.x files have no header.
struct FileHead { uint32_t magic; uint16_t recSize; uint16_t count; };
constexpr uint32_t TRIPS_MAGIC=0x32505254, FUEL_MAGIC=0x324C4546; // "TRP2", "FEL2"
bool fsWriteRecords(const char *path,uint32_t magic,const void *recs,uint16_t recSize,uint16_t count){
  const size_t len=sizeof(FileHead)+(size_t)recSize*count;
  uint8_t *buf=(uint8_t*)malloc(len);
  if(!buf)return false;
  FileHead h={magic,recSize,count};
  memcpy(buf,&h,sizeof(h));memcpy(buf+sizeof(h),recs,(size_t)recSize*count);
  const bool ok=fsWrite(path,buf,len);
  free(buf);return ok;
}
// Whole file into a new buffer (caller frees), or nullptr.
uint8_t *fsSlurp(const char *path,size_t &n,size_t maxLen){
  n=0;
  if(!fsReady||!LittleFS.exists(path))return nullptr;
  File f=LittleFS.open(path,"r");
  if(!f)return nullptr;
  n=f.size();
  if(!n||n>maxLen){f.close();n=0;return nullptr;}
  uint8_t *buf=(uint8_t*)malloc(n);
  if(buf&&f.read(buf,n)!=n){free(buf);buf=nullptr;}
  f.close();
  return buf;
}
void snapFromV1(FuelSnap &d,const FuelSnapV1 &s){d=FuelSnap();for(uint8_t c=0;c<2;c++){d.raw[c]=s.raw[c];d.lit[c]=s.lit[c];}d.missed=s.missed;}
void saveTrips(){fsWriteRecords("/trips.bin",TRIPS_MAGIC,trips,sizeof(TripRec),tripCount);}
void loadTrips(){
  size_t n;uint8_t *buf=fsSlurp("/trips.bin",n,sizeof(FileHead)+sizeof(trips)+4096);
  tripCount=0;
  if(!buf)return;
  FileHead h;memcpy(&h,buf,min(n,sizeof(h)));
  if(n>=sizeof(h)&&h.magic==TRIPS_MAGIC&&h.recSize==sizeof(TripRec)&&n==sizeof(h)+(size_t)h.count*sizeof(TripRec)){
    tripCount=min<uint16_t>(h.count,MAX_TRIPS);memcpy(trips,buf+sizeof(h),sizeof(TripRec)*tripCount);
  }else if(n%sizeof(TripRecV1)==0){
    const uint16_t c=min<size_t>(n/sizeof(TripRecV1),MAX_TRIPS);
    for(uint16_t i=0;i<c;i++){
      TripRecV1 o;memcpy(&o,buf+i*sizeof(o),sizeof(o));
      TripRec &r=trips[i];r=TripRec();
      r.id=o.id;r.group=o.group;r.startUtc=o.startUtc;r.endUtc=o.endUtc;r.anySec=o.anySec;
      r.bootId=o.bootId;r.startSec=o.startSec;r.endSec=o.endSec;
      for(uint8_t k=0;k<2;k++){r.rawH[k]=o.rawH[k];r.litJ[k]=o.litJ[k];}
      r.maxRpm=o.maxRpm;r.people=o.people;r.sea=o.sea;r.wind=o.wind;r.extras=o.extras;memcpy(r.note,o.note,sizeof(r.note));
    }
    tripCount=c;saveTrips();
  }
  free(buf);
}
void saveFuel(){fsWriteRecords("/fuel.bin",FUEL_MAGIC,fuelLog,sizeof(FuelRec),fuelCount);}
void loadFuel(){
  size_t n;uint8_t *buf=fsSlurp("/fuel.bin",n,sizeof(FileHead)+sizeof(fuelLog)+4096);
  fuelCount=0;
  if(!buf)return;
  FileHead h;memcpy(&h,buf,min(n,sizeof(h)));
  if(n>=sizeof(h)&&h.magic==FUEL_MAGIC&&h.recSize==sizeof(FuelRec)&&n==sizeof(h)+(size_t)h.count*sizeof(FuelRec)){
    fuelCount=min<uint16_t>(h.count,MAX_FUEL);memcpy(fuelLog,buf+sizeof(h),sizeof(FuelRec)*fuelCount);
  }else if(n%sizeof(FuelRecV1)==0){
    const uint16_t c=min<size_t>(n/sizeof(FuelRecV1),MAX_FUEL);
    for(uint16_t i=0;i<c;i++){
      FuelRecV1 o;memcpy(&o,buf+i*sizeof(o),sizeof(o));
      FuelRec &r=fuelLog[i];r=FuelRec();
      r.utc=o.utc;r.litres=o.litres;r.price=o.price;r.full=o.full;r.counted=o.counted;r.rawDelta=o.rawDelta;
      snapFromV1(r.at,o.at);r.prevRefL=o.prevRefL;snapFromV1(r.prevRef,o.prevRef);
      r.quality=o.full?1:0; // unknown: treated as "nozzle clicked off" until the owner says otherwise
    }
    fuelCount=c;saveFuel();
  }
  free(buf);
}
void saveHealth(){fsWrite("/health.bin",&health,sizeof(health));healthDirty=false;}
void loadHealth(){
  Health h;
  if(fsRead("/health.bin",&h,sizeof(h),sizeof(h))==sizeof(h)&&h.magic==health.magic)health=h;
}
void saveDiag(){fsWrite("/diag.bin",diagTests,sizeof(diagTests));}
void loadDiag(){fsRead("/diag.bin",diagTests,sizeof(diagTests),sizeof(diagTests));}

// ---------- Fuel log and calibration ----------
// Every full tank after an earlier full tank is one calibration equation
// (fuelcal.h): the pump litres of the full tank and of any partial fill-ups
// in between, against what EngineLink counted in that time, split by
// engine-speed band. The calibration is solved again from the whole log after
// every change, so deleting or correcting an entry also undoes its effect.
enum CalState : uint8_t { CAL_NONE=0, CAL_USED=1, CAL_ODD=2, CAL_OFF=3, CAL_SMALL=4, CAL_FIRST=5 };
uint8_t calState[MAX_FUEL];
float calZ[MAX_FUEL],calPump[MAX_FUEL],calCounted[MAX_FUEL],calBase[MAX_FUEL];
// The equation for full tank i against the next older full tank; false if
// there is none.
bool calRow(uint8_t i,fuelcal::Row &row,uint8_t &older){
  older=0xFF;
  for(uint8_t k=i+1;k<fuelCount;k++)if(fuelLog[k].full){older=k;break;}
  if(older==0xFF)return false;
  const FuelRec &a=fuelLog[i],&o=fuelLog[older];
  double pump=a.litres;
  for(uint8_t k=i+1;k<older;k++)pump+=fuelLog[k].litres;
  const double div=(double)cfg.kadFuelDiv;
  double X=0,x[FUEL_BANDS]={0,0,0};
  for(uint8_t c=0;c<2;c++){
    if(cfg.protocol==PROTO_KAD){
      X+=(a.at.raw[c]-o.at.raw[c])/div;
      for(uint8_t b=0;b<FUEL_BANDS;b++)x[b]+=(a.at.rawB[c][b]-o.at.rawB[c][b])/div;
    }else X+=a.at.lit[c]-o.at.lit[c];
  }
  X+=a.at.missed-o.at.missed;
  for(uint8_t b=0;b<FUEL_BANDS;b++)x[b]=max(0.0,x[b]);
  row.a[0]=max(0.0,X);row.a[1]=x[0];row.a[2]=x[1];row.a[3]=x[2];
  row.y=pump;
  row.var=fuelcal::rowVariance(a.quality,o.quality,pump,row.a[0]);
  return true;
}
void recalibrate(){
  const float before[7]={cfg.fuelCorr[0],cfg.fuelCorr[1],cfg.bandAdj[0],cfg.bandAdj[1],cfg.bandAdj[2],cfg.calSig,(float)cfg.calFills};
  fuelcal::Row rows[MAX_FUEL];
  bool use[MAX_FUEL];
  int8_t rowOf[MAX_FUEL];
  int n=0;
  for(uint8_t i=0;i<fuelCount;i++){
    calState[i]=CAL_NONE;calZ[i]=0;calPump[i]=0;calCounted[i]=-1;calBase[i]=-1;rowOf[i]=-1;
    if(!fuelLog[i].full)continue;
    uint8_t older;
    fuelcal::Row r;
    if(!calRow(i,r,older)){calState[i]=CAL_FIRST;continue;}
    calPump[i]=(float)r.y;calBase[i]=(float)r.a[0];
    if(r.a[0]<3||r.y<3){calState[i]=CAL_SMALL;continue;}
    const double ratio=r.y/r.a[0];
    if(fuelLog[i].calUse==2)calState[i]=CAL_OFF;
    else if(fuelLog[i].calUse!=1&&(ratio<0.5||ratio>1.6))calState[i]=CAL_ODD;
    else calState[i]=CAL_USED;
    rows[n]=r;use[n]=calState[i]==CAL_USED;rowOf[i]=n;n++;
  }
  // Each equation is checked against all the others; one that does not fit
  // (more than 3 sigma) is set aside and the owner is asked about it.
  for(uint8_t i=0;i<fuelCount;i++){
    if(rowOf[i]<0||calState[i]!=CAL_USED)continue;
    const double z=fuelcal::zScore(rows,use,n,rowOf[i]);
    calZ[i]=(float)z;
    if(fuelLog[i].calUse!=1&&fabs(z)>3.0){calState[i]=CAL_ODD;use[rowOf[i]]=false;}
  }
  uint8_t used=0;double litres=0;
  fuelcal::Row mix{{0,0,0,0},0,0};
  for(uint8_t i=0;i<fuelCount;i++)if(rowOf[i]>=0&&use[rowOf[i]]){
    ++used;litres+=rows[rowOf[i]].y;
    for(int k=0;k<fuelcal::P;k++)mix.a[k]+=rows[rowOf[i]].a[k];
  }
  if(used){
    const fuelcal::Fit f=fuelcal::solve(rows,use,n);
    if(f.ok){
      const double common=constrain(f.th[0],0.5,2.0);
      for(uint8_t c=0;c<2;c++)cfg.fuelCorr[c]=(float)common;
      for(uint8_t b=0;b<FUEL_BANDS;b++)cfg.bandAdj[b]=(float)constrain((common+f.th[1+b])/common,0.8,1.25);
      // Uncertainty of the owner's own typical mix of trips.
      const double est=fuelcal::predict(f,mix);
      cfg.calSig=est>0?(float)(sqrt(fuelcal::predictVar(f,mix))/est):0.1f;
    }
  }else if(cfg.calFills){
    // The last fill-up that calibrated was removed: back to the plain scale.
    for(uint8_t c=0;c<2;c++)cfg.fuelCorr[c]=1.0f;
    for(uint8_t b=0;b<FUEL_BANDS;b++)cfg.bandAdj[b]=1.0f;
    cfg.calSig=0;
  }
  cfg.calFills=used;cfg.calLitres=(float)litres;
  cfg.fineTunes=used;
  // What EngineLink counts now (with the new figures) between full tanks.
  for(uint8_t i=0;i<fuelCount;i++)if(fuelLog[i].full&&calState[i]!=CAL_FIRST){
    uint8_t older;fuelcal::Row r;
    if(calRow(i,r,older)){
      const FuelRec &a=fuelLog[i],&o=fuelLog[older];
      double l=0;
      for(uint8_t c=0;c<2;c++){
        if(cfg.protocol==PROTO_KAD)l+=rawToLitres(c,a.at.raw[c]-o.at.raw[c],a.at.rawB[c][0]-o.at.rawB[c][0],a.at.rawB[c][1]-o.at.rawB[c][1],a.at.rawB[c][2]-o.at.rawB[c][2]);
        else l+=a.at.lit[c]-o.at.lit[c];
      }
      calCounted[i]=(float)(l+a.at.missed-o.at.missed);
    }
  }
  const float after[7]={cfg.fuelCorr[0],cfg.fuelCorr[1],cfg.bandAdj[0],cfg.bandAdj[1],cfg.bandAdj[2],cfg.calSig,(float)cfg.calFills};
  if(memcmp(before,after,sizeof(before)))saveSettings();
}
// quality: 0 not full, 1 nozzle clicked off, 2 topped up to the filler pipe.
bool addFuel(float litres,float price,uint8_t quality,int64_t utc,String &msg){
  if(!(litres>=0&&litres<=5000)){msg="Litres out of range";return false;}
  if(!(price>=0&&price<1000)){msg="Price out of range";return false;}
  if(quality>2)quality=1;
  const bool full=quality>0;
  if(!full&&litres<=0){msg="Enter the litres you added";return false;}
  FuelRec r;
  r.utc=utc>1700000000?utc:(validWall()?(int64_t)time(nullptr):0);
  r.litres=litres;r.price=price;r.full=full?1:0;r.quality=quality;
  r.prevRefL=totals.tankRefL;r.prevRef=totals.tankRef;
  // Old fields, kept for the scale check (1/128 against 1/256).
  const int pf=lastFullIndex();
  if(full&&pf>=0){
    r.counted=(float)usedSince(fuelLog[pf].at);
    double rd=0;for(uint8_t c=0;c<2;c++)rd+=totals.lifeRawH[c]-fuelLog[pf].at.raw[c];
    r.rawDelta=(float)rd;
  }
  if(full){
    totals.tankRefL=cfg.tankL>0?cfg.tankL:-1;
    totals.tankRefVar=fuelcal::fillError(quality)*fuelcal::fillError(quality);
  }else{
    const double a=tankAboard();
    if(a>=0){totals.tankRefVar=tankVariance()+(0.005*litres)*(0.005*litres);totals.tankRefL=min((double)cfg.tankL,a+litres);}
  }
  r.at=fuelSnapNow();
  totals.tankRef=r.at;
  if(fuelCount==MAX_FUEL)--fuelCount;
  memmove(&fuelLog[1],&fuelLog[0],sizeof(FuelRec)*fuelCount);
  fuelLog[0]=r;++fuelCount;
  recalibrate();
  saveFuel();saveTotals();
  msg="Saved";
  if(full)switch(calState[0]){
    case CAL_USED:
      msg="Saved. EngineLink counted "+String(calBase[0]*cfg.fuelCorr[0],1)+" L, the pump "+String(calPump[0],1)+" L. Fuel figures now within about "+String(cfg.calSig*200.0f,1)+" %";
      break;
    case CAL_ODD:
      msg="EngineLink counted "+String(calBase[0]*cfg.fuelCorr[0],1)+" L but the pump "+String(calPump[0],1)+" L. Was the tank really full? See the fuel log";
      break;
    case CAL_SMALL:msg="Saved. Too little fuel since the last full tank to calibrate";break;
    case CAL_FIRST:msg="Saved. From the next full tank on, EngineLink compares its count with the pump";break;
    default:break;
  }
  return true;
}
// Deleting the newest entry also restores the tank estimate before it.
bool deleteFuel(uint8_t i){
  if(i>=fuelCount)return false;
  if(i==0){totals.tankRefL=fuelLog[0].prevRefL;totals.tankRef=fuelLog[0].prevRef;saveTotals();}
  memmove(&fuelLog[i],&fuelLog[i+1],sizeof(FuelRec)*(fuelCount-i-1));
  --fuelCount;
  recalibrate();
  saveFuel();
  return true;
}
// The owner corrects how full the tank was, or answers "was it really full?".
bool editFuel(uint8_t i,int quality,int use){
  if(i>=fuelCount)return false;
  FuelRec &r=fuelLog[i];
  if(quality>=1&&quality<=2&&r.full){
    r.quality=(uint8_t)quality;
    if(i==0)totals.tankRefVar=fuelcal::fillError(r.quality)*fuelcal::fillError(r.quality);
  }
  if(use>=0&&use<=2)r.calUse=(uint8_t)use;
  recalibrate();
  saveFuel();saveTotals();
  return true;
}

// ---------- Trips ----------
// A trip starts when the first engine starts and ends when both engines have
// been off for cfg.tripEndMin minutes. If EngineLink restarts during a trip,
// the open trip is closed at boot; two parts can be combined in History.
uint32_t lastEngineOnMs=0,lastEngineOnSec=0;
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
void finishTripHealth();
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
// ---------- Wi-Fi: built to keep the phone connected ----------
// Rules that keep the EngineLink network steady:
//  * The access point is always on and never restarted for router problems.
//  * Power saving is off, transmit power is at maximum, phones that sleep
//    are kept for 10 minutes instead of being dropped.
//  * The radio has one channel. With a router, the AP starts on the router's
//    channel (found once at setup), and reconnects are aimed at the router's
//    own channel and address, so the radio never hops away from the phone.
//  * A full router scan (which hops channels) only runs when no phone is
//    connected, or when the user asks for one on the Boat router page.
//  * A watchdog restarts only the AP if it ever stops.
bool routerConfigured(){return cfg.router&&cfg.ssid.length();}
void radioTuning(){
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  // Setting the country again on a running network makes it start over and
  // drops every phone, so it is only set when it is not already right.
  wifi_country_t cur={};
  if(esp_wifi_get_country(&cur)!=ESP_OK||cur.cc[0]!='E'||cur.cc[1]!='U'||cur.schan!=1||cur.nchan!=13||cur.policy!=WIFI_COUNTRY_POLICY_MANUAL){
    wifi_country_t cc={{'E','U',' '},1,13,20,WIFI_COUNTRY_POLICY_MANUAL};
    esp_wifi_set_country(&cc);
  }
}
void startAP(){
  // softAP() leaves a running network alone when nothing changed, so calling
  // this never drops a phone unless the name, password or channel differ.
  if(WiFi.softAPIP()!=IPAddress(192,168,4,1))WiFi.softAPConfig(IPAddress(192,168,4,1),IPAddress(192,168,4,1),IPAddress(255,255,255,0));
  apStarted=WiFi.softAP(cfg.apName.c_str(),cfg.apPassword.c_str(),apChannelNow,false,6);
  esp_wifi_set_inactive_time(WIFI_IF_AP,600);
  if(apStarted){
    if(apStartedChannel&&apStartedChannel!=apChannelNow)++apChannelChanges;
    apStartedChannel=apChannelNow;
  }
}
// Blocking scan for the saved router. Only used when the network is set up.
bool findRouter(){
  const int16_t n=WiFi.scanNetworks(false,true,false,150);
  int best=-1;int32_t bestRssi=-200;
  for(int16_t i=0;i<n;i++){
    if(WiFi.SSID(i)==cfg.ssid&&WiFi.RSSI(i)>bestRssi){best=i;bestRssi=WiFi.RSSI(i);}
  }
  if(best>=0){
    staChannel=(uint8_t)WiFi.channel(best);
    memcpy(staBssid,WiFi.BSSID(best),6);
  }
  WiFi.scanDelete();
  return best>=0;
}
void connectRouter(){
  if(staBssidKnown)WiFi.begin(cfg.ssid.c_str(),cfg.wifiPassword.c_str(),staChannel,staBssid,true);
  else WiFi.begin(cfg.ssid.c_str(),cfg.wifiPassword.c_str(),apChannelNow,nullptr,true);
}
// A name for http://<name>.local: letters, digits and hyphens, as DNS allows.
bool validHostName(const String &n){
  if(n.length()<1||n.length()>32||n[0]=='-'||n[n.length()-1]=='-')return false;
  for(size_t i=0;i<n.length();i++){const char c=n[i];if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'))return false;}
  return true;
}
// mDNS answers <name>.local on the EngineLink network and on the router; the
// router also learns the name from DHCP.
void startNameService(){
  if(mdnsReady){MDNS.end();mdnsReady=false;}
  if(MDNS.begin(cfg.hostName.c_str())){
    MDNS.addService("http","tcp",80);
    MDNS.setInstanceName(cfg.apName.c_str());
    mdnsReady=true;
  }
}
void applyNetwork(){
  // Only a saved network change rebuilds the interfaces.
  const bool withRouter=routerConfigured();
  WiFi.persistent(false);
  const wifi_mode_t want=withRouter?WIFI_AP_STA:WIFI_AP;
  WiFi.setHostname(cfg.hostName.c_str());
  if(WiFi.getMode()!=want)WiFi.mode(want);
  radioTuning();
  staConflict=false;staGotConflict=false;
  uint8_t ch=cfg.apChannel;
  if(withRouter){
    WiFi.setAutoReconnect(false);
    // The scan the user picked the router from already gave its channel. A new
    // scan hops across channels, so it only runs while no phone is connected.
    if(!staBssidKnown&&WiFi.softAPgetStationNum()==0)staBssidKnown=findRouter();
    if(staBssidKnown)ch=staChannel;
    else if(WiFi.status()==WL_CONNECTED&&WiFi.channel())ch=(uint8_t)WiFi.channel();
  }else{
    ntpStarted=false;staBssidKnown=false;
  }
  apChannelNow=ch;
  startAP();
  if(withRouter){
    esp_wifi_disconnect();
    connectRouter();
    staBackoff=15000;staFails=0;staNextTry=millis()+staBackoff;
  }
  if(!mdnsReady)startNameService();
}
void serviceNetwork(){
  if(pendingNetwork){pendingNetwork=false;applyNetwork();}
  // Watchdog for the access point.
  if((uint32_t)(millis()-lastApCheck)>=3000){
    lastApCheck=millis();
    // Only a network that is really gone is restarted: a restart itself drops
    // every phone, so a false alarm here is worse than none.
    const bool modeOk=WiFi.getMode()&WIFI_MODE_AP;
    const bool stopped=apLastEvent==2&&(uint32_t)(millis()-apLastEventMs)>=3000;
    const bool noIp=modeOk&&!stopped&&WiFi.softAPIP()!=IPAddress(192,168,4,1);
    if(!modeOk||stopped||noIp){
      ++apRestarts;
      if(!modeOk)WiFi.mode(routerConfigured()?WIFI_AP_STA:WIFI_AP);
      if(stopped){esp_wifi_start();apLastEvent=0;}
      radioTuning();startAP();
    }
  }
  // A router that uses EngineLink's own addresses would swallow the replies to
  // every phone on the EngineLink network. Leave it and say why in the app.
  if(staGotConflict){
    staGotConflict=false;staConflict=true;
    esp_wifi_disconnect();
  }
  if(routerConfigured()&&!staConflict){
    if(WiFi.status()==WL_CONNECTED){
      staFails=0;staBackoff=15000;
      const uint8_t ch=(uint8_t)WiFi.channel();
      if(ch&&ch!=apChannelNow){apChannelNow=ch;staChannel=ch;}
    }else if((int32_t)(millis()-staNextTry)>=0){
      ++staFails;
      // The router may have moved to another channel: look again, but only
      // while no phone depends on the EngineLink network.
      if(staFails%6==0&&WiFi.softAPgetStationNum()==0&&!scanBusy){
        staBssidKnown=findRouter();
        if(staBssidKnown&&staChannel!=apChannelNow){apChannelNow=staChannel;startAP();}
      }
      esp_wifi_disconnect();
      connectRouter();
      staBackoff=min<uint32_t>(staBackoff*2,300000UL);
      staNextTry=millis()+staBackoff;
    }
  }
  if(routerConfigured()&&WiFi.status()==WL_CONNECTED&&!ntpStarted&&cfg.ntpOn&&allowTime("ntp")){
    configTime(0,0,cfg.ntpServer.c_str(),"pool.ntp.org");
    ntpStarted=true;
  }
  if(ntpNotification){
    ntpNotification=false;
    if(allowTime("ntp")&&validWall()){hasTime=true;timeSource="ntp";backfillTripTime();}
  }
  // A scan the user asked for on the Boat router page.
  if(scanBusy){
    const int16_t r=WiFi.scanComplete();
    if(r>=0||r==WIFI_SCAN_FAILED||(uint32_t)(millis()-scanStartedMs)>15000){scanBusy=false;scanCount=r;}
  }
}
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
void saveLife(){fsWrite("/life.bin",&life,sizeof(life));lifeDirty=false;}
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
}
void tripSeriesClose(bool keep){
  if(!tripSeries)return;
  tripSeries.close();
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
    if((uint32_t)(millis()-tripSeriesFlush)>30000){tripSeries.flush();tripSeriesFlush=millis();}
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
  char name[32];snprintf(name,sizeof(name),"/TRIPS/T%06lu.BIN",(unsigned long)web.arg("id").toInt());
  sendSeriesFile(name);
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
  web.sendHeader("Content-Disposition","attachment; filename=EngineLink-trip-log.csv");
  web.setContentLength(CONTENT_LENGTH_UNKNOWN);
  web.send(200,"text/csv","");
  const int pi=portIndex();
  const char *A=pi==1?"stbd":"port",*B=pi==1?"port":"stbd";
  if(pi<0){A="j7";B="j6";}
  String s="trip_id,time_utc,seconds";
  const char *cols[]={"rpm","requested_rpm","fuel_lph","coolant_c","charge_air_c","boost_kpa","battery_v"};
  for(const char *side:{A,B})for(const char *c:cols){s+=',';s+=side;s+='_';s+=c;}
  s+="\n";web.sendContent(s);s="";
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
      if(s.length()>3000){web.sendContent(s);s="";}
    }
    f.close();
  }
  web.sendContent(s);web.sendContent("");
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
  if(cfg.router&&cfg.ssid.length()&&WiFi.status()!=WL_CONNECTED)return S_ROUTER;
  const uint64_t now=elapsedMs();
  for(uint8_t c=0;c<2;c++)if(kad[c].polling&&rpmFresh(c,now))return S_LIVE;
  if(cfg.protocol!=PROTO_KAD&&(rpmFresh(0,now)||rpmFresh(1,now)))return S_LIVE;
  return S_READY;
}
constexpr uint32_t LED_DEMO_STEP_MS=3000;
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
void saveSettings(){
  pref.begin("enginelink",false);
  pref.putBool("done",cfg.completed);
  pref.putBool("router",cfg.router);
  pref.putBool("can",cfg.canListen);
  pref.putUChar("port",cfg.portChannel);
  pref.putUChar("apch",cfg.apChannel);
  pref.putString("appass",cfg.apPassword);
  pref.putString("ssid",cfg.ssid);
  pref.putString("wpass",cfg.wifiPassword);
  pref.putString("time",cfg.timePreference);
  pref.putUChar("proto",cfg.protocol);
  pref.putBool("kbe",cfg.kadBigEndian);
  pref.putUShort("kdiv",cfg.kadFuelDiv);
  pref.putUChar("kvolt",cfg.kadVolt);
  pref.putFloat("corr0",cfg.fuelCorr[0]);
  pref.putFloat("corr1",cfg.fuelCorr[1]);
  pref.putBytes("badj",cfg.bandAdj,sizeof(cfg.bandAdj));
  pref.putFloat("calsig",cfg.calSig);pref.putUChar("calfills",cfg.calFills);pref.putFloat("call",cfg.calLitres);
  {uint16_t g[4]={cfg.gearMaskF,cfg.gearValF,cfg.gearMaskR,cfg.gearValR};pref.putBytes("gear",g,sizeof(g));}
  pref.putString("apname",cfg.apName);
  pref.putString("host",cfg.hostName);
  pref.putFloat("tank",cfg.tankL);
  pref.putUChar("reserve",cfg.reserve);
  pref.putFloat("price",cfg.defaultPrice);
  pref.putUChar("tripend",cfg.tripEndMin);
  pref.putBool("autopoll",cfg.autoPoll);
  pref.putUShort("rawkeep",cfg.rawKeepDays);
  pref.putBool("ntp",cfg.ntpOn);
  pref.putString("ntpsrv",cfg.ntpServer);
  pref.putBool("scaleok",cfg.fuelScaleOk);
  pref.putBool("voltok",cfg.voltOk);
  pref.putUChar("tunes",cfg.fineTunes);
  pref.putFloat("tunedev",cfg.lastTuneDev);
  pref.end();
}
void savePrefs(){fsWrite("/prefs.json",cfg.prefs.c_str(),cfg.prefs.length());}
void loadPrefs(){
  if(!fsReady||!LittleFS.exists("/prefs.json"))return;
  File f=LittleFS.open("/prefs.json","r");
  if(f&&f.size()<=4000){String p=f.readString();if(p.length()>=2&&p[0]=='{')cfg.prefs=p;}
  if(f)f.close();
}
void loadSettings(){
  pref.begin("enginelink",true);
  cfg.completed=pref.getBool("done",false);
  cfg.router=pref.getBool("router",false);
  cfg.canListen=pref.getBool("can",false);
  cfg.portChannel=pref.getUChar("port",1);
  cfg.apChannel=pref.getUChar("apch",6);
  cfg.apPassword=pref.getString("appass","12345678");
  cfg.ssid=pref.getString("ssid","");
  cfg.wifiPassword=pref.getString("wpass","");
  cfg.timePreference=pref.getString("time","auto");
  cfg.protocol=pref.getUChar("proto",PROTO_KAD);
  cfg.kadBigEndian=pref.getBool("kbe",true);
  cfg.kadFuelDiv=pref.getUShort("kdiv",128);
  cfg.kadVolt=pref.getUChar("kvolt",0);
  cfg.fuelCorr[0]=pref.getFloat("corr0",1.0f);
  if(pref.getBytesLength("badj")==sizeof(cfg.bandAdj))pref.getBytes("badj",cfg.bandAdj,sizeof(cfg.bandAdj));
  for(uint8_t b=0;b<3;b++)if(!(cfg.bandAdj[b]>=0.7f&&cfg.bandAdj[b]<=1.4f))cfg.bandAdj[b]=1.0f;
  cfg.calSig=pref.getFloat("calsig",0);cfg.calFills=pref.getUChar("calfills",0);cfg.calLitres=pref.getFloat("call",0);
  {uint16_t g[4]={0,0,0,0};if(pref.getBytesLength("gear")==sizeof(g))pref.getBytes("gear",g,sizeof(g));
   cfg.gearMaskF=g[0];cfg.gearValF=g[1];cfg.gearMaskR=g[2];cfg.gearValR=g[3];}
  cfg.fuelCorr[1]=pref.getFloat("corr1",1.0f);
  cfg.apName=pref.getString("apname","EngineLink");
  cfg.hostName=pref.getString("host","enginelink");
  if(!validHostName(cfg.hostName))cfg.hostName="enginelink";
  cfg.tankL=pref.getFloat("tank",0);
  cfg.reserve=pref.getUChar("reserve",10);
  cfg.defaultPrice=pref.getFloat("price",0);
  cfg.tripEndMin=pref.getUChar("tripend",2);
  cfg.autoPoll=pref.getBool("autopoll",false);
  cfg.rawKeepDays=pref.getUShort("rawkeep",90);
  cfg.ntpOn=pref.getBool("ntp",true);
  cfg.ntpServer=pref.getString("ntpsrv","pool.ntp.org");
  cfg.fuelScaleOk=pref.getBool("scaleok",false);
  cfg.voltOk=pref.getBool("voltok",false);
  cfg.fineTunes=pref.getUChar("tunes",0);
  cfg.lastTuneDev=pref.getFloat("tunedev",0);
  pref.end();
  if(!cfg.apName.length()||cfg.apName.length()>31)cfg.apName="EngineLink";
  if(!(cfg.tankL>=0&&cfg.tankL<=10000))cfg.tankL=0;
  if(cfg.reserve>50)cfg.reserve=10;
  if(!(cfg.defaultPrice>=0&&cfg.defaultPrice<1000))cfg.defaultPrice=0;
  if(cfg.tripEndMin<1||cfg.tripEndMin>60)cfg.tripEndMin=2;
  if(!cfg.ntpServer.length())cfg.ntpServer="pool.ntp.org";
  if(cfg.protocol>1)cfg.protocol=PROTO_KAD;
  if(cfg.kadFuelDiv!=128&&cfg.kadFuelDiv!=256)cfg.kadFuelDiv=128;
  for(int c=0;c<2;c++)if(!(cfg.fuelCorr[c]>=0.5f&&cfg.fuelCorr[c]<=2.0f))cfg.fuelCorr[c]=1.0f;
  if(cfg.apPassword.length()<8)cfg.apPassword="12345678";
  if(cfg.portChannel>2)cfg.portChannel=0;
  if(cfg.apChannel<1||cfg.apChannel>13)cfg.apChannel=6;
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
// Standard J1587 parameters shown on the overview when an ECU sends them.
// Scaling is applied in the UI and is labelled as unverified for the ECU.
constexpr uint8_t OVERVIEW_PIDS[]={91,92,94,96,100,102,105,110,168,174,175,247,250};
// KAD: latest reply per DataINDEX with both byte orders; the UI applies the
// VODIA scale and shows the chosen order.
void appendKadParams(String &s,uint8_t c,uint64_t now){
  s+=",\"kad\":{";
  bool first=true;
  for(uint8_t i=0;i<kadSeenCount;i++){
    const KadSeen &k=kadSeen[i];
    if(k.ch!=c||now<k.at||now-k.at>PARAM_FRESH_MS)continue;
    const KadParam *p=kadParam(k.index);
    if(!first)s+=',';
    first=false;
    s+='"';s+=k.index;s+="\":{\"v\":";s+=kadSigned(cfg.kadBigEndian?k.be:k.le,p);
    s+=",\"be\":";s+=k.be;s+=",\"le\":";s+=k.le;
    s+=",\"age\":";s+=(unsigned long)(now-k.at);
    s+='}';
  }
  s+='}';
}
void appendParams(String &s,uint8_t c,uint64_t now){
  if(cfg.protocol==PROTO_KAD){appendKadParams(s,c,now);return;}
  s+=",\"params\":{";
  bool first=true;
  for(uint8_t pid:OVERVIEW_PIDS){
    const PidSeen *best=nullptr;
    for(uint8_t i=0;i<pidCount;i++){
      if(pids[i].ch==c&&pids[i].pid==pid&&(!best||pids[i].at>best->at))best=&pids[i];
    }
    if(!best||now<best->at||now-best->at>PARAM_FRESH_MS)continue;
    uint32_t raw=0;
    const uint8_t n=min(best->len,(uint8_t)4);
    for(uint8_t b=0;b<n;b++)raw|=(uint32_t)best->data[b]<<(8*b);
    if(!first)s+=',';
    first=false;
    s+='"';s+=pid;s+="\":{\"raw\":";s+=raw;
    s+=",\"len\":";s+=best->len;
    s+=",\"mid\":";s+=best->mid;
    s+=",\"age\":";s+=(unsigned long)(now-best->at);
    s+='}';
  }
  s+='}';
}
void handleState(){
  const uint64_t now=elapsedMs();
  bool freshFuel[2]={fuelFresh(0,now),fuelFresh(1,now)};
  bool freshRPM[2]={rpmFresh(0,now),rpmFresh(1,now)};
  bool mapped=cfg.portChannel==1||cfg.portChannel==2;
  int port=mapped?(cfg.portChannel-1):-1,starboard=mapped?(1-port):-1;
  bool totalAvailable=mapped&&freshFuel[0]&&freshFuel[1];
  float total=totalAvailable?motor[0].fuelLph+motor[1].fuelLph:0;
  bool speedAvailable=isfinite(sogKnots)&&sogAt&&now-sogAt<7000 && sogKnots>0.5;
  String s;s.reserve(14000);
  s+="{\"version\":\"" EL_VERSION "\",\"build\":\"" __DATE__ " " __TIME__ "\",\"setup\":";
  s+=cfg.completed?"true":"false";
  s+=",\"mode\":\"PASSIVE\",\"uptime\":";
  s+=(unsigned long)(now/1000ULL);
  s+=",\"bootId\":";s+=bootId;
  s+=",\"timeKnown\":";s+=validWall()?"true":"false";
  s+=",\"timeSource\":";jsonString(s,timeSource);
  s+=",\"utc\":";s+=validWall()?(long long)time(nullptr):0;
  s+=",\"network\":{\"apSsid\":\"EngineLink\",\"apClients\":";
  s+=WiFi.softAPgetStationNum();
  s+=",\"apIP\":\"192.168.4.1\",\"apChannel\":";s+=cfg.apChannel;
  s+=",\"mdns\":";s+=mdnsReady?"true":"false";
  s+=",\"router\":";s+=cfg.router?"true":"false";
  s+=",\"staConnected\":";s+=WiFi.status()==WL_CONNECTED?"true":"false";
  s+=",\"staIP\":";jsonString(s,WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():String(""));
  s+=",\"rssi\":";if(WiFi.status()==WL_CONNECTED)s+=WiFi.RSSI();else s+="null";
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
  s+=",\"apChannel\":";s+=cfg.apChannel;
  s+=",\"timePref\":";jsonString(s,cfg.timePreference);
  s+=",\"canListen\":";s+=cfg.canListen?"true":"false";
  s+=",\"defaultPassword\":";s+=cfg.apPassword=="12345678"?"true":"false";
  s+=",\"protocol\":";s+=cfg.protocol==PROTO_KAD?"\"kad\"":"\"j1587\"";
  s+=",\"kadBigEndian\":";s+=cfg.kadBigEndian?"true":"false";
  s+=",\"kadFuelDiv\":";s+=cfg.kadFuelDiv;
  s+=",\"kadVolt\":";s+=cfg.kadVolt;
  s+=",\"fuelCorr\":[";s+=String(cfg.fuelCorr[0],4);s+=',';s+=String(cfg.fuelCorr[1],4);s+=']';
  s+=",\"apName\":";jsonString(s,cfg.apName);
  s+=",\"host\":";jsonString(s,cfg.hostName);
  s+=",\"gear\":[";s+=cfg.gearMaskF;s+=',';s+=cfg.gearValF;s+=',';s+=cfg.gearMaskR;s+=',';s+=cfg.gearValR;s+=']';
  s+=",\"ssid\":";jsonString(s,cfg.ssid);
  s+=",\"tankL\":";s+=String(cfg.tankL,0);
  s+=",\"reserve\":";s+=cfg.reserve;
  s+=",\"price\":";s+=String(cfg.defaultPrice,2);
  s+=",\"tripEnd\":";s+=cfg.tripEndMin;
  s+=",\"autoPoll\":";s+=cfg.autoPoll?"true":"false";
  s+=",\"rawKeep\":";s+=cfg.rawKeepDays;
  s+=",\"ntp\":";s+=cfg.ntpOn?"true":"false";
  s+=",\"ntpServer\":";jsonString(s,cfg.ntpServer);
  s+=",\"scaleOk\":";s+=cfg.fuelScaleOk?"true":"false";
  s+=",\"voltOk\":";s+=cfg.voltOk?"true":"false";
  s+=",\"fineTunes\":";s+=cfg.fineTunes;
  s+="},\"sd\":{\"ready\":";
  s+=sdReady?"true":"false";
  s+=",\"inserted\":";s+=cardInserted()?"true":"false";
  s+=",\"logging\":";s+=sdLogging?"true":"false";
  s+=",\"fault\":";s+=sdFaultLatched?"true":"false";
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
  s+=",\"stepSeconds\":";s+=calActive?((millis()-calStepStartMs)/1000UL):0;
  // Cached: avoid an SD directory lookup on every status poll.
  s+=",\"available\":";s+=(sdReady&&!calActive&&testFileAvailable)?"true":"false";
  // Report the real system state even while the LED demo sequence runs, so a
  // test never hides an active fault in the UI.
  s+="},\"led\":{\"name\":";jsonString(s,ledlang::PATTERNS[ledStateNow()].name);
  s+=",\"status\":";s+=(unsigned)ledStateNow();
  s+=",\"testing\":";s+=ledTestRunning()?"true":"false";
  s+="},\"hardware\":{\"heap\":";
  s+=ESP.getFreeHeap();
  s+=",\"minHeap\":";s+=ESP.getMinFreeHeap();
  s+=",\"psram\":";s+=psramFound()?ESP.getPsramSize():0;
  s+=",\"freePsram\":";s+=psramFound()?ESP.getFreePsram():0;
  s+=",\"opto\":";s+=digitalRead(OPTO_PIN);
  s+=",\"canReady\":";s+=canReady?"true":"false";
  s+=",\"nmeaRx\":";s+=ctr.nmeaFrames;
  s+=",\"resetReason\":";s+=(unsigned int)esp_reset_reason();
  s+=",\"sketchKB\":";s+=sketchKB;
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
    s+=",\"fuelAge\":";if(motor[c].fuelEver)s+=(unsigned long)(now-motor[c].fuelAt);else s+="null";
    s+=",\"rpm\":";
    if(freshRPM[c])s+=String(motor[c].rpm,1);else s+="null";
    s+=",\"running\":";s+=motor[c].running?"true":"false";
    s+=",\"runSeconds\":";s+=motor[c].running?(unsigned long)((now-motor[c].runStartMs)/1000ULL):0UL;
    s+=",\"observedLiters\":";s+=String(motor[c].observedLiters,4);
    s+=",\"fuelSamples\":";s+=motor[c].fuelSamples;
    s+=",\"rpmSamples\":";s+=motor[c].rpmSamples;
    s+=",\"gaps\":";s+=motor[c].gaps;
    s+=",\"lastMID\":";s+=motor[c].lastMID;
    s+=",\"tripLiters\":";s+=String(tripLitersFor(c),3);
    s+=",\"lifetimeLiters\":";s+=String(lifetimeLitersFor(c),2);
    // Uncorrected candidates for the tank-to-tank scale decision.
    s+=",\"trip128\":";s+=String(totals.tripRawH[c]/128.0,3);
    s+=",\"trip256\":";s+=String(totals.tripRawH[c]/256.0,3);
    const KadLink &k=kad[c];
    s+=",\"link\":{\"polling\":";s+=k.polling?"true":"false";
    s+=",\"requests\":";s+=k.requests;
    s+=",\"replies\":";s+=k.replies;
    s+=",\"foreign\":";s+=k.foreignRequests;
    s+=",\"foreignAge\":";if(k.foreignRequests)s+=(unsigned long)(now-k.lastForeignAt);else s+="null";
    s+=",\"unanswered\":";s+=k.unanswered;
    s+=",\"orphans\":";s+=k.orphanReplies;
    s+=",\"replyAge\":";if(k.lastReplyAt)s+=(unsigned long)(now-k.lastReplyAt);else s+="null";
    s+=",\"pollSeconds\":";s+=k.polling?(unsigned long)((millis()-k.startedMs)/1000UL):0UL;
    s+=",\"stopReason\":";jsonString(s,k.stopReason);
    s+='}';
    s+=",\"engineSeconds\":";s+=totals.runSeconds[c];
    s+=",\"tripSeconds\":";s+=totals.tripRunSeconds[c];
    appendParams(s,c,now);
    s+="}";
  }
  s+="],\"trip\":{\"startUtc\":";s+=(long long)totals.tripStartUtc;
  s+=",\"resets\":";s+=totals.tripResets;
  s+="},\"segments\":[";
  for(uint8_t i=0;i<segmentCount;i++){
    const Segment &g=segments[(segmentIndex+MAX_SEGMENTS-1-i)%MAX_SEGMENTS];
    if(i)s+=',';
    s+="{\"ch\":";s+=g.ch;
    s+=",\"start\":";s+=(unsigned long)(g.startMs/1000ULL);
    s+=",\"end\":";s+=(unsigned long)(g.endMs/1000ULL);
    s+=",\"utc\":";s+=(long long)g.startUtc;
    s+=",\"liters\":";s+=String(g.liters,3);
    s+=",\"maxRpm\":";s+=String(g.maxRpm,0);
    s+='}';
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
    s+=",\"dir\":\"";s+=e.dir;s+='"';
    s+=",\"kad\":";s+=e.kadIndex;
    s+=",\"hex\":";jsonString(s,e.hex);
    s+="}";
  }
  s+="]}";
  sendJSON(200,s);
}
void handlePids(){
  const uint64_t now=elapsedMs();
  String s;s.reserve(9000);
  s+="{\"full\":";s+=ctr.pidTableFull;
  s+=",\"pids\":[";
  for(uint8_t i=0;i<pidCount;i++){
    const PidSeen &p=pids[i];
    if(i)s+=',';
    s+="{\"ch\":";s+=p.ch;
    s+=",\"mid\":";s+=p.mid;
    s+=",\"pid\":";s+=p.pid;
    s+=",\"len\":";s+=p.len;
    s+=",\"count\":";s+=p.count;
    s+=",\"age\":";s+=(unsigned long)(now>=p.at?now-p.at:0);
    s+=",\"hex\":\"";
    const uint8_t n=min(p.len,(uint8_t)sizeof(p.data));
    for(uint8_t b=0;b<n;b++){char h[4];snprintf(h,sizeof(h),b?" %02X":"%02X",p.data[b]);s+=h;}
    if(p.len>sizeof(p.data))s+=" …";
    s+="\"}";
  }
  s+="],\"kad\":[";
  for(uint8_t i=0;i<kadSeenCount;i++){
    const KadSeen &k=kadSeen[i];
    if(i)s+=',';
    s+="{\"ch\":";s+=k.ch;
    s+=",\"index\":";s+=k.index;
    s+=",\"len\":";s+=k.len;
    s+=",\"count\":";s+=k.count;
    s+=",\"age\":";s+=(unsigned long)(now>=k.at?now-k.at:0);
    s+=",\"be\":";s+=k.be;s+=",\"le\":";s+=k.le;
    s+=",\"hex\":\"";
    const uint8_t n=min(k.len,(uint8_t)sizeof(k.reply));
    for(uint8_t b=0;b<n;b++){char h[4];snprintf(h,sizeof(h),b?" %02X":"%02X",k.reply[b]);s+=h;}
    if(k.len>sizeof(k.reply))s+=" …";
    s+="\"}";
  }
  s+="]}";
  sendJSON(200,s);
}
void handleHistory(){
  String s;s.reserve(HISTORY_LEN*24+200);
  s+="{\"step\":";s+=HISTORY_STEP_MS/1000;
  s+=",\"count\":";s+=histCount;
  const char *names[]={"fuel0","fuel1","rpm0","rpm1","sog"};
  for(uint8_t k=0;k<5;k++){
    s+=",\"";s+=names[k];s+="\":[";
    for(uint16_t i=0;i<histCount;i++){
      const uint16_t idx=(histIndex+HISTORY_LEN-histCount+i)%HISTORY_LEN;
      if(i)s+=',';
      if(k<2){int16_t v=histFuel[k][idx];if(v<0)s+="null";else s+=String(v/100.0f,2);}
      else if(k<4){uint16_t v=histRpm[k-2][idx];if(v==0xFFFF)s+="null";else s+=v;}
      else{int16_t v=histSog[idx];if(v<0)s+="null";else s+=String(v/100.0f,2);}
    }
    s+=']';
  }
  s+='}';
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
  int apch=web.hasArg("apch")?web.arg("apch").toInt():cfg.apChannel;
  if(apch!=1&&apch!=6&&apch!=11){reply(false,"Wi-Fi channel must be 1, 6 or 11",400);return;}
  // Start/stop CAN first: a failure must not leave half-applied settings in RAM.
  bool newCAN=web.arg("can")=="1";
  if(newCAN!=cfg.canListen){
    if(!newCAN)stopCAN();
    else if(!startCAN()){reply(false,"NMEA2000 listen-only interface could not start",409);return;}
    cfg.canListen=newCAN;
  }
  bool changeNetwork=cfg.router!=(network=="router")||cfg.ssid!=ssid||cfg.apChannel!=(uint8_t)apch;
  String newRouterPass=web.arg("pass");
  if(newRouterPass.length()){cfg.wifiPassword=newRouterPass;changeNetwork=true;}
  if(apPass.length()&&apPass!=cfg.apPassword){cfg.apPassword=apPass;changeNetwork=true;}
  cfg.router=network=="router";
  cfg.ssid=ssid;
  cfg.apChannel=(uint8_t)apch;
  cfg.timePreference=preference;
  cfg.portChannel=(uint8_t)port;
  cfg.completed=true;
  saveSettings();
  reply(true,changeNetwork?"Saved. Wi-Fi restarts now; reconnect to EngineLink if your phone drops.":"Settings saved.");
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
  if(!totals.tripStartUtc&&totals.tripResets==0){totals.tripStartUtc=sec;totalsDirty=true;}
  reply(true,"Clock synchronized from phone");
}
void handleLEDTest(){
  ledTestStartedMs=millis();ledPrev=0xFFFFFFFF;
  reply(true,"Showing every LED pattern, 3 seconds each");
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
void handleSegmentsDownload(){
  if(!sdReady||!SD.exists("/SEGMENTS.CSV")){web.send(404,"text/plain","No engine-run segments recorded on this SD card yet");return;}
  File f=SD.open("/SEGMENTS.CSV",FILE_READ);
  if(!f){web.send(500,"text/plain","Cannot open segments file");return;}
  web.sendHeader("Content-Disposition","attachment; filename=EngineLink-engine-runs.csv");
  web.streamFile(f,"text/csv");
  f.close();
}
// Polling is never persisted: after any restart EngineLink only listens.
void handleKadPoll(){
  const int ch=web.arg("ch").toInt();
  if(ch<0||ch>1){reply(false,"Invalid connector",400);return;}
  if(web.arg("on")!="1"){
    if(kad[ch].polling)stopPolling(ch,"Stopped by user");
    reply(true,"Polling stopped. EngineLink only listens on this connector.");return;
  }
  if(web.arg("confirm")!="POLL"){reply(false,"Confirmation missing",400);return;}
  String why;
  if(!kad[ch].polling&&!startPolling(ch,why)){reply(false,why,409);return;}
  kad[ch].byAuto=false;kad[ch].byTest=false;
  reply(true,String("Polling ")+(ch==0?"J7":"J6")+": reading as fast as the engine answers. It stops after a restart.");
}
void applyProtocol(){
  for(uint8_t c=0;c<2;c++){
    if(kad[c].polling)stopPolling(c,"Protocol changed");
    kad[c].pending=false;
    kad[c].lastAutoTry=0;  // automatic reading starts again right away
    if(uartReady[c])uart_set_rx_timeout(portFor(c),rxTimeoutSymbols());
  }
}
void handleKadConfig(){
  if(web.hasArg("protocol")){
    const String v=web.arg("protocol");
    if(v!="kad"&&v!="j1587"){reply(false,"Invalid protocol",400);return;}
    const uint8_t np=v=="kad"?PROTO_KAD:PROTO_J1587;
    if(np!=cfg.protocol){cfg.protocol=np;applyProtocol();}
  }
  if(web.hasArg("endian"))cfg.kadBigEndian=web.arg("endian")!="le";
  if(web.hasArg("fuelDiv")){
    const int d=web.arg("fuelDiv").toInt();
    if(d!=128&&d!=256){reply(false,"Fuel scale must be 1/128 or 1/256",400);return;}
    cfg.kadFuelDiv=(uint16_t)d;
    recalibrate();
  }
  if(web.hasArg("volt")){
    const int v=web.arg("volt").toInt();
    if(v!=0&&v!=1){reply(false,"Invalid voltage scale",400);return;}
    cfg.kadVolt=(uint8_t)v;
  }
  for(uint8_t c=0;c<2;c++){
    const String key=String("corr")+c;
    if(!web.hasArg(key))continue;
    const float f=web.arg(key).toFloat();
    if(!(f>=0.5f&&f<=2.0f)){reply(false,"Correction must be between 0.5 and 2.0",400);return;}
    cfg.fuelCorr[c]=f;
  }
  saveSettings();
  reply(true,"Engine data settings saved. Totals are recalculated from the raw record.");
}

// ---------- 0.5.0 app API ----------
bool parseIds(const String &ids,uint32_t *out,uint8_t &n){
  n=0;int from=0;
  while(from<(int)ids.length()&&n<MAX_TRIPS){
    int comma=ids.indexOf(',',from);if(comma<0)comma=ids.length();
    const uint32_t id=ids.substring(from,comma).toInt();
    if(id)out[n++]=id;
    from=comma+1;
  }
  return n>0;
}

void jnum(String &s,double v,uint8_t dec){if(isfinite(v))s+=String(v,(unsigned int)dec);else s+="null";}
void jkey(String &s,const char *k){s+='"';s+=k;s+="\":";}
int portIndex(){return cfg.portChannel==1?0:cfg.portChannel==2?1:-1;}
// Small, fast summary for the live screens. Polled about twice a second.
void handleLive(){
  const uint64_t now=elapsedMs();
  String s;s.reserve(2600);
  s+="{\"v\":\"" EL_VERSION "\",\"up\":";s+=(unsigned long)(now/1000ULL);
  s+=",\"utc\":";s+=validWall()?(long long)time(nullptr):0;
  s+=",\"ts\":";jsonString(s,timeSource);
  s+=",\"proto\":";s+=cfg.protocol==PROTO_KAD?"\"kad\"":"\"j1587\"";
  s+=",\"port\":";s+=portIndex();
  s+=",\"e\":[";
  for(uint8_t c=0;c<2;c++){
    if(c)s+=',';
    const KadLink &k=kad[c];
    s+="{\"run\":";s+=motor[c].running?"true":"false";
    s+=",\"rpm\":";if(rpmFresh(c,now))jnum(s,motor[c].rpm,0);else s+="null";
    s+=",\"lph\":";if(fuelFresh(c,now))jnum(s,motor[c].fuelLph,2);else s+="null";
    s+=",\"seen\":";
    const uint64_t last=max(motor[c].rpmAt,motor[c].fuelAt);
    if(last&&now>=last)s+=(unsigned long)((now-last)/1000ULL);else s+="null";
    s+=",\"poll\":";s+=k.polling?"true":"false";
    s+=",\"why\":";jsonString(s,k.stopReason);
    s+=",\"hrs\":";jnum(s,totals.lastEcuHours[c],1);
    s+=",\"x\":{";
    bool first=true;
    for(uint8_t i=0;i<KAD_N;i++){
      float v;
      if(!engineValueAge(c,KAD_PARAMS[i].index,v,15000))continue;
      if(!first)s+=',';
      first=false;
      s+='"';s+=KAD_PARAMS[i].index;s+="\":";jnum(s,v,2);
    }
    s+="}}";
  }
  s+="],\"trip\":{\"act\":";s+=totals.tripActive?"true":"false";
  s+=",\"start\":";s+=(long long)totals.tripStartUtc;
  s+=",\"run\":";s+=totals.tripAnySec;
  s+=",\"l\":[";jnum(s,tripLitersFor(0),2);s+=',';jnum(s,tripLitersFor(1),2);s+=']';
  s+=",\"max\":";s+=totals.tripMaxRpm;
  s+="},\"tank\":{\"cap\":";jnum(s,cfg.tankL,0);
  const double ab=tankAboard();
  s+=",\"aboard\":";if(ab>=0)jnum(s,ab,1);else s+="null";
  s+=",\"pm\":";jnum(s,tankPlusMinus(),0);
  s+=",\"reserve\":";s+=cfg.reserve;
  s+="},\"price\":";jnum(s,latestPrice(),2);
  s+=",\"sog\":";if(sogFresh())jnum(s,sogKnots,1);else s+="null";
  s+=",\"net\":{\"ap\":";jsonString(s,cfg.apName);
  s+=",\"host\":";jsonString(s,cfg.hostName);
  s+=",\"cl\":";s+=WiFi.softAPgetStationNum();
  s+=",\"ch\":";s+=apChannelNow;
  s+=",\"rt\":";s+=routerConfigured()?"true":"false";
  s+=",\"sta\":";s+=WiFi.status()==WL_CONNECTED?"true":"false";
  s+=",\"ssid\":";jsonString(s,cfg.ssid);
  s+=",\"rssi\":";if(WiFi.status()==WL_CONNECTED)s+=WiFi.RSSI();else s+="null";
  s+=",\"ip\":";if(WiFi.status()==WL_CONNECTED)jsonString(s,WiFi.localIP().toString());else s+="null";
  s+=",\"conflict\":";s+=staConflict?"true":"false";
  s+="},\"sd\":{\"ok\":";s+=sdReady?"true":"false";
  s+=",\"log\":";s+=sdLogging?"true":"false";
  s+="},\"test\":";s+=calActive?"true":"false";
  s+=",\"rb\":";s+=rollbackPending?"true":"false";
  s+=",\"fw\":";s+=(unsigned)fwState;
  s+=",\"led\":\"";s+=ledlang::PATTERNS[ledStateNow()].id;s+='"';
  s+=",\"ev\":{\"n\":";s+=unackedEvents();s+=",\"alarm\":";s+=diagRules.alarm()?"true":"false";s+=",\"warn\":";s+=diagRules.warning()?"true":"false";
  s+=",\"last\":";
  if(eventCount){const EventRec &e=events[0];s+="{\"id\":";s+=e.id;s+=",\"t\":\"";s+=diag::TYPE_ID[e.type<diag::T_COUNT?e.type:0];s+="\",\"eng\":";s+=e.eng;s+=",\"sev\":";s+=e.sev;
    s+=",\"v\":";jnum(s,e.value,2);s+=",\"ref\":";jnum(s,e.ref,2);s+=",\"utc\":";s+=(long long)e.utc;s+=",\"ack\":";s+=e.ack;s+='}';}
  else s+="null";
  s+='}';
  s+='}';
  sendJSON(200,s);
}
void appendTrip(String &s,const TripRec &r){
  s+="{\"id\":";s+=r.id;s+=",\"g\":";s+=r.group;
  s+=",\"s\":";s+=(long long)r.startUtc;s+=",\"e\":";s+=(long long)r.endUtc;
  s+=",\"run\":";s+=r.anySec;
  s+=",\"l\":[";jnum(s,tripLitres(r,0),2);s+=',';jnum(s,tripLitres(r,1),2);s+=']';
  s+=",\"max\":";s+=r.maxRpm;
  s+=",\"p\":";s+=r.people;s+=",\"sea\":";s+=r.sea;s+=",\"w\":";s+=r.wind;s+=",\"x\":";s+=r.extras;
  s+=",\"n\":";jsonString(s,String(r.note));
  s+=",\"b\":";s+=r.bootId;
  s+='}';
}
void handleTrips(){
  String s;s.reserve(220*tripCount+200);
  s+="{\"trips\":[";
  for(uint8_t i=0;i<tripCount;i++){if(i)s+=',';appendTrip(s,trips[i]);}
  s+="],\"port\":";s+=portIndex();
  s+='}';
  sendJSON(200,s);
}
void handleTripNote(){
  TripRec *r=tripById(web.arg("id").toInt());
  if(!r){reply(false,"Trip not found",404);return;}
  const int p=web.arg("people").toInt(),sea=web.arg("sea").toInt(),w=web.arg("wind").toInt(),x=web.arg("extras").toInt();
  if(p<0||p>30||sea<0||sea>3||w<0||w>3||x<0||x>7){reply(false,"Invalid note",400);return;}
  r->people=p;r->sea=sea;r->wind=w;r->extras=x;
  String n=web.arg("note");n.trim();
  memset(r->note,0,sizeof(r->note));
  strncpy(r->note,n.c_str(),sizeof(r->note)-1);
  saveTrips();
  reply(true,"Trip notes saved");
}
// Combines the chosen trips (and the groups they already belong to) into
// one group, named after the oldest trip.
void handleTripCombine(){
  uint32_t sel[MAX_TRIPS];uint8_t n=0;
  parseIds(web.arg("ids"),sel,n);
  if(n<2){reply(false,"Select two or more trips",400);return;}
  bool hit[MAX_TRIPS]={};
  for(uint8_t j=0;j<n;j++){
    const TripRec *r=tripById(sel[j]);
    if(!r){reply(false,"Trip not found",404);return;}
    for(uint8_t i=0;i<tripCount;i++)if(trips[i].id==r->id||(r->group&&trips[i].group==r->group))hit[i]=true;
  }
  uint32_t groupId=0xFFFFFFFF;
  for(uint8_t i=0;i<tripCount;i++)if(hit[i]&&trips[i].id<groupId)groupId=trips[i].id;
  for(uint8_t i=0;i<tripCount;i++)if(hit[i])trips[i].group=groupId;
  saveTrips();
  reply(true,"Trips combined");
}
void handleTripSplit(){
  const uint32_t g=web.arg("group").toInt();
  if(!g){reply(false,"Invalid group",400);return;}
  bool any=false;
  for(uint8_t i=0;i<tripCount;i++)if(trips[i].group==g){trips[i].group=0;any=true;}
  if(!any){reply(false,"Group not found",404);return;}
  saveTrips();
  reply(true,"Split into separate trips");
}
void handleFuelList(){
  String s;s.reserve(120*fuelCount+400);
  s+="{\"log\":[";
  for(uint8_t i=0;i<fuelCount;i++){
    const FuelRec &r=fuelLog[i];
    if(i)s+=',';
    s+="{\"utc\":";s+=(long long)r.utc;
    s+=",\"l\":";jnum(s,r.litres,1);
    s+=",\"p\":";jnum(s,r.price,2);
    s+=",\"full\":";s+=r.full?"true":"false";
    s+=",\"c\":";if(r.counted>=0)jnum(s,r.counted,1);else s+="null";
    s+=",\"r128\":";jnum(s,r.rawDelta/128.0,1);
    s+=",\"r256\":";jnum(s,r.rawDelta/256.0,1);
    s+=",\"q\":";s+=r.quality;s+=",\"use\":";s+=r.calUse;
    s+=",\"cs\":";s+=calState[i];s+=",\"z\":";jnum(s,calZ[i],1);
    s+=",\"pump\":";jnum(s,calPump[i],1);
    s+=",\"now\":";if(calCounted[i]>=0)jnum(s,calCounted[i],1);else s+="null";
    s+=",\"base\":";if(calBase[i]>=0)jnum(s,calBase[i],1);else s+="null";
    s+='}';
  }
  s+="],\"cal\":{\"f\":";jnum(s,cfg.fuelCorr[0],4);
  s+=",\"b\":[";for(uint8_t b=0;b<FUEL_BANDS;b++){if(b)s+=',';jnum(s,cfg.bandAdj[b],4);}
  s+="],\"sig\":";jnum(s,cfg.calSig,4);s+=",\"n\":";s+=cfg.calFills;s+=",\"l\":";jnum(s,cfg.calLitres,1);
  s+=",\"bands\":[";
  for(uint8_t b=0;b<FUEL_BANDS;b++){
    double l=0;for(uint8_t c=0;c<2;c++)l+=totals.lifeRawB[c][b]/(double)cfg.kadFuelDiv;
    if(b)s+=',';jnum(s,l,1);
  }
  s+="],\"pm\":";jnum(s,tankPlusMinus(),1);
  s+=",\"gaps\":";s+=(unsigned long)(totals.gapFillSec[0]+totals.gapFillSec[1]);
  s+="},\"tunes\":";s+=cfg.fineTunes;
  s+=",\"tuneDev\":";jnum(s,cfg.lastTuneDev,4);
  s+=",\"since\":";if(tankKnown())jnum(s,usedSince(totals.tankRef),1);else s+="null";
  s+=",\"missed\":{\"n\":";s+=totals.missedEvents;s+=",\"l\":";jnum(s,totals.estMissedL,1);
  s+=",\"last\":";s+=(long long)lastMissedUtc;s+=",\"lastL\":";jnum(s,lastMissedL,1);
  s+="}}";
  sendJSON(200,s);
}
void handleFuelAdd(){
  String msg;
  const float litres=web.arg("litres").toFloat(),price=web.arg("price").toFloat();
  int q=web.hasArg("q")?web.arg("q").toInt():(web.arg("full")=="1"?1:0);
  if(q<0||q>2)q=1;
  const int64_t utc=strtoll(web.arg("utc").c_str(),nullptr,10);
  if(q&&cfg.tankL<=0){reply(false,"Set the fuel tank size in Settings first",409);return;}
  const bool ok=addFuel(litres,price,(uint8_t)q,utc,msg);
  reply(ok,msg,ok?200:400);
}
void handleFuelEdit(){
  const int i=web.arg("i").toInt();
  const int q=web.hasArg("q")?web.arg("q").toInt():-1,use=web.hasArg("use")?web.arg("use").toInt():-1;
  if(i<0||!editFuel((uint8_t)i,q,use)){reply(false,"Entry not found",404);return;}
  reply(true,cfg.calFills?"Fuel figures now within about "+String(cfg.calSig*200.0f,1)+" %":"Saved");
}
void handleFuelDelete(){
  const int i=web.arg("i").toInt();
  if(i<0||!deleteFuel((uint8_t)i)){reply(false,"Entry not found",404);return;}
  reply(true,"Entry deleted");
}
void handleEconomy(){
  String s;s.reserve(3200);
  s+="{\"season\":";s+=(long long)totals.seasonStartUtc;
  s+=",\"b\":[";
  for(uint8_t b=0;b<RPM_BUCKETS;b++){
    if(b)s+=',';
    s+="[";s+=1000+b*250;s+=',';jnum(s,totals.bucketSec[b],0);s+=',';jnum(s,totals.bucketL[b],2);
    s+=',';jnum(s,totals.bucketSogSec[b],0);s+=',';jnum(s,totals.bucketSogL[b],2);s+=',';jnum(s,totals.bucketNm[b],2);s+=']';
  }
  s+="],\"z\":[";
  for(uint8_t z=0;z<ZONES;z++){
    if(z)s+=',';
    s+='[';jnum(s,totals.zoneL[z],2);s+=',';jnum(s,totals.zoneSec[z],0);s+=',';jnum(s,totals.zoneNm[z],2);s+=']';
  }
  s+="],\"trips\":";s+=tripCount;
  s+=",\"h\":{\"dev\":[";
  for(uint8_t c=0;c<2;c++){
    if(c)s+=',';
    uint16_t maxTrips=0;
    for(uint8_t b=0;b<RPM_BUCKETS;b++)if(health.baseTrips[c][b]>maxTrips)maxTrips=health.baseTrips[c][b];
    s+="{\"trips\":";s+=maxTrips;s+=",\"n\":";s+=health.devSamples[c];s+=",\"rpm\":";s+=health.devRpm[c];
    s+=",\"d\":[";for(uint8_t m=0;m<HEALTH_METRICS;m++){if(m)s+=',';jnum(s,health.dev[c][m],4);}s+="]";
    // Normal values at the rpm step of the latest comparison.
    s+=",\"base\":[";
    const int bb=health.devRpm[c]>=1000?bucketFor(health.devRpm[c]):-1;
    for(uint8_t m=0;m<HEALTH_METRICS;m++){if(m)s+=',';if(bb>=0)jnum(s,health.base[c][bb][m],2);else s+="null";}
    s+="]}";
  }
  s+="],\"bal\":";jnum(s,health.balance,4);
  s+=",\"upd\":";s+=(long long)health.updatedUtc;
  s+="}}";
  sendJSON(200,s);
}
void handlePrefsGet(){
  web.sendHeader("Cache-Control","no-store");
  web.send(200,"application/json; charset=utf-8",cfg.prefs);
}
void handlePrefsSet(){
  String p=web.arg("p");
  if(p.length()<2||p.length()>4000||p[0]!='{'){reply(false,"Invalid preferences",400);return;}
  cfg.prefs=p;savePrefs();
  reply(true,"Saved");
}
// Device settings the firmware itself uses. Every field is optional.
void handleCfg(){
  if(web.hasArg("tank")){
    const float t=web.arg("tank").toFloat();
    if(!(t>=50&&t<=10000)){reply(false,"Tank size must be 50 to 10000 litres",400);return;}
    // A new size invalidates the estimate until the next full tank.
    if(fabsf(t-cfg.tankL)>0.5f){cfg.tankL=t;totals.tankRefL=-1;saveTotals();}
  }
  if(web.hasArg("reserve")){const int r=web.arg("reserve").toInt();if(r<0||r>50){reply(false,"Invalid reserve",400);return;}cfg.reserve=r;}
  if(web.hasArg("price")){const float p=web.arg("price").toFloat();if(!(p>=0&&p<1000)){reply(false,"Invalid price",400);return;}cfg.defaultPrice=p;}
  if(web.hasArg("tripEnd")){const int m=web.arg("tripEnd").toInt();if(m<1||m>60){reply(false,"Invalid trip end time",400);return;}cfg.tripEndMin=m;}
  if(web.hasArg("autoPoll")){
    const bool on=web.arg("autoPoll")=="1";
    // Switching on sends read requests to the engines: it needs the typed confirmation.
    if(on&&!cfg.autoPoll&&web.arg("confirm")!="POLL"){reply(false,"Confirmation missing",400);return;}
    cfg.autoPoll=on;
    if(!cfg.autoPoll)for(uint8_t c=0;c<2;c++)if(kad[c].polling&&kad[c].byAuto)stopPolling(c,"Automatic reading switched off");
  }
  if(web.hasArg("rawKeep")){const int d=web.arg("rawKeep").toInt();if(d<0||d>3650){reply(false,"Invalid value",400);return;}cfg.rawKeepDays=d;}
  if(web.hasArg("ntp")){cfg.ntpOn=web.arg("ntp")=="1";ntpStarted=false;}
  // Gear bits learned at the dock: "mask:value" for forward and reverse, "" to forget.
  for(uint8_t g=0;g<2;g++){
    const char *key=g?"gearR":"gearF";
    if(!web.hasArg(key))continue;
    const String v=web.arg(key);
    uint16_t m=0,val=0;
    if(v.length()){
      const int c=v.indexOf(':');
      if(c<1){reply(false,"Invalid gear pattern",400);return;}
      m=(uint16_t)strtoul(v.substring(0,c).c_str(),nullptr,0);val=(uint16_t)strtoul(v.substring(c+1).c_str(),nullptr,0)&m;
    }
    if(g){cfg.gearMaskR=m;cfg.gearValR=val;}else{cfg.gearMaskF=m;cfg.gearValF=val;}
  }
  if(web.hasArg("host")){
    String v=web.arg("host");v.trim();v.toLowerCase();v.replace(' ','-');
    if(v.endsWith(".local"))v.remove(v.length()-6);
    if(!validHostName(v)){reply(false,"Use 1 to 32 letters, digits or hyphens, for example enginelink",400);return;}
    if(v!=cfg.hostName){cfg.hostName=v;WiFi.setHostname(v.c_str());startNameService();}
  }
  if(web.hasArg("ntpServer")){String v=web.arg("ntpServer");v.trim();if(v.length()<3||v.length()>63){reply(false,"Invalid server",400);return;}cfg.ntpServer=v;ntpStarted=false;}
  if(web.hasArg("port")){const int p=web.arg("port").toInt();if(p<0||p>2){reply(false,"Invalid socket choice",400);return;}cfg.portChannel=p;}
  if(web.hasArg("can")){
    const bool on=web.arg("can")=="1";
    if(on!=cfg.canListen){if(!on)stopCAN();else if(!startCAN()){reply(false,"NMEA 2000 listen-only interface could not start",409);return;}cfg.canListen=on;}
  }
  if(web.hasArg("fuelDiv")){
    const int d=web.arg("fuelDiv").toInt();
    if(d!=128&&d!=256){reply(false,"Fuel scale must be 1/128 or 1/256",400);return;}
    cfg.kadFuelDiv=d;cfg.fuelScaleOk=true;
    recalibrate();
  }
  if(web.hasArg("volt")){const int v=web.arg("volt").toInt();if(v!=0&&v!=1){reply(false,"Invalid voltage factor",400);return;}cfg.kadVolt=v;cfg.voltOk=true;}
  if(web.hasArg("protocol")){
    const String v=web.arg("protocol");
    if(v!="kad"&&v!="j1587"){reply(false,"Invalid protocol",400);return;}
    const uint8_t np=v=="kad"?PROTO_KAD:PROTO_J1587;
    if(np!=cfg.protocol){cfg.protocol=np;applyProtocol();}
  }
  cfg.completed=true;
  saveSettings();
  reply(true,"Saved");
}
// EngineLink's own network: name and password.
void handleApSave(){
  String name=web.arg("name");name.trim();
  String pass=web.arg("pass");
  if(pass=="__keep__")pass=cfg.apPassword; // rename only
  if(name.length()<1||name.length()>31){reply(false,"Network name must be 1 to 31 characters",400);return;}
  if(pass.length()<8||pass.length()>63){reply(false,"Password must be 8 to 63 characters",400);return;}
  const bool change=name!=cfg.apName||pass!=cfg.apPassword;
  cfg.apName=name;cfg.apPassword=pass;cfg.completed=true;saveSettings();
  reply(true,change?"Saved. The EngineLink network restarts with the new name and password; connect your phone to it again.":"Nothing changed");
  if(change)pendingNetwork=true;
}
void handleWifiScan(){
  if(web.arg("start")=="1"&&!scanBusy){
    WiFi.scanDelete();
    if(WiFi.scanNetworks(true,false,false,120)==WIFI_SCAN_FAILED){reply(false,"Scan could not start",409);return;}
    scanBusy=true;scanCount=-1;scanStartedMs=millis();
  }
  String s="{\"busy\":";s+=scanBusy?"true":"false";
  s+=",\"nets\":[";
  if(!scanBusy&&scanCount>0){
    bool first=true;
    for(int16_t i=0;i<scanCount&&i<30;i++){
      const String id=WiFi.SSID(i);
      if(!id.length())continue;
      // One row per network name, the strongest one.
      bool dup=false;for(int16_t j=0;j<i;j++)if(WiFi.SSID(j)==id&&WiFi.RSSI(j)>=WiFi.RSSI(i)){dup=true;break;}
      if(dup)continue;
      if(!first)s+=',';
      first=false;
      s+="{\"s\":";jsonString(s,id);
      s+=",\"r\":";s+=WiFi.RSSI(i);
      s+=",\"lock\":";s+=WiFi.encryptionType(i)==WIFI_AUTH_OPEN?"false":"true";
      s+=",\"ch\":";s+=WiFi.channel(i);
      s+='}';
    }
  }
  s+="]}";
  sendJSON(200,s);
}
void handleWifiConnect(){
  String ssid=web.arg("ssid");ssid.trim();
  if(!ssid.length()||ssid.length()>32){reply(false,"Choose a network",400);return;}
  const String pass=web.arg("pass");
  if(pass.length()>63){reply(false,"Password too long",400);return;}
  cfg.router=true;cfg.ssid=ssid;cfg.wifiPassword=pass;cfg.completed=true;
  saveSettings();
  staBssidKnown=false;
  if(!scanBusy&&scanCount>0){
    int best=-1;
    for(int16_t i=0;i<scanCount;i++)if(WiFi.SSID(i)==ssid&&(best<0||WiFi.RSSI(i)>WiFi.RSSI(best)))best=i;
    if(best>=0){staChannel=(uint8_t)WiFi.channel(best);memcpy(staBssid,WiFi.BSSID(best),6);staBssidKnown=true;}
  }
  const bool move=staBssidKnown&&staChannel!=apChannelNow;
  String m="Connecting to "+ssid+".";
  if(move)m+=" EngineLink Wi-Fi moves to channel "+String(staChannel)+" to match the router, so your phone drops for a few seconds and joins again.";
  reply(true,m);
  pendingNetwork=true;
}
void handleWifiForget(){
  cfg.router=false;cfg.ssid="";cfg.wifiPassword="";saveSettings();
  esp_wifi_disconnect();
  reply(true,"Disconnected from the router");
  pendingNetwork=true;
}
void handleSeasonReset(){
  totals.seasonStartUtc=validWall()?(int64_t)time(nullptr):0;
  memset(totals.bucketSec,0,sizeof(totals.bucketSec));memset(totals.bucketL,0,sizeof(totals.bucketL));
  memset(totals.bucketSogSec,0,sizeof(totals.bucketSogSec));memset(totals.bucketSogL,0,sizeof(totals.bucketSogL));
  memset(totals.bucketNm,0,sizeof(totals.bucketNm));
  memset(totals.zoneL,0,sizeof(totals.zoneL));memset(totals.zoneSec,0,sizeof(totals.zoneSec));memset(totals.zoneNm,0,sizeof(totals.zoneNm));
  saveTotals();
  reply(true,"A new season has started");
}

// ---------- Diagnostics ----------
void handleDiag(){
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
  s+=",\"staConnects\":";s+=staConnects;s+=",\"ch\":";s+=apChannelNow;
  s+=",\"conflict\":";s+=staConflict?"true":"false";
  s+=",\"apStops\":";s+=(unsigned long)apStops;s+=",\"chMoves\":";s+=apChannelChanges;
  s+=",\"httpServed\":";s+=web.served;s+=",\"httpReused\":";s+=web.reused;
  s+=",\"httpEvicted\":";s+=web.evicted;s+=",\"httpSlowMs\":";s+=web.slowestMs;
  s+=",\"httpOpen\":";s+=web.openSockets();s+=",\"loopMaxMs\":";s+=loopMaxMs;
  s+="},\"heap\":";s+=ESP.getFreeHeap();s+=",\"minHeap\":";s+=ESP.getMinFreeHeap();
  s+=",\"reset\":";s+=(unsigned)esp_reset_reason();
  s+=",\"up\":";s+=(unsigned long)(now/1000ULL);
  s+='}';
  sendJSON(200,s);
}
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

// ---------- SD card: info, wipe, clean-up, export ----------
bool isRawName(const String &n){return n.startsWith("EL")||n.startsWith("TEST")||(n.startsWith("T")&&n.endsWith(".CSV")&&n!="TRIPS.CSV");}
SdCounts sdCounts(){
  SdCounts k;
  if(!sdReady)return k;
  File root=SD.open("/");
  if(!root)return k;
  for(File f=root.openNextFile();f;f=root.openNextFile()){
    String n=f.name();
    if(n.startsWith("/"))n=n.substring(1);
    ++k.files;
    if(!f.isDirectory()&&isRawName(n)){k.rawBytes+=f.size();++k.rawFiles;}
    else if(!f.isDirectory()&&n.startsWith("EXPORT"))++k.exports;
    f.close();
  }
  root.close();
  return k;
}
void handleSdInfo(){
  const SdCounts k=sdCounts();
  String s="{\"ok\":";s+=sdReady?"true":"false";
  s+=",\"inserted\":";s+=cardInserted()?"true":"false";
  s+=",\"logging\":";s+=sdLogging?"true":"false";
  s+=",\"usedMB\":";s+=(unsigned long)sdUsedMB();s+=",\"totalMB\":";s+=(unsigned long)sdTotalMB();
  s+=",\"files\":";s+=k.files;s+=",\"rawMB\":";jnum(s,k.rawBytes/1048576.0,1);s+=",\"rawFiles\":";s+=k.rawFiles;
  s+=",\"exports\":";s+=k.exports;
  s+=",\"trips\":";s+=tripCount;s+=",\"fuel\":";s+=fuelCount;
  s+=",\"status\":";jsonString(s,sdMessage);
  s+='}';
  sendJSON(200,s);
}
uint32_t removeTree(const String &path){
  uint32_t n=0;
  File dir=SD.open(path);
  if(!dir)return 0;
  if(!dir.isDirectory()){dir.close();return SD.remove(path)?1:0;}
  for(File f=dir.openNextFile();f;f=dir.openNextFile()){
    String child=String(f.path());
    const bool sub=f.isDirectory();
    f.close();
    if(sub){n+=removeTree(child);SD.rmdir(child);}else if(SD.remove(child))++n;
  }
  dir.close();
  return n;
}
void handleSdWipe(){
  if(web.arg("confirm")!="WIPE"){reply(false,"Type WIPE to confirm",400);return;}
  if(!sdReady||!cardInserted()){reply(false,"No SD card",409);return;}
  if(calActive)finishCalibration(false);
  const bool wasLogging=sdLogging;
  if(sdLogging)stopLog();
  if(tripSeries)tripSeries.close();
  const uint32_t n=removeTree("/");
  if(totals.tripActive)tripSeriesOpen();
  refreshTestAvailable();
  if(wasLogging)initSD();
  reply(true,String("SD card wiped: ")+n+" files deleted");
}
// Deletes raw logs older than the chosen number of days (needs the clock).
void serviceRawCleanup(){
  if(!cfg.rawKeepDays||!sdReady||!validWall()||calActive||otaActive)return;
  if(lastRawCleanup&&(uint32_t)(millis()-lastRawCleanup)<86400000UL)return;
  if(!lastRawCleanup&&millis()<120000)return;
  lastRawCleanup=millis();
  const time_t limit=time(nullptr)-(time_t)cfg.rawKeepDays*86400;
  File root=SD.open("/");
  if(!root)return;
  String doomed[16];uint8_t n=0;
  for(File f=root.openNextFile();f&&n<16;f=root.openNextFile()){
    String name=f.name();if(name.startsWith("/"))name=name.substring(1);
    const time_t w=f.getLastWrite();
    if(!f.isDirectory()&&isRawName(name)&&w>1700000000&&w<limit&&("/"+name)!=sessionName&&("/"+name)!=calName)doomed[n++]="/"+name;
    f.close();
  }
  root.close();
  for(uint8_t i=0;i<n;i++)SD.remove(doomed[i]);
}
void handleExportTrips(){
  uint32_t ids[MAX_TRIPS];uint8_t n;
  if(!parseIds(web.arg("ids"),ids,n)){web.send(400,"text/plain","No trips selected");return;}
  const int pi=portIndex();
  String s="trip_id,group,start_utc,end_utc,running_s,port_l,starboard_l,total_l,max_rpm,people,sea,wind,extras,note\n";
  for(uint8_t j=0;j<n;j++){
    const TripRec *r=tripById(ids[j]);
    if(!r)continue;
    const double a=tripLitres(*r,pi==1?1:0),b=tripLitres(*r,pi==1?0:1);
    char line[200];
    snprintf(line,sizeof(line),"%lu,%lu,%lld,%lld,%lu,%.2f,%.2f,%.2f,%u,%u,%u,%u,%u,",(unsigned long)r->id,(unsigned long)r->group,
      (long long)r->startUtc,(long long)r->endUtc,(unsigned long)r->anySec,a,b,a+b,(unsigned)r->maxRpm,
      (unsigned)r->people,(unsigned)r->sea,(unsigned)r->wind,(unsigned)r->extras);
    s+=line;
    String note=String(r->note);note.replace("\"","'");
    s+='"';s+=note;s+="\"\n";
  }
  web.sendHeader("Content-Disposition","attachment; filename=EngineLink-trips.csv");
  web.send(200,"text/csv",s);
}
// Uptime (ms) of the raw row that starts at or after pos; false at the end.
bool rowAt(File &f,uint32_t pos,uint64_t &ms,uint32_t &lineStart){
  f.seek(pos);
  if(pos){int c;do{c=f.read();}while(c>=0&&c!='\n');}
  lineStart=f.position();
  char buf[64];uint8_t i=0;int c;uint8_t field=0;
  while((c=f.read())>=0&&c!='\n'){
    if(c==','){if(++field==2)break;i=0;continue;}
    if(field==1&&i<sizeof(buf)-1)buf[i++]=(char)c;
  }
  if(c<0&&field<2)return false;
  buf[i]=0;ms=strtoull(buf,nullptr,10);
  return true;
}
// Raw engine rows of the chosen trips, streamed from the session files.
void handleExportRaw(){
  uint32_t ids[MAX_TRIPS];uint8_t n;
  if(!parseIds(web.arg("ids"),ids,n)){web.send(400,"text/plain","No trips selected");return;}
  if(!sdReady){web.send(409,"text/plain","No SD card");return;}
  if(sdLogging)logFile.flush();
  web.sendHeader("Content-Disposition","attachment; filename=EngineLink-raw-trips.csv");
  web.setContentLength(CONTENT_LENGTH_UNKNOWN);
  web.send(200,"text/csv","");
  web.sendContent("trip_id,boot_id,uptime_ms,utc_epoch,channel,mid,checksum,raw_hex,engine,fuel_lph,rpm,direction,kad_index,kad_value\n");
  char line[400];
  for(uint8_t j=0;j<n;j++){
    const TripRec *r=tripById(ids[j]);
    if(!r)continue;
    char name[25];snprintf(name,sizeof(name),"/EL%08lX.CSV",(unsigned long)r->bootId);
    File f=SD.open(name,FILE_READ);
    if(!f)continue;
    const uint64_t from=(uint64_t)r->startSec*1000ULL,to=(uint64_t)r->endSec*1000ULL+15000ULL;
    // Rows are in time order: binary search for the first row of the trip.
    uint32_t lo=0,hi=f.size();
    while(hi-lo>8192){
      const uint32_t mid=lo+(hi-lo)/2;uint64_t ms;uint32_t ls;
      if(!rowAt(f,mid,ms,ls)||ms>=from)hi=mid;else lo=mid;
    }
    f.seek(lo);
    {int c;if(lo==0){do{c=f.read();}while(c>=0&&c!='\n');}else{do{c=f.read();}while(c>=0&&c!='\n');}}
    char prefix[16];const int pl=snprintf(prefix,sizeof(prefix),"%lu,",(unsigned long)r->id);
    while(f.available()){
      const size_t len=f.readBytesUntil('\n',line,sizeof(line)-1);
      if(!len)continue;
      line[len]=0;
      const char *c1=strchr(line,',');if(!c1)continue;
      const uint64_t ms=strtoull(c1+1,nullptr,10);
      if(ms<from)continue;
      if(ms>to)break;
      web.sendContent(prefix,pl);
      web.sendContent(line,len);
      web.sendContent("\n",1);
    }
    f.close();
  }
  web.sendContent("");
}

// ---------- Backup ----------
void handleBackup(){
  String s="{\"app\":\"EngineLink\",\"version\":\"" EL_VERSION "\",\"settings\":{";
  s+="\"tank\":";jnum(s,cfg.tankL,0);s+=",\"reserve\":";s+=cfg.reserve;s+=",\"price\":";jnum(s,cfg.defaultPrice,2);
  s+=",\"tripEnd\":";s+=cfg.tripEndMin;s+=",\"autoPoll\":";s+=cfg.autoPoll?1:0;s+=",\"rawKeep\":";s+=cfg.rawKeepDays;
  s+=",\"ntp\":";s+=cfg.ntpOn?1:0;s+=",\"ntpServer\":";jsonString(s,cfg.ntpServer);
  s+=",\"host\":";jsonString(s,cfg.hostName);
  s+=",\"port\":";s+=cfg.portChannel;s+=",\"protocol\":";s+=cfg.protocol==PROTO_KAD?"\"kad\"":"\"j1587\"";
  s+=",\"fuelDiv\":";s+=cfg.kadFuelDiv;s+=",\"volt\":";s+=cfg.kadVolt;
  s+=",\"corr0\":";jnum(s,cfg.fuelCorr[0],4);s+=",\"corr1\":";jnum(s,cfg.fuelCorr[1],4);
  s+="},\"prefs\":";s+=cfg.prefs;s+='}';
  web.sendHeader("Content-Disposition","attachment; filename=EngineLink-settings.json");
  sendJSON(200,s);
}

// ---------- Firmware from GitHub ----------
// The worker task fetches the release list or downloads and installs one
// update file. The new image must confirm itself after a healthy minute,
// otherwise the bootloader goes back to the previous version.
void fwWorker(void *){
  for(;;){
    if(fwJob==FW_IDLE){vTaskDelay(pdMS_TO_TICKS(200));continue;}
    const uint8_t job=fwJob;
    NetworkClientSecure client;
    client.useBuiltinCACertBundle();
    HTTPClient http;
    http.setTimeout(15000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setUserAgent("EngineLink/" EL_VERSION);
    if(job==FW_LIST){
      fwState=1;
      if(!http.begin(client,EL_RELEASES_URL)){fwError="Could not start the request";fwState=9;}
      else{
        const int code=http.GET();
        if(code==200&&http.getSize()<16000){String body=http.getString();fwList=body;fwState=2;}
        else{fwError=String("GitHub answered ")+code;fwState=9;}
        http.end();
      }
    }else if(job==FW_INSTALL){
      fwState=3;fwBytes=0;fwTotal=0;
      if(!http.begin(client,fwUrl)){fwError="Could not start the download";fwState=9;}
      else{
        const int code=http.GET();
        const int len=http.getSize();
        if(code!=200||len<100000||len>3200000){fwError=String("Download failed (")+code+")";fwState=9;}
        else{
          fwTotal=len;
          NetworkClient *stream=http.getStreamPtr();
          bool ok=Update.begin(len,U_FLASH);
          uint8_t buf[2048];bool checked=false;uint32_t idle=millis();
          while(ok&&fwBytes<(size_t)len){
            const size_t avail=stream->available();
            if(!avail){if(millis()-idle>20000){fwError="Download stalled";ok=false;break;}vTaskDelay(pdMS_TO_TICKS(5));continue;}
            const int got=stream->readBytes(buf,min(avail,sizeof(buf)));
            if(got<=0)continue;
            idle=millis();
            if(!checked){
              checked=true;
              if(got<0x30||buf[0]!=0xE9||buf[0x20]!=0x32||buf[0x21]!=0x54||buf[0x22]!=0xCD||buf[0x23]!=0xAB){fwError="Not an EngineLink update file";ok=false;break;}
            }
            if(Update.write(buf,got)!=(size_t)got){fwError=Update.errorString();ok=false;break;}
            fwBytes+=got;
          }
          if(ok&&!Update.end(true)){fwError=Update.errorString();ok=false;}
          if(!ok){Update.abort();fwState=9;}else fwState=4;
        }
        http.end();
      }
    }
    fwJob=FW_IDLE;
  }
}
void handleFwCheck(){
  if(!routerConfigured()||WiFi.status()!=WL_CONNECTED){reply(false,"Connect EngineLink to a router with internet first",409);return;}
  if(fwJob!=FW_IDLE||fwState==3){reply(false,"Busy",409);return;}
  fwError="";fwJob=FW_LIST;
  reply(true,"Checking GitHub");
}
void handleFwInstall(){
  const String url=web.arg("url"),v=web.arg("v");
  if(!url.startsWith(EL_RELEASE_PREFIX)||!url.endsWith(".bin")){reply(false,"Only EngineLink releases from GitHub can be installed",400);return;}
  if(!routerConfigured()||WiFi.status()!=WL_CONNECTED){reply(false,"Connect EngineLink to a router with internet first",409);return;}
  if(calActive){reply(false,"Stop the motor test first",409);return;}
  if(fwJob!=FW_IDLE||fwState==3){reply(false,"Busy",409);return;}
  for(uint8_t c=0;c<2;c++)if(kad[c].polling)stopPolling(c,"Firmware update");
  fwUrl=url;fwTarget=v;fwError="";fwJob=FW_INSTALL;
  reply(true,"Downloading "+v);
}
void handleFwStatus(){
  String s="{\"state\":";s+=(unsigned)fwState;
  s+=",\"bytes\":";s+=(unsigned long)fwBytes;s+=",\"total\":";s+=(unsigned long)fwTotal;
  s+=",\"error\":";jsonString(s,fwError);
  s+=",\"target\":";jsonString(s,fwTarget);
  s+=",\"running\":\"" EL_VERSION "\"";
  s+=",\"pending\":";s+=rollbackPending?"true":"false";
  s+=",\"online\":";s+=(routerConfigured()&&WiFi.status()==WL_CONNECTED)?"true":"false";
  s+=",\"list\":";
  if(fwState==2&&fwList.length()&&fwList[0]=='{')s+=fwList;else s+="null";
  s+='}';
  sendJSON(200,s);
}
// Sends a file from flash with a long cache time (icons, manifest).
void sendStatic(const char *type,const uint8_t *data,size_t len){
  web.sendHeader("Cache-Control","public, max-age=604800");
  web.send_P(200,type,(const char*)data,len);
}

// Closes files and stores totals before any deliberate restart.
void prepareRestart(){
  for(uint8_t c=0;c<2;c++)if(kad[c].polling)stopPolling(c,"Restart");
  if(calActive)finishCalibration(false);
  for(uint8_t c=0;c<2;c++)if(motor[c].running)closeSegment(c);
  if(sdLogging)stopLog();
  saveTotals();
}

// ---------- Wi-Fi firmware update (application image only) ----------
void handleUpdateUpload(){
  HTTPUpload &u=web.upload();
  if(u.status==UPLOAD_FILE_START){
    otaError="";otaBytes=0;otaChecked=false;
    if(calActive){otaError="Stop the motor test first";return;}
    for(uint8_t c=0;c<2;c++)if(kad[c].polling)stopPolling(c,"Firmware update");
    if(u.filename.indexOf("merged")>=0){otaError="This is the USB (merged) image. Use the Wi-Fi update file enginelink-update.bin";return;}
    otaResumeLogging=sdLogging;
    if(sdLogging)stopLog();
    otaActive=true;
    if(!Update.begin(UPDATE_SIZE_UNKNOWN,U_FLASH)){otaError=Update.errorString();otaActive=false;}
  }else if(u.status==UPLOAD_FILE_WRITE){
    if(otaError.length()||!otaActive)return;
    // An ESP32 application image carries its app descriptor magic at 0x20.
    if(!otaChecked){
      otaChecked=true;
      if(u.currentSize<0x30||u.buf[0]!=0xE9||u.buf[0x20]!=0x32||u.buf[0x21]!=0x54||u.buf[0x22]!=0xCD||u.buf[0x23]!=0xAB){
        otaError="Not an ESP32 application image";Update.abort();otaActive=false;return;
      }
    }
    if(Update.write(u.buf,u.currentSize)!=u.currentSize){otaError=Update.errorString();Update.abort();otaActive=false;return;}
    otaBytes+=u.currentSize;
  }else if(u.status==UPLOAD_FILE_END){
    if(otaError.length()||!otaActive)return;
    if(!Update.end(true))otaError=Update.errorString();
  }else if(u.status==UPLOAD_FILE_ABORTED){
    if(otaActive)Update.abort();
    otaError="Upload interrupted";
  }
}
void handleUpdateDone(){
  const bool ok=otaActive&&!otaError.length()&&!Update.hasError()&&otaBytes>0;
  otaActive=false;
  if(!ok){
    if(otaResumeLogging&&!sdLogging)initSD();
    otaResumeLogging=false;
    reply(false,"Update failed: "+(otaError.length()?otaError:String("no data received")),400);
    return;
  }
  prepareRestart();
  reply(true,"Update installed. EngineLink restarts now; reconnect in about 20 seconds.");
  restartAt=millis()+1500;
}
void initializeWeb(){
  web.on("/",HTTP_GET,[](){
    // no-cache + ETag: the phone checks for a newer app every time, but a
    // reload after a dropped connection is answered with a tiny 304.
    web.sendHeader("Cache-Control","no-cache");
    web.sendHeader("ETag","\"" EL_VERSION "-" __DATE__ " " __TIME__ "\"");
    if(web.header("If-None-Match")=="\"" EL_VERSION "-" __DATE__ " " __TIME__ "\""){web.send(304,"text/plain","");return;}
    web.sendHeader("Content-Encoding","gzip");
    web.send_P(200,"text/html; charset=utf-8",(const char*)ENGINE_UI_GZ,ENGINE_UI_GZ_LEN);
  });
  web.on("/favicon.ico",HTTP_GET,[](){web.send(204,"text/plain","");});
  web.on("/api/state",HTTP_GET,handleState);
  web.on("/api/pids",HTTP_GET,handlePids);
  web.on("/api/history",HTTP_GET,handleHistory);
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
  web.on("/api/segments.csv",HTTP_GET,handleSegmentsDownload);
  web.on("/api/trip/reset",HTTP_POST,[](){resetTrip();reply(true,"Trip counter reset. Lifetime totals are kept.");});
  web.on("/api/reboot",HTTP_POST,[](){prepareRestart();reply(true,"EngineLink restarts now.");restartAt=millis()+1000;});
  web.on("/api/factory-reset",HTTP_POST,[](){
    if(web.arg("confirm")!="ERASE"){reply(false,"Confirmation missing",400);return;}
    const bool keep=web.arg("keep")=="1";
    prepareRestart();
    pref.begin("enginelink",false);pref.clear();pref.end();
    for(const char *f:{"/prefs.json","/health.bin","/diag.bin"})LittleFS.remove(f);
    if(!keep){
      pref.begin("totals",false);pref.clear();pref.end();
      for(const char *f:{"/trips.bin","/fuel.bin"})LittleFS.remove(f);
      if(sdReady&&cardInserted()){if(sdLogging)stopLog();removeTree("/");}
    }
    reply(true,keep?"Factory settings restored. Trips and the fuel log were kept. EngineLink restarts with Wi-Fi EngineLink, password 12345678.":"Everything erased. EngineLink restarts with Wi-Fi EngineLink, password 12345678.");
    restartAt=millis()+1200;
  });
  web.on("/api/update",HTTP_POST,handleUpdateDone,handleUpdateUpload);
  web.on("/api/kad/poll",HTTP_POST,handleKadPoll);
  web.on("/api/kad/config",HTTP_POST,handleKadConfig);
  web.on("/api/health",HTTP_GET,[](){web.send(200,"text/plain","OK");});
  web.on("/api/live",HTTP_GET,handleLive);
  web.on("/api/trips",HTTP_GET,handleTrips);
  web.on("/api/trip/note",HTTP_POST,handleTripNote);
  web.on("/api/trip/combine",HTTP_POST,handleTripCombine);
  web.on("/api/trip/split",HTTP_POST,handleTripSplit);
  web.on("/api/fuel",HTTP_GET,handleFuelList);
  web.on("/api/fuel/add",HTTP_POST,handleFuelAdd);
  web.on("/api/fuel/delete",HTTP_POST,handleFuelDelete);
  web.on("/api/fuel/edit",HTTP_POST,handleFuelEdit);
  web.on("/api/economy",HTTP_GET,handleEconomy);
  web.on("/api/season/reset",HTTP_POST,handleSeasonReset);
  web.on("/api/prefs",HTTP_GET,handlePrefsGet);
  web.on("/api/prefs",HTTP_POST,handlePrefsSet);
  web.on("/api/cfg",HTTP_POST,handleCfg);
  web.on("/api/wifi/ap",HTTP_POST,handleApSave);
  web.on("/api/wifi/scan",HTTP_GET,handleWifiScan);
  web.on("/api/wifi/connect",HTTP_POST,handleWifiConnect);
  web.on("/api/wifi/forget",HTTP_POST,handleWifiForget);
  web.on("/api/diag",HTTP_GET,handleDiag);
  web.on("/api/diag/test",HTTP_POST,handleDiagTest);
  web.on("/api/diag/focus",HTTP_POST,handleDiagFocus);
  web.on("/api/kad/discover",HTTP_POST,handleKadDiscover);
  web.on("/api/events",HTTP_GET,handleEvents);
  web.on("/api/events/ack",HTTP_POST,handleEventsAck);
  web.on("/api/mark",HTTP_POST,handleMark);
  web.on("/api/event/series",HTTP_GET,handleEventSeries);
  web.on("/api/trip/series",HTTP_GET,handleTripSeries);
  web.on("/api/series/recent",HTTP_GET,handleRecentSeries);
  web.on("/api/export/series.csv",HTTP_GET,handleExportSeries);
  web.on("/api/life",HTTP_GET,handleLife);
  web.on("/api/led",HTTP_GET,handleLed);
  web.on("/api/sd/info",HTTP_GET,handleSdInfo);
  web.on("/api/sd/wipe",HTTP_POST,handleSdWipe);
  web.on("/api/export/trips.csv",HTTP_GET,handleExportTrips);
  web.on("/api/export/raw.csv",HTTP_GET,handleExportRaw);
  web.on("/api/backup",HTTP_GET,handleBackup);
  web.on("/api/fw/check",HTTP_POST,handleFwCheck);
  web.on("/api/fw/install",HTTP_POST,handleFwInstall);
  web.on("/api/fw/status",HTTP_GET,handleFwStatus);
  web.on("/icon.svg",HTTP_GET,[](){sendStatic("image/svg+xml",(const uint8_t*)ICON_SVG,strlen_P(ICON_SVG));});
  web.on("/apple-touch-icon.png",HTTP_GET,[](){sendStatic("image/png",ICON_PNG_180,ICON_PNG_180_LEN);});
  web.on("/icon-192.png",HTTP_GET,[](){sendStatic("image/png",ICON_PNG_192,ICON_PNG_192_LEN);});
  web.on("/icon-512.png",HTTP_GET,[](){sendStatic("image/png",ICON_PNG_512,ICON_PNG_512_LEN);});
  web.on("/manifest.webmanifest",HTTP_GET,[](){
    web.sendHeader("Cache-Control","no-cache");
    web.send(200,"application/manifest+json","{\"name\":\"Engine Link\",\"short_name\":\"Engine Link\",\"start_url\":\"/\",\"scope\":\"/\",\"display\":\"standalone\",\"background_color\":\"#0a1420\",\"theme_color\":\"#0a1420\",\"icons\":[{\"src\":\"/icon-192.png\",\"sizes\":\"192x192\",\"type\":\"image/png\"},{\"src\":\"/icon-512.png\",\"sizes\":\"512x512\",\"type\":\"image/png\",\"purpose\":\"any maskable\"}]}");
  });
  web.onNotFound([](){web.send(404,"text/plain","Not found");});
  const char *keep[]={"If-None-Match","Connection"};
  web.collectHeaders(keep,2);
  web.enableCORS(true); // the app can also reach EngineLink through the boat router
  web.begin();
}
void serialStatus(){
  if((uint32_t)(millis()-lastSerialStatus)<60000UL)return;
  lastSerialStatus=millis();
  Serial.printf("[%lus] J7 %lu/%lu  J6 %lu/%lu frames/invalid  drops %lu  heap %u  AP clients %u  SD %s\n",
    (unsigned long)(elapsedMs()/1000ULL),(unsigned long)ctr.frames[0],(unsigned long)ctr.invalid[0],
    (unsigned long)ctr.frames[1],(unsigned long)ctr.invalid[1],
    (unsigned long)(ctr.rxDropped[0]+ctr.rxDropped[1]),(unsigned)ESP.getFreeHeap(),
    (unsigned)WiFi.softAPgetStationNum(),sdMessage.c_str());
}
void setup(){
  pinMode(LED_PIN,OUTPUT);digitalWrite(LED_PIN,LOW);
  led.begin();led.setBrightness(ledBrightness);led.clear();led.show();
  Serial.begin(115200);
  delay(1200); // do not block boot for USB Serial Monitor
  Serial.println("\nEngineLink " EL_VERSION " - engine monitoring, listen-only at boot unless automatic reading is on");
  {
    esp_ota_img_states_t st;
    const esp_partition_t *run=esp_ota_get_running_partition();
    rollbackPending=run&&esp_ota_get_state_partition(run,&st)==ESP_OK&&st==ESP_OTA_IMG_PENDING_VERIFY;
  }
  bootId=esp_random();
  sketchKB=ESP.getSketchSize()/1024;
  pinMode(SD_DETECT,INPUT);
  pinMode(OPTO_PIN,INPUT);
  loadSettings();
  loadTotals();
  fsReady=LittleFS.begin(true,"/littlefs",8,"spiffs");
  if(!fsReady)Serial.println("Internal storage (LittleFS) could not start; trips are kept on the SD card only");
  loadPrefs();
  loadTrips();
  loadFuel();
  recalibrate();
  loadHealth();
  loadEvents();
  loadLife();
  ring=(Sample*)ps_malloc(sizeof(Sample)*RING_N);
  loadDiag();
  lastEngineOnMs=millis();
  // A trip that was still open when power went is closed with its last values.
  if(totals.tripActive&&totals.tripBoot!=bootId)endTrip();
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
  xTaskCreatePinnedToCore(fwWorker,"fw",12288,nullptr,1,nullptr,0);
  Serial.printf("SoftAP EngineLink at http://%s (http://enginelink.local) ; passive J1708 only\n",WiFi.softAPIP().toString().c_str());
}
void loop(){
  const uint32_t loopStart=millis();
  // Give the UART data priority over serving the user interface.
  Frame f;
  int handled=0;
  while(framesQueue&&handled<32&&xQueueReceive(framesQueue,&f,0)==pdTRUE){processFrame(f);handled++;}
  serviceCAN();
  web.handleClient();
  serviceSD();
  serviceKad();
  serviceAutoPoll();
  serviceDiag();
  serviceEngines();
  serviceRawCleanup();
  serviceHistory();
  serviceNetwork();
  updateLED();
  serialStatus();
  if(rollbackPending&&millis()>60000&&apStarted){esp_ota_mark_app_valid_cancel_rollback();rollbackPending=false;}
  if(fwState==4&&!restartAt){prepareRestart();restartAt=millis()+2000;}
  if(restartAt&&(int32_t)(millis()-restartAt)>=0){delay(100);ESP.restart();}
  const uint32_t took=millis()-loopStart;if(took>loopMaxMs)loopMaxMs=took;
  delay(1);
}
