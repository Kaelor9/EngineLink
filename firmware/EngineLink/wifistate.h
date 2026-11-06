// Wi-Fi 2.0 product logic without Arduino (docs/WIFI_REWRITE_PLAN.md 6.3, 9.3):
// the trace folding and the one product state machine (Standalone, Router
// test, Router join, Router, Router lost). Host-tested by
// tools/test_tracefold.cpp and tools/test_wifistate.cpp: no Arduino headers,
// no radio calls, no clock reads. The firmware feeds it events and the time
// and performs its outputs (radio, look, save) in serviceNetwork().
#pragma once
#include <stdint.h>

namespace wifistate {

// ---- Trace folding (plan F2, Stage 1c) ----
constexpr uint8_t TRACE_DISCONNECTED = 4;   // traceText() in wifi.ino
constexpr uint8_t REASON_LEAVE = 36;        // the core's own disconnect() before a new connect()
constexpr uint32_t FOLD_MS = 50;
// Folds a new Wi-Fi event into the newest trace entry. E: ms, lastMs, count,
// type, reason (RouterTrace). The same event again: count + 1. A reason-36
// disconnect within 50 ms after another disconnect: the core's own
// disconnect() before its next connect() (STA.cpp), part of that entry.
// Returns true when folded (nothing new to add).
template <class E>
bool traceFold(E &last, uint8_t type, uint8_t reason, uint32_t now) {
  if (last.type == type && last.reason == reason) {
    last.lastMs = now;
    if (last.count < 65535) ++last.count;
    return true;
  }
  return type == TRACE_DISCONNECTED && reason == REASON_LEAVE && last.type == TRACE_DISCONNECTED &&
         last.reason != REASON_LEAVE && (uint32_t)(now - last.lastMs) <= FOLD_MS;
}

// ---- The state machine (plan 6.3) ----
enum State : uint8_t { STANDALONE, ROUTER_TEST, ROUTER_JOIN, ROUTER, ROUTER_LOST };
enum TestKind : uint8_t { TEST_NONE, TEST_SETUP, TEST_RETURN };
enum TestResult : uint8_t { TR_NONE, TR_TESTING, TR_OK, TR_WRONG_CODE, TR_NOT_FOUND, TR_TIMEOUT };
enum Radio : uint8_t { RADIO_APSTA_IDLE, RADIO_APSTA_JOIN, RADIO_STA };
enum Look : uint8_t { LOOK_SKIP, LOOK_CHANNEL, LOOK_ALL };
enum Ev : uint8_t { EV_AP_START, EV_AP_STOP, EV_AP_STA_JOIN, EV_AP_STA_LEAVE, EV_AP_STA_IP,
                    EV_STA_CONNECTED, EV_STA_GOT_IP, EV_STA_LOST_IP, EV_STA_DISCONNECTED };

// Fixed timings (D7: no settings).
constexpr uint32_t BOOT_JOIN_MS = 180000;      // D5
constexpr uint32_t LOST_MS = 120000;           // D4
constexpr uint32_t TEST_MS = 20000;            // P5
constexpr uint32_t LOOK_MS = 60000;
constexpr uint32_t LOOK_AFTER_FAIL_MS[2] = {300000, 900000};
constexpr uint8_t LOOK_FULL_EVERY = 5;

inline bool due(uint32_t now, uint32_t at) { return (int32_t)(now - at) >= 0; }   // safe across the 49-day clock wrap

// Which look is due: with a phone joined only the known channel (~0.15 s off
// the AP's channel), never all; without a phone every fifth look, and every
// look while no channel is known, scans all channels (the router may move).
inline Look lookKind(uint32_t lookNo, uint8_t knownChannel, bool phoneJoined) {
  if (phoneJoined) return knownChannel ? LOOK_CHANNEL : LOOK_SKIP;
  if (!knownChannel || (lookNo + 1) % LOOK_FULL_EVERY == 0) return LOOK_ALL;
  return LOOK_CHANNEL;
}

struct Machine {
  // inputs, set by the firmware before each call
  bool routerSaved = false, phoneJoined = false, saeAvailable = true, setupWpa3Only = false, userScan = false;
  uint8_t knownChannel = 0;
  // outputs
  State state = STANDALONE; Radio radio = RADIO_APSTA_IDLE; bool radioDirty = false;
  TestKind testKind = TEST_NONE; TestResult testResult = TR_NONE; uint32_t testMs = 0; uint8_t testReason = 0;
  bool allowSae = false, saeTried = false, lastJoinUsedSae = false, returnOkFromApp = false, autoReturn = true;
  uint8_t authFails = 0;
  bool wantLook = false, lookActive = false; Look lookKindNow = LOOK_SKIP;
  bool saveCreds = false, routerBackSeen = false, staDisconnect = false, forgetCreds = false;
  uint32_t looks = 0, looksSeen = 0, returns = 0, returnsFailed = 0, lostEvents = 0;
  // timers
  uint32_t since = 0, lostAt = 0, testStart = 0, nextLookAt = 0, lookEvery = LOOK_MS, lookNo = 0;
  uint8_t failStep = 0;

