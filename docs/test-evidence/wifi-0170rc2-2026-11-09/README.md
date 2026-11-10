# 2026-11-09/10 — EngineLink 0.17.0-rc2 on the home router (owner report)

Raw report: [`firmware-0170rc2-maggan.txt`](firmware-0170rc2-maggan.txt)
(built Nov 9 2026 16:57:58, fetched at uptime 3209 s on the own Wi-Fi). Owner
observations, not instrumented: no packet capture, no ping log. Analysis and
the resulting build (0.17.0-rc3) are in
[WIFI_PLAN_2026-11-09.md](../../WIFI_PLAN_2026-11-09.md) section 8.

## Owner's observations

- EngineLink joins the home router (TP-Link 4G router, WPA2) and gets
  `192.168.0.2` in about 2 s; signal −47 dBm.
- The page does not open from the PC on the same router: Chrome and Firefox
  time out. Earlier builds: `http://192.168.0.2/` and `/api/health` too.
- Standalone works: 577–689 kB/s, round trip 7–10 ms.
- **+411 s: beacons lost (code 200) without the router being touched.**
- +2217 s: IP again (the app's Connect).
- **+2986 s: the owner switched the router off.** This loss is not a fault.
- "It sometimes seemed I had to type `https://` instead of `http://`."

## Timeline from the report

| Uptime | Event |
|---|---|
| +0.4 s | connect (start after a software restart), associated at +0.9 s, IP at +2.5 s |
| +411 s | code 200, beacons lost; 49 × 201 until +529 s; own Wi-Fi on |
| +1131 s, +1851 s | automatic return attempts: 49 × 201 each (router not heard for 2 min) |
| +1998–2215 s | phone on the own Wi-Fi (speed tests ~+2093 s) |
| +2215 s | app's Connect: associated in 319 ms, IP 1.6 s later |
| +2986 s | code 200 (router switched off by the owner) |

## What this report cannot tell (and rc3 now records)

- Which HTTP requests came through the router: the counters (307 requests,
  4 send failures, slowest `/` 10.9 s) mix both networks and the self-test.
- Whether EngineLink could exchange packets with the router during a session.
- The station's power save: the line was read in access point mode. From the
  source (Arduino core `STA.cpp:122`, `WiFiGeneric.cpp:784`): the first session
  (straight after start) ran with modem sleep **on**, the second (after the
  own Wi-Fi had been up, whose setup stores "sleep off" in the core) with
  modem sleep **off**. The first lost the router by itself after 409 s; the
  second ran 769 s until the router was switched off.
- Whether anything at all was heard in the 2 min without the router
  (neighbours' networks), i.e. router radio away or EngineLink deaf.
