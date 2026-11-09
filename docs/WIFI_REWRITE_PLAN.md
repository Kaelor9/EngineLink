# Wi-Fi 2.0 plan (2026-11-04 … 2026-11-08) — superseded

This plan is history. On 2026-11-09 the owner asked for a review from the
evidence; the result and the current plan are in
[WIFI_PLAN_2026-11-09.md](WIFI_PLAN_2026-11-09.md). Of the old plan, Stages
1–3 were built (access point radio settings, `esp_http_server`, the OTA
self-test, the trace folding) and stay; Stage 4 (the `wifistate.h` state
machine, the AP+STA idle station, router tests and "looks") and the two
exceptions (`wifiwpa3`, `netboost`) were dropped and removed from the
firmware. The full text of every revision is in this file's git history
(`git log -- docs/WIFI_REWRITE_PLAN.md`); the field evidence it analysed is
under [test-evidence/](test-evidence/).