  void go(uint32_t now, State s, Radio r) {
    state = s; since = now;
    if (r != radio) { radio = r; radioDirty = true; }
    if (s == STANDALONE) nextLookAt = now + lookEvery;
  }
  void boot(uint32_t now, bool saved, bool rapidBoots) {
    routerSaved = saved; radioDirty = true; lookEvery = LOOK_MS;
    if (rapidBoots) autoReturn = false;   // D9: this boot stays on the own Wi-Fi unless the app says "Switch now"
    if (saved && !rapidBoots) { radio = RADIO_STA; go(now, ROUTER_JOIN, RADIO_STA); }
    else { radio = RADIO_APSTA_IDLE; go(now, STANDALONE, RADIO_APSTA_IDLE); }
  }
  void startTest(uint32_t now, TestKind k, bool sae, bool fromApp) {
    testKind = k; testResult = TR_TESTING; testStart = now; testMs = 0; testReason = 0;
    allowSae = sae; saeTried = false; authFails = 0; returnOkFromApp = fromApp;
    if (k == TEST_RETURN) ++returns;
    go(now, ROUTER_TEST, RADIO_APSTA_JOIN);
  }
  void endTest(uint32_t now, TestResult r) {
    testResult = r; testMs = now - testStart;
    if (testKind == TEST_RETURN) {   // a failed return spaces the looks out: 5, then 15 min
      ++returnsFailed;
      lookEvery = LOOK_AFTER_FAIL_MS[failStep];
      if (failStep < 1) ++failStep;
    }
    go(now, STANDALONE, RADIO_APSTA_IDLE);
  }
  void routerUp(uint32_t now) {   // an IP from the router
    lastJoinUsedSae = allowSae; lookEvery = LOOK_MS; failStep = 0; routerBackSeen = false;
    go(now, ROUTER, RADIO_STA);
  }
  void forget(uint32_t now) {
    routerSaved = false; lastJoinUsedSae = false; routerBackSeen = false; forgetCreds = true;
    if (state != STANDALONE) go(now, STANDALONE, RADIO_APSTA_IDLE);
  }

