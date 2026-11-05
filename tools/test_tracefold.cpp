// Host test for wifistate::traceFold (Wi-Fi 2.0 plan Stage 1c, finding F2).
// g++ -std=c++17 -Wall -Wextra -I firmware/EngineLink tools/test_tracefold.cpp
#include <cassert>
#include <cstdio>
#include "wifistate.h"

struct Entry { uint32_t ms, lastMs; uint16_t count; uint8_t type, reason; };

// A tiny ring like wifi.ino's traceWifi(): fold or append.
struct Ring {
  Entry e[64]; int n = 0;
  void add(uint8_t type, uint8_t reason, uint32_t now) {
    if (n && wifistate::traceFold(e[n - 1], type, reason, now)) return;
    e[n++] = {now, now, 1, type, reason};
  }
};

int main() {
  using wifistate::TR_DISCONNECTED;
  {  // the plan's case: 201,36,201,36 at 0,0,1574,1574 ms -> 1 entry, count 2
    Ring r;
    r.add(TR_DISCONNECTED, 201, 0); r.add(TR_DISCONNECTED, 36, 0);
    r.add(TR_DISCONNECTED, 201, 1574); r.add(TR_DISCONNECTED, 36, 1574);
    assert(r.n == 1 && r.e[0].count == 2 && r.e[0].reason == 201 && r.e[0].lastMs == 1574);
  }
  {  // two minutes of the core's retry (every ~1.57 s): still one entry
    Ring r;
    for (uint32_t t = 0; t < 120000; t += 1574) { r.add(TR_DISCONNECTED, 201, t); r.add(TR_DISCONNECTED, 36, t + 1); }
    assert(r.n == 1 && r.e[0].count == 77);
  }
  {  // 36 more than 50 ms later is its own event
    Ring r;
    r.add(TR_DISCONNECTED, 201, 0); r.add(TR_DISCONNECTED, 36, 51);
    assert(r.n == 2 && r.e[1].reason == 36);
  }
  {  // 36 after a different event type is not folded
    Ring r;
    r.add(3, 0, 0); r.add(TR_DISCONNECTED, 36, 1);
    assert(r.n == 2);
  }
  {  // 36 alone, then 36 again: plain repeat
    Ring r;
    r.add(TR_DISCONNECTED, 36, 0); r.add(TR_DISCONNECTED, 36, 5000);
    assert(r.n == 1 && r.e[0].count == 2);
  }
  {  // a different reason starts a new entry; the first cause stays visible
    Ring r;
    r.add(TR_DISCONNECTED, 200, 0); r.add(TR_DISCONNECTED, 36, 2);
    r.add(TR_DISCONNECTED, 201, 1600); r.add(TR_DISCONNECTED, 36, 1601);
    assert(r.n == 2 && r.e[0].reason == 200 && r.e[1].reason == 201);
  }
  {  // millis() wrap: 0xFFFFFFF0 -> 0x10 is 32 ms
    Ring r;
    r.add(TR_DISCONNECTED, 201, 0xFFFFFFF0u); r.add(TR_DISCONNECTED, 36, 0x10);
    assert(r.n == 1);
  }
  {  // count saturates
    Ring r;
    for (int i = 0; i < 70000; i++) r.add(7, 0, (uint32_t)i);
    assert(r.n == 1 && r.e[0].count == 65535);
  }
  std::puts("traceFold: all tests passed");
  return 0;
}
