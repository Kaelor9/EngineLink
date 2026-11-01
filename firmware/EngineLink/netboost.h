// More data in flight per TCP connection (netboost.cpp).
#pragma once
#include <stdint.h>
// Adds `extra` bytes to the send buffer of the connection behind socket fd.
bool elBoostSocket(int fd, uint16_t extra);
extern uint32_t elBoostedSockets, elBoostMisses;