  void event(uint32_t now, uint8_t ev, uint8_t reason) {
    switch (state) {
      case ROUTER_JOIN:
        if (ev == EV_STA_GOT_IP) routerUp(now);
        else if (ev == EV_STA_DISCONNECTED && (reason == 210 || reason == 211) && !saeTried && saeAvailable) {
          saeTried = true; allowSae = true; radioDirty = true;   // D10: WPA3 only for a WPA3-only router
        }
        break;
      case ROUTER:
        if (ev == EV_STA_DISCONNECTED || ev == EV_STA_LOST_IP) { state = ROUTER_LOST; since = lostAt = now; ++lostEvents; }
        break;
      case ROUTER_LOST:
        if (ev == EV_STA_GOT_IP) routerUp(now);
        break;
      case STANDALONE:
        if (ev == EV_STA_GOT_IP) {   // late IP from an attempt that was given up
          if (!phoneJoined && autoReturn && routerSaved) routerUp(now);
          else { staDisconnect = true; routerBackSeen = routerSaved; }   // D21: never cut a joined phone off
        }
        break;
      case ROUTER_TEST:
        if (ev == EV_STA_GOT_IP) {
          testResult = TR_OK; testMs = now - testStart;
          if (testKind == TEST_SETUP) { saveCreds = true; routerSaved = true; }
          routerUp(now);
        } else if (ev == EV_STA_DISCONNECTED) {
          testReason = reason;
          if (reason == 210 || reason == 211) {
            if (!saeTried && saeAvailable) { saeTried = true; allowSae = true; radioDirty = true; }
            else endTest(now, TR_WRONG_CODE);   // "Router security not supported"
          } else if (reason == 15 || (reason >= 202 && reason <= 205)) {
            // one auth failure is not proof (HANDOVER: 202 does not prove a wrong password);
            // the core's own first-connect retry supplies the second attempt
            if (++authFails >= 2) endTest(now, TR_WRONG_CODE);
          } else if (reason == 201) endTest(now, TR_NOT_FOUND);
        } else if (ev == EV_AP_STA_JOIN && testKind == TEST_RETURN && !returnOkFromApp) {
          testResult = TR_NONE; routerBackSeen = true;   // D21: a phone arrived, stay; no penalty
          go(now, STANDALONE, RADIO_APSTA_IDLE);
        }
        break;
    }
  }
  void scanResult(uint32_t now, bool seen, uint8_t channel) {
    lookActive = false; wantLook = false;
    if (state != STANDALONE) return;
    if (!seen) { routerBackSeen = false; return; }
    ++looksSeen;
    if (channel) knownChannel = channel;
    if (!phoneJoined && autoReturn) startTest(now, TEST_RETURN, lastJoinUsedSae, false);
    else routerBackSeen = true;   // D6/D21: the app offers "Switch now"
  }
  void cmdConnect(uint32_t now) {
    if (state != STANDALONE) return;   // the handler answers 409
    lookEvery = LOOK_MS; failStep = 0;
    startTest(now, TEST_SETUP, setupWpa3Only, true);
  }
  void cmdForget(uint32_t now) { forget(now); }
  void cmdReturn(uint32_t now) {
    if (state == STANDALONE && routerBackSeen && routerSaved) { routerBackSeen = false; startTest(now, TEST_RETURN, lastJoinUsedSae, true); }
  }
  void tick(uint32_t now) {
    switch (state) {
      case ROUTER_JOIN: if (now - since >= BOOT_JOIN_MS) go(now, STANDALONE, RADIO_APSTA_IDLE); break;
      case ROUTER_LOST: if (now - lostAt >= LOST_MS) go(now, STANDALONE, RADIO_APSTA_IDLE); break;
      case ROUTER_TEST: if (now - testStart >= TEST_MS) endTest(now, authFails ? TR_WRONG_CODE : TR_TIMEOUT); break;
      case STANDALONE:
        if (!routerSaved || lookActive || !due(now, nextLookAt)) break;
        nextLookAt += lookEvery;
        if (due(now, nextLookAt)) nextLookAt = now + lookEvery;   // never a burst of overdue looks
        if (userScan) break;                                       // a due look is skipped while a user scan runs
        lookKindNow = lookKind(lookNo, knownChannel, phoneJoined);
        if (lookKindNow == LOOK_SKIP) break;
        ++lookNo; ++looks; wantLook = true; lookActive = true;
        break;
      default: break;
    }
  }
  uint32_t nextLookInMs(uint32_t now) const {
    if (state != STANDALONE || !routerSaved) return 0;
    return due(now, nextLookAt) ? 0 : nextLookAt - now;
  }
};

}  // namespace wifistate
