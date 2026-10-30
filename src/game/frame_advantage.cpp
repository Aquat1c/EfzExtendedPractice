#include "../include/game/frame_advantage.h"
#include "../include/core/constants.h"
#include "../include/utils/utilities.h"
#include "../include/utils/config.h"

#include "../include/core/memory.h"
#include "../include/core/logger.h"
#include "../include/game/auto_action.h"
#include "../include/game/frame_analysis.h"
#include "../include/game/frame_monitor.h"
#include "../include/game/game_state.h"
#include "../include/gui/framebar.h"
#include "../include/gui/overlay.h"
#include "../include/input/input_core.h"
#include "../include/utils/i18n.h"
#include "../include/utils/xp_compat.h"

// Aggregate-initialiser field order for FrameAdvantageState.
FrameAdvantageState frameAdvState = {
    false, false, false, false,    // p1InBlockstun, p2InBlockstun, p1InHitstun, p2InHitstun
    false, false,                  // p1Defending, p2Defending
    false, false,                  // p1Attacking, p2Attacking
    -1, -1,                        // p1AttackStartInternalFrame, p2AttackStartInternalFrame
    0, 0,                          // p1AttackMoveID, p2AttackMoveID
    -1, -1,                        // p1BlockstunStartInternalFrame, p2BlockstunStartInternalFrame
    -1, -1,                        // p1HitstunStartInternalFrame, p2HitstunStartInternalFrame
    -1, -1,                        // p1ActionableInternalFrame, p2ActionableInternalFrame
    -1, -1,                        // p1DefenderFreeInternalFrame, p2DefenderFreeInternalFrame
    0.0, 0.0,                      // p1FrameAdvantage, p2FrameAdvantage
    false, false,                  // p1AdvantageCalculated, p2AdvantageCalculated
    0, 0,                          // p1GapFrames, p2GapFrames
    false, false,                  // p1GapCalculated, p2GapCalculated
    0, 0,                          // p1InitialBlockstunMoveID, p2InitialBlockstunMoveID
    -1,                            // displayUntilInternalFrame
    -1                             // gapDisplayUntilInternalFrame
};

// Suppress regular FA overlay updates until this internal frame (0 = off)
std::atomic<int> g_SkipRegularFAOverlayUntilFrame{0};

namespace {

std::atomic<int> g_engineTick{0};
std::atomic<int> g_actionableEdgeTick[3];
std::atomic<bool> g_engineTickLive{false};
bool g_superFlashNow = false; // game thread, set per tick before MonitorFrameAdvantage

struct FrameAdvantageScratchState {
    int p1LastDefenderFreeFrame = -1;
    int p2LastDefenderFreeFrame = -1;
    int p1FreezeAccumSinceFree = 0;
    int p2FreezeAccumSinceFree = 0;
    int p1FreezeAfterAtkActionable = 0;
    int p2FreezeAfterAtkActionable = 0;
    int p1HitConnectCooldown = 0;
    int p2HitConnectCooldown = 0;
    int p1LastAttackEdgeFrame = -1;
    int p2LastAttackEdgeFrame = -1;
    bool p1DefenderFreeNeedsDelay = false;
    bool p2DefenderFreeNeedsDelay = false;
    int pendingAdvantageAttacker = 0;
    int pendingAdvantageInternal = 0;
    int pendingAdvantageReadyInternalFrame = -1;
};

struct FrameAdvantageRuntimeState {
    FrameAdvantageScratchState scratch;
    ULONGLONG displayUntilTimeMs = 0;
    int staleFrameCounter = 0;
    bool loggedFirstCall = false;
    int debugLogCounter = 0;
    short lastLoggedMoveID1 = -1;
    short lastLoggedMoveID2 = -1;
    short diagnosticPreviousMove1 = -1;
    short diagnosticPreviousMove2 = -1;
    int step3LogDecimator = 0;
    int step4LogDecimator = 0;
    int step5LogDecimator = 0;
};

FrameAdvantageRuntimeState g_faRuntime{};
FrameAdvantageScratchState& g_faScratch = g_faRuntime.scratch;
ULONGLONG& g_displayUntilTimeMs = g_faRuntime.displayUntilTimeMs;

void ResetFrameAdvantageScratchState() {
    g_faScratch = FrameAdvantageScratchState{};
}

void ClearGapAnchorForDefender(int defender) {
    if (defender == 1) {
        g_faScratch.p1LastDefenderFreeFrame = -1;
        g_faScratch.p1FreezeAccumSinceFree = 0;
    } else if (defender == 2) {
        g_faScratch.p2LastDefenderFreeFrame = -1;
        g_faScratch.p2FreezeAccumSinceFree = 0;
    }
}

bool HasResolvedFrameAdvantageDisplayOrPending() {
    return frameAdvState.p1AdvantageCalculated ||
           frameAdvState.p2AdvantageCalculated ||
           g_faScratch.pendingAdvantageAttacker != 0 ||
           g_displayUntilTimeMs != 0 ||
           g_FrameAdvantageId != -1 ||
           g_FrameAdvantage2Id != -1;
}

void ClearFrameAdvantageOverlayMessages() {
    if (g_FrameAdvantageId != -1) {
        DirectDrawHook::RemovePermanentMessage(g_FrameAdvantageId);
        g_FrameAdvantageId = -1;
    }
    if (g_FrameAdvantage2Id != -1) {
        DirectDrawHook::RemovePermanentMessage(g_FrameAdvantage2Id);
        g_FrameAdvantage2Id = -1;
    }
    if (g_FrameGapId != -1) {
        DirectDrawHook::RemovePermanentMessage(g_FrameGapId);
        g_FrameGapId = -1;
    }
}

} // namespace

constexpr int kStaleResetInternalFrames = static_cast<int>(INTERNAL_FRAMES_PER_SECOND * 21.0);
constexpr int kPendingRegularFADisplayInternalFrames = 90; // 30 visual frames

static ULONGLONG GetDisplayDurationMs() {
    float durationSeconds = Config::GetSettings().frameAdvantageDisplayDuration;
    if (durationSeconds < 0.5f) durationSeconds = 0.5f;
    if (durationSeconds > 30.0f) durationSeconds = 30.0f;
    return static_cast<ULONGLONG>(durationSeconds * 1000.0f);
}

void ArmFrameAdvantageDisplayTimer() {
    g_displayUntilTimeMs = XPCompat::GetTickCount64Compat() + GetDisplayDurationMs();
    frameAdvState.displayUntilInternalFrame = -1;
}

void CancelPendingRegularFrameAdvantage() {
    g_faScratch.pendingAdvantageAttacker = 0;
    g_faScratch.pendingAdvantageInternal = 0;
    g_faScratch.pendingAdvantageReadyInternalFrame = -1;
}

void QueuePendingRegularFrameAdvantage(int attacker, int frameAdvantageInternal, int currentInternalFrame, bool delayDisplay) {
    g_faScratch.pendingAdvantageAttacker = attacker;
    g_faScratch.pendingAdvantageInternal = frameAdvantageInternal;
    g_faScratch.pendingAdvantageReadyInternalFrame = delayDisplay
        ? currentInternalFrame + kPendingRegularFADisplayInternalFrames
        : currentInternalFrame;
}

