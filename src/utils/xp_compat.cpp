#include "../../include/utils/xp_compat.h"

#include <atomic>
#include <sstream>

namespace XPCompat {

bool IsEnabled() {
#if defined(EFZ_XP_COMPAT)
    return true;
#else
    return false;
#endif
}

unsigned long long GetTickCount64Compat() {
#if defined(EFZ_XP_COMPAT)
    static std::atomic<unsigned long long> s_extendedTick{0};

    const unsigned long lowNow = ::GetTickCount();
    unsigned long long observed = s_extendedTick.load(std::memory_order_relaxed);

    for (;;) {
        unsigned long long candidate = (observed & 0xFFFFFFFF00000000ULL) | lowNow;
        if (candidate < observed) {
            candidate += 0x100000000ULL;
        }
        if (s_extendedTick.compare_exchange_weak(
                observed,
                candidate,
                std::memory_order_relaxed,
                std::memory_order_relaxed)) {
            return candidate;
        }
    }
#else
    return ::GetTickCount64();
#endif
}

std::string GetRuntimeSummary() {
    std::ostringstream oss;
    if (IsEnabled()) {
        oss << "[XP] XP compatibility mode active"
            << " | ticks=GetTickCount rollover wrapper"
            << " | consoleVT=disabled"
            << " | HID names=fallback"
            << " | WinSDK guards enabled";
    } else {
        oss << "[XP] XP compatibility mode inactive";
    }
    return oss.str();
}

}
