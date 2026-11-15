// Preserve the previous OTA slot until the independent sketch has booted for 60 s.
extern "C" bool verifyRollbackLater() { return true; }
