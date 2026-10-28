// A new firmware image confirms itself after a healthy minute (see loop() in
// EngineLink.ino). Until then the bootloader rolls back to the previous image
// if the new one crashes or restarts. Kept in its own file because the
// Arduino prototype generator mishandles extern "C" functions in a sketch.
extern "C" bool verifyRollbackLater() { return true; }
