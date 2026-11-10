# 2026-11-10 — EngineLink 0.17.0-rc4 on the home router (owner report)

Owner's report, pasted in the session of 2026-11-10 (built Nov 10 2026
12:14:11, fetched at uptime 394 s on the own Wi-Fi). The figures below are
copied from it; the full text was not stored. Analysis and the resulting
build (0.17.0-rc5): [WIFI_PLAN_2026-11-09.md](../../WIFI_PLAN_2026-11-09.md)
section 10.

## What the report says

| Uptime | Event |
|---|---|
| +0 s | own Wi-Fi on (channel 6); a phone joined at +1 s |
| +112.2 s | the phone left, app's Connect: join started |
| +113–142 s | **23 failed attempts, none associated:** code 2 × 17 (no answer to the authentication), code 4 × 4, code 15 × 1 (handshake timeout), code 202 × 1 at +142.1 s |
| +142–232 s | **no further attempt for 90 s** (no event in the list) |
| ~+232 s | recovery fallback (1), own Wi-Fi on |
| +240 s, +298 s | scans: the router **heard** on channel 1 at −51 and −62 dBm; others on channels 9 (−60/−66) and 11 (−47/−49) |
| +354 s | phone on the own Wi-Fi; speed tests 566–682 kB/s, 8–12 ms |

Counters: associations 0, router addresses 0, disconnect events 23,
pre-connection failures 23, app-initiated attempts 1. Station profile as
reported: power save off, 20 MHz, 802.11n. System: longest main-loop pass
119 ms, engine task longest wait 46 ms, free memory 92 KB (lowest 82 KB),
0 failed allocations, CPU idle 96 %/90 %, no crash.

## What follows directly from it

- The router was found at every attempt (an authentication was sent each
  time) and heard in both scans, at a good signal. Its answers to the
  authentication, association and handshake frames did not arrive, or
  EngineLink's frames were not heard by it.
- **After code 202 the Arduino core stopped reconnecting** (`STA.cpp`:
  202 is not in its list of reasons to retry). EngineLink then stood idle
  for 90 s until the 2-minute fallback. A firmware fault; fixed in rc5.
- The core retried about once a second without a pause (23 attempts in
  30 s).
- The own Wi-Fi works as before.
