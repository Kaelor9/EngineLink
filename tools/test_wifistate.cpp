// Host test for the Wi-Fi 2.0 state machine (docs/WIFI_REWRITE_PLAN.md 9.3,
// scenarios 1-20). Each block names the rule it checks.
// g++ -std=c++17 -Wall -Wextra -I firmware/EngineLink tools/test_wifistate.cpp
#include <cassert>
#include <cstdio>
#include "wifistate.h"

using namespace wifistate;

static const uint32_t S = 1000, MIN = 60000;

// Runs tick() every second from `from` to `to` (inclusive), like the loop.
static void run(Machine &m, uint32_t from, uint32_t to) { for (uint32_t t = from; (int32_t)(to - t) >= 0; t += S) m.tick(t); }
// Lets a due look happen and answers it.
static bool lookAt(Machine &m, uint32_t t, bool seen, uint8_t ch = 6) {
  m.tick(t);
  if (!m.wantLook) return false;
  m.scanResult(t + 150, seen, seen ? ch : 0);
  return true;
}
static Machine bootedStandaloneWithRouter(uint32_t t0 = 0) {   // boot with router, never reached
  Machine m; m.knownChannel = 6; m.boot(t0, true, false); run(m, t0, t0 + 180 * S);
  assert(m.state == STANDALONE);
  return m;
}

