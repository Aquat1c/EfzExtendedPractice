#include "../include/core/globals.h"

std::atomic<bool> g_isShuttingDown(false);

HMODULE g_hSelfModule = nullptr;