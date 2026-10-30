#include "../firmware/EngineLink/routerguard.h"
#include <cassert>
#include <cstdio>
int main(){
  RouterGuard g;
  assert(!g.expired(123));
  g.attempt(1000);
  // A router must be given several 20-second connection attempts, not
  // abandoned at the old 45-second threshold.
  static_assert(RouterGuard::CONNECT_TIMEOUT_MS==120000UL, "Router recovery window changed");
  assert(!g.expired(1000+45000UL));
  assert(!g.expired(1000+RouterGuard::CONNECT_TIMEOUT_MS-1));
  assert(g.expired(1000+RouterGuard::CONNECT_TIMEOUT_MS));
  g.reset();assert(!g.expired(100000));
  g.attempt(0);
  assert(!g.expired(1000));
  assert(g.expired(1+RouterGuard::CONNECT_TIMEOUT_MS));
  // The 32-bit Arduino millis() clock wraps after 49.7 days.
  g.attempt(0xFFFFFFF0UL);
  assert(!g.expired(0x10UL));
  assert(g.expired((uint32_t)(0xFFFFFFF0UL+RouterGuard::CONNECT_TIMEOUT_MS)));
  g.reset();assert(!g.expired(0xFFFFFFFFUL));
  std::puts("Router safe-AP fallback: timeout, reset and millis wrap OK");
}
