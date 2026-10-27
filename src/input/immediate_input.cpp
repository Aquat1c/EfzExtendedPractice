#include "../include/input/immediate_input.h"
#include "../include/input/input_core.h"
#include "../include/input/input_hook.h"
#include "../include/input/auto_action_motion_transaction.h"
#include "../include/input/injection_control.h"
#include "../include/input/input_buffer.h"
#include "../include/input/input_freeze.h"
#include "../include/input/motion_system.h"
#include "../include/input/scoped_input_reservation.h"
#include "../include/core/logger.h"
#include "../include/core/memory.h"
#include "../include/game/auto_action.h"
#include "../include/game/macro_controller.h"
#include "../include/utils/utilities.h" // for g_onlineModeActive
#include "../include/runtime/practice_runtime.h"
#include "../include/runtime/practice_session.h"
#include <process.h>
#include <chrono>
#include <mutex>

namespace ImmediateInput {

namespace {

class ImmediateWriteLease {
public:
    explicit ImmediateWriteLease(int playerNum)
        : playerNum_(playerNum), acquired_(
              TryAcquireImmediateInputWriteLease(playerNum)) {}
    ~ImmediateWriteLease() {
        if (acquired_) ReleaseImmediateInputWriteLease(playerNum_);
    }
    explicit operator bool() const { return acquired_; }

private:
    int playerNum_{0};
    bool acquired_{false};
};

} // namespace

static HANDLE s_thread = nullptr;
static uint64_t s_launchGeneration = 0;
static std::atomic<bool> s_running{false};
static std::atomic<bool> s_stop{false};
static std::atomic<uint64_t> s_workerGeneration{0};
static std::mutex s_lifecycleMutex;
static HANDLE s_stopEvent = nullptr;
static EfzTmEntryV1 s_boundWorld{}; // immutable from Start until its joined return
static bool s_ownedRaw[3]{}; // guarded by the existing input/control barrier
static std::atomic<bool> s_practiceAdmissionClosed{false};
static constexpr auto kVisualFrameDuration = std::chrono::nanoseconds(15625000); // exact 1/64 s

struct Slot {
    std::atomic<uint8_t> desired{0};
    std::atomic<int>     ticks{0};      // remaining visual ticks for timed press
    std::atomic<uint8_t> lastWritten{0};
    std::atomic<bool>    needNeutralEdge{false};
};

static Slot s_slot[3]; // 1=P1, 2=P2
static std::atomic<uint64_t> s_tutorialToken[3]{};
static std::atomic<uint64_t> s_nextTutorialToken{1};

// Caller holds g_p2ControlMutex. Never arm a delayed desired value behind
// another producer: the detached worker would deliver it after the lane is
// released.
static bool OtherInputOwnerActive(int playerNum) {
    return (playerNum == 1 && IsP1StartupNeutralGateActive()) ||
           IsScopedInputReserved(playerNum) ||
           IsAutoActionMotionTransactionActive(playerNum) ||
           IsAutoActionNormalPulseActive(playerNum) ||
           g_manualInputOverride[playerNum].load(std::memory_order_acquire) ||
           g_pollOverrideActive[playerNum].load(std::memory_order_acquire) ||
           GetMotionQueueSnapshot(playerNum).active ||
           TutorialMotionQueueLeaseActive(playerNum) ||
           TutorialBufferFreezeLeaseActive() ||
           (g_bufferFreezingActive.load(std::memory_order_acquire) &&
            (g_activeFreezePlayer.load(std::memory_order_acquire) == 0 ||
             g_activeFreezePlayer.load(std::memory_order_acquire) == playerNum)) ||
           (MacroController::IsExclusivePlayback() &&
            MacroController::GetPlaybackPlayer() == playerNum);
}

static uint64_t NextWorkerGeneration() {
    uint64_t generation =
        s_workerGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (generation == 0) {
        generation =
            s_workerGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
    }
    return generation;
}

static bool OwnsWorkerGeneration(uint64_t generation) {
    return generation != 0 &&
        s_workerGeneration.load(std::memory_order_acquire) == generation;
}

static bool WriteWorkerInput(int player, uint8_t mask) {
    if (!s_boundWorld.id.battleWorld) return WritePlayerInputImmediate(player, mask);
    const bool written = WritePlayerInputImmediateAt(
        player == 1 ? s_boundWorld.player1 : s_boundWorld.player2, mask);
    // A failed copy may have changed a prefix; keep the old-world lane until
    // a full neutral succeeds.
    s_ownedRaw[player] = !written || mask != 0;
    return written;
}

static Practice::WorkLease EnterBoundWork() {
    auto work = Practice::TryEnterMonitorWork();
    if (!work || work.Context().id.providerIncarnation != s_boundWorld.id.providerIncarnation ||
        work.Context().id.practiceSession != s_boundWorld.id.practiceSession ||
        work.Context().id.battleWorld != s_boundWorld.id.battleWorld ||
        work.Context().id.timeline != s_boundWorld.id.timeline) return {};
    return work;
}

static void Worker(uint64_t generation) {
    // CRITICAL: Never run during online mode
    if (g_onlineModeActive.load(std::memory_order_acquire) ||
        !OwnsWorkerGeneration(generation)) {
        if (OwnsWorkerGeneration(generation)) {
            s_running.store(false, std::memory_order_release);
        }
        return;
    }

    using clock = std::chrono::steady_clock;
    auto next = clock::now();
    if (detailedLogging.load()) {
        LogOut("[IMMEDIATE_INPUT] Worker started with exact 64 Hz cadence (" +
               std::to_string(kVisualFrameDuration.count()) + " ns per visual frame)", true);
    }

    while (!s_stop.load(std::memory_order_acquire) &&
           OwnsWorkerGeneration(generation)) {
        Practice::WorkLease work;
        if (s_boundWorld.id.battleWorld) {
            work = EnterBoundWork();
            if (!work) break;
        }
        if (g_onlineModeActive.load(std::memory_order_acquire)) {
            break;
        }

        auto now = clock::now();
        if (now < next) {
            const auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(next - now).count();
            const DWORD waitMs = static_cast<DWORD>((remaining + 999999) / 1000000);
            if (WaitForSingleObject(s_stopEvent, waitMs) != WAIT_TIMEOUT) break;
        }
        next += kVisualFrameDuration;

        // Netplay may publish while this detached worker sleeps; the old
        // offline iteration must not write once the online owner is live.
        if (g_onlineModeActive.load(std::memory_order_acquire) ||
            s_stop.load(std::memory_order_acquire) ||
            !OwnsWorkerGeneration(generation) || (work && !work.IsCurrent())) {
            break;
        }

        for (int p = 1; p <= 2; ++p) {
            // A hook-owned AI normal must survive to EFZ's later character
            // consumer; hold the write lease across the whole pulse/release
            // transaction.
            ImmediateWriteLease writeLease(p);
            if (!writeLease) continue;
            if (g_onlineModeActive.load(std::memory_order_acquire) ||
                s_stop.load(std::memory_order_acquire) ||
                !OwnsWorkerGeneration(generation) || (work && !work.IsCurrent())) {
                break;
            }

            // Use acquire to ensure we see desired before checking ticks
            uint8_t curDesired = s_slot[p].desired.load(std::memory_order_acquire);
            int t = s_slot[p].ticks.load(std::memory_order_relaxed);
            uint8_t last = s_slot[p].lastWritten.load(std::memory_order_relaxed);
            bool needNeutral = s_slot[p].needNeutralEdge.exchange(false);

            if (t > 0) {
                if (curDesired == 0) {
                    s_slot[p].ticks.store(0, std::memory_order_relaxed);
                    continue;
                }
                if (last != 0 && last == curDesired) {
                    WriteWorkerInput(p, curDesired);

                    if ((curDesired & GAME_INPUT_UP) != 0) {
                        uint16_t moveID = 0;
                        uintptr_t playerPtr = GetPlayerPointer(p);
                        if (playerPtr) {
                            SafeReadMemory(playerPtr + MOVE_ID_OFFSET, &moveID, sizeof(uint16_t));
                        }
                        LogOut("[IMMEDIATE_INPUT] Holding UP for P" + std::to_string(p) + 
                               " mask=" + std::to_string(curDesired) + 
                               " ticksLeft=" + std::to_string(t-1) +
                               " moveID=" + std::to_string(moveID), true);
                    }
                } else {
                    if (last != 0) {
                        WriteWorkerInput(p, 0);
                    }
                    WriteWorkerInput(p, curDesired);

                    if ((curDesired & GAME_INPUT_UP) != 0) {
                        uint16_t moveID = 0;
                        uintptr_t playerPtr = GetPlayerPointer(p);
                        if (playerPtr) {
                            SafeReadMemory(playerPtr + MOVE_ID_OFFSET, &moveID, sizeof(uint16_t));
                        }
                        LogOut("[IMMEDIATE_INPUT] Initial UP write for P" + std::to_string(p) + 
                               " mask=" + std::to_string(curDesired) + 
                               " ticksLeft=" + std::to_string(t-1) +
                               " moveID=" + std::to_string(moveID), true);
                    }
                }
                s_slot[p].lastWritten.store(curDesired, std::memory_order_relaxed);
                s_slot[p].ticks.store(t - 1, std::memory_order_relaxed);
                if (t - 1 <= 0) {
                    WriteWorkerInput(p, 0);
                    s_slot[p].lastWritten.store(0, std::memory_order_relaxed);
                    s_slot[p].desired.store(0, std::memory_order_relaxed);
                }
                continue;
            }

            if (needNeutral && curDesired != 0) {
                WriteWorkerInput(p, 0);
                s_slot[p].lastWritten.store(0, std::memory_order_relaxed);
            }

            if (curDesired != 0) {
                // Maintain hold; ensure the mask is reasserted periodically since the game may clear per frame
                if (last != 0 && last != curDesired) {
                    WriteWorkerInput(p, 0);
                }
                WriteWorkerInput(p, curDesired);
                s_slot[p].lastWritten.store(curDesired, std::memory_order_relaxed);
            } else {
                if (last != 0) {
                    WriteWorkerInput(p, 0);
                    s_slot[p].lastWritten.store(0, std::memory_order_relaxed);
                }
            }
        }
    }

    // Offline stop releases to neutral; a delayed neutral write is still an
    // unauthorized netplay input. An obsolete worker must not clear a newer
    // worker's state or issue a late neutral write.
    if (!OwnsWorkerGeneration(generation)) return;

    // No new neutral write follows a closed work lease; the held-world
    // retirement callback owns raw release.
    if (s_boundWorld.id.battleWorld) {
        s_running.store(false, std::memory_order_release);
        return;
    }

    const bool onlineExit = g_onlineModeActive.load(std::memory_order_acquire);
    for (int p = 1; p <= 2; ++p) {
        if (!onlineExit) {
            ImmediateWriteLease writeLease(p);
            if (writeLease) WriteWorkerInput(p, 0);
        }
        s_slot[p].lastWritten.store(0, std::memory_order_relaxed);
        s_slot[p].desired.store(0, std::memory_order_relaxed);
        s_slot[p].ticks.store(0, std::memory_order_relaxed);
        s_slot[p].needNeutralEdge.store(false, std::memory_order_relaxed);
    }
    if (OwnsWorkerGeneration(generation)) {
        s_running.store(false, std::memory_order_release);
    }
}

void Start() {
    // Match netplay publication's lock order; the recheck under the shared
    // barrier closes the hand-off gap.
    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    std::lock_guard<std::mutex> lifecycleLock(s_lifecycleMutex);
    if (s_practiceAdmissionClosed.load(std::memory_order_acquire) ||
        g_onlineModeActive.load(std::memory_order_acquire) ||
        s_running.load(std::memory_order_acquire)) return;
    if (s_boundWorld.id.battleWorld && (s_ownedRaw[1] || s_ownedRaw[2])) return;

    // Join an exited worker before starting a new generation; its input
    // barrier uses try_lock, so this cannot wait on the caller's control
    // mutex.
    if (s_thread) {
        WaitForSingleObject(s_thread, INFINITE);
        CloseHandle(s_thread);
        s_thread = nullptr;
    }
    if (!s_stopEvent) s_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!s_stopEvent) return;
    ResetEvent(s_stopEvent);