void FlushPendingRegularFrameAdvantage(int currentInternalFrame, ULONGLONG currentTimeMs) {
    if (g_faScratch.pendingAdvantageAttacker == 0) return;
    if (currentInternalFrame < g_faScratch.pendingAdvantageReadyInternalFrame) return;

    const int frameAdvantage = g_faScratch.pendingAdvantageInternal;
    const int attacker = g_faScratch.pendingAdvantageAttacker;
    const std::string frameAdvText = FormatFrameAdvantage(frameAdvantage);

    if (g_showFrameAdvantageOverlay.load() && currentInternalFrame >= g_SkipRegularFAOverlayUntilFrame.load()) {
        if (g_FrameGapId != -1) {
            DirectDrawHook::RemovePermanentMessage(g_FrameGapId);
            g_FrameGapId = -1;
            frameAdvState.gapDisplayUntilInternalFrame = -1;
        }

        if (g_FrameAdvantageId != -1) {
            DirectDrawHook::UpdatePermanentMessage(g_FrameAdvantageId, frameAdvText,
                frameAdvantage >= 0 ? RGB(0, 255, 0) : RGB(255, 0, 0));
        } else {
            g_FrameAdvantageId = DirectDrawHook::AddPermanentMessage(frameAdvText,
                frameAdvantage >= 0 ? RGB(0, 255, 0) : RGB(255, 0, 0), 305, 430);
        }
        if (g_FrameAdvantage2Id != -1) {
            DirectDrawHook::RemovePermanentMessage(g_FrameAdvantage2Id);
            g_FrameAdvantage2Id = -1;
        }

        g_displayUntilTimeMs = currentTimeMs + GetDisplayDurationMs();
    }

    LogOut(std::string("[FRAME_ADV] P") + std::to_string(attacker) +
           "->P" + std::to_string(3 - attacker) +
           " Frame Advantage: " + frameAdvText, true);

    CancelPendingRegularFrameAdvantage();
}

// Used by frame_monitor.cpp.
int GetDisplayDurationInternalFrames() {
    // 192 internal FPS = 3 internal frames per visual frame.
    float durationSeconds = Config::GetSettings().frameAdvantageDisplayDuration;
    if (durationSeconds < 0.5f) durationSeconds = 0.5f;
    if (durationSeconds > 30.0f) durationSeconds = 30.0f;
    int frames = static_cast<int>(durationSeconds * 192.0f);
    
    return frames;
}

void ResetFrameAdvantageState() {
    frameAdvState.p1InBlockstun = false;
    frameAdvState.p2InBlockstun = false;
    frameAdvState.p1InHitstun = false;
    frameAdvState.p2InHitstun = false;
    frameAdvState.p1Defending = false;
    frameAdvState.p2Defending = false;
    frameAdvState.p1Attacking = false;
    frameAdvState.p2Attacking = false;
    frameAdvState.p1AttackStartInternalFrame = -1;
    frameAdvState.p2AttackStartInternalFrame = -1;
    frameAdvState.p1AttackMoveID = 0;
    frameAdvState.p2AttackMoveID = 0;
    frameAdvState.p1BlockstunStartInternalFrame = -1;
    frameAdvState.p2BlockstunStartInternalFrame = -1;
    frameAdvState.p1HitstunStartInternalFrame = -1;
    frameAdvState.p2HitstunStartInternalFrame = -1;
    frameAdvState.p1ActionableInternalFrame = -1;
    frameAdvState.p2ActionableInternalFrame = -1;
    frameAdvState.p1DefenderFreeInternalFrame = -1;
    frameAdvState.p2DefenderFreeInternalFrame = -1;
    frameAdvState.p1FrameAdvantage = 0.0;
    frameAdvState.p2FrameAdvantage = 0.0;
    frameAdvState.p1AdvantageCalculated = false;
    frameAdvState.p2AdvantageCalculated = false;
    frameAdvState.p1GapFrames = 0;
    frameAdvState.p2GapFrames = 0;
    frameAdvState.p1GapCalculated = false;
    frameAdvState.p2GapCalculated = false;
    frameAdvState.p1InitialBlockstunMoveID = 0;
    frameAdvState.p2InitialBlockstunMoveID = 0;
    frameAdvState.displayUntilInternalFrame = -1;
    frameAdvState.gapDisplayUntilInternalFrame = -1;
    g_displayUntilTimeMs = 0;
    g_SkipRegularFAOverlayUntilFrame.store(0);
    ResetFrameAdvantageScratchState();
    
    if (detailedLogging.load()) {
    #if defined(ENABLE_FRAME_ADV_DEBUG)
    LogOut("[FRAME_ADV_DEBUG] Frame advantage state reset", false);
    #endif
    }

    ClearFrameAdvantageOverlayMessages();
}

void ResetFrameAdvantageTimeline() {
    // Full reset once old measurement work has drained.
    ResetFrameAdvantageState();
    g_faRuntime = FrameAdvantageRuntimeState{};
}

// Clears overlay/messages only; tracking state untouched (teleport/reset).
void ClearFrameAdvantageDisplay() {
    g_displayUntilTimeMs = 0;
    g_SkipRegularFAOverlayUntilFrame.store(0);
    frameAdvState.displayUntilInternalFrame = -1;
    frameAdvState.gapDisplayUntilInternalFrame = -1;
    CancelPendingRegularFrameAdvantage();
    ClearFrameAdvantageOverlayMessages();
}

// For artificial state changes (teleport/reset) where the current exchange
// would produce meaningless results.
void CancelFrameAdvantageCalculation() {
    ResetFrameAdvantageState();

#if defined(ENABLE_FRAME_ADV_DEBUG)
    if (detailedLogging.load()) {
        LogOut("[FRAME_ADV_DEBUG] Cancelled current frame advantage calculation due to external reset/teleport", false);
    }
#endif
}

int GetEngineTick() {
    return g_engineTick.load(std::memory_order_acquire);
}

int GetActionableEdgeTick(int player) {
    return (player == 1 || player == 2) ? g_actionableEdgeTick[player].load(std::memory_order_acquire) : 0;
}

int GetCurrentInternalFrame() {
    return GetEngineTick();
}

void DropEngineTickState() {
    if (!g_engineTickLive.load(std::memory_order_acquire)) return;
    if (!DropPrearmWorkAtPracticeBoundary()) return;
    g_engineTickLive.store(false, std::memory_order_release);
    g_engineTick.store(0, std::memory_order_release);
    g_actionableEdgeTick[1].store(0, std::memory_order_release);
    g_actionableEdgeTick[2].store(0, std::memory_order_release);
    ResetFrameAdvantageState();
    FrameBar::Reset();
}

void OnEngineTick() {
    // Netplay gates run first; online, suspend, session, non-Practice and non-battle
    // all stop here, with no game memory read while any netplay flag is up.
    if (g_onlineModeActive.load(std::memory_order_acquire) ||
        !g_featuresEnabled.load(std::memory_order_acquire) ||
        !IsTrainingMenuContext()) {
        DropEngineTickState();
        return;
    }

    static short s_prevMove1 = 0;
    static short s_prevMove2 = 0;
    const int tick = g_engineTick.fetch_add(1, std::memory_order_acq_rel) + 1;
    g_engineTickLive.store(true, std::memory_order_release);
    if (tick == 1) {
        s_prevMove1 = 0;
        s_prevMove2 = 0;
    }

    const uintptr_t p1 = GetPlayerPointer(1);
    const uintptr_t p2 = GetPlayerPointer(2);
    short move1 = 0, move2 = 0, flash1 = 0, flash2 = 0;
    if (!p1 || !p2 ||
        !SafeReadMemory(p1 + MOVE_ID_OFFSET, &move1, sizeof(move1)) ||
        !SafeReadMemory(p2 + MOVE_ID_OFFSET, &move2, sizeof(move2)) ||
        !SafeReadMemory(p1 + SUPERFLASH_FREEZE_OFFSET, &flash1, sizeof(flash1)) ||
        !SafeReadMemory(p2 + SUPERFLASH_FREEZE_OFFSET, &flash2, sizeof(flash2))) {
        return;
    }

    if (IsActionable(move1) && !IsActionable(s_prevMove1)) g_actionableEdgeTick[1].store(tick, std::memory_order_release);
    if (IsActionable(move2) && !IsActionable(s_prevMove2)) g_actionableEdgeTick[2].store(tick, std::memory_order_release);

    FrameBar::TickSample();

    g_superFlashNow = flash1 > 0 || flash2 > 0;
    MonitorFrameAdvantage(move1, move2, s_prevMove1, s_prevMove2);
    s_prevMove1 = move1;
    s_prevMove2 = move2;
}

