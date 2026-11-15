// Defer Arduino-ESP32 OTA validation until v3.1 has observed an actual
// HTTP request or WebSocket handshake on the ESP32 AP.
// The bootloader can roll back if the new app never becomes reachable.
// WARNING: not yet physically tested; do not rely on this as sole recovery.
extern "C" bool verifyRollbackLater() { return true; }
