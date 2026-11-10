// EngineLink's own use of the PSRAM (0.17.2).
//
// The ESP32 core lets the Wi-Fi driver and lwIP put their packet buffers in
// PSRAM whenever PSRAM has room (CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP) and
// falls back to internal memory when it has none. Espressif advises keeping
// Wi-Fi buffers in internal memory, and the owner's plain sketch that was
// fast on the home router ran without PSRAM. So EngineLink takes all of the
// PSRAM at start: one part is its own pool (engine queue, sample rings, web
// buffers), the rest is held as spare. With no PSRAM left free, the Wi-Fi
// buffers land in internal memory, exactly as in that sketch.
//
// Documented ESP-IDF heap API only (esp_heap_caps.h, multi_heap.h).
#pragma once
#include <stddef.h>
#include <stdint.h>

void elPsBegin();               // first thing in setup(), before the Wi-Fi starts
void *elPsAlloc(size_t bytes);  // from EngineLink's pool; nullptr without PSRAM or when full
void elPsFree(void *p);         // a pool pointer, or any ordinary malloc() pointer
void elPsService();             // main loop: takes back PSRAM that became free
void elPsLend(bool lend);       // the spare part goes to the system for a while (HTTPS to GitHub)
uint32_t elPsPoolFree();        // bytes free in the pool
uint32_t elPsReliefCount();     // times the spare went back because internal memory ran low
uint32_t elPsSystemFree();      // bytes of PSRAM the system could still hand to Wi-Fi (should be near 0)
