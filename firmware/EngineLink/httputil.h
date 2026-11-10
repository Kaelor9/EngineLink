// HTTP helpers without Arduino (docs/WIFI_REWRITE_PLAN.md 6.6, 6.9):
// URL decoding of form values and the own-address allow-list for the Host
// and Origin headers. Host-tested by tools/test_httputil.cpp.
// esp_http_server finds a key in a query or form body
// (httpd_query_key_value) but returns the value still URL-encoded and has no
// notion of which hosts are "us"; that is all this file adds.
#pragma once
#include <stddef.h>
#include <string.h>

namespace httputil {

inline int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Decodes application/x-www-form-urlencoded text: '+' is a space, %xx a
// byte; a '%' not followed by two hex digits is kept as it is. Writes at
// most cap-1 bytes and a terminating 0; returns the decoded length.
inline size_t urlDecode(const char *in, size_t len, char *out, size_t cap) {
  if (!cap) return 0;
  size_t n = 0;
  for (size_t i = 0; i < len && n + 1 < cap; i++) {
    const char c = in[i];
    if (c == '+') { out[n++] = ' '; continue; }
    if (c == '%' && i + 2 < len) {
      const int h = hexVal(in[i + 1]), l = hexVal(in[i + 2]);
      if (h >= 0 && l >= 0) { out[n++] = (char)(h * 16 + l); i += 2; continue; }
    }
    out[n++] = c;
  }
  out[n] = 0;
  return n;
}

inline bool ieq(const char *a, size_t an, const char *b) {
  const size_t bn = strlen(b);
  if (an != bn) return false;
  for (size_t i = 0; i < an; i++) {
    char x = a[i], y = b[i];
    if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
    if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
    if (x != y) return false;
  }
  return true;
}

// EngineLink's own addresses (plan 6.9 item 1): 192.168.4.1, <name>.local,
// enginelink.local, the name as a router's DNS serves it (<name>, <name>.lan,
// ...) and the current router IP (empty when not on a router).
// `host` is the bare host, no port. Case-insensitive; a trailing dot is
// allowed (an absolute DNS name).
inline bool ownHost(const char *host, size_t n, const char *hostName, const char *routerIp) {
  if (n && host[n - 1] == '.') --n;
  if (!n) return false;
  if (ieq(host, n, "192.168.4.1") || ieq(host, n, "enginelink.local")) return true;
  if (routerIp && *routerIp && ieq(host, n, routerIp)) return true;
  if (hostName && *hostName) {
    // <name>.local (mDNS), the bare name, and the suffixes home routers' own
    // DNS gives DHCP clients. None of these can be registered on the internet,
    // so the DNS-rebinding defence holds.
    const size_t hn = strlen(hostName);
    if (n >= hn && ieq(host, hn, hostName))
      for (const char *sfx : {"", ".local", ".lan", ".home", ".home.arpa", ".internal"})
        if (ieq(host + hn, n - hn, sfx)) return true;
  }
  return false;
}

// Length of the host part of "host[:port]" (an IPv6 literal is not ours).
inline size_t hostPart(const char *v) {
  const char *c = strchr(v, ':');
  return c ? (size_t)(c - v) : strlen(v);
}

// The Host header (plan 6.9 item 3): one of our addresses, or the board's
// own loopback (the boot self-test, 6.10). Anything else is answered 421.
inline bool hostAllowed(const char *hostHeader, const char *hostName, const char *routerIp) {
  if (!hostHeader || !*hostHeader || hostHeader[0] == '[') return false;
  const size_t n = hostPart(hostHeader);
  if (ieq(hostHeader, n, "127.0.0.1")) return true;
  return ownHost(hostHeader, n, hostName, routerIp);
}

// The Origin header (plan 6.9 item 1): "http://" + one of our addresses
// (+ optional port). Only such origins get an Access-Control-Allow-Origin.
inline bool originAllowed(const char *origin, const char *hostName, const char *routerIp) {
  if (!origin) return false;
  static const char kHttp[] = "http://";
  if (strncmp(origin, kHttp, 7) != 0) return false;
  const char *h = origin + 7;
  if (!*h || *h == '[' || strchr(h, '/')) return false;
  return ownHost(h, hostPart(h), hostName, routerIp);
}

}  // namespace httputil
