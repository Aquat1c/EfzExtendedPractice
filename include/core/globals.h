#pragma once
#include <atomic>
#include <windows.h>

extern std::atomic<bool> g_isShuttingDown;

extern std::atomic<bool> g_initialized;

extern std::atomic<bool> g_featuresEnabled;

extern HMODULE g_hSelfModule;