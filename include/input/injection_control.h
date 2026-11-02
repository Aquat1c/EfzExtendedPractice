#pragma once
#include <atomic>

// Index 0 is unused; use [1] for P1, [2] for P2.
extern std::atomic<bool> g_forceBypass[3];
extern std::atomic<bool> g_pollOverrideActive[3];
extern std::atomic<uint8_t> g_pollOverrideMask[3];
