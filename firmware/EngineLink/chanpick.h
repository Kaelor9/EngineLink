// Automatic channel for the own Wi-Fi (Standalone), the way routers' "Auto"
// channel works: measure how busy the air is on 1, 6 and 11 (other networks'
// traffic, not just how many there are) and use the quietest. Plan section 19.
// Pure C++, no radio calls; host-tested by tools/test_chanpick.cpp, fed by
// chanauto.ino from the Wi-Fi driver's documented sniffer.
#pragma once
#include <stdint.h>

namespace chanpick {

static const uint8_t CANDIDATES[3] = {1, 6, 11};
static const uint16_t BUSY_PERMILLE = 100;   // 10 % of the air used by others: worth a look
static const uint16_t MIN_GAIN_PERMILLE = 50; // a move must win 5 percentage points...
                                              // ...and halve the busy time

inline int indexOf(uint8_t ch) {
  for (int i = 0; i < 3; i++) if (CANDIDATES[i] == ch) return i;
  return -1;
}

// PHY rate of a received frame in kbit/s, from the driver's rx header:
// sigMode 0 = 11b/g (rate code), 1 = 11n (MCS, 20/40 MHz).
inline uint32_t rateKbps(uint8_t sigMode, uint8_t rate, uint8_t mcs, bool wide) {
  if (sigMode == 0) {
    static const uint32_t r[16] = {1000, 2000, 5500, 11000, 0, 2000, 5500, 11000,
                                   48000, 24000, 12000, 6000, 54000, 36000, 18000, 9000};
    const uint32_t k = rate < 16 ? r[rate] : 0;
    return k ? k : 6000;
  }
  static const uint32_t ht[8] = {6500, 13000, 19500, 26000, 39000, 52000, 58500, 65000};
  uint32_t k = ht[mcs % 8] * (uint32_t)(mcs / 8 + 1);
  if (mcs > 31) k = ht[mcs % 8];
  return wide ? k * 2 : k;
}
// Air time of one frame in microseconds: preamble plus the bits at its rate.
inline uint32_t airtimeUs(uint8_t sigMode, uint8_t rate, uint8_t mcs, bool wide, uint16_t len) {
  uint32_t pre = 20;                                   // 11g OFDM
  if (sigMode == 0 && rate < 8) pre = rate < 4 ? 192 : 96;  // 11b long / short preamble
  if (sigMode != 0) pre = 36;                          // 11n mixed mode
  return pre + (uint32_t)(((uint64_t)len * 8000) / rateKbps(sigMode, rate, mcs, wide));
}

// One survey: busy air time heard on each candidate while listening.
struct Survey {
  uint32_t busyUs[3] = {0, 0, 0}, listenMs[3] = {0, 0, 0}, frames[3] = {0, 0, 0};
  void heard(uint8_t ch, uint32_t us, uint32_t ms, uint32_t n) {
    const int i = indexOf(ch);
    if (i < 0) return;
    busyUs[i] += us; listenMs[i] += ms; frames[i] += n;
  }
  // Busy share in per mille; a channel not listened to counts as full.
  uint16_t busyPermille(uint8_t ch) const {
    const int i = indexOf(ch);
    if (i < 0 || !listenMs[i]) return 1000;
    const uint32_t p = busyUs[i] / listenMs[i];   // us per ms = per mille
    return p > 1000 ? 1000 : (uint16_t)p;
  }
  // The channel to use: the quietest, unless `current` is about as good
  // (no hop for a small gain). `current` 0 = no channel yet: the quietest.
  uint8_t pick(uint8_t current) const {
    uint8_t b = CANDIDATES[0];
    for (int i = 1; i < 3; i++) if (busyPermille(CANDIDATES[i]) < busyPermille(b)) b = CANDIDATES[i];
    if (indexOf(current) < 0) return b;
    const uint16_t pb = busyPermille(b), pc = busyPermille(current);
    return (pb * 2 < pc && pc - pb >= MIN_GAIN_PERMILLE) ? b : current;
  }
};

// While the own Wi-Fi runs: the current channel is checked now and then; two
// busy checks in a row ask for a new survey (only done with no phone joined).
struct Watch {
  uint8_t busyRow = 0;
  bool check(uint16_t permille) {
    if (permille < BUSY_PERMILLE) { busyRow = 0; return false; }
    if (++busyRow < 2) return false;
    busyRow = 0; return true;
  }
};

}  // namespace chanpick
