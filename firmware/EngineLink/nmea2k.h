#pragma once
// NMEA 2000, listen only: decoding of the standard messages EngineLink can use,
// fast-packet reassembly and a census of what is on the bus. No Arduino, so the
// decoding is tested on a PC (tools/test_nmea2k.cpp). EngineLink never sends on
// NMEA 2000 (the CAN controller runs in listen-only mode).
//
// On the owner's boat (2004 Fairline Targa 34) the instruments are Raymarine
// SeaTalk1 (ST60 Tridata depth/log/trip, ST6001 autopilot, a Raymarine plotter);
// through a SeaTalk1-to-SeaTalkNG converter they appear as the standard PGNs
// below. Anything else that speaks NMEA 2000 is read the same way.
#include <stdint.h>
#include <string.h>
#include <math.h>

namespace n2k {

constexpr uint32_t FRESH_MS = 7000;   // a value older than this is "not known"

struct Val { float v = NAN; uint32_t at = 0; };
inline bool fresh(const Val &x, uint32_t now) { return x.at && !isnan(x.v) && (uint32_t)(now - x.at) < FRESH_MS; }
inline void put(Val &x, float v, uint32_t now) { x.v = v; x.at = now ? now : 1; }

struct Nav {
  Val lat, lon;           // degrees
  Val sog, cog;           // knots, degrees true
  Val hdg; bool hdgMag = true;  // degrees, magnetic unless the source says true
  Val stw;                // speed through water, knots
  Val depth;              // metres below the transducer plus its offset
  Val waterC, airC;       // °C
  Val battV[2], battA[2]; // battery instances 0 and 1
  Val rudder;             // degrees, starboard positive
  Val windKn, windDeg; bool windTrue = false;
  Val tankPct[4]; uint8_t tankType[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  Val engRpm[2];          // from other instruments (PGN 127488), for comparison
  Val logNm, tripNm;      // distance log (PGN 128275)
  Val sats, hdop; uint8_t fixType = 0xFF;
  // time from the bus: seconds since 1970 (0 = none), PGN 126992 or 129029
  int64_t utc = 0; uint32_t utcAt = 0;
};

// ---- little-endian field readers; NA values mean "not available" ----
inline uint16_t u16(const uint8_t *d) { return (uint16_t)(d[0] | (d[1] << 8)); }
inline int16_t s16(const uint8_t *d) { return (int16_t)u16(d); }
inline uint32_t u24(const uint8_t *d) { return (uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16); }
inline uint32_t u32(const uint8_t *d) { return (uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24); }
inline int32_t s32(const uint8_t *d) { return (int32_t)u32(d); }
inline int64_t s64(const uint8_t *d) { uint64_t v = 0; for (int i = 7; i >= 0; i--) v = (v << 8) | d[i]; return (int64_t)v; }
constexpr float RAD = 57.29577951f, MS_KN = 1.94384449f;
inline float k2c(float k) { return k - 273.15f; }

// PGN from a 29-bit identifier (PDU1 messages carry a destination, not part of the PGN)
inline uint32_t pgnOf(uint32_t id) { uint32_t p = (id >> 8) & 0x3FFFF; if (((id >> 16) & 0xFF) < 240) p &= 0x3FF00; return p; }
inline uint8_t srcOf(uint32_t id) { return (uint8_t)(id & 0xFF); }

// Fast-packet PGNs EngineLink reassembles (everything else is single frame).
inline bool isFast(uint32_t pgn) { return pgn == 128275 || pgn == 129029; }

// Decode one complete message (a single frame, or a reassembled fast packet).
// Returns true when the PGN is one EngineLink understands.
inline bool decode(uint32_t pgn, const uint8_t *d, uint8_t len, uint32_t now, Nav &n) {
  switch (pgn) {
  case 129025:  // position, rapid
    if (len < 8) return false;
    if (s32(d) != 0x7FFFFFFF && s32(d + 4) != 0x7FFFFFFF) { put(n.lat, s32(d) * 1e-7f, now); put(n.lon, s32(d + 4) * 1e-7f, now); }
    return true;
  case 129026:  // COG and SOG, rapid
    if (len < 6) return false;
    if (u16(d + 2) != 0xFFFF) put(n.cog, u16(d + 2) * 1e-4f * RAD, now);
    if (u16(d + 4) != 0xFFFF) put(n.sog, u16(d + 4) * 0.01f * MS_KN, now);
    return true;
  case 127250:  // vessel heading
    if (len < 8) return false;
    if (u16(d + 1) != 0xFFFF) { put(n.hdg, u16(d + 1) * 1e-4f * RAD, now); n.hdgMag = (d[7] & 3) == 1; }
    return true;
  case 128259:  // speed through water
    if (len < 3) return false;
    if (u16(d + 1) != 0xFFFF) put(n.stw, u16(d + 1) * 0.01f * MS_KN, now);
    return true;
  case 128267:  // water depth
    if (len < 7) return false;
    if (u32(d + 1) != 0xFFFFFFFF) { float off = s16(d + 5) == 0x7FFF ? 0 : s16(d + 5) * 0.001f; put(n.depth, u32(d + 1) * 0.01f + off, now); }
    return true;
  case 130310:  // environmental (water and air temperature)
    if (len < 5) return false;
    if (u16(d + 1) != 0xFFFF) put(n.waterC, k2c(u16(d + 1) * 0.01f), now);
    if (u16(d + 3) != 0xFFFF) put(n.airC, k2c(u16(d + 3) * 0.01f), now);
    return true;
  case 130311:  // environmental, with a temperature source
    if (len < 4) return false;
    if (u16(d + 2) != 0xFFFF) { uint8_t src = d[1] & 0x3F; if (src == 0) put(n.waterC, k2c(u16(d + 2) * 0.01f), now); else if (src == 1) put(n.airC, k2c(u16(d + 2) * 0.01f), now); }
    return true;
  case 130312:  // temperature
    if (len < 5) return false;
    if (u16(d + 3) != 0xFFFF) { if (d[2] == 0) put(n.waterC, k2c(u16(d + 3) * 0.01f), now); else if (d[2] == 1) put(n.airC, k2c(u16(d + 3) * 0.01f), now); }
    return true;
  case 130316:  // temperature, extended range
    if (len < 6) return false;
    if (u24(d + 3) != 0xFFFFFF) { if (d[2] == 0) put(n.waterC, k2c(u24(d + 3) * 0.001f), now); else if (d[2] == 1) put(n.airC, k2c(u24(d + 3) * 0.001f), now); }
    return true;
  case 127245:  // rudder
    if (len < 6) return false;
    if (s16(d + 4) != 0x7FFF) put(n.rudder, s16(d + 4) * 1e-4f * RAD, now);
    return true;
  case 127508:  // battery status
    if (len < 5) return false;
    if (d[0] < 2) { if (u16(d + 1) != 0xFFFF) put(n.battV[d[0]], u16(d + 1) * 0.01f, now); if (s16(d + 3) != 0x7FFF) put(n.battA[d[0]], s16(d + 3) * 0.1f, now); }
    return true;
  case 130306:  // wind
    if (len < 6) return false;
    if (u16(d + 1) != 0xFFFF) put(n.windKn, u16(d + 1) * 0.01f * MS_KN, now);
    if (u16(d + 3) != 0xFFFF) put(n.windDeg, u16(d + 3) * 1e-4f * RAD, now);
    n.windTrue = (d[5] & 7) != 2;
    return true;
  case 127505: {  // fluid level
    if (len < 3) return false;
    uint8_t inst = d[0] & 0x0F;
    if (inst < 4 && s16(d + 1) != 0x7FFF) { put(n.tankPct[inst], s16(d + 1) * 0.004f, now); n.tankType[inst] = d[0] >> 4; }
    return true; }
  case 127488:  // engine, rapid (other instruments' rpm)
    if (len < 3) return false;
    if (d[0] < 2 && u16(d + 1) != 0xFFFF) put(n.engRpm[d[0]], u16(d + 1) * 0.25f, now);
    return true;
  case 128275:  // distance log (fast packet)
    if (len < 14) return false;
    if (u32(d + 6) != 0xFFFFFFFF) put(n.logNm, u32(d + 6) / 1852.0f, now);
    if (u32(d + 10) != 0xFFFFFFFF) put(n.tripNm, u32(d + 10) / 1852.0f, now);
    return true;
  case 129029: {  // GNSS position (fast packet): fix, satellites, HDOP, date and time
    if (len < 36) return false;
    n.fixType = d[31] >> 4;
    if (d[33] != 0xFF) put(n.sats, d[33], now);
    if (s16(d + 34) != 0x7FFF) put(n.hdop, s16(d + 34) * 0.01f, now);
    uint16_t days = u16(d + 1); uint32_t t = u32(d + 3);
    if (days != 0xFFFF && t != 0xFFFFFFFF && t < 864000000UL) { n.utc = (int64_t)days * 86400 + t / 10000; n.utcAt = now ? now : 1; }
    int64_t la = s64(d + 7), lo = s64(d + 15);
    if (la != INT64_MAX && lo != INT64_MAX) { put(n.lat, (float)(la * 1e-16), now); put(n.lon, (float)(lo * 1e-16), now); }
    return true; }
  case 126992: {  // system time
    if (len < 8) return false;
    uint16_t days = u16(d + 2); uint32_t t = u32(d + 4);
    if (days != 0xFFFF && t != 0xFFFFFFFF && t < 864000000UL) { n.utc = (int64_t)days * 86400 + t / 10000; n.utcAt = now ? now : 1; }
    return true; }
  }
  return false;
}

// Fast-packet reassembly: frame 0 carries the length and 6 bytes, later frames 7.
struct Fast { uint32_t pgn = 0; uint8_t src = 0, seq = 0, next = 0, len = 0, got = 0; uint8_t buf[64]; uint32_t at = 0; };
struct FastPool {
  Fast slot[4];
  // Feed one frame; returns the completed message or nullptr.
  const Fast *feed(uint32_t pgn, uint8_t src, const uint8_t *d, uint8_t dlc, uint32_t now) {
    if (dlc < 2) return nullptr;
    uint8_t seq = d[0] >> 5, idx = d[0] & 0x1F;
    Fast *f = nullptr;
    for (Fast &s : slot) if (s.pgn == pgn && s.src == src && s.seq == seq && s.len) { f = &s; break; }
    if (idx == 0) {
      if (!f) { f = &slot[0]; for (Fast &s : slot) if (!s.len || (uint32_t)(now - s.at) > 1000) { f = &s; break; } }
      f->pgn = pgn; f->src = src; f->seq = seq; f->len = d[1] > sizeof(f->buf) ? sizeof(f->buf) : d[1]; f->got = 0; f->next = 1; f->at = now;
      uint8_t n = dlc - 2; if (n > f->len) n = f->len;
      memcpy(f->buf, d + 2, n); f->got = n;
    } else {
      if (!f || idx != f->next) { if (f) f->len = 0; return nullptr; }   // lost a frame: drop it
      uint8_t n = dlc - 1; if (f->got + n > f->len) n = f->len - f->got;
      memcpy(f->buf + f->got, d + 1, n); f->got += n; f->next++; f->at = now;
    }
    if (f->got >= f->len) { f->len = 0; return f; }
    return nullptr;
  }
};

// Census: which source address sends which PGN, how often, and the maker from
// its address claim (PGN 60928, which every device broadcasts when it joins).
struct Seen { uint32_t pgn = 0; uint8_t src = 0; uint32_t count = 0, firstAt = 0, lastAt = 0; };
struct Census {
  static constexpr uint8_t CAP = 40;
  Seen e[CAP]; uint8_t n = 0; uint32_t dropped = 0;
  uint16_t maker[256]; uint8_t func[256];
  Census() { for (int i = 0; i < 256; i++) { maker[i] = 0xFFFF; func[i] = 0xFF; } }
  void note(uint32_t pgn, uint8_t src, uint32_t now) {
    for (uint8_t i = 0; i < n; i++) if (e[i].pgn == pgn && e[i].src == src) { e[i].count++; e[i].lastAt = now; return; }
    if (n < CAP) { e[n].pgn = pgn; e[n].src = src; e[n].count = 1; e[n].firstAt = e[n].lastAt = now; n++; } else dropped++;
  }
  void claim(uint8_t src, const uint8_t *d, uint8_t len) {
    if (len < 8) return;
    uint32_t lo = u32(d), hi = u32(d + 4);
    maker[src] = (uint16_t)((lo >> 21) & 0x7FF);
    func[src] = (uint8_t)(hi >> 8);
  }
};

inline const char *makerName(uint16_t m) {
  switch (m) {
  case 1851: return "Raymarine"; case 229: return "Garmin"; case 275: return "Navico (Simrad, B&G, Lowrance)"; case 1855: return "Furuno";
  case 358: return "Victron Energy"; case 419: return "Fusion"; case 135: return "Airmar"; case 174: return "Volvo Penta"; case 144: return "Mercury";
  case 1857: return "Simrad"; case 381: return "B&G"; case 140: return "Lowrance"; case 137: return "Maretron"; case 273: return "Actisense";
  case 1863: return "Yacht Devices"; case 717: return "Yacht Devices"; case 2046: return "Digital Yacht"; case 211: return "Nautibus";
  default: return nullptr; }
}
inline const char *pgnName(uint32_t p) {
  switch (p) {
  case 129025: return "Position"; case 129026: return "Course and speed over ground"; case 129029: return "GNSS fix";
  case 127250: return "Heading"; case 128259: return "Speed through water"; case 128267: return "Depth"; case 128275: return "Distance log";
  case 130310: case 130311: case 130312: case 130316: return "Temperature"; case 127245: return "Rudder"; case 127508: return "Battery";
  case 130306: return "Wind"; case 127505: return "Tank level"; case 127488: return "Engine rpm"; case 127489: return "Engine parameters";
  case 126992: return "Time"; case 60928: return "Address claim"; case 126996: return "Product information"; case 126993: return "Heartbeat";
  case 127258: return "Magnetic variation"; case 129283: return "Cross track error"; case 129284: return "Navigation data"; case 129285: return "Route";
  case 129539: return "GNSS DOPs"; case 129540: return "Satellites in view"; case 127237: return "Heading/track control (autopilot)";
  case 65359: case 65360: case 65379: return "Raymarine autopilot"; case 126208: return "Command/request"; case 59904: return "Request";
  case 130577: return "Direction data"; case 128000: return "Leeway"; case 127251: return "Rate of turn"; case 127257: return "Attitude";
  default: return nullptr; }
}

}  // namespace n2k
