// The last crash, read from the core dump the chip saves in flash when it
// crashes. The program counter and backtrace addresses can be matched to the
// source with the firmware's .elf file (xtensa-esp32s3-elf-addr2line).
#include <Arduino.h>
#include <esp_core_dump.h>

String elCrashSummary() {
  String s;
  if (esp_core_dump_image_check() != ESP_OK) return s;  // no saved crash
  esp_core_dump_summary_t *sum = (esp_core_dump_summary_t *)malloc(sizeof(esp_core_dump_summary_t));
  if (!sum) return s;
  if (esp_core_dump_get_summary(sum) == ESP_OK) {
    char b[24];
    s += "Last crash: task ";
    s += String(sum->exc_task).substring(0, 15);
    snprintf(b, sizeof(b), " · PC 0x%08lx", (unsigned long)sum->exc_pc); s += b;
    snprintf(b, sizeof(b), " · cause %lu", (unsigned long)sum->ex_info.exc_cause); s += b;
    snprintf(b, sizeof(b), " · address 0x%08lx", (unsigned long)sum->ex_info.exc_vaddr); s += b;
    s += "\nBacktrace:";
    for (uint32_t i = 0; i < sum->exc_bt_info.depth && i < 16; i++) {
      snprintf(b, sizeof(b), " 0x%08lx", (unsigned long)sum->exc_bt_info.bt[i]); s += b;
    }
    if (sum->exc_bt_info.corrupted) s += " (incomplete)";
    s += "\nFirmware build (ELF SHA256): ";
    s += String((const char *)sum->app_elf_sha256).substring(0, 16);
    s += "\n";
  }
  free(sum);
  return s;
}
