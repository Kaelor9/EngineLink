#include "psram.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <multi_heap.h>
#include <string.h>

static const size_t POOL_BYTES = 2u * 1024u * 1024u;  // the rings, the queue and the web buffers need about 0.5 MB
static const size_t SMALLEST = 256;                    // free pieces below this are left alone
static const uint8_t SPARE_MAX = 24;

static multi_heap_handle_t pool = nullptr;
static uint8_t *poolStart = nullptr;
static size_t poolSize = 0;
static portMUX_TYPE poolMux = portMUX_INITIALIZER_UNLOCKED;
static void *spare[SPARE_MAX] = {};
static SemaphoreHandle_t spareMx = nullptr;
static volatile bool lent = false;
static uint32_t lastService = 0, reliefUntil = 0;
static volatile uint32_t reliefCount = 0;

// Takes whatever PSRAM is free into the spare list, largest piece first.
static void takeSpare() {
  for (uint8_t i = 0; i < SPARE_MAX; i++) {
    if (spare[i]) continue;
    size_t n = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    if (n < SMALLEST) return;
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    if (!p && n > 2 * SMALLEST) p = heap_caps_malloc(n - SMALLEST, MALLOC_CAP_SPIRAM);  // room for the block header
    if (!p) return;
    spare[i] = p;
  }
}

void elPsBegin() {
  if (pool || !psramFound()) return;
  spareMx = xSemaphoreCreateMutex();
  size_t n = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  if (n > POOL_BYTES) n = POOL_BYTES;
  if (n >= 64 * 1024) poolStart = (uint8_t *)heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
  if (poolStart) { poolSize = n; pool = multi_heap_register(poolStart, n); }
  takeSpare();
}

void *elPsAlloc(size_t bytes) {
  if (!pool || !bytes) return nullptr;
  portENTER_CRITICAL(&poolMux);
  void *p = multi_heap_malloc(pool, bytes);
  portEXIT_CRITICAL(&poolMux);
  return p;
}

void elPsFree(void *p) {
  if (!p) return;
  if (pool && (uint8_t *)p >= poolStart && (uint8_t *)p < poolStart + poolSize) {
    portENTER_CRITICAL(&poolMux);
    multi_heap_free(pool, p);
    portEXIT_CRITICAL(&poolMux);
    return;
  }
  free(p);
}

void elPsService() {
  if (!pool || (uint32_t)(millis() - lastService) < 2000) return;
  lastService = millis();
  // Safety valve: internal memory nearly gone (largest free piece under
  // 12 KB) -> the spare PSRAM goes back to the system for a minute. Slower
  // Wi-Fi is better than a failed allocation. Counted for the report.
  if (!lent && heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < 12 * 1024) {
    elPsLend(true); reliefUntil = millis() + 60000UL; reliefCount = reliefCount + 1; return;
  }
  if (reliefUntil && (int32_t)(millis() - reliefUntil) >= 0) { reliefUntil = 0; elPsLend(false); }
  if (lent) return;
  if (heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) < SMALLEST) return;
  if (xSemaphoreTake(spareMx, 0) != pdTRUE) return;
  if (!lent) takeSpare();
  xSemaphoreGive(spareMx);
}

void elPsLend(bool lend) {   // also used by the safety valve above
  if (!pool) return;
  xSemaphoreTake(spareMx, portMAX_DELAY);
  lent = lend;
  if (lend) for (uint8_t i = 0; i < SPARE_MAX; i++) { if (spare[i]) { heap_caps_free(spare[i]); spare[i] = nullptr; } }
  xSemaphoreGive(spareMx);
}

uint32_t elPsPoolFree() {
  if (!pool) return 0;
  portENTER_CRITICAL(&poolMux);
  const size_t n = multi_heap_free_size(pool);
  portEXIT_CRITICAL(&poolMux);
  return (uint32_t)n;
}
uint32_t elPsSystemFree() { return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM); }
uint32_t elPsReliefCount() { return reliefCount; }
