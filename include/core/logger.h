#pragma once
#include <windows.h>
#include <string>
#include <fstream>
#include <mutex>
    #include <atomic>

extern std::mutex g_logMutex;
extern std::ofstream g_log;
extern std::atomic<bool> detailedTitleMode;
extern std::atomic<bool> detailedDebugOutput;
extern std::atomic<bool> g_reducedLogging;

namespace Logger {
    std::string hwndToString(HWND hwnd);
}

void LogOut(const std::string& msg, bool consoleOutput = false);
void InitializeLogging();
void UpdateConsoleTitle();
short GetCurrentMoveID(int player);
void FlushPendingConsoleLogs();
void SetConsoleReady(bool ready);
void SetReducedLogging(bool reduced);
void SetCurrentLogMatchInternalFrame(int internalFrame);
int GetCurrentLogMatchInternalFrame();
