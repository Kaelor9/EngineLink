// Network statistics without Arduino (docs/WIFI_PLAN_2026-11-09.md section 9):
// the link check's round-trip figures and the web server's counters by network
// with a log of single requests. Pure C++, no clock reads, no radio or socket calls. Host-tested by
// tools/test_netstat.cpp; netdiag.ino and httpd.ino feed it.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace netstat {

// ---- Link check: one ping to the router every 2 s ------------------------
struct Rtt {
  uint32_t sent = 0, ok = 0, sumMs = 0, maxMs = 0, lostRow = 0, lostRowMax = 0;
  uint32_t bucket[5] = {};   // answered in <10, <50, <200, <1000, >=1000 ms
  void answered(uint32_t ms) {
    ++sent; ++ok; sumMs += ms; lostRow = 0;
    if (ms > maxMs) maxMs = ms;
    ++bucket[ms < 10 ? 0 : ms < 50 ? 1 : ms < 200 ? 2 : ms < 1000 ? 3 : 4];
  }
  void lost() { ++sent; if (++lostRow > lostRowMax) lostRowMax = lostRow; }
  uint32_t avgMs() const { return ok ? sumMs / ok : 0; }
  uint32_t lossPct() const { return sent ? (uint32_t)(((uint64_t)(sent - ok) * 100 + sent / 2) / sent) : 0; }
};

// ---- Web server by network -------------------------------------------------
enum : uint8_t { NET_OWN = 0, NET_ROUTER = 1, NET_SELF = 2, NET_N = 3 };   // own Wi-Fi, router, the board's own self-test
enum : uint8_t { EP_ROOT = 0, EP_HEALTH = 1, EP_LIVE = 2, EP_OTHER = 3, EP_N = 4 };

inline uint8_t endpointOf(const char *uri) {
  if (!uri) return EP_OTHER;
  const char *q = strchr(uri, '?');
  const size_t n = q ? (size_t)(q - uri) : strlen(uri);
  auto is = [&](const char *p) { return strlen(p) == n && memcmp(uri, p, n) == 0; };
  return is("/") ? EP_ROOT : is("/api/health") ? EP_HEALTH : is("/api/live") ? EP_LIVE : EP_OTHER;
}

struct Endpoint { uint32_t n = 0, failed = 0, maxMs = 0; };
struct HttpNet {
  // connsEmpty: closed without a single request (the client's request never
  // arrived). connsReused: more than one request (the client received the
  // first answer and went on). The answers themselves cannot be seen arriving.
  uint32_t conns = 0, connsEmpty = 0, connsReused = 0, requests = 0, failed = 0, lastMs = 0, slowMs = 0;
  uint32_t byClass[4] = {};   // 2xx, 3xx, 4xx, 5xx
  uint64_t bytes = 0;
  char slowPath[24] = {};
  Endpoint ep[EP_N];
};
struct Req {
  uint32_t atMs = 0, bytes = 0, ms = 0;
  uint16_t firstMs = 0, status = 0;
  uint8_t net = 0, nthOnConn = 0;
  bool failed = false;
  char path[24] = {};
};
constexpr uint8_t REQ_CAP = 32, FD_SLOTS = 16;

struct Http {
  HttpNet net[NET_N];
  Req log[REQ_CAP];
  uint8_t head = 0, size = 0;
  uint32_t logged = 0;
  uint8_t fdNet[FD_SLOTS] = {}, fdReq[FD_SLOTS] = {};
  bool fdOpen[FD_SLOTS] = {};

