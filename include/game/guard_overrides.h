#pragma once
#include <cstdint>

namespace GuardOverrides {
    bool IsGroundedOverhead(int charId, int moveId, uintptr_t attackerBase);
    
    bool IsAirborneLow(int charId, int moveId);
}
