# EngineLink Wi-Fi 2.0 / v0.16.6 — owner hardware evidence

**Recorded:** 2026-11-06 (reports provided by owner). **Firmware:** 0.16.6 development build, Wi-Fi rewrite Stages 1–3. **Branch:** `claude/wizardly-lovelace-v6tlqz` (draft PR #160).  
**Purpose:** evidence for Opus to assess Stage 0 (root-cause gate, plan §13.6) and remaining Stage 1/2 bench acceptance. This file is **not** approval to merge, release, or advance to Stage 4 automatically. No firmware or configuration changes were made by this documentation commit.

## Source material

All five primary Wi-Fi reports below are preserved in this folder, including their embedded machine-readable JSON, verbatim except for one edit: the router's network name is replaced by `<router name hidden>` (2026-11-06, privacy; the earlier commits of this folder still contain it). These are user-supplied tests, not simulated runs.

| File | Setup | Samples | Mean throughput | Mean request RTT | AP client RSSI at report | HTTP send failures |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| [USB-1.txt](USB-1.txt) | USB-1 standalone | 5 | 65.6 kB/s | 123.8 ms | -47 dBm | 0 |
| [LAB-1.txt](LAB-1.txt) | LAB-1 standalone | 7 | 83.1 kB/s | 97.4 ms | -40 dBm | 0 |
| [USB-2.txt](USB-2.txt) | USB-2 standalone | 5 | 707.1 kB/s | 13 ms | -40 dBm | 0 |
| [STABILITY-30MIN.txt](STABILITY-30MIN.txt) | standalone, two clients, 2239 s uptime | 12 | 793.7 kB/s | 11.6 ms | -48 / -44 dBm | 0 |
| [ROUTER.txt](ROUTER.txt) | router (name hidden), then recovery AP | 0 | n/a | n/a | -40 dBm on recovery AP | 14 |

**Important controls/limitations:**
- All throughput numbers above are **client downloads on the board's own SoftAP**, not router throughput. The `ROUTER.txt` report contains **no router speed measurement**.
- USB–lab–USB is an A–B–A attempt, but the client RSSI differs (USB1 -47 dBm, LAB -40, USB2 -40), and speed tests occurred at different uptimes. This is evidence of variability, not a controlled proof of a defective buck/LDO or of a pure RF cause.
- The 30-minute exercise is evidenced by uptime and app usage, but uptime itself is not a formal uninterrupted-duration certificate. The 2239 s report shows 2 AP clients, 2 joins, **0 leaves**, 6660 HTTP requests, **0 HTTP send failures**, **1 HTTP 503**, and slowest request **2617 ms** at `/api/series/recent?secs=600`. Longest main-loop pass 71 ms; engine-task max wait 105 ms. **Promising partial bench evidence, not an unconditional pass of latency/zero-503 acceptance.**
- USB1: slowest HTTP request 4521 ms at `/api/speedtest?kb=64`, 0 send failures, main-loop max 66 ms. LAB: 2473 ms at `/`, 0 send failures, main-loop max 60 ms. USB2: slowest request 111 ms, main-loop max 18 ms.
- The three HTTP smoke/security commands were run by the owner in Windows PowerShell and screenshot verified: `GET /api/live` -> **200**, `GET /api/live` with `Host: evil.example` -> **421**, `POST /api/speedlog` without `X-EngineLink` -> **403**. Remaining Stage 2 security, upload, export-regression, stress checks are **not claimed complete**.
- OTA boot self-test was previously reported as `passed (client response)`; subsequent reports say `not needed (this firmware was confirmed before)`.

## Router failure (correct password)

`ROUTER.txt` confirms actual association at **+67 s**, DHCP IP `192.168.0.2` at **+69 s**, `code 200: Router beacons lost` at **+1174 s** (~18 min 25 sec after IP), and `code 201: Router not found` **49** times until **+1292 s**, then return to recovery SoftAP at +1309 s. There were **14 HTTP send failures**, slowest request **10935 ms** at `/`, main-loop max **1556 ms**, engine-task max wait **1568 ms** (network portion **1534 ms**).

**Diagnostic limitation:** after recovery the report shows router signal **0 dBm** and channel **0** despite recording a last router IP; these are not credible measured RF values. Investigate preserving channel/RSSI and meaningful event counters across the switch from STA to SoftAP. Current post-recovery `associationEvents=0`, `dhcpIpEvents=0` despite explicit association/IP events in the trace. Do not assume the "0" counter means no connection occurred.

### Earlier trials (diagnostic context, not independent clean controls)

- [earlier/USB-initial.txt](earlier/USB-initial.txt): early USB report with fast downloads and AP reconnects.
- [earlier/LAB-wrong-password.txt](earlier/LAB-wrong-password.txt): original lab router password was **mistyped**. Its WPA handshake failures are not evidence of a defect with the correct credentials.
- [earlier/LAB-corrected-password.txt](earlier/LAB-corrected-password.txt): lab attempt after owner corrected password; received IP `192.168.0.3` at +397 s, lost beacons at +596 s; own-AP throughput poor. It is separate from the later `ROUTER.txt` test.

Do **not** conflate wrong-password WPA failures with the later beacon-loss events.

## Oscilloscope evidence (owner's 20-image Rigol DHO804 ZIP, reviewed in chat)

- Measurement target: non-isolated ESP32-S3 **3.3 V** supply, USB versus normal lab supply, idle and active Wi-Fi.
- Captured Vmin readings approximately **3.25–3.26 V** and Vmax approximately **3.36–3.40 V**; no captured reading below **3.0 V** was identified. No documented brownout resets in the new bench reports.
- A single reported **124.5 mVpp peak** in the lab Wi-Fi measurement is **not proven genuine rail ripple**. The owner notes awkward spring-ground probe contact; screenshots use different timebases, so there is plausible probe/contact artefact and limited comparability.
- **Preliminary finding:** no demonstrated severe 3V3 rail collapse in the available captures. Hardware power integrity is **not definitively cleared**: probe setup, acquisition limits, unmeasured 5 V/12 V nodes, and the USB+12 V interaction remain limitations.
- The **original oscilloscope ZIP is held by the owner** and was not added to this repository commit. This note does not substitute for time-aligned raw waveforms.
- Owner explicitly prefers **no further repetitive probing** solely to chase the isolated 124.5 mV peak unless a new, concrete hypothesis requires it.

## Gate assessment requested from Opus

1. **Review root-cause gate §13.6 with these results.** State explicitly whether they justify selecting H1/H2/H3/H4, whether the Stage 0 gate can be closed under the *existing* plan, or precisely which *minimal, discriminating* test is still necessary. Do not re-plan architecture just to avoid a missing measurement.
2. **Investigate** speed variability with comparable client RSSI, 18-minute router beacon loss and 49 `201` retries, 14 HTTP send failures after router loss, possible CPU/engine-lock stalls during router mode, and the isolated 503 / 2.6 s latency in the 30-minute standalone run. Prior 7–18 s SD requests should also be understood if reproducible; `/api/sd/wipe` is destructive and is **not** a diagnostic test to repeat.
3. **Stage 2 partially demonstrated on hardware** (self-test, app, throughput, three security cases, two-client run), not all plan §9.2 acceptance rows completed. **Stage 0 not formally passed**; boat RF/router-off comparison and complete power-path characterization from §13.6 were not supplied. The old-firmware A/B comparison was not controlled.
4. Preserve the user's settings and SD data. No firmware changes, branch merges, or releases in this evidence-only commit. Respect the draft PR #160 and boat acceptance gate (§9.9).

**Privacy/provenance:** owner-submitted Wi-Fi reports contain local addresses and station maker prefixes (two of three are randomised private addresses), no router passwords; the router SSID is hidden. The router is a dedicated 4G router (owner, 2026-11-06), not a phone hotspot. The personal `EngineLink-settings(1).json` backup was deliberately excluded as unnecessary for this diagnosis.
