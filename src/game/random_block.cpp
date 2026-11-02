#include "../include/game/random_block.h"
#include "../include/game/practice_patch.h"
#include "../include/core/logger.h"
#include "../include/game/game_state.h"
#include "../include/core/constants.h"
#include "../include/utils/utilities.h"
#include "../include/utils/switch_players.h"
#include <cstdlib>
#include <atomic>
#include <mutex>

namespace RandomBlock {
    static std::atomic<bool> g_enabled{false};
    static std::atomic<int>  g_lastApplied{-1};
    static std::atomic<bool> g_pendingOff{false};
    static std::atomic<bool> g_restoreOn{false};
    static std::atomic<bool> g_restoreKnown{false};
    static std::mutex g_writeMutex;
    static std::atomic<uint64_t> g_generation{0};

    static bool BaselineForMode(int mode) {
        return mode == DAB_All || mode == DAB_FirstHitThenOff;
    }

    static void ApplyEnabled(bool enabled) {
        if (enabled) {
            // Own +4936 for the whole enabled lifetime, or the native-F7 watcher mistakes a coin-flip OFF for user intent.
            SetExternalAutoBlockController(true);
            g_restoreKnown.store(false, std::memory_order_relaxed);
        }
        g_enabled.store(enabled);
        g_pendingOff.store(false);
        g_lastApplied.store(-1);
        if (!enabled) {
            const bool restoreOn = g_restoreKnown.load(std::memory_order_relaxed)
                ? g_restoreOn.load(std::memory_order_relaxed)
                : BaselineForMode(GetDummyAutoBlockMode());
            (void)SetPracticeAutoBlockEnabled(
                restoreOn, restoreOn
                    ? "RandomBlock: restore configured block window ON"
                    : "RandomBlock: restore configured block window OFF");
            g_restoreKnown.store(false, std::memory_order_relaxed);
            SetExternalAutoBlockController(false);
        }
        LogOut(std::string("[RANDOM_BLOCK] ") + (enabled ? "ENABLED" : "DISABLED"), true);
    }

    void SetEnabled(bool enabled) {
        std::lock_guard<std::mutex> lk(g_writeMutex);
        ApplyEnabled(enabled);
        g_generation.fetch_add(1, std::memory_order_release);
    }

    bool IsEnabled() { return g_enabled.load(); }

    uint64_t GetMutationGeneration() {
        return g_generation.load(std::memory_order_acquire);
    }

    bool SetEnabledIfGeneration(bool enabled, uint64_t expectedGeneration) {
        std::lock_guard<std::mutex> lk(g_writeMutex);
        if (g_generation.load(std::memory_order_relaxed) != expectedGeneration) return false;
        ApplyEnabled(enabled);
        g_generation.store(expectedGeneration + 1, std::memory_order_release);
        return true;
    }

    static inline bool IsDummyBlockingOrBlockstun(short moveId) {
        return (moveId >= 150 && moveId <= 156);
    }

    void Tick(short p1MoveId, short p2MoveId) {
        std::lock_guard<std::mutex> lk(g_writeMutex);
        if (!g_enabled.load()) return;
        if (GetCurrentGameMode() != GameMode::Practice) return;
        if (GetCurrentGamePhase() != GamePhase::Match) return;

        // Safety reads must target the dummy (P1 after a control swap); the +4936 write is slot-agnostic.
        const int dummyPlayer = SwitchPlayers::GetRemotePlayerIndex();
        const short dummyMoveId = (dummyPlayer == 1) ? p1MoveId : p2MoveId;

        bool wantWindow = false;
        if (!GetCurrentDesiredAutoBlockOn(wantWindow)) {
            bool curOn = false; GetPracticeAutoBlockEnabled(curOn);
            wantWindow = curOn;
        }
        g_restoreOn.store(wantWindow, std::memory_order_relaxed);
        g_restoreKnown.store(true, std::memory_order_relaxed);
        int dabMode = GetDummyAutoBlockMode();
        if (dabMode == DAB_FirstHitThenOff && wantWindow) {
            bool curOn = false; if (!GetPracticeAutoBlockEnabled(curOn)) return;
            if (!curOn || g_lastApplied.load() != 1) {
                if (SetPracticeAutoBlockEnabled(true, "RandomBlock: force ON (FirstHitThenOff window)")) {
                    g_lastApplied.store(1);
                }
            }
            g_pendingOff.store(false);
            return;
        }

        if (!wantWindow) {
            bool curOn = false; if (!GetPracticeAutoBlockEnabled(curOn)) return;
            bool wantOnFinal = false;
            if (IsDummyBlockingOrBlockstun(dummyMoveId) || !::IsActionable(dummyMoveId)) {
                g_pendingOff.store(true);
                wantOnFinal = true;
            }
            if (g_pendingOff.load()) {
                if (!IsDummyBlockingOrBlockstun(dummyMoveId) && ::IsActionable(dummyMoveId)) {
                    wantOnFinal = false; g_pendingOff.store(false);
                } else {
                    wantOnFinal = true;
                }
            }
            int wantVal = wantOnFinal ? 1 : 0;
            if (curOn != wantOnFinal || wantVal != g_lastApplied.load()) {
                if (SetPracticeAutoBlockEnabled(wantOnFinal, wantOnFinal ? "RandomBlock: hold ON (deferring OFF)" : "RandomBlock: OFF (mode window closed)")) {
                    g_lastApplied.store(wantVal);
                }
            }
            return;
        }

        bool wantOn = (rand() & 1) != 0;

        bool curOn = false; if (!GetPracticeAutoBlockEnabled(curOn)) return;

        // Never turn OFF mid-guard or while inactionable.
        if (!wantOn) {
            if (IsDummyBlockingOrBlockstun(dummyMoveId) || !::IsActionable(dummyMoveId)) {
                g_pendingOff.store(true);
                wantOn = true;
            }
        }

        if (g_pendingOff.load()) {
            if (!IsDummyBlockingOrBlockstun(dummyMoveId) && ::IsActionable(dummyMoveId)) {
                wantOn = false;
                g_pendingOff.store(false);
            } else {
                wantOn = true;
            }
        }

        int wantVal = wantOn ? 1 : 0;
        if (curOn != wantOn || wantVal != g_lastApplied.load()) {
            if (SetPracticeAutoBlockEnabled(wantOn, wantOn ? "RandomBlock: coin ON" : "RandomBlock: coin OFF")) {
                g_lastApplied.store(wantVal);
            }
        }
    }
}
