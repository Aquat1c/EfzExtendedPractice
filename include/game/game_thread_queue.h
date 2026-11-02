#pragma once
#include <cstdint>

namespace GameThread {
    using Command = void (*)(uintptr_t arg);

    // Inline when already on the game thread; otherwise queued, coalesced by cmd+arg.
    // False outside Practice, without the battle hooks, when full, or from a Clear until the next Drain.
    bool Post(Command cmd, uintptr_t arg = 0);
    void Drain();
    // Boundary: empties the queue and closes admission. A batch being drained stops at the next item.
    void Clear();

    int SyncPracticePatches(const char* reason);
}
