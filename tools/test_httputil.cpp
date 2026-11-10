// Host test for httputil.h (Wi-Fi 2.0 plan 6.6, 6.9, section 8 item 1).
// g++ -std=c++17 -Wall -Wextra -I firmware/EngineLink tools/test_httputil.cpp
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include "httputil.h"

using namespace httputil;

static std::string dec(const char *s, size_t cap = 2049) {
  std::string out(cap, '\0');
  const size_t n = urlDecode(s, strlen(s), &out[0], cap);
  assert(out[n] == 0);
  out.resize(n);
  return out;
}

int main() {
  // URL decoding
  assert(dec("a+b") == "a b");
  assert(dec("a%2Bb") == "a+b");
  assert(dec("%7B%22p%22%3A1%7D") == "{\"p\":1}");
  assert(dec("%e2%9c%93") == "\xe2\x9c\x93");   // UTF-8 bytes, lower-case hex
  assert(dec("100%") == "100%");                 // bad % sequences kept
  assert(dec("%4") == "%4");
  assert(dec("%zz1") == "%zz1");
  assert(dec("%%41") == "%A");
  assert(dec("") == "");
  {  // truncation at the buffer size (2 KB values)
    std::string big(5000, 'x');
    char out[2049];
    const size_t n = urlDecode(big.c_str(), big.size(), out, sizeof(out));
    assert(n == 2048 && out[2048] == 0);
    assert(urlDecode("abc", 3, out, 0) == 0);
    assert(urlDecode("abc", 3, out, 1) == 0 && out[0] == 0);
    assert(urlDecode("%41%42", 6, out, 2) == 1 && strcmp(out, "A") == 0);
  }

  const char *name = "enginelink", *ip = "192.168.1.57";
  // Host header: the four own addresses, loopback, ports, case, trailing dot
  assert(hostAllowed("192.168.4.1", name, ip));
  assert(hostAllowed("192.168.4.1:80", name, ip));
  assert(hostAllowed("enginelink.local", name, ip));
  assert(hostAllowed("EngineLink.LOCAL", name, ip));
  assert(hostAllowed("enginelink.local.", name, ip));
  assert(hostAllowed("192.168.1.57", name, ip));
  assert(hostAllowed("127.0.0.1", name, ip));
  assert(hostAllowed("boat.local", "boat", ip));          // a renamed board
  assert(hostAllowed("enginelink.local", "boat", ip));    // the default name stays ours
  // The name as a home router's own DNS serves it to the LAN (not registrable publicly).
  assert(hostAllowed("enginelink", name, ip));
  assert(hostAllowed("EngineLink.lan:80", name, ip));
  assert(hostAllowed("enginelink.home", name, ip));
  assert(hostAllowed("enginelink.home.arpa", name, ip));
  assert(hostAllowed("boat.lan", "boat", ip));
  assert(!hostAllowed("enginelink.com", name, ip));
  assert(!hostAllowed("enginelink.lan.evil.example", name, ip));
  assert(!hostAllowed("enginelinkx", name, ip));
  assert(!hostAllowed("engine", name, ip));
  assert(originAllowed("http://enginelink", name, ip));
  assert(!hostAllowed("evil.example", name, ip));         // a foreign name (DNS rebinding)
  assert(!hostAllowed("192.168.1.58", name, ip));         // a foreign IP
  assert(!hostAllowed("192.168.1.57", name, ""));         // no router: its old IP is not ours
  assert(!hostAllowed("enginelink.local.evil.example", name, ip));
  assert(!hostAllowed("xenginelink.local", name, ip));
  assert(!hostAllowed("", name, ip));                      // an empty header
  assert(!hostAllowed(nullptr, name, ip));
  assert(!hostAllowed("[::1]", name, ip));
  assert(!hostAllowed("boat.local", "", ip));
  // Origin header
  assert(originAllowed("http://enginelink.local", name, ip));
  assert(originAllowed("http://192.168.4.1", name, ip));
  assert(originAllowed("http://192.168.1.57:80", name, ip));
  assert(originAllowed("http://ENGINELINK.local", name, ip));
  assert(!originAllowed("https://enginelink.local", name, ip));
  assert(!originAllowed("http://evil.example", name, ip));
  assert(!originAllowed("http://127.0.0.1", name, ip));    // a page on the phone itself is not us
  assert(!originAllowed("null", name, ip));
  assert(!originAllowed("", name, ip));
  assert(!originAllowed(nullptr, name, ip));
  assert(!originAllowed("http://", name, ip));
  assert(!originAllowed("http://enginelink.local/x", name, ip));
  std::puts("httputil: all tests passed");
  return 0;
}
