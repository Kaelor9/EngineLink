// EngineLink data types and constants, shared by every part of the firmware.
// In a header (included once from EngineLink.ino after the libraries) so the
// types exist before the prototypes Arduino generates for the .ino files.
#pragma once

struct ChunkOut;   // defined in EngineLink.ino: it uses the web server

// ---------- Pins ----------
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
constexpr uint32_t TOTALS_SAVE_RUNNING_MS=30000;  // while an engine runs
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
// lwIP has 16 sockets in all (CONFIG_LWIP_MAX_SOCKETS). The listeners on
// 80/81/82, mDNS and the update client need some; 8 kept-open browser
// connections on top could use up the rest, and then a new connection is
// accepted by the stack but never answered ("server stopped responding").
constexpr uint8_t HTTP_POOL=4;
constexpr uint32_t HTTP_IDLE_MS=3000;      // silent sockets are closed after this
constexpr uint32_t HTTP_IO_TIMEOUT_MS=1000;
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
  String ghToken="";          // GitHub token for sending device reports as issues (never shown)
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
  // 0.7.7: our own recent requests, so their echo is always recognised even
  // when it is handled late; the echo of the current request must be seen
  // before a reply counts (a late reply to the previous request would
  // otherwise be filed under the wrong value).
  uint16_t sentIdx[4]={};uint64_t sentAt[4]={};uint8_t sentPos=0;
  bool echoSeen=false;uint32_t echoes=0;
  uint8_t foreignBurst=0;uint64_t foreignFirstAt=0;
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
struct DlStat {
  char what[20]="";uint32_t bytes=0,raw=0,ms=0,findMs=0,sdMs=0,netMs=0;bool ap=false,z=false;int16_t rssi=0;bool active=false;
};
// How much of the engine data was actually read: every second an engine runs
// counts as read when a fresh answer (under 2 s old) is there. A gap counts
// only when the engine ran on both sides of it, so stopping and starting is
// never counted as missing data.
struct Coverage {
  uint32_t magic=0xC0E10001;
  uint32_t good[2]={},missing[2]={},fuelGood[2]={};
  uint16_t longest[2]={},gaps[2]={};
  uint32_t lastGoodSec[2]={};float lastGoodRpm[2]={};
};
// Latest decoded value per KAD parameter (position in KAD_PARAMS), signed raw.
constexpr uint8_t KAD_N=sizeof(KAD_PARAMS)/sizeof(KAD_PARAMS[0]);
struct DiagRun { bool on=false; uint32_t startMs=0; uint32_t req0=0,rep0=0,inv0=0; uint64_t ms0=0; uint32_t cnt0=0; } diagRun[2];
// Bounded uptime-based router event log (kept during rescue).
struct RouterTrace {uint32_t ms;uint8_t type,reason;};
constexpr uint8_t ROUTER_TRACE_CAP=64;
constexpr uint32_t ROUTER_LAST_RESORT_RETRY_MS=30000UL;
// Discovery is a short-lived, explicit commissioning operation. Normal
// operation remains either AP-only (standalone) or STA-only (router).
// A channel-by-channel asynchronous scan returns to the AP channel between
// probes so browser requests can still get through on a shared radio.
constexpr uint8_t SCAN_CHANNEL_FIRST=1,SCAN_CHANNEL_LAST=13,SCAN_PASSES=2;
// First pass sends active probe requests, the second listens for beacons
// independently. Two methods reduce missed SSIDs without a blocking scan.
constexpr uint32_t SCAN_DWELL_MS=220,SCAN_PASSIVE_DWELL_MS=300;
constexpr uint32_t SCAN_GAP_MS=130,SCAN_TIMEOUT_MS=32000;
// Snapshot the scan results into our own storage. Arduino's scan buffer can be
// deleted by a separate router search or reconnect, while the phone still
// needs the results. Keep the strongest 30 UNIQUE SSIDs, not the first 30 raw
// BSSIDs (a busy marina can have many radios advertising the same SSID).
constexpr uint8_t WIFI_SCAN_MAX_RESULTS=60;
struct ScanEntry {
  String ssid;
  int32_t rssi=-127;
  uint8_t channel=0;
  bool locked=true;
  uint8_t auth=0;   // wifi_auth_mode_t
  uint8_t bssid[6]={};
};
// Firmware from GitHub, done by a worker task so the app keeps working.
enum FwJob : uint8_t { FW_IDLE, FW_LIST, FW_INSTALL, FW_REPORT };
// Restart log: why EngineLink started, how long it had run before, and the
// version. "alive" is the uptime at the last save, so after a power cut it
// tells roughly how long the board had been on.
struct RestartRec{uint32_t utc=0,upBefore=0;uint8_t reason=0;char ver[11]="";};
constexpr uint8_t RESTART_LOG_N=8;
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
// Files from 0.6.0 on start with a small header (magic, record size), so a
// later layout can still read them. 0.5.x files have no header.
struct FileHead { uint32_t magic; uint16_t recSize; uint16_t count; };
constexpr uint32_t TRIPS_MAGIC=0x32505254, FUEL_MAGIC=0x324C4546; // "TRP2", "FEL2"
// ---------- Fuel log and calibration ----------
// Every full tank after an earlier full tank is one calibration equation
// (fuelcal.h): the pump litres of the full tank and of any partial fill-ups
// in between, against what EngineLink counted in that time, split by
// engine-speed band. The calibration is solved again from the whole log after
// every change, so deleting or correcting an entry also undoes its effect.
enum CalState : uint8_t { CAL_NONE=0, CAL_USED=1, CAL_ODD=2, CAL_OFF=3, CAL_SMALL=4, CAL_FIRST=5 };
constexpr uint32_t LED_DEMO_STEP_MS=3000;
// Standard J1587 parameters shown on the overview when an ECU sends them.
// Scaling is applied in the UI and is labelled as unverified for the ECU.
constexpr uint8_t OVERVIEW_PIDS[]={91,92,94,96,100,102,105,110,168,174,175,247,250};

// Download tests measured by the phone (newest last), for the network report.
struct SpeedLogRec { uint32_t at; uint8_t level; uint16_t kbs10; uint16_t rttMs; };
constexpr uint8_t SPEED_LOG_N=12;
