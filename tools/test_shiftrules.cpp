// Checks the gear and clutch analysis in firmware/EngineLink/shiftrules.h with
// simulated shifts at idle, four readings a second:
//   g++ -O2 -std=c++17 -Wall -I firmware/EngineLink tools/test_shiftrules.cpp -o /tmp/t && /tmp/t
// The figures follow a KAD300/DP-G recorded on the boat: about 600 rpm and
// 2.2 L/h in neutral, 3.0-3.3 L/h in gear, a short rpm flare to ~760 when the
// clutch lets go, and gear bits read about once a second.
#include <cstdio>
#include <random>
#include <string>
#include <vector>
#include "shiftrules.h"
using namespace shift;

static int failures = 0;
static void check(bool ok, const std::string &what) {
  printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) failures++;
}

// One engine. A programme of phases: what the lever (gear bits) says and
// whether the propeller pulls, each for a number of milliseconds.
struct Phase { uint32_t ms; uint8_t bits; bool pull; float rpm = 600; };
struct Sim {
  std::mt19937 rng{7};
  std::normal_distribution<float> nz{0, 1};
  Tracker tr;
  std::vector<Shift> shifts;
  std::vector<Ev> events;
  uint32_t t = 0;
  float idleLph = 2.2f, gearLph = 3.15f;
  uint32_t bitsPeriod = 700;   // gear bits read this often
  bool flare = true;           // rpm flare when the clutch lets go
  void run(const std::vector<Phase> &prog) {
    uint8_t shownBits = G_UNKNOWN;
    uint32_t bitsAt = 0, lastRead = 0;
    bool wasPull = false;
    uint32_t flareUntil = 0;
    for (const Phase &p : prog) {
      for (uint32_t e = 0; e < p.ms; e += 250, t += 250) {
        if (t - lastRead >= bitsPeriod || !lastRead) { shownBits = p.bits; bitsAt = t; lastRead = t ? t : 1; }
        if (wasPull && !p.pull && flare) flareUntil = t + 600;
        wasPull = p.pull;
        In in;
        in.ms = t + 100000;
        in.running = true;
        const bool fl = t < flareUntil;
        in.rpm = p.rpm + (fl ? 160 : 0) + (p.pull ? -15 : 0) + nz(rng) * 12;
        float lph = p.rpm > 800 ? 2.2f + (p.rpm - 600) / 100 : (p.pull ? gearLph : idleLph);
        if (fl) lph = 2.0f;
        in.lph = lph + nz(rng) * 0.12f;
        in.req = p.rpm;
        in.gear = shownBits;
        in.gearAgeMs = t - bitsAt;
        Shift sh[2]; Ev ev[2]; int ns, ne;
        tr.step(in, sh, ns, ev, ne);
        for (int i = 0; i < ns; i++) shifts.push_back(sh[i]);
        for (int i = 0; i < ne; i++) events.push_back(ev[i]);
      }
    }
  }
  int count(uint8_t kind) const { int n = 0; for (auto &s : shifts) n += s.kind == kind; return n; }
  bool has(uint8_t type, int why = -1) const {
    for (auto &e : events) if (e.type == type && (why < 0 || (int)e.ref == why)) return true;
    return false;
  }
  std::string list() const {
    std::string s;
    for (auto &e : events) s += (e.type == E_PULL_NEUTRAL ? "pull-neutral" : "hard-shift(" + std::to_string((int)e.ref) + ")") + std::string(" ");
    return s.empty() ? "none" : s;
  }
};

