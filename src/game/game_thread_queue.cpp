#include "../include/game/game_thread_queue.h"
#include "../include/game/game_state.h"
#include "../include/game/savestate_hook.h"
#include "../include/game/final_memory_patch.h"
#include "../include/game/timer_freeze_patch.h"
#include "../include/utils/utilities.h"
#include "../include/core/logger.h"
#include <windows.h>
#include <atomic>
#include <mutex>
#include <string>

namespace {
    struct Item {
        GameThread::Command cmd;
        uintptr_t arg;
    };
    constexpr int kCapacity = 16;

    std::mutex g_lock;
    Item g_items[kCapacity];  // guarded by g_lock
    int g_count = 0;          // guarded by g_lock
    // Only a Drain in a live Practice battle opens admission and Clear closes
    // it, so nothing posted after a boundary can reach a later battle.
    bool g_admitting = false; // guarded by g_lock
    std::atomic<uint32_t> g_clearGeneration{0};
    std::atomic<bool> g_syncPending{false};
    std::atomic<DWORD> g_gameThreadId{0};

    void SyncPracticePatchesCommand() {
        SyncFinalMemoryBypassForCurrentMode("game thread sync");
        TimerFreeze::SyncForCurrentMode("game thread sync");
    }
}

namespace GameThread {
    bool Post(Command cmd, uintptr_t arg) {
        if (!cmd || !AreFrontendControlHooksInstalled() || !IsPracticeContext()) {
            return false;
        }
        std::unique_lock<std::mutex> lock(g_lock);
        if (!g_admitting) {
            return false;
        }
        if (GetCurrentThreadId() == g_gameThreadId.load(std::memory_order_acquire)) {
            lock.unlock();
            cmd(arg);
            return true;
        }
        for (int i = 0; i < g_count; ++i) {
            if (g_items[i].cmd == cmd && g_items[i].arg == arg) return true;
        }
        if (g_count == kCapacity) {
            return false;
        }
        g_items[g_count++] = Item{cmd, arg};
        return true;
    }

    void Drain() {
        g_gameThreadId.store(GetCurrentThreadId(), std::memory_order_release);
        Item local[kCapacity];
        int count = 0;
        uint32_t generation = 0;
        bool admit = false;
        {
            std::lock_guard<std::mutex> lock(g_lock);
            // Read under the lock: boundaries publish before Clear, so a Drain that loses to Clear stays closed.
            admit = IsPracticeContext() && g_featuresEnabled.load(std::memory_order_acquire);
            g_admitting = admit;
            count = g_count;
            for (int i = 0; i < count; ++i) local[i] = g_items[i];
            g_count = 0;
            generation = g_clearGeneration.load(std::memory_order_relaxed);
        }
        if (!admit) {
            return;
        }
        SavestateHook::EnsureInstalled();
        if (g_syncPending.exchange(false, std::memory_order_acq_rel)) {
            SyncPracticePatchesCommand();
        }
        for (int i = 0; i < count; ++i) {
            // A boundary published mid-batch ends the batch; each command stays Practice-only.
            if (g_clearGeneration.load(std::memory_order_acquire) != generation || !IsPracticeContext()) {
                return;
            }
            local[i].cmd(local[i].arg);
        }
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(g_lock);
        g_count = 0;
        g_admitting = false;
        g_syncPending.store(false, std::memory_order_release);
        g_clearGeneration.fetch_add(1, std::memory_order_acq_rel);
    }

    int SyncPracticePatches(const char* reason) {
        const bool mayApply = g_featuresEnabled.load(std::memory_order_acquire)
            && IsPracticeContext()
            && GetCurrentGamePhase() == GamePhase::Match;
        if (mayApply) {
            // A level, not an item: the first Drain of this battle re-reads every gate under the patch mutexes.
            if (AreFrontendControlHooksInstalled()) {
                g_syncPending.store(true, std::memory_order_release);
            } else {
                LogOut(std::string("[GAME_THREAD] Practice patch sync refused reason=")
                       + (reason ? reason : "unspecified"), true);
            }
            return 0;
        }
        const int changes = SyncFinalMemoryBypassForCurrentMode(reason);
        TimerFreeze::SyncForCurrentMode(reason);
        return changes;
    }
}