    const uint64_t generation = NextWorkerGeneration();
    s_stop.store(false, std::memory_order_release);
    for (int p = 1; p <= 2; ++p) {
        s_slot[p].desired.store(0);
        s_slot[p].ticks.store(0);
        s_slot[p].lastWritten.store(0);
        s_slot[p].needNeutralEdge.store(false);
    }
    s_running.store(true, std::memory_order_release);
    s_launchGeneration = generation;
    s_thread = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0,
        [](void* context) -> unsigned int {
            Worker(*static_cast<const uint64_t*>(context));
            return 0;
        }, &s_launchGeneration, 0, nullptr));
    if (!s_thread) {
        s_running.store(false, std::memory_order_release);
        s_stop.store(true, std::memory_order_release);
        CloseHandle(s_stopEvent);
        s_stopEvent = nullptr;
    }
}

static bool StopWorker(bool releaseNeutral, const EfzTmEntryV1* heldWorld = nullptr) {
    // Worker input acquisition is nonblocking and never takes
    // s_lifecycleMutex, so it can return while these locks are held.
    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    std::lock_guard<std::mutex> lifecycleLock(s_lifecycleMutex);
    if (heldWorld && (heldWorld->id.providerIncarnation != s_boundWorld.id.providerIncarnation ||
        heldWorld->id.practiceSession != s_boundWorld.id.practiceSession ||
        heldWorld->id.battleWorld != s_boundWorld.id.battleWorld ||
        heldWorld->id.timeline != s_boundWorld.id.timeline ||
        heldWorld->player1 != s_boundWorld.player1 || heldWorld->player2 != s_boundWorld.player2)) return false;
    if (heldWorld) s_practiceAdmissionClosed.store(true, std::memory_order_release);
    if (s_practiceAdmissionClosed.load(std::memory_order_acquire)) releaseNeutral = false;
    const bool wasRunning = s_running.exchange(false, std::memory_order_acq_rel);
    s_stop.store(true, std::memory_order_release);
    (void)NextWorkerGeneration();
    if (s_stopEvent) SetEvent(s_stopEvent);
    if (s_thread) {
        WaitForSingleObject(s_thread, INFINITE);
        CloseHandle(s_thread);
        s_thread = nullptr;
    }
    if (s_stopEvent) {
        CloseHandle(s_stopEvent);
        s_stopEvent = nullptr;
    }

    bool restored = true;
    for (int p = 1; p <= 2; ++p) {
        if (heldWorld && s_ownedRaw[p] && !WriteWorkerInput(p, 0)) {
            restored = false;
            continue;
        }
        s_slot[p].desired.store(0, std::memory_order_relaxed);
        s_slot[p].ticks.store(0, std::memory_order_relaxed);
        s_slot[p].needNeutralEdge.store(false, std::memory_order_relaxed);
        if (releaseNeutral && !g_onlineModeActive.load(std::memory_order_acquire)) {
            ImmediateWriteLease writeLease(p);
            if (writeLease) restored = WriteWorkerInput(p, 0) && restored;
        }
        s_slot[p].lastWritten.store(0, std::memory_order_relaxed);
        if (!releaseNeutral) s_tutorialToken[p].store(0, std::memory_order_release);
    }
    if (wasRunning && detailedLogging.load()) {
        LogOut("[IMMEDIATE_INPUT] Worker stopped and generation retired", true);
    }
    if (heldWorld && restored) s_boundWorld = {};
    return restored;
}

