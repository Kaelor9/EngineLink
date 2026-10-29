// Checks the LED language in firmware/EngineLink/ledpatterns.h:
//   g++ -std=c++17 -Wall -I firmware/EngineLink tools/test_ledpatterns.cpp -o /tmp/t && /tmp/t
// - lit steps that follow each other (also around the loop) never use
//   colours that are easy to confuse, and red never follows green
// - every state looks different from every other state
// - every pattern has a dark moment or is steady, and a sensible cycle length
#include <cstdio>
#include <string>
#include <set>
#include "ledpatterns.h"
using namespace ledlang;

static int failures = 0;
static void check(bool ok, const std::string &what) {
  if (!ok) { printf("FAIL  %s\n", what.c_str()); failures++; }
}
// The look of a pattern: its colours and rhythm, ignoring brightness.
static std::string look(const Pattern &p) {
  std::string s = p.breathe ? "~" : "";
  for (uint8_t i = 0; i < p.n; i++) s += std::string(COLOUR_NAME[p.s[i].c]) + ":" + std::to_string(p.s[i].ms) + " ";
  return s;
}
// The same look with similar colours merged (orange as red, magenta as blue):
// two states must still differ, by rhythm if not by colour.
static std::string blurred(const Pattern &p) {
  std::string s = p.breathe ? "~" : "";
  for (uint8_t i = 0; i < p.n; i++) {
    Colour c = p.s[i].c;
    if (c == ORANGE) c = RED;
    if (c == MAGENTA) c = BLUE;
    s += std::string(COLOUR_NAME[c]) + ":" + std::to_string(p.s[i].ms) + " ";
  }
  return s;
}
static void rules(const Pattern &p) {
  // Lit steps in order, around the loop.
  Colour lit[8]; int n = 0;
  for (uint8_t i = 0; i < p.n; i++) if (p.s[i].c != OFF) lit[n++] = p.s[i].c;
  for (int i = 0; i < n && n > 1; i++) {
    const Colour a = lit[i], b = lit[(i + 1) % n];
    if (a == b) continue;
    check(!similar(a, b), std::string(p.id) + ": " + COLOUR_NAME[a] + " next to " + COLOUR_NAME[b]);
    check(!(a == GREEN && b == RED) && !(a == RED && b == GREEN), std::string(p.id) + ": red and green together");
  }
  const uint32_t c = cycleMs(p);
  check(c >= 200 && c <= 3000, std::string(p.id) + ": cycle " + std::to_string(c) + " ms");
  check(p.level > 0, std::string(p.id) + ": brightness");
}

int main() {
  std::set<std::string> seen, seenBlurred;
  for (int i = 0; i < S_COUNT; i++) {
    const Pattern &p = PATTERNS[i];
    rules(p);
    const std::string l = look(p);
    check(!seen.count(l), std::string(p.id) + " looks like another state");
    seen.insert(l);
    const std::string b = blurred(p);
    check(!seenBlurred.count(b), std::string(p.id) + " differs from another state only by a similar colour");
    seenBlurred.insert(b);
    printf("%-9s %-26s %s\n", p.id, p.name, l.c_str());
  }
  rules(MARK);
  check(!seen.count(look(MARK)), "mark looks like a state");
  // at(): the first step of the alarm is red at its brightness, the gap is off.
  uint32_t rgb; uint8_t lv;
  at(PATTERNS[S_ALARM], 10, rgb, lv);
  check(rgb == 0xFF0000 && lv == PATTERNS[S_ALARM].level, "alarm starts red");
  at(PATTERNS[S_ALARM], 130, rgb, lv);
  check(lv == 0, "alarm gap is dark");
  at(PATTERNS[S_SETUP], 1000, rgb, lv);
  uint8_t peak = lv;
  at(PATTERNS[S_SETUP], 100, rgb, lv);
  check(peak > lv, "setup breathes");
  printf(failures ? "%d FAILED\n" : "all passed\n", failures);
  return failures ? 1 : 0;
}
