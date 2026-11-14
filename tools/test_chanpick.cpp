// Host test for firmware/EngineLink/chanpick.h (plan section 19).
#include <cassert>
#include <cstdio>
#include "chanpick.h"
using namespace chanpick;

int main() {
  // Rates and air time.
  assert(rateKbps(0, 0, 0, false) == 1000 && rateKbps(0, 11, 0, false) == 6000 && rateKbps(0, 12, 0, false) == 54000);
  assert(rateKbps(0, 4, 0, false) == 6000 && rateKbps(0, 31, 0, false) == 6000);
  assert(rateKbps(1, 0, 7, false) == 65000 && rateKbps(1, 0, 15, false) == 130000 && rateKbps(1, 0, 7, true) == 130000);
  assert(airtimeUs(0, 0, 0, false, 300) == 192 + 2400);   // a 300-byte beacon at 1 Mbit/s
  assert(airtimeUs(0, 11, 0, false, 300) == 20 + 400);
  assert(airtimeUs(1, 0, 7, false, 1500) == 36 + 184);

  // The bench on 2026-11-14: channel 6 busy (a neighbour's traffic), 1 quiet
  // although it had more networks. Busy time decides, not the network count.
  Survey bench;
  bench.heard(1, 30000, 500, 40);      //  6 %
  bench.heard(6, 160000, 500, 300);    // 32 %
  bench.heard(11, 60000, 500, 90);     // 12 %
  assert(bench.busyPermille(1) == 60 && bench.busyPermille(6) == 320 && bench.busyPermille(11) == 120);
  assert(bench.pick(6) == 1 && bench.pick(0) == 1 && bench.pick(1) == 1);
  assert(bench.pick(11) == 11);         // 120 -> 60 is not under half: stays

  // Small gains do not move; equal channels keep the current one.
  Survey close;
  close.heard(1, 20000, 1000, 1); close.heard(6, 30000, 1000, 1); close.heard(11, 25000, 1000, 1);
  assert(close.pick(6) == 6 && close.pick(11) == 11 && close.pick(0) == 1);
  Survey quiet;
  quiet.heard(1, 0, 500, 0); quiet.heard(6, 0, 500, 0); quiet.heard(11, 0, 500, 0);
  assert(quiet.pick(6) == 6 && quiet.pick(0) == 1);
  // Halved but under 5 points: stays.
  Survey low;
  low.heard(1, 10000, 1000, 1); low.heard(6, 40000, 1000, 1); low.heard(11, 40000, 1000, 1);
  assert(low.pick(6) == 6);

  // A channel not listened to never wins; other channels are ignored.
  Survey part;
  part.heard(6, 50000, 500, 1); part.heard(3, 0, 500, 0);
  assert(part.busyPermille(1) == 1000 && part.busyPermille(3) == 1000 && part.pick(6) == 6);
  // Saturated air is capped at 100 %.
  Survey full; full.heard(1, 900000, 500, 1);
  assert(full.busyPermille(1) == 1000);

  // Watch: two busy checks in a row ask for a survey; a quiet one resets.
  Watch w;
  assert(!w.check(50) && !w.check(150) && w.check(200));
  assert(!w.check(150) && !w.check(20) && !w.check(150) && w.check(400));

  std::puts("chanpick tests passed");
  return 0;
}