std::string FormatFrameAdvantage(int advantageInternal) {
    int visualFrames = advantageInternal / 3;
    int subframes = std::abs(advantageInternal % 3);

    // Sign only for positives - avoids double "+" later.
    std::string sign = (advantageInternal >= 0) ? "+" : "";
    std::string subframeStr = "";
    
    if (subframes == 1) {
        subframeStr = ".33";
    } else if (subframes == 2) {
        subframeStr = ".66";
    }
    
    // Special case for negative values where the whole part is zero
    if (visualFrames == 0 && advantageInternal < 0) {
        return "-0" + subframeStr;
    }

    return sign + std::to_string(visualFrames) + subframeStr;
}

bool IsAttackMove(short moveID) {
    // Attack move ID ranges.
    return (moveID >= 200 && moveID <= 350) ||           
           (moveID >= 400 && moveID <= 500);
}

bool IsDefenderFreeForFA(short prevMoveID, short moveID, bool actionable) {
    if (!actionable) return false;
    if (moveID != FALLING_ID) return true;
    return IsAirtech(prevMoveID);
}

bool ShouldDelayRegularFADisplay(short prevMoveID, short moveID) {
    const bool isLanding = moveID == LANDING_ID ||
                           moveID == LANDING_1_ID ||
                           moveID == LANDING_2_ID ||
                           moveID == LANDING_3_ID;
    return IsGroundtech(prevMoveID) ||
           IsAirtech(prevMoveID) ||
           prevMoveID == FALLING_ID ||
           moveID == FALLING_ID ||
           isLanding;
}