  static bool slotOk(int slot) { return slot >= 0 && slot < FD_SLOTS; }
  void opened(int slot, uint8_t n) {
    if (n >= NET_N) return;
    ++net[n].conns;
    if (slotOk(slot)) { fdOpen[slot] = true; fdNet[slot] = n; fdReq[slot] = 0; }
  }
  void closed(int slot) {
    if (!slotOk(slot) || !fdOpen[slot]) return;
    fdOpen[slot] = false;
    HttpNet &h = net[fdNet[slot]];
    if (fdReq[slot] == 0) ++h.connsEmpty; else if (fdReq[slot] > 1) ++h.connsReused;
  }
  // A request arrived on this connection: its number on the connection (1 = first).
  uint8_t began(int slot) {
    if (!slotOk(slot) || !fdOpen[slot]) return 0;
    if (fdReq[slot] < 255) ++fdReq[slot];
    return fdReq[slot];
  }
  // The answer is over (sent or failed). Router requests, failures and
  // anything slower than 2 s also go to the log.
  void finished(uint8_t n, const char *uri, uint16_t status, bool failed, uint32_t bytes, uint32_t ms,
                uint32_t firstMs, uint32_t nowMs, uint8_t nth) {
    if (n >= NET_N) return;
    HttpNet &h = net[n];
    ++h.requests; h.bytes += bytes; h.lastMs = nowMs;
    if (failed) ++h.failed;
    if (status >= 200 && status < 600) ++h.byClass[status / 100 - 2];
    if (ms >= h.slowMs) { h.slowMs = ms; copyPath(h.slowPath, uri); }
    Endpoint &e = h.ep[endpointOf(uri)];
    ++e.n; if (failed) ++e.failed; if (ms > e.maxMs) e.maxMs = ms;
    if (n != NET_ROUTER && !failed && ms < 2000) return;
    Req &r = log[head];
    r.atMs = nowMs; r.bytes = bytes; r.ms = ms; r.firstMs = firstMs > 65535 ? 65535 : (uint16_t)firstMs;
    r.status = status; r.net = n; r.nthOnConn = nth; r.failed = failed; copyPath(r.path, uri);
    head = (uint8_t)((head + 1) % REQ_CAP); if (size < REQ_CAP) ++size; ++logged;
  }
  const Req &at(uint8_t i) const { return log[(head + REQ_CAP - size + i) % REQ_CAP]; }   // 0 = oldest kept

  static void copyPath(char *dst, const char *uri) {   // the path without the query, cut to 23 characters
    size_t i = 0;
    if (uri) for (; i < 23 && uri[i] && uri[i] != '?'; i++) dst[i] = uri[i];
    dst[i] = 0;
  }
};

// ---- A router session in 30 s pieces (netdiag.ino fills them) -----------
struct Slot {
  uint32_t startS = 0;                                   // uptime, seconds
  uint16_t pingSent = 0, pingOk = 0, rttAvg = 0;
  uint16_t requests = 0, failed = 0;
  uint32_t bytes = 0;
  int8_t rssi = 0;                                       // dBm; 0 = not read
};
constexpr uint8_t SLOT_CAP = 60;   // 30 minutes

// ---- One router session, kept after it ended ---------------------------
struct HttpDelta { uint32_t conns = 0, empty = 0, reused = 0, requests = 0, failed = 0, byClass[4] = {}; uint64_t bytes = 0; };
inline HttpDelta httpSince(const HttpNet &now, const HttpNet &then) {
  HttpDelta d;
  d.conns = now.conns - then.conns; d.empty = now.connsEmpty - then.connsEmpty; d.reused = now.connsReused - then.connsReused;
  d.requests = now.requests - then.requests; d.failed = now.failed - then.failed; d.bytes = now.bytes - then.bytes;
  for (int i = 0; i < 4; i++) d.byClass[i] = now.byClass[i] - then.byClass[i];
  return d;
}
constexpr uint8_t END_OWN = 0;   // EngineLink changed network itself (the driver's reasons are 1..)
struct RouterSession {
  bool active = false, seen = false, b = false, g = false, n = false;
  uint32_t startMs = 0, endMs = 0, joinMs = 0, dhcpMs = 0;
  int rssiStart = 0, rssiMin = 0, rssiMax = -127, rssiLast = 0, rssiEnd = 0;
  int32_t rssiSum = 0; uint32_t rssiN = 0;
  uint8_t ch = 0, second = 0, phy = 255, pair = 255, group = 255, ps = 255, bw = 0, proto = 0, endReason = 0;
  int8_t txQdbm = 0;
  uint32_t ip = 0, mask = 0, gw = 0, dns = 0, beaconTimeouts = 0, channelChanges = 0;
  Rtt ping;
  HttpNet http0;    // the web server's router counters when the session began
  HttpDelta http;   // what the session added (set when it ends)
};
constexpr uint8_t SESSION_CAP = 4;

// ---- A slow step of the network part of the main loop --------------------
struct Step { const char *what = nullptr; uint32_t ms = 0, atMs = 0; };
constexpr uint8_t STEP_CAP = 6;

}  // namespace netstat
