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
    std::atomic<DWORD> g_gameThreadId{0};

    void SyncPracticePatchesCommand(uintptr_t) {
        SyncFinalMemoryBypassForCurrentMode("game thread sync");
        TimerFreeze::SyncForCurrentMode("game thread sync");
    }
}

namespace GameThread {
    bool Post(Command cmd, uintptr_t arg) {
        if (!cmd || !AreFrontendControlHooksInstalled() || !IsPracticeContext()) {
            return false;
        }
        if (GetCurrentThreadId() == g_gameThreadId.load(std::memory_order_acquire)) {
            cmd(arg);
            return true;
        }
        std::lock_guard<std::mutex> lock(g_lock);
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
        {
            std::lock_guard<std::mutex> lock(g_lock);
            count = g_count;
            for (int i = 0; i < count; ++i) local[i] = g_items[i];
            g_count = 0;
        }
        if (!IsPracticeContext() || !g_featuresEnabled.load(std::memory_order_acquire)) {
            return;
        }
        SavestateHook::EnsureInstalled();
        for (int i = 0; i < count; ++i) {
            local[i].cmd(local[i].arg);
        }
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(g_lock);
        g_count = 0;
    }

    int SyncPracticePatches(const char* reason) {
        const bool mayApply = g_featuresEnabled.load(std::memory_order_acquire)
            && IsPracticeContext()
            && GetCurrentGamePhase() == GamePhase::Match;
        if (mayApply) {
            if (!Post(&SyncPracticePatchesCommand)) {
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
