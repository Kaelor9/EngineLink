// Folding of repeated Wi-Fi events in the trace (docs/WIFI_PLAN_2026-11-09.md).
// Pure C++: no Arduino headers, no radio calls, no clock reads. Host-tested by
// tools/test_tracefold.cpp; wifi.ino's traceWifi() feeds it every event.
#pragma once
#include <stdint.h>

namespace tracefold {

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

}  // namespace tracefold
