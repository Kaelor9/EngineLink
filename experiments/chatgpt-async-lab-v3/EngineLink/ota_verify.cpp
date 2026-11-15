// Delay OTA confirmation until the async HTTP + WebSocket health test passes.
extern "C" bool verifyRollbackLater() { return true; }
