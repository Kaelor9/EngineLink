// WPA3 on or off, chosen at run time.
//
// Most ESP32 products (Shelly, Tasmota, ESPHome builds and the like) join a
// router with WPA2: their Wi-Fi is built without WPA3, so on a WPA2/WPA3
// "mixed" router they use WPA2, which every router of the last 15 years
// handles well. The prebuilt Arduino ESP32 libraries always offer WPA3, and
// some routers (a Zyxel in the field) then fail the WPA3-SAE login again and
// again. Arduino starts the Wi-Fi driver with esp_wifi_init(); with the link
// option -Wl,--wrap=esp_wifi_init this function sits in front of it and
// clears the driver's WPA3-SAE capability unless EngineLink allows it.
// EngineLink allows WPA3 only for a router that needs it (WPA3-only); the
// driver is started again on every Wi-Fi mode change, so this can switch
// without a restart. In its own file: Arduino would add a C++ prototype for
// it in the sketch, which clashes with the C linkage the linker needs.
#include <esp_wifi.h>

volatile bool elAllowSae = false;
// A-MPDU (several Wi-Fi frames sent as one bundle) from EngineLink to the
// router. Single pings got through every time while bulk downloads through a
// Zyxel crawled at a few kB/s: the classic sign of a router that handles the
// ESP32's bundles badly. Off by default in router mode (Diagnostics can turn
// it on); the phone-to-EngineLink direction is not affected.
volatile bool elAmpduTx = true;

extern "C" esp_err_t __real_esp_wifi_init(const wifi_init_config_t *config) __attribute__((weak));

extern "C" esp_err_t __wrap_esp_wifi_init(const wifi_init_config_t *config) {
  wifi_init_config_t c = *config;
  if (!elAllowSae) c.feature_caps &= ~(uint64_t)CONFIG_FEATURE_WPA3_SAE_BIT;
  if (!elAmpduTx) c.ampdu_tx_enable = 0;
  return __real_esp_wifi_init(&c);
}

// False when built without the link option: then WPA3 cannot be switched off.
bool elSaeControlActive() { return __real_esp_wifi_init != nullptr; }
