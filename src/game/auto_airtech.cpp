#include "../include/game/auto_airtech.h"
#include "../include/core/constants.h"
#include "../include/utils/utilities.h"
#include "../include/utils/network.h"
#include "../include/game/auto_action.h"
#include "../include/game/frame_advantage.h"
#include "../include/game/game_state.h"
#include "../include/game/macro_controller.h"

#include "../include/core/memory.h"
#include "../include/core/logger.h"
#include "../include/input/auto_action_motion_transaction.h"
#include "../include/input/immediate_input.h"
#include "../include/input/injection_control.h"
#include "../include/input/input_buffer.h"
#include "../include/input/input_core.h" // for WritePlayerInputImmediate and GAME_INPUT_*
#include "../include/input/input_freeze.h"
#include "../include/input/input_hook.h"
#include "../include/input/motion_system.h"
#include "../include/input/scoped_input_reservation.h"
#include "../include/game/validation_metrics.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>

namespace {
constexpr int kInternalTicksPerVisualFrame = 3;
bool s_airtechOverrideOwned[3] = {false, false, false};

inline int VisualFramesToInternalTicks(int visualFrames) {
    return (visualFrames <= 0) ? 0 : (visualFrames * kInternalTicksPerVisualFrame);
}

inline int InternalTicksToVisualFramesCeil(int internalTicks) {
    return (internalTicks <= 0) ? 0 : ((internalTicks + kInternalTicksPerVisualFrame - 1) / kInternalTicksPerVisualFrame);
}

bool PublishAirtechOverride(int playerNum, uint8_t mask, bool buffered) {
    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    if (g_onlineModeActive.load(std::memory_order_acquire) ||
        IsScopedInputReserved(playerNum) ||
        IsAutoActionMotionTransactionActive(playerNum) ||
        IsAutoActionNormalPulseActive(playerNum) ||
        ImmediateInput::TutorialLeaseActive(playerNum) ||
        TutorialMotionQueueLeaseActive(playerNum) ||
        TutorialBufferFreezeLeaseActive() ||
        g_pollOverrideActive[playerNum].load(std::memory_order_acquire) ||
        (MacroController::IsExclusivePlayback() &&
         MacroController::GetPlaybackPlayer() == playerNum) ||
        ImmediateInput::GetCurrentDesired(playerNum) != 0 ||
        ImmediateInput::GetRemainingTicks(playerNum) != 0 ||
        (g_bufferFreezingActive.load(std::memory_order_acquire) &&
         (g_activeFreezePlayer.load(std::memory_order_acquire) == 0 ||
          g_activeFreezePlayer.load(std::memory_order_acquire) == playerNum)) ||
        (g_manualInputOverride[playerNum].load(std::memory_order_acquire) &&
         !s_airtechOverrideOwned[playerNum])) {
        return false;
    }
    g_manualInputMask[playerNum].store(mask, std::memory_order_release);
    g_manualInputOverride[playerNum].store(true, std::memory_order_release);
    // Airtech normally needs the buffered path so EFZ sees a clean edge in
    // its native history; callers may still request an immediate-only pulse.
    g_injectImmediateOnly[playerNum].store(!buffered,
                                            std::memory_order_release);
    s_airtechOverrideOwned[playerNum] = true;
    return true;
}

bool ReleaseAirtechOverride(int playerNum) {
    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    if (!s_airtechOverrideOwned[playerNum]) return true;
    if (IsScopedInputReserved(playerNum)) return false;
    g_manualInputOverride[playerNum].store(false, std::memory_order_release);
    g_manualInputMask[playerNum].store(0, std::memory_order_release);
    g_injectImmediateOnly[playerNum].store(false,
                                            std::memory_order_release);
    s_airtechOverrideOwned[playerNum] = false;
    return true;
}
} // namespace

char originalEnableBytes[2] = {0x74, 0x71};
char originalForwardBytes[2] = {0x75, 0x24};
char originalBackwardBytes[2] = {0x75, 0x21};
bool patchesApplied = false;

bool p1IsAirteching = false;
bool p2IsAirteching = false;

std::atomic<bool> g_airtechP1Active{false};
std::atomic<bool> g_airtechP2Active{false};
std::atomic<bool> g_airtechP1Airtechable{false};
std::atomic<bool> g_airtechP2Airtechable{false};
std::atomic<bool> g_airtechP1FacingRight{true};
std::atomic<bool> g_airtechP2FacingRight{true};

