#pragma once
#include <atomic>

// Each Practice frame, flips a coin and writes 0x3C (armed) or 0 to the dummy's RG arm byte
// (+334, decimal).
namespace RandomRG {
    void SetEnabled(bool enabled);
    bool IsEnabled();
    void Tick(short p1MoveId, short p2MoveId);
}
