#pragma once
// Gear and clutch analysis for one engine with a Volvo Penta DP drive (no
// Arduino code, tested on a PC by tools/test_shiftrules.cpp).
//
// A KAD300/DP shifts electrically: the EDC control unit drives a shift
// actuator, and a cone clutch in the drive couples the propeller. Two things
// are measured independently:
//   1. What the control unit says: the gear bits in value 264, learned in the
//      drive test (neutral, in gear, and forward/reverse when the bits tell).
//   2. Whether the propeller actually pulls: at idle an engine in gear burns
//      clearly more fuel than in neutral at the same rpm (the propeller is a
//      load). Measured on the boat: about 2.2 L/h in neutral against 3.0 L/h
//      in gear at 600 rpm.
// From both together every shift at idle is timed (lever to clutch in, and
// neutral to clutch out), a propeller that keeps pulling in neutral raises an
// event, and hard shifts (forward to reverse without a pause, or a shift above
// idle) are counted, because they wear the cone clutch.
// Called about four times a second.
#include <math.h>
#include <stdint.h>

namespace shift {

enum Gear : uint8_t { G_UNKNOWN = 0, G_NEUTRAL = 1, G_FWD = 2, G_REV = 3, G_IN = 4 };  // G_IN: in gear, direction not told
inline bool inGear(uint8_t g) { return g == G_FWD || g == G_REV || g == G_IN; }

// Limits. Idle is where load can be told from fuel; above it nothing changes.
constexpr float IDLE_MAX_RPM = 900;     // load is only judged below this
constexpr float BASE_MIN_RPM = 450, BASE_MAX_RPM = 850;
constexpr float LOAD_RATIO = 1.22f;     // fuel at least 22 % above neutral idle
constexpr float LOAD_MIN_LPH = 0.35f;   // and at least this much more
constexpr uint32_t LOAD_ON_MS = 750;    // held this long before it counts as in gear (a nudge into gear at the dock lasts about a second)
constexpr float FLARE_RPM = 70;         // above idle rpm by more than this: a blip or the flare at clutch out, not load
constexpr uint32_t LOAD_OFF_MS = 500;   // and this long before it counts as out
constexpr uint32_t NEUTRAL_PULL_MS = 2500;  // still pulling this long after neutral: event
constexpr uint32_t GEAR_FRESH_MS = 1600;    // gear bits younger than this are trusted
constexpr uint32_t QUICK_REVERSE_MS = 1200; // less time than this in neutral between directions
constexpr float HARD_RPM = 1000;        // a shift above this rpm is hard on the clutch
constexpr uint32_t LAG_MAX_MS = 8000;   // a lag longer than this is not one shift

// One reading of one engine.
struct In {
  uint32_t ms = 0;          // a millisecond clock
  bool running = false;
  float rpm = NAN, lph = NAN, req = NAN;
  uint8_t gear = G_UNKNOWN; // from the learned bits, G_UNKNOWN when not learned or not read
  uint32_t gearAgeMs = 0xFFFFFFFF;  // age of the gear bits
};
enum Kind : uint8_t { K_ENGAGE = 1, K_DISENGAGE = 2 };
enum Flag : uint8_t { F_QUICK = 1, F_HIGH_RPM = 2, F_SLOW = 4, F_NO_BITS = 8 };
struct Shift {
  uint32_t ms;
  uint8_t kind, gear, flags;
  int32_t lagMs;            // lever (gear bits) to clutch, -1 = not known
  float rpm;
};
enum EvType : uint8_t { E_PULL_NEUTRAL = 1, E_HARD_SHIFT = 2 };
struct Ev { uint8_t type; float value, ref; };

class Tracker {
 public:
  float base = NAN;         // fuel at idle in neutral (L/h), learned while running
  float baseRpm = NAN;      // idle rpm
  bool loaded = false;      // the propeller pulls
  bool known = false;       // loaded is meaningful (at idle, fuel read)

