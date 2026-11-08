# v0.16.6 bench build 3 — router reachability failure (owner evidence, 2026-11-08)

**Status: failed owner hardware test / root cause not established.** This is evidence, not a firmware fix, Stage 0 approval, Stage 4 approval, merge or release.

- **Repo:** `Kaelor9/EngineLink`, development branch `claude/wizardly-lovelace-v6tlqz`, draft PR [#160](https://github.com/Kaelor9/EngineLink/pull/160).
- **Firmware being tested:** owner reports installing the third v0.16.6 bench build from commit [`0641917`](https://github.com/Kaelor9/EngineLink/commit/0641917f1a69e0ff3829ef61c6efc6a63259ef9b), GitHub Actions compile run [42462319124](https://github.com/Kaelor9/EngineLink/actions/runs/42462319124). The firmware's *post-flash build timestamp/report was not supplied*; verify the build identity before attributing a specific radio setting.
- **Date:** 2026-11-08. User-provided Chrome, router UI, and Windows PowerShell screenshots.
- **Equipment:** actual custom EngineLink ESP32-S3 PCB; TP-Link Archer MR600 4G router (SSID not reproduced here), Windows PC on the same router. Firmware power input during this particular build-3 test was not independently documented; do not assume USB or lab supply.
- **Addresses during this run:** router client table listed EngineLink at `192.168.0.2` and PC at `192.168.0.4`. Client list alone is not proof the station is continuously responsive.
- **No engine-bus acceptance/boat validation is claimed.**

## Owner observations (build 3)

1. Chrome `http://enginelink.local` -> `ERR_CONNECTION_TIMED_OUT`. A subsequent attempt produced `ERR_NETWORK_CHANGED` in Chrome; this is a browser/client network-change symptom, not itself proof of an EngineLink disconnect.
2. Chrome `http://192.168.0.2/` -> `ERR_CONNECTION_TIMED_OUT`.
3. Chrome `http://192.168.0.2/api/live` -> `ERR_CONNECTION_TIMED_OUT` during the same follow-up sequence. **Unlike the earlier build-2 test, this failure is not limited to the large home page.**
4. Router's Network Map showed two wireless clients, including `enginelink` at `192.168.0.2` and the PC at `192.168.0.4`.
5. Windows PowerShell `ping 192.168.0.2 -n 10` was run from `192.168.0.4`. Exact relevant console output:

```text
Pinging 192.168.0.2 with 32 bytes of data:
Reply from 192.168.0.4: Destination host unreachable.
Request timed out.
Request timed out.
Reply from 192.168.0.2: bytes=32 time=18ms TTL=64
Request timed out.
Request timed out.
Request timed out.
Request timed out.
Request timed out.
Request timed out.

Ping statistics for 192.168.0.2:
    Packets: Sent = 10, Received = 2, Lost = 8 (80% loss),
Approximate round trip times in milli-seconds:
    Minimum = 18ms, Maximum = 18ms, Average = 18ms
```

**Interpretation:** The Windows summary shows `Received = 2`, but one of those is a **local error from the PC** (`192.168.0.4: Destination host unreachable`), *not* a successful reply from EngineLink. Only **one of ten probes** received an actual ICMP echo reply from `192.168.0.2` (18 ms). Eight further probes timed out. This is severe intermittent IP-level reachability, not merely a DNS/mDNS, gzip, or home-page rendering problem. The precise failure point (PC, AP, RF, driver, firmware, routing/ARP) remains undetermined.

## Contrast with the previous bench build (different firmware; do not conflate)

Before `0641917`, the owner provided a Wi-Fi report identifying `v0.16.6 (built Nov 6 2026 14:02:16)` (bench build 2) and repeatedly received valid JSON at `http://192.168.0.2/api/live`, while `http://192.168.0.2/` timed out. The API reported the router link at about -42 to -43 dBm with the same IP and SD status OK. The build-2 Wi-Fi report also documented a historical `GET /` worst latency of 20,526 ms and 5 HTTP send failures. That report was captured after a 44,923-second uptime, mostly on a different timeline, and cannot prove the later connectivity state.

**Build 3 differs:** after the 20 MHz / 20 s station-radio restoration, the owner reports `/` **and** `/api/live` failing and the ICMP results above. This does **not prove** the radio-setting change caused a regression, nor that it fixed the original defect. A live build-3 Wi-Fi report has **not** been supplied.

Earlier evidence: [v0.16.6 owner bench evidence, 2026-11-06](../wifi-0166-2026-11-06/README.md). Release notes: [v0.16.6, third bench build](../../releases/v0.16.6.md).

## Diagnostic consequence and requested engineering follow-up

- Treat this as a **router-mode network reachability defect** with a possible HTTP symptom, not a proved `GET /`/gzip defect. Establish whether the station remains truly associated and reachable while its DHCP lease persists in the router UI; check actual station state, driver disconnect/reconnect events, Wi-Fi channel/bandwidth/inactivity configuration, ARP/ICMP loss, and server counters rather than assuming one cause.
- Investigate whether the restoration under plan §12.1 I14 is effective *at runtime* (read back the driver setting); isolate the earliest failing layer before modifying HTTP handling or introducing router-specific workarounds.
- Read and follow **`docs/WIFI_REWRITE_PLAN.md`**, especially §6.2, §9 acceptance/gates, §12.1 I14, §13.6/§13.6.1, and decision D30 (router-independent compatibility), together with `HANDOVER.md`, `docs/STATUS.md` and the earlier evidence linked above.
- Only request **one targeted discriminating owner test** if existing evidence and code instrumentation cannot resolve the next hypothesis; avoid repeating broad standalone, power, speed and oscilloscope tests without a concrete reason.
- Any proposed fix must have a defensible root-cause hypothesis, focused automated regression tests and updated plan/evidence documentation. **No unsolicited Stage 4 integration, merge to `main`, or release.** Mark Stage 0/Stage 2 acceptance accurately; the failed test is not a pass.

**Provenance:** Observations and ping transcript transcribed from screenshots supplied by the owner in ChatGPT on 2026-11-08. The image files themselves have not been committed; the numeric output and paths above are the preserved evidence.