int main() {
  // Bits from the boat: neutral 0x4945, in gear 0x4845 (bit 0x100 clears).
  check(gearFromBits(0x4945, 0x100, 0, 0x100, 0) == G_NEUTRAL, "bits 0x4945 with the in-gear pattern 0x100=0 -> neutral");
  check(gearFromBits(0x4845, 0x100, 0, 0x100, 0) == G_IN, "bits 0x4845 -> in gear, direction not told");
  check(gearFromBits(0x0955, 0x18, 0x10, 0x18, 0x08) == G_FWD, "separate forward pattern -> forward");
  check(gearFromBits(0x094D, 0x18, 0x10, 0x18, 0x08) == G_REV, "separate reverse pattern -> reverse");
  check(gearFromBits(0x4945, 0, 0, 0, 0) == G_UNKNOWN, "nothing learned -> unknown");
  check(gearFromBits(-1, 0x100, 0, 0x100, 0) == G_UNKNOWN, "bits not read -> unknown");

  // A minute of warm-up, then normal manoeuvring at the dock.
  {
    Sim s;
    s.run({{60000, G_NEUTRAL, false},
           {500, G_IN, false}, {6000, G_IN, true}, {300, G_NEUTRAL, true}, {8000, G_NEUTRAL, false},
           {500, G_IN, false}, {1200, G_IN, true}, {300, G_NEUTRAL, true}, {9000, G_NEUTRAL, false},
           {400, G_IN, false}, {12000, G_IN, true}, {250, G_NEUTRAL, true}, {20000, G_NEUTRAL, false}});
    check(s.count(K_ENGAGE) == 3 && s.count(K_DISENGAGE) == 3, "three shifts in and out are counted (" + std::to_string(s.count(K_ENGAGE)) + "/" + std::to_string(s.count(K_DISENGAGE)) + ")");
    check(s.events.empty(), "normal manoeuvring raises nothing: " + s.list());
    bool lagOk = true;
    for (auto &x : s.shifts) if (x.lagMs < 0 || x.lagMs > 1800) lagOk = false;
    check(lagOk, "every shift is timed from the gear bits (under 1.8 s)");
    check(s.tr.base > 2.0f && s.tr.base < 2.45f, "neutral idle fuel learned: " + std::to_string(s.tr.base));
  }
  // The clutch keeps pulling for 5 s after the lever went to neutral.
  {
    Sim s;
    s.run({{60000, G_NEUTRAL, false}, {500, G_IN, false}, {8000, G_IN, true}, {5000, G_NEUTRAL, true}, {20000, G_NEUTRAL, false}});
    check(s.has(E_PULL_NEUTRAL), "propeller pulling 5 s in neutral -> event (got " + s.list() + ")");
    bool slow = false;
    for (auto &x : s.shifts) if (x.kind == K_DISENGAGE && (x.flags & F_SLOW) && x.lagMs >= 4500) slow = true;
    check(slow, "and the disengagement is marked slow with its lag");
  }
  // Pulling 1.5 s after neutral is still within normal.
  {
    Sim s;
    s.run({{60000, G_NEUTRAL, false}, {500, G_IN, false}, {8000, G_IN, true}, {1500, G_NEUTRAL, true}, {20000, G_NEUTRAL, false}});
    check(!s.has(E_PULL_NEUTRAL), "1.5 s to let go raises nothing (got " + s.list() + ")");
  }
  // Forward straight to reverse, with direction bits.
  {
    Sim s;
    s.run({{60000, G_NEUTRAL, false}, {500, G_FWD, false}, {8000, G_FWD, true}, {300, G_NEUTRAL, true}, {300, G_REV, true}, {8000, G_REV, true}, {20000, G_NEUTRAL, false}});
    s.bitsPeriod = 700;
    check(s.has(E_HARD_SHIFT), "forward to reverse within a second -> hard shift (got " + s.list() + ")");
  }
  // Without direction bits: the clutch lets go and bites again within a second.
  {
    Sim s; s.flare = false;
    s.run({{60000, G_NEUTRAL, false}, {500, G_IN, false}, {8000, G_IN, true}, {800, G_IN, false}, {8000, G_IN, true}, {20000, G_NEUTRAL, false}});
    check(s.has(E_HARD_SHIFT, 3), "clutch out and in again within a second -> hard shift (got " + s.list() + ")");
  }
  // Shifting at 1500 rpm.
  {
    Sim s;
    s.run({{60000, G_NEUTRAL, false}, {4000, G_NEUTRAL, false, 1500}, {1000, G_IN, false, 1500}, {6000, G_IN, true, 1500}, {6000, G_IN, true}, {20000, G_NEUTRAL, false}});
    check(s.has(E_HARD_SHIFT, 2), "shift at 1500 rpm -> hard shift (got " + s.list() + ")");
  }
  // Revving in neutral and a cold start must not look like a shift.
  {
    Sim s;
    s.run({{60000, G_NEUTRAL, false}, {3000, G_NEUTRAL, false, 1500}, {6000, G_NEUTRAL, false}, {3000, G_NEUTRAL, false, 1400}, {30000, G_NEUTRAL, false}});
    check(s.shifts.empty() && s.events.empty(), "revving in neutral is not a shift (" + std::to_string(s.shifts.size()) + " shifts, " + s.list() + ")");
  }
  {
    Sim s;
    // Idle fuel falls from 3.2 to 2.2 L/h over ten minutes while warming up.
    for (int m = 0; m < 10; m++) { s.idleLph = 3.2f - m * 0.1f; s.run({{60000, G_NEUTRAL, false}}); }
    check(s.shifts.empty(), "warming up at idle is not a shift (" + std::to_string(s.shifts.size()) + ")");
  }
  // No gear learned: shifts are still counted from the fuel, without timing.
  {
    Sim s;
    s.run({{60000, G_UNKNOWN, false}, {6000, G_UNKNOWN, true}, {15000, G_UNKNOWN, false}, {6000, G_UNKNOWN, true}, {15000, G_UNKNOWN, false}});
    check(s.count(K_ENGAGE) == 2, "without learned bits shifts are counted from the fuel (" + std::to_string(s.count(K_ENGAGE)) + ")");
    check(!s.has(E_PULL_NEUTRAL), "and no neutral event without bits");
  }
  // Acceleration from idle with the compressor bit.
  {
    Accel a; bool done = false, comp = false; uint32_t t = 0;
    for (; t < 3000; t += 250) a.step(t, 600, 0, 0);
    for (int i = 0; i < 60 && !done; i++, t += 250) { done = a.step(t, 600 + i * 120.0f, i * 3.0f, i > 4 ? 1 : 0); comp = a.compSeen; }
    check(done && comp, "an acceleration from idle sees the compressor come on");
  }
  printf(failures ? "%d FAILED\n" : "all passed\n", failures);
  return failures ? 1 : 0;
}
