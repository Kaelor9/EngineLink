// More data in flight on a TCP connection.
//
// The prebuilt Arduino ESP32 libraries give each TCP connection a send buffer
// of 5744 bytes: four packets may be on their way before EngineLink must wait
// for the phone's acknowledgement. On EngineLink's own Wi-Fi the phone answers
// in a few milliseconds, so that is plenty. Through a router every answer took
// 50-80 ms (field reports, 0.12.7/0.12.8), which caps a download below about
// 90 kB/s, and with only four packets out a single lost packet cannot be
// repaired quickly (that needs three later packets to arrive): the connection
// then waits a second or more. The buffer size is fixed when the library is
// built, but it is only a counter in each connection, so it can be raised for
// a connection right after it is accepted. Done under lwIP's core lock.
// lwIP queues at most 16 packets per connection, which bounds the extra memory.
#include <lwip/tcpip.h>
#include <lwip/sockets.h>
#include <lwip/priv/tcp_priv.h>

uint32_t elBoostedSockets = 0;

static bool portsOf(int fd, u16_t &lport, u16_t &rport) {
  // Arduino's servers listen on a dual-stack IPv6 socket, so IPv4 phones
  // show up as IPv4-mapped IPv6 addresses: take the ports from either form.
  struct sockaddr_storage a = {}, b = {};
  socklen_t al = sizeof(a), bl = sizeof(b);
  if (getsockname(fd, (struct sockaddr *)&a, &al) != 0) return false;
  if (getpeername(fd, (struct sockaddr *)&b, &bl) != 0) return false;
  if (a.ss_family == AF_INET) lport = lwip_ntohs(((struct sockaddr_in *)&a)->sin_port);
  else if (a.ss_family == AF_INET6) lport = lwip_ntohs(((struct sockaddr_in6 *)&a)->sin6_port);
  else return false;
  if (b.ss_family == AF_INET) rport = lwip_ntohs(((struct sockaddr_in *)&b)->sin_port);
  else if (b.ss_family == AF_INET6) rport = lwip_ntohs(((struct sockaddr_in6 *)&b)->sin6_port);
  else return false;
  return true;
}

uint32_t elBoostMisses = 0;

bool elBoostSocket(int fd, uint16_t extra) {
  u16_t lport = 0, rport = 0;
  if (fd < 0 || !portsOf(fd, lport, rport)) { ++elBoostMisses; return false; }
  bool ok = false;
  LOCK_TCPIP_CORE();
  for (struct tcp_pcb *pcb = tcp_active_pcbs; pcb; pcb = pcb->next) {
    if (pcb->local_port != lport || pcb->remote_port != rport) continue;
    const uint32_t nb = (uint32_t)pcb->snd_buf + extra;
    pcb->snd_buf = (tcpwnd_size_t)(nb > 0xFFFF ? 0xFFFF : nb);
    ok = true;
    break;
  }
  UNLOCK_TCPIP_CORE();
  if (ok) ++elBoostedSockets; else ++elBoostMisses;
  return ok;
}
