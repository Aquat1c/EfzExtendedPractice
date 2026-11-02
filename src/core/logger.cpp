#include <fstream>
#include <iomanip>
#include <chrono>
#include "../include/core/logger.h"
#include "../include/core/version.h"
#include "../include/utils/utilities.h"
#include "../include/utils/network.h"

#include "../include/core/memory.h"
#include "../include/core/constants.h"
#include <iostream>
#include <sstream>
#include <deque>
#include <chrono>
#include <iomanip>
#include <windows.h>
#include <atomic>
#include <thread>
#include "../include/game/game_state.h"
#include "../include/core/globals.h"
 
#include "../include/input/input_motion.h"
#include "../include/game/frame_monitor.h"
#include "../include/game/character_settings.h"
#include "../include/utils/debug_log.h"

std::mutex g_logMutex;
std::atomic<bool> detailedTitleMode(false);
std::atomic<bool> detailedDebugOutput(false);
std::atomic<bool> g_reducedLogging(true);

static std::deque<std::string> g_pendingConsoleLogs;
static size_t g_pendingConsoleLogsDropped = 0;
std::atomic<bool> g_consoleReady{false};
static std::atomic<int> g_logMatchInternalFrame{-1};

namespace Logger {
    std::string hwndToString(HWND hwnd) {
        if (hwnd == NULL) {
            return "NULL";
        }
        std::ostringstream oss;
        oss << hwnd;
        return oss.str();
    }
}

void SetCurrentLogMatchInternalFrame(int internalFrame) {
    g_logMatchInternalFrame.store(internalFrame, std::memory_order_relaxed);
}

int GetCurrentLogMatchInternalFrame() {
    return g_logMatchInternalFrame.load(std::memory_order_relaxed);
}

void LogOut(const std::string& msg, bool consoleOutput) {
    if (g_isShuttingDown.load()) {
        return;
    }

    if (DebugLog::IsEnabled() && !msg.empty()) {
        DebugLog::Write(msg);
    }
    
    if (consoleOutput) {
        std::string currentCategory = "OTHER";
        {
            size_t startBracket = msg.find('[');
            size_t endBracket = msg.find(']', startBracket);
            if (startBracket != std::string::npos && endBracket != std::string::npos) {
                currentCategory = msg.substr(startBracket + 1, endBracket - startBracket - 1);
            }
        }
        bool isDetailedDebugMsg =
            currentCategory == "WINDOW" ||
            currentCategory == "OVERLAY" ||
            currentCategory == "IMGUI" ||
            currentCategory == "IMGUI_MONITOR" ||
            currentCategory == "CONFIG" ||
            currentCategory == "KEYBINDS" ||
            currentCategory == "INPUT_BUFFER" ||
            currentCategory == "BUFFER_FREEZE" ||
            currentCategory == "BUFFER_DEBUG" ||
            currentCategory == "BUFFER_COMBO" ||
            currentCategory == "BUFFER_DUMP" ||
            currentCategory == "AUTO-ACTION" ||
            currentCategory == "TRIGGER_DIAG" ||
            currentCategory == "DELAY" ||
            currentCategory == "COOLDOWN" ||
            currentCategory == "DASH" ||
            currentCategory == "DASH_DEBUG" ||
            currentCategory == "AUTO_GUARD" ||
            currentCategory == "CRG" ||
            currentCategory == "RG" ||
            currentCategory == "CANCEL";
        if (isDetailedDebugMsg && !detailedLogging.load()) {
            return;
        }

        std::lock_guard<std::mutex> lock(g_logMutex);

        auto buildPrefix = []() -> std::string {
            auto now = std::chrono::system_clock::now();
            auto timeT = std::chrono::system_clock::to_time_t(now);
            tm timeInfo{};
            localtime_s(&timeInfo, &timeT);
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
            char ts[32];
            std::strftime(ts, sizeof(ts), "%H:%M:%S", &timeInfo);
            const int matchInternalFrame = GetCurrentLogMatchInternalFrame();
            std::ostringstream tsoss;
            tsoss << ts << "." << std::setw(3) << std::setfill('0') << ms.count() << " ";
            if (matchInternalFrame >= 0) {
                static const char* kFrameSuffix[3] = { "00", "33", "66" };
                const int visualFrame = matchInternalFrame / 3;
                const int subframe = matchInternalFrame % 3;
                tsoss << "MF" << visualFrame << "." << kFrameSuffix[subframe] << " ";
            } else {
                tsoss << "MF----.-- ";
            }
            return tsoss.str();
        };

        // Drained only when a console appears, so capacity is fixed. Report overflow to the file log,
        // never through LogOut: g_logMutex is held here.
        if (!g_consoleReady.load() || GetConsoleWindow() == nullptr) {
            std::string formatted = msg.empty() ? std::string() : (buildPrefix() + msg);
            constexpr size_t kMaxPending = 2000;
            if (g_pendingConsoleLogs.size() >= kMaxPending) {
                g_pendingConsoleLogs.pop_front();
                ++g_pendingConsoleLogsDropped;
                if (g_pendingConsoleLogsDropped == 1 ||
                    (g_pendingConsoleLogsDropped % 500) == 0) {
                    DebugLog::Write("[LOGGER][OVERFLOW] Pending console buffer full; dropped " +
                                    std::to_string(g_pendingConsoleLogsDropped) +
                                    " oldest line(s)");
                }
            }
            g_pendingConsoleLogs.emplace_back(std::move(formatted));
            return;
        }

        if (msg.empty()) {
            std::cout << std::endl;
            return;
        }

        static std::string lastCategory = "";
        static bool wasEmptyLine = false;

        bool isHelpMessage = (msg.find("Key") != std::string::npos && msg.find(":") != std::string::npos) ||
                             msg.find("NOTE:") != std::string::npos ||
                             msg.find("---") != std::string::npos;

        if (!wasEmptyLine && !isHelpMessage && !lastCategory.empty() && currentCategory != lastCategory) {
            std::cout << std::endl;
        }

        if (g_reducedLogging.load() && !detailedLogging.load()) {
            static std::string s_lastMsg;
            static auto s_lastMsgAt = std::chrono::steady_clock::time_point{};
            auto nowSteady = std::chrono::steady_clock::now();
            if (msg == s_lastMsg &&
                s_lastMsgAt.time_since_epoch().count() != 0 &&
                (nowSteady - s_lastMsgAt) < std::chrono::milliseconds(250)) {
                return;
            }
            s_lastMsg = msg;
            s_lastMsgAt = nowSteady;
        }

        {
            std::string formatted = buildPrefix() + msg;
            std::cout << formatted << std::endl;
        }

        wasEmptyLine = msg.empty();
        if (!msg.empty() && !isHelpMessage) {
            lastCategory = currentCategory;
        }
    }
}

