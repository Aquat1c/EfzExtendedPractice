#include "utils/debug_log.h"
#include "utils/utilities.h"
#include <windows.h>
#include <chrono>
#include <ctime>
#include <cctype>
#include <cstring>

namespace DebugLog {
    // Startup code may assign this atomic before Initialize(); runtime code must use SetEnabled().
    std::atomic<bool> g_EnableDebugLog{false};
    
    static std::ofstream g_LogFile;
    static std::mutex g_LogMutex;
    static bool g_Initialized = false;
    static bool g_HasOpenedThisSession = false;
    static unsigned g_LinesSinceFlush = 0;
    static std::chrono::steady_clock::time_point g_LastFlushAt;
    
    static std::string GetDllDirectory() {
        char path[MAX_PATH];
        HMODULE hModule = NULL;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | 
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)&GetDllDirectory, &hModule)) {
            GetModuleFileNameA(hModule, path, sizeof(path));
            std::string fullPath(path);
            size_t pos = fullPath.find_last_of("\\/");
            return fullPath.substr(0, pos);
        }
        return "";
    }

    static bool ContainsNoCase(const std::string& value, const char* needle) {
        if (!needle || !*needle) return true;
        const size_t needleLen = std::strlen(needle);
        if (needleLen > value.size()) return false;
        for (size_t i = 0; i + needleLen <= value.size(); ++i) {
            size_t j = 0;
            for (; j < needleLen; ++j) {
                const unsigned char lhs = static_cast<unsigned char>(value[i + j]);
                const unsigned char rhs = static_cast<unsigned char>(needle[j]);
                if (std::tolower(lhs) != std::tolower(rhs)) break;
            }
            if (j == needleLen) return true;
        }
        return false;
    }

    static bool IsUrgentMessage(const std::string& message) {
        if (ContainsNoCase(message, "[urgent]") ||
            ContainsNoCase(message, "[error]") ||
            ContainsNoCase(message, "[fatal]") ||
            ContainsNoCase(message, "[crash]") ||
            ContainsNoCase(message, "[exception]") ||
            ContainsNoCase(message, "[seh]") ||
            ContainsNoCase(message, "overflow")) {
            return true;
        }
        return ContainsNoCase(message, "setup") &&
               (ContainsNoCase(message, "fail") ||
                ContainsNoCase(message, "error") ||
                ContainsNoCase(message, "exception"));
    }

    static void GetLocalTime(std::time_t value, std::tm& out) {
        out = {};
        localtime_s(&out, &value);
    }

    static void WriteRuntimeMarkerLocked(const char* marker) {
        if (!g_LogFile.is_open()) return;
        const auto now = std::chrono::system_clock::now();
        const auto time = std::chrono::system_clock::to_time_t(now);
        std::tm local{};
        GetLocalTime(time, local);
        char timeStr[32] = {};
        std::strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &local);
        g_LogFile << "[" << timeStr << "] " << marker << "\n";
    }

    // g_LogMutex must be held; first open truncates, later re-enables append.
    static bool OpenLogFileLocked() {
        if (g_LogFile.is_open()) return true;

        std::string dllDir = GetDllDirectory();
        if (dllDir.empty()) dllDir = ".";
        const std::string logPath = dllDir + "\\efz_training_debug.log";

        g_LogFile.clear();
        const std::ios::openmode mode = std::ios::out |
            (g_HasOpenedThisSession ? std::ios::app : std::ios::trunc);
        g_LogFile.open(logPath, mode);
        if (!g_LogFile.is_open()) {
            const std::string failure =
                "[EFZ_TM][SETUP-FAILURE] Could not open debug log: " + logPath + "\n";
            OutputDebugStringA(failure.c_str());
            return false;
        }

        const auto now = std::chrono::system_clock::now();
        const auto time = std::chrono::system_clock::to_time_t(now);
        std::tm local{};
        GetLocalTime(time, local);
        char timeStr[100] = {};

        if (!g_HasOpenedThisSession) {
            std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &local);
            g_LogFile << "========================================\n";
            g_LogFile << "EfzExtendedPractice Debug Log\n";
            g_LogFile << "Session Start: " << timeStr << "\n";
            g_LogFile << "Log File: " << logPath << "\n";
            g_LogFile << "========================================\n\n";
            g_HasOpenedThisSession = true;
        } else {
            WriteRuntimeMarkerLocked("Debug file logging enabled at runtime");
        }

        g_LinesSinceFlush = 0;
        g_LastFlushAt = std::chrono::steady_clock::now();
        g_LogFile.flush();
        return true;
    }

    bool IsEnabled() {
        return g_EnableDebugLog.load(std::memory_order_acquire);
    }

    bool SetEnabled(bool enabled) {
        std::lock_guard<std::mutex> lock(g_LogMutex);

        if (!enabled) {
            // Publish disabled before closing; writers recheck under this mutex.
            g_EnableDebugLog.store(false, std::memory_order_release);
            if (g_LogFile.is_open()) {
                WriteRuntimeMarkerLocked("Debug file logging disabled at runtime");
                g_LogFile.flush();
                g_LogFile.close();
                g_LogFile.clear();
            }
            g_LinesSinceFlush = 0;
            return true;
        }

        if (!g_Initialized) {
            g_EnableDebugLog.store(true, std::memory_order_release);
            return true;
        }
        if (!OpenLogFileLocked()) {
            g_EnableDebugLog.store(false, std::memory_order_release);
            return false;
        }
        g_EnableDebugLog.store(true, std::memory_order_release);
        return true;
    }

    void Initialize() {
        std::lock_guard<std::mutex> lock(g_LogMutex);
        if (!g_Initialized) g_Initialized = true;
        if (g_EnableDebugLog.load(std::memory_order_acquire) &&
            !OpenLogFileLocked()) {
            g_EnableDebugLog.store(false, std::memory_order_release);
        }
    }
    
    void Write(const std::string& message) {
        if (!IsEnabled()) return;
        
        std::lock_guard<std::mutex> lock(g_LogMutex);
        if (!g_Initialized ||
            !g_EnableDebugLog.load(std::memory_order_acquire)) return;

        if (!g_LogFile.is_open() && !OpenLogFileLocked()) {
            g_EnableDebugLog.store(false, std::memory_order_release);
            return;
        }

        if (g_LogFile.is_open()) {
            auto now = std::chrono::system_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) % 1000;
            auto time = std::chrono::system_clock::to_time_t(now);
            std::tm local{};
            GetLocalTime(time, local);
            char timeStr[100] = {};
            std::strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &local);
            
            g_LogFile << "[" << timeStr << "." << std::setfill('0') << std::setw(3)
                      << ms.count() << "] " << message << "\n";
            const auto steadyNow = std::chrono::steady_clock::now();
            if (++g_LinesSinceFlush >= 16 || IsUrgentMessage(message) ||
                steadyNow - g_LastFlushAt >= std::chrono::milliseconds(250)) {
                g_LogFile.flush();
                g_LinesSinceFlush = 0;
                g_LastFlushAt = steadyNow;
            }
        }
    }
    
    void WriteHex(const std::string& label, uintptr_t address, const void* data, size_t size) {
        if (!IsEnabled()) return;
        
        std::ostringstream oss;
        oss << "[HEXDUMP] " << label << " @0x" << std::hex << std::uppercase << address 
            << " [" << std::dec << size << " bytes]: ";
        
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i) {
            oss << std::hex << std::setw(2) << std::setfill('0') << (int)bytes[i] << " ";
        }
        
        Write(oss.str());
    }
    
    void Flush() {
        std::lock_guard<std::mutex> lock(g_LogMutex);
        if (g_LogFile.is_open()) {
            g_LogFile.flush();
            g_LinesSinceFlush = 0;
            g_LastFlushAt = std::chrono::steady_clock::now();
        }
    }
    
    void Shutdown() {
        std::lock_guard<std::mutex> lock(g_LogMutex);
        g_EnableDebugLog.store(false, std::memory_order_release);
        if (g_LogFile.is_open()) {
            g_LogFile << "\n========================================\n";
            g_LogFile << "Session End\n";
            g_LogFile << "========================================\n";
            g_LogFile.flush();
            g_LogFile.close();
            g_LogFile.clear();
        }

        g_Initialized = false;
        g_HasOpenedThisSession = false;
        g_LinesSinceFlush = 0;
        g_LastFlushAt = {};
    }
}
