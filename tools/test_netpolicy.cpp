// Host test for netpolicy.h: router reconnect back-off and rescue return rules.
#include "netpolicy.h"
#include <cassert>
#include <cstdio>
using namespace netpolicy;

int main() {
  // Back-off: 1, 2, 4, 8, then 15 s for ever (never 0, never longer).
  assert(retryDelayMs(0) == 1000);
  assert(retryDelayMs(1) == 2000);
  assert(retryDelayMs(2) == 4000);
  assert(retryDelayMs(3) == 8000);
  assert(retryDelayMs(4) == 15000);
  assert(retryDelayMs(65535) == 15000);

  // A router that is away for 50 s (field report #126): count the tries.
  // The old Arduino loop tried every 1.6 s (31 tries); now about 7.
  {
    uint32_t t = 0; uint16_t fails = 0; int tries = 0;
    while (t < 50000) { t += retryDelayMs(fails) + 1600; ++fails; ++tries; }
    assert(tries <= 8);
    // ... and it is found again at most 15 s + one scan after it is back.
    assert(retryDelayMs(fails) + 1600 <= 16600);
  }

  // A router that never answers: 120 s rescue window still gets several tries.
  {
    uint32_t t = 0; uint16_t fails = 0; int tries = 0;
    while (t < 120000) { t += retryDelayMs(fails) + 1600; ++fails; ++tries; }
    assert(tries >= 8);
  }

  // Probe interval: every minute; doubling to at most 15 min after failed returns.
  assert(nextProbeEvery(0, false) == 60000);
  assert(nextProbeEvery(900000, false) == 60000);
  assert(nextProbeEvery(60000, true) == 120000);
  assert(nextProbeEvery(120000, true) == 240000);
  uint32_t p = 60000;
  for (int i = 0; i < 20; i++) p = nextProbeEvery(p, true);
  assert(p == 900000);
  assert(nextProbeEvery(0, true) == 120000);

  // Short look on the known channel, all channels every fifth time or when unknown.
  assert(probeKind(1, 6, false) == PROBE_CHANNEL && probeKind(4, 11, false) == PROBE_CHANNEL);
  assert(probeKind(5, 6, false) == PROBE_ALL && probeKind(10, 6, false) == PROBE_ALL);
  assert(probeKind(1, 0, false) == PROBE_ALL && probeKind(3, 14, false) == PROBE_ALL);
  // App in use: never the long all-channel look when the channel is known,
  // and without a known channel only every fifth minute.
  for (uint32_t n = 1; n <= 20; n++) assert(probeKind(n, 6, true) == PROBE_CHANNEL);
  assert(probeKind(1, 0, true) == PROBE_SKIP && probeKind(4, 0, true) == PROBE_SKIP);
  assert(probeKind(5, 0, true) == PROBE_ALL);

  // Return: when the app is idle, or after 10 min unless the last return failed.
  assert(mayReturn(UINT32_MAX, 0, false));
  assert(mayReturn(60000, 0, true));
  assert(!mayReturn(1000, 60000, false));
  assert(!mayReturn(1000, 599999, false));
  assert(mayReturn(1000, 600000, false));
  assert(!mayReturn(1000, 3600000, true));  // wrong password: never cut the phone off

  std::puts("Network policy: back-off, probes and rescue return OK");
}