void MonitorFrameAdvantage(short moveID1, short moveID2, short prevMoveID1, short prevMoveID2) {
    if (g_deepFrameAdvDebug.load()) {
        auto& s_loggedOnce = g_faRuntime.loggedFirstCall;
        if (!s_loggedOnce) {
            LogOut("[FA_DIAG] MonitorFrameAdvantage ACTIVE - first call", true);
            s_loggedOnce = true;
        }
    }
    int currentInternalFrame = GetCurrentInternalFrame();
    ULONGLONG currentTimeMs = XPCompat::GetTickCount64Compat();
    int &p1_last_defender_free_frame = g_faScratch.p1LastDefenderFreeFrame;
    int &p2_last_defender_free_frame = g_faScratch.p2LastDefenderFreeFrame;
    int &p1_freeze_accum_since_free = g_faScratch.p1FreezeAccumSinceFree;
    int &p2_freeze_accum_since_free = g_faScratch.p2FreezeAccumSinceFree;
    int &p1_freeze_after_atk_actionable = g_faScratch.p1FreezeAfterAtkActionable;
    int &p2_freeze_after_atk_actionable = g_faScratch.p2FreezeAfterAtkActionable;
    int &p1_hit_connect_cooldown = g_faScratch.p1HitConnectCooldown;
    int &p2_hit_connect_cooldown = g_faScratch.p2HitConnectCooldown;
    int &p1_last_attack_edge_frame = g_faScratch.p1LastAttackEdgeFrame;
    int &p2_last_attack_edge_frame = g_faScratch.p2LastAttackEdgeFrame;
    
    auto& debugLogCounter = g_faRuntime.debugLogCounter;
    if (++debugLogCounter % 60 == 0 && g_displayUntilTimeMs != 0) {
        #if defined(ENABLE_FRAME_ADV_DEBUG)
        ULONGLONG remaining = (currentTimeMs < g_displayUntilTimeMs) ? (g_displayUntilTimeMs - currentTimeMs) : 0;
        LogOut("[FRAME_ADV_DEBUG] Waiting for timer: current=" + std::to_string(currentTimeMs) + 
               " expiry=" + std::to_string(g_displayUntilTimeMs) +
               " remaining=" + std::to_string(remaining) + "ms", true);
        #endif
    }
    
    if (p1_hit_connect_cooldown > 0) p1_hit_connect_cooldown--;
    if (p2_hit_connect_cooldown > 0) p2_hit_connect_cooldown--;
    const bool p1_attack_edge = IsAttackMove(moveID1) && !IsAttackMove(prevMoveID1);
    const bool p2_attack_edge = IsAttackMove(moveID2) && !IsAttackMove(prevMoveID2);
    if (p1_attack_edge) p1_last_attack_edge_frame = currentInternalFrame;
    if (p2_attack_edge) p2_last_attack_edge_frame = currentInternalFrame;
    
    auto& lastLoggedMoveID1 = g_faRuntime.lastLoggedMoveID1;
    auto& lastLoggedMoveID2 = g_faRuntime.lastLoggedMoveID2;
    if (detailedLogging.load() && (moveID1 != lastLoggedMoveID1 || moveID2 != lastLoggedMoveID2)) {
     #if defined(ENABLE_FRAME_ADV_DEBUG)
        LogOut("[FRAME_ADV_DEBUG] MoveChange: frame=" + std::to_string(currentInternalFrame) +
               " p1Prev=" + std::to_string(prevMoveID1) +
               " p1Curr=" + std::to_string(moveID1) +
               " p2Prev=" + std::to_string(prevMoveID2) +
               " p2Curr=" + std::to_string(moveID2) +
               " p1Block=" + std::to_string(frameAdvState.p1InBlockstun) +
               " p2Block=" + std::to_string(frameAdvState.p2InBlockstun) +
               " p1Def=" + std::to_string(frameAdvState.p1Defending) +
               " p2Def=" + std::to_string(frameAdvState.p2Defending) +
               " p1Atk=" + std::to_string(frameAdvState.p1Attacking) +
               " p2Atk=" + std::to_string(frameAdvState.p2Attacking),
               false);
     #endif
        lastLoggedMoveID1 = moveID1;
        lastLoggedMoveID2 = moveID2;
    }

            if (g_displayUntilTimeMs != 0 && currentTimeMs >= g_displayUntilTimeMs) {
        #if defined(ENABLE_FRAME_ADV_DEBUG)
             LogOut("[FRAME_ADV_DEBUG] Timer expired: current=" + std::to_string(currentTimeMs) + 
                 " expiry=" + std::to_string(g_displayUntilTimeMs), true);
             LogOut("[FA_TIMER] Message cleared - current=" + std::to_string(currentTimeMs) + 
                 " expiry=" + std::to_string(g_displayUntilTimeMs), true);
        #endif
        if (g_FrameAdvantageId != -1) {
            DirectDrawHook::RemovePermanentMessage(g_FrameAdvantageId);
            g_FrameAdvantageId = -1;
        }
        if (g_FrameAdvantage2Id != -1) {
            DirectDrawHook::RemovePermanentMessage(g_FrameAdvantage2Id);
            g_FrameAdvantage2Id = -1;
        }
        g_displayUntilTimeMs = 0;
        g_SkipRegularFAOverlayUntilFrame.store(0);
        frameAdvState.displayUntilInternalFrame = -1;
    }
    
    if (frameAdvState.gapDisplayUntilInternalFrame != -1 && currentInternalFrame >= frameAdvState.gapDisplayUntilInternalFrame) {
        if (g_FrameGapId != -1) {
            DirectDrawHook::RemovePermanentMessage(g_FrameGapId);
            g_FrameGapId = -1;
        }
        frameAdvState.gapDisplayUntilInternalFrame = -1;
    }
    
    const struct { bool actionable1, actionable2; } faSample{IsActionable(moveID1), IsActionable(moveID2)};

    bool p1NewAction = (IsAttackMove(moveID1) && !IsAttackMove(prevMoveID1)) ||
                       (moveID1 == FORWARD_DASH_START_ID || moveID1 == BACKWARD_DASH_START_ID) ||
                       (moveID1 == STRAIGHT_JUMP_ID || moveID1 == FORWARD_JUMP_ID || moveID1 == BACKWARD_JUMP_ID);
    bool p2NewAction = (IsAttackMove(moveID2) && !IsAttackMove(prevMoveID2)) ||
                       (moveID2 == FORWARD_DASH_START_ID || moveID2 == BACKWARD_DASH_START_ID) ||
                       (moveID2 == STRAIGHT_JUMP_ID || moveID2 == FORWARD_JUMP_ID || moveID2 == BACKWARD_JUMP_ID);
    
    if (p1NewAction || p2NewAction) {
        #if defined(ENABLE_FRAME_ADV_DEBUG)
        LogOut("[FA_TIMER] Message cleared by new action - current=" + std::to_string(currentTimeMs) + 
               " p1NewAction=" + std::to_string(p1NewAction) + 
               " p2NewAction=" + std::to_string(p2NewAction) +
               " p1Move=" + std::to_string(moveID1) +
               " p2Move=" + std::to_string(moveID2), detailedLogging.load());
        #endif
        if (g_FrameAdvantageId != -1) {
            DirectDrawHook::RemovePermanentMessage(g_FrameAdvantageId);
            g_FrameAdvantageId = -1;
        }
        if (g_FrameAdvantage2Id != -1) {
            DirectDrawHook::RemovePermanentMessage(g_FrameAdvantage2Id);
            g_FrameAdvantage2Id = -1;
        }
        g_displayUntilTimeMs = 0;
        g_SkipRegularFAOverlayUntilFrame.store(0);
        frameAdvState.displayUntilInternalFrame = -1;
    }
    
    
    // Detect when defender becomes actionable again (robust vs knockdown/tech/wakeup) for gap detection
    bool p1_becomes_actionable = !IsActionable(prevMoveID1) && IsDefenderFreeForFA(prevMoveID1, moveID1, faSample.actionable1);
    bool p2_becomes_actionable = !IsActionable(prevMoveID2) && IsDefenderFreeForFA(prevMoveID2, moveID2, faSample.actionable2);

    if (p1_becomes_actionable && frameAdvState.p1Defending && frameAdvState.p1DefenderFreeInternalFrame == -1) {
        p1_last_defender_free_frame = currentInternalFrame;
        p1_freeze_accum_since_free = 0;
        #if defined(ENABLE_FRAME_ADV_DEBUG)
        LogOut("[FRAME_ADV_DEBUG] P1 became actionable at frame " + std::to_string(currentInternalFrame), 
            detailedLogging.load());
        #endif
    }

    if (p2_becomes_actionable && frameAdvState.p2Defending && frameAdvState.p2DefenderFreeInternalFrame == -1) {
        p2_last_defender_free_frame = currentInternalFrame;
        p2_freeze_accum_since_free = 0;
        #if defined(ENABLE_FRAME_ADV_DEBUG)
        LogOut("[FRAME_ADV_DEBUG] P2 became actionable at frame " + std::to_string(currentInternalFrame), 
            detailedLogging.load());
        #endif
    }

    // Ticks with an active superflash stop the other side's scripts (efz.c:12231) and
    // are removed from gap and FA. Fire/electric/frozen stun (81-86) stops neither
    // side's clock, and a real game-speed freeze runs no ticks at all.
    const int freezeTick = g_superFlashNow ? 1 : 0;
    if (p1_last_defender_free_frame != -1) p1_freeze_accum_since_free += freezeTick;
    if (p2_last_defender_free_frame != -1) p2_freeze_accum_since_free += freezeTick;

    // Also accumulate freeze that occurs after attacker recovery but before defender free.
    if (frameAdvState.p1Attacking && frameAdvState.p1ActionableInternalFrame != -1 &&
        frameAdvState.p2DefenderFreeInternalFrame == -1) {
        p1_freeze_after_atk_actionable += freezeTick;
    }
    if (frameAdvState.p2Attacking && frameAdvState.p2ActionableInternalFrame != -1 &&
        frameAdvState.p1DefenderFreeInternalFrame == -1) {
        p2_freeze_after_atk_actionable += freezeTick;
    }

    // STEP 1: Detect if an attack connects (P1 attacking P2)
    bool p2_entering_blockstun = IsBlockstunState(moveID2) && !IsBlockstunState(prevMoveID2);
    bool p2_entering_hitstun = IsHitstun(moveID2) && !IsHitstun(prevMoveID2);
    bool p2_entering_launch = IsLaunched(moveID2) && !IsLaunched(prevMoveID2);
    bool p2_entering_thrown   = IsThrown(moveID2)    && !IsThrown(prevMoveID2);
    // Fallback: treat transition from actionable->non-actionable as a connect if it occurs shortly after an attack edge
    bool p2_entering_nonactionable = IsActionable(prevMoveID2) && !faSample.actionable2;
    bool p1_recent_attack_window = (p1_last_attack_edge_frame >= 0) && (currentInternalFrame - p1_last_attack_edge_frame <= 60);
    
    // Suppress false "connect" detection caused by IC/BIC/FIC superflash.
    // Do NOT re-add IsGameSpeedFrozen(): it can read stale 0 on some versions
    // (e.g. Revival 1.02e slot=1); the IC move ID checks are sufficient.
    // on certain game versions (e.g. Revival 1.02e slot=1 reading stale 0).
    // The IC move ID checks alone are sufficient to detect superflash states.
    bool superflashActive = (moveID1 == GROUND_IC_ID || moveID1 == AIR_IC_ID) ||
                            (moveID2 == GROUND_IC_ID || moveID2 == AIR_IC_ID);

    if (g_deepFrameAdvDebug.load()) {
        auto& s_diagPrevM1 = g_faRuntime.diagnosticPreviousMove1;
        auto& s_diagPrevM2 = g_faRuntime.diagnosticPreviousMove2;
        if (moveID1 != s_diagPrevM1 || moveID2 != s_diagPrevM2) {
            LogOut("[FA_DIAG] MoveChange frame=" + std::to_string(currentInternalFrame) +
                   " p1=" + std::to_string(prevMoveID1) + "->" + std::to_string(moveID1) +
                   " p2=" + std::to_string(prevMoveID2) + "->" + std::to_string(moveID2) +
                   " p2Block=" + std::to_string(IsBlockstunState(moveID2)) +
                   " p2Hit=" + std::to_string(IsHitstun(moveID2)) +
                   " p2Launch=" + std::to_string(IsLaunched(moveID2)) +
                   " p2Thrown=" + std::to_string(IsThrown(moveID2)) +
                   " p1Atk=" + std::to_string(IsAttackMove(moveID1)) +
                   " p1Act=" + std::to_string(faSample.actionable1) +
                   " p2Act=" + std::to_string(faSample.actionable2) +
                   " p2BlockEdge=" + std::to_string(p2_entering_blockstun) +
                   " p2HitEdge=" + std::to_string(p2_entering_hitstun) +
                   " p2LaunchEdge=" + std::to_string(p2_entering_launch) +
                   " p2ThrownEdge=" + std::to_string(p2_entering_thrown) +
                   " superflash=" + std::to_string(superflashActive) +
                   " p1AtkEdge=" + std::to_string(p1_attack_edge) +
                   " cooldown=" + std::to_string(p1_hit_connect_cooldown),
                   detailedLogging.load());
            s_diagPrevM1 = moveID1;
            s_diagPrevM2 = moveID2;
        }
    }

    if (!superflashActive &&
        (p2_entering_blockstun || p2_entering_hitstun || p2_entering_launch || p2_entering_thrown ||
         (p2_entering_nonactionable && p1_recent_attack_window))
        && p1_hit_connect_cooldown == 0) {

        if (g_deepFrameAdvDebug.load()) {
            LogOut("[FA_DIAG] STEP1 P1->P2 CONNECT at frame=" + std::to_string(currentInternalFrame) +
                   " block=" + std::to_string(p2_entering_blockstun) +
                   " hit=" + std::to_string(p2_entering_hitstun) +
                   " launch=" + std::to_string(p2_entering_launch) +
                   " thrown=" + std::to_string(p2_entering_thrown) +
                   " nonact=" + std::to_string(p2_entering_nonactionable) +
                   " atkEdge=" + std::to_string(p1_attack_edge) +
                   " m1=" + std::to_string(moveID1) + " m2=" + std::to_string(moveID2), true);
        }

        const bool hadResolvedFrameAdvantage = HasResolvedFrameAdvantageDisplayOrPending();

        // A fresh regular contact takes precedence over any previous RG/FA display.
        // even when it comes from a later hit of the same move rather than a new attack edge.
        ClearFrameAdvantageDisplay();
        
        // Calculate gap if there was a previous defender free frame (string of attacks)
        if (!hadResolvedFrameAdvantage && p2_last_defender_free_frame != -1) {
            int gapFramesRaw = currentInternalFrame - p2_last_defender_free_frame;
            int gapFrames = gapFramesRaw - p2_freeze_accum_since_free;
            if (gapFrames < 0) gapFrames = 0;
            
            if (gapFrames > 0 && gapFrames <= 60) {
                int visualGapFrames = gapFrames / 3;
                int subframes = gapFrames % 3;
                
                std::string gapText = I18n::TrFormat("Gap: %d", visualGapFrames);
                if (subframes == 1) {
                    gapText += ".33";
                } else if (subframes == 2) {
                    gapText += ".66";
                }
                
                // Display gap message immediately (reuses the FA message slot temporarily)
                if (g_showFrameAdvantageOverlay.load()) {
                    if (g_FrameGapId != -1) {
                        DirectDrawHook::UpdatePermanentMessage(g_FrameGapId, gapText, RGB(255, 255, 0));
                    } else {
                        // Position gap at same location as frame advantage (Y=430)
                        g_FrameGapId = DirectDrawHook::AddPermanentMessage(gapText, RGB(255, 255, 0), 305, 430);
                    }
                    // Display for ~1/3 second (60 internal frames)
                    frameAdvState.gapDisplayUntilInternalFrame = currentInternalFrame + 60;
                }
                
                if (g_deepFrameAdvDebug.load()) {
                    LogOut(std::string("[FRAME_ADV] Gap detected: ") + gapText +
                           " (raw=" + std::to_string(gapFramesRaw) +
                           ", freeze-removed=" + std::to_string(p2_freeze_accum_since_free) + ")", true);
                }
            }
        } else if (hadResolvedFrameAdvantage) {
            ClearGapAnchorForDefender(2);
        }
        
        
        if (frameAdvState.p1Attacking || frameAdvState.p2Attacking) {
            frameAdvState.p1Attacking = false;
            frameAdvState.p2Attacking = false;
            frameAdvState.p1AttackStartInternalFrame = -1;
            frameAdvState.p2AttackStartInternalFrame = -1;
            frameAdvState.p1ActionableInternalFrame = -1;
            frameAdvState.p2ActionableInternalFrame = -1;
            frameAdvState.p1AdvantageCalculated = false;
            frameAdvState.p2AdvantageCalculated = false;
        }
        
        // Always clear previous calculation flags on a brand-new hit to avoid alternating suppression
        frameAdvState.p1AdvantageCalculated = false;
        frameAdvState.p2AdvantageCalculated = false;
        
        frameAdvState.p1Attacking = true;
        frameAdvState.p2Defending = true;
        frameAdvState.p1Defending = false;
        frameAdvState.p1AttackStartInternalFrame = currentInternalFrame;
        frameAdvState.p2DefenderFreeInternalFrame = -1;
        g_faScratch.p2DefenderFreeNeedsDelay = false;
    p1_freeze_after_atk_actionable = 0;
        
        if (p2_entering_blockstun) {
            frameAdvState.p2InBlockstun = true;
            frameAdvState.p2InHitstun = false;
            frameAdvState.p2BlockstunStartInternalFrame = currentInternalFrame;
            frameAdvState.p2InitialBlockstunMoveID = moveID2;
        } else if (p2_entering_hitstun || p2_entering_launch) {
            frameAdvState.p2InBlockstun = false;
            frameAdvState.p2InHitstun = true;
            frameAdvState.p2HitstunStartInternalFrame = currentInternalFrame;
        } else if (p2_entering_thrown) {
            // Thrown: treat as a connect without setting block/hitstun flags; timings will resolve on actionable
            frameAdvState.p2InBlockstun = false;
            frameAdvState.p2InHitstun = false;
        } else {
            // Non-actionable fallback (e.g., knockdown transition not classified as hit/throw)
            frameAdvState.p2InBlockstun = false;
            frameAdvState.p2InHitstun = false;
        }
        
         #if defined(ENABLE_FRAME_ADV_DEBUG)
         LogOut("[FRAME_ADV_DEBUG] P1->P2 hit connected at frame " + std::to_string(currentInternalFrame), 
             detailedLogging.load());
         #endif
        
    p2_last_defender_free_frame = -1;
    p2_freeze_accum_since_free = 0;
        
        // Apply a minimal cooldown (just 3 internal frames = 1 visual frame)
        p1_hit_connect_cooldown = 3;
    }
    
    // STEP 2: Detect if an attack connects (P2 attacking P1) - mirror of P1 logic
    bool p1_entering_blockstun = IsBlockstunState(moveID1) && !IsBlockstunState(prevMoveID1);
    bool p1_entering_hitstun = IsHitstun(moveID1) && !IsHitstun(prevMoveID1);
    bool p1_entering_launch = IsLaunched(moveID1) && !IsLaunched(prevMoveID1);
    bool p1_entering_thrown   = IsThrown(moveID1)    && !IsThrown(prevMoveID1);
    bool p1_entering_nonactionable = IsActionable(prevMoveID1) && !faSample.actionable1;
    bool p2_recent_attack_window = (p2_last_attack_edge_frame >= 0) && (currentInternalFrame - p2_last_attack_edge_frame <= 60);
    
    if (!superflashActive &&
        (p1_entering_blockstun || p1_entering_hitstun || p1_entering_launch || p1_entering_thrown ||
         (p1_entering_nonactionable && p2_recent_attack_window))
        && p2_hit_connect_cooldown == 0) {

        if (g_deepFrameAdvDebug.load()) {
            LogOut("[FA_DIAG] STEP2 P2->P1 CONNECT at frame=" + std::to_string(currentInternalFrame) +
                   " block=" + std::to_string(p1_entering_blockstun) +
                   " hit=" + std::to_string(p1_entering_hitstun) +
                   " launch=" + std::to_string(p1_entering_launch) +
                   " thrown=" + std::to_string(p1_entering_thrown) +
                   " nonact=" + std::to_string(p1_entering_nonactionable) +
                   " atkEdge=" + std::to_string(p2_attack_edge) +
                   " m1=" + std::to_string(moveID1) + " m2=" + std::to_string(moveID2), true);
        }
        const bool hadResolvedFrameAdvantage = HasResolvedFrameAdvantageDisplayOrPending();

        // A fresh regular contact takes precedence over any previous RG/FA display.
        // even when it comes from a later hit of the same move rather than a new attack edge.
        ClearFrameAdvantageDisplay();
        
        // Calculate gap if there was a previous defender free frame (string of attacks)
        if (!hadResolvedFrameAdvantage && p1_last_defender_free_frame != -1) {
            int gapFramesRaw = currentInternalFrame - p1_last_defender_free_frame;
            int gapFrames = gapFramesRaw - p1_freeze_accum_since_free;
            if (gapFrames < 0) gapFrames = 0;
            
            if (gapFrames > 0 && gapFrames <= 60) {
                int visualGapFrames = gapFrames / 3;
                int subframes = gapFrames % 3;
                
                std::string gapText = I18n::TrFormat("Gap: %d", visualGapFrames);
                if (subframes == 1) {
                    gapText += ".33";
                } else if (subframes == 2) {
                    gapText += ".66";
                }
                
                // Display gap message immediately (reuses the FA message slot temporarily)
                if (g_showFrameAdvantageOverlay.load()) {
                    if (g_FrameGapId != -1) {
                        DirectDrawHook::UpdatePermanentMessage(g_FrameGapId, gapText, RGB(255, 255, 0));
                    } else {
                        // Position gap at same location as frame advantage (Y=430)
                        g_FrameGapId = DirectDrawHook::AddPermanentMessage(gapText, RGB(255, 255, 0), 305, 430);
                    }
                    // Display for ~1/3 second (60 internal frames)
                    frameAdvState.gapDisplayUntilInternalFrame = currentInternalFrame + 60;
                }
                
                if (g_deepFrameAdvDebug.load()) {
                    LogOut(std::string("[FRAME_ADV] Gap detected: ") + gapText +
                           " (raw=" + std::to_string(gapFramesRaw) +
                           ", freeze-removed=" + std::to_string(p1_freeze_accum_since_free) + ")", true);
                }
            }
        } else if (hadResolvedFrameAdvantage) {
            ClearGapAnchorForDefender(1);
        }
        
        
        if (frameAdvState.p1Attacking || frameAdvState.p2Attacking) {
            frameAdvState.p1Attacking = false;
            frameAdvState.p2Attacking = false;
            frameAdvState.p1AttackStartInternalFrame = -1;
            frameAdvState.p2AttackStartInternalFrame = -1;
            frameAdvState.p1ActionableInternalFrame = -1;
            frameAdvState.p2ActionableInternalFrame = -1;
            frameAdvState.p1AdvantageCalculated = false;
            frameAdvState.p2AdvantageCalculated = false;
        }
        
        // Always clear previous calculation flags on a brand-new hit to avoid alternating suppression
        frameAdvState.p1AdvantageCalculated = false;
        frameAdvState.p2AdvantageCalculated = false;
        
        frameAdvState.p2Attacking = true;
        frameAdvState.p1Defending = true;
        frameAdvState.p2Defending = false;
        frameAdvState.p2AttackStartInternalFrame = currentInternalFrame;
        frameAdvState.p1DefenderFreeInternalFrame = -1;
        g_faScratch.p1DefenderFreeNeedsDelay = false;
    p2_freeze_after_atk_actionable = 0;
        
        if (p1_entering_blockstun) {
            frameAdvState.p1InBlockstun = true;
            frameAdvState.p1InHitstun = false;
            frameAdvState.p1BlockstunStartInternalFrame = currentInternalFrame;
            frameAdvState.p1InitialBlockstunMoveID = moveID1;
        } else if (p1_entering_hitstun || p1_entering_launch) {
            frameAdvState.p1InBlockstun = false;
            frameAdvState.p1InHitstun = true;
            frameAdvState.p1HitstunStartInternalFrame = currentInternalFrame;
        } else if (p1_entering_thrown) {
            frameAdvState.p1InBlockstun = false;
            frameAdvState.p1InHitstun = false;
        } else {
            frameAdvState.p1InBlockstun = false;
            frameAdvState.p1InHitstun = false;
        }
        
         #if defined(ENABLE_FRAME_ADV_DEBUG)
         LogOut("[FRAME_ADV_DEBUG] P2->P1 hit connected at frame " + std::to_string(currentInternalFrame), 
             detailedLogging.load());
         #endif
        
    p1_last_defender_free_frame = -1;
    p1_freeze_accum_since_free = 0;
        
        // Apply a minimal cooldown (just 3 internal frames = 1 visual frame)
        p2_hit_connect_cooldown = 3;
    }
    
    // STEP 3: Detect when attacker exits recovery
    if (frameAdvState.p1Attacking && frameAdvState.p1ActionableInternalFrame == -1) {
        bool attackerRecoveryEdge = (!IsActionable(prevMoveID1) && faSample.actionable1);
        if (g_deepFrameAdvDebug.load()) {
            auto& s_step3LogDecim = g_faRuntime.step3LogDecimator;
            if ((s_step3LogDecim++ % 48) == 0) {
                LogOut("[FA_DIAG] STEP3_WAIT P1atk: prevM1=" + std::to_string(prevMoveID1) +
                       " curM1=" + std::to_string(moveID1) +
                       " IsActionable(prev)=" + std::to_string(IsActionable(prevMoveID1)) +
                       " actionable1=" + std::to_string(faSample.actionable1) +
                       " edge=" + std::to_string(attackerRecoveryEdge), detailedLogging.load());
            }
        }
        if (attackerRecoveryEdge) {
            frameAdvState.p1ActionableInternalFrame = currentInternalFrame;
         if (g_deepFrameAdvDebug.load()) {
             LogOut("[FA_DIAG] STEP3 P1 attacker recovery at frame=" + std::to_string(currentInternalFrame) +
                    " prevM1=" + std::to_string(prevMoveID1) + " curM1=" + std::to_string(moveID1), true);
             LogOut("[FRAME_ADV_DEBUG] P1 attacker recovery ended at frame " + 
                 std::to_string(currentInternalFrame) +
                 " (prevMoveID=" + std::to_string(prevMoveID1) +
                 ", currMoveID=" + std::to_string(moveID1) +
                 ", actionable1=" + std::to_string(faSample.actionable1) + ")",
                 detailedLogging.load());
         }
        }
    }
    
    if (frameAdvState.p2Attacking && frameAdvState.p2ActionableInternalFrame == -1) {
        bool attackerRecoveryEdge = (!IsActionable(prevMoveID2) && faSample.actionable2);
        if (attackerRecoveryEdge) {
            frameAdvState.p2ActionableInternalFrame = currentInternalFrame;
         #if defined(ENABLE_FRAME_ADV_DEBUG)
         LogOut("[FRAME_ADV_DEBUG] P2 attacker recovery ended at frame " + 
             std::to_string(currentInternalFrame) +
             " (prevMoveID=" + std::to_string(prevMoveID2) +
             ", currMoveID=" + std::to_string(moveID2) +
             ", actionable2=" + std::to_string(faSample.actionable2) + ")",
             detailedLogging.load());
         #endif
        }
    }

    // Attacker recommitted before defender free: next recovery becomes the FA anchor.
    // before the defender becomes free, the next recovery should become the new FA anchor.
    if (frameAdvState.p1Attacking &&
        frameAdvState.p1ActionableInternalFrame != -1 &&
        frameAdvState.p2DefenderFreeInternalFrame == -1) {
        bool attackerRecommitted = IsActionable(prevMoveID1) && !faSample.actionable1;
        if (attackerRecommitted) {
            if (g_deepFrameAdvDebug.load()) {
                LogOut("[FA_DIAG] STEP3_RESET P1 actionable anchor cleared by new action at frame=" +
                       std::to_string(currentInternalFrame) +
                       " prevM1=" + std::to_string(prevMoveID1) +
                       " curM1=" + std::to_string(moveID1), true);
            }
            frameAdvState.p1ActionableInternalFrame = -1;
            p1_freeze_after_atk_actionable = 0;
        }
    }

    if (frameAdvState.p2Attacking &&
        frameAdvState.p2ActionableInternalFrame != -1 &&
        frameAdvState.p1DefenderFreeInternalFrame == -1) {
        bool attackerRecommitted = IsActionable(prevMoveID2) && !faSample.actionable2;
        if (attackerRecommitted) {
            if (g_deepFrameAdvDebug.load()) {
                LogOut("[FA_DIAG] STEP3_RESET P2 actionable anchor cleared by new action at frame=" +
                       std::to_string(currentInternalFrame) +
                       " prevM2=" + std::to_string(prevMoveID2) +
                       " curM2=" + std::to_string(moveID2), true);
            }
            frameAdvState.p2ActionableInternalFrame = -1;
            p2_freeze_after_atk_actionable = 0;
        }
    }
    
    // STEP 4: Detect when defender becomes actionable (covers knockdowns, ground/air tech, wakeup)
    if (frameAdvState.p2Defending && frameAdvState.p2DefenderFreeInternalFrame == -1) {
        bool isLanding = (moveID2 == LANDING_ID || moveID2 == LANDING_1_ID || 
                          moveID2 == LANDING_2_ID || moveID2 == LANDING_3_ID);
        // Only exclude landing if they were defending (hit/blocking)
        bool shouldExcludeLanding = isLanding && frameAdvState.p2Defending;
        
        bool defenderFreeEdge = (!IsActionable(prevMoveID2) && IsDefenderFreeForFA(prevMoveID2, moveID2, faSample.actionable2) && !shouldExcludeLanding);
        if (g_deepFrameAdvDebug.load()) {
            auto& s_step4LogDecim = g_faRuntime.step4LogDecimator;
            if ((s_step4LogDecim++ % 48) == 0) {
                LogOut("[FA_DIAG] STEP4_WAIT P2def: prevM2=" + std::to_string(prevMoveID2) +
                       " curM2=" + std::to_string(moveID2) +
                       " IsActionable(prev)=" + std::to_string(IsActionable(prevMoveID2)) +
                       " actionable2=" + std::to_string(faSample.actionable2) +
                       " landing=" + std::to_string(isLanding) +
                       " excludeLanding=" + std::to_string(shouldExcludeLanding) +
                       " edge=" + std::to_string(defenderFreeEdge), detailedLogging.load());
            }
        }
        if (defenderFreeEdge) {
            frameAdvState.p2DefenderFreeInternalFrame = currentInternalFrame;
            g_faScratch.p2DefenderFreeNeedsDelay = ShouldDelayRegularFADisplay(prevMoveID2, moveID2);
            if (g_deepFrameAdvDebug.load()) {
                LogOut("[FA_DIAG] STEP4 P2 defender free at frame=" + std::to_string(currentInternalFrame) +
                       " prevM2=" + std::to_string(prevMoveID2) + " curM2=" + std::to_string(moveID2), true);
                LogOut("[FRAME_ADV_DEBUG] P2 defender actionable at frame " + 
                    std::to_string(currentInternalFrame) +
                    " (prevMoveID=" + std::to_string(prevMoveID2) +
                    ", currMoveID=" + std::to_string(moveID2) +
                    ", actionable2=" + std::to_string(faSample.actionable2) +
                    ", isLanding=" + std::to_string(isLanding) +
                    ", shouldExcludeLanding=" + std::to_string(shouldExcludeLanding) + ")",
                    detailedLogging.load());
            }
        }
    }

    if (frameAdvState.p1Defending && frameAdvState.p1DefenderFreeInternalFrame == -1) {
        bool isLanding = (moveID1 == LANDING_ID || moveID1 == LANDING_1_ID || 
                          moveID1 == LANDING_2_ID || moveID1 == LANDING_3_ID);
        // Only exclude landing if they were defending (hit/blocking)
        bool shouldExcludeLanding = isLanding && frameAdvState.p1Defending;
        
        bool defenderFreeEdge = (!IsActionable(prevMoveID1) && IsDefenderFreeForFA(prevMoveID1, moveID1, faSample.actionable1) && !shouldExcludeLanding);
        if (defenderFreeEdge) {
            frameAdvState.p1DefenderFreeInternalFrame = currentInternalFrame;
            g_faScratch.p1DefenderFreeNeedsDelay = ShouldDelayRegularFADisplay(prevMoveID1, moveID1);
            #if defined(ENABLE_FRAME_ADV_DEBUG)
            LogOut("[FRAME_ADV_DEBUG] P1 defender actionable at frame " + 
                std::to_string(currentInternalFrame) +
                " (prevMoveID=" + std::to_string(prevMoveID1) +
                ", currMoveID=" + std::to_string(moveID1) +
                ", actionable1=" + std::to_string(faSample.actionable1) +
                ", isLanding=" + std::to_string(isLanding) +
                ", shouldExcludeLanding=" + std::to_string(shouldExcludeLanding) + ")",
                detailedLogging.load());
            #endif
        }
    }

    // Whiff/cancel: defender never locked; synthesize defender-free at attacker recovery.
    // If attacker recovery edge and defender free edge occur close together without any connect classification,
    // attempt a synthetic frame advantage using neutral alignment.
    // Track potential synthetic windows for P1 attacking
    if (frameAdvState.p1Attacking && frameAdvState.p1ActionableInternalFrame != -1 && frameAdvState.p2DefenderFreeInternalFrame == -1) {
        // Defender remained actionable (never lost actionability) implies whiff sequence;
        // If we detect attacker actionable and defender already actionable, synthesize immediate zero or negative adv.
        if (IsDefenderFreeForFA(prevMoveID2, moveID2, faSample.actionable2)) {
            // defender was never locked: treat advantage as (defender frame - attacker recovery frame)
            frameAdvState.p2DefenderFreeInternalFrame = frameAdvState.p1ActionableInternalFrame; // same frame
            g_faScratch.p2DefenderFreeNeedsDelay = false;
            #if defined(ENABLE_FRAME_ADV_DEBUG)
            LogOut("[FRAME_ADV_DEBUG] Synthetic defender free (P2) at frame " + std::to_string(frameAdvState.p2DefenderFreeInternalFrame) +
                " (whiff/cancel sequence)", detailedLogging.load());
            #endif
        }
    }
    if (frameAdvState.p2Attacking && frameAdvState.p2ActionableInternalFrame != -1 && frameAdvState.p1DefenderFreeInternalFrame == -1) {
        if (IsDefenderFreeForFA(prevMoveID1, moveID1, faSample.actionable1)) {
            frameAdvState.p1DefenderFreeInternalFrame = frameAdvState.p2ActionableInternalFrame;
            g_faScratch.p1DefenderFreeNeedsDelay = false;
            #if defined(ENABLE_FRAME_ADV_DEBUG)
            LogOut("[FRAME_ADV_DEBUG] Synthetic defender free (P1) at frame " + std::to_string(frameAdvState.p1DefenderFreeInternalFrame) +
                " (whiff/cancel sequence)", detailedLogging.load());
            #endif
        }
    }
    
    // STEP 5: Calculate frame advantage when all necessary data is available
    if (g_deepFrameAdvDebug.load()) {
        if (frameAdvState.p1Attacking || frameAdvState.p2Attacking) {
            auto& s_step5LogDecim = g_faRuntime.step5LogDecimator;
            if ((s_step5LogDecim++ % 48) == 0) {
                LogOut("[FA_DIAG] STEP5_STATE: p1Atk=" + std::to_string(frameAdvState.p1Attacking) +
                       " p1Calc=" + std::to_string(frameAdvState.p1AdvantageCalculated) +
                       " p1AtkFrame=" + std::to_string(frameAdvState.p1ActionableInternalFrame) +
                       " p2DefFree=" + std::to_string(frameAdvState.p2DefenderFreeInternalFrame) +
                       " p2Atk=" + std::to_string(frameAdvState.p2Attacking) +
                       " p2Calc=" + std::to_string(frameAdvState.p2AdvantageCalculated) +
                       " p2AtkFrame=" + std::to_string(frameAdvState.p2ActionableInternalFrame) +
                       " p1DefFree=" + std::to_string(frameAdvState.p1DefenderFreeInternalFrame) +
                       " p1Def=" + std::to_string(frameAdvState.p1Defending) +
                       " p2Def=" + std::to_string(frameAdvState.p2Defending) +
                       " skipUntil=" + std::to_string(g_SkipRegularFAOverlayUntilFrame.load()) +
                       " curFrame=" + std::to_string(currentInternalFrame), detailedLogging.load());
            }
        }
    }
    if (frameAdvState.p1Attacking && !frameAdvState.p1AdvantageCalculated &&
        frameAdvState.p1ActionableInternalFrame != -1 && frameAdvState.p2DefenderFreeInternalFrame != -1) {
        
        if (g_deepFrameAdvDebug.load()) {
            LogOut("[FA_DIAG] STEP5 CALCULATING P1->P2: atkActionable=" +
                   std::to_string(frameAdvState.p1ActionableInternalFrame) +
                   " defFree=" + std::to_string(frameAdvState.p2DefenderFreeInternalFrame), true);
            LogOut("[FA_CALC] P1 attacking - atkActionable=" + std::to_string(frameAdvState.p1ActionableInternalFrame) + 
                   " defFree=" + std::to_string(frameAdvState.p2DefenderFreeInternalFrame) + 
                   " p1Move=" + std::to_string(moveID1) + " p2Move=" + std::to_string(moveID2), true);
        }
        
        // FA = defenderFree - attackerActionable; positive = attacker advantage.
        int frameAdvantage = frameAdvState.p2DefenderFreeInternalFrame - frameAdvState.p1ActionableInternalFrame;
        // Subtract any freeze that occurred after attacker recovery and before defender free (e.g., IC/BIC/FIC)
        if (p1_freeze_after_atk_actionable > 0) {
            frameAdvantage -= p1_freeze_after_atk_actionable;
        }
        frameAdvState.p1FrameAdvantage = frameAdvantage;
        frameAdvState.p1AdvantageCalculated = true;
        
        QueuePendingRegularFrameAdvantage(1, frameAdvantage, currentInternalFrame, g_faScratch.p2DefenderFreeNeedsDelay);
        ClearGapAnchorForDefender(2);
        
        // Reset exchange state for the next sequence; FA has consumed the defender-free anchor.
        frameAdvState.p1Attacking = false;
        frameAdvState.p2Defending = false;
        frameAdvState.p2InBlockstun = false;
        frameAdvState.p2InHitstun = false;
        frameAdvState.p1AttackStartInternalFrame = -1;
        frameAdvState.p1ActionableInternalFrame = -1;
    }
    
    if (frameAdvState.p2Attacking && !frameAdvState.p2AdvantageCalculated &&
        frameAdvState.p2ActionableInternalFrame != -1 && frameAdvState.p1DefenderFreeInternalFrame != -1) {
        
        #if defined(ENABLE_FRAME_ADV_DEBUG)
        LogOut("[FA_CALC] P2 attacking - atkActionable=" + std::to_string(frameAdvState.p2ActionableInternalFrame) + 
               " defFree=" + std::to_string(frameAdvState.p1DefenderFreeInternalFrame) + 
               " p1Move=" + std::to_string(moveID1) + " p2Move=" + std::to_string(moveID2), true);
        #endif
        
        int frameAdvantage = frameAdvState.p1DefenderFreeInternalFrame - frameAdvState.p2ActionableInternalFrame;
        if (p2_freeze_after_atk_actionable > 0) {
            frameAdvantage -= p2_freeze_after_atk_actionable;
        }
        frameAdvState.p2FrameAdvantage = frameAdvantage;
        frameAdvState.p2AdvantageCalculated = true;
        
        QueuePendingRegularFrameAdvantage(2, frameAdvantage, currentInternalFrame, g_faScratch.p1DefenderFreeNeedsDelay);
        ClearGapAnchorForDefender(1);
        
        // Reset exchange state for the next sequence; FA has consumed the defender-free anchor.
        frameAdvState.p2Attacking = false;
        frameAdvState.p1Defending = false;
        frameAdvState.p1InBlockstun = false;
        frameAdvState.p1InHitstun = false;
        frameAdvState.p2AttackStartInternalFrame = -1;
        frameAdvState.p2ActionableInternalFrame = -1;
    }
    
    auto& staleFrameCounter = g_faRuntime.staleFrameCounter;
    bool hasActiveTracking = (frameAdvState.p1Attacking && !frameAdvState.p1AdvantageCalculated) ||
                             (frameAdvState.p2Attacking && !frameAdvState.p2AdvantageCalculated);

    auto stillWaitingForTimings = [&](short curP1, short curP2) -> bool {
        bool waitingP1Atk = frameAdvState.p1Attacking && frameAdvState.p1ActionableInternalFrame == -1 && !IsActionable(curP1);
        bool waitingP2Atk = frameAdvState.p2Attacking && frameAdvState.p2ActionableInternalFrame == -1 && !IsActionable(curP2);
        bool waitingP1Def = frameAdvState.p1Defending && frameAdvState.p1DefenderFreeInternalFrame == -1 && !IsActionable(curP1);
        bool waitingP2Def = frameAdvState.p2Defending && frameAdvState.p2DefenderFreeInternalFrame == -1 && !IsActionable(curP2);
        return waitingP1Atk || waitingP2Atk || waitingP1Def || waitingP2Def;
    };

    if (hasActiveTracking) {
        // Pause the stale counter during any non-actionable lockout we're explicitly waiting to resolve
        if (stillWaitingForTimings(moveID1, moveID2)) {
            staleFrameCounter = 0;
        } else {
            staleFrameCounter++;
            if (g_deepFrameAdvDebug.load() && (staleFrameCounter % 192) == 0) {
                LogOut("[FA_DIAG] STALE ticking: count=" + std::to_string(staleFrameCounter) +
                       " p1Atk=" + std::to_string(frameAdvState.p1Attacking) +
                       " p1Calc=" + std::to_string(frameAdvState.p1AdvantageCalculated) +
                       " p1AtkFrame=" + std::to_string(frameAdvState.p1ActionableInternalFrame) +
                       " p2DefFree=" + std::to_string(frameAdvState.p2DefenderFreeInternalFrame) +
                       " p2Atk=" + std::to_string(frameAdvState.p2Attacking) +
                       " p2Calc=" + std::to_string(frameAdvState.p2AdvantageCalculated) +
                       " p2AtkFrame=" + std::to_string(frameAdvState.p2ActionableInternalFrame) +
                       " p1DefFree=" + std::to_string(frameAdvState.p1DefenderFreeInternalFrame), true);
            }
        }
        // If we've been tracking without progress for more than 20 seconds, reset to avoid getting stuck.
        if (staleFrameCounter > kStaleResetInternalFrames) {
            LogOut("[FRAME_ADV] Stale state detected (no progress), resetting", true);
            ResetFrameAdvantageState();
            staleFrameCounter = 0;
            p1_last_defender_free_frame = -1;
            p2_last_defender_free_frame = -1;
            p1_freeze_after_atk_actionable = 0;
            p2_freeze_after_atk_actionable = 0;
        }
    } else {
        staleFrameCounter = 0;
    }

    FlushPendingRegularFrameAdvantage(currentInternalFrame, currentTimeMs);
}

bool IsFrameAdvantageActive() {
    return frameAdvState.p1Attacking || frameAdvState.p2Attacking ||
           frameAdvState.p1Defending || frameAdvState.p2Defending;
}

FrameAdvantageState GetFrameAdvantageState() {
    return frameAdvState;
}
