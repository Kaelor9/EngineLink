#pragma once
// EngineLink's diagnosis rules (no Arduino code, tested on a PC by
// tools/test_diagrules.cpp with simulated trips).
//
// Once a second the firmware passes what both engines reported; the rules
// look for three kinds of trouble:
//  1. Fixed limits for a KAD300: coolant too hot, charge air too hot,
//     battery not charging, overspeed, full throttle on a cold engine.
//  2. Departures from what is normal for this engine at this rpm, learned
//     over earlier trips (coolant, boost, charge air, fuel). CUSUM (a running
//     sum of the excess over a small allowance) catches a small but lasting
//     change early and ignores a single spike.
//  3. Behaviour: an engine that does not reach the rpm the lever asks for,
//     rpm hunting at a steady lever, one engine using clearly more fuel or
//     less boost than the other at the same rpm, an engine that stops while
//     running fast, lost contact while running, status bits that change,
//     an engine that never warms up, long idling.
// Each problem raises one event and then waits 10 minutes before repeating.
#include <math.h>
#include <stdint.h>
#include <string.h>

namespace diag {

enum Type : uint8_t {
  T_NONE = 0, T_COOL_HIGH, T_COOL_CRIT, T_AIR_HIGH, T_VOLT_LOW, T_VOLT_HIGH, T_RPM_SHORT, T_RPM_HUNT,
  T_DEV_COOL, T_DEV_BOOST, T_DEV_AIR, T_DEV_FUEL, T_IMB_FUEL, T_IMB_BOOST, T_COMM_LOST, T_STOPPED,
  T_OVERSPEED, T_COLD_LOAD, T_STATUS, T_LONG_IDLE, T_STAYS_COLD, T_MARK, T_COUNT
};
enum Sev : uint8_t { INFO = 0, WARN = 1, ALARM = 2 };
constexpr uint8_t SEVERITY[T_COUNT] = {INFO, WARN, ALARM, WARN, WARN, WARN, WARN, WARN, WARN, WARN, WARN, WARN,
                                       WARN, WARN, WARN, ALARM, ALARM, WARN, INFO, INFO, WARN, INFO};
constexpr const char *TYPE_ID[T_COUNT] = {"", "cool-high", "cool-crit", "air-high", "volt-low", "volt-high", "rpm-short",
  "rpm-hunt", "dev-cool", "dev-boost", "dev-air", "dev-fuel", "imb-fuel", "imb-boost", "comm-lost", "stopped",
  "overspeed", "cold-load", "status", "long-idle", "stays-cold", "mark"};

// What one engine reported this second. NAN = not known.
struct Eng {
  bool running = false;   // above cranking speed
  float rpm = NAN, req = NAN, lph = NAN, cool = NAN, air = NAN, boost = NAN, volt = NAN;
  int32_t st263 = -1, st264 = -1;  // status bits, -1 = not known
  bool polled = false;    // EngineLink is reading this engine
  float baseCool = NAN, baseBoost = NAN, baseAir = NAN, baseLph = NAN;  // normal at this rpm, NAN = not learned yet
};
struct Input {
  uint32_t t = 0;         // seconds since start
  Eng e[2];
  uint16_t gearMask = 0;  // gear bits in 264, ignored by the status rule
};
struct Event {
  uint8_t type, eng, sev;  // eng: 0, 1, or 2 for both / none
  float value, ref;        // what was measured, and against what
};

constexpr uint32_t REPEAT_S = 600;

struct Cusum {
  float s = 0;
  // Adds the excess over k; true once the sum passes h.
  bool add(float x, float k, float h) {
    s = fmaxf(0, s + x - k);
    return s > h;
  }
  void decay() { s *= 0.9f; }
};

class Rules {
 public:
  // Runs the rules for one second; returns how many events were written.
  int step(const Input &in, Event *out, int max) {
    n_ = 0; out_ = out; max_ = max; t_ = in.t;
    for (uint8_t c = 0; c < 2; c++) engine(in, c);
    pair(in);
    for (uint8_t c = 0; c < 3; c++) prevRun_[c] = c < 2 ? in.e[c].running : false;
    return n_;
  }
  void mark() { pendingMark_ = true; }
  // An alarm is active while its condition lasts and for a minute after.
  bool alarm() const { return t_ < alarmUntil_; }
  // A warning stays on the LED for 10 minutes or until it is acknowledged.
  bool warning() const { return t_ < warnUntil_; }
  void acknowledge() { warnUntil_ = 0; }
  float cusumOf(uint8_t c, uint8_t m) const { return dev_[c][m].s; }

