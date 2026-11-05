// Wi-Fi 2.0 product logic without Arduino (docs/WIFI_REWRITE_PLAN.md 6.3).
// Host-tested (tools/test_tracefold.cpp): no Arduino headers, no radio calls, no clock reads.
#pragma once
#include <stdint.h>

namespace wifistate {

// Trace event types (traceText() in wifi.ino).
constexpr uint8_t TR_DISCONNECTED = 4;
constexpr uint8_t REASON_LEAVE = 36;   // the core's own disconnect() before a new connect()
constexpr uint32_t FOLD_MS = 50;

// Folds a new Wi-Fi event into the newest trace entry (plan F2, Stage 1c).
// E is any struct with ms, lastMs, count, type, reason (RouterTrace).
//  * the same type and reason again: one entry, count + 1;
//  * a DISCONNECTED with reason 36 within 50 ms after a DISCONNECTED with
//    another reason: the core's own disconnect() before its next connect()
//    (STA.cpp), so it belongs to that entry and is not counted.
// Returns true when the event was folded (nothing new to add).
template <class E>
bool traceFold(E &last, uint8_t type, uint8_t reason, uint32_t now) {
  if (last.type == type && last.reason == reason) {
    last.lastMs = now;
    if (last.count < 65535) ++last.count;
    return true;
  }
  if (type == TR_DISCONNECTED && reason == REASON_LEAVE && last.type == TR_DISCONNECTED &&
      last.reason != REASON_LEAVE && (uint32_t)(now - last.lastMs) <= FOLD_MS) {
    return true;
  }
  return false;
}

}  // namespace wifistate
