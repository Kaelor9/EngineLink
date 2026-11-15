// Keep previous application bootable until the web endpoint has been reached.
// ESP32 bootloader rollback stays pending for the initial 60-second health check.
extern "C" bool verifyRollbackLater() { return true; }