 private:
  Event *out_ = nullptr;
  int n_ = 0, max_ = 0;
  uint32_t t_ = 0, alarmUntil_ = 0, warnUntil_ = 0;
  uint32_t last_[T_COUNT][3] = {};
  bool fired_[T_COUNT][3] = {};
  uint16_t over_[T_COUNT][2] = {};  // seconds a condition has held
  Cusum dev_[2][4], imbFuel_, imbBoost_;
  float reqHist_[2][12] = {}, rpmHist_[2][12] = {};
  uint8_t histN_[2] = {0, 0}, histPos_[2] = {0, 0};
  float prevRpm_[2][6] = {};
  float lastRpm_[2] = {0, 0};
  uint32_t lastSeen_[2] = {0, 0}, runSince_[2] = {0, 0}, idleSince_[2] = {0, 0};
  int32_t lastBits_[2][2] = {{-1, -1}, {-1, -1}}, newBits_[2][2] = {{-1, -1}, {-1, -1}};
  uint8_t bitsStable_[2][2] = {};
  bool prevRun_[3] = {};
  bool coldLoadTrip_[2] = {};
  bool pendingMark_ = false;

  void emit(Type ty, uint8_t eng, float v, float ref) {
    const uint8_t e = eng > 2 ? 2 : eng;
    if (fired_[ty][e] && t_ - last_[ty][e] < REPEAT_S) return;
    fired_[ty][e] = true; last_[ty][e] = t_;
    const uint8_t sev = SEVERITY[ty];
    if (sev == ALARM) alarmUntil_ = t_ + 60;
    if (sev == WARN && warnUntil_ < t_ + REPEAT_S) warnUntil_ = t_ + REPEAT_S;
    if (n_ < max_) out_[n_++] = Event{ty, e, sev, v, ref};
  }
  // True once cond has held for need seconds in a row.
  bool held(Type ty, uint8_t c, bool cond, uint16_t need) {
    uint16_t &o = over_[ty][c];
    o = cond ? (o < 65000 ? o + 1 : o) : 0;
    return o >= need;
  }
  void keepAlarm(bool cond) { if (cond && alarmUntil_ < t_ + 60) alarmUntil_ = t_ + 60; }

