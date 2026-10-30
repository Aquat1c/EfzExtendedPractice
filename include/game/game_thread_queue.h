#pragma once
#include <cstdint>

// Runs engine/Revival calls and code-patch writes on the game thread, at the
// start of the next Practice battle update. Never reached under netplay.
namespace GameThread {
    using Command = void (*)(uintptr_t arg);

    // Inline when already on the game thread; otherwise queued, coalesced by
    // cmd+arg. False outside Practice, without the battle hooks, when full, or
    // from a Clear until the next Practice battle's first Drain.
    bool Post(Command cmd, uintptr_t arg = 0);
    // HookedBattleUpdate only. Drops queued work outside Practice or with features off.
    void Drain();
    // Boundary: empties the queue and closes admission. A batch being drained stops at the next item.
    void Clear();

    // FinalMemory/TimerFreeze sync: applies are posted, restores run inline.
    int SyncPracticePatches(const char* reason);
}