void CancelWork() {
    std::lock_guard<std::mutex> lifecycleLock(s_lifecycleMutex);
    // Only BindPracticeWorld reopens admission; never close it without a bound world.
    if (s_boundWorld.id.battleWorld) s_practiceAdmissionClosed.store(true, std::memory_order_release);
    s_stop.store(true, std::memory_order_release);
    if (s_stopEvent) SetEvent(s_stopEvent);
}

void Stop() { StopWorker(true); }
void RetireWorker() { StopWorker(false); }
bool RetireWorker(const EfzTmEntryV1& world) { return StopWorker(false, &world); }

bool BindPracticeWorld(const EfzTmEntryV1& world) {
    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    std::lock_guard<std::mutex> lifecycleLock(s_lifecycleMutex);
    if (!world.id.battleWorld || !world.id.timeline || !world.player1 || !world.player2 ||
        s_running.load(std::memory_order_acquire)) return false;
    if (s_thread) {
        WaitForSingleObject(s_thread, INFINITE);
        CloseHandle(s_thread);
        s_thread = nullptr;
    }
    if (s_boundWorld.id.battleWorld) return
        s_boundWorld.id.providerIncarnation == world.id.providerIncarnation &&
        s_boundWorld.id.practiceSession == world.id.practiceSession &&
        s_boundWorld.id.battleWorld == world.id.battleWorld && s_boundWorld.id.timeline == world.id.timeline &&
        s_boundWorld.player1 == world.player1 && s_boundWorld.player2 == world.player2;
    s_boundWorld = world;
    s_practiceAdmissionClosed.store(false, std::memory_order_release);
    return true;
}

