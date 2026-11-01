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

bool elBoostSocket(int fd, uint16_t extra) {
  if (fd < 0) return false;
  struct sockaddr_in local = {}, peer = {};
  socklen_t ll = sizeof(local), pl = sizeof(peer);
  if (getsockname(fd, (struct sockaddr *)&local, &ll) != 0) return false;
  if (getpeername(fd, (struct sockaddr *)&peer, &pl) != 0) return false;
  if (local.sin_family != AF_INET || peer.sin_family != AF_INET) return false;
  const u16_t lport = lwip_ntohs(local.sin_port), rport = lwip_ntohs(peer.sin_port);
  bool ok = false;
  LOCK_TCPIP_CORE();
  for (struct tcp_pcb *pcb = tcp_active_pcbs; pcb; pcb = pcb->next) {
    if (pcb->local_port != lport || pcb->remote_port != rport) continue;
    if (!IP_IS_V4(&pcb->remote_ip) || ip_2_ip4(&pcb->remote_ip)->addr != peer.sin_addr.s_addr) continue;
    const uint32_t nb = (uint32_t)pcb->snd_buf + extra;
    pcb->snd_buf = (tcpwnd_size_t)(nb > 0xFFFF ? 0xFFFF : nb);
    ok = true;
    break;
  }
  UNLOCK_TCPIP_CORE();
  if (ok) ++elBoostedSockets;
  return ok;
}
