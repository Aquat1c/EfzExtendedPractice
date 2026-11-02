#pragma once
#include <atomic>
#include <cstdint>

namespace RandomBlock {
    void SetEnabled(bool enabled);
    bool IsEnabled();
    uint64_t GetMutationGeneration();
    bool SetEnabledIfGeneration(bool enabled, uint64_t expectedGeneration);

    void Tick(short p1MoveId, short p2MoveId);
}