void ApplyAirtechPatches() {
    uintptr_t base = GetEFZBase();
    if (!base) {
        LogOut("[AUTO-AIRTECH] Cannot get game base address", true);
        return;
    }
    if (patchesApplied) {
        LogOut("[AUTO-AIRTECH] Patches were unexpectedly applied; removing for safe mode", true);
        PatchMemory(base + AIRTECH_ENABLE_ADDR, originalEnableBytes, 2);
        PatchMemory(base + AIRTECH_FORWARD_ADDR, originalForwardBytes, 2);
        PatchMemory(base + AIRTECH_BACKWARD_ADDR, originalBackwardBytes, 2);
        patchesApplied = false;
    }
}

void RemoveAirtechPatches() {
// Nothing is applied in safe mode; never rewrite live code bytes we did not change - they may belong to another mod.
    if (!patchesApplied) return;
    uintptr_t base = GetEFZBase();
    if (!base) {
        LogOut("[AUTO-AIRTECH] Cannot get game base address", true);
        return;
    }

    PatchMemory(base + AIRTECH_ENABLE_ADDR, originalEnableBytes, 2);
    PatchMemory(base + AIRTECH_FORWARD_ADDR, originalForwardBytes, 2);
    PatchMemory(base + AIRTECH_BACKWARD_ADDR, originalBackwardBytes, 2);

    patchesApplied = false;
    LogOut("[AUTO-AIRTECH] Patches removed", detailedLogging.load());
}

// Check if the player is currently in an airtechable state
bool IsPlayerAirtechable(short moveID, int playerNum) {
    uintptr_t base = GetEFZBase();
    if (!base) return false;
    
    uintptr_t baseOffset = (playerNum == 1) ? EFZ_BASE_OFFSET_P1 : EFZ_BASE_OFFSET_P2;
    
    short untechValue = 0;
    uintptr_t untechAddr = ResolvePointer(base, baseOffset, UNTECH_OFFSET);
    if (untechAddr) {
        SafeReadMemory(untechAddr, &untechValue, sizeof(short));
    }
    
    // Airtechable: launched hitstun or FIRE/ELECTRIC stun; AIR_GUARD excluded (spurious triggers while air-blocking).
    bool airtechableMoveID = (moveID >= LAUNCHED_HITSTUN_START && moveID <= LAUNCHED_HITSTUN_END) ||
                             (moveID == FIRE_STATE || moveID == ELECTRIC_STATE);
    
    // Untech value == 0 means the player can tech.
    return airtechableMoveID && (untechValue == 0);
}

bool IsAirtechAnimation(short moveID) {
    return moveID == FORWARD_AIRTECH || moveID == BACKWARD_AIRTECH;
}

void MonitorAutoAirtech(short moveID1, short moveID2) {
    if (GetCurrentGameMode() != GameMode::Practice) return;
    if (IsNetplaySuspendActive()) return;
    
    static bool prevEnabled = false;
    static int prevDirection = -1;
    static int prevDelay = -1;
    static bool p1WasAirtechable = false;
    static bool p2WasAirtechable = false;
    static int p1DelayReadyTick = -1;
    static int p2DelayReadyTick = -1;
    static int p1LastLoggedRemainingVisual = -1;
    static int p2LastLoggedRemainingVisual = -1;
    static int debugCounter = 0;
    static bool p1WasInjecting = false;
    static bool p2WasInjecting = false;
    static uint32_t s_lastLifecycleGeneration = 0;
    static int s_lastInternalTick = -1;
    static int p1InjectRemaining = 0;
    static int p2InjectRemaining = 0;
    static bool p1Armed = false;
    static bool p2Armed = false;
    static int p1AttemptCount = 0;
    static int p2AttemptCount = 0;
    constexpr int kPreNeutralFrames = 3;          // internal frames (~1 visual) of forced neutral before each attempt
    constexpr int kInjectHoldFramesInitial = 6;   // internal frames (~2 visual) hold of dir+button
    constexpr int kMaxRetryAttempts = 3;          // A, then B, then C
    static int p1PreNeutral = 0, p2PreNeutral = 0;
    static bool p1AttemptActive = false, p2AttemptActive = false;
    static int p1PhaseAnchorTick = -1, p2PhaseAnchorTick = -1; // tick the current phase was last seen published

    bool p1FacingRightNow = GetPlayerFacingDirection(1);
    bool p2FacingRightNow = GetPlayerFacingDirection(2);
    g_airtechP1FacingRight.store(p1FacingRightNow);
    g_airtechP2FacingRight.store(p2FacingRightNow);
    const uint32_t lifecycleGeneration = GetRuntimeLifecycleGeneration();
    const int currentInternalTick = GetEngineTick();

    auto clearPlayerState = [&](int playerNum, const char* reason, bool verboseLog) {
        ((playerNum == 1) ? p1PhaseAnchorTick : p2PhaseAnchorTick) = -1;
        int* readyTick = (playerNum == 1) ? &p1DelayReadyTick : &p2DelayReadyTick;
        int* lastRemainingVisual = (playerNum == 1) ? &p1LastLoggedRemainingVisual : &p2LastLoggedRemainingVisual;
        int* injectRemaining = (playerNum == 1) ? &p1InjectRemaining : &p2InjectRemaining;
        int* attemptCount = (playerNum == 1) ? &p1AttemptCount : &p2AttemptCount;
        int* preNeutral = (playerNum == 1) ? &p1PreNeutral : &p2PreNeutral;
        bool* wasAirtechable = (playerNum == 1) ? &p1WasAirtechable : &p2WasAirtechable;
        bool* isAirteching = (playerNum == 1) ? &p1IsAirteching : &p2IsAirteching;
        bool* armed = (playerNum == 1) ? &p1Armed : &p2Armed;
        bool* attemptActive = (playerNum == 1) ? &p1AttemptActive : &p2AttemptActive;
        bool* wasInjecting = (playerNum == 1) ? &p1WasInjecting : &p2WasInjecting;
        std::atomic<bool>& activeFlag = (playerNum == 1) ? g_airtechP1Active : g_airtechP2Active;
        std::atomic<bool>& airtechableFlag = (playerNum == 1) ? g_airtechP1Airtechable : g_airtechP2Airtechable;

        const bool hadWork =
            *armed || *attemptActive || *injectRemaining > 0 || *preNeutral > 0 ||
            *readyTick >= 0 || *isAirteching || *wasAirtechable ||
            g_manualInputOverride[playerNum].load() || g_manualInputMask[playerNum].load() != 0;

        (void)ReleaseAirtechOverride(playerNum);
        *readyTick = -1;
        *lastRemainingVisual = -1;
        *injectRemaining = 0;
        *attemptCount = 0;
        *preNeutral = 0;
        *wasAirtechable = false;
        *isAirteching = false;
        *armed = false;
        *attemptActive = false;
        *wasInjecting = false;
        activeFlag.store(false);
        airtechableFlag.store(false);

        if (verboseLog && hadWork) {
            LogOut(std::string("[AUTO-AIRTECH] P") + std::to_string(playerNum) +
                   " cleared state (" + reason + ")", true);
        }
    };

    if (s_lastLifecycleGeneration != 0 && lifecycleGeneration != s_lastLifecycleGeneration) {
        LogOut("[AUTO-AIRTECH] Lifecycle generation changed " +
               std::to_string(s_lastLifecycleGeneration) + " -> " + std::to_string(lifecycleGeneration) +
               "; resetting airtech delay/injection state", true);
        clearPlayerState(1, "lifecycle change", false);
        clearPlayerState(2, "lifecycle change", false);
    }
    if (s_lastInternalTick >= 0 && currentInternalTick < s_lastInternalTick) {
        LogOut("[AUTO-AIRTECH] Internal frame counter moved backwards " +
               std::to_string(s_lastInternalTick) + " -> " + std::to_string(currentInternalTick) +
               "; resetting airtech delay/injection state", true);
        clearPlayerState(1, "frame counter rewind", false);
        clearPlayerState(2, "frame counter rewind", false);
    }
    s_lastLifecycleGeneration = lifecycleGeneration;
    s_lastInternalTick = currentInternalTick;

    if (!autoAirtechEnabled.load()) {
        g_airtechP1Active.store(false);
        g_airtechP2Active.store(false);
        g_airtechP1Airtechable.store(false);
        g_airtechP2Airtechable.store(false);
        if (g_manualInputOverride[1].load()) (void)ReleaseAirtechOverride(1);
        if (g_manualInputOverride[2].load()) (void)ReleaseAirtechOverride(2);
        // A stale armed delay must not fire the moment the feature is re-enabled.
        if (prevEnabled) {
            clearPlayerState(1, "disabled", false);
            clearPlayerState(2, "disabled", false);
            prevEnabled = false;
        }
        static auto s_lastDisabledBeat = std::chrono::steady_clock::now();
        auto nowBeat = std::chrono::steady_clock::now();
        if (detailedLogging.load() && (nowBeat - s_lastDisabledBeat) >= std::chrono::seconds(30)) {
            LogOut("[AUTO-AIRTECH] Idle (disabled) - skipping per-frame processing", true);
            s_lastDisabledBeat = nowBeat;
        }
        return;
    }

    bool p1CurrentlyAirteching = IsAirtechAnimation(moveID1);
    bool p2CurrentlyAirteching = IsAirtechAnimation(moveID2);
    
    if (p1CurrentlyAirteching && !p1IsAirteching) {
        LogOut("[AUTO-AIRTECH] P1 entered airtech animation", detailedLogging.load());
        if (ValidationMetricsEnabled()) { GetValidationMetrics().p1AirtechSuccess++; }
    if (patchesApplied) RemoveAirtechPatches();
        clearPlayerState(1, "entered airtech animation", false);
    }
    
    if (p2CurrentlyAirteching && !p2IsAirteching) {
        LogOut("[AUTO-AIRTECH] P2 entered airtech animation", detailedLogging.load());
        if (ValidationMetricsEnabled()) { GetValidationMetrics().p2AirtechSuccess++; }
    if (patchesApplied) RemoveAirtechPatches();
        clearPlayerState(2, "entered airtech animation", false);
    }
    
    p1IsAirteching = p1CurrentlyAirteching;
    p2IsAirteching = p2CurrentlyAirteching;

    static bool lastP1Active=false, lastP2Active=false, lastP1Able=false, lastP2Able=false;
    static auto lastHeartbeat = std::chrono::steady_clock::now();
    bool p1ActiveNow = p1IsAirteching;
    bool p2ActiveNow = p2IsAirteching;
    bool p1AbleNow = IsPlayerAirtechable(moveID1, 1);
    bool p2AbleNow = IsPlayerAirtechable(moveID2, 2);
    if (ValidationMetricsEnabled()) {
        if (p1AbleNow && !p1WasAirtechable) GetValidationMetrics().p1AirtechableEdges++;
        if (p2AbleNow && !p2WasAirtechable) GetValidationMetrics().p2AirtechableEdges++;
    }
    g_airtechP1Active.store(p1ActiveNow);
    g_airtechP2Active.store(p2ActiveNow);
    g_airtechP1Airtechable.store(p1AbleNow);
    g_airtechP2Airtechable.store(p2AbleNow);

    bool changed = (p1ActiveNow!=lastP1Active) || (p2ActiveNow!=lastP2Active) ||
                   (p1AbleNow!=lastP1Able) || (p2AbleNow!=lastP2Able);
    auto nowTs = std::chrono::steady_clock::now();
    bool heartbeat = (nowTs - lastHeartbeat) >= std::chrono::seconds(5) && (p1ActiveNow || p2ActiveNow || p1AbleNow || p2AbleNow);
    if (detailedLogging.load() && (changed || heartbeat)) {
        LogOut("[AUTO-AIRTECH] Status: Enabled=" + std::to_string(autoAirtechEnabled.load()) +
               ", Direction=" + std::to_string(autoAirtechDirection.load()) +
               ", Delay=" + std::to_string(autoAirtechDelay.load()) +
               ", Patches=" + (patchesApplied ? "YES" : "NO") +
               ", P1Active=" + std::string(p1ActiveNow?"YES":"NO") +
               ", P2Active=" + std::string(p2ActiveNow?"YES":"NO") +
               ", P1Able=" + std::string(p1AbleNow?"YES":"NO") +
               ", P2Able=" + std::string(p2AbleNow?"YES":"NO"),
               true);
        lastP1Active=p1ActiveNow; lastP2Active=p2ActiveNow;
        lastP1Able=p1AbleNow; lastP2Able=p2AbleNow;
        if (heartbeat) lastHeartbeat = nowTs;
    }

    bool settingsChanged = (prevEnabled != autoAirtechEnabled.load() ||
                           prevDirection != autoAirtechDirection.load() ||
                           prevDelay != autoAirtechDelay.load());
    
    if (settingsChanged) {
        if (patchesApplied) RemoveAirtechPatches();
        clearPlayerState(1, "settings changed", false);
        clearPlayerState(2, "settings changed", false);
        
        prevEnabled = autoAirtechEnabled.load();
        prevDirection = autoAirtechDirection.load();
        prevDelay = autoAirtechDelay.load();

        LogOut(
            std::string("[AUTO-AIRTECH] Settings changed -> Enabled=") + (prevEnabled ? "1" : "0") +
            ", Direction=" + std::to_string(prevDirection) +
            ", DelayVisual=" + std::to_string(prevDelay) +
            ", DelayInternal=" + std::to_string(VisualFramesToInternalTicks(prevDelay)),
            detailedLogging.load());
    }

    auto composeMaskForAttempt = [&](int playerNum, int attemptIdx) -> uint8_t {
        bool facingRight = (playerNum == 1) ? g_airtechP1FacingRight.load() : g_airtechP2FacingRight.load();
        bool forward = autoAirtechDirection.load() == 0;
        uint8_t horzMask = 0;
        if (forward) horzMask = facingRight ? GAME_INPUT_RIGHT : GAME_INPUT_LEFT;
        else         horzMask = facingRight ? GAME_INPUT_LEFT  : GAME_INPUT_RIGHT;
        uint8_t buttonMask = 0;
        switch (attemptIdx) {
            case 0: buttonMask = GAME_INPUT_A; break;
            case 1: buttonMask = GAME_INPUT_B; break;
            default: buttonMask = GAME_INPUT_C; break;
        }
        return horzMask | buttonMask;
    };

    if (autoAirtechEnabled.load()) {
        bool p1Airtechable = p1AbleNow;
        bool p2Airtechable = p2AbleNow;
        bool p1JustBecameAirtechable = (p1Airtechable && !p1WasAirtechable);
        bool p2JustBecameAirtechable = (p2Airtechable && !p2WasAirtechable);

    if (!p1IsAirteching) {
            if (p1JustBecameAirtechable) {
                const int delayVisual = autoAirtechDelay.load();
                const int delayInternal = VisualFramesToInternalTicks(delayVisual);
                p1DelayReadyTick = currentInternalTick + delayInternal;
                p1LastLoggedRemainingVisual = delayVisual;
                p1Armed = true;
                p1AttemptCount = 0;
                short untechValue = 0;
                uintptr_t base = GetEFZBase();
                uintptr_t untechAddr = ResolvePointer(base, EFZ_BASE_OFFSET_P1, UNTECH_OFFSET);
                if (untechAddr) {
                    SafeReadMemory(untechAddr, &untechValue, sizeof(short));
                }
                LogOut("[AUTO-AIRTECH] P1 became airtechable (moveID=" + std::to_string(moveID1) +
                       ", untech=" + std::to_string(untechValue) +
                       ", delayVisual=" + std::to_string(delayVisual) +
                       ", delayInternal=" + std::to_string(delayInternal) +
                       ", armTick=" + std::to_string(currentInternalTick) +
                       ", readyTick=" + std::to_string(p1DelayReadyTick) + ")", true);
            }
            if (p1Armed && !p1Airtechable && p1WasAirtechable) {
                LogOut(std::string("[AUTO-AIRTECH] P1 airtechable window ended before delay expiry; canceling armed attempt ") +
                       "(readyTick=" + std::to_string(p1DelayReadyTick) +
                       ", currentTick=" + std::to_string(currentInternalTick) + ")", true);
                p1Armed = false;
                p1DelayReadyTick = -1;
                p1LastLoggedRemainingVisual = -1;
            } else if (p1Armed && p1DelayReadyTick > currentInternalTick) {
                const int remainingInternal = p1DelayReadyTick - currentInternalTick;
                const int remainingVisual = InternalTicksToVisualFramesCeil(remainingInternal);
                if (detailedLogging.load() && remainingVisual != p1LastLoggedRemainingVisual) {
                    LogOut("[AUTO-AIRTECH] P1 delay countdown visual=" + std::to_string(remainingVisual) +
                           " internal=" + std::to_string(remainingInternal) +
                           " readyTick=" + std::to_string(p1DelayReadyTick), true);
                    p1LastLoggedRemainingVisual = remainingVisual;
                }
            } else if (p1Armed && p1Airtechable && p1DelayReadyTick >= 0 && currentInternalTick >= p1DelayReadyTick) {
                p1PreNeutral = kPreNeutralFrames;
                p1InjectRemaining = kInjectHoldFramesInitial;
                p1Armed = false;
                p1DelayReadyTick = -1;
                p1LastLoggedRemainingVisual = -1;
                p1AttemptCount++;
                bool facingRight = p1FacingRightNow;
                bool forward = autoAirtechDirection.load() == 0;
                uint8_t horz = 0;
                if (forward) horz = facingRight ? 1 : 255; else horz = facingRight ? 255 : 1;
                const char* dirStr = forward ? "FORWARD" : "BACKWARD";
                LogOut(std::string("[AUTO-AIRTECH] P1 delay ready -> injecting A+dir (dir=") + dirStr +
                       ", facingRight=" + (facingRight ? "1" : "0") +
                       ", horz=" + std::to_string(horz) +
                       ", attempt=" + std::to_string(p1AttemptCount) +
                       ", currentTick=" + std::to_string(currentInternalTick) + ")",
                       true);
            }
            p1WasAirtechable = p1Airtechable;
        }

    if (!p2IsAirteching) {
            if (p2JustBecameAirtechable) {
                const int delayVisual = autoAirtechDelay.load();
                const int delayInternal = VisualFramesToInternalTicks(delayVisual);
                p2DelayReadyTick = currentInternalTick + delayInternal;
                p2LastLoggedRemainingVisual = delayVisual;
                p2Armed = true;
                p2AttemptCount = 0;
                short untechValue = 0;
                uintptr_t base = GetEFZBase();
                uintptr_t untechAddr = ResolvePointer(base, EFZ_BASE_OFFSET_P2, UNTECH_OFFSET);
                if (untechAddr) {
                    SafeReadMemory(untechAddr, &untechValue, sizeof(short));
                }
                LogOut("[AUTO-AIRTECH] P2 became airtechable (moveID=" + std::to_string(moveID2) +
                       ", untech=" + std::to_string(untechValue) +
                       ", delayVisual=" + std::to_string(delayVisual) +
                       ", delayInternal=" + std::to_string(delayInternal) +
                       ", armTick=" + std::to_string(currentInternalTick) +
                       ", readyTick=" + std::to_string(p2DelayReadyTick) + ")", true);
            }
            if (p2Armed && !p2Airtechable && p2WasAirtechable) {
                LogOut(std::string("[AUTO-AIRTECH] P2 airtechable window ended before delay expiry; canceling armed attempt ") +
                       "(readyTick=" + std::to_string(p2DelayReadyTick) +
                       ", currentTick=" + std::to_string(currentInternalTick) + ")", true);
                p2Armed = false;
                p2DelayReadyTick = -1;
                p2LastLoggedRemainingVisual = -1;
            } else if (p2Armed && p2DelayReadyTick > currentInternalTick) {
                const int remainingInternal = p2DelayReadyTick - currentInternalTick;
                const int remainingVisual = InternalTicksToVisualFramesCeil(remainingInternal);
                if (detailedLogging.load() && remainingVisual != p2LastLoggedRemainingVisual) {
                    LogOut("[AUTO-AIRTECH] P2 delay countdown visual=" + std::to_string(remainingVisual) +
                           " internal=" + std::to_string(remainingInternal) +
                           " readyTick=" + std::to_string(p2DelayReadyTick), true);
                    p2LastLoggedRemainingVisual = remainingVisual;
                }
            } else if (p2Armed && p2Airtechable && p2DelayReadyTick >= 0 && currentInternalTick >= p2DelayReadyTick) {
                p2PreNeutral = kPreNeutralFrames;
                p2InjectRemaining = kInjectHoldFramesInitial;
                p2Armed = false;
                p2DelayReadyTick = -1;
                p2LastLoggedRemainingVisual = -1;
                p2AttemptCount++;
                bool facingRight = p2FacingRightNow;
                bool forward = autoAirtechDirection.load() == 0;
                uint8_t horz = 0;
                if (forward) horz = facingRight ? 1 : 255; else horz = facingRight ? 255 : 1;
                const char* dirStr = forward ? "FORWARD" : "BACKWARD";
                LogOut(std::string("[AUTO-AIRTECH] P2 delay ready -> injecting A+dir (dir=") + dirStr +
                       ", facingRight=" + (facingRight ? "1" : "0") +
                       ", horz=" + std::to_string(horz) +
                       ", attempt=" + std::to_string(p2AttemptCount) +
                       ", currentTick=" + std::to_string(currentInternalTick) + ")",
                       true);
            }
            p2WasAirtechable = p2Airtechable;
        }
    }

    // Counts down engine ticks only, from the first sighting with the mask published.
    auto consumeTicks = [&](int& remaining, int& anchorTick) {
        if (anchorTick >= 0) {
            remaining -= currentInternalTick - anchorTick;
            if (remaining < 0) remaining = 0;
        }
        anchorTick = remaining > 0 ? currentInternalTick : -1;
    };

    if (autoAirtechEnabled.load()) {
        if (p1PreNeutral > 0) {
            if (PublishAirtechOverride(1, 0, true)) {
                consumeTicks(p1PreNeutral, p1PhaseAnchorTick);
                p1AttemptActive = false;
            }
        } else if (p1InjectRemaining > 0) {
            if (!p1AttemptActive) {
                uint8_t mask = composeMaskForAttempt(1, p1AttemptCount - 1);
                if (PublishAirtechOverride(1, mask, true)) {
                    p1AttemptActive = true;
                }
            }
            if (p1AttemptActive) consumeTicks(p1InjectRemaining, p1PhaseAnchorTick);
        } else if (p1AttemptActive) {
            (void)ReleaseAirtechOverride(1);
            p1AttemptActive = false;
            LogOut("[AUTO-AIRTECH] P1 attempt window complete", detailedLogging.load());
            if (!p1IsAirteching && IsPlayerAirtechable(moveID1,1) && p1AttemptCount < kMaxRetryAttempts) {
                p1PreNeutral = kPreNeutralFrames;
                p1InjectRemaining = kInjectHoldFramesInitial;
                p1AttemptCount++;
                LogOut("[AUTO-AIRTECH] P1 scheduling retry attempt=" + std::to_string(p1AttemptCount), detailedLogging.load());
            }
        }

        if (p2PreNeutral > 0) {
            if (PublishAirtechOverride(2, 0, true)) {
                consumeTicks(p2PreNeutral, p2PhaseAnchorTick);
                p2AttemptActive = false;
            }
        } else if (p2InjectRemaining > 0) {
            if (!p2AttemptActive) {
                uint8_t mask = composeMaskForAttempt(2, p2AttemptCount - 1);
                if (PublishAirtechOverride(2, mask, true)) {
                    p2AttemptActive = true;
                }
            }
            if (p2AttemptActive) consumeTicks(p2InjectRemaining, p2PhaseAnchorTick);
        } else if (p2AttemptActive) {
            (void)ReleaseAirtechOverride(2);
            p2AttemptActive = false;
            LogOut("[AUTO-AIRTECH] P2 attempt window complete", detailedLogging.load());
            if (!p2IsAirteching && IsPlayerAirtechable(moveID2,2) && p2AttemptCount < kMaxRetryAttempts) {
                p2PreNeutral = kPreNeutralFrames;
                p2InjectRemaining = kInjectHoldFramesInitial;
                p2AttemptCount++;
                LogOut("[AUTO-AIRTECH] P2 scheduling retry attempt=" + std::to_string(p2AttemptCount), detailedLogging.load());
            }
        }
    } else {
        if (p1WasInjecting || g_manualInputOverride[1].load()) {
            (void)ReleaseAirtechOverride(1);
            p1WasInjecting = false;
            p1InjectRemaining = 0;
            p1DelayReadyTick = -1;
            p1LastLoggedRemainingVisual = -1;
            p1Armed = false;
            p1AttemptCount = 0;
            p1PreNeutral = 0;
            p1AttemptActive = false;
        }
        if (p2WasInjecting || g_manualInputOverride[2].load()) {
            (void)ReleaseAirtechOverride(2);
            p2WasInjecting = false;
            p2InjectRemaining = 0;
            p2DelayReadyTick = -1;
            p2LastLoggedRemainingVisual = -1;
            p2Armed = false;
            p2AttemptCount = 0;
            p2PreNeutral = 0;
            p2AttemptActive = false;
        }
    }
}