void InitializeLogging() {
    std::thread titleThread([]() {
        UpdateConsoleTitle();
    });
    titleThread.detach();

    LogOut("==============================================", true);
    LogOut(std::string("  EfzExtendedPractice v") + EFZ_TRAINING_MODE_VERSION, true);
    LogOut(std::string("  Build: ") + EFZ_TRAINING_MODE_BUILD_DATE + " " + EFZ_TRAINING_MODE_BUILD_TIME, true);
    LogOut("==============================================", true);
    
    EfzRevivalVersion detectedVer = GetEfzRevivalVersion();
    std::string verMsg = "[VERSION] Detected: ";
    verMsg += EfzRevivalVersionName(detectedVer);
    verMsg += IsEfzRevivalVersionSupported(detectedVer) ? " (supported)" : " (UNSUPPORTED)";
    LogOut(verMsg, true);
}

short GetCurrentMoveID(int player) {
    uintptr_t base = GetEFZBase();
    if (base == 0) return 0;
    
    uintptr_t baseOffset = (player == 1) ? EFZ_BASE_OFFSET_P1 : EFZ_BASE_OFFSET_P2;
    uintptr_t moveIDAddr = ResolvePointer(base, baseOffset, MOVE_ID_OFFSET);
    
    short moveID = 0;
    if (moveIDAddr) memcpy(&moveID, (void*)moveIDAddr, sizeof(short));
    return moveID;
}

void UpdateConsoleTitle() {
    std::string lastTitle;
    int sleepMs = 100;
    int stableIters = 0;

    while (!g_isShuttingDown.load()) {
        if (g_onlineModeActive.load() || !g_featuresEnabled.load()) {
            Sleep(500);
            continue;
        }
        HWND hWnd = GetConsoleWindow();
        if (hWnd == nullptr || !IsWindow(hWnd) || !IsWindowVisible(hWnd)) {
            Sleep(500);
            continue;
        }
        FrameSnapshot snap{};
        if (!TryGetLatestSnapshot(snap, 500) || snap.mode != GameMode::Practice) {
            Sleep(500);
            continue;
        }
        const std::string p1Name = snap.p1CharId >= 0
            ? CharacterSettings::GetCharacterName(snap.p1CharId) : "?";
        const std::string p2Name = snap.p2CharId >= 0
            ? CharacterSettings::GetCharacterName(snap.p2CharId) : "?";
        char title[512];
        sprintf_s(title, sizeof(title),
            "P1 (%s): %d HP, %d Meter, %.1f RF | P2 (%s): %d HP, %d Meter, %.1f RF | Frame: %u | Mode: Practice",
            p1Name.c_str(), snap.p1Hp, snap.p1Meter, snap.p1RF,
            p2Name.c_str(), snap.p2Hp, snap.p2Meter, snap.p2RF,
            GetDisplayedFrameCounter() / 3u);
        if (lastTitle != title) {
            SetConsoleTitleA(title);
            lastTitle = title;
            sleepMs = 100;
            stableIters = 0;
        } else if (++stableIters > 2) {
            sleepMs = 250;
        }
        Sleep(sleepMs);
    }
}
void FlushPendingConsoleLogs() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_consoleReady.load() && GetConsoleWindow() != nullptr) {
        if (g_pendingConsoleLogsDropped != 0) {
            std::cout << "[LOGGER][OVERFLOW] " << g_pendingConsoleLogsDropped
                      << " old buffered console line(s) were dropped" << std::endl;
        }
        for (const auto& line : g_pendingConsoleLogs) {
            if (line.empty()) {
                std::cout << std::endl;
                continue;
            }
            size_t startBracket = line.find('[');
            size_t endBracket = line.find(']', startBracket);
            std::string currentCategory = "OTHER";
            if (startBracket != std::string::npos && endBracket != std::string::npos) {
                currentCategory = line.substr(startBracket + 1, endBracket - startBracket - 1);
            }
            bool isDetailedDebugMsg =
                currentCategory == "WINDOW" ||
                currentCategory == "OVERLAY" ||
                currentCategory == "IMGUI" ||
                currentCategory == "IMGUI_MONITOR" ||
                currentCategory == "CONFIG" ||
                currentCategory == "KEYBINDS";
            if (isDetailedDebugMsg && !detailedLogging.load()) {
                continue;
            }
            std::cout << line << std::endl;
        }
        g_pendingConsoleLogs.clear();
        g_pendingConsoleLogsDropped = 0;
    }
}

void SetConsoleReady(bool ready) {
    g_consoleReady = ready;
    if (ready) {
        FlushPendingConsoleLogs();
    }
}

void SetReducedLogging(bool reduced) {
    g_reducedLogging.store(reduced);
}
