#pragma once
#include <stdint.h>

namespace PracticeHotkeyGate {
    bool Install();
    void Uninstall();
    uint64_t GetSuppressedFrameCount();
    void NotifyMenuVisibility(bool visible);
}