bool IsRunning() { return s_running.load(); }

static void SetImpl(int playerNum, uint8_t mask) {
    if (playerNum < 1 || playerNum > 2) return;
    uint8_t last = s_slot[playerNum].lastWritten.load(std::memory_order_relaxed);
    if (mask != 0 && last == mask) {
        s_slot[playerNum].needNeutralEdge.store(true, std::memory_order_relaxed);
    }
    s_slot[playerNum].ticks.store(0, std::memory_order_relaxed);
    s_slot[playerNum].desired.store(mask, std::memory_order_relaxed);
}

bool Set(int playerNum, uint8_t mask) {
    if (playerNum < 1 || playerNum > 2 ||
        s_tutorialToken[playerNum].load(std::memory_order_acquire) != 0) return false;
    std::unique_lock<std::recursive_mutex> p2ControlLock(g_p2ControlMutex);
    auto work = s_boundWorld.id.battleWorld ? EnterBoundWork() : Practice::WorkLease{};
    if (s_boundWorld.id.battleWorld && !work) return false;
    if (s_tutorialToken[playerNum].load(std::memory_order_acquire) != 0 ||
        s_practiceAdmissionClosed.load(std::memory_order_acquire) ||
        g_onlineModeActive.load(std::memory_order_acquire) ||
        OtherInputOwnerActive(playerNum)) return false;
    // A non-zero continuous source must win admission now, never be cached
    // behind another owner to appear later out of context.
    if (mask != 0) {
        ImmediateWriteLease writeLease(playerNum);
        if (!writeLease) return false;
        SetImpl(playerNum, mask);
        return true;
    }
    SetImpl(playerNum, mask);
    return true;
}

