#pragma once

#include <string>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <atomic>

namespace DebugLog {
    // Startup-config flag only; at runtime use SetEnabled(), which opens the file or flushes and closes it.
    extern std::atomic<bool> g_EnableDebugLog;

    bool IsEnabled();
    bool SetEnabled(bool enabled);

    void Initialize();

    void Write(const std::string& message);

    void WriteHex(const std::string& label, uintptr_t address, const void* data, size_t size);

    template<typename T>
    void LogRead(const std::string& label, uintptr_t address, T value) {
        if (!IsEnabled()) return;
        std::ostringstream oss;
        oss << "[READ] " << label << " @0x" << std::hex << std::uppercase << address 
            << " = 0x" << std::setw(sizeof(T)*2) << std::setfill('0') << (uint64_t)value 
            << " (" << std::dec << (int64_t)value << ")";
        Write(oss.str());
    }

    template<typename T>
    void LogWrite(const std::string& label, uintptr_t address, T before, T after) {
        if (!IsEnabled()) return;
        std::ostringstream oss;
        oss << "[WRITE] " << label << " @0x" << std::hex << std::uppercase << address 
            << " : 0x" << std::setw(sizeof(T)*2) << std::setfill('0') << (uint64_t)before 
            << " -> 0x" << std::setw(sizeof(T)*2) << std::setfill('0') << (uint64_t)after;
        Write(oss.str());
    }

    void Flush();

    void Shutdown();
}
