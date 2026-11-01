// Automatic transmit power and link measurements (linktune.cpp).
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

// Tests every step against the router; returns the step to use and leaves the
// radio on it. Blocks for about half a minute: call it from a task.
uint8_t runLinkTune(uint8_t current);