int main() {
  {  // 1 boot without a router: Standalone, AP+idle STA
    Machine m; m.boot(0, false, false);
    assert(m.state == STANDALONE && m.radio == RADIO_APSTA_IDLE && m.radioDirty);
    run(m, 0, 10 * MIN); assert(m.looks == 0 && !m.wantLook);   // nothing to look for
  }
  {  // 2 boot with a router, IP at 1.4 s -> Router
    Machine m; m.boot(0, true, false);
    assert(m.state == ROUTER_JOIN && m.radio == RADIO_STA);
    m.event(1400, EV_STA_GOT_IP, 0);
    assert(m.state == ROUTER && m.radio == RADIO_STA);
  }
  {  // 3 boot with a router, nothing for 180 s -> Standalone at >= 180 000, not 120 000 (D5)
    Machine m; m.boot(0, true, false);
    run(m, 0, 179 * S); assert(m.state == ROUTER_JOIN);
    m.tick(120000); assert(m.state == ROUTER_JOIN);
    m.tick(180000); assert(m.state == STANDALONE && m.radio == RADIO_APSTA_IDLE);
  }
  {  // 4 Router -> DISCONNECTED(200) -> Router lost -> IP at +50 s -> Router, lostEvents 1 (D4)
    Machine m; m.boot(0, true, false); m.event(1000, EV_STA_GOT_IP, 0);
    m.event(10 * S, EV_STA_DISCONNECTED, 200);
    assert(m.state == ROUTER_LOST && m.lostEvents == 1);
    run(m, 10 * S, 59 * S); assert(m.state == ROUTER_LOST);
    m.event(60 * S, EV_STA_GOT_IP, 0); assert(m.state == ROUTER && m.lostEvents == 1);
  }
  {  // 5 Router lost for 120 s -> Standalone
    Machine m; m.boot(0, true, false); m.event(1000, EV_STA_GOT_IP, 0);
    m.event(5 * S, EV_STA_LOST_IP, 0);
    m.tick(5 * S + 119 * S); assert(m.state == ROUTER_LOST);
    m.tick(5 * S + 120 * S); assert(m.state == STANDALONE && m.radioDirty);
  }
  {  // 6 looks at 60/120/180 s; lookKind rules
    Machine m; m.knownChannel = 6; m.boot(0, false, false); m.routerSaved = true; m.nextLookAt = 60 * S;
    uint32_t seenAt[3]; int n = 0;
    for (uint32_t t = 0; t <= 180 * S; t += S) { m.tick(t); if (m.wantLook) { seenAt[n++] = t; m.scanResult(t, false, 0); } }
    assert(n == 3 && seenAt[0] == 60 * S && seenAt[1] == 120 * S && seenAt[2] == 180 * S);
    // phone joined: the known channel only, never all; nothing when unknown
    for (uint32_t i = 0; i < 20; i++) { assert(lookKind(i, 6, true) == LOOK_CHANNEL); assert(lookKind(i, 0, true) == LOOK_SKIP); }
    // no phone: all channels every fifth look and whenever the channel is unknown
    for (uint32_t i = 0; i < 20; i++) {
      assert(lookKind(i, 0, false) == LOOK_ALL);
      assert(lookKind(i, 6, false) == ((i + 1) % 5 == 0 ? LOOK_ALL : LOOK_CHANNEL));
    }
  }
  {  // 7 look sees the router, no phone -> RETURN test -> OK -> Router
    Machine m = bootedStandaloneWithRouter();
    uint32_t t = 180 * S; bool looked = false;
    for (; t <= 300 * S && !looked; t += S) looked = lookAt(m, t, true);
    assert(looked && m.state == ROUTER_TEST && m.testKind == TEST_RETURN && m.radio == RADIO_APSTA_JOIN && m.returns == 1);
    m.event(t + 3 * S, EV_STA_GOT_IP, 0);
    assert(m.state == ROUTER && m.testResult == TR_OK && !m.saveCreds);
  }
  {  // 8 look sees the router, phone joined -> stays for 24 h; routerBackSeen; returns 0 (D21)
    Machine m = bootedStandaloneWithRouter(); m.phoneJoined = true;
    uint32_t looks = 0;
    for (uint32_t t = 181 * S; t <= 181 * S + 24u * 60 * MIN; t += S) looks += lookAt(m, t, true);
    assert(looks > 1000 && m.state == STANDALONE && m.routerBackSeen && m.returns == 0);
    uint32_t t = 181 * S + 24u * 60 * MIN + S;
    while (!lookAt(m, t, false)) t += S;   // a later unseen look clears it
    assert(!m.routerBackSeen);
    Machine q = m; q.cmdReturn(t + S); assert(q.state == STANDALONE);   // without it: no change
    while (!lookAt(m, t, true)) t += S;
    assert(m.routerBackSeen);
    m.cmdReturn(t + S);   // "Switch now" -> RETURN -> Router
    assert(m.state == ROUTER_TEST && m.returnOkFromApp && m.returns == 1);
    m.event(t + 5 * S, EV_STA_GOT_IP, 0); assert(m.state == ROUTER);
  }
  {  // 9 RETURN -> 202, 202 -> Standalone, returnsFailed 1, next looks at 300 s then 900 s, never more
    Machine m = bootedStandaloneWithRouter();
    uint32_t t = 180 * S; while (!lookAt(m, t, true)) t += S;
    m.event(t + 2 * S, EV_STA_DISCONNECTED, 202); assert(m.state == ROUTER_TEST);
    m.event(t + 4 * S, EV_STA_DISCONNECTED, 202);
    assert(m.state == STANDALONE && m.testResult == TR_WRONG_CODE && m.returnsFailed == 1);
    const uint32_t end1 = t + 4 * S;
    assert(m.nextLookInMs(end1) == 300 * S);
    t = end1; while (!lookAt(m, t, true)) t += S;
    assert(t == end1 + 300 * S);
    m.event(t + 1 * S, EV_STA_DISCONNECTED, 201);   // second failure
    assert(m.returnsFailed == 2 && m.nextLookInMs(t + S) == 900 * S);
    const uint32_t end2 = t + S;
    t = end2; while (!lookAt(m, t, true)) t += S;
    m.event(t + 1 * S, EV_STA_DISCONNECTED, 201);   // third failure: still 15 min
    assert(m.returnsFailed == 3 && m.nextLookInMs(t + S) == 900 * S);
  }
  {  // 10 SETUP: GOT_IP -> saveCreds; 201 -> NOT_FOUND, no save; two auth failures -> WRONG_CODE
    Machine m; m.boot(0, false, false);
    m.cmdConnect(10 * S); assert(m.state == ROUTER_TEST && m.testKind == TEST_SETUP && m.radio == RADIO_APSTA_JOIN);
    m.event(12 * S, EV_STA_GOT_IP, 0);
    assert(m.state == ROUTER && m.saveCreds && m.routerSaved && m.testResult == TR_OK && m.testMs == 2 * S);
    Machine a; a.boot(0, false, false); a.cmdConnect(0);
    a.event(3 * S, EV_STA_DISCONNECTED, 201);
    assert(a.state == STANDALONE && a.testResult == TR_NOT_FOUND && !a.saveCreds && !a.routerSaved);
    Machine b; b.boot(0, false, false); b.cmdConnect(0);
    b.event(2 * S, EV_STA_DISCONNECTED, 202); assert(b.state == ROUTER_TEST && b.testResult == TR_TESTING);
    b.event(5 * S, EV_STA_DISCONNECTED, 202); assert(b.state == STANDALONE && b.testResult == TR_WRONG_CODE && !b.saveCreds);
    Machine c; c.boot(0, false, false); c.cmdConnect(0);
    c.event(2 * S, EV_STA_DISCONNECTED, 202); c.event(8 * S, EV_STA_GOT_IP, 0);
    assert(c.state == ROUTER && c.testResult == TR_OK && c.saveCreds);
    assert(b.returnsFailed == 0 && b.lookEvery == LOOK_MS);   // a failed SETUP is no return penalty
  }
  {  // 11 DISCONNECTED(8) ignored; timeout at 20 s -> TIMEOUT, with one auth failure -> WRONG_CODE
    Machine m; m.boot(0, false, false); m.cmdConnect(0);
    m.event(1 * S, EV_STA_DISCONNECTED, 8); assert(m.state == ROUTER_TEST);
    m.tick(19 * S); assert(m.state == ROUTER_TEST);
    m.tick(20 * S); assert(m.state == STANDALONE && m.testResult == TR_TIMEOUT);
    Machine w; w.boot(0, false, false); w.cmdConnect(0);
    w.event(1 * S, EV_STA_DISCONNECTED, 15); w.tick(20 * S);
    assert(w.state == STANDALONE && w.testResult == TR_WRONG_CODE);
  }
  {  // 12 rapid boots -> Standalone, autoReturn false; looks only set routerBackSeen; cmdReturn works (D9)
    Machine m; m.knownChannel = 6; m.boot(0, true, true);
    assert(m.state == STANDALONE && !m.autoReturn && m.routerSaved);
    uint32_t t = 0; while (!lookAt(m, t, true)) t += S;
    assert(m.state == STANDALONE && m.routerBackSeen && m.returns == 0);
    m.cmdReturn(t + S); assert(m.state == ROUTER_TEST && m.testKind == TEST_RETURN);
  }
  {  // 13 cmdForget in Router and Router lost -> Standalone, looks stop
    Machine m; m.boot(0, true, false); m.event(S, EV_STA_GOT_IP, 0);
    m.cmdForget(2 * S); assert(m.state == STANDALONE && !m.routerSaved && m.forgetCreds && m.radioDirty);
    run(m, 2 * S, 30 * MIN); assert(m.looks == 0);
    Machine l; l.boot(0, true, false); l.event(S, EV_STA_GOT_IP, 0); l.event(2 * S, EV_STA_DISCONNECTED, 200);
    l.cmdForget(3 * S); assert(l.state == STANDALONE && !l.routerSaved);
    run(l, 3 * S, 30 * MIN); assert(l.looks == 0);
  }
  {  // 14 radioDirty only on radio-changing transitions: boot -> JOIN -> STANDALONE -> TEST -> ROUTER = 3; looks 0
    Machine m; m.knownChannel = 6; m.boot(0, true, false); m.radioDirty = false;
    int dirty = 0;
    auto step = [&](uint32_t t) { m.tick(t); if (m.radioDirty) { ++dirty; m.radioDirty = false; } if (m.wantLook) m.scanResult(t, false, 0); };
    for (uint32_t t = 0; t <= 600 * S; t += S) step(t);   // join fails at 180 s, then looks that see nothing
    assert(m.state == STANDALONE && m.looks >= 5 && dirty == 1);
    m.cmdConnect(601 * S); if (m.radioDirty) { ++dirty; m.radioDirty = false; }
    m.event(605 * S, EV_STA_GOT_IP, 0); if (m.radioDirty) { ++dirty; m.radioDirty = false; }
    assert(m.state == ROUTER && dirty == 3);
    m.event(606 * S, EV_STA_DISCONNECTED, 200); m.event(640 * S, EV_STA_GOT_IP, 0);   // lost and back: the core reconnects
    assert(m.state == ROUTER && !m.radioDirty);
  }
  {  // 15 millis() wrap 0xFFFFFF00 -> 0x100
    const uint32_t t0 = 0xFFFFFF00u;
    Machine m; m.boot(t0, true, false);
    m.tick(0x100); assert(m.state == ROUTER_JOIN);                    // 512 ms later, not "forever"
    m.tick(t0 + BOOT_JOIN_MS); assert(m.state == STANDALONE);
    Machine t; t.boot(0, false, false); t.cmdConnect(t0);
    t.tick(0x100); assert(t.state == ROUTER_TEST);
    t.tick(t0 + TEST_MS); assert(t.state == STANDALONE && t.testResult == TR_TIMEOUT);
    Machine l; l.knownChannel = 6; l.boot(t0 - LOOK_MS, false, false); l.routerSaved = true;
    l.tick(0x100); assert(l.wantLook);                                // look due at t0, seen after the wrap
  }
  {  // 16 stale GOT_IP in Standalone: no phone -> Router; phone joined -> stays + disconnect
    Machine m = bootedStandaloneWithRouter();
    m.event(200 * S, EV_STA_GOT_IP, 0); assert(m.state == ROUTER);
    Machine p = bootedStandaloneWithRouter(); p.phoneJoined = true;
    p.event(200 * S, EV_STA_GOT_IP, 0); assert(p.state == STANDALONE && p.staDisconnect && p.routerBackSeen);
  }
  {  // 17 RETURN without the app's OK: a phone joins at 3 s -> Standalone, no penalty; with it -> ignored
    Machine m = bootedStandaloneWithRouter();
    uint32_t t = 180 * S; while (!lookAt(m, t, true)) t += S;
    const uint32_t every = m.lookEvery;
    m.event(t + 3 * S, EV_AP_STA_JOIN, 0);
    assert(m.state == STANDALONE && m.returnsFailed == 0 && m.lookEvery == every && m.routerBackSeen);
    Machine o = bootedStandaloneWithRouter(); o.phoneJoined = true;
    t = 180 * S; while (!lookAt(o, t, true)) t += S;
    o.cmdReturn(t + S); o.event(t + 3 * S, EV_AP_STA_JOIN, 0);
    assert(o.state == ROUTER_TEST);
  }
  {  // 18 210 -> allowSae, radioDirty, still testing; second 210 -> WRONG_CODE; no SAE -> first 210 is WRONG_CODE (D10)
    Machine m; m.boot(0, false, false); m.cmdConnect(0); m.radioDirty = false;
    m.event(2 * S, EV_STA_DISCONNECTED, 210);
    assert(m.allowSae && m.saeTried && m.radioDirty && m.state == ROUTER_TEST);
    m.event(4 * S, EV_STA_DISCONNECTED, 210); assert(m.state == STANDALONE && m.testResult == TR_WRONG_CODE);
    Machine n; n.saeAvailable = false; n.boot(0, false, false); n.cmdConnect(0);
    n.event(2 * S, EV_STA_DISCONNECTED, 211); assert(n.state == STANDALONE && n.testResult == TR_WRONG_CODE && !n.allowSae);
    Machine j; j.boot(0, true, false); j.radioDirty = false;   // the same at boot join
    j.event(2 * S, EV_STA_DISCONNECTED, 210); assert(j.allowSae && j.radioDirty && j.state == ROUTER_JOIN);
    Machine w; w.setupWpa3Only = true; w.boot(0, false, false); w.cmdConnect(0); assert(w.allowSae);   // WPA3-only from the scan
  }
  {  // 19 look back-off reset after a success
    Machine m = bootedStandaloneWithRouter();
    uint32_t t = 180 * S; while (!lookAt(m, t, true)) t += S;
    m.event(t + S, EV_STA_DISCONNECTED, 201); assert(m.lookEvery == 300 * S);
    t += S; while (!lookAt(m, t, true)) t += S;
    m.event(t + S, EV_STA_GOT_IP, 0); assert(m.state == ROUTER && m.lookEvery == LOOK_MS);
    m.event(t + 2 * S, EV_STA_DISCONNECTED, 200); m.tick(t + 2 * S + LOST_MS);
    assert(m.state == STANDALONE && m.nextLookInMs(t + 2 * S + LOST_MS) == LOOK_MS);
    Machine c = bootedStandaloneWithRouter();   // and after a new /api/wifi/connect
    t = 180 * S; while (!lookAt(c, t, true)) t += S;
    c.event(t + S, EV_STA_DISCONNECTED, 201); assert(c.lookEvery == 300 * S);
    c.cmdConnect(t + 2 * S); assert(c.lookEvery == LOOK_MS);
  }
  {  // 20 lastJoinUsedSae seeds RETURN tests; cmdForget clears it
    Machine m; m.knownChannel = 6; m.boot(0, true, false);
    m.event(S, EV_STA_DISCONNECTED, 210); m.event(3 * S, EV_STA_GOT_IP, 0);
    assert(m.state == ROUTER && m.lastJoinUsedSae);
    m.event(10 * S, EV_STA_DISCONNECTED, 200); m.tick(10 * S + LOST_MS);
    uint32_t t = 10 * S + LOST_MS; while (!lookAt(m, t, true)) t += S;
    assert(m.state == ROUTER_TEST && m.allowSae);
    m.event(t + S, EV_STA_GOT_IP, 0);
    m.cmdForget(t + 2 * S); assert(!m.lastJoinUsedSae && !m.routerSaved);
  }
  {  // extra: a user scan postpones a due look; a look in progress is not doubled
    Machine m = bootedStandaloneWithRouter(); m.userScan = true;
    run(m, 181 * S, 400 * S); assert(m.looks == 0);
    m.userScan = false; uint32_t t = 401 * S; while (!m.wantLook) m.tick(t += S);
    const uint32_t n = m.looks; run(m, t, t + 10 * MIN); assert(m.looks == n);   // no answer yet: no second look
  }
  std::puts("wifistate: all 20 plan scenarios passed");
  return 0;
}
