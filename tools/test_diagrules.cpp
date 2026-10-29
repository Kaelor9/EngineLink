// Checks the diagnosis rules in firmware/EngineLink/diagrules.h with simulated trips:
//   g++ -O2 -std=c++17 -Wall -I firmware/EngineLink tools/test_diagrules.cpp -o /tmp/t && /tmp/t
// A normal trip (start, warm-up, harbour, cruise, full throttle, back to idle,
// key off) must raise nothing; each fault must raise its own event.
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <vector>
#include "diagrules.h"
using namespace diag;

static int failures = 0;
static void check(bool ok, const std::string &what) {
  printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) failures++;
}

// A simulated twin KAD300 trip, second by second. fault() may change the
// engines after the normal values are set.
struct Trip {
  std::mt19937 rng{3};
  std::normal_distribution<float> nz{0, 1};
  std::vector<Event> events;
  Rules rules;
  // Lever programme: seconds, requested rpm.
  std::vector<std::pair<int, float>> lever = {{600, 600}, {300, 1200}, {60, 2000}, {1500, 2900}, {60, 3800}, {600, 2900}, {300, 1200}, {300, 600}};
  void run(std::function<void(uint32_t, Input &)> fault = nullptr, bool learned = true) {
    uint32_t t = 0;
    float cool[2] = {15, 16};
    for (auto [len, req] : lever)
      for (int s = 0; s < len; s++, t++) {
        Input in;
        in.t = t;
        for (int c = 0; c < 2; c++) {
          Eng &e = in.e[c];
          e.running = true;
          e.polled = true;
          e.req = req;
          const float reach = req > 3300 ? 3150 + c * 40 : req;  // full throttle limited by the propeller
          e.rpm = reach + nz(rng) * (req < 1000 ? 6 : 10);
          const float load = fmaxf(0, (e.rpm - 600) / 3200);
          const float target = 78 + load * 5;
          cool[c] += (target - cool[c]) * 0.004f;
          e.cool = cool[c] + nz(rng) * 0.2f;
          e.lph = 1.8f + 60 * load * load * load + nz(rng) * 0.3f;
          e.boost = 100 * load * load + nz(rng) * 1.5f;
          e.air = 20 + 30 * load + nz(rng) * 0.5f;
          e.volt = 13.9f + nz(rng) * 0.05f;
          e.st263 = 0x24;
          e.st264 = 0x0945 | (c ? 0x4000 : 0);
          if (learned) {
            e.baseCool = target; e.baseLph = 1.8f + 60 * load * load * load;
            e.baseBoost = 100 * load * load; e.baseAir = 20 + 30 * load;
          }
        }
        if (fault) fault(t, in);
        Event out[8];
        const int n = rules.step(in, out, 8);
        for (int i = 0; i < n; i++) events.push_back(out[i]);
      }
    // Key off at idle: the control units stop answering.
    for (int s = 0; s < 30; s++, t++) {
      Input in;
      in.t = t;
      for (int c = 0; c < 2; c++) { in.e[c].polled = true; in.e[c].running = s < 10; }
      Event out[8];
      const int n = rules.step(in, out, 8);
      for (int i = 0; i < n; i++) events.push_back(out[i]);
    }
  }
  bool has(Type ty, int eng = -1) const {
    for (auto &e : events) if (e.type == ty && (eng < 0 || e.eng == eng)) return true;
    return false;
  }
  std::string list() const {
    std::string s;
    for (auto &e : events) s += std::string(TYPE_ID[e.type]) + "(" + std::to_string(e.eng) + ") ";
    return s.empty() ? "none" : s;
  }
};

int main() {
  {
    Trip t; t.run();
    check(t.events.empty(), "normal trip raises nothing: " + t.list());
    check(!t.rules.alarm(), "no alarm after a normal trip");
  }
  {
    Trip t; t.run(nullptr, false);
    check(t.events.empty(), "normal trip before anything is learned raises nothing: " + t.list());
  }
  auto fault = [](const char *what, std::function<void(uint32_t, Input &)> f, Type ty, int eng) {
    Trip t; t.run(f);
    check(t.has(ty, eng), std::string(what) + " -> " + TYPE_ID[ty] + " (got " + t.list() + ")");
    return t;
  };
  // Starboard runs 7 °C warmer than normal during the cruise (worn impeller).
  fault("starboard 7 °C warmer at cruise", [](uint32_t t, Input &in) { if (t > 1300 && t < 2400) in.e[1].cool += 7; }, T_DEV_COOL, 1);
  // Port boost 25 % low at cruise (turbo or compressor).
  fault("port boost 25 % low", [](uint32_t t, Input &in) { if (t > 1300 && t < 2400) in.e[0].boost *= 0.75f; }, T_DEV_BOOST, 0);
  // Starboard uses 30 % more fuel than port at the same rpm.
  fault("starboard 30 % more fuel", [](uint32_t t, Input &in) { if (t > 1300 && t < 2400) { in.e[1].lph *= 1.3f; in.e[1].baseLph = NAN; in.e[0].baseLph = NAN; } }, T_IMB_FUEL, 1);
  // Overheating.
  {
    Trip t = fault("coolant to 104 °C", [](uint32_t t, Input &in) { if (t > 1500 && t < 1600) in.e[0].cool = 104; }, T_COOL_CRIT, 0);
    check(t.has(T_COOL_HIGH, 0), "and the warning before it");
  }
  // Port cannot reach 2900 rpm (fouled filter): stays at 2650.
  fault("port stuck 250 rpm below the lever", [](uint32_t t, Input &in) { if (t > 1300 && t < 2400) in.e[0].rpm -= 250; }, T_RPM_SHORT, 0);
  // Starboard hunting at idle.
  fault("starboard hunting at idle", [](uint32_t t, Input &in) { if (t > 200 && t < 500) in.e[1].rpm += (t % 2 ? 70 : -70); }, T_RPM_HUNT, 1);
  // Alternator not charging.
  fault("port at 12.2 V while running", [](uint32_t t, Input &in) { if (t > 1000) in.e[0].volt = 12.2f; }, T_VOLT_LOW, 0);
  // Port stops while at cruise.
  fault("port stops at cruise", [](uint32_t t, Input &in) { if (t > 1800 && t < 1830) { in.e[0].rpm = 0; } }, T_STOPPED, 0);
  // Port stops answering while at cruise.
  fault("port stops answering at cruise", [](uint32_t t, Input &in) {
    if (t > 1800 && t < 1830) { Eng &e = in.e[0]; e.rpm = e.req = e.lph = e.cool = e.air = e.boost = e.volt = NAN; e.st263 = e.st264 = -1; }
  }, T_COMM_LOST, 0);
  // A status bit appears at cruise (a lamp or relay).
  fault("a status bit appears", [](uint32_t t, Input &in) { if (t > 2000) in.e[1].st264 |= 0x0200; }, T_STATUS, 1);
  // Full throttle on a cold engine.
  {
    Trip t;
    t.lever = {{120, 600}, {60, 3000}, {600, 1000}};
    t.run();
    check(t.has(T_COLD_LOAD), "full throttle on a cold engine -> cold-load (got " + t.list() + ")");
  }
  // Mark from the app.
  {
    Trip t; t.run([&](uint32_t s, Input &) { if (s == 100) t.rules.mark(); });
    check(t.has(T_MARK, 2), "something's odd mark is recorded");
  }
  printf(failures ? "%d FAILED\n" : "all passed\n", failures);
  return failures ? 1 : 0;
}
