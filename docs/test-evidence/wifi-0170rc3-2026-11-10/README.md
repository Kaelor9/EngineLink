# 2026-11-10 — EngineLink 0.17.0-rc3 on the home router (owner report)

Raw report: [`firmware-0170rc3-maggan.txt`](firmware-0170rc3-maggan.txt)
(built Nov 10 2026 11:09:47, fetched at uptime 1321 s on the own Wi-Fi; two
lines of unrelated browser text before the report were removed). Analysis,
fixes and the next build (0.17.0-rc4):
[WIFI_PLAN_2026-11-09.md](../../WIFI_PLAN_2026-11-09.md) section 9.

## What the report says

| Uptime | Event |
|---|---|
| +0–118 s | start in router mode: 49 × 201 (router not found), own Wi-Fi on |
| +276 s | app's Connect: associated in 378 ms, address `192.168.0.2` 1.7 s later |
| +278–581 s | router session, 302 s: WPA2, **router on channel 1 with 40 MHz, negotiated 802.11n 40 MHz**, pairwise CCMP, group TKIP, power save off, signal −47 dBm from start to end |
| same | **link check: 3 of 152 pings to the router answered**, those in 355–633 ms (average 462); longest run of losses 76 pings = 152 s |
| same | web server, through the router: 10 connections, 10 requests, 9 answered with 2xx, 0 failed sends, the last at +429 s |
| +581 s | code 200, beacons lost |
| +591 s | scan: 3 other networks heard (strongest −50 dBm), **the router not heard** |
| +741 s | phone on the own Wi-Fi; Standalone speed tests 550–720 kB/s, 7–8 ms |

System: longest main-loop pass 1660 ms, of it 1645 ms in the network part,
the engine task waiting as long for the lock; free memory 98 KB, lowest
87 KB; no crash or watchdog restart; RF calibration made in full at this
start.

## What follows directly from it (details in plan 9.1–9.2)

- 581 − 76 × 2 s = 429 s: the last web request and the start of the final run
  of lost pings coincide. Everything that got through, got through before
  +429 s; after that 152 s of silence **with the beacons still heard**, then
  the beacons stopped too.
- Requests = connections = 10: no connection carried a second request.
- The own Wi-Fi came on about 5 s after the loss (scan at +591 s, no 201
  retries after +581 s in the event list) instead of after 2 minutes, and the
  session's end code reads 36 although the driver reported 200: both are
  firmware faults of rc3, found and fixed in rc4.
- Not recorded by rc3 and therefore unknown: which addresses the 10 requests
  were for, whether their answers arrived, what the one non-2xx answer was,
  on which channels the three other networks are, which driver call took the
  1645 ms.