  void engine(const Input &in, uint8_t c) {
    const Eng &e = in.e[c];
    const bool rpmOk = !isnan(e.rpm);
    if (rpmOk) { lastSeen_[c] = t_; lastRpm_[c] = e.rpm; }
    if (e.running && !prevRun_[c]) { runSince_[c] = t_; idleSince_[c] = t_; coldLoadTrip_[c] = false; histN_[c] = 0; }
    const uint32_t ran = e.running ? t_ - runSince_[c] : 0;

    // Fixed limits.
    if (!isnan(e.cool)) {
      if (held(T_COOL_CRIT, c, e.cool > 102, 5)) emit(T_COOL_CRIT, c, e.cool, 102);
      keepAlarm(e.cool > 102);
      if (held(T_COOL_HIGH, c, e.cool > 95, 10)) emit(T_COOL_HIGH, c, e.cool, 95);
      if (held(T_STAYS_COLD, c, e.running && ran > 1200 && rpmOk && e.rpm > 2000 && e.cool < 60, 120)) emit(T_STAYS_COLD, c, e.cool, 60);
      if (!coldLoadTrip_[c] && held(T_COLD_LOAD, c, rpmOk && e.rpm > 2800 && e.cool < 50, 5)) { emit(T_COLD_LOAD, c, e.cool, 50); coldLoadTrip_[c] = true; }
    }
    if (!isnan(e.air) && held(T_AIR_HIGH, c, e.air > 70, 20)) emit(T_AIR_HIGH, c, e.air, 70);
    if (!isnan(e.volt) && e.running && ran > 60) {
      if (held(T_VOLT_LOW, c, e.volt < 12.6f, 30)) emit(T_VOLT_LOW, c, e.volt, 12.6f);
      if (held(T_VOLT_HIGH, c, e.volt > 15.0f, 10)) emit(T_VOLT_HIGH, c, e.volt, 15.0f);
    }
    if (rpmOk) {
      if (held(T_OVERSPEED, c, e.rpm > 4000, 2)) emit(T_OVERSPEED, c, e.rpm, 4000);
      keepAlarm(e.rpm > 4000);
    }
    // Stopped while running fast: above 1200 rpm within the last 5 s, now below 300.
    float recent = 0;
    for (float r : prevRpm_[c]) recent = fmaxf(recent, r);
    if (rpmOk && e.rpm < 300 && recent > 1200) emit(T_STOPPED, c, e.rpm, recent);
    memmove(&prevRpm_[c][1], &prevRpm_[c][0], sizeof(float) * 5);
    prevRpm_[c][0] = rpmOk ? e.rpm : 0;
    // Lost contact: being read, last seen above idle, no rpm for 6 s. (At
    // idle it is most likely the key: the control unit stops answering.)
    if (e.polled && lastSeen_[c] && !rpmOk && lastRpm_[c] > 1300 && t_ - lastSeen_[c] >= 6 && t_ - lastSeen_[c] < 8)
      emit(T_COMM_LOST, c, (float)(t_ - lastSeen_[c]), 6);

    // Lever against rpm, over the last 12 seconds.
    if (e.running && rpmOk && !isnan(e.req)) {
      reqHist_[c][histPos_[c]] = e.req; rpmHist_[c][histPos_[c]] = e.rpm;
      histPos_[c] = (histPos_[c] + 1) % 12;
      if (histN_[c] < 12) histN_[c]++;
    } else histN_[c] = 0;
    if (histN_[c] == 12) {
      float rqMin = 1e9, rqMax = -1e9, mean = 0, var = 0;
      for (int i = 0; i < 12; i++) { rqMin = fminf(rqMin, reqHist_[c][i]); rqMax = fmaxf(rqMax, reqHist_[c][i]); mean += rpmHist_[c][i]; }
      mean /= 12;
      for (int i = 0; i < 12; i++) var += (rpmHist_[c][i] - mean) * (rpmHist_[c][i] - mean);
      const float sd = sqrtf(var / 11), req = (rqMin + rqMax) / 2;
      // Hunting swings back and forth: count the crossings of the mean, in
      // time order, so a single step up or down does not count.
      int cross = 0;
      for (int i = 1; i < 12; i++) {
        const float a = rpmHist_[c][(histPos_[c] + i - 1) % 12] - mean, b = rpmHist_[c][(histPos_[c] + i) % 12] - mean;
        if ((a < 0) != (b < 0)) cross++;
      }
      const bool steady = rqMax - rqMin < 40;
      // At part throttle the governor reaches the requested rpm; full
      // throttle is limited by the propeller, which is normal.
      if (held(T_RPM_SHORT, c, steady && req >= 1000 && req < 3200 && mean > 500 && mean < req - 150, 10)) emit(T_RPM_SHORT, c, mean, req);
      if (held(T_RPM_HUNT, c, steady && ran > 120 && cross >= 5 && sd > (req < 1000 ? 35 : 50), 10)) emit(T_RPM_HUNT, c, sd, req < 1000 ? 35 : 50);
    }
    // Departure from this engine's normal at this rpm (warm engine only).
    const bool warm = !isnan(e.cool) && e.cool >= 70 && e.running && ran > 300;
    // Relative values are only compared where they are big enough to mean
    // something: boost from 25 kPa, fuel from 6 L/h (not at idle).
    struct M { float x, base; float k, h; Type ty; bool rel, low; float floor; };
    const M ms[4] = {{e.cool, e.baseCool, 3, 60, T_DEV_COOL, false, false, 0},
                     {e.boost, e.baseBoost, 0.08f, 2.4f, T_DEV_BOOST, true, true, 25},
                     {e.air, e.baseAir, 4, 80, T_DEV_AIR, false, false, 0},
                     {e.lph, e.baseLph, 0.08f, 3.0f, T_DEV_FUEL, true, false, 6}};
    for (int m = 0; m < 4; m++) {
      const M &x = ms[m];
      if (!warm || isnan(x.x) || isnan(x.base) || (x.rel && fabsf(x.base) < x.floor)) { dev_[c][m].decay(); continue; }
      float d = x.rel ? (x.x - x.base) / fabsf(x.base) : x.x - x.base;
      if (x.low) d = -d;
      if (dev_[c][m].add(d, x.k, x.h)) { emit(x.ty, c, x.x, x.base); dev_[c][m].s = 0; }
    }
    // Long idling (soot and wet stacking in a diesel).
    if (!e.running || !rpmOk || e.rpm >= 1000) idleSince_[c] = t_;
    else if (t_ - idleSince_[c] == 1200) emit(T_LONG_IDLE, c, 1200, 1200);
    // Status bits: a change that holds for 3 s, outside the gear bits.
    if (ran > 30) {
      const int32_t v[2] = {e.st263, e.st264 < 0 ? -1 : (int32_t)(e.st264 & ~in.gearMask)};
      for (int k = 0; k < 2; k++) {
        if (v[k] < 0) continue;
        if (lastBits_[c][k] < 0) { lastBits_[c][k] = v[k]; continue; }
        if (v[k] == lastBits_[c][k]) { bitsStable_[c][k] = 0; continue; }
        if (v[k] != newBits_[c][k]) { newBits_[c][k] = v[k]; bitsStable_[c][k] = 0; }
        if (++bitsStable_[c][k] >= 3) {
          emit(T_STATUS, c, (float)v[k], (float)lastBits_[c][k]);
          lastBits_[c][k] = v[k]; bitsStable_[c][k] = 0;
        }
      }
    }
  }
  // Port against starboard at the same rpm.
  void pair(const Input &in) {
    const Eng &a = in.e[0], &b = in.e[1];
    const bool same = a.running && b.running && !isnan(a.rpm) && !isnan(b.rpm) && fabsf(a.rpm - b.rpm) < 60 &&
                      (a.rpm + b.rpm) / 2 > 1500 && !isnan(a.cool) && a.cool >= 70 && !isnan(b.cool) && b.cool >= 70;
    if (same && !isnan(a.lph) && !isnan(b.lph) && a.lph + b.lph > 4) {
      const float rel = (b.lph - a.lph) / ((a.lph + b.lph) / 2);
      if (imbFuel_.add(fabsf(rel), 0.10f, 6.0f)) { emit(T_IMB_FUEL, rel > 0 ? 1 : 0, rel, 0); imbFuel_.s = 0; }
    } else imbFuel_.decay();
    if (same && !isnan(a.boost) && !isnan(b.boost) && a.boost + b.boost > 20) {
      const float rel = (b.boost - a.boost) / ((a.boost + b.boost) / 2);
      if (imbBoost_.add(fabsf(rel), 0.15f, 6.0f)) { emit(T_IMB_BOOST, rel < 0 ? 1 : 0, rel, 0); imbBoost_.s = 0; }
    } else imbBoost_.decay();
    if (pendingMark_) { pendingMark_ = false; fired_[T_MARK][2] = false; emit(T_MARK, 2, 0, 0); }
  }
};

// The engine's life story, per engine, updated once a second.
constexpr uint8_t LIFE_BUCKETS = 13;  // under 1000 rpm, then 250 rpm steps up to 3750+
struct Life {
  uint32_t magic = 0xE11F0001;
  uint32_t sec[2][LIFE_BUCKETS] = {};
  uint32_t starts[2] = {}, coldStarts[2] = {}, hotSec[2] = {}, coldLoadSec[2] = {}, overSec[2] = {}, longIdles[2] = {};
  float lowStartVolt[2] = {0, 0};       // lowest battery voltage seen in the first 20 s after the last start
  float wotRpm[2] = {0, 0};             // highest rpm on the last trip (full throttle reach)
  float wotRpmBest[2] = {0, 0};         // and the best ever
  uint32_t eventsBy[T_COUNT] = {};
};
inline int lifeBucket(float rpm) {
  if (rpm < 1000) return 0;
  const int b = 1 + (int)((rpm - 1000) / 250);
  return b >= LIFE_BUCKETS ? LIFE_BUCKETS - 1 : b;
}

}  // namespace diag
