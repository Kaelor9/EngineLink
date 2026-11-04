// Transmit power steps and the radio test (linktune.cpp).
#pragma once
#include <Arduino.h>
#include <WiFi.h>

struct PingStat { uint8_t sent, got; uint16_t minMs, avgMs, maxMs; };
struct LinkTuneStep { uint8_t level; PingStat p; };

// Transmit power steps, lowest to highest (index = step).
extern const wifi_power_t elTxPower[5];
extern const char *const elTxName[5];

// Results of the last test, for the network report.
extern LinkTuneStep linkTuneSteps[5];
extern PingStat linkSmall, linkNet;
extern volatile uint8_t linkTuneCount, linkTuneChosen;
extern volatile bool linkTuneRunning;
extern volatile uint32_t linkTuneAt;
extern volatile bool linkScanDone;
extern volatile uint8_t linkScanTotal, linkScanSame, linkScanOverlap;
extern volatile int8_t linkScanSameBest;
extern String linkScanList;
extern volatile uint8_t linkScanQuiet, linkScanCh;
extern volatile uint32_t linkScanAt, linkScanDurationMs;
extern volatile uint16_t linkScanFailures;
extern volatile uint8_t linkChannelCounts[14], linkChannelStrong[14];

// Pings and channel survey at the current power (`level`, for the report).
// Changes nothing on the radio. Blocks for about 15 s: call it from a task.
void runRadioTest(uint8_t level);