  // Returns the number of shifts written to sh (at most 2) and events to ev (at most 2).
  void step(const In &in, Shift *sh, int &ns, Ev *ev, int &ne) {
    ns = 0; ne = 0;
    if (!in.running) { reset(); return; }
    const bool fresh = in.gear != G_UNKNOWN && in.gearAgeMs <= GEAR_FRESH_MS;
    // Gear bit changes, with the rpm at that moment.
    if (fresh && in.gear != gear_) {
      const uint8_t was = gear_;
      gear_ = in.gear; gearAt_ = in.ms;
      if (inGear(in.gear)) {
        // Straight from one direction to the other, or back in gear right after neutral.
        const bool reversal = (was == G_FWD && in.gear == G_REV) || (was == G_REV && in.gear == G_FWD);
        const bool quick = lastNeutralAt_ && in.ms - lastNeutralAt_ < QUICK_REVERSE_MS &&
                           lastDir_ != G_UNKNOWN && in.gear != G_IN && lastDir_ != in.gear;
        if (reversal || quick) hardEv(ev, ne, 1, reversal ? 0 : (float)(in.ms - lastNeutralAt_));
        if (in.gear != G_IN) lastDir_ = in.gear;
      } else if (in.gear == G_NEUTRAL) {
        lastNeutralAt_ = in.ms; pullFired_ = false; loadedAtNeutral_ = loaded;
      }
      if (was != G_UNKNOWN && fin(in.rpm) && in.rpm > HARD_RPM) hardEv(ev, ne, 2, in.rpm);
    }
    // Neutral idle fuel: follows quickly while the bits say neutral, otherwise
    // the lower envelope (down fast, up slowly) so time in gear does not lift it.
    const bool idle = fin(in.rpm) && in.rpm < IDLE_MAX_RPM && fin(in.lph) && in.lph > 0;
    const bool flare = fin(baseRpm) && fin(in.rpm) && in.rpm > baseRpm + FLARE_RPM;
    if (idle && in.rpm >= BASE_MIN_RPM && in.rpm <= BASE_MAX_RPM) {
      if (!fin(baseRpm)) baseRpm = in.rpm;
      else if (!flare) baseRpm += 0.02f * (in.rpm - baseRpm);
    }
    if (idle && !flare && in.rpm >= BASE_MIN_RPM && in.rpm <= BASE_MAX_RPM) {
      if (!fin(base)) base = in.lph;
      else if (fresh && gear_ == G_NEUTRAL && !loaded) base += 0.05f * (in.lph - base);
      else if (!(fresh && inGear(gear_))) base += (in.lph < base ? 0.05f : 0.002f) * (in.lph - base);
    }
    if (!idle || !fin(base)) { onSince_ = offSince_ = 0; known = idle && known; return; }
    known = true;
    // A blip or the short flare when the clutch lets go burns fuel too, but
    // the rpm rises; under propeller load the rpm stays at idle or dips.
    if (flare) { onSince_ = 0; return; }
    const bool pull = in.lph > base * LOAD_RATIO && in.lph - base > LOAD_MIN_LPH;
    if (pull) {
      offSince_ = 0;
      if (!onSince_) onSince_ = in.ms;
      if (!loaded && in.ms - onSince_ >= LOAD_ON_MS) {
        loaded = true;
        Shift s{onSince_, K_ENGAGE, gear_, 0, -1, in.rpm};
        if (inGear(gear_) && gearAt_ && onSince_ >= gearAt_ && onSince_ - gearAt_ < LAG_MAX_MS) s.lagMs = (int32_t)(onSince_ - gearAt_);
        else if (!inGear(gear_)) s.flags |= F_NO_BITS;
        if (lastOffAt_ && onSince_ - lastOffAt_ < QUICK_REVERSE_MS) {
          s.flags |= F_QUICK;
          // Without direction bits a clutch that lets go and bites again within a
          // second is the only sign of a fast forward-to-reverse shift.
          if (gear_ != G_FWD && gear_ != G_REV) hardEv(ev, ne, 3, (float)(onSince_ - lastOffAt_));
        }
        sh[ns++] = s;
      }
    } else {
      onSince_ = 0;
      if (!offSince_) offSince_ = in.ms;
      if (loaded && in.ms - offSince_ >= LOAD_OFF_MS) {
        loaded = false; lastOffAt_ = offSince_; loadedAtNeutral_ = false;
        Shift s{offSince_, K_DISENGAGE, gear_, 0, -1, in.rpm};
        if (gear_ == G_NEUTRAL && gearAt_ && offSince_ >= gearAt_ && offSince_ - gearAt_ < LAG_MAX_MS) {
          s.lagMs = (int32_t)(offSince_ - gearAt_);
          if (s.lagMs >= (int32_t)NEUTRAL_PULL_MS) s.flags |= F_SLOW;
        } else if (gear_ != G_NEUTRAL) s.flags |= F_NO_BITS;
        sh[ns++] = s;
      }
    }
    // Neutral, freshly confirmed, and the propeller still pulls.
    // Only when it was pulling as the lever went to neutral and never let go.
    if (loaded && loadedAtNeutral_ && fresh && gear_ == G_NEUTRAL && !pullFired_ && in.ms - gearAt_ >= NEUTRAL_PULL_MS) {
      pullFired_ = true;
      if (ne < 2) ev[ne++] = Ev{E_PULL_NEUTRAL, (float)(in.ms - gearAt_) / 1000.0f, (float)NEUTRAL_PULL_MS / 1000.0f};
    }
  }
  uint8_t gear() const { return gear_; }

