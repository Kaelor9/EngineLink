// WPA3-SAE on or off for the Wi-Fi driver (wifiwpa3.cpp).
#pragma once
// Read when the driver starts: set it before WiFi.mode() brings Wi-Fi up.
extern volatile bool elAllowSae;
// False when built without -Wl,--wrap=esp_wifi_init (WPA3 cannot be switched off).
bool elSaeControlActive();
