# 2026-11-09 — EngineLink 0.16.6, stock ESP32 station/TCP, Maggan failure

**Read this first, alongside [the full device report](firmware-75faba4-maggan-recovery.txt).** This is new evidence after the owner's IC5 solder repair and after removing the two special network implementations; it supersedes the earlier A/B-test instructions in the 2026-11-08 session handoff. Evidence, not a verified root-cause analysis.

## Build and reproduction

- Development branch: `claude/wizardly-lovelace-v6tlqz`, draft PR [#160](https://github.com/Kaelor9/EngineLink/pull/160), **not merged or released**.
- Firmware version string remains **0.16.6** across multiple different builds. Identify this test by **build timestamp `Nov 9 2026 14:08:09`**, commit **`75faba4253eb6d3a755541995ca8892fa2e47a0d`**, and successful [GitHub Actions compile run #261](https://github.com/Kaelor9/EngineLink/actions/runs/42631425052). The run produced one application artifact, `EngineLink-App-ESP32S3-75faba4253eb6d3a755541995ca8892fa2e47a0d`.
- The immediately preceding [run #260](https://github.com/Kaelor9/EngineLink/actions/runs/42629964143) **failed to compile** at `wifi.ino:255` because the cleanup removed `routerHoldSince`, a Recovery variable unrelated to WPA3. Restored in `75faba4`; #260 must not be used or rerun as the fix.
- Removed `wifiwpa3.cpp/.h` and the `--wrap=esp_wifi_init` linker interception; removed `netboost.cpp/.h` and private lwIP `tcp_pcb->snd_buf` writes. The normal build no longer includes the previous A/B driver-default variant. Router STA uses Arduino ESP32 3.3.12 / IDF 5.5.5 WPA2/WPA3 and radio defaults. **These changes did not resolve router reachability.**
- Following a repaired suspected cold solder joint on **IC5 (pins 5–8, especially pin 8)**, the board again had its normal 3.3 V rail and worked in own-Wi-Fi mode. A plausible hardware fault was fixed; **this does not prove hardware or firmware caused the router issue**.
- Maggan (TP-Link router) was powered on and working for both Windows PC and iPhone. The owner did not change the router's Wi-Fi channel or security to accommodate EngineLink.

## What worked

- **Standalone/Recovery AP:** Windows PC can join EngineLink's Wi-Fi, open the web UI, and use **Test speed**. In this specific report, 12 tests measure **539.6–745.9 kB/s**, RTT **8–12 ms**. Earlier on the repaired board, the owner saw ~845–1,105 kB/s on a different firmware build. Do not combine these as one run.
- The report's aggregate HTTP counters during the boot: **1,680 requests**, **1,676 external successful answers**, **0 send failures**, **0 busy (503)**, slowest **146 ms** for `/`. These counters span the entire boot, particularly functioning own-Wi-Fi sessions, **not evidence that HTTP worked over Maggan**.
- Loop/engine figures in the report: longest main-loop pass **111 ms**, longest engine mutex wait **79 ms**, **0** waits over 100 ms, longest engine round **11 ms**; no reset reason 9 (brownout) reported on this boot.

## What failed, with exact timeline from the device report

At report uptime **1,798 s**, EngineLink was in **Recovery AP**, not on Maggan. Counters since boot: **1 successful association**, **1 router DHCP IP**, **1 active connection lost**, **196 pre-connection failures**, **197 disconnect events**. These 197 are not 197 successfully connected sessions.

| Uptime | Observation |
|---|---|
| +0–118 s | Initial router attempt yielded **49 `201` / router not found** events; Recovery AP subsequently starts. The report does not prove why discovery initially failed. |
| +1,067 s | A new connect began; **associated with Maggan** within 422 ms; `WPA2-Personal`, channel **1**, last signal **−59 dBm**. |
| +1,069 s | DHCP supplied **`192.168.0.2`** after 1,707 ms. Maggan's client list showed this entry, but that alone cannot establish live bidirectional traffic. |
| During router session | On a PC connected to Maggan, `http://enginelink.local` timed out / name lookup failed; **`http://192.168.0.2/` and `http://192.168.0.2/api/health` both timed out**. Thus not only an mDNS/hostname failure. No packet capture or ARP/ping from this specific build yet separates layer 2/3 from HTTP-layer causes. |
| +1,608 s | Router link **disconnected, reason `200` (beacons lost)**, about **539 s after obtaining IP**. |
| +1,610–1,726 s | **49** additional `201` router-not-found events. |
| +1,728 s | `STA_LOST_IP` was logged; the event arrives well after the disconnection and is not proof that the IP remained useful until then. |
| +1,759 s | PC/phone joined Recovery AP again, permitting the report to be retrieved. |

Additional nuances: report says **2 recovery fallbacks**, **0 auto-returns**, **1 app-initiated router attempt** (yet logs show two connect-start entries), **9 AP starts**, **0 AP watchdog restarts**, driver reason **36** (later intentional mode change), and `failedReason=0` despite repeated `201` events. Some diagnostic counters/wording may be inconsistent; audit before relying on their precise semantics.

## Power and oscilloscope context

- After IC5 rework: lab supply around **13.50 V**, **0.088–0.089 A** average at the upstream supply. Series sense resistor initially believed 1 Ω was measured to be **0.1 Ω**; Rigol DHO804 shunt average measured ~75–84 mA (close to lab supply), with brief apparent peaks ~230–300 mA that **may include measurement noise and ringing**.
- Own-Wi-Fi speed tests while measuring current did not show a sustained large current increase. **No synchronized oscilloscope test of the 3.3 V rail during router association, beacon loss or Wi-Fi traffic has established its minimum transient voltage.** Do not assert power integrity or a hardware root cause from these measurements.

## The owner's exact product requirements (2026-11-09)

Only **two equally valid user-selectable modes**:
1. **Standalone:** EngineLink itself broadcasts Wi-Fi, a phone/PC joins it and the complete web app loads and works.
2. **Router:** EngineLink associates to an ordinary compatible 2.4 GHz router (including home/4G/5G routers, WPA2, WPA3, or mixed as supported), receives an IP, and any device on the same LAN can open and use the complete web app. No user router-channel or security reconfiguration.

No special Wi-Fi/TCP linker interposition, private lwIP manipulation, or ad-hoc undocumented driver workarounds. Prefer documented Espressif / Arduino ESP32 APIs. Automatic router failure fallback may remain as a recovery mechanism; it is **not** a third user-selected product mode. The browser UI must actually serve over each mode; association/DHCP or a router's remembered client entry alone do not count as success.

**Next actor: Fable, architecture and root-cause review only.** Read this complete evidence, previous 2026-11-06 and 2026-11-08 reports, current implementation, `CLAUDE.md`, `HANDOVER.md`, `docs/STATUS.md`, `docs/TODO.md`, and `docs/WIFI_REWRITE_PLAN.md`. Critically reassess prior design decisions and write a **revised, evidence-backed plan** (including why HTTP on the router failed before beacons were lost), with a minimal decisive verification path. Avoid repeated A/B builds and repeated owner tests without a discriminating hypothesis. **Do not implement, merge, or release until the owner approves the new plan.**

## Data provenance and limitations

Raw report: [`firmware-75faba4-maggan-recovery.txt`](firmware-75faba4-maggan-recovery.txt). It was copied from the owner's provided `Indsat tekst(10).txt` after removing an unrelated browser-added message preceding `ENGINELINK - WI-FI REPORT`. The original report contains non-printable characters after `Wi-Fi country (driver): DK`; those bytes were escaped as printable `\\xNN` sequences for safe GitHub text storage. The full machine-readable JSON is retained. Browser timeouts and power measurements are owner-observed and not independently instrumented. No Wi-Fi packet capture from this specific build exists in the supplied evidence.
