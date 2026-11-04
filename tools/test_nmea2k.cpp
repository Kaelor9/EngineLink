// Host test for nmea2k.h: NMEA 2000 decoding, fast packets and the census.
#include "nmea2k.h"
#include <cassert>
#include <cstdio>
#include <cmath>
using namespace n2k;
static bool near(float a, float b, float e) { return fabsf(a - b) <= e; }
static void le16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void le32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = v >> (8 * i); }
int main() {
  Nav n; const uint32_t T = 5000;
  // PGN extraction: 129026 from source 0x23 (PDU2) and a PDU1 message with a destination
  assert(pgnOf(0x09F80223) == 129026 && srcOf(0x09F80223) == 0x23);
  assert(pgnOf(0x0CEA2523) == 59904);
  // 129025 position 55.6761, 12.5683
  { uint8_t d[8]; le32(d, (uint32_t)(int32_t)556761000); le32(d + 4, (uint32_t)(int32_t)125683000);
    assert(decode(129025, d, 8, T, n)); assert(near(n.lat.v, 55.6761f, 1e-4f) && near(n.lon.v, 12.5683f, 1e-4f)); }
  // 129026 COG 90°, SOG 5 m/s = 9.72 kn
  { uint8_t d[8] = {0, 0}; le16(d + 2, 15708); le16(d + 4, 500); d[6] = d[7] = 0xFF;
    assert(decode(129026, d, 8, T, n)); assert(near(n.cog.v, 90, .1f) && near(n.sog.v, 9.719f, .01f)); }
  // not available values leave the old value untouched
  { uint8_t d[8] = {0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; Nav m; decode(129026, d, 8, T, m); assert(!fresh(m.sog, T) && isnan(m.sog.v)); }
  // 127250 heading 180° magnetic
  { uint8_t d[8] = {0}; le16(d + 1, 31416); le16(d + 3, 0x7FFF); le16(d + 5, 0x7FFF); d[7] = 0xFD;
    assert(decode(127250, d, 8, T, n)); assert(near(n.hdg.v, 180, .1f) && n.hdgMag); }
  // 128267 depth 12.34 m with +0.5 m offset
  { uint8_t d[8] = {0}; le32(d + 1, 1234); le16(d + 5, 500); d[7] = 0xFF;
    assert(decode(128267, d, 8, T, n)); assert(near(n.depth.v, 12.84f, .005f)); }
  // 128259 speed through water 3 m/s
  { uint8_t d[8] = {0}; le16(d + 1, 300); assert(decode(128259, d, 8, T, n)); assert(near(n.stw.v, 5.83f, .01f)); }
  // 130310 water 288.15 K = 15 °C
  { uint8_t d[8] = {0}; le16(d + 1, 28815); le16(d + 3, 0xFFFF); assert(decode(130310, d, 8, T, n)); assert(near(n.waterC.v, 15, .01f)); }
  // 130312 air temperature (source 1) 293.15 K
  { uint8_t d[8] = {0, 0, 1}; le16(d + 3, 29315); assert(decode(130312, d, 8, T, n)); assert(near(n.airC.v, 20, .01f)); }
  // 127508 battery 0: 12.8 V, -3.5 A
  { uint8_t d[8] = {0}; le16(d + 1, 1280); le16(d + 3, (uint16_t)(int16_t)-35); assert(decode(127508, d, 8, T, n)); assert(near(n.battV[0].v, 12.8f, .001f) && near(n.battA[0].v, -3.5f, .01f)); }
  // 127245 rudder 10° to starboard
  { uint8_t d[8] = {0}; le16(d + 4, 1745); assert(decode(127245, d, 8, T, n)); assert(near(n.rudder.v, 10, .05f)); }
  // 127505 fuel tank 1 (type 0) at 62.5 %
  { uint8_t d[8] = {0x01}; le16(d + 1, 15625); assert(decode(127505, d, 8, T, n)); assert(near(n.tankPct[1].v, 62.5f, .01f) && n.tankType[1] == 0); }
  // 127488 engine 1 at 2,000 rpm
  { uint8_t d[8] = {1}; le16(d + 1, 8000); assert(decode(127488, d, 8, T, n)); assert(near(n.engRpm[1].v, 2000, .1f)); }
  // fast packet 128275: 14 bytes in two frames; log 1852 m = 1 nm, trip 926 m
  { FastPool fp; uint8_t p[14]; memset(p, 0xFF, 6); le32(p + 6, 1852); le32(p + 10, 926);
    uint8_t f0[8] = {0x40, 14}; memcpy(f0 + 2, p, 6); uint8_t f1[8] = {0x41}; memcpy(f1 + 1, p + 6, 7); uint8_t f2[8] = {0x42}; memcpy(f2 + 1, p + 13, 1);
    assert(!fp.feed(128275, 7, f0, 8, T)); assert(!fp.feed(128275, 7, f1, 8, T)); const Fast *m = fp.feed(128275, 7, f2, 2, T);
    assert(m && m->got == 14); assert(decode(128275, m->buf, m->got, T, n)); assert(near(n.logNm.v, 1, 1e-4f) && near(n.tripNm.v, .5f, 1e-4f));
    // a lost middle frame drops the message
    FastPool q; assert(!q.feed(128275, 7, f0, 8, T)); assert(!q.feed(128275, 7, f2, 2, T)); assert(!q.feed(128275, 7, f1, 8, T)); }
  // 126992 time: 2026-11-04 12:00:00 UTC
  { uint8_t d[8] = {0, 0}; le16(d + 2, 20761); le32(d + 4, 432000000UL); assert(decode(126992, d, 8, T, n)); assert(n.utc == (int64_t)20761 * 86400 + 43200); }
  // freshness: 7 s
  assert(fresh(n.sog, T + 6999) && !fresh(n.sog, T + 7000));
  // census and address claim: Raymarine, manufacturer code 1851
  { Census c; c.note(128267, 5, T); c.note(128267, 5, T + 1000); c.note(127250, 5, T); assert(c.n == 2 && c.e[0].count == 2);
    uint8_t d[8] = {0}; uint32_t lo = (1851u << 21) | 12345; le32(d, lo); d[5] = 130; c.claim(5, d, 8); assert(c.maker[5] == 1851 && c.func[5] == 130);
    assert(makerName(1851) && pgnName(128267)); for (int i = 0; i < 60; i++) c.note(200000 + i, 9, T); assert(c.n == Census::CAP && c.dropped > 0); }
  std::puts("NMEA 2000: decoding, fast packets, census OK");
}
