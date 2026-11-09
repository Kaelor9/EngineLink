# Session handoff, 2026-11-08 (router-mode fault, CI)

**LATEST 2026-11-09 — this handoff is now historical.** The current, exhaustive [2026-11-09 test case](docs/test-evidence/wifi-0166-2026-11-09/README.md) and its [raw 0.16.6 report](docs/test-evidence/wifi-0166-2026-11-09/firmware-75faba4-maggan-recovery.txt) take precedence. **Build #261** at `75faba4` passed CI and was installed on repaired hardware: Standalone loads the UI and reaches 540–746 kB/s; Router associates + gets `192.168.0.2` but HTTP times out, then reason-200 beacon loss and repeated reason-201 not found events. Previous A/B builds, custom `wifiwpa3` and `netboost` exceptions are retired. Fable has explicit permission to **revise** the old plan (plan only); no further implementation, merge or release without approval. The historical instructions below are retained solely as background, **not as current next steps**.

Read with plan `docs/WIFI_REWRITE_PLAN.md` 12.1 I14–I16 and 13.6.1, and the
router check in `docs/TODO.md`. Branch `claude/wizardly-lovelace-v6tlqz`,
draft PR [#160](https://github.com/Kaelor9/EngineLink/pull/160): no merge,
no release, no Stage 4 (Stage 0 gate still open).

## Where things stand

| Item | State | Evidence |
|---|---|---|
| CI `compile` | **fixed**, green on `05d7337` (3.5 min, both builds) | the runners got HTTP 504 from GitHub release downloads; arduino-cli 1.2.2 now from downloads.arduino.cc (SHA-256 pinned), the core's archives from Espressif's official mirror first (`tools/prefetch_arduino_tools.py`, SHA-256 from the index), cached, two tries, 30 min job cap |
| Router mode | **fault open** | build 2: `/api/live` OK, `/` (110 KB) failed after 20.5 s; build 3 (`0641917`, station 20 MHz + 20 s restored, I14): `/`, `/api/live`, `.local` time out, 1/10 pings, PC's ARP unanswered |
| Working hypothesis | the station keeps transmitting but stops receiving until the driver restarts (I16) | every router run: beacons lost, then 49 full scans without the router for 2 min, while the own Wi-Fi started right after hears the phone and the router still lists EngineLink |
| Report / SD log | **fixed** (`c9b9897`, I15) | router counters, last signal and channel before a loss, fallbacks, beacon timeout read back |
| Prism comparison | done (I16) | Prism uses WiFiManager and the driver's defaults: no bandwidth, beacon timeout, country, power-save or TX-power settings, no WPA3 wrapper |
| A/B test build | **built**, not yet run | `EL_STA_DEFAULTS=1`: router mode on the driver's defaults as Prism, nothing else different; CI artifact `EngineLink-AB-test-router-driver-defaults-<sha>` |
| Standalone | unchanged | 1048 kB/s on the bench (owner, build 2) |

Local builds given to the owner (core 3.3.12, source `7856b63`):

```text
a42fc825b5418ce4cd267c8166ca6d45aef61db985875dbb3764fbea55d12cd7  EngineLink-AB-router-driver-defaults.bin
f79a7e00894024693a23d69bb926618f4a390467972c2663006a5ff8ab0e2b06  EngineLink-0.16.6-bench4.bin
```

## Next action (owner): one router check, the A/B build

As in `docs/TODO.md` "Router check, one short test": copy `/WIFI.LOG` and
`/WIFIREP.TXT` from the SD card (they cover build 3), install the A/B
build from a file, router mode, `ping -n 100`, 20 × `/api/live`,
10 × `/`, 15 min, then the report. Pass criteria (owner, 2026-11-08):
≥ 99/100 pings answered by EngineLink, 20/20 `/api/live` within 2 s,
10/10 complete `/` within 5 s, 15 min without unexpected router
disconnects or Recovery, standalone unchanged. Then put the normal build
back.

## How to read the result (implementer's next step)

- **A/B build passes**: one of EngineLink's station settings (20 MHz, beacon
  timeout, country DK with 802.11d, power save off, TX power 19.5 dBm)
  causes it. Find which by halving, then record the fix per plan rules
  (D15 fixes the TX power, D27 the country; changing them needs the owner).
- **A/B build fails the same way**: not the firmware's radio settings. Read
  the I15 lines in the SD log: a steady last signal before the loss means
  the receiver stopped, a falling one the RF path. Next, Prism on the same
  router and spot separates the board from the place.
- Either way the single server task makes a lossy link worse (`/` blocks
  everything else, I9 open, owner's decision).
