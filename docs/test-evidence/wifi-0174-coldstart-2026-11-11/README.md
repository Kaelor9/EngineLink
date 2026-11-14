# 0.17.4 cold start in Standalone, 2026-11-11 (plan section 18)

> **Later (plan section 19, 2026-11-14):** the slow link was a busy channel 6,
> not the board's warm-up or crystal: the same tests on channel 1 were fast.

Board: repaired PCB, firmware 0.17.4 (build Nov 10 2026 18:47:00, installed
with the web installer, no router saved). Powered **from the PC's USB only**
(the bench supply was not connected; section 17 used the bench supply without
USB). Off for about 15 minutes before the test.

PC: Intel AX210 on `EngineLink` (channel 6, 20 MHz, about −46 dBm at the
board). Every request bound to the PC's Wi-Fi address (`192.168.4.2`); the PC's
Ethernet stayed on for internet. Speed test = `GET /api/speedtest?kb=256`,
logged back with `POST /api/speedlog`, so the board stored each result with
its chip temperature.

| File | What |
|---|---|
| `1-coldstart-serial.log` | USB serial from 32 s after power-on: `status`, and the `wifi` report at 0.5, 3.5, 6.5, 10.5, 15.5 and 20.5 min |
| `1-coldstart-measure.log`, `1-coldstart-network-report.txt` | health every 20 s, speed test every 90 s, from the PC's join (uptime 373 s) to 1590 s |
| `2-rejoin-warm.log` | PC left and rejoined at uptime about 1600 s, speed test every 30 s for 8 min |
| `3-warm-restart.log`, `3-warm-restart-network-report.txt` | the app's restart (`POST /api/reboot`) with the board warm, then the same (uptime 544 s in the report: the restart happened) |

A first attempt at a chip reset over USB (RTS) did not reset the board
(uptime 2727 s in its report) and is not used.

## Results

Cold power-on:

| Uptime | Chip | kB/s | Request round trip |
|---|---|---|---|
| 34 s | 28 °C | — | — |
| 209 s | 37 °C | — | — |
| 389 s | 41 °C | PC joined at 373 s; PC ping to 192.168.4.1 69–198 ms | |
| 440 s | 41 °C | **37** | 33 ms (health 63–286 ms) |
| 544 s | 42 °C | 90 | 53 ms |
| 626 s | 42 °C | 180 | 100 ms |
| 706 s | 43 °C | 476 | 18 ms |
| 807 s | 43 °C | 690 | 16 ms |
| 887–1590 s | 42–44 °C | 713–760 | 11–38 ms (health 8–30 ms) |

Warm board, PC rejoined (about 1600 s): **714 kB/s in the first second**,
then 601–769 for 8 minutes.

Warm board, software restart: **752 kB/s at the first test** (about 120 s
after the restart; Windows took that long to rejoin), then 579–773 for
8 minutes.

## What it shows

- The slow phase belongs to the **cold power-on**, not to the PC's join (a
  rejoin on the warm board is fast at once) and not to anything EngineLink
  does at every start (a restart on the warm board is fast at once; same
  firmware, same settings, same calibration data in NVS).
- It does **not follow the chip's own temperature**: the speed went from 37 to
  720 kB/s while the sensor moved from 41 to 43 °C. Whatever settles warms
  more slowly than the die, for example the 40 MHz crystal on the module or
  the board around it. Not proven.
- USB power and bench supply give the same pattern (this test against
  section 17), so the supply path is not a likely cause.
- Router mode was not measured in this session.

## 4. After reflowing the module's GND pads 41–49 (owner, same day)

Power-on at 13:14 with the board still slightly warm from soldering (first
chip reading 31 °C), PC joined at about 2 min, speed test every 30 s for
55 min (`4-after-gnd-reflow-*`).

| Time after the PC joined | Chip | kB/s |
|---|---|---|
| 1–5 min | 39–43 °C | 3–12 (one test timed out; PC ping 524–1602 ms) |
| 6–10 min | 44 °C | 42–70 |
| 11–17 min | 44 °C | 114–263 |
| 18–25 min | 44 °C | 303–551 |
| 27–40 min | 44 °C | 532–705 |
| 41–55 min | 44 °C | 616–776 |

Same pattern as the cold start, but slower to recover (about 40 min instead
of 13) while the chip sensor stood at 44 °C from minute 8. The reflow did not
remove it; the link was worse at the start than before the reflow.
