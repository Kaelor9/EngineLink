# 2026-11-10 — EngineLink 0.17.0-rc6 (core 3.2.0) on the home router (owner report)

Owner's report pasted in the session (built Nov 10 2026 14:41:29, Arduino
core 3.2.0 / IDF v5.4.1, fetched at uptime 635 s on the own Wi-Fi). Figures
copied from it. The PC got nothing for `/api/health`, `/` and
`enginelink.local` when the owner tried; the tab showed the app's title on
an empty dark page.

| Uptime | Event |
|---|---|
| +145 s | app's Connect; associated in 593 ms; address `192.168.0.2` at +147 s |
| +148–454 s | one router session, 306 s: 802.11n **40 MHz** negotiated, channel 1, power save off, signal −39…−47 dBm |
| +454 s | code 200 (beacons lost); then 49 × 201 until +572 s; own Wi-Fi |

**Pings to the router, per 30 s:** 12/15 (average 355 ms), 4/15 (819 ms),
8/15 (1068 ms), 2/15, 1/15, 2/15, 1/15, then 0/14, 0/15, 0/16. In all 30 of
153; **none answered in under 10 ms**, 2 under 50 ms, 6 under 200 ms, 14
under 1 s, 8 over 1 s (timeout 1.9 s).

**Web through the router** (17 connections, 8 closed without a request,
none used twice):

| Request | Result |
|---|---|
| `/manifest.webmanifest` (310 B) × 2 | 200 in 5–6 ms |
| `/api/health` (2 B) | 200 in 3 ms |
| `/` with a cached copy | 304 in 8 ms |
| `/` (110 KB) × 5 | **send failed** each time after 5.6–26 s |

Own Wi-Fi in the same start: 348 requests, none failed, 511–557 kB/s
(64–188 kB/s in the first 30 s after the start). System: CPU idle 89 %/85 %,
102 KB free, no failed allocation.

## What follows

- The same picture on core 3.2.0 as on 3.3.12: **the core version is
  cleared.**
- Every small answer through the router worked; every large one failed.
  The owner's sketch sent one small answer. So the sketch and EngineLink do
  not yet differ in anything that was tested.
- Replies that do arrive are late by 50–1800 ms at −40 dBm, where 1–3 ms is
  normal, and the share answered falls from 80 % to 0 over five minutes.
  That is not random loss at a weak signal; frames are held back or
  repeated many times.
