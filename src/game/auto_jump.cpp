#include "../include/game/auto_jump.h"
#include "../include/game/auto_action.h"
#include "../include/core/constants.h"
#include "../include/utils/utilities.h"
#include "../include/utils/network.h"
#include "../include/game/frame_advantage.h"
#include "../include/game/game_state.h"
#include "../include/game/validation_metrics.h" // validation metrics instrumentation

#include "../include/core/memory.h"
#include "../include/core/logger.h"
#include "../include/input/input_motion.h"
#include "../include/input/immediate_input.h"
#include "../include/input/input_hook.h"
#include "../include/input/input_buffer.h"
#include "../include/input/motion_system.h"
#include <chrono>
#include <cmath>

// Prefer relative X; within the epsilon keep the last known direction (the facing flag lags side swaps).
static bool ForwardIsRightForPlayer(int p) {
    static uintptr_t cachedBase = 0;
    static uintptr_t xAddr[3] = { 0, 0, 0 }; // [1]=P1, [2]=P2
    static uint32_t cachedGeneration = 0;
    uintptr_t baseNow = GetEFZBase();
    const uint32_t generationNow = GetRuntimeLifecycleGeneration();
    if (generationNow != cachedGeneration) {
        cachedBase = 0;
        xAddr[1] = 0;
        xAddr[2] = 0;
        cachedGeneration = generationNow;
    }
    if (baseNow != 0 && baseNow != cachedBase) {
        cachedBase = baseNow;
        xAddr[1] = ResolvePointer(baseNow, EFZ_BASE_OFFSET_P1, XPOS_OFFSET);
        xAddr[2] = ResolvePointer(baseNow, EFZ_BASE_OFFSET_P2, XPOS_OFFSET);
    }

    auto getXPos = [&](int player) -> double {
        double x = 0.0;
        if (player >= 1 && player <= 2 && xAddr[player]) {
            SafeReadMemory(xAddr[player], &x, sizeof(double));
        }
        return x;
    };

    static bool s_lastForwardRight[3] = { true, true, true }; // default forward->right
    static bool s_init[3] = { false, false, false };

    int opp = (p == 1 ? 2 : 1);
    double selfX = getXPos(p);
    double oppX = getXPos(opp);
    double dx = oppX - selfX;

    constexpr double kEpsilon = 2.0; // pixels

    if (std::fabs(dx) >= kEpsilon) {
        bool fr = dx > 0.0; // forward is toward opponent
        s_lastForwardRight[p] = fr;
        s_init[p] = true;
        return fr;
    }

    if (s_init[p]) return s_lastForwardRight[p];

    bool facingRight = GetPlayerFacingDirection(p);
    s_lastForwardRight[p] = facingRight;
    s_init[p] = true;
    return s_lastForwardRight[p];
}

void ApplyJump(uintptr_t moveIDAddr, int playerNum, int jumpType) {
    using clock = std::chrono::steady_clock;
    static clock::time_point lastLogAt[3] = { clock::time_point(), clock::time_point(), clock::time_point() };
    const auto now = clock::now();
    const auto logInterval = std::chrono::seconds(2);
    bool shouldLog = (!detailedLogging.load()) ? false : (lastLogAt[playerNum] == clock::time_point() || (now - lastLogAt[playerNum]) >= logInterval);
    if (shouldLog) {
        LogOut("[AUTO-JUMP] Executing jump via input system for P" + std::to_string(playerNum), true);
        lastLogAt[playerNum] = now;
    }

    std::string jumpTypeName;
    uint8_t inputMask = MOTION_INPUT_UP;

    switch (jumpType) {
        case 0: // Straight
            inputMask = MOTION_INPUT_UP;
            jumpTypeName = "straight";
            break;
        case 1: // Forward
            if (ForwardIsRightForPlayer(playerNum)) {
                inputMask = MOTION_INPUT_UP | MOTION_INPUT_RIGHT;
            } else {
                inputMask = MOTION_INPUT_UP | MOTION_INPUT_LEFT;
            }
            jumpTypeName = "forward";
            break;
        case 2: // Backward
            if (ForwardIsRightForPlayer(playerNum)) {
                inputMask = MOTION_INPUT_UP | MOTION_INPUT_LEFT;
            } else {
                inputMask = MOTION_INPUT_UP | MOTION_INPUT_RIGHT;
            }
            jumpTypeName = "backward";
            break;
        default:
            inputMask = MOTION_INPUT_UP;
            jumpTypeName = "straight (default)";
            break;
    }

    // Use centralized 64fps immediate writer
    ImmediateInput::Set(playerNum, inputMask);

    if (shouldLog) {
        LogOut("[AUTO-JUMP] Holding " + jumpTypeName + " jump input for P" +
               std::to_string(playerNum) + " (immediate-only)", true);
    }
}