 private:
  uint8_t gear_ = G_UNKNOWN, lastDir_ = G_UNKNOWN;
  uint32_t gearAt_ = 0, lastNeutralAt_ = 0, onSince_ = 0, offSince_ = 0, lastOffAt_ = 0;
  bool pullFired_ = false, loadedAtNeutral_ = false;
  static bool fin(float v) { return !isnan(v) && !isinf(v); }
  void hardEv(Ev *ev, int &ne, int why, float v) { if (ne < 2) ev[ne++] = Ev{E_HARD_SHIFT, v, (float)why}; }
  void reset() {
    loaded = false; known = false; gear_ = G_UNKNOWN; gearAt_ = lastNeutralAt_ = onSince_ = offSince_ = lastOffAt_ = 0;
    lastDir_ = G_UNKNOWN; pullFired_ = loadedAtNeutral_ = false;
  }
};

// Gear from the raw 264 bits and the patterns learned in the drive test.
// Forward and reverse may share one pattern when the bits only tell "in gear".
inline uint8_t gearFromBits(int32_t bits, uint16_t mF, uint16_t vF, uint16_t mR, uint16_t vR) {
  if (bits < 0 || (!mF && !mR)) return G_UNKNOWN;
  const uint16_t b = (uint16_t)bits;
  const bool f = mF && (b & mF) == vF, r = mR && (b & mR) == vR;
  if (mF == mR && vF == vR) return f ? G_IN : G_NEUTRAL;
  if (f && !r) return G_FWD;
  if (r && !f) return G_REV;
  if (f && r) return G_IN;
  return G_NEUTRAL;
}

// Accelerations from idle and whether the compressor clutch came on (bit
// learned in the drive test), with the highest boost seen.
struct Accel {
  bool active = false, compSeen = false;
  float boostPeak = NAN;
  uint32_t startMs = 0, lowAt = 0;
  // True when an acceleration has just finished; compSeen/boostPeak describe it.
  bool step(uint32_t ms, float rpm, float boost, int8_t comp) {
    if (isnan(rpm)) { active = false; return false; }
    if (!active) {
      if (rpm < IDLE_MAX_RPM) lowAt = ms;
      else if (rpm > 1300 && lowAt && ms - lowAt < 3000) { active = true; compSeen = false; boostPeak = NAN; startMs = ms; }
      if (!active) return false;
    }
    if (comp > 0) compSeen = true;
    if (!isnan(boost) && (isnan(boostPeak) || boost > boostPeak)) boostPeak = boost;
    if (ms - startMs >= 6000 || rpm < IDLE_MAX_RPM) { active = false; lowAt = rpm < IDLE_MAX_RPM ? ms : 0; return true; }
    return false;
  }
};

}  // namespace shift
