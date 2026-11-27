# Last contents of the board's SD card (read 2026-11-27)

The microSD card that sat in the revision 1 board during the boat test of
2026-11-14 and the bench work that followed was taken out and moved to another
project. Before that, on 2026-11-27, every file on it was copied and compared
with the card by SHA-256 (76 files, 35,153,366 bytes, no difference).

**This page is the public summary of that copy. The data itself is kept by the
owner outside this repository:** it holds the trip's navigation track, network
addresses and device details. Router names, addresses, phone identifiers and
positions are left out here. Nothing newer exists from that card.

## What was on the card

| Files | What |
|---|---|
| `EL0F7B250D.CSV` (9.6 MB, 128,641 rows) | Complete raw J1708 log of the boat trip 2026-11-14 |
| 2 more `EL*.CSV` from 2026-11-14 (312 and 4,208 rows) | Raw logs of the two boots right after the trip |
| `ELC279216D.CSV` (20.7 MB, 282,720 rows) | Raw log of one boot of 14.7 hours, ending 2026-11-15 07:26 UTC |
| 5 more `EL*.CSV` from 2026-11-15 (204 to 10,204 rows) | Raw logs of the morning's boots |
| 4 short `EL*.CSV` without clock time (56 to 800 rows) | Raw logs of boots that never got the time |
| 36 `EL*.CSV` of 108 bytes and 1 of 0 bytes | Header only: boots where nothing was logged (bench, 2026-11-15 and 11-16) |
| `TRIPS.CSV`, `TRIPS/T000001.BIN/.FST/.NAV/.COV` | Trip 1 of 2026-11-14: 1715 s with an engine running |
| `EVENTS/E000001`…`E000008` (`.BIN` and `.FST`) | Eight recorded events from the trip, 14:38 to 14:53 UTC |
| `SEGMENTS.CSV`, `NMEA/BUS.CSV` | Engine segments and the NMEA bus log of the trip |
| `WIFI.LOG` (784 lines) | Wi-Fi event log; the source of [the boat evidence of 2026-11-14](../boat-2026-11-14/README.md) |
| `WIFIREP.OLD`, `WIFIREP.TXT` | The 15 last half-hourly Wi-Fi reports, 2026-11-26 03:15 to 10:15 UTC |

One log file per boot, named after the boot's id, is what the firmware writes;
50 of the 76 files are such logs.

## The one new result: 211 hours in router mode without a lost ping

The Wi-Fi reports are the end of one continuous bench run of firmware 0.17.7
in router mode. The last report, 2026-11-26 10:15:43 UTC, says:

| | |
|---|---|
| Uptime, one boot | 759,773 s (211 hours, 8.8 days) |
| Pings to the router, one every 2 s | 379,886 sent, 379,886 answered |
| Reply time | 3 ms average, 309 ms longest; 367,176 under 10 ms, 12,518 from 10 to 50 ms, 184 from 50 to 200 ms, 8 from 200 to 1000 ms |
| Associations / IP assignments / disconnects / beacon time-outs | 1 / 1 / 0 / 0 |
| Signal | −46 dBm average, −79 lowest, channel 1, 802.11n 20 MHz |
| Web requests through the router | 4,795, one answered 4xx, none failed while sending (the last one at 145 hours) |
| Memory allocation failures | 0 |
| Chip temperature, last 30 minutes | 43 to 44 °C |

Board state during the run, as last confirmed by the owner on 2026-11-17 and
not re-confirmed afterwards: the two isolated DC/DC converters of the J1708
channels (PS1 and PS3) removed and the isolated 5 V rails fed from +5 V, so
without J1708 isolation; no engine connected; J1708 polling off, nothing sent
on J1708.

*Bench-verified* for this board state only. It is not a boat test and says
nothing about a board with the converters fitted.

## What was not on the card

No Wi-Fi report from the trip of 2026-11-14 itself: 0.17.5 kept only the
latest reports and they had been rotated away, as noted in
[STATUS](../../STATUS.md). 0.17.7 saves a report in each trip's own file, but
no trip was made with it, so that function is still not exercised.