bool IsAutoActionActiveForPlayer(int playerNum) {
    // Timed ImmediateInput presses are owned by other producers; auto-jump uses the untimed Set path.
    if (ImmediateInput::GetRemainingTicks(playerNum) > 0 ||
        IsAutoActionNormalPulseActive(playerNum) ||
        IsAutoActionNormalPulseOwningImmediateRegisters(playerNum) ||
        GetMotionQueueSnapshot(playerNum).active ||
        (g_bufferFreezingActive.load(std::memory_order_acquire) &&
         (g_activeFreezePlayer.load(std::memory_order_acquire) == 0 ||
          g_activeFreezePlayer.load(std::memory_order_acquire) == playerNum))) {
        return true;
    }

    if (!HasAnyAutoActionTriggerEnabled()) {
        return false;
    }

    if (ResolveAutoActionTargetPlayer() != playerNum) {
        return false;
    }
    
    if (playerNum == 1) {
        return p1DelayState.isDelaying || p1ActionApplied;
    } else if (playerNum == 2) {
        return p2DelayState.isDelaying || p2ActionApplied;
    }
    
    return false;
}

void MonitorAutoJump() {
    if (GetCurrentGameMode() != GameMode::Practice) return;
    if (IsNetplaySuspendActive()) return;

    // One visual frame, so the engine's next burst actually polls the neutral.
    constexpr int kLandingNeutralTicks = 3;
    using clock = std::chrono::steady_clock;
    static clock::time_point holdUntil[3] = { clock::time_point(), clock::time_point(), clock::time_point() };
    static clock::time_point nextApplyAt[3] = { clock::time_point(), clock::time_point(), clock::time_point() };

    // Landing-edge handling: we must present a neutral frame between jumps so the game sees a new UP press.
    static bool s_wasGrounded[3] = { true, true, true };
    static int  s_neutralUntilTick[3] = { 0, 0, 0 };
    static int  s_prevTarget = 0;
    const int tickNow = GetEngineTick();
    // The clock restarts each Practice battle.
    for (int p = 1; p <= 2; ++p) {
        if (s_neutralUntilTick[p] > tickNow + kLandingNeutralTicks) s_neutralUntilTick[p] = 0;
    }

    auto isGrounded = [](int p) -> bool {
        static uintptr_t s_cachedBase = 0;
        static uintptr_t s_yAddr[3] = { 0, 0, 0 };
        static uint32_t s_cachedGeneration = 0;
        uintptr_t baseNow = GetEFZBase();
        const uint32_t generationNow = GetRuntimeLifecycleGeneration();
        if (generationNow != s_cachedGeneration) {
            s_cachedBase = 0;
            s_yAddr[1] = 0;
            s_yAddr[2] = 0;
            s_cachedGeneration = generationNow;
        }
        if (baseNow != 0 && baseNow != s_cachedBase) {
            s_cachedBase = baseNow;
            s_yAddr[1] = ResolvePointer(baseNow, EFZ_BASE_OFFSET_P1, YPOS_OFFSET);
            s_yAddr[2] = ResolvePointer(baseNow, EFZ_BASE_OFFSET_P2, YPOS_OFFSET);
        }
        double y = 0.0;
        if (p >= 1 && p <= 2 && s_yAddr[p]) {
            SafeReadMemory(s_yAddr[p], &y, sizeof(double));
        }
        return (y >= -0.1); // world Y is zero on the floor and negative above it
    };

    auto releaseIfOurJump = [](int p) {
        // Don't interfere with timed presses (e.g., wake-up jumps)
        int remainingTicks = ImmediateInput::GetRemainingTicks(p);
        if (remainingTicks > 0) return;
        
        uint8_t mask = ImmediateInput::GetCurrentDesired(p);
        bool looksLikeJump = (mask & MOTION_INPUT_UP) && ((mask & MOTION_INPUT_BUTTON) == 0);
        if (looksLikeJump) { 
            ImmediateInput::Clear(p);
        }
    };

    if (!autoJumpEnabled.load()) {
        releaseIfOurJump(1);
        releaseIfOurJump(2);
        holdUntil[1] = holdUntil[2] = clock::time_point();
        nextApplyAt[1] = nextApplyAt[2] = clock::time_point();
        return;
    }

    // Caller already runs at ~192 Hz.
    const auto now = clock::now();

    uintptr_t moveIDAddr1 = 0;
    uintptr_t moveIDAddr2 = 0;

    int targetPlayer = jumpTarget.load();
    int direction = jumpDirection.load();

    // A side dropped from the target must not keep holding UP.
    if (targetPlayer != s_prevTarget) {
        if (targetPlayer != 1 && targetPlayer != 3) releaseIfOurJump(1);
        if (targetPlayer != 2 && targetPlayer != 3) releaseIfOurJump(2);
        s_prevTarget = targetPlayer;
    }

    const auto holdDuration = std::chrono::milliseconds(1000); // unused in continuous mode
    const auto reapplyInterval = std::chrono::milliseconds(1000); // unused in continuous mode

    if (targetPlayer == 1 || targetPlayer == 3) {
        if (IsAutoActionActiveForPlayer(1)) {
            releaseIfOurJump(1);
        } else {
            bool grounded = isGrounded(1);
            bool landing = grounded && !s_wasGrounded[1];
            if (landing && ValidationMetricsEnabled()) { GetValidationMetrics().p1LandingEdges++; }
            s_wasGrounded[1] = grounded;
            bool fwdRight = ForwardIsRightForPlayer(1);
            uint8_t wantMask = MOTION_INPUT_UP | (
                direction == 0 ? 0 :
                (direction == 1 ? (fwdRight ? MOTION_INPUT_RIGHT : MOTION_INPUT_LEFT)
                                 : (fwdRight ? MOTION_INPUT_LEFT  : MOTION_INPUT_RIGHT))
            );
            uint8_t curMask = ImmediateInput::GetCurrentDesired(1);

            if (landing) {
                s_neutralUntilTick[1] = tickNow + kLandingNeutralTicks;
                if (ValidationMetricsEnabled()) { GetValidationMetrics().p1ForcedNeutralFrames++; }
            }

            if (tickNow < s_neutralUntilTick[1]) {
                ImmediateInput::Set(1, 0);
            } else {
                bool needApply = curMask != wantMask || curMask == 0;
                if (needApply) {
                    ApplyJump(moveIDAddr1, 1, direction);
                    holdUntil[1] = now + holdDuration;
                }
            }
        }
    }

    if (targetPlayer == 2 || targetPlayer == 3) {
        if (IsAutoActionActiveForPlayer(2)) {
            releaseIfOurJump(2);
        } else {
            bool grounded = isGrounded(2);
            bool landing = grounded && !s_wasGrounded[2];
            if (landing && ValidationMetricsEnabled()) { GetValidationMetrics().p2LandingEdges++; }
            s_wasGrounded[2] = grounded;
            bool fwdRight = ForwardIsRightForPlayer(2);
            uint8_t wantMask = MOTION_INPUT_UP | (
                direction == 0 ? 0 :
                (direction == 1 ? (fwdRight ? MOTION_INPUT_RIGHT : MOTION_INPUT_LEFT)
                                 : (fwdRight ? MOTION_INPUT_LEFT  : MOTION_INPUT_RIGHT))
            );
            uint8_t curMask = ImmediateInput::GetCurrentDesired(2);

            if (landing) {
                s_neutralUntilTick[2] = tickNow + kLandingNeutralTicks;
                if (ValidationMetricsEnabled()) { GetValidationMetrics().p2ForcedNeutralFrames++; }
            }

            if (tickNow < s_neutralUntilTick[2]) {
                ImmediateInput::Set(2, 0);
            } else {
                bool needApply = curMask != wantMask || curMask == 0;
                if (needApply) {
                    ApplyJump(moveIDAddr2, 2, direction);
                    holdUntil[2] = now + holdDuration;
                }
            }
        }
    }

}

void AutoJumpReleaseForPlayer(int playerNum) {
    if (playerNum < 1 || playerNum > 2) return;
    uint8_t mask = g_manualInputMask[playerNum].load();
    bool looksLikeJump = (mask & MOTION_INPUT_UP) && ((mask & MOTION_INPUT_BUTTON) == 0);
    if (g_manualInputOverride[playerNum].load() && g_injectImmediateOnly[playerNum].load() && looksLikeJump) {
        g_manualInputOverride[playerNum].store(false);
        g_manualInputMask[playerNum].store(0);
        g_injectImmediateOnly[playerNum].store(false);
    }
}
