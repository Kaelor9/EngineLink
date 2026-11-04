#pragma once
// Router reconnect and rescue timing (wifi.ino), without Arduino so the
// rules can be tested on a PC (tools/test_netpolicy.cpp).
//
// Router mode: after every failed or lost connection the station waits a
// little longer before the next try (1, 2, 4, 8, then every 15 s), instead of
// the Arduino core's immediate retry loop. After 120 s without a router IP
// (RouterGuard) EngineLink's own Wi-Fi comes back ("rescue"). Rescue is not
// remembered over a restart: every start in router mode tries the router
// first. While in rescue, EngineLink looks for the router every minute (a
// short look on its last channel; all channels every fifth time) and goes
// back when it is there and nobody is using the app on EngineLink's Wi-Fi.
#include <stdint.h>

namespace netpolicy {

constexpr uint32_t RETRY_MAX_MS = 15000UL;
// A connect attempt that gives no answer (no event) at all is retried.
constexpr uint32_t ATTEMPT_TIMEOUT_MS = 30000UL;
constexpr uint32_t PROBE_EVERY_MS = 60000UL;       // first look, and after a good return
constexpr uint32_t PROBE_MAX_MS = 900000UL;        // after failed returns: at most every 15 min
constexpr uint8_t FULL_SCAN_EVERY = 5;             // every fifth look scans all channels
constexpr uint32_t RETURN_IDLE_MS = 60000UL;       // app unused on EngineLink Wi-Fi this long
constexpr uint32_t RETURN_FORCE_MS = 600000UL;     // ... or rescue has lasted this long

// Wait before connect attempt number `fails` + 1 (fails = failed tries in a row).
inline uint32_t retryDelayMs(uint16_t fails) {
  if (fails == 0) return 1000UL;
  if (fails >= 4) return RETRY_MAX_MS;
  const uint32_t d = 1000UL << fails;                // 2, 4, 8 s
  return d > RETRY_MAX_MS ? RETRY_MAX_MS : d;
}

// Interval between looks for the router while in rescue.
inline uint32_t nextProbeEvery(uint32_t current, bool returnFailed) {
  if (!returnFailed) return PROBE_EVERY_MS;
  if (current < PROBE_EVERY_MS) current = PROBE_EVERY_MS;
  const uint32_t d = current * 2;
  return d > PROBE_MAX_MS ? PROBE_MAX_MS : d;
}

// What the probe number `n` (1, 2, ...) does: skip, a short look on the
// router's last channel, or all channels. While a phone uses the app on
// EngineLink's Wi-Fi only the short look (about 0.1 s away from the phones'
// channel); without a known channel then only every fifth time (about 1.6 s).
enum Probe : uint8_t { PROBE_SKIP, PROBE_CHANNEL, PROBE_ALL };
inline Probe probeKind(uint32_t n, uint8_t knownChannel, bool appInUse) {
  const bool known = knownChannel >= 1 && knownChannel <= 13;
  const bool fifth = n % FULL_SCAN_EVERY == 0;
  if (appInUse) return known ? PROBE_CHANNEL : fifth ? PROBE_ALL : PROBE_SKIP;
  return known && !fifth ? PROBE_CHANNEL : PROBE_ALL;
}

// The router was seen while in rescue: switch back now?
// idleMs: time since the last app request on EngineLink's own Wi-Fi
// (UINT32_MAX when no phone is connected). heldMs: time in rescue.
// A forced return while the app is in use only when the last return worked,
// so a router that refuses EngineLink (wrong password) cannot keep cutting
// the phone off.
inline bool mayReturn(uint32_t idleMs, uint32_t heldMs, bool lastReturnFailed) {
  if (idleMs >= RETURN_IDLE_MS) return true;
  return !lastReturnFailed && heldMs >= RETURN_FORCE_MS;
}

}  // namespace netpolicy
