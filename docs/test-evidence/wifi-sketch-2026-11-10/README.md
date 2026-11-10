# 2026-11-10 — a plain Arduino sketch on the EngineLink board serves a page through the home router

Owner's test, after the rc4 report. The same EngineLink board, powered from
the lab supply only (no USB), with a sketch that uses nothing but `WiFi.h`,
`WebServer.h` and `ESPmDNS.h`:

```cpp
WiFi.mode(WIFI_STA);
WiFi.setSleep(false);
WiFi.begin(ssid, password);            // credentials not stored in this repository
while (WiFi.status() != WL_CONNECTED) delay(500);
MDNS.begin("enginelink"); MDNS.addService("http", "tcp", 80);
server.on("/", HTTP_GET, [] { server.send(200, "text/html", "...<h1>ENGINE LINK IS UP</h1>..."); });
server.begin();
// loop: server.handleClient(); delay(2);
```

(`setup()` begins with `Serial.begin(115200); delay(1000);`.)

**Result:** the board joined the home router and the owner's PC, on the same
router, opened `http://enginelink.local` in Chrome and got the page
(screenshot 14:28).

## What it proves, and what it does not

- Proven: this board, at that place, can join the home router and answer an
  HTTP request from another device through it, by name. The board, its
  antenna and the router are therefore able to work together.
- Not shown by this test: more than one small page (about 150 bytes; the
  app page is 110 KB), more than a moment (earlier EngineLink sessions also
  answered requests in their first minutes: rc3 answered 10 through the
  router before the link died at +150 s), a ping record, the build options
  the sketch was compiled with (PSRAM, USB mode).

## How 0.17.0-rc5 uses it

The station in rc5 is this sequence and nothing else (plan section 10). The
remaining differences between the sketch and EngineLink in router mode are
listed in plan 10.4, so a failure of rc5 can be narrowed to them.
