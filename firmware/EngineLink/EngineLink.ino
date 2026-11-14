/*
 * EngineLink — application firmware
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
 * This file holds the libraries, global state, setup() and loop(). Types and
 * constants are in model.h; each area has its own file (engines.ino, fuel.ino,
 * trips.ino, wifi.ino, webapi.ino, ...). See HANDOVER.md for the layout.
 *
 * Read docs/STATUS.md before attaching to any engine or CAN bus.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <lwip/sockets.h>
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
#include <esp_phy_init.h>
#include "ping/ping_sock.h"
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include "ui.h"
#include "icons.h"
#include "fuelcal.h"
#include "ledpatterns.h"
#include "diagrules.h"
#include "shiftrules.h"
#include "otaheader.h"
#include "nmea2k.h"
#include "crashinfo.h"
#include "model.h"
#include "tracefold.h"
#include "httputil.h"
#include "netstat.h"
#include "httpd.h"
// Transmit power step (0 = 8.5 dBm .. 4 = 19.5 dBm). Fixed: the automatic
// choice (0.12.2-0.14.1) never took effect at start (the country setting
// reset the power to 20 dBm, field reports #95/#125/#130) and its test never
// completed in the field. The board has run well at 20 dBm. wifi.ino sets it
// after the country and checks that the driver really uses it.
constexpr uint8_t TX_LEVEL_FIXED=4;
uint8_t txLevel=TX_LEVEL_FIXED;
uint32_t txReapplied=0;   // the driver had another power and it was set again
// Transmit power steps, lowest to highest (index = level); the AP uses 4.
const wifi_power_t elTxPower[5]={WIFI_POWER_8_5dBm,WIFI_POWER_11dBm,WIFI_POWER_13dBm,WIFI_POWER_15dBm,WIFI_POWER_19_5dBm};
const char *const elTxName[5]={"very low (8.5 dBm)","low (11 dBm)","moderate (13 dBm)","medium (15 dBm)","high (19.5 dBm)"};
SpeedLogRec speedLog[SPEED_LOG_N];uint8_t speedLogCount=0;
// The chip's own temperature (ESP32-S3 internal sensor), read every 5 s in the
// main loop (0.17.4): the same firmware served 54-121 kB/s a minute after a
// cold power-on and 200-480 kB/s four minutes later, so the report now says
// how warm the chip was at every speed test and in every 30 s of a router session.
volatile int8_t chipTempC=0,chipTempStartC=0,chipTempMaxC=-128;

#define EL_VERSION "0.17.5"
// Releases list published by the Pages workflow; firmware files come from
// GitHub Releases of the same repository. Nothing else is downloaded.
#define EL_RELEASES_URL "https://kaelor9.github.io/EngineLink/releases.json"
#define EL_RELEASE_PREFIX "https://github.com/Kaelor9/EngineLink/releases/download/"

// ---------- Engine task and its lock ----------
// Reading the engines runs in its own task (engineTask, on core 0), so the
// web server, Wi-Fi and SD work can no longer hold it up. They share the
// engine data under one lock: the main loop holds it while it works and lets
// go once per pass; the web server (httpd.ino, its own task) takes it for a
// handler and lets go whenever it waits for the network.
SemaphoreHandle_t engineMx=nullptr;
bool engineTaskOn=false;
// How the engine task is doing: when it last finished a round, since when it
// waits for the lock, the longest wait and how often it waited over 100 ms.
volatile uint32_t engineBeat=0,engineWaitSince=0;
uint32_t engineMaxWaitMs=0,engineSlowWaits=0;
char engineMaxWaitBy[16]="";   // the task holding the lock when the longest wait began
uint32_t engineMaxRoundMs=0;    // longest round of the engine task's own work (raw SD writes included)
// Macros, not functions: Arduino puts its prototypes before the first function.
// The loop holds engineMx whenever it exists (also without the engine task),
// so web handlers in the server task never run beside the loop's work.
#define netWaitBegin() do{if(engineMx)xSemaphoreGive(engineMx);}while(0)
#define netWaitEnd() do{if(engineMx)xSemaphoreTake(engineMx,portMAX_DELAY);}while(0)
// SD card work without the engine lock (sdGapBegin/End in storage.ino): how
// many tasks are inside the card right now with the lock let go. Changed only
// while holding engineMx; nothing unmounts the card while it is not 0.
volatile uint8_t sdOffLock=0;

Web web;  // esp_http_server adapter (httpd.h, httpd.ino)
DlStat dlStat,spStat;  // last export; last speed test
Coverage tripCov,bootCov;
const Coverage *seriesCov=nullptr;  // added to the next series reply
// One streamed download at a time (the xfer task, httpd.ino): rows are
// gathered into 8 kB pieces and each piece is one chunk, sent uncompressed
// (plan D26).
struct ChunkOut {
  char b[8192];size_t n=0;
  ChunkOut &add(const void *s,size_t len){
    const char *p=(const char*)s;
    while(len){
      if(n==sizeof(b))flush();
      const size_t k=min(len,sizeof(b)-n);
      memcpy(b+n,p,k);n+=k;p+=k;len-=k;
    }
    return *this;
  }
  ChunkOut &add(const char *s){return add(s,strlen(s));}
  ChunkOut &add(const String &s){return add(s.c_str(),s.length());}
  void flush(){
    if(!n)return;
    dlStat.raw+=n;
    send(b,n);
    n=0;
  }
  void finish(){
    flush();
    web.sendContent("");
  }
  void send(const char *s,size_t len){
    if(web.sendFailed())return;  // the phone went away: the rest is not sent
    const uint32_t t0=millis();web.sendContent(s,len);dlStat.netMs+=millis()-t0;dlStat.bytes+=len;
  }
};
// Functions with default arguments need their declaration written out:
// Arduino does not generate prototypes for them.
void traceWifi(uint8_t type,uint8_t reason=0);
static ChunkOut chunkOut;
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
uint32_t lastSdRetry=0,sdRetries=0,sdRecoveries=0;
bool uartReady[2]={false,false};
bool sdReady=false,sdLogging=false,canReady=false,pendingNetwork=false,mdnsReady=false;
uint32_t networkSwitchAt=0;
// Wi-Fi events run on a separate task. They only request a deferred mDNS
// refresh; the main loop performs the potentially blocking restart.
volatile bool mdnsRefreshPending=false;
volatile uint32_t mdnsNetworkChangedAt=0;
uint32_t mdnsLastAttemptAt=0,mdnsRestarts=0,mdnsFailures=0;
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
volatile uint32_t apJoins=0,apLeaves=0,apDhcpLeases=0,apStarts=0,apStops=0,staDisconnects=0;
volatile uint8_t apReason=0;
float sogKnots=NAN;
uint64_t sogAt=0;
volatile bool ntpNotification=false;
uint32_t loopMaxMs=0;
// Longest time of each part of loop() (the loop holds the engine lock, so
// this is also how long engine reading can wait). Names in netreport.ino.
constexpr uint8_t LOOP_PARTS=9;
uint32_t loopPartMaxMs[LOOP_PARTS]={};
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
// NMEA 2000 (nmea2000.ino): live navigation values, fast packets, the bus census
// and the per-trip navigation log.
n2k::Nav nav;
n2k::FastPool navFast;
n2k::Census navSeen;
File tripNav;
File navCap;  // bus recording (/NMEA/BUS.CSV), see nmea2000.ino
uint32_t navCapUntil=0,navCapFrames=0,navCapBytes=0,navCapStart=0;
uint32_t tripNavId=0,tripNavFlush=0,navFrames=0,navErrFrames=0;
const char *const NAV_COLS="lat,lon,sog_kn,cog_deg,heading_deg,stw_kn,depth_m,water_c,rudder_deg,battery_v,wind_kn,wind_deg,tank1_pct,tank2_pct,satellites";
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
// ---- 0.14.0: gear and clutch, four readings a second (gearbox.ino) ----
FastSample *fastRing=nullptr;
uint16_t fastHead=0,fastCount=0;
uint32_t lastFastMs=0;
shift::Tracker shiftTr[2];
shift::Accel accelTr[2];
ShiftLife shiftLife;
bool shiftLifeDirty=false;
ShiftRec shiftLog[SHIFT_N];
uint8_t shiftCount=0;              // shiftLog[0] is the newest
bool shiftLogDirty=false;
uint32_t shiftLogSavedAt=0;
File tripFast;                     // the running trip, four readings a second
uint32_t tripFastId=0;
FastSample fastBuf[40];uint8_t fastBufN=0;
uint32_t pendingFastBox[8];uint8_t pendingFastBoxN=0;
uint32_t lastPullEventMs[2]={0,0},lastHardEventMs[2]={0,0};
uint8_t kadManPos[2]={0,0};        // slow-slot pattern while manoeuvring
uint16_t kadFocusIndex2=0;         // a second value studied with kadFocusIndex (the drive test)
bool warmTiming[2]={false,false};uint32_t warmStartUp[2]={0,0};
// Per-trip sums for the health comparison: [engine][bucket][metric] and samples.
float tripHealthSum[2][RPM_BUCKETS][HEALTH_METRICS];
uint16_t tripHealthN[2][RPM_BUCKETS];
DiagTest diagTests[2];
bool ecuHoursChecked[2]={false,false};
int64_t lastMissedUtc=0; float lastMissedL=0;
// Wi-Fi: own access point or router, never both (wifi.ino).
uint8_t apChannelNow=6;          // channel the AP actually uses
// Recovery, not a mode: the router was not reached for 2 minutes, so
// EngineLink's own Wi-Fi is on until the router is tried again, every
// 10 minutes while no phone is joined or at once when the app asks. Not kept
// over a restart: each start in router mode tries the router first.
bool routerHold=false;
uint32_t routerFallbacks=0,routerRetryAt=0;
// The application never calls begin()/disconnect() while authentication runs;
// it only starts a new attempt after the driver reported the last one over
// (staTryAt: when the last attempt started; serviceNetwork).
uint32_t staTryAt=0;
uint32_t staFails=0,staConnects=0,apRestarts=0;
uint32_t routerAttempts=0;
volatile uint8_t staLastReason=0;
volatile uint32_t staLastDropAt=0;
volatile bool staAssociated=false;
uint32_t staDownSince=0;
// Only the driver event callback writes these measurements. The main loop
// reads snapshots and performs network operations outside the callback.
volatile uint32_t staBeginAt=0,staAssociatedAt=0,staIpAt=0,staIpLostAt=0;
volatile uint32_t staAssociatedEvents=0,staIpEvents=0,staIpLostEvents=0;
volatile uint32_t staJoinMs=0,staDhcpMs=0;
volatile uint8_t staLastUnexpectedReason=0;
volatile uint32_t staUnexpectedDisconnects=0;
// Count failed association attempts separately from dropped live connections.
volatile uint32_t staAssociationFailures=0;
volatile bool staHadIp=false;
// The browser's 32-KiB speed measurement includes HTTP setup. Track the
// worker's own TCP write duration separately to locate actual bottlenecks.
volatile uint32_t speedTxLastMs=0,speedTxLastBytes=0,speedTxLastWrites=0,speedTxLastAt=0;
RouterTrace routerTrace[ROUTER_TRACE_CAP]={};
uint8_t traceHead=0,traceSize=0;
uint32_t traceTotal=0;   // Wi-Fi events ever added; the SD log (serialcmd.ino) follows it
portMUX_TYPE traceMux=portMUX_INITIALIZER_UNLOCKED;
String lastRouterIp="";
int lastRouterRssi=0;
uint8_t lastRouterChannel=0;
// WPA3 diagnostics: report what the driver negotiated, not just what the AP advertises.
volatile uint8_t staNegotiatedAuth=255;  // 255 = no successful association this boot
volatile uint32_t networkIpEpoch=0,networkIpEventAt=0;
uint32_t netEpochSeen=0;   // networkIpEpoch already followed up (TX power, log)
// Last access point event. AP_STOP also arrives (late) when the network only
// changed its settings, so the watchdog acts on "stopped and not started again".
volatile uint8_t apLastEvent=0;      // 1 started, 2 stopped
volatile uint32_t apLastEventMs=0;
uint32_t apChannelChanges=0;
uint8_t apStartedChannel=0;
bool scanBusy=false,scanRequested=false,scanInFlight=false,scanStaTemporary=false;
int16_t scanCount=-1;
uint32_t scanStartedMs=0,scanNextAt=0,scanRestoreAt=0,scanDurationMs=0;
uint8_t scanChannel=0,scanPass=1,scanPassOneCount=0,scanFailures=0,scanStartFailures=0,scanStartRetry=0;
uint16_t scanRawCount=0;
ScanEntry scanEntries[WIFI_SCAN_MAX_RESULTS];
uint8_t scanEntriesCount=0;
uint32_t scanCompletedMs=0;
// What EngineLink measures about its networks by itself (netdiag.ino,
// netstat.h; plan section 9). Kept in RAM over network changes: the report
// can usually only be fetched later, on the own Wi-Fi.
netstat::Http httpStat;                                  // the web server by network, with a request log
portMUX_TYPE httpStatMux=portMUX_INITIALIZER_UNLOCKED;   // two server tasks write it
netstat::RouterSession rsessHist[netstat::SESSION_CAP];  // router sessions, rsessCur the newest
uint8_t rsessCur=0,rsessCount=0;
netstat::Slot slots[netstat::SLOT_CAP];                  // the newest session in 30 s pieces
uint8_t slotHead=0,slotSize=0;
netstat::Step netSteps[netstat::STEP_CAP];               // slow steps of the network part
uint8_t netStepHead=0,netStepSize=0;
// From the driver's events (data only, no Wi-Fi calls).
volatile int8_t staDropRssi=0;
volatile uint8_t staSessionEndReason=0;
volatile uint32_t staBeaconTimeouts=0,staChannelChanges=0;
// Device reports to GitHub: the board files its Wi-Fi report as an issue in
// the EngineLink repository when it has internet, so nobody has to fetch it
// by hand. Needs a token that may only create issues; the router name is
// removed (the repository is public). At most one every 30 min, 10 a day.
volatile bool reportWanted=false;bool reportManual=false,reportedThisBoot=false;
String reportReason="",reportBody="",reportError="",reportTitle="";
// Trip whose engine report still has to be sent (0 = none); kept in NVS so a
// trip that ended at sea is reported once the board is back on the router.
uint32_t engineReportTrip=0;bool engineReportNow=false;
uint32_t reportSent=0,reportLastIssue=0,reportLastAt=0,reportDayStart=0,reportDayCount=0;
volatile uint8_t fwJob=FW_IDLE;
volatile uint8_t fwState=0;      // 0 idle, 1 checking, 2 list ready, 3 downloading, 4 installed, 9 error
volatile size_t fwBytes=0,fwTotal=0;
String fwList="",fwError="",fwUrl="",fwTarget="";
bool rollbackPending=false;      // running a new image that is not confirmed yet
uint32_t lastRawCleanup=0;

void backfillTripTime();

void saveLife();
RestartRec restartLog[RESTART_LOG_N];
bool restartUtcPending=false;

void reply(bool ok,const String &message,int code=200);
void finishCalibration(bool resume);

static uint8_t rawIn[4096];

double prevFuelRaw[2]={0,0};
uint32_t gapFills[2]={0,0};
void stopPolling(uint8_t ch,const String &why);
void checkEcuHours(uint8_t ch,float hours);
int32_t kadHeld[2][32];
bool kadHeldOk[2][32];
uint32_t kadRejected[2]={0,0},kadHeldBack[2]={0,0};
uint32_t kadTransientUntil[2]={0,0};
uint16_t kadFocusIndex=0;uint32_t kadFocusUntil=0;

// ---------- Internal storage for trips, fuel log, health, tests, app prefs ----------
// LittleFS on the 1.5 MB data partition: NVS is only 20 KB and would fill up.
// Every file is written to a temporary name first and then renamed, so a
// power cut never leaves a half-written file.
bool fsReady=false;
uint32_t fsWriteErrors=0,fsRecoveredFiles=0;
char fsLastFailure[48]={};

uint8_t calState[MAX_FUEL];
float calZ[MAX_FUEL],calPump[MAX_FUEL],calCounted[MAX_FUEL],calBase[MAX_FUEL];

// ---------- Trips ----------
// A trip starts when the first engine starts and ends when both engines have
// been off for cfg.tripEndMin minutes. If EngineLink restarts during a trip,
// the open trip is closed at boot; two parts can be combined in History.
uint32_t lastEngineOnMs=0,lastEngineOnSec=0;
void finishTripHealth();


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
  // USB serial must never hold the board up. With a computer attached that
  // does not read the port, each print waited up to 2 s (20 x 100 ms in the
  // Arduino core's HWCDC.cpp): the main loop, web server and engine reading
  // stood still for up to 9.4 s (#141). Without a reader output is dropped.
  Serial.setTxTimeoutMs(0);
  // The radio calibrates once per start, when the Wi-Fi first comes up. The
  // working test sketch waited a second after power-on before that; so does
  // EngineLink (it started the Wi-Fi 120 ms after power-on until rc4).
  delay(1000);
  Serial.println("\nEngineLink " EL_VERSION " - engine monitoring, listen-only at boot unless automatic reading is on");
  {
    esp_ota_img_states_t st;
    const esp_partition_t *run=esp_ota_get_running_partition();
    rollbackPending=run&&esp_ota_get_state_partition(run,&st)==ESP_OK&&st==ESP_OTA_IMG_PENDING_VERIFY;
  }
  bootId=esp_random();
  restartLogBoot();
  sketchKB=ESP.getSketchSize()/1024;
  pinMode(SD_DETECT,INPUT);
  pinMode(OPTO_PIN,INPUT);
  loadSettings();
  // Bring up the selected Wi-Fi mode as soon as configuration is read.
  // Filesystem recovery and SD mounting can take several seconds; the
  // router association or standalone AP beaconing now proceeds in parallel.
  WiFi.onEvent(onWiFi);
  applyNetwork();
  netdiagInit();
  loadTotals();
  // Formatting an existing data partition after a transient mount failure
  // would permanently destroy history. Permit auto-format only before the
  // first successful mount, remembered independently of factory settings.
  Preferences storageMeta;
  storageMeta.begin("el_storage",false);
  // A configured 0.8.2 installation may have existing files even though it
  // has not yet written the new NVS marker. Never autoformat such a board.
  const bool previouslyMounted=cfg.completed||storageMeta.getBool("mounted",false);
  fsReady=LittleFS.begin(!previouslyMounted,"/littlefs",8,"spiffs");
  if(fsReady)storageMeta.putBool("mounted",true);
  storageMeta.end();
  if(!fsReady)Serial.println("LittleFS mount failed - storage preserved, NOT formatted");
  for(const char *p:{"/prefs.json","/trips.bin","/fuel.bin","/health.bin","/events.bin","/life.bin","/diag.bin","/shiftlife.bin","/shifts.bin"})fsRecover(p);
  loadPrefs();
  loadTrips();
  loadFuel();
  recalibrate();
  loadHealth();
  loadEvents();
  loadLife();
  ring=(Sample*)ps_malloc(sizeof(Sample)*RING_N);
  fastRing=(FastSample*)ps_malloc(sizeof(FastSample)*FAST_N);
  loadShiftLife();
  loadShifts();
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
  if(cfg.canListen && !startCAN())Serial.println("CAN listen-only init failed");
  // NTP time sync callback is non-blocking.
  sntp_set_time_sync_notification_cb(ntpCallback);
  xTaskCreatePinnedToCore(fwWorker,"fw",12288,nullptr,1,nullptr,0);
  // Watchdog for the main loop: if it hangs for a minute (it normally runs
  // every few ms), the chip restarts and the restart log says "watchdog".
  {esp_task_wdt_config_t w={60000,(1<<0),true};esp_task_wdt_reconfigure(&w);enableLoopWDT();}
  engineMx=xSemaphoreCreateMutex();
  if(engineMx){
    xSemaphoreTake(engineMx,portMAX_DELAY);  // the loop holds it while it works
    if(framesQueue)engineTaskOn=xTaskCreatePinnedToCore(engineTask,"engines",16384,nullptr,3,nullptr,0)==pdPASS;
  }
  // The web server starts last: every handler's data is loaded and the lock
  // exists. It binds every interface, so a Wi-Fi change needs no rebind.
  initializeWeb();
  Serial.printf("EngineLink HTTP %s on port 80 (%s)\n",web.handle?"ready":"FAILED to start",
    routerConfigured()&&!routerHold?"router":"standalone");
}
void loop(){
  const uint32_t loopStart=millis();
  if(!engineTaskOn){
    // Fallback without the engine task: the loop reads the engines itself.
    Frame f;
    int handled=0;
    while(framesQueue&&handled<32&&xQueueReceive(framesQueue,&f,0)==pdTRUE){processFrame(f);handled++;}
  }
  // Each part's longest time goes to the network report (loopPartMaxMs).
  uint32_t partAt=millis();
  auto part=[&partAt](uint8_t i){const uint32_t t=millis(),d=t-partAt;if(d>loopPartMaxMs[i])loopPartMaxMs[i]=d;partAt=t;};
  serviceCAN();
  part(8);
  serviceSD();
  part(1);
  if(!engineTaskOn){serviceKad();serviceAutoPoll();}
  serviceDiag();
  if(!engineTaskOn){serviceFast();serviceEngines();}
  part(2);
  serviceSdWalk();
  part(3);
  serviceHistory();
  part(4);
  serviceNetwork();
  part(5);
  updateLED();
  part(6);
  serialStatus();
  serviceSerialCommands();
  serviceWifiLog();
  part(7);
  restartLogDate();
  part(8);
  // Watchdog for the engine task: stuck for 10 s without waiting for the lock
  // means something inside it hangs. Without it nothing is read, so start over
  // (totals are saved first; automatic reading starts again by itself).
  if(engineTaskOn&&!restartAt&&engineBeat&&!engineWaitSince&&(uint32_t)(millis()-engineBeat)>10000){
    Serial.println("Engine task stalled: restarting");
    prepareRestart();restartAt=millis()+200;
  }
  {static uint32_t tAt=0;if(!tAt||(uint32_t)(millis()-tAt)>=5000UL){tAt=millis()?millis():1;const float t=temperatureRead();if(t>-40&&t<125){const int8_t c=(int8_t)lroundf(t);if(chipTempMaxC==-128)chipTempStartC=c;chipTempC=c;if(c>chipTempMaxC)chipTempMaxC=c;}}}
  serviceSelfTest();
  if(fwState==4&&!restartAt){prepareRestart();restartAt=millis()+2000;}
  if(restartAt&&(int32_t)(millis()-restartAt)>=0){delay(100);ESP.restart();}
  const uint32_t took=millis()-loopStart;if(took>loopMaxMs)loopMaxMs=took;
  netWaitBegin();delay(1);netWaitEnd();  // the engine task gets its turn
}
