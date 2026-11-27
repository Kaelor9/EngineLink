# Last contents of the board's SD card (read 2026-11-27)

The microSD card that sat in the revision 1 board during the boat test of
2026-11-14 and the bench work that followed was taken out and moved to another
project. Before that, on 2026-11-27, every file on it was copied and compared
with the card by SHA-256 (76 files, 35,153,366 bytes, no difference).

**The card was then formatted for its new use. Nothing newer exists from it.**
A sanitised copy of its data is in [`data/`](data/), in the card's own file and
folder names; the owner keeps the unsanitised copy outside this repository.

## What the copy in `data/` is

[`data/MANIFEST.csv`](data/MANIFEST.csv) lists all 76 files of the card with
their size and SHA-256 on the card, the SHA-256 of the file here, and what was
done to it. `data/.gitattributes` keeps every file byte for byte (no line-ending
conversion), so the hashes can be checked, and `data/.gitignore` lets the
`.BIN` data files in despite the repository's rule against build output.

| Files | Here |
|---|---|
| 13 `EL*.CSV` with rows, `TRIPS.CSV`, `SEGMENTS.CSV`, `TRIPS/T000001.BIN/.FST/.COV`, `EVENTS/*` | Unchanged, byte for byte as on the card. The J1708 logs hold only the KAD read requests (MID 1) and the ECU replies (MID 131); no VIN, unit or software identification |
| `TRIPS/T000001.NAV` | Latitude and longitude of all 1,834 one-second records set to the firmware's not-available value (INT32_MIN). Speed, course, heading, depth and the other fields are unchanged |
| `NMEA/BUS.CSV` | Only these PGNs kept: 59904, 126208, 126464, 126992, 126993, 127245, 127250, 128259, 128267, 128275, 129026, 129033, 129283, 129291, 130311, 130577. Kept 12,414 of 33,774 rows. Removed: own position (129025, 129029), magnetic variation and datum (127258, 129044), AIS of other vessels (129038, 129039, 129794, 129809, 129810), device identity and product information (60928, 126996), proprietary messages (61184, 65288–65535, 126720, 130919, 130934, 130935). A comment line at the top says so |
| `WIFI.LOG`, `WIFIREP.OLD`, `WIFIREP.TXT` | Every IPv4 address replaced by `<ip>` and every MAC prefix by `<mac>`. The router's name was already hidden by the firmware. No password or key is in them |
| 36 `EL*.CSV` of 108 bytes and 1 of 0 bytes | Left out: header only, no rows. Listed in the manifest |

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