static bool PressForImpl(int playerNum, uint8_t mask, int ticks) {
    if (s_practiceAdmissionClosed.load(std::memory_order_acquire)) return false;
    if (playerNum < 1 || playerNum > 2 || ticks <= 0) return false;
    auto work = s_boundWorld.id.battleWorld ? EnterBoundWork() : Practice::WorkLease{};
    if (s_boundWorld.id.battleWorld && !work) return false;
    const uint8_t last = s_slot[playerNum].lastWritten.load(std::memory_order_relaxed);
    ImmediateWriteLease writeLease(playerNum);
    if (!writeLease) return false;
    if (mask != 0) {
        const bool neutralOk = last == 0 || WriteWorkerInput(playerNum, 0);
        const bool pressOk = WriteWorkerInput(playerNum, mask);
        if (!neutralOk || !pressOk) {
            // Do not retain a rejected one-shot for the worker to deliver at a
            // later, unrelated actionable window.
            (void)WriteWorkerInput(playerNum, 0);
            s_slot[playerNum].lastWritten.store(0, std::memory_order_relaxed);
            return false;
        }
        s_slot[playerNum].lastWritten.store(mask, std::memory_order_relaxed);
        if (detailedLogging.load()) {
            LogOut("[IMMEDIATE_INPUT] Primed timed press for P" + std::to_string(playerNum) +
                   " mask=" + std::to_string(mask) +
                   " ticks=" + std::to_string(ticks), true);
        }
    }
    // Set desired first with release semantics, then arm the remaining worker ticks.
    s_slot[playerNum].desired.store(mask, std::memory_order_release);
    s_slot[playerNum].ticks.store((ticks > 1) ? (ticks - 1) : 1, std::memory_order_relaxed);
    return true;
}

