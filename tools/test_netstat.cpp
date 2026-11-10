// Host test for firmware/EngineLink/netstat.h (plan section 9).
#include <cassert>
#include <cstdio>
#include "netstat.h"
using namespace netstat;

int main() {
  // Link check figures, with the 0.17.0-rc3 session: 3 of 152 answered.
  Rtt r;
  for (int i = 0; i < 73; i++) r.lost();
  r.answered(633); r.answered(400); r.answered(355);
  for (int i = 0; i < 76; i++) r.lost();
  assert(r.sent == 152 && r.ok == 3 && r.avgMs() == 462 && r.maxMs == 633);
  assert(r.lostRow == 76 && r.lostRowMax == 76 && r.lossPct() == 98);
  assert(r.bucket[3] == 3 && r.bucket[0] == 0);
  Rtt g; g.answered(3); g.answered(12); g.answered(60); g.answered(1500);
  assert(g.bucket[0] == 1 && g.bucket[1] == 1 && g.bucket[2] == 1 && g.bucket[4] == 1 && g.lossPct() == 0);
  assert(Rtt().avgMs() == 0 && Rtt().lossPct() == 0);

  // Endpoints.
  assert(endpointOf("/") == EP_ROOT && endpointOf("/?v=2") == EP_ROOT);
  assert(endpointOf("/api/health") == EP_HEALTH && endpointOf("/api/live?x=1") == EP_LIVE);
  assert(endpointOf("/api/liver") == EP_OTHER && endpointOf("") == EP_OTHER && endpointOf(nullptr) == EP_OTHER);

  // Connections: empty, single request, reused.
  Http h;
  h.opened(3, NET_ROUTER); h.closed(3);                       // connected, sent nothing
  h.opened(3, NET_ROUTER); assert(h.began(3) == 1); h.closed(3);
  h.opened(4, NET_ROUTER); assert(h.began(4) == 1 && h.began(4) == 2); h.closed(4);
  h.closed(4);                                                // a second close changes nothing
  h.opened(99, NET_OWN); assert(h.began(99) == 0); h.closed(99);   // an fd outside the table is counted, not tracked
  h.opened(0, 7);                                             // an unknown network is ignored
  assert(h.net[NET_ROUTER].conns == 3 && h.net[NET_ROUTER].connsEmpty == 1 && h.net[NET_ROUTER].connsReused == 1);
  assert(h.net[NET_OWN].conns == 1 && h.began(-1) == 0);

  // Requests: counted by class and endpoint; the log keeps router requests,
  // failures and slow ones, newest REQ_CAP.
  h.finished(NET_ROUTER, "/", 304, false, 0, 4, 3, 1000, 1);
  h.finished(NET_ROUTER, "/api/live?since=12", 200, false, 950, 12, 9, 2000, 2);
  h.finished(NET_ROUTER, "/", 200, true, 40000, 5100, 20, 9000, 1);       // the page, cut off
  h.finished(NET_OWN, "/api/live", 200, false, 950, 8, 6, 9100, 5);       // own Wi-Fi, fine: not logged
  h.finished(NET_OWN, "/api/export/raw.csv", 200, false, 900000, 2500, 30, 9500, 1);   // slow: logged
  h.finished(NET_SELF, "/api/health", 200, false, 20, 1, 1, 9600, 1);
  h.finished(NET_ROUTER, "/nothing", 404, false, 9, 1, 1, 9700, 1);
  h.finished(9, "/", 200, false, 1, 1, 1, 1, 1);                           // ignored
  const HttpNet &rt = h.net[NET_ROUTER];
  assert(rt.requests == 4 && rt.failed == 1 && rt.byClass[0] == 2 && rt.byClass[1] == 1 && rt.byClass[2] == 1);
  assert(rt.bytes == 40959 && rt.slowMs == 5100 && strcmp(rt.slowPath, "/") == 0 && rt.lastMs == 9700);
  assert(rt.ep[EP_ROOT].n == 2 && rt.ep[EP_ROOT].failed == 1 && rt.ep[EP_ROOT].maxMs == 5100);
  assert(rt.ep[EP_LIVE].n == 1 && rt.ep[EP_OTHER].n == 1 && h.net[NET_SELF].ep[EP_HEALTH].n == 1);
  assert(h.size == 5 && h.logged == 5);
  assert(h.at(0).status == 304 && h.at(1).nthOnConn == 2 && strcmp(h.at(1).path, "/api/live") == 0);   // query cut off
  assert(h.at(2).failed && h.at(2).bytes == 40000 && h.at(3).net == NET_OWN && h.at(4).status == 404);
  for (int i = 0; i < 40; i++) h.finished(NET_ROUTER, "/api/a-very-long-path-that-does-not-fit-in-the-log", 200, false, 1, 1, 70000, 10000 + i, 1);
  assert(h.size == REQ_CAP && h.logged == 45 && h.at(REQ_CAP - 1).atMs == 10039 && h.at(0).atMs == 10008);
  assert(strlen(h.at(0).path) == 23 && h.at(0).firstMs == 65535);

  // What a session added to the router counters.
  HttpNet then = h.net[NET_ROUTER];
  h.opened(5, NET_ROUTER); h.began(5); h.finished(NET_ROUTER, "/api/live", 200, false, 100, 5, 4, 20000, 1); h.closed(5);
  const HttpDelta d = httpSince(h.net[NET_ROUTER], then);
  assert(d.conns == 1 && d.requests == 1 && d.byClass[0] == 1 && d.bytes == 100 && d.failed == 0 && d.empty == 0 && d.reused == 0);

  std::puts("netstat: all tests passed");
  return 0;
}
