# Boat 2026-11-14 (0.17.5) and bench 2026-11-15

Source: `/WIFI.LOG` from the board's SD card (read over USB on 2026-11-15),
the trip's exports, the owner's observations, and bench measurements on
2026-11-15. Router names, phone addresses and positions are left out.

## What the owner saw

On the boat with both engines idling at the dock (trip 14:27–14:56 UTC): the
app kept showing "Reconnecting"; saving the tank size and logging a fill-up
took many attempts; the drive test could not be run. EngineLink was set to
router mode with a phone hotspot (mobile data) as the router.

## What the Wi-Fi log shows

| Time (UTC) | State | Measured by EngineLink |
|---|---|---|
| to 14:43:46 | router not found, own Wi-Fi on, two phones at −36…−51 dBm | in 5 minutes the phones left 6 times: AP codes 2 and 209 (EngineLink dropped a phone that did not answer) three times, code 8 (phone left) twice, code 3 once |
| 14:43:46 | the last phone left; **the same second** EngineLink switched its own Wi-Fi off and joined the hotspot (channel 6) | |
| 14:43:48–14:57:16 | on the hotspot, 806 s, signal −37…−71 dBm | pings 266 of 403 answered (34 % lost), average 300 ms, 43 lost in a row; 270 requests, 2 failed while sending; ended with code 200 (beacons lost) |
| 14:58:19–15:09:35 | on the hotspot, 675 s, signal to −72 dBm | pings 104 of 337 (69 % lost), average 594 ms; 100 requests, 11 failed |
| 15:13:06–15:15:36 | on the hotspot after a restart, 149 s, signal −31…−50 dBm | pings 31 of 74 (58 % lost), average 597 ms; 5 requests, 4 failed |

At home that evening (router mode, the hotspot not there): "Router not
reached in 2 minutes: own Wi-Fi on" **33 times between 17:26 and midnight**,
one every 12 minutes, each with the own Wi-Fi off for 2 minutes. The channel
survey before each start found the air quiet (2–12 % busy on 1, 6 and 11).

No device report reached GitHub from the boat (the report needs a working
internet path through the router).

## What follows from it

1. **Fault in 0.17.5, fixed in 0.17.6:** in router mode without the router,
   the own Wi-Fi was switched off for 2 minutes whenever no phone was joined
   at the moment of the 10-minute retry, and at once when the last phone
   left after that time. A phone that left for a moment (screen locked, iOS
   leaving a network without internet) came back to nothing.
2. **The hotspot link lost 34–69 % of the pings, also at −31 dBm.** Not
   explained. The same board on the boat's LTE router at home the next day:
   0 % lost (below). A phone hotspot is not a good router for EngineLink.
3. **Own Wi-Fi on the boat: phones dropped with codes 2 and 209** in the
   5 minutes the log covers, the same signature as on the boat on 2026-11-05
   (STATUS). Open. The older part of the log (`/WIFI.OLD`) and the saved
   reports (`/WIFIREP.OLD`) from the trip are still on the card; 0.17.6 makes
   them downloadable.
4. Not measured: whether answers were slow because of the board's own work
   with both engines running (SD log about 10 MB in 30 minutes). The saved
   reports hold every request's times; read them before concluding.
5. Report bug seen: "longest … ms" printed 4294967268 for a session (a
   negative ping time from the driver, shown unsigned). Not fixed here.

## Bench, 2026-11-15 (0.17.5, the boat's LTE router at home, channel 1)

| Test | Result |
|---|---|
| Morning, PC on the router's Wi-Fi, phone speed test | 28–139 kB/s; EngineLink's pings to the router 673 of 673, average 89 ms, longest 998 ms at −43 dBm; web answers 7 ms at the server |
| After a restart, PC on the router by cable: PC → router → Wi-Fi → EngineLink | 30 of 30 pings, average 2 ms; 32 KB speed test 385–578 kB/s; the app page (110 KB) in 0.11 s; raw export 277 kB/s for 20 s |
| EngineLink's own pings to the router then | 64 of 64, average 1 ms |
| Phone speed test then | over 400 kB/s |

Same board, firmware, router and channel; the slow morning session is not
reproduced. Probable cause: other traffic on the router's channel (the PC on
its Wi-Fi); not proven.

## Power path, measured on the bench 2026-11-15

After the boat test the board's USB protection (U3, USBLC6-2SC6) had burned
and the buck converter (IC1, LM5164) was suspected. Measured with a lab
supply and an oscilloscope (Rigol DHO804, spring ground, 20 MHz limit):

| Measurement | Result |
|---|---|
| Input at 13.0 V | 92 mA, constant voltage (about 1.2 W) |
| With the supply limited to 100 mA | the input fell to 4.6 V and IC1 restarted in a loop: the limit was too low, not a fault |
| IC1 feedback pin | 1.2 V with about 70 mV ripple, period about 3 µs (as designed: 318 kHz, 74 mV) |
| 5 V rail at C8 | 5.36 V average; ripple 34–48 mVpp, up to 75 mVpp on load steps |
| Switching frequency read on the 5 V rail | 100–336 kHz: at about 0.2 A the converter is partly in discontinuous mode |

IC1 works. Probable cause of U3's failure (not proven): a damp finger on
the running board bridged IC1's feedback pin (divider 340 kΩ / 101 kΩ) to
ground, the output rose, and the output reaches U3's VBUS pin through D3.
Revision 2 ideas, not decided: U3's VBUS pin before D10 (drawn by the
owner), a lower-impedance feedback divider, a clamp on the 5 V rail,
R4 = 330 kΩ for more margin to the isolated converters' 5.5 V limit.