bool PressFor(int playerNum, uint8_t mask, int ticks) {
    if (playerNum < 1 || playerNum > 2 ||
        s_tutorialToken[playerNum].load(std::memory_order_acquire) != 0 ||
        IsScopedInputReserved(playerNum)) return false;
    std::unique_lock<std::recursive_mutex> p2ControlLock(g_p2ControlMutex);
    if (s_tutorialToken[playerNum].load(std::memory_order_acquire) != 0 ||
        g_onlineModeActive.load(std::memory_order_acquire) ||
        OtherInputOwnerActive(playerNum)) return false;
    return PressForImpl(playerNum, mask, ticks);
}

static void ClearImpl(int playerNum) {
    if (s_practiceAdmissionClosed.load(std::memory_order_acquire)) return;
    if (playerNum < 1 || playerNum > 2) return;
    if (g_onlineModeActive.load(std::memory_order_acquire)) {
        // Netplay owns the fighters: drop the desired mask, never write it.
        s_slot[playerNum].desired.store(0, std::memory_order_relaxed);
        s_slot[playerNum].ticks.store(0, std::memory_order_relaxed);
        s_slot[playerNum].lastWritten.store(0, std::memory_order_relaxed);
        return;
    }
    auto work = s_boundWorld.id.battleWorld ? EnterBoundWork() : Practice::WorkLease{};
    if (s_boundWorld.id.battleWorld && !work) return;
    s_slot[playerNum].desired.store(0, std::memory_order_relaxed);
    s_slot[playerNum].ticks.store(0, std::memory_order_relaxed);
    ImmediateWriteLease writeLease(playerNum);
    if (writeLease) WriteWorkerInput(playerNum, 0);
    s_slot[playerNum].lastWritten.store(0, std::memory_order_relaxed);
}

void Clear(int playerNum) {
    if (playerNum < 1 || playerNum > 2 ||
        s_tutorialToken[playerNum].load(std::memory_order_acquire) != 0) return;
    std::unique_lock<std::recursive_mutex> controlLock(g_p2ControlMutex);
    if (s_tutorialToken[playerNum].load(std::memory_order_acquire) != 0) return;
    ClearImpl(playerNum);
}

uint8_t GetCurrentDesired(int playerNum) {
    if (playerNum < 1 || playerNum > 2) return 0;
    return s_slot[playerNum].desired.load(std::memory_order_relaxed);
}

int GetRemainingTicks(int playerNum) {
    if (playerNum < 1 || playerNum > 2) return 0;
    return s_slot[playerNum].ticks.load(std::memory_order_relaxed);
}

