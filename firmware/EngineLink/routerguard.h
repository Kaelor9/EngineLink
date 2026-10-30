#pragma once
// Safety guard for a dual-mode Wi-Fi radio: after a sustained failed router
// association the firmware must return to a standalone access point.
// Pure C++ so the timing logic can be regression tested without hardware.
#include <stdint.h>
struct RouterGuard {
  static constexpr uint32_t CONNECT_TIMEOUT_MS=45000UL;
  uint32_t disconnectedSince=0;
  void reset(){disconnectedSince=0;}
  void attempt(uint32_t now){disconnectedSince=now?now:1;}
  bool expired(uint32_t now)const{
    return disconnectedSince && (uint32_t)(now-disconnectedSince)>=CONNECT_TIMEOUT_MS;
  }
};
