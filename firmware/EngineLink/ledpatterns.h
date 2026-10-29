#pragma once
// The status LED's language (no Arduino code, checked on a PC by
// tools/test_ledpatterns.cpp, which also enforces the colour rules).
//
// Six colours that are hard to mix up: green, blue, red, orange, white and
// magenta. The rhythm carries the meaning as much as the colour, so the LED
// can be read in sunlight and by people who see red and green alike:
//   steady       everything is fine
//   heartbeat    fine and reading the engines (a short gap every 2 s)
//   fast blink   act now (red: engine alarm, white: update, keep power on)
//   double blink attention (orange: engine warning)
//   triple blink a fault in EngineLink itself (red: engine data not received)
//   two colours  a problem with storage: long red + short blue = SD error,
//                short blue + long orange = no SD card
// Rules: two lit steps next to each other never use similar colours
// (red/orange, blue/magenta), and red never follows green directly.
#include <stdint.h>

namespace ledlang {

enum Colour : uint8_t { OFF = 0, GREEN, BLUE, RED, ORANGE, WHITE, MAGENTA };
constexpr uint32_t RGB[] = {0x000000, 0x00E020, 0x0030FF, 0xFF0000, 0xFF5A00, 0xFFFFFF, 0xC000FF};
constexpr const char *COLOUR_NAME[] = {"off", "green", "blue", "red", "orange", "white", "magenta"};

struct Step { Colour c; uint16_t ms; };
// Breathe: the brightness rises and falls over the step instead of being steady.
struct Pattern {
  const char *id, *name, *meaning;
  uint8_t level;  // brightness 0-255
  bool breathe;
  uint8_t n;
  Step s[8];
};

// In priority order: the first state that applies is shown.
enum State : uint8_t {
  S_UPDATE, S_ALARM, S_SD_ERROR, S_RX_ERROR, S_WARNING, S_NO_SD, S_TEST, S_SETUP, S_ROUTER, S_LIVE, S_READY, S_BOOT, S_COUNT
};
constexpr Pattern PATTERNS[S_COUNT] = {
  {"update", "Updating firmware", "A new version is being installed. Keep the power on.", 60, false, 2,
   {{WHITE, 100}, {OFF, 100}}},
  {"alarm", "Engine alarm", "Something needs attention now, for example the coolant is too hot. See Events in the app.", 110, false, 2,
   {{RED, 120}, {OFF, 120}}},
  {"sd-error", "SD card error", "EngineLink cannot write to the SD card, so raw data and the trip log are not saved.", 60, false, 4,
   {{RED, 700}, {OFF, 200}, {BLUE, 200}, {OFF, 900}}},
  {"rx-error", "Engine data fault", "Data from an engine connector arrives garbled or overflows. Check the J1708 wiring.", 60, false, 6,
   {{RED, 150}, {OFF, 150}, {RED, 150}, {OFF, 150}, {RED, 150}, {OFF, 1250}}},
  {"warning", "Engine warning", "An engine value is outside what is normal for this engine. See Events in the app.", 70, false, 4,
   {{ORANGE, 150}, {OFF, 150}, {ORANGE, 150}, {OFF, 1550}}},
  {"no-sd", "No SD card", "Everything works, but raw engine data and the trip log are not saved.", 50, false, 4,
   {{BLUE, 150}, {OFF, 150}, {ORANGE, 600}, {OFF, 1100}}},
  {"test", "Recording a motor test", "A guided motor test is being recorded.", 50, false, 2,
   {{MAGENTA, 1000}, {OFF, 1000}}},
  {"setup", "Setup not finished", "Join the EngineLink Wi-Fi and open the app to finish the setup.", 50, true, 2,
   {{BLUE, 2000}, {OFF, 400}}},
  {"router", "Boat router not reached", "EngineLink works, but the boat router is not reachable. The EngineLink Wi-Fi is unaffected.", 40, false, 4,
   {{GREEN, 1400}, {OFF, 200}, {BLUE, 200}, {OFF, 400}}},
  {"live", "Reading the engines", "All is well and EngineLink is reading both engines.", 40, false, 2,
   {{GREEN, 1850}, {OFF, 150}}},
  {"ready", "Ready", "All is well. The engines are off or not being read.", 25, false, 1,
   {{GREEN, 2000}}},
  {"boot", "Starting", "EngineLink is starting.", 40, false, 1,
   {{WHITE, 2000}}},
};
// A short sign that the "Something's odd" mark was taken: three magenta flashes.
constexpr Pattern MARK = {"mark", "Mark saved", "The moment you marked was saved with the data around it.", 90, false, 6,
  {{MAGENTA, 100}, {OFF, 100}, {MAGENTA, 100}, {OFF, 100}, {MAGENTA, 100}, {OFF, 500}}};

inline uint32_t cycleMs(const Pattern &p) {
  uint32_t t = 0;
  for (uint8_t i = 0; i < p.n; i++) t += p.s[i].ms;
  return t ? t : 1;
}
// Colour (0xRRGGBB) and brightness at time t (ms) into the pattern.
inline void at(const Pattern &p, uint32_t t, uint32_t &rgb, uint8_t &level) {
  uint32_t x = t % cycleMs(p);
  for (uint8_t i = 0; i < p.n; i++) {
    if (x < p.s[i].ms) {
      rgb = RGB[p.s[i].c];
      level = p.s[i].c == OFF ? 0 : p.level;
      if (p.breathe && p.s[i].c != OFF) {
        // Triangle 15 % .. 100 % over the step.
        const uint32_t half = p.s[i].ms / 2;
        const uint32_t d = x < half ? x : p.s[i].ms - x;
        level = (uint8_t)(p.level * (15 + 85 * d / (half ? half : 1)) / 100);
      }
      return;
    }
    x -= p.s[i].ms;
  }
  rgb = 0; level = 0;
}
// Two colours that are easy to confuse when they follow each other.
inline bool similar(Colour a, Colour b) {
  auto pair = [&](Colour x, Colour y) { return (a == x && b == y) || (a == y && b == x); };
  return pair(RED, ORANGE) || pair(BLUE, MAGENTA) || pair(RED, MAGENTA);
}

}  // namespace ledlang