bool RetireCompletedBookkeeping(int playerNum) {
    if (playerNum < 1 || playerNum > 2) return false;
    if (s_slot[playerNum].desired.load(std::memory_order_acquire) != 0 ||
        s_slot[playerNum].ticks.load(std::memory_order_acquire) != 0) {
        return false;
    }
    s_slot[playerNum].lastWritten.store(0, std::memory_order_release);
    s_slot[playerNum].needNeutralEdge.store(false,
                                             std::memory_order_release);
    return true;
}

bool AcquireTutorialLease(int playerNum, uint64_t& tokenOut) {
    tokenOut = 0;
    if (playerNum < 1 || playerNum > 2) return false;
    std::unique_lock<std::recursive_mutex> p2ControlLock(g_p2ControlMutex);
    if (s_practiceAdmissionClosed.load(std::memory_order_acquire)) return false;
    auto work = s_boundWorld.id.battleWorld ? EnterBoundWork() : Practice::WorkLease{};
    if (s_boundWorld.id.battleWorld && !work) return false;
    if (s_tutorialToken[playerNum].load(std::memory_order_acquire) != 0 ||
        g_onlineModeActive.load(std::memory_order_acquire) ||
        OtherInputOwnerActive(playerNum)) return false;
    if (s_slot[playerNum].desired.load(std::memory_order_acquire) != 0 ||
        s_slot[playerNum].ticks.load(std::memory_order_acquire) != 0) return false;
    uint64_t expected = 0;
    uint64_t token = s_nextTutorialToken.fetch_add(1, std::memory_order_relaxed);
    if (token == 0) token = s_nextTutorialToken.fetch_add(1, std::memory_order_relaxed);
    if (!s_tutorialToken[playerNum].compare_exchange_strong(
            expected, token, std::memory_order_acq_rel)) return false;
    tokenOut = token;
    return true;
}

bool PressForTutorial(int playerNum, uint64_t token, uint8_t mask, int ticks) {
    if (playerNum < 1 || playerNum > 2 || token == 0 || ticks <= 0 ||
        s_tutorialToken[playerNum].load(std::memory_order_acquire) != token)
        return false;
    std::unique_lock<std::recursive_mutex> p2ControlLock(g_p2ControlMutex);
    if (s_tutorialToken[playerNum].load(std::memory_order_acquire) != token ||
        g_onlineModeActive.load(std::memory_order_acquire) ||
        OtherInputOwnerActive(playerNum)) return false;
    return PressForImpl(playerNum, mask, ticks);
}

void ReleaseTutorialLease(int playerNum, uint64_t token) {
    if (playerNum < 1 || playerNum > 2 || token == 0 ||
        s_tutorialToken[playerNum].load(std::memory_order_acquire) != token)
        return;
    std::unique_lock<std::recursive_mutex> controlLock(g_p2ControlMutex);
    auto work = s_boundWorld.id.battleWorld ? EnterBoundWork() : Practice::WorkLease{};
    if (s_boundWorld.id.battleWorld && !work) return;
    if (s_tutorialToken[playerNum].load(std::memory_order_acquire) != token) return;
    s_slot[playerNum].desired.store(0, std::memory_order_relaxed);
    s_slot[playerNum].ticks.store(0, std::memory_order_relaxed);
    s_slot[playerNum].needNeutralEdge.store(false, std::memory_order_relaxed);
    ImmediateWriteLease writeLease(playerNum);
    if (writeLease) WriteWorkerInput(playerNum, 0);
    s_slot[playerNum].lastWritten.store(0, std::memory_order_relaxed);
    uint64_t expected = token;
    (void)s_tutorialToken[playerNum].compare_exchange_strong(
        expected, 0, std::memory_order_acq_rel);
}

bool TutorialLeaseActive(int playerNum) {
    return playerNum >= 1 && playerNum <= 2 &&
        s_tutorialToken[playerNum].load(std::memory_order_acquire) != 0;
}

} // namespace ImmediateInput
