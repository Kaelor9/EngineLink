# V3.1 EngineLink asynchronous Wi-Fi diagnostic test evidence

Date: 2026-11-15. Board: ESP32-S3-WROOM-1-N8R8.

## Symptom observed

User repeatedly uploaded v3 OTA image through v2 dashboard and received
{"ok":true,"message":"App-only OTA installed..."}, but returned to v2 after
reboot. The dashboard also displayed Unexpected token '<' / <!doctype is
not valid JSON, meaning that the browser received an HTML response where
JSON was expected. No serial boot log or valid JSON export was available.

## Confirmed source-code issue

The prior v3 deferred bootloader validation until at least 60 seconds of
uptime and multiple successful JSON/API calls (or API + WebSocket activity).
That requirement was unnecessarily restrictive. If bootloader rollback
was active, a restart before validation or a three-minute timeout could
restore v2. This is a plausible cause, not proven without hardware logs.

V3.1 now validates on one real HTTP GET or WebSocket handshake once the
AP/HTTP server has survived 8 seconds. Plain-text /lab/health avoids
requiring JSON. The page can report raw response MIME type and first
bytes if a request returns HTML unexpectedly.

## Gates

- [ ] Arduino-ESP32 3.3.12 + pinned ESP32Async libraries build
- [ ] Test UI JavaScript parse
- [ ] ESP32-S3 8 MiB merged USB image and app-only OTA image generated
- [ ] OTA image descriptor compatibility check and 3.2 MB size limit
- [ ] Normal EngineLink and earlier test installers unaffected
- [ ] Physical v2 -> v3.1 OTA boot and client health response verified
- [ ] Physical v3.1 -> v2 rollback/update independently verified
- [ ] WebSocket live status and 5 x 512 KiB throughput tests
- [ ] Recovery after intentional Wi-Fi failure verified via USB/serial

Do NOT mark hardware and rollback tests complete based on CI alone.
