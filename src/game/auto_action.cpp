#include "../include/game/auto_action.h"
#include "../include/core/constants.h"
#include "../include/utils/utilities.h"
#include "../include/utils/network.h"
#include "../include/game/game_state.h"
#include "../include/game/frame_advantage.h"
#include "../include/core/memory.h"
#include "../include/core/logger.h"
#include "../include/input/input_core.h"
#include "../include/input/input_hook.h"
#include "../include/input/auto_action_motion_transaction.h"
#include "../include/input/motion_system.h"     
#include "../include/game/auto_action_helpers.h"
#include "../include/input/motion_constants.h"  
#include "../include/input/input_motion.h"      
#include "../include/input/input_freeze.h"
#include "../include/input/input_buffer.h"
#include "../include/game/attack_reader.h"
#include "../include/input/immediate_input.h"
#include "../include/input/injection_control.h"
#include "../include/input/scoped_input_reservation.h"
#include "../include/game/fm_commands.h"
#include "../include/game/macro_controller.h"
#include "../include/game/mission/mission_engine.h"
#include "../include/game/mission/tutorial_episode_policy.h"
#include "../include/game/mission/tutorial_session.h"
#include "../include/game/character_settings.h"
#include "../include/game/character_action_catalog.h"
#include "../include/game/auto_action_charge.h"
#include "../include/game/auto_action_charge_policy.h"
#include "../include/game/kaori_recoil_duck.h"
#include "../include/game/frame_analysis.h"
#include "../include/game/per_frame_sample.h"
#include "../include/game/validation_metrics.h"
#include "../include/game/validation_metrics.h"
#include "../include/gui/overlay.h"
#include "../include/utils/switch_players.h"

bool IsThrown(short moveID);
static inline bool HasEnabledRows(int triggerType);
static inline bool PickRandomRow(int triggerType, int playerNum,
                                 TriggerOption &out);
static inline bool PickRandomPoolOption(int triggerType, int playerNum, TriggerOption &out);
#include <cmath>
#include <algorithm>
#include <vector>
#include <sstream>
#include <mutex>

#ifndef EFZ_ENABLE_AUTO_ACTION_WAKE_TRACE
#define EFZ_ENABLE_AUTO_ACTION_WAKE_TRACE 0
#endif

#ifndef MOTION_INPUT_UP
#define MOTION_INPUT_UP INPUT_UP
#endif

#ifndef MOTION_INPUT_LEFT
#define MOTION_INPUT_LEFT INPUT_LEFT
#endif

#ifndef ACTION_BACK_DASH
#define ACTION_BACK_DASH ACTION_BACKDASH
#endif

AutoActionApplyResult ApplyAutoAction(int playerNum, uintptr_t moveIDAddr,
                                      short currentMoveID, short prevMoveID,
                                      uint64_t* motionGenerationOut,
                                      uint64_t* normalGenerationOut,
                                      uint64_t* recipeGenerationOut);
static int ResolveButtonMaskForAction(int actionType, int triggerType, int strengthOverride = -1);
static bool ExecuteWakeSpecialNow(int playerNum, int actionType,
                                  short currentMoveID,
                                  int strengthOverride = -1,
                                  uint64_t* generationOut = nullptr);
static bool ExecuteWakeSpecialWithChargeWatcher(
    int playerNum, int actionType, short currentMoveID,
    int strengthOverride, int chargeFollowup,
    uint64_t* generationOut, uint64_t* chargeReservationOut);
static bool SupportsImmediateWakeHold(int actionType);
static bool BuildImmediateWakeHoldMask(int playerNum, int actionType, uint8_t &outMask);
static bool IssueWakeImmediateHold(int playerNum, int actionType);
static constexpr int kInternalTicksPerVisualFrame = 3;
static constexpr int kMaxAutoActionDispatchAttempts = 3;

static constexpr bool IsStrengthButtonAction(int actionType) {
    switch (actionType) {
        case ACTION_QCF:
        case ACTION_DP:
        case ACTION_QCB:
        case ACTION_421:
        case ACTION_SUPER1:
        case ACTION_SUPER2:
        case ACTION_236236:
        case ACTION_214214:
        case ACTION_641236:
        case ACTION_463214:
        case ACTION_412:
        case ACTION_22:
        case ACTION_4123641236:
        case ACTION_6321463214:
        case ACTION_1X:
        case ACTION_3X:
        case ACTION_J2X:
        case ACTION_J6X:
        case ACTION_66X:
        case ACTION_662X:
        case ACTION_664X:
            return true;
        default:
            return false;
    }
}

static constexpr bool IsDashRecipeAction(int actionType) {
    return actionType == ACTION_FORWARD_DASH || actionType == ACTION_BACKDASH ||
           actionType == ACTION_BACK_DASH || actionType == ACTION_66X ||
           actionType == ACTION_662X || actionType == ACTION_664X ||
           actionType == ACTION_KAORI_RECOIL_DUCK;
}

static constexpr bool ActionSupportsChargeFollowup(int actionType) {
    if (IsDashRecipeAction(actionType) || actionType == ACTION_FINAL_MEMORY ||
        actionType == ACTION_JUMP || actionType == ACTION_BLOCK ||
        actionType < 0) {
        return false;
    }
    if (actionType >= ACTION_5A && actionType <= ACTION_4D) return true;
    if (actionType == ACTION_1X || actionType == ACTION_3X ||
        actionType == ACTION_J2X || actionType == ACTION_J6X) {
        return true;
    }
    return IsStrengthButtonAction(actionType);
}

static constexpr int CatalogStrengthForAction(int actionType, int configured) {
    if (actionType >= ACTION_5A && actionType <= ACTION_4D) {
        return actionType & 3;
    }
    return configured;
}

static inline int VisualFramesToInternalTicks(int visualFrames) {
    return (visualFrames <= 0) ? 0 : (visualFrames * kInternalTicksPerVisualFrame);
}

static inline int InternalTicksToVisualFramesCeil(int internalTicks) {
    return (internalTicks <= 0) ? 0 : ((internalTicks + kInternalTicksPerVisualFrame - 1) / kInternalTicksPerVisualFrame);
}

static int s_p1WakeJumpTrackFrame = -1;
static int s_p2WakeJumpTrackFrame = -1;

static inline bool IsBufferableAction(short moveID) {
    if (IsAttackMove(moveID)) return true;
    
    if (moveID == WALK_FWD_ID || moveID == WALK_BACK_ID) return true;
    
    if (moveID == FORWARD_DASH_START_ID || moveID == GROUND_BACKWARD_DASH_ID ||
        moveID == FORWARD_DASH_RECOVERY_SENTINEL_ID ||
        moveID == AIR_FORWARD_DASH_ID || moveID == AIR_BACKWARD_DASH_ID ||
        moveID == KAORI_FORWARD_DASH_START_ID) {
        return true;
    }
    
    if (moveID == STRAIGHT_JUMP_ID || moveID == FORWARD_JUMP_ID || moveID == BACKWARD_JUMP_ID) {
        return true;
    }
    
    if (moveID == CROUCH_ID) return true;
    
    return false;
}

// Rising duration in move 96 per character, in visual frames (64 Hz).
// Multiply by 3 for logical ticks (192 Hz).
struct WakeupTiming {
    int charID;
    int risingFramesVisual;
};

static const WakeupTiming s_wakeupTimings[] = {
    { CHAR_ID_MIZUKA,        13 }, // Mizuka Nagamori (resource "nagamori")
    { CHAR_ID_UNKNOWN_BOSS,  13 },
    { CHAR_ID_MAKOTO,    16 },
    { CHAR_ID_MINAGI,    16 },
    { CHAR_ID_EXNANASE,  16 }, 
    { CHAR_ID_NANASE,    16 }, 
    { CHAR_ID_AKANE,     17 },
    { CHAR_ID_NAYUKI,    17 }, 
    { CHAR_ID_MISUZU,    19 },
    { CHAR_ID_UNKNOWN,   19 },
    { CHAR_ID_IKUMI,     20 },
    { CHAR_ID_KAORI,     20 },
    { CHAR_ID_SAYURI,    20 },
    { CHAR_ID_SHIORI,    20 },
    { CHAR_ID_AYU,       21 },
    { CHAR_ID_MISAKI,    21 },
    { CHAR_ID_MISHIO,    21 },
    { CHAR_ID_MAI,       23 },  
    { CHAR_ID_AKIKO,     26 },
    { CHAR_ID_MIO,       26 },  
    { CHAR_ID_NAYUKIB,   27 },  
    { CHAR_ID_KANO,      29 },
    { CHAR_ID_KANNA,     31 },
    { CHAR_ID_MAYU,      39 }, 
};

static int GetWakeupRisingFrames(int charID) {
    for (const auto& timing : s_wakeupTimings) {
        if (timing.charID == charID) {
            return timing.risingFramesVisual;
        }
    }
    return 20;
}

static int GetWakeupRisingTicks(int charID) {
    for (const auto& timing : s_wakeupTimings) {
        if (timing.charID == charID) {
            return VisualFramesToInternalTicks(timing.risingFramesVisual);
        }
    }
    return VisualFramesToInternalTicks(20);
}

TriggerDelayState p1DelayState = {false, 0, TRIGGER_NONE, 0, -1, -1, 0, -1, -1, 0, 0, 0};
TriggerDelayState p2DelayState = {false, 0, TRIGGER_NONE, 0, -1, -1, 0, -1, -1, 0, 0, 0};
bool p1ActionApplied = false;
bool p2ActionApplied = false;

std::atomic<int> g_lastActiveTriggerType(TRIGGER_NONE);
std::atomic<int> g_lastActiveTriggerFrame(0);

std::atomic<bool> g_triggersCancelledActive(false);
std::atomic<int> g_triggersCancelledFrame(0);

struct AutoActionLogScope {
    AutoActionLogScope(const char* phaseLabel, int playerNum, int triggerType)
        : player(playerNum), trigger(triggerType), phase(phaseLabel ? phaseLabel : "AutoAction") {
        LogOut("=================", true);
        LogOut("[AUTO-ACTION] " + phase + " begin (player=" + std::to_string(player) +
               ", trigger=" + std::to_string(trigger) + ")", true);
    }
    ~AutoActionLogScope() {
        LogOut("[AUTO-ACTION] " + phase + " end (player=" + std::to_string(player) +
               ", trigger=" + std::to_string(trigger) + ")", true);
        LogOut("=================", true);
    }
    int player;
    int trigger;
    std::string phase;
};

struct TriggerFlowTracker {
    bool active = false;
    int player = 0;
    int startFrame = 0;
    short startMoveID = -1;
    short lastMoveID = -1;
    const char* reason = "Unknown";
};

static TriggerFlowTracker g_flowTrackers[3];

static inline TriggerFlowTracker& GetFlowTracker(int playerNum) {
    return g_flowTrackers[(playerNum == 2) ? 2 : 1];
}

static const char* TriggerTypeLabel(int triggerType) {
    switch (triggerType) {
        case TRIGGER_AFTER_BLOCK: return "AfterBlock";
        case TRIGGER_ON_WAKEUP: return "OnWakeup";
        case TRIGGER_AFTER_HITSTUN: return "AfterHitstun";
        case TRIGGER_AFTER_AIRTECH: return "AfterAirtech";
        case TRIGGER_ON_RG: return "OnRG";
        default: return "Unknown";
    }
}

static void BeginFlowSequence(int playerNum, const char* reason, short prevMoveID, short currMoveID) {
    TriggerFlowTracker& tracker = GetFlowTracker(playerNum);
    if (tracker.active) return;
    tracker.active = true;
    tracker.player = playerNum;
    tracker.startFrame = frameCounter.load();
    tracker.startMoveID = currMoveID;
    tracker.lastMoveID = currMoveID;
    tracker.reason = reason ? reason : "Unknown";
    (void)prevMoveID;
}

static void LogFlowSequenceEvent(int playerNum, const char* label, short prevMoveID, short currMoveID) {
    TriggerFlowTracker& tracker = GetFlowTracker(playerNum);
    if (!tracker.active) return;
    tracker.lastMoveID = currMoveID;
    (void)label;
    (void)prevMoveID;
}

static void EndFlowSequence(int playerNum, const char* reason, short currMoveID) {
    TriggerFlowTracker& tracker = GetFlowTracker(playerNum);
    if (!tracker.active) return;
    (void)reason;
    (void)currMoveID;
    tracker.active = false;
    tracker.reason = "Unknown";
    tracker.startFrame = 0;
    tracker.startMoveID = -1;
    tracker.lastMoveID = -1;
}

static void MaybeBeginFlowSequence(int playerNum, short prevMoveID, short currMoveID) {
    TriggerFlowTracker& tracker = GetFlowTracker(playerNum);
    if (tracker.active) return;
    bool enterGround = !IsGroundtech(prevMoveID) && IsGroundtech(currMoveID);
    bool enterLaunch = !IsLaunched(prevMoveID) && IsLaunched(currMoveID);
    bool enterAirtech = !IsAirtech(prevMoveID) && IsAirtech(currMoveID);
    const char* reason = nullptr;
    if (enterGround) reason = "EnteredGroundtech";
    else if (enterLaunch) reason = "Launched";
    else if (enterAirtech) reason = "Airtech";
    if (reason) {
        BeginFlowSequence(playerNum, reason, prevMoveID, currMoveID);
    }
}

static void MaybeAutoCloseFlowSequence(int playerNum, short prevMoveID, short currMoveID, bool wakePrearmed, bool restorePending) {
    TriggerFlowTracker& tracker = GetFlowTracker(playerNum);
    if (!tracker.active) return;
    bool stillInRecovery = IsGroundtech(currMoveID) || IsAirtech(currMoveID) || IsLaunched(currMoveID);
    if (!stillInRecovery && !restorePending && !wakePrearmed) {
        EndFlowSequence(playerNum, "SequenceAutoClosed", currMoveID);
    } else if (tracker.lastMoveID != currMoveID) {
        tracker.lastMoveID = currMoveID;
    }
    (void)prevMoveID;
}

static bool p1TriggerActive = false;
static bool p2TriggerActive = false;
static int p1TriggerCooldown = 0;
static int p2TriggerCooldown = 0;
static constexpr int TRIGGER_COOLDOWN_FRAMES = 6;
static constexpr int RESTORE_GRACE_PERIOD = 0;
static int g_restoreGraceCounter = 0;
std::atomic<bool> g_p2ControlOverridden{false};
uint32_t g_originalP2ControlFlag = 1; // 1 = AI control
std::recursive_mutex g_p2ControlMutex;

namespace {
// Serializes the tick producer, the legacy frame producer, and cancel/reset entry points.
// Recursive because the tick entry calls the public processing helpers.
std::recursive_mutex g_autoActionStateMutex;
std::atomic<uint64_t> g_tutorialP2Token{0};
std::atomic<uint64_t> g_nextTutorialP2Token{1};
std::atomic<bool> g_tutorialP2ReleaseRequested{false};
struct TutorialP2Snapshot {
    uintptr_t gameState = 0;
    uintptr_t character = 0;
    uint8_t cpuFlag = 1;
    uint32_t aiFlag = 1;
};
TutorialP2Snapshot g_tutorialP2Snapshot;

struct LegacyP2ControlSnapshot {
    bool valid = false;
    uintptr_t gameState = 0;
    uintptr_t character = 0;
    uint8_t cpuFlag = 1;
    uint32_t aiFlag = 1;
};
LegacyP2ControlSnapshot g_legacyP2ControlSnapshot;

struct TutorialP2WakeSnapshot {
    bool enabled = false;
    int player = 1;
    bool wake = false;
    int action = 0;
    int strength = 0;
    int delay = 0;
    int macroSlot = 0;
    int charge = 0;
    bool usePool = false;
    bool afterBlock = false;
    bool afterHitstun = false;
    bool afterAirtech = false;
    bool onRG = false;
    bool randomize = false;
    bool wakeBuffering = false;
};
TutorialP2WakeSnapshot g_tutorialP2WakeSnapshot;
std::atomic<uint64_t> g_tutorialP2WakeToken{0};
std::atomic<uint64_t> g_nextTutorialP2WakeToken{1};
int g_tutorialP2WakeAppliedAction = 0;
int g_tutorialP2WakeAppliedStrength = 0;
// Indexed by playerNum (1..2); index 0 is unused. Serves whichever side is the dummy.
uint64_t s_wakeForcedNeutralToken[3] = {0, 0, 0};
uint64_t s_wakeMacroCleanupToken[3] = {0, 0, 0};

static inline bool IsWakeLeasePlayer(int playerNum) {
    return playerNum == 1 || playerNum == 2;
}

bool TryPublishWakeForcedNeutral(int playerNum) {
    if (!IsWakeLeasePlayer(playerNum)) return false;
    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    if (ScopedInputReservationMatches(
            playerNum, ScopedInputOwner::WakeForcedNeutral,
            s_wakeForcedNeutralToken[playerNum])) {
        return true;
    }
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
        (g_bufferFreezingActive.load(std::memory_order_acquire) &&
         (g_activeFreezePlayer.load(std::memory_order_acquire) == 0 ||
          g_activeFreezePlayer.load(std::memory_order_acquire) == playerNum)) ||
        g_manualInputOverride[playerNum].load(std::memory_order_acquire) ||
        ImmediateInput::GetCurrentDesired(playerNum) != 0 ||
        ImmediateInput::GetRemainingTicks(playerNum) != 0) {
        return false;
    }

    uint64_t token = 0;
    if (!TryReserveScopedInput(
            playerNum, ScopedInputOwner::WakeForcedNeutral, token)) {
        return false;
    }

    g_manualInputMask[playerNum].store(0, std::memory_order_release);
    g_injectImmediateOnly[playerNum].store(true, std::memory_order_release);
    g_manualInputOverride[playerNum].store(true, std::memory_order_release);
    s_wakeForcedNeutralToken[playerNum] = token;
    return true;
}

void ReleaseWakeForcedNeutral(int playerNum) {
    if (!IsWakeLeasePlayer(playerNum)) return;
    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    const uint64_t token = s_wakeForcedNeutralToken[playerNum];
    if (!ScopedInputReservationMatches(
            playerNum, ScopedInputOwner::WakeForcedNeutral, token)) {
        s_wakeForcedNeutralToken[playerNum] = 0;
        return;
    }
    const bool stillOwnsExactValue =
        g_manualInputOverride[playerNum].load(std::memory_order_acquire) &&
        g_manualInputMask[playerNum].load(std::memory_order_acquire) == 0 &&
        g_injectImmediateOnly[playerNum].load(std::memory_order_acquire);
    if (stillOwnsExactValue) {
        g_manualInputOverride[playerNum].store(false, std::memory_order_release);
        g_manualInputMask[playerNum].store(0, std::memory_order_release);
        g_injectImmediateOnly[playerNum].store(false, std::memory_order_release);
    }
    ReleaseScopedInputReservation(
        playerNum, ScopedInputOwner::WakeForcedNeutral, token);
    s_wakeForcedNeutralToken[playerNum] = 0;
}

// The wake-macro epilogue clears the shared command token and input ring, so it owns that lane
// for its whole lifetime. If another producer is active, stay armed and retry on a later tick.
bool TryBeginWakeMacroCleanup(int playerNum) {
    if (!IsWakeLeasePlayer(playerNum)) return false;
    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    if (ScopedInputReservationMatches(
            playerNum, ScopedInputOwner::WakeMacroCleanup,
            s_wakeMacroCleanupToken[playerNum])) {
        return true;
    }

    const MotionQueueSnapshot queue = GetMotionQueueSnapshot(playerNum);
    if (g_onlineModeActive.load(std::memory_order_acquire) ||
        IsScopedInputReserved(playerNum) ||
        IsAutoActionMotionTransactionActive(playerNum) ||
        IsAutoActionNormalPulseActive(playerNum) ||
        queue.active ||
        ImmediateInput::TutorialLeaseActive(playerNum) ||
        TutorialMotionQueueLeaseActive(playerNum) ||
        TutorialBufferFreezeLeaseActive() ||
        g_pollOverrideActive[playerNum].load(std::memory_order_acquire) ||
        g_manualInputOverride[playerNum].load(std::memory_order_acquire) ||
        g_injectImmediateOnly[playerNum].load(std::memory_order_acquire) ||
        g_forceBypass[playerNum].load(std::memory_order_acquire) ||
        ImmediateInput::GetCurrentDesired(playerNum) != 0 ||
        ImmediateInput::GetRemainingTicks(playerNum) != 0 ||
        (g_bufferFreezingActive.load(std::memory_order_acquire) &&
         (g_activeFreezePlayer.load(std::memory_order_acquire) == 0 ||
          g_activeFreezePlayer.load(std::memory_order_acquire) == playerNum)) ||
        (MacroController::IsExclusivePlayback() &&
         MacroController::GetPlaybackPlayer() == playerNum)) {
        return false;
    }

    uint64_t token = 0;
    if (!TryReserveScopedInput(
            playerNum, ScopedInputOwner::WakeMacroCleanup, token)) {
        return false;
    }
    s_wakeMacroCleanupToken[playerNum] = token;
    return true;
}

void EndWakeMacroCleanup(int playerNum) {
    if (!IsWakeLeasePlayer(playerNum)) return;
    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    const uint64_t token = s_wakeMacroCleanupToken[playerNum];
    if (ScopedInputReservationMatches(
            playerNum, ScopedInputOwner::WakeMacroCleanup, token)) {
        ReleaseScopedInputReservation(
            playerNum, ScopedInputOwner::WakeMacroCleanup, token);
    }
    s_wakeMacroCleanupToken[playerNum] = 0;
}
}

struct DashFollowDeferred {
    std::atomic<int> pendingSel{0};
    std::atomic<int> player{0};
    std::atomic<int> dashStartLatched{0}; // 0 = not yet, 1 = latched and fired, -1 = latched but failed to fire
    std::atomic<int> isBack{0};
    std::atomic<uint64_t> motionGeneration{0};
    std::atomic<uint64_t> normalGeneration{0};
    std::atomic<int> normalDispatchAttempts{0};
    std::atomic<int> expiryFrame{0};
};
static DashFollowDeferred g_dashDeferred;
static void ResetDeferredDashFollowupState();

// Character ID caching: cached once per MATCH phase, invalidated when leaving
// match or clearing triggers. Sentinel -999 = not yet cached; -1 = cached but unknown.
static int g_cachedP1CharID = -999;
static int g_cachedP2CharID = -999;
static bool g_loggedCharInvalidation = false;
static uint32_t g_characterCacheGeneration = 0;

static void InvalidateCachedCharacterIDs(const char* reason) {
    const uint32_t generation = GetRuntimeLifecycleGeneration();
    if ((g_cachedP1CharID != -999 || g_cachedP2CharID != -999) && !g_loggedCharInvalidation) {
        LogOut(std::string("[AUTO-ACTION][CHAR] Invalidating cached character IDs gen=")
            + std::to_string(generation) + " (" + reason + ")", detailedLogging.load());
        g_loggedCharInvalidation = true;
    }
    g_cachedP1CharID = -999;
    g_cachedP2CharID = -999;
    g_characterCacheGeneration = generation;
}

void InvalidateAutoActionCharacterCaches(const char* reason) {
    InvalidateCachedCharacterIDs(reason);
}

static void EnsureCachedCharacterIDs(uintptr_t base) {
    const uint32_t lifecycleGeneration = GetRuntimeLifecycleGeneration();
    if (g_characterCacheGeneration != lifecycleGeneration) {
        const bool hadCachedValues = (g_cachedP1CharID != -999 || g_cachedP2CharID != -999);
        if (hadCachedValues || detailedLogging.load()) {
            LogOut(std::string("[AUTO-ACTION][CHAR] Lifecycle generation changed ")
                + std::to_string(g_characterCacheGeneration) + "->" + std::to_string(lifecycleGeneration),
                true);
        }
        InvalidateCachedCharacterIDs("lifecycle generation change");
    }

    if (g_cachedP1CharID == -999) {
        char nameBuf1[16] = {0};
        uintptr_t nameAddr1 = ResolvePointer(base, EFZ_BASE_OFFSET_P1, CHARACTER_NAME_OFFSET);
        if (nameAddr1) SafeReadMemory(nameAddr1, &nameBuf1, sizeof(nameBuf1)-1);
        g_cachedP1CharID = CharacterSettings::GetCharacterID(std::string(nameBuf1));
        if (g_cachedP1CharID < 0) g_cachedP1CharID = -1;
        g_characterCacheGeneration = lifecycleGeneration;
        LogOut(std::string("[AUTO-ACTION][CHAR] Cached P1 gen=") + std::to_string(g_characterCacheGeneration)
            + " char name='" + nameBuf1 + "' id=" + std::to_string(g_cachedP1CharID), detailedLogging.load());
        g_loggedCharInvalidation = false;
    }
    if (g_cachedP2CharID == -999) {
        char nameBuf2[16] = {0};
        uintptr_t nameAddr2 = ResolvePointer(base, EFZ_BASE_OFFSET_P2, CHARACTER_NAME_OFFSET);
        if (nameAddr2) SafeReadMemory(nameAddr2, &nameBuf2, sizeof(nameBuf2)-1);
        g_cachedP2CharID = CharacterSettings::GetCharacterID(std::string(nameBuf2));
        if (g_cachedP2CharID < 0) g_cachedP2CharID = -1;
        g_characterCacheGeneration = lifecycleGeneration;
        LogOut(std::string("[AUTO-ACTION][CHAR] Cached P2 gen=") + std::to_string(g_characterCacheGeneration)
            + " char name='" + nameBuf2 + "' id=" + std::to_string(g_cachedP2CharID), detailedLogging.load());
        g_loggedCharInvalidation = false;
    }
}

static void ScheduleForwardDashFollowup(int playerNum, int sel,
                                        bool,
                                        uint64_t motionGeneration = 0) {
    if (g_dashDeferred.pendingSel.load() > 0) {
        LogOut(std::string("[AUTO-ACTION] Forward dash follow-up schedule ignored; pending sel=") + std::to_string(g_dashDeferred.pendingSel.load()), true);
        return;
    }
    g_dashDeferred.pendingSel.store(sel);
    g_dashDeferred.player.store(playerNum);
    g_dashDeferred.dashStartLatched.store(0);
    g_dashDeferred.isBack.store(0);
    g_dashDeferred.motionGeneration.store(motionGeneration);
    g_dashDeferred.normalGeneration.store(0);
    g_dashDeferred.normalDispatchAttempts.store(0);
    g_dashDeferred.expiryFrame.store(frameCounter.load() + 180);
    LogOut(std::string("[AUTO-ACTION] Forward dash follow-up armed (sel=") + std::to_string(sel) + ")", true);
}

static void ScheduleBackDashFollowup(int playerNum, int sel,
                                     uint64_t motionGeneration = 0) {
    if (g_dashDeferred.pendingSel.load() > 0) {
        LogOut(std::string("[AUTO-ACTION] Backdash follow-up schedule ignored; pending sel=") + std::to_string(g_dashDeferred.pendingSel.load()), true);
        return;
    }
    g_dashDeferred.pendingSel.store(sel);
    g_dashDeferred.player.store(playerNum);
    g_dashDeferred.dashStartLatched.store(0);
    g_dashDeferred.isBack.store(1);
    g_dashDeferred.motionGeneration.store(motionGeneration);
    g_dashDeferred.normalGeneration.store(0);
    g_dashDeferred.normalDispatchAttempts.store(0);
    g_dashDeferred.expiryFrame.store(frameCounter.load() + 180);
    LogOut(std::string("[AUTO-ACTION] Backdash follow-up armed (sel=") + std::to_string(sel) + ")", true);
}

static void ProcessDeferredDashFollowups(short curP1, short prevP1, short curP2, short prevP2) {
    int sel = g_dashDeferred.pendingSel.load();
    if (sel <= 0) return;
    int targetP = g_dashDeferred.player.load();
    const uint64_t motionGeneration =
        g_dashDeferred.motionGeneration.load(std::memory_order_acquire);
    const uint64_t normalGeneration =
        g_dashDeferred.normalGeneration.load(std::memory_order_acquire);
    const bool expired =
        frameCounter.load() > g_dashDeferred.expiryFrame.load();
    if (expired) {
        if (motionGeneration != 0 &&
            GetAutoActionMotionTransactionOutcome(targetP, motionGeneration) ==
                AutoActionMotionOutcome::Pending) {
            CancelAutoActionMotionTransaction(
                targetP,
                "dash follow-up transaction expired before producer/consumer");
        }
        if (normalGeneration != 0 &&
            GetAutoActionNormalPulseOutcome(targetP, normalGeneration) ==
                AutoActionNormalPulseOutcome::Pending) {
            CancelAutoActionNormalPulse(targetP);
        }
        LogOut("[AUTO-ACTION] Dash follow-up expired before a matching dash start", true);
        ResetDeferredDashFollowupState();
        return;
    }
    if (motionGeneration != 0) {
        const AutoActionMotionOutcome outcome =
            GetAutoActionMotionTransactionOutcome(targetP, motionGeneration);
        if (outcome == AutoActionMotionOutcome::Pending) return;
        if (outcome != AutoActionMotionOutcome::Accepted) {
            LogOut("[AUTO-ACTION] Dash follow-up cancelled with motion generation=" +
                       std::to_string(motionGeneration),
                   true);
            ResetDeferredDashFollowupState();
            return;
        }
    }
    EnsureCachedCharacterIDs(GetEFZBase());
    short prev = (targetP == 1) ? prevP1 : prevP2;
    short curr = (targetP == 1) ? curP1  : curP2;
    bool back = (g_dashDeferred.isBack.load() != 0);
    const bool targIsKaori = targetP == 1
        ? (g_cachedP1CharID == CHAR_ID_KAORI)
        : (g_cachedP2CharID == CHAR_ID_KAORI);
    const bool forwardDashStart = !back &&
        (curr == GROUND_FORWARD_DASH_ID || curr == AIR_FORWARD_DASH_ID ||
         (targIsKaori && curr == KAORI_FORWARD_DASH_START_ID));
    const bool backDashStart = back &&
        (curr == GROUND_BACKWARD_DASH_ID || curr == AIR_BACKWARD_DASH_ID);

    if (normalGeneration != 0) {
        const AutoActionNormalPulseOutcome outcome =
            GetAutoActionNormalPulseOutcome(targetP, normalGeneration);
        if (outcome == AutoActionNormalPulseOutcome::Pending) return;
        if (outcome == AutoActionNormalPulseOutcome::Accepted) {
            LogOut("[AUTO-ACTION] Dash-normal gen=" +
                       std::to_string(normalGeneration) +
                       " was consumed by EFZ",
                   true);
            if (targetP == 2) {
                if (p2TriggerCooldown < 6) p2TriggerCooldown = 6;
            } else if (p1TriggerCooldown < 6) {
                p1TriggerCooldown = 6;
            }
            ResetDeferredDashFollowupState();
            return;
        }

        const int attempts =
            g_dashDeferred.normalDispatchAttempts.load(
                std::memory_order_acquire);
        g_dashDeferred.normalGeneration.store(0, std::memory_order_release);
        g_dashDeferred.dashStartLatched.store(0, std::memory_order_release);
        if (attempts >= kMaxAutoActionDispatchAttempts ||
            (!forwardDashStart && !backDashStart)) {
            LogOut("[AUTO-ACTION] Dash-normal was not consumed and its retry window closed",
                   true);
            ResetDeferredDashFollowupState();
            return;
        }
        LogOut("[AUTO-ACTION] Dash-normal was not consumed; retrying the exact selection while dash start is active",
               true);
    }
    
    static short lastLoggedP1 = -999;
    static short lastLoggedP2 = -999;
        if (targetP == 1 && curr != lastLoggedP1) {
        bool targIsKaori = (g_cachedP1CharID == CHAR_ID_KAORI);
        const std::string waitId = back ? "164/166" :
            (targIsKaori ? "163/165/250" : "163/165");
            if (detailedLogging.load()) {
                LogOut("[DASH_DEBUG] P1 MoveID: " + std::to_string(prev) + " -> " + std::to_string(curr) + 
                       std::string(" (waiting for ") + waitId + ")", true);
            }
        lastLoggedP1 = curr;
            if ((back && (curr == GROUND_BACKWARD_DASH_ID || curr == AIR_BACKWARD_DASH_ID)) ||
                (!back && (curr == GROUND_FORWARD_DASH_ID || curr == AIR_FORWARD_DASH_ID ||
                           (targIsKaori && curr == KAORI_FORWARD_DASH_START_ID)))) {
                if (detailedLogging.load()) {
                    DumpInputBuffer(1, "DASH_START_DETECTED");
                }
        }
    }
        if (targetP == 2 && curr != lastLoggedP2) {
        bool targIsKaori = (g_cachedP2CharID == CHAR_ID_KAORI);
        const std::string waitId = back ? "164/166" :
            (targIsKaori ? "163/165/250" : "163/165");
            if (detailedLogging.load()) {
                LogOut("[DASH_DEBUG] P2 MoveID: " + std::to_string(prev) + " -> " + std::to_string(curr) + 
                       std::string(" (waiting for ") + waitId + ")", true);
            }
        lastLoggedP2 = curr;
            if ((back && (curr == GROUND_BACKWARD_DASH_ID || curr == AIR_BACKWARD_DASH_ID)) ||
                (!back && (curr == GROUND_FORWARD_DASH_ID || curr == AIR_FORWARD_DASH_ID ||
                           (targIsKaori && curr == KAORI_FORWARD_DASH_START_ID)))) {
                if (detailedLogging.load()) {
                    DumpInputBuffer(2, "DASH_START_DETECTED");
                }
        }
    }
    
    if (forwardDashStart || backDashStart) {
        const bool airDashStart = curr == AIR_FORWARD_DASH_ID ||
                                  curr == AIR_BACKWARD_DASH_ID;
        NormalInputPolicy::Intent intent{};
        if (sel >= 1 && sel <= 9) {
            const int buttonIndex = (sel - 1) % 3;
            intent.button = buttonIndex == 0 ? GAME_INPUT_A
                          : buttonIndex == 1 ? GAME_INPUT_B
                                             : GAME_INPUT_C;
            intent.airborne = airDashStart;
            if (airDashStart) {
                intent.direction = NormalInputPolicy::RelativeDirection::Neutral;
            } else {
                const bool reverse = !back && sel >= 7;
                const bool low = sel >= 4 && sel <= 6;
                intent.direction = reverse
                    ? NormalInputPolicy::RelativeDirection::Back
                    : low
                        ? (back ? NormalInputPolicy::RelativeDirection::DownBack
                                : NormalInputPolicy::RelativeDirection::DownForward)
                        : (back ? NormalInputPolicy::RelativeDirection::Back
                                : NormalInputPolicy::RelativeDirection::Forward);
            }
        }
        uint64_t generation = 0;
        const auto submit = QueueAutoActionNormalPulse(
            targetP, intent, NormalInputPolicy::Timing::Immediate, &generation);
        if (submit == NormalInputPolicy::SubmitResult::Accepted) {
            LogOut(std::string("[AUTO-ACTION] Dash-normal queued sel=") +
                   std::to_string(sel) + " gen=" + std::to_string(generation) +
                   " on first dash tick", true);
            
            if (targetP == 1 && motionGeneration == 0) {
                ClearPlayerInputBuffer(targetP, true);
                LogOut("[AUTO-ACTION] Cleared legacy P1 dash pattern after dash normal injection", true);
            }
            
            g_dashDeferred.normalGeneration.store(
                generation, std::memory_order_release);
            g_dashDeferred.normalDispatchAttempts.fetch_add(
                1, std::memory_order_acq_rel);
            g_dashDeferred.dashStartLatched.store(2);
        } else if (submit == NormalInputPolicy::SubmitResult::Busy) {
            LogOut("[AUTO-ACTION] Dash follow-up input lane busy; retaining selection inside dash-start window",
                   detailedLogging.load());
        } else {
            g_dashDeferred.dashStartLatched.store(-1);
            LogOut("[AUTO-ACTION] Dash follow-up intent invalid; retiring selection", true);
            ResetDeferredDashFollowupState();
        }
    }
}

std::atomic<bool> g_pendingControlRestore(false);
std::atomic<short> g_lastP2MoveID(-1);
std::atomic<bool> g_crgFastRestore(false);
static std::atomic<bool> g_recentDashQueued(false);
static std::atomic<int> g_recentDashQueuedFrame(0);

static void ResetDeferredDashFollowupState() {
    g_dashDeferred.pendingSel.store(0, std::memory_order_release);
    g_dashDeferred.player.store(0, std::memory_order_release);
    g_dashDeferred.dashStartLatched.store(0, std::memory_order_release);
    g_dashDeferred.isBack.store(0, std::memory_order_release);
    g_dashDeferred.motionGeneration.store(0, std::memory_order_release);
    g_dashDeferred.normalGeneration.store(0, std::memory_order_release);
    g_dashDeferred.normalDispatchAttempts.store(0, std::memory_order_release);
    g_dashDeferred.expiryFrame.store(0, std::memory_order_release);
    g_recentDashQueued.store(false, std::memory_order_release);
    g_recentDashQueuedFrame.store(0, std::memory_order_release);
}
static std::atomic<DWORD> g_pendingRestoreTimestamp(0);

std::atomic<bool> g_macroWakePreserveBuffer(false);
std::atomic<int> g_macroWakePreservePlayer(0);

std::atomic<int> g_wakeMacroPlaybackCompleted(0);
static int s_wakeMacroTokenNeutralizeCountdown[3] = {0, 0, 0};
static short s_lastWakeMoveID[3] = {-1, -1, -1};
static bool s_wakeMoveIDChanged[3] = {false, false, false};

static bool s_p1WakePrearmed = false;
static bool s_p2WakePrearmed = false;
static int  s_p1WakePrearmExpiry = 0;
static int  s_p2WakePrearmExpiry = 0;
static int  s_p1WakePrearmActionType = -1;
static int  s_p2WakePrearmActionType = -1;
static int  s_p1WakePrearmStrength  = -1;
static int  s_p2WakePrearmStrength  = -1;
static int  s_p1WakePrearmCharge = 0;
static int  s_p2WakePrearmCharge = 0;
static bool s_p1WakePrearmIsSpecial = false;
static bool s_p2WakePrearmIsSpecial = false;
static bool s_p1WakePrearmIsMacro = false;
static bool s_p2WakePrearmIsMacro = false;
static bool s_p1WakeActionableWarning = false;
static bool s_p2WakeActionableWarning = false;
// Ticks (192 Hz) in move 96; a wake special buffers on the last rising tick before actionable.
static int s_p1WakeMoveID96FrameCount = 0;
static int s_p2WakeMoveID96FrameCount = 0;
static bool s_p1WakeBufferFrozen = false;
static bool s_p2WakeBufferFrozen = false;
static uint64_t s_p1WakeMotionGeneration = 0;
static uint64_t s_p2WakeMotionGeneration = 0;
static uint64_t s_p1WakeChargeReservation = 0;
static uint64_t s_p2WakeChargeReservation = 0;
static uint64_t s_p1WakeNormalGeneration = 0;
static uint64_t s_p2WakeNormalGeneration = 0;
static int s_p1WakeNormalAttempts = 0;
static int s_p2WakeNormalAttempts = 0;
static bool s_p1WakeHoldPrimed = false;
static bool s_p2WakeHoldPrimed = false;
static bool s_p1WakeHoldIssued = false;
static bool s_p2WakeHoldIssued = false;
static int  s_p1WakeMacro96FrameCount = 0;
static bool s_p1WakeMacroQueued       = false;
static int  s_p1WakeMacroTargetFrame  = -1;
static int  s_p1WakeMacroSlot         = -1;
static int  s_p1WakeMacroStartTick    = 0;
static int  s_p2WakeMacro96FrameCount = 0;
static bool s_p2WakeMacroQueued       = false;
static TriggerOption s_p1WakePrePickedOption = {};
static TriggerOption s_p2WakePrePickedOption = {};
static int  s_p2WakeMacroTargetFrame  = -1;
static int  s_p2WakeMacroSlot         = -1;
static int  s_p2WakeMacroStartFrame   = -1;
static int  s_p2WakeMacroStartTick    = 0;
static bool s_p1WakeOptionPicked = false;
static bool s_p2WakeOptionPicked = false;

static void ResetWakeRuntimeState() {
    ReleaseWakeForcedNeutral(1);
    ReleaseWakeForcedNeutral(2);
    EndWakeMacroCleanup(1);
    EndWakeMacroCleanup(2);
    s_p1WakePrearmed = false; s_p1WakePrearmExpiry = 0; s_p1WakePrearmActionType = -1;
    s_p1WakePrearmStrength = -1; s_p1WakePrearmIsSpecial = false; s_p1WakePrearmIsMacro = false;
    s_p1WakePrearmCharge = 0;
    s_p1WakeActionableWarning = false; s_p1WakeMoveID96FrameCount = 0;
    s_p1WakeBufferFrozen = false; s_p1WakeHoldPrimed = false; s_p1WakeHoldIssued = false;
    s_p2WakePrearmed = false; s_p2WakePrearmExpiry = 0; s_p2WakePrearmActionType = -1;
    s_p2WakePrearmStrength = -1; s_p2WakePrearmIsSpecial = false; s_p2WakePrearmIsMacro = false;
    s_p2WakePrearmCharge = 0;
    s_p2WakeActionableWarning = false; s_p2WakeMoveID96FrameCount = 0;
    s_p2WakeBufferFrozen = false; s_p2WakeHoldPrimed = false; s_p2WakeHoldIssued = false;
    s_p1WakeMotionGeneration = 0; s_p2WakeMotionGeneration = 0;
    s_p1WakeChargeReservation = 0; s_p2WakeChargeReservation = 0;
    s_p1WakeNormalGeneration = 0; s_p2WakeNormalGeneration = 0;
    s_p1WakeNormalAttempts = 0; s_p2WakeNormalAttempts = 0;

    s_p1WakeMacro96FrameCount = 0; s_p1WakeMacroQueued = false;
    s_p1WakeMacroTargetFrame = -1; s_p1WakeMacroSlot = -1; s_p1WakeMacroStartTick = 0;
    s_p2WakeMacro96FrameCount = 0; s_p2WakeMacroQueued = false;
    s_p2WakeMacroTargetFrame = -1; s_p2WakeMacroSlot = -1;
    s_p2WakeMacroStartFrame = -1; s_p2WakeMacroStartTick = 0;
    s_p1WakeOptionPicked = false; s_p1WakePrePickedOption = {};
    s_p2WakeOptionPicked = false; s_p2WakePrePickedOption = {};
    g_macroWakePreserveBuffer.store(false, std::memory_order_release);
    g_macroWakePreservePlayer.store(0, std::memory_order_release);
    g_wakeMacroPlaybackCompleted.store(0, std::memory_order_release);
    for (int p = 1; p <= 2; ++p) {
        s_wakeMacroTokenNeutralizeCountdown[p] = 0;
        s_lastWakeMoveID[p] = -1;
        s_wakeMoveIDChanged[p] = false;
    }
    s_p1WakeJumpTrackFrame = -1;
    s_p2WakeJumpTrackFrame = -1;
}

#if EFZ_ENABLE_AUTO_ACTION_WAKE_TRACE
static const char* TraceBool(bool value) {
    return value ? "1" : "0";
}

static const char* TraceSubframeSuffix(int internalFrame) {
    static const char* kSuffixes[3] = { "00", "33", "66" };
    int subframe = internalFrame % 3;
    if (subframe < 0) subframe += 3;
    return kSuffixes[subframe];
}

static const char* TraceMoveClass(short moveID) {
    switch (moveID) {
        case IDLE_MOVE_ID: return "idle";
        case WALK_FWD_ID: return "walk-f";
        case WALK_BACK_ID: return "walk-b";
        case CROUCH_ID: return "crouch";
        case CROUCH_TO_STAND_ID: return "crouch-stand";
        case GROUNDTECH_PRE: return "gtech-pre";
        case GROUNDTECH_RECOVERY: return "gtech-96";
        case GROUNDTECH_START: return "gtech-start";
        case GROUNDTECH_END: return "gtech-end";
        case STRAIGHT_JUMP_ID: return "jump-n";
        case FORWARD_JUMP_ID: return "jump-f";
        case BACKWARD_JUMP_ID: return "jump-b";
        case FALLING_ID: return "fall";
        case FORWARD_DASH_START_ID: return "dash-f-start";
        case GROUND_BACKWARD_DASH_ID: return "backdash";
        case FORWARD_DASH_RECOVERY_SENTINEL_ID: return "dash-f-sentinel";
        case AIR_FORWARD_DASH_ID: return "airdash-f";
        case AIR_BACKWARD_DASH_ID: return "airdash-b";
        case KAORI_FORWARD_DASH_START_ID: return "kaori-dash-f";
        case STAND_GUARD_ID: return "stand-guard";
        case CROUCH_GUARD_ID: return "crouch-guard";
        case AIR_GUARD_ID: return "air-guard";
        case RG_STAND_ID: return "rg-stand";
        case RG_CROUCH_ID: return "rg-crouch";
        case RG_AIR_ID: return "rg-air";
        default: break;
    }
    if (IsHitstun(moveID)) return "hitstun";
    if (IsLaunched(moveID)) return "launch";
    if (IsAirtech(moveID)) return "airtech";
    if (IsThrown(moveID)) return "throw";
    if (IsFrozen(moveID)) return "frozen";
    if (IsAttackMove(moveID)) return "attack";
    if (IsBlockstun(moveID)) return "blockstun";
    if (IsActionable(moveID)) return "actionable";
    return "other";
}

static bool TraceWakeRelevantForPlayer(int playerNum, short prevMoveID, short moveID) {
    if (IsGroundtech(prevMoveID) || IsGroundtech(moveID)) return true;
    if (playerNum == 1) {
        return s_p1WakePrearmed || s_p1WakeMacroQueued || p1DelayState.triggerType == TRIGGER_ON_WAKEUP ||
               s_p1WakeMoveID96FrameCount > 0 || s_p1WakeMacro96FrameCount > 0 ||
               s_wakeMacroTokenNeutralizeCountdown[1] > 0;
    }
    return s_p2WakePrearmed || s_p2WakeMacroQueued || p2DelayState.triggerType == TRIGGER_ON_WAKEUP ||
           s_p2WakeMoveID96FrameCount > 0 || s_p2WakeMacro96FrameCount > 0 ||
           s_wakeMacroTokenNeutralizeCountdown[2] > 0;
}

static bool TraceWakeWindowActive(short moveID1, short moveID2, short prevMoveID1, short prevMoveID2) {
    static int s_traceWindow[3] = { 0, 0, 0 };
    static int s_lastUpdatedFrame = -1;
    const int now = frameCounter.load();

    if (s_lastUpdatedFrame != now) {
        s_lastUpdatedFrame = now;
        const bool p1Relevant = TraceWakeRelevantForPlayer(1, prevMoveID1, moveID1);
        const bool p2Relevant = TraceWakeRelevantForPlayer(2, prevMoveID2, moveID2);
        if (p1Relevant) {
            s_traceWindow[1] = 90;
        } else if (s_traceWindow[1] > 0) {
            --s_traceWindow[1];
        }
        if (p2Relevant) {
            s_traceWindow[2] = 90;
        } else if (s_traceWindow[2] > 0) {
            --s_traceWindow[2];
        }
    }

    return s_traceWindow[1] > 0 || s_traceWindow[2] > 0;
}

static void TraceAppendBufferSnapshot(std::ostringstream& oss, int playerNum) {
    uintptr_t playerPtr = GetPlayerPointer(playerNum);
    if (!playerPtr) {
        oss << " p" << playerNum << "Buf=null";
        return;
    }

    uint16_t head = 0;
    uint8_t last = 0;
    bool haveHead = SafeReadMemory(playerPtr + INPUT_BUFFER_INDEX_OFFSET, &head, sizeof(head));
    if (haveHead) {
        const uint16_t lastIndex = (head > 0) ? static_cast<uint16_t>(head - 1) : 0;
        (void)SafeReadMemory(playerPtr + INPUT_BUFFER_OFFSET + lastIndex, &last, sizeof(last));
        oss << " p" << playerNum << "BufHead=" << head
            << " p" << playerNum << "BufLast=" << static_cast<int>(last);
    } else {
        oss << " p" << playerNum << "BufHead=read-fail";
    }
}

static void TraceAppendPlayerWakeState(std::ostringstream& oss, int playerNum, short prevMoveID, short moveID) {
    const bool actionable = IsActionable(moveID);
    const bool groundtech = IsGroundtech(moveID);
    oss << " p" << playerNum << "=" << prevMoveID << "->" << moveID
        << "(" << TraceMoveClass(prevMoveID) << "->" << TraceMoveClass(moveID) << ")"
        << " act=" << TraceBool(actionable)
        << " gtech=" << TraceBool(groundtech);

    if (playerNum == 1) {
        oss << " pre=" << TraceBool(s_p1WakePrearmed)
            << " spec=" << TraceBool(s_p1WakePrearmIsSpecial)
            << " macro=" << TraceBool(s_p1WakePrearmIsMacro)
            << " action=" << s_p1WakePrearmActionType
            << " str=" << s_p1WakePrearmStrength
            << " c96=" << s_p1WakeMoveID96FrameCount
            << " m96=" << s_p1WakeMacro96FrameCount
            << " mQ=" << TraceBool(s_p1WakeMacroQueued)
            << " mTarget=" << s_p1WakeMacroTargetFrame
            << " frozen=" << TraceBool(s_p1WakeBufferFrozen)
            << " hold=" << TraceBool(s_p1WakeHoldPrimed) << "/" << TraceBool(s_p1WakeHoldIssued)
            << " dly=" << TraceBool(p1DelayState.isDelaying) << ":" << p1DelayState.delayFramesRemaining
            << " dTrig=" << TriggerTypeLabel(p1DelayState.triggerType)
            << " trig=" << TraceBool(p1TriggerActive) << ":" << p1TriggerCooldown;
    } else {
        oss << " pre=" << TraceBool(s_p2WakePrearmed)
            << " spec=" << TraceBool(s_p2WakePrearmIsSpecial)
            << " macro=" << TraceBool(s_p2WakePrearmIsMacro)
            << " action=" << s_p2WakePrearmActionType
            << " str=" << s_p2WakePrearmStrength
            << " c96=" << s_p2WakeMoveID96FrameCount
            << " m96=" << s_p2WakeMacro96FrameCount
            << " mQ=" << TraceBool(s_p2WakeMacroQueued)
            << " mTarget=" << s_p2WakeMacroTargetFrame
            << " mSlot=" << s_p2WakeMacroSlot
            << " opt=" << TraceBool(s_p2WakeOptionPicked)
            << " frozen=" << TraceBool(s_p2WakeBufferFrozen)
            << " hold=" << TraceBool(s_p2WakeHoldPrimed) << "/" << TraceBool(s_p2WakeHoldIssued)
            << " tokenCd=" << s_wakeMacroTokenNeutralizeCountdown[2]
            << " dly=" << TraceBool(p2DelayState.isDelaying) << ":" << p2DelayState.delayFramesRemaining
            << " dTrig=" << TriggerTypeLabel(p2DelayState.triggerType)
            << " trig=" << TraceBool(p2TriggerActive) << ":" << p2TriggerCooldown
            << " restore=" << TraceBool(g_pendingControlRestore.load())
            << " p2Override=" << TraceBool(g_p2ControlOverridden);
    }
    TraceAppendBufferSnapshot(oss, playerNum);
}

static void TraceAutoActionWakePhase(const char* phase, short moveID1, short moveID2, short prevMoveID1, short prevMoveID2) {
    if (!TraceWakeWindowActive(moveID1, moveID2, prevMoveID1, prevMoveID2)) return;

    const int internalFrame = frameCounter.load();
    std::ostringstream oss;
    oss << "[AA_WAKE_TRACE] phase=" << (phase ? phase : "unknown")
        << " IF=" << internalFrame
        << " VF=" << (internalFrame / 3) << "." << TraceSubframeSuffix(internalFrame)
        << " macroState=" << static_cast<int>(MacroController::GetState())
        << " freeze=" << TraceBool(g_bufferFreezingActive.load())
        << " freezeOwner=" << g_activeFreezePlayer.load()
        << " wakeBuf=" << TraceBool(g_wakeBufferingEnabled.load())
        << " random=" << TraceBool(triggerRandomizeEnabled.load());
    TraceAppendPlayerWakeState(oss, 1, prevMoveID1, moveID1);
    TraceAppendPlayerWakeState(oss, 2, prevMoveID2, moveID2);
    LogOut(oss.str(), false);
}

static void TraceAutoActionWakeEvent(const char* eventLabel, int playerNum, short prevMoveID, short moveID, const std::string& extra = std::string()) {
    if (!TraceWakeRelevantForPlayer(playerNum, prevMoveID, moveID)) return;

    const int internalFrame = frameCounter.load();
    std::ostringstream oss;
    oss << "[AA_WAKE_TRACE] event=" << (eventLabel ? eventLabel : "unknown")
        << " IF=" << internalFrame
        << " VF=" << (internalFrame / 3) << "." << TraceSubframeSuffix(internalFrame)
        << " p=" << playerNum;
    TraceAppendPlayerWakeState(oss, playerNum, prevMoveID, moveID);
    if (!extra.empty()) {
        oss << " " << extra;
    }
    LogOut(oss.str(), false);
}
#else
static inline void TraceAutoActionWakePhase(const char*, short, short, short, short) {}
static inline void TraceAutoActionWakeEvent(const char*, int, short, short, const std::string& = std::string()) {}
#endif

struct StunTimers {
    int blockStart = -1;
    int hitStart   = -1;
    int wakeMarker = -1;
    int lastBlockDuration = 0;
    int lastHitDuration   = 0;
    int lastWakeDelay     = 0;
    int sinceBlockEnd = -1;
    int sinceHitEnd   = -1;
    int sinceWake     = -1;
};

static StunTimers s_p1Timers;
static StunTimers s_p2Timers;

static inline bool AutoActionWorkPending();
static void ConsumeTutorialP2WakeCancel();

static bool s_p1RGPending = false;
static int  s_p1RGExpiry = 0;
static bool s_p2RGPending = false;
static int  s_p2RGExpiry = 0;

std::atomic<bool> g_tickIntegratedAutoActions{ true };

void AutoActionsTick_Inline(short moveID1, short moveID2) {
    // Online mode hard stop (never operate in netplay)
    if (g_onlineModeActive.load()) return;
    if (Mission::Engine::IsPracticeAutomationSuppressed()) return;
    TickAutoActionChargeFollowups();
    KaoriRecoilDuck::Tick();
    // Runs in EFZ's input hook: never stall behind a reset or tutorial lease transition, and
    // never inspect the producer's non-atomic delay/cooldown state without owning its mutex.
    std::unique_lock<std::recursive_mutex> stateLock(
        g_autoActionStateMutex, std::try_to_lock);
    if (!stateLock.owns_lock()) return;
    if (!AutoActionWorkPending()) return;

    static short prevMoveID1 = -1;
    static short prevMoveID2 = -1;

    TraceAutoActionWakePhase("tick:begin", moveID1, moveID2, prevMoveID1, prevMoveID2);
    
    if (s_p1WakeJumpTrackFrame >= 0) {
        int elapsed = frameCounter.load() - s_p1WakeJumpTrackFrame;
        if (elapsed <= 10) {
            if (moveID1 == 4 || moveID1 == 5 || moveID1 == 6) {
                LogOut("[AUTO-ACTION] P1 wake jump SUCCESS! moveID=" + std::to_string(moveID1) + 
                       " elapsed=" + std::to_string(elapsed) + "F", true);
                s_p1WakeJumpTrackFrame = -1;
            }
        } else {
            LogOut("[AUTO-ACTION] P1 wake jump FAILED - no jump state reached within 10F (moveID=" + 
                   std::to_string(moveID1) + ")", true);
            s_p1WakeJumpTrackFrame = -1;
        }
    }
    if (s_p2WakeJumpTrackFrame >= 0) {
        int elapsed = frameCounter.load() - s_p2WakeJumpTrackFrame;
        if (elapsed <= 10) {
            if (moveID2 == 4 || moveID2 == 5 || moveID2 == 6) {
                LogOut("[AUTO-ACTION] P2 wake jump SUCCESS! moveID=" + std::to_string(moveID2) + 
                       " elapsed=" + std::to_string(elapsed) + "F", true);
                s_p2WakeJumpTrackFrame = -1;
            }
        } else {
            LogOut("[AUTO-ACTION] P2 wake jump FAILED - no jump state reached within 10F (moveID=" + 
                   std::to_string(moveID2) + ")", true);
            s_p2WakeJumpTrackFrame = -1;
        }
    }

    // Validate an existing delay against the new fighter state before its countdown expires, or a
    // delay ending on the tick a hit/block/freeze starts queues a stale normal that fires much later.
    ClearDelayStatesIfNonActionable(moveID1, moveID2, prevMoveID1, prevMoveID2, "tick");
    TraceAutoActionWakePhase("tick:after-clear", moveID1, moveID2, prevMoveID1, prevMoveID2);
    ProcessTriggerDelays(moveID1, moveID2, prevMoveID1, prevMoveID2);
    TraceAutoActionWakePhase("tick:after-delays", moveID1, moveID2, prevMoveID1, prevMoveID2);
    // Monitor last, so a new edge on this tick can arm a fresh delay once the old one is resolved.
    MonitorAutoActions(moveID1, moveID2, prevMoveID1, prevMoveID2);
    TraceAutoActionWakePhase("tick:after-monitor", moveID1, moveID2, prevMoveID1, prevMoveID2);

    TraceAutoActionWakePhase("tick:prev-update", moveID1, moveID2, moveID1, moveID2);
    prevMoveID1 = moveID1;
    prevMoveID2 = moveID2;
}

static bool s_p1RGPrearmed = false;
static bool s_p2RGPrearmed = false;
static bool s_p1RGPrearmIsSpecial = false;
static bool s_p2RGPrearmIsSpecial = false;
static int  s_p1RGPrearmActionType = -1;
static int  s_p2RGPrearmActionType = -1;
static int  s_p1RGPrearmExpiry = 0;        // safety expiry (internal frames)
static int  s_p2RGPrearmExpiry = 0;
static uint64_t s_p1RGPrearmGeneration = 0;
static uint64_t s_p2RGPrearmGeneration = 0;

static inline void UpdateStunTimersForPlayer(StunTimers& t, short prevMoveID, short currMoveID) {
    int now = frameCounter.load();

    bool wasBlk = IsBlockstun(prevMoveID);
    bool isBlk  = IsBlockstun(currMoveID);
    if (!wasBlk && isBlk) {
        t.blockStart = now;
        t.sinceBlockEnd = -1;
    } else if (wasBlk && !isBlk) {
        if (t.blockStart >= 0) t.lastBlockDuration = now - t.blockStart;
        t.blockStart = -1;
        t.sinceBlockEnd = 0;
    } else if (!isBlk && t.sinceBlockEnd >= 0) {
        t.sinceBlockEnd++;
    }

    bool wasHit = IsHitstun(prevMoveID);
    bool isHit  = IsHitstun(currMoveID);
    if (!wasHit && isHit) {
        t.hitStart = now;
        t.sinceHitEnd = -1;
    } else if (wasHit && !isHit) {
        if (t.hitStart >= 0) t.lastHitDuration = now - t.hitStart;
        t.hitStart = -1;
        t.sinceHitEnd = 0;
    } else if (!isHit && t.sinceHitEnd >= 0) {
        t.sinceHitEnd++;
    }

    bool wasGtech = IsGroundtech(prevMoveID);
    bool isGtech  = IsGroundtech(currMoveID);
    bool actionableNow = IsActionable(currMoveID);
    if (currMoveID == GROUNDTECH_RECOVERY) {
        t.wakeMarker = now;
        t.sinceWake = -1;
    }
    if (wasGtech && actionableNow && !isGtech) {
        if (t.wakeMarker >= 0) t.lastWakeDelay = now - t.wakeMarker;
        t.sinceWake = 0;
        t.wakeMarker = -1;
    } else if (t.sinceWake >= 0 && actionableNow) {
        t.sinceWake++;
    }
}
static inline bool WakeCleanupWorkPending() {
    return s_wakeForcedNeutralToken[1] != 0 ||
           s_wakeForcedNeutralToken[2] != 0 ||
           s_wakeMacroCleanupToken[1] != 0 ||
           s_wakeMacroCleanupToken[2] != 0 ||
           g_wakeMacroPlaybackCompleted.load(std::memory_order_acquire) != 0 ||
           s_wakeMacroTokenNeutralizeCountdown[1] > 0 ||
           s_wakeMacroTokenNeutralizeCountdown[2] > 0;
}

static inline bool AutoActionWorkPending() {
    if (MacroController::GetState() == MacroController::State::Replaying) {
        return false;
    }
    const bool tutorialWake =
        g_tutorialP2WakeToken.load(std::memory_order_acquire) != 0;
    const bool tutorialSuppressesTriggers =
        Mission::TutorialEpisodePolicy::SuppressUserAutoActions(
            Mission::TutorialSession::IsActive(), tutorialWake);
    const bool triggersEnabled =
        !tutorialSuppressesTriggers && HasAnyAutoActionTriggerEnabled();
    bool delaysActive = p1DelayState.isDelaying || p2DelayState.isDelaying;
    bool cooldownsActive = p1TriggerActive || p2TriggerActive || (p1TriggerCooldown > 0) || (p2TriggerCooldown > 0);
    bool wakePrearmed = s_p1WakePrearmed || s_p2WakePrearmed;
    bool restorePending = g_pendingControlRestore.load();
    const bool chargePending = IsAutoActionChargeFollowupActive(1) ||
                               IsAutoActionChargeFollowupActive(2);
    return triggersEnabled || delaysActive || cooldownsActive || wakePrearmed ||
           restorePending || chargePending || WakeCleanupWorkPending();
}


bool IsCharacterGrounded(int playerNum) {
    uintptr_t base = GetEFZBase();
    if (!base) return true;
    
    uintptr_t playerOffset = (playerNum == 1) ? EFZ_BASE_OFFSET_P1 : EFZ_BASE_OFFSET_P2;
    uintptr_t yAddr = ResolvePointer(base, playerOffset, YPOS_OFFSET);
    uintptr_t yVelAddr = ResolvePointer(base, playerOffset, YVEL_OFFSET);
    
    if (!yAddr || !yVelAddr) return true;
    
    double yPos = 0.0, yVel = 0.0;
    SafeReadMemory(yAddr, &yPos, sizeof(double));
    SafeReadMemory(yVelAddr, &yVel, sizeof(double));
    
    return (yPos <= 0.1 && fabs(yVel) < 0.1);
}

short GetActionMoveID(int actionType, int triggerType, int playerNum) {
    bool isGrounded = IsCharacterGrounded(playerNum);
    LogOut("[AUTO-ACTION] Player " + std::to_string(playerNum) + 
           " ground state: " + (isGrounded ? "grounded" : "airborne") + 
           ", action type: " + std::to_string(actionType), detailedLogging.load());
    
    if (!isGrounded) {
        switch (actionType) {
            case ACTION_JA:
                return BASE_ATTACK_JA;
            case ACTION_JB:
                return BASE_ATTACK_JB;
            case ACTION_JC:
                return BASE_ATTACK_JC;
            case ACTION_JD:
                return BASE_ATTACK_JD;
            default:
                return STRAIGHT_JUMP_ID;
        }
    }
    else {
        if (actionType == ACTION_JA || actionType == ACTION_JB || actionType == ACTION_JC || actionType == ACTION_JD) {
            LogOut("[AUTO-ACTION] Cannot perform air move on ground, using JUMP instead", true);
            return STRAIGHT_JUMP_ID;
        }
        
        switch (actionType) {
            case ACTION_5A:
                return BASE_ATTACK_5A;
            case ACTION_5B:
                return BASE_ATTACK_5B;
            case ACTION_5C:
                return BASE_ATTACK_5C;
            case ACTION_5D:
                return BASE_ATTACK_5D;
            case ACTION_2A:
                return BASE_ATTACK_2A;
            case ACTION_2B:
                return BASE_ATTACK_2B;
            case ACTION_2C:
                return BASE_ATTACK_2C;
            case ACTION_2D:
                return BASE_ATTACK_2D;
            case ACTION_JUMP:
                return STRAIGHT_JUMP_ID;
            case ACTION_BACKDASH:
                return GROUND_BACKWARD_DASH_ID;
            case ACTION_BLOCK:
                return STAND_GUARD_ID;
            default:
                return BASE_ATTACK_5A;
        }
    }
}

static void ResetDelayState(TriggerDelayState& state) {
    state.isDelaying = false;
    state.delayFramesRemaining = 0;
    state.triggerType = TRIGGER_NONE;
    state.pendingMoveID = 0;
    state.chosenAction = -1;
    state.chosenStrength = -1;
    state.chosenMacroSlot = 0;
    state.chosenCustomId = -1;
    state.chosenDelay = -1;
    state.chosenChargeFollowup = 0;
    state.pendingMotionGeneration = 0;
    state.pendingNormalGeneration = 0;
    state.pendingRecipeGeneration = 0;
    state.dispatchAttempts = 0;
    state.pendingWaitTicks = 0;
}

// Max hold by a submitted but unconsumed normal pulse: 180 internal ticks = 60 visual frames.
// Longer than the slowest legitimate wait (the 42-frame RG state, or falling after an airtech).
static constexpr int kMaxPendingNormalWaitTicks = 180;

// Cancel only while still Pending, so an already-accepted normal is never disturbed.
static void CancelPendingNormalPulseIfAny(int playerNum,
                                          TriggerDelayState& state,
                                          const char* why) {
    if (state.pendingNormalGeneration == 0) return;
    if (GetAutoActionNormalPulseOutcome(playerNum, state.pendingNormalGeneration) ==
        AutoActionNormalPulseOutcome::Pending) {
        CancelAutoActionNormalPulse(playerNum);
        LogOut("[AUTO-ACTION] P" + std::to_string(playerNum) +
                   " cancelled pending normal gen=" +
                   std::to_string(state.pendingNormalGeneration) + ": " +
                   (why ? why : "unspecified"),
               true);
    }
    state.pendingNormalGeneration = 0;
}

static const char* P2MotionOutcomeLabel(P2AutoActionMotionOutcome outcome) {
    switch (outcome) {
        case P2AutoActionMotionOutcome::Pending: return "pending";
        case P2AutoActionMotionOutcome::Accepted: return "accepted";
        case P2AutoActionMotionOutcome::Failed: return "failed";
        case P2AutoActionMotionOutcome::Cancelled: return "cancelled";
        case P2AutoActionMotionOutcome::Unknown:
        default: return "unknown";
    }
}

static bool IsDashLikeState(short moveID) {
    return moveID == FORWARD_DASH_START_ID ||
           moveID == GROUND_BACKWARD_DASH_ID ||
           moveID == FORWARD_DASH_RECOVERY_SENTINEL_ID ||
           moveID == AIR_FORWARD_DASH_ID ||
           moveID == AIR_BACKWARD_DASH_ID ||
           moveID == KAORI_FORWARD_DASH_START_ID;
}

static bool IsJumpLikeState(short moveID) {
    return moveID == STRAIGHT_JUMP_ID ||
           moveID == FORWARD_JUMP_ID ||
           moveID == BACKWARD_JUMP_ID ||
           moveID == FALLING_ID;
}

static inline int TriggerStrengthForType(int triggerType) {
    switch (triggerType) {
        case TRIGGER_AFTER_BLOCK:   return triggerAfterBlockStrength.load();
        case TRIGGER_ON_WAKEUP:     return triggerOnWakeupStrength.load();
        case TRIGGER_AFTER_HITSTUN: return triggerAfterHitstunStrength.load();
        case TRIGGER_AFTER_AIRTECH: return triggerAfterAirtechStrength.load();
        case TRIGGER_ON_RG:         return triggerOnRGStrength.load();
        default:                    return 0;
    }
}

static inline int TriggerDelayForType(int triggerType) {
    switch (triggerType) {
        case TRIGGER_AFTER_BLOCK:   return triggerAfterBlockDelay.load();
        case TRIGGER_ON_WAKEUP:     return triggerOnWakeupDelay.load();
        case TRIGGER_AFTER_HITSTUN: return triggerAfterHitstunDelay.load();
        case TRIGGER_AFTER_AIRTECH: return triggerAfterAirtechDelay.load();
        case TRIGGER_ON_RG:         return triggerOnRGDelay.load();
        default:                    return 0;
    }
}

static inline int TriggerChargeForType(int triggerType) {
    switch (triggerType) {
        case TRIGGER_AFTER_BLOCK:   return triggerAfterBlockCharge.load();
        case TRIGGER_ON_WAKEUP:     return triggerOnWakeupCharge.load();
        case TRIGGER_AFTER_HITSTUN: return triggerAfterHitstunCharge.load();
        case TRIGGER_AFTER_AIRTECH: return triggerAfterAirtechCharge.load();
        case TRIGGER_ON_RG:         return triggerOnRGCharge.load();
        default:                    return 0;
    }
}

static int s_rolledMacroSlot[TRIGGER_ON_RG + 1] = {};

static inline int TriggerMacroSlotForType(int triggerType) {
    int slot = 0;
    switch (triggerType) {
        case TRIGGER_AFTER_BLOCK:   slot = triggerAfterBlockMacroSlot.load(); break;
        case TRIGGER_ON_WAKEUP:     slot = triggerOnWakeupMacroSlot.load(); break;
        case TRIGGER_AFTER_HITSTUN: slot = triggerAfterHitstunMacroSlot.load(); break;
        case TRIGGER_AFTER_AIRTECH: slot = triggerAfterAirtechMacroSlot.load(); break;
        case TRIGGER_ON_RG:         slot = triggerOnRGMacroSlot.load(); break;
        default:                    return 0;
    }
    if (slot != MacroController::kRandomSlotChoice) return slot;
    int& rolled = s_rolledMacroSlot[triggerType];
    if (rolled == 0 || MacroController::IsSlotEmpty(rolled)) rolled = MacroController::PickRandomFilledSlot();
    return rolled;
}

static inline int TriggerPoolDelayForIndex(int triggerType, int idx) {
    if (idx < 0 || idx >= MAX_ACTION_POOL_OPTIONS) {
        return TriggerDelayForType(triggerType);
    }

    const int* delays = nullptr;
    switch (triggerType) {
        case TRIGGER_AFTER_BLOCK:   delays = g_afterBlockActionPoolDelays; break;
        case TRIGGER_ON_WAKEUP:     delays = g_onWakeupActionPoolDelays; break;
        case TRIGGER_AFTER_HITSTUN: delays = g_afterHitstunActionPoolDelays; break;
        case TRIGGER_AFTER_AIRTECH: delays = g_afterAirtechActionPoolDelays; break;
        case TRIGGER_ON_RG:         delays = g_onRGActionPoolDelays; break;
        default:                    break;
    }
    if (!delays) {
        return TriggerDelayForType(triggerType);
    }

    const int explicitDelay = delays[idx];
    if (explicitDelay < 0) {
        return TriggerDelayForType(triggerType);
    }
    return CLAMP(explicitDelay, 0, 60);
}

static inline int TriggerPoolChargeForIndex(int triggerType, int idx) {
    if (idx < 0 || idx >= MAX_ACTION_POOL_OPTIONS) return 0;
    const int* charges = nullptr;
    switch (triggerType) {
        case TRIGGER_AFTER_BLOCK:   charges = g_afterBlockActionPoolCharges; break;
        case TRIGGER_ON_WAKEUP:     charges = g_onWakeupActionPoolCharges; break;
        case TRIGGER_AFTER_HITSTUN: charges = g_afterHitstunActionPoolCharges; break;
        case TRIGGER_AFTER_AIRTECH: charges = g_afterAirtechActionPoolCharges; break;
        case TRIGGER_ON_RG:         charges = g_onRGActionPoolCharges; break;
        default:                    break;
    }
    return charges ? CLAMP(charges[idx], 0, 2) : 0;
}

static inline int TriggerCustomIdForType(int triggerType) {
    switch (triggerType) {
        case TRIGGER_AFTER_BLOCK:   return triggerAfterBlockCustomID.load();
        case TRIGGER_ON_WAKEUP:     return triggerOnWakeupCustomID.load();
        case TRIGGER_AFTER_HITSTUN: return triggerAfterHitstunCustomID.load();
        case TRIGGER_AFTER_AIRTECH: return triggerAfterAirtechCustomID.load();
        case TRIGGER_ON_RG:         return triggerOnRGCustomID.load();
        default:                    return BASE_ATTACK_5A;
    }
}

static inline bool TriggerPoolConfigForType(int triggerType, uint32_t& legacyMask,
                                            uint64_t& maskLo, uint64_t& maskHi,
                                            bool& usePool) {
    switch (triggerType) {
        case TRIGGER_AFTER_BLOCK:
            legacyMask = triggerAfterBlockActionPoolMask.load();
            maskLo = triggerAfterBlockActionPoolMaskLo.load();
            maskHi = triggerAfterBlockActionPoolMaskHi.load();
            usePool = triggerAfterBlockUsePool.load();
            break;
        case TRIGGER_ON_WAKEUP:
            legacyMask = triggerOnWakeupActionPoolMask.load();
            maskLo = triggerOnWakeupActionPoolMaskLo.load();
            maskHi = triggerOnWakeupActionPoolMaskHi.load();
            usePool = triggerOnWakeupUsePool.load();
            break;
        case TRIGGER_AFTER_HITSTUN:
            legacyMask = triggerAfterHitstunActionPoolMask.load();
            maskLo = triggerAfterHitstunActionPoolMaskLo.load();
            maskHi = triggerAfterHitstunActionPoolMaskHi.load();
            usePool = triggerAfterHitstunUsePool.load();
            break;
        case TRIGGER_AFTER_AIRTECH:
            legacyMask = triggerAfterAirtechActionPoolMask.load();
            maskLo = triggerAfterAirtechActionPoolMaskLo.load();
            maskHi = triggerAfterAirtechActionPoolMaskHi.load();
            usePool = triggerAfterAirtechUsePool.load();
            break;
        case TRIGGER_ON_RG:
            legacyMask = triggerOnRGActionPoolMask.load();
            maskLo = triggerOnRGActionPoolMaskLo.load();
            maskHi = triggerOnRGActionPoolMaskHi.load();
            usePool = triggerOnRGUsePool.load();
            break;
        default:
            legacyMask = 0;
            maskLo = 0;
            maskHi = 0;
            usePool = false;
            return false;
    }

    if ((maskLo | maskHi) == 0 && legacyMask != 0) {
        const int strength = CLAMP(TriggerStrengthForType(triggerType), 0, 3);
        auto setBit = [&](int index) {
            if (index < 0 || index >= 128) return;
            if (index < 64) maskLo |= (1ull << index);
            else maskHi |= (1ull << (index - 64));
        };
        auto legacyToConcrete = [&](int motionIdx) -> int {
            switch (motionIdx) {
                case 0:  return 0 + strength;   // 5A-D
                case 1:  return 4 + strength;   // 2A-D
                case 2:  return 8 + strength;   // jA-D
                case 3:  return 20 + strength;  // 236A-D
                case 4:  return 24 + strength;  // 623A-D
                case 5:  return 28 + strength;  // 214A-D
                case 6:  return 32 + strength;  // 421A-D
                case 7:  return 44 + strength;  // 41236A-D
                case 8:  return 48 + strength;  // 2141236A-D
                case 9:  return 52 + strength;  // 236236A-D
                case 10: return 56 + strength;  // 214214A-D
                case 11: return 60 + strength;  // 641236A-D
                case 12: return 64 + strength;  // 463214A-D
                case 13: return 36 + strength;  // 412A-D
                case 14: return 40 + strength;  // 22A-D
                case 15: return 68 + strength;  // 4123641236A-D
                case 16: return 72 + strength;  // 6321463214A-D
                case 17: return 77 + CLAMP(TriggerStrengthForType(triggerType), 0, 2);
                case 18: return 80;
                case 19: return 81;
                case 20: return 82;
                case 21: return 76;
                case 22: return 12 + strength;  // 6A-D
                case 23: return 16 + strength;  // 4A-D
                default: return -1;
            }
        };
        for (int bit = 0; bit < 24; ++bit) {
            if ((legacyMask & (1u << bit)) == 0) continue;
            setBit(legacyToConcrete(bit));
        }
    }

    return true;
}

static inline TriggerOption MapConcretePoolIndexToOption(int idx, int triggerType) {
    int action = ACTION_5A;
    int strength = 0;
    if (idx >= 0 && idx <= 19) {
        action = idx;
        strength = idx % 4;
    } else if (idx >= 20 && idx <= 43) {
        const int local = idx - 20;
        strength = local % 4;
        switch (local / 4) {
            case 0: action = ACTION_QCF; break;
            case 1: action = ACTION_DP; break;
            case 2: action = ACTION_QCB; break;
            case 3: action = ACTION_421; break;
            case 4: action = ACTION_412; break;
            case 5: action = ACTION_22; break;
            default: action = ACTION_QCF; break;
        }
    } else if (idx >= 44 && idx <= 75) {
        const int local = idx - 44;
        strength = local % 4;
        switch (local / 4) {
            case 0: action = ACTION_SUPER1; break;
            case 1: action = ACTION_SUPER2; break;
            case 2: action = ACTION_236236; break;
            case 3: action = ACTION_214214; break;
            case 4: action = ACTION_641236; break;
            case 5: action = ACTION_463214; break;
            case 6: action = ACTION_4123641236; break;
            case 7: action = ACTION_6321463214; break;
            default: action = ACTION_SUPER1; break;
        }
    } else if (idx <= 82) {
        switch (idx) {
            case 76: action = ACTION_FINAL_MEMORY; strength = 0; break;
            case 77: action = ACTION_JUMP; strength = 0; break;
            case 78: action = ACTION_JUMP; strength = 1; break;
            case 79: action = ACTION_JUMP; strength = 2; break;
            case 80: action = ACTION_BACKDASH; strength = 0; break;
            case 81: action = ACTION_FORWARD_DASH; strength = 0; break;
            case 82: action = ACTION_BLOCK; strength = 0; break;
            default: action = ACTION_5A; strength = 0; break;
        }
    } else {
        // Appended catalog recipes keep every shipped pool bit (0..82) stable.
        if (!CharacterActionCatalog::PoolIndexToAction(idx, action, strength)) {
            action = ACTION_5A;
            strength = 0;
        }
    }
    return TriggerOption{
        true,
        action,
        strength,
        TriggerPoolDelayForIndex(triggerType, idx),
        TriggerCustomIdForType(triggerType),
        0,
        TriggerPoolChargeForIndex(triggerType, idx)
    };
}

static inline int ResolvePickedDelayForTrigger(int triggerType, int currentDelayFrames, int pickedUserDelay) {
    pickedUserDelay = CLAMP(pickedUserDelay, 0, 60);
    switch (triggerType) {
        case TRIGGER_ON_WAKEUP:
            return (pickedUserDelay <= 1) ? 0 : (pickedUserDelay - 1);
        case TRIGGER_ON_RG: {
            const int baseUserDelay = triggerOnRGDelay.load();
            const int resolved = currentDelayFrames - baseUserDelay + pickedUserDelay;
            return resolved < 0 ? 0 : resolved;
        }
        default:
            return pickedUserDelay;
    }
}

static inline bool PickRandomPoolOption(int triggerType, int playerNum, TriggerOption& out) {
    uint32_t legacyMask = 0;
    uint64_t maskLo = 0;
    uint64_t maskHi = 0;
    bool usePool = false;
    if (!TriggerPoolConfigForType(triggerType, legacyMask, maskLo, maskHi, usePool) ||
        !usePool || ((maskLo | maskHi) == 0)) {
        return false;
    }

    int candidates[128];
    int count = 0;
    const int targetChar = playerNum == 1 ? displayData.p1CharID
                                           : displayData.p2CharID;
    for (int bit = 0; bit < CharacterActionCatalog::kPoolCount; ++bit) {
        const bool set = bit < 64
            ? (((maskLo >> bit) & 1ull) != 0)
            : (((maskHi >> (bit - 64)) & 1ull) != 0);
        if (!set) continue;
        if (!CharacterActionCatalog::IsPoolIndexAvailable(targetChar, bit)) continue;
        candidates[count++] = bit;
    }
    if (count <= 0) return false;

    int seed = frameCounter.load() + playerNum * 31 + triggerType * 17;
    if (seed < 0) seed = -seed;
    const int poolIdx = candidates[seed % count];
    out = MapConcretePoolIndexToOption(poolIdx, triggerType);
    if (!ActionSupportsChargeFollowup(out.action)) {
        out.chargeFollowup = 0;
    }
    return true;
}

static bool ShouldCancelDelayForState(const TriggerDelayState& state, short prevMoveID, short moveID, std::string& reason) {
    if (!state.isDelaying) {
        return false;
    }

    // Once a native transaction is admitted, its attack or dash state is expected: keep the delay.
    // Hitstun, throw, launch, freeze or new blockstun are not: holding through them fires a stale action.
    if (state.pendingMotionGeneration != 0 ||
        state.pendingRecipeGeneration != 0 ||
        state.pendingNormalGeneration != 0) {
        // Deliberately narrow: only unambiguous "this fighter was hit" states.
        // AFTER_AIRTECH and ON_WAKEUP legitimately span airtech and groundtech recovery.
        const bool freshBlockstun = IsBlockstun(moveID) && !IsBlockstun(prevMoveID);
        if (IsHitstun(moveID) || IsThrown(moveID) || IsLaunched(moveID) ||
            IsFrozen(moveID) || freshBlockstun) {
            reason = "interrupted_while_pending";
            return true;
        }
        return false;
    }

    if (state.triggerType == TRIGGER_AFTER_BLOCK) {
        if (!IsBlockstun(prevMoveID) && IsBlockstun(moveID)) {
            reason = "new_blockstun";
            return true;
        }
        if (IsHitstun(moveID)) {
            reason = "hitstun";
            return true;
        }
        if (IsThrown(moveID)) {
            reason = "thrown";
            return true;
        }
        if (IsLaunched(moveID)) {
            reason = "launched";
            return true;
        }
        if (IsAirtech(moveID)) {
            reason = "airtech";
            return true;
        }
        if (IsGroundtech(moveID)) {
            reason = "groundtech";
            return true;
        }
        if (IsFrozen(moveID)) {
            reason = "frozen";
            return true;
        }
        if (IsJumpLikeState(moveID)) {
            reason = "jump";
            return true;
        }
        if (IsDashLikeState(moveID)) {
            reason = "dash";
            return true;
        }
        if (IsAttackMove(moveID)) {
            reason = "attack";
            return true;
        }
        if (IsRecoilGuard(moveID)) {
            reason = "recoil_guard";
            return true;
        }
        return false;
    }

    if (IsBlockstun(moveID)) {
        reason = "blockstun";
        return true;
    }
    if (IsHitstun(moveID)) {
        reason = "hitstun";
        return true;
    }
    if (IsFrozen(moveID)) {
        reason = "frozen";
        return true;
    }
    if (IsThrown(moveID)) {
        reason = "thrown";
        return true;
    }
    if (IsLaunched(moveID)) {
        reason = "launched";
        return true;
    }
    if (IsAirtech(moveID)) {
        reason = "airtech";
        return true;
    }
    if (IsGroundtech(moveID)) {
        reason = "groundtech";
        return true;
    }

    return false;
}

static bool StartTriggerMacroPlayback(int playerNum) {
    if (MacroController::GetState() == MacroController::State::PreRecord) {
        MacroController::UnswapThenStop();
        DirectDrawHook::AddMessage("Macro: PreRecord canceled", "MACRO", RGB(255, 200, 120), 900, 0, 120);
        return false;
    }
    if (MacroController::GetState() == MacroController::State::Recording) {
        DirectDrawHook::AddMessage("Macro: Cannot play while recording", "MACRO", RGB(255, 180, 120), 900, 0, 120);
        return false;
    }
    if (MacroController::GetState() != MacroController::State::Idle) return false;
    if (!MacroController::PlayForPlayer(playerNum, 0, false)) {
        DirectDrawHook::AddMessage("Macro: Slot empty or unavailable", "MACRO", RGB(255, 120, 120), 1000, 0, 120);
        return false;
    }
    return true;
}

void ProcessTriggerDelays(short moveID1, short moveID2, short prevMoveID1, short prevMoveID2) {
    std::lock_guard<std::recursive_mutex> stateLock(g_autoActionStateMutex);
    uintptr_t base = GetEFZBase();
    if (!base) return;

    if (p1DelayState.isDelaying) {
        const int remainingBefore = p1DelayState.delayFramesRemaining;
        const int visualBefore = InternalTicksToVisualFramesCeil(remainingBefore);
        LogOut("[DELAY] P1 delaying: internalRemaining=" + std::to_string(remainingBefore) +
               ", visualRemaining=" + std::to_string(visualBefore) +
               ", triggerType=" + std::to_string(p1DelayState.triggerType), detailedLogging.load());
        p1DelayState.delayFramesRemaining--;
        const int remainingAfter = p1DelayState.delayFramesRemaining;
        const int visualAfter = InternalTicksToVisualFramesCeil(remainingAfter);

        if (remainingAfter > 0 && visualAfter != visualBefore && detailedLogging.load()) {
            LogOut("[AUTO-ACTION] P1 delay countdown: visualRemaining=" + std::to_string(visualAfter) +
                   " internalRemaining=" + std::to_string(remainingAfter),
                   true);
        }
        
        if (remainingAfter <= 0) {
            LogOut("[AUTO-ACTION] P1 delay expired, applying action", true);
            TraceAutoActionWakeEvent("p1-delay-expired", 1, prevMoveID1, moveID1);
            bool dispatchThisTick = true;
            if (p1DelayState.pendingRecipeGeneration != 0) {
                const uint64_t generation =
                    p1DelayState.pendingRecipeGeneration;
                const AutoActionMotionOutcome outcome =
                    KaoriRecoilDuck::GetOutcome(1, generation);
                if (outcome == AutoActionMotionOutcome::Pending) {
                    p1DelayState.delayFramesRemaining = 1;
                    dispatchThisTick = false;
                } else if (outcome == AutoActionMotionOutcome::Accepted) {
                    LogOut("[AUTO-ACTION] P1 staged recipe gen=" +
                               std::to_string(generation) +
                               " completed its exact destination",
                           true);
                    if (ValidationMetricsEnabled()) {
                        GetValidationMetrics().p1ActionsApplied++;
                    }
                    ResetDelayState(p1DelayState);
                    p1ActionApplied = true;
                    dispatchThisTick = false;
                } else {
                    LogOut("[AUTO-ACTION] P1 staged recipe gen=" +
                               std::to_string(generation) +
                               " ended before its exact destination",
                           true);
                    p1DelayState.pendingRecipeGeneration = 0;
                    if (p1DelayState.dispatchAttempts >=
                        kMaxAutoActionDispatchAttempts) {
                        ResetDelayState(p1DelayState);
                        p1TriggerActive = false;
                        p1TriggerCooldown = 0;
                        dispatchThisTick = false;
                    }
                }
            }

            if (dispatchThisTick &&
                p1DelayState.pendingNormalGeneration != 0) {
                const uint64_t generation =
                    p1DelayState.pendingNormalGeneration;
                const AutoActionNormalPulseOutcome outcome =
                    GetAutoActionNormalPulseOutcome(1, generation);
                if (outcome == AutoActionNormalPulseOutcome::Pending) {
                    if (++p1DelayState.pendingWaitTicks >
                        kMaxPendingNormalWaitTicks) {
                        LogOut("[AUTO-ACTION] P1 normal gen=" +
                                   std::to_string(generation) +
                                   " never reached a press window within " +
                                   std::to_string(kMaxPendingNormalWaitTicks) +
                                   " internal ticks; retiring trigger",
                               true);
                        CancelPendingNormalPulseIfAny(
                            1, p1DelayState,
                            "pulse exceeded its bounded press window");
                        CancelAutoActionChargeFollowup(
                            1, "primary normal never reached a press window");
                        ResetDelayState(p1DelayState);
                        p1TriggerActive = false;
                        p1TriggerCooldown = 0;
                    } else {
                        p1DelayState.delayFramesRemaining = 1;
                    }
                    dispatchThisTick = false;
                } else if (outcome == AutoActionNormalPulseOutcome::Accepted) {
                    LogOut("[AUTO-ACTION] P1 normal gen=" +
                               std::to_string(generation) +
                               " was consumed by EFZ; completing delayed action",
                           true);
                    if (ValidationMetricsEnabled()) {
                        GetValidationMetrics().p1ActionsApplied++;
                    }
                    ResetDelayState(p1DelayState);
                    p1ActionApplied = true;
                    dispatchThisTick = false;
                } else {
                    LogOut("[AUTO-ACTION] P1 normal gen=" +
                               std::to_string(generation) +
                               " ended without consumer acceptance after attempt " +
                               std::to_string(p1DelayState.dispatchAttempts),
                           true);
                    CancelAutoActionChargeFollowup(
                        1, "primary normal was not accepted");
                    p1DelayState.pendingNormalGeneration = 0;
                    if (p1DelayState.dispatchAttempts >=
                        kMaxAutoActionDispatchAttempts) {
                        ResetDelayState(p1DelayState);
                        p1TriggerActive = false;
                        p1TriggerCooldown = 0;
                        dispatchThisTick = false;
                    }
                }
            }

            if (dispatchThisTick &&
                p1DelayState.pendingMotionGeneration != 0) {
                const uint64_t generation =
                    p1DelayState.pendingMotionGeneration;
                const AutoActionMotionOutcome outcome =
                    GetAutoActionMotionTransactionOutcome(1, generation);
                if (outcome == AutoActionMotionOutcome::Pending) {
                    p1DelayState.delayFramesRemaining = 1;
                    LogOut("[AUTO-ACTION] P1 native transaction gen=" +
                               std::to_string(generation) +
                               " still pending; retaining the exact delayed option",
                           detailedLogging.load());
                    dispatchThisTick = false;
                } else if (outcome == AutoActionMotionOutcome::Accepted) {
                    LogOut("[AUTO-ACTION] P1 native transaction gen=" +
                               std::to_string(generation) +
                               " was consumed by EFZ; completing delayed action",
                           true);
                    if (ValidationMetricsEnabled()) {
                        GetValidationMetrics().p1ActionsApplied++;
                    }
                    ResetDelayState(p1DelayState);
                    p1ActionApplied = true;
                    dispatchThisTick = false;
                } else {
                    LogOut("[AUTO-ACTION] P1 native transaction gen=" +
                               std::to_string(generation) +
                               " ended " + P2MotionOutcomeLabel(outcome) +
                               " after attempt " +
                               std::to_string(p1DelayState.dispatchAttempts),
                           true);
                    CancelAutoActionChargeFollowup(
                        1, "primary motion was not accepted");
                    p1DelayState.pendingMotionGeneration = 0;
                    if (p1DelayState.dispatchAttempts >=
                        kMaxAutoActionDispatchAttempts) {
                        LogOut("[AUTO-ACTION] P1 native transaction exhausted its bounded retries; retiring trigger",
                               true);
                        ResetDelayState(p1DelayState);
                        p1TriggerActive = false;
                        p1TriggerCooldown = 0;
                        dispatchThisTick = false;
                    }
                }
            }

            if (dispatchThisTick &&
                p1DelayState.triggerType == TRIGGER_ON_RG &&
                s_p1RGPrearmed && s_p1RGPrearmIsSpecial &&
                s_p1RGPrearmGeneration != 0 &&
                GetAutoActionMotionTransactionOutcome(
                    1, s_p1RGPrearmGeneration) ==
                    AutoActionMotionOutcome::Pending) {
                p1DelayState.delayFramesRemaining = 1;
                dispatchThisTick = false;
            }

            if (dispatchThisTick) {
                int sel = TriggerMacroSlotForType(p1DelayState.triggerType);
                if (p1DelayState.chosenMacroSlot > 0) sel = p1DelayState.chosenMacroSlot;
                if (sel > 0 && MacroController::GetState() != MacroController::State::Recording) {
                    sel = CLAMP(sel, 1, MacroController::GetSlotCount());
                    if (!MacroController::IsSlotEmpty(sel)) {
                        MacroController::SetCurrentSlot(sel);
                        if (!(MacroController::GetState() == MacroController::State::Replaying &&
                              MacroController::GetPlaybackPlayer() == 1)) {
                            LogOut("[AUTO-ACTION][MACRO] Starting macro playback (slot=" + std::to_string(sel) + ") for P1 on trigger expiry", true);
                            const bool started = StartTriggerMacroPlayback(1);
                            if (started &&
                                MacroController::GetState() == MacroController::State::Replaying) {
                                ResetDelayState(p1DelayState);
                                p1TriggerActive = false;
                                p1TriggerCooldown = 0;
                                if (ValidationMetricsEnabled()) { GetValidationMetrics().p1ActionsApplied++; }
                            } else {
                                p1DelayState.delayFramesRemaining = 1;
                                LogOut("[AUTO-ACTION][MACRO] Playback owner was busy; retrying the same slot next tick", true);
                            }
                            dispatchThisTick = false;
                        }
                    }
                }
            }

            if (dispatchThisTick) {
                AutoActionApplyResult result = AutoActionApplyResult::Busy;
                uint64_t motionGeneration = 0;
                uint64_t recipeGeneration = 0;
                uint64_t normalGeneration = 0;
                if (!(MacroController::GetState() == MacroController::State::Replaying &&
                      MacroController::GetPlaybackPlayer() == 1)) {
                    result = ApplyAutoAction(1, 0, moveID1, prevMoveID1,
                                             &motionGeneration,
                                             &normalGeneration,
                                             &recipeGeneration);
                } else {
                    LogOut("[AUTO-ACTION][MACRO] P1 macro active; skipping ApplyAutoAction", detailedLogging.load());
                    if (ValidationMetricsEnabled()) { GetValidationMetrics().p1SuppressedByMacro++; }
                }
                if (result == AutoActionApplyResult::Accepted) {
                    if (recipeGeneration != 0) {
                        p1DelayState.pendingRecipeGeneration = recipeGeneration;
                        ++p1DelayState.dispatchAttempts;
                        p1DelayState.delayFramesRemaining = 1;
                        LogOut("[AUTO-ACTION] P1 staged recipe admitted gen=" +
                                   std::to_string(recipeGeneration) +
                                   "; waiting for its exact destination",
                               true);
                    } else if (motionGeneration != 0) {
                        p1DelayState.pendingMotionGeneration = motionGeneration;
                        ++p1DelayState.dispatchAttempts;
                        p1DelayState.delayFramesRemaining = 1;
                        LogOut("[AUTO-ACTION] P1 native transaction admitted gen=" +
                                   std::to_string(motionGeneration) +
                                   " attempt=" +
                                   std::to_string(p1DelayState.dispatchAttempts) +
                                   "; waiting for EFZ's command consumer",
                               true);
                    } else if (normalGeneration != 0) {
                        p1DelayState.pendingNormalGeneration = normalGeneration;
                        p1DelayState.pendingWaitTicks = 0;
                        ++p1DelayState.dispatchAttempts;
                        p1DelayState.delayFramesRemaining = 1;
                        LogOut("[AUTO-ACTION] P1 normal admitted gen=" +
                                   std::to_string(normalGeneration) +
                                   "; waiting for EFZ's character consumer",
                               true);
                    } else {
                        LogOut("[AUTO-ACTION] P1 action accepted by input system", true);
                        if (ValidationMetricsEnabled()) {
                            GetValidationMetrics().p1ActionsApplied++;
                        }
                        ResetDelayState(p1DelayState);
                        p1ActionApplied = true;
                    }
                } else if (result == AutoActionApplyResult::Busy) {
                    p1DelayState.delayFramesRemaining = 1;
                    LogOut("[AUTO-ACTION] P1 input owner busy; retrying the same delayed option next tick", true);
                } else {
                    LogOut("[AUTO-ACTION] P1 delayed option is invalid in the current world; retiring it", true);
                    ResetDelayState(p1DelayState);
                    p1TriggerActive = false;
                    p1TriggerCooldown = 0;
                }
            }
        }
    }
    
    if (p2DelayState.isDelaying) {
        const int remainingBefore = p2DelayState.delayFramesRemaining;
        const int visualBefore = InternalTicksToVisualFramesCeil(remainingBefore);
        LogOut("[DELAY] P2 delaying: internalRemaining=" + std::to_string(remainingBefore) +
               ", visualRemaining=" + std::to_string(visualBefore) +
               ", triggerType=" + std::to_string(p2DelayState.triggerType), detailedLogging.load());
        p2DelayState.delayFramesRemaining--;
        const int remainingAfter = p2DelayState.delayFramesRemaining;
        const int visualAfter = InternalTicksToVisualFramesCeil(remainingAfter);

        if (remainingAfter > 0 && visualAfter != visualBefore && detailedLogging.load()) {
            LogOut("[AUTO-ACTION] P2 delay countdown: visualRemaining=" + std::to_string(visualAfter) +
                   " internalRemaining=" + std::to_string(remainingAfter),
                   true);
        }
        
    if (remainingAfter <= 0) {
            LogOut("[AUTO-ACTION] P2 delay expired, applying action", true);
            TraceAutoActionWakeEvent("p2-delay-expired", 2, prevMoveID2, moveID2);

            if (p2DelayState.pendingRecipeGeneration != 0) {
                const uint64_t generation =
                    p2DelayState.pendingRecipeGeneration;
                const AutoActionMotionOutcome outcome =
                    KaoriRecoilDuck::GetOutcome(2, generation);
                if (outcome == AutoActionMotionOutcome::Pending) {
                    p2DelayState.delayFramesRemaining = 1;
                    LogOut("[AUTO-ACTION] P2 staged recipe gen=" +
                               std::to_string(generation) +
                               " still pending",
                           detailedLogging.load());
                    return;
                }
                if (outcome == AutoActionMotionOutcome::Accepted) {
                    LogOut("[AUTO-ACTION] P2 staged recipe gen=" +
                               std::to_string(generation) +
                               " completed its exact destination",
                           true);
                    if (ValidationMetricsEnabled()) {
                        GetValidationMetrics().p2ActionsApplied++;
                    }
                    ResetDelayState(p2DelayState);
                    p2ActionApplied = true;
                    return;
                }

                LogOut("[AUTO-ACTION] P2 staged recipe gen=" +
                           std::to_string(generation) +
                           " ended before its exact destination after attempt " +
                           std::to_string(p2DelayState.dispatchAttempts),
                       true);
                p2DelayState.pendingRecipeGeneration = 0;
                if (p2DelayState.dispatchAttempts >=
                    kMaxAutoActionDispatchAttempts) {
                    ResetDelayState(p2DelayState);
                    p2TriggerActive = false;
                    p2TriggerCooldown = 0;
                    return;
                }
            }

            if (p2DelayState.pendingNormalGeneration != 0) {
                const uint64_t generation =
                    p2DelayState.pendingNormalGeneration;
                const AutoActionNormalPulseOutcome outcome =
                    GetAutoActionNormalPulseOutcome(2, generation);
                if (outcome == AutoActionNormalPulseOutcome::Pending) {
                    if (++p2DelayState.pendingWaitTicks >
                        kMaxPendingNormalWaitTicks) {
                        LogOut("[AUTO-ACTION] P2 normal gen=" +
                                   std::to_string(generation) +
                                   " never reached a press window within " +
                                   std::to_string(kMaxPendingNormalWaitTicks) +
                                   " internal ticks; retiring trigger",
                               true);
                        CancelPendingNormalPulseIfAny(
                            2, p2DelayState,
                            "pulse exceeded its bounded press window");
                        CancelAutoActionChargeFollowup(
                            2, "primary normal never reached a press window");
                        ResetDelayState(p2DelayState);
                        p2TriggerActive = false;
                        p2TriggerCooldown = 0;
                        return;
                    }
                    p2DelayState.delayFramesRemaining = 1;
                    LogOut("[AUTO-ACTION] P2 normal gen=" +
                               std::to_string(generation) +
                               " still pending; retaining the exact delayed option",
                           detailedLogging.load());
                    return;
                }
                if (outcome == AutoActionNormalPulseOutcome::Accepted) {
                    LogOut("[AUTO-ACTION] P2 normal gen=" +
                               std::to_string(generation) +
                               " was consumed by EFZ; completing delayed action",
                           true);
                    if (ValidationMetricsEnabled()) {
                        GetValidationMetrics().p2ActionsApplied++;
                    }
                    ResetDelayState(p2DelayState);
                    p2ActionApplied = true;
                    return;
                }

                LogOut("[AUTO-ACTION] P2 normal gen=" +
                           std::to_string(generation) +
                           " ended without consumer acceptance after attempt " +
                           std::to_string(p2DelayState.dispatchAttempts),
                       true);
                CancelAutoActionChargeFollowup(
                    2, "primary normal was not accepted");
                p2DelayState.pendingNormalGeneration = 0;
                if (p2DelayState.dispatchAttempts >=
                    kMaxAutoActionDispatchAttempts) {
                    LogOut("[AUTO-ACTION] P2 normal exhausted its bounded retries; retiring trigger",
                           true);
                    ResetDelayState(p2DelayState);
                    p2TriggerActive = false;
                    p2TriggerCooldown = 0;
                    return;
                }
            }

            if (p2DelayState.pendingMotionGeneration != 0) {
                const uint64_t generation =
                    p2DelayState.pendingMotionGeneration;
                const P2AutoActionMotionOutcome outcome =
                    GetP2AutoActionMotionTransactionOutcome(generation);
                if (outcome == P2AutoActionMotionOutcome::Pending) {
                    p2DelayState.delayFramesRemaining = 1;
                    LogOut("[AUTO-ACTION] P2 native transaction gen=" +
                               std::to_string(generation) +
                               " still pending; retaining the exact delayed option",
                           detailedLogging.load());
                    return;
                }
                if (outcome == P2AutoActionMotionOutcome::Accepted) {
                    LogOut("[AUTO-ACTION] P2 native transaction gen=" +
                               std::to_string(generation) +
                               " was consumed by EFZ; completing delayed action",
                           true);
                    if (ValidationMetricsEnabled()) {
                        GetValidationMetrics().p2ActionsApplied++;
                    }
                    ResetDelayState(p2DelayState);
                    p2ActionApplied = true;
                    return;
                }

                LogOut("[AUTO-ACTION] P2 native transaction gen=" +
                           std::to_string(generation) +
                           " ended " + P2MotionOutcomeLabel(outcome) +
                           " after attempt " +
                           std::to_string(p2DelayState.dispatchAttempts),
                       true);
                CancelAutoActionChargeFollowup(
                    2, "primary motion was not accepted");
                p2DelayState.pendingMotionGeneration = 0;
                if (p2DelayState.dispatchAttempts >=
                    kMaxAutoActionDispatchAttempts) {
                    LogOut("[AUTO-ACTION] P2 native transaction exhausted its bounded retries; retiring trigger",
                           true);
                    ResetDelayState(p2DelayState);
                    p2TriggerActive = false;
                    p2TriggerCooldown = 0;
                    return;
                }
            }

            if (p2DelayState.triggerType == TRIGGER_ON_RG &&
                s_p2RGPrearmed && s_p2RGPrearmIsSpecial &&
                s_p2RGPrearmGeneration != 0 &&
                GetP2AutoActionMotionTransactionOutcome(
                    s_p2RGPrearmGeneration) ==
                    P2AutoActionMotionOutcome::Pending) {
                p2DelayState.delayFramesRemaining = 1;
                return;
            }

            int sel = 0;
            switch (p2DelayState.triggerType) {
                case TRIGGER_AFTER_BLOCK: sel = TriggerMacroSlotForType(TRIGGER_AFTER_BLOCK); break;
                case TRIGGER_ON_WAKEUP: sel = TriggerMacroSlotForType(TRIGGER_ON_WAKEUP); break;
                case TRIGGER_AFTER_HITSTUN: sel = TriggerMacroSlotForType(TRIGGER_AFTER_HITSTUN); break;
                case TRIGGER_AFTER_AIRTECH: sel = TriggerMacroSlotForType(TRIGGER_AFTER_AIRTECH); break;
                case TRIGGER_ON_RG: sel = TriggerMacroSlotForType(TRIGGER_ON_RG); break;
                default: sel = 0; break;
            }
            if (p2DelayState.chosenMacroSlot > 0) sel = p2DelayState.chosenMacroSlot;
            if (sel > 0 && MacroController::GetState() != MacroController::State::Recording) {
                sel = CLAMP(sel, 1, MacroController::GetSlotCount());
                if (!MacroController::IsSlotEmpty(sel)) {
                    MacroController::SetCurrentSlot(sel);
                    if (!(MacroController::GetState() == MacroController::State::Replaying &&
                          MacroController::GetPlaybackPlayer() == 2)) {
                        LogOut("[AUTO-ACTION][MACRO] Starting macro playback (slot=" + std::to_string(sel) + ") for P2 on trigger expiry", true);
                        const bool started = StartTriggerMacroPlayback(2);
                        if (started &&
                            MacroController::GetState() == MacroController::State::Replaying) {
                            ResetDelayState(p2DelayState);
                            p2TriggerActive = false;
                            p2TriggerCooldown = 0;
                            if (ValidationMetricsEnabled()) { GetValidationMetrics().p2ActionsApplied++; }
                        } else {
                            p2DelayState.delayFramesRemaining = 1;
                            LogOut("[AUTO-ACTION][MACRO] Playback owner was busy; retrying the same slot next tick", true);
                        }
                        return;
                    }
                }
            }

            AutoActionApplyResult result = AutoActionApplyResult::Busy;
            uint64_t motionGeneration = 0;
            uint64_t normalGeneration = 0;
            uint64_t recipeGeneration = 0;
            if (!(MacroController::GetState() == MacroController::State::Replaying &&
                  MacroController::GetPlaybackPlayer() == 2)) {
                result = ApplyAutoAction(2, 0, moveID2, prevMoveID2,
                                         &motionGeneration,
                                         &normalGeneration,
                                         &recipeGeneration);
            } else {
                LogOut("[AUTO-ACTION][MACRO] P2 macro active; skipping ApplyAutoAction", detailedLogging.load());
                if (ValidationMetricsEnabled()) { GetValidationMetrics().p2SuppressedByMacro++; }
            }
            if (result == AutoActionApplyResult::Accepted) {
                if (recipeGeneration != 0) {
                    p2DelayState.pendingRecipeGeneration = recipeGeneration;
                    ++p2DelayState.dispatchAttempts;
                    p2DelayState.delayFramesRemaining = 1;
                    LogOut("[AUTO-ACTION] P2 staged recipe admitted gen=" +
                               std::to_string(recipeGeneration) +
                               " attempt=" +
                               std::to_string(p2DelayState.dispatchAttempts) +
                               "; waiting for its exact destination",
                           true);
                } else if (motionGeneration != 0) {
                    p2DelayState.pendingMotionGeneration = motionGeneration;
                    ++p2DelayState.dispatchAttempts;
                    p2DelayState.delayFramesRemaining = 1;
                    LogOut("[AUTO-ACTION] P2 native transaction admitted gen=" +
                               std::to_string(motionGeneration) +
                               " attempt=" +
                               std::to_string(p2DelayState.dispatchAttempts) +
                               "; waiting for EFZ's command consumer",
                           true);
                } else if (normalGeneration != 0) {
                    p2DelayState.pendingNormalGeneration = normalGeneration;
                    p2DelayState.pendingWaitTicks = 0;
                    ++p2DelayState.dispatchAttempts;
                    p2DelayState.delayFramesRemaining = 1;
                    LogOut("[AUTO-ACTION] P2 normal admitted gen=" +
                               std::to_string(normalGeneration) +
                               " attempt=" +
                               std::to_string(p2DelayState.dispatchAttempts) +
                               "; waiting for EFZ's character consumer",
                           true);
                } else {
                    LogOut("[AUTO-ACTION] P2 action accepted by input system", true);
                    if (ValidationMetricsEnabled()) { GetValidationMetrics().p2ActionsApplied++; }
                    ResetDelayState(p2DelayState);
                    p2ActionApplied = true;
                    if (detailedLogging.load()) {
                        LogOut(std::string("[AUTO-ACTION] P2 delayed action accepted; pendingRestore=") +
                               (g_pendingControlRestore.load()?"true":"false"), true);
                    }
                }
            } else if (result == AutoActionApplyResult::Busy) {
                p2DelayState.delayFramesRemaining = 1;
                LogOut("[AUTO-ACTION] P2 input owner busy; retrying the same delayed option next tick", true);
            } else {
                LogOut("[AUTO-ACTION] P2 delayed option is invalid in the current world; retiring it", true);
                ResetDelayState(p2DelayState);
                p2TriggerActive = false;
                p2TriggerCooldown = 0;
            }
        }
    }
}

void StartTriggerDelay(int playerNum, int triggerType, short moveID, int delayFrames) {
    if (MacroController::GetState() == MacroController::State::Replaying &&
        MacroController::GetPlaybackPlayer() == playerNum) {
        LogOut("[AUTO-ACTION][MACRO] Suppressing P" + std::to_string(playerNum) +
               " StartTriggerDelay during macro playback", detailedLogging.load());
        return;
    }
    if (playerNum == 1 && p1TriggerActive) {
    LogOut("[AUTO-ACTION] P1 StartTriggerDelay ignored - trigger already active/cooldown=" + std::to_string(p1TriggerCooldown), detailedLogging.load());
        return;
    }
    if (playerNum == 2 && p2TriggerActive) {
    LogOut("[AUTO-ACTION] P2 StartTriggerDelay ignored - trigger already active/cooldown=" + std::to_string(p2TriggerCooldown), detailedLogging.load());
        return;
    }

    g_lastActiveTriggerType.store(triggerType);
    g_lastActiveTriggerFrame.store(frameCounter.load());

    AutoActionLogScope triggerScope("StartTriggerDelay", playerNum, triggerType);
    (void)triggerScope;

    LogOut("[AUTO-ACTION] StartTriggerDelay called: Player=" + std::to_string(playerNum) + 
           ", triggerType=" + std::to_string(triggerType) + 
           ", delay=" + std::to_string(delayFrames) +
           ", p1ActApplied=" + std::to_string(p1ActionApplied) +
           ", p2ActApplied=" + std::to_string(p2ActionApplied) +
           ", p1TrigActive=" + std::to_string(p1TriggerActive) +
           ", p2TrigActive=" + std::to_string(p2TriggerActive) +
           ", p1Cooldown=" + std::to_string(p1TriggerCooldown) +
           ", p2Cooldown=" + std::to_string(p2TriggerCooldown), detailedLogging.load());
    if (triggerType == TRIGGER_ON_WAKEUP) {
        const short stateMoveID = static_cast<short>(GetPlayerMoveID(playerNum));
        TraceAutoActionWakeEvent("start-trigger-delay:wakeup", playerNum, stateMoveID, stateMoveID,
                                 "pendingActionMoveID=" + std::to_string(moveID) +
                                 " delayF=" + std::to_string(delayFrames));
    }
    if (ValidationMetricsEnabled()) {
        ValidationMetrics &vm = GetValidationMetrics();
        if (playerNum == 1) vm.p1TriggerStarts++; else if (playerNum == 2) vm.p2TriggerStarts++;
    }
    // ApplyAutoAction never sets the AI flag itself. Motion/pattern transactions humanize the target
    // for one producer pass; normals read the live flag to pick NativePoll or AiPostWrite.
    if (playerNum == 1) {
        p1TriggerActive = true;
        p1TriggerCooldown = TRIGGER_COOLDOWN_FRAMES;
        p1DelayState.triggerType = triggerType;
    } else {
        p2TriggerActive = true;
        p2TriggerCooldown = TRIGGER_COOLDOWN_FRAMES;
        p2DelayState.triggerType = triggerType;
    }

    TriggerDelayState& dstate = (playerNum == 1) ? p1DelayState : p2DelayState;
    dstate.chosenAction = -1;
    dstate.chosenStrength = -1;
    dstate.chosenMacroSlot = 0;
    dstate.chosenCustomId = -1;
    dstate.chosenDelay = -1;
    dstate.chosenChargeFollowup = CLAMP(
        TriggerChargeForType(triggerType), 0, 2);
    dstate.pendingMotionGeneration = 0;
    dstate.pendingNormalGeneration = 0;
    dstate.pendingRecipeGeneration = 0;
    dstate.dispatchAttempts = 0;
    dstate.pendingWaitTicks = 0;
    
    const bool reuseP1WakePick = (playerNum == 1 &&
                                  triggerType == TRIGGER_ON_WAKEUP &&
                                  s_p1WakeOptionPicked);
    const bool reuseP2WakePick = (playerNum == 2 &&
                                  triggerType == TRIGGER_ON_WAKEUP &&
                                  s_p2WakeOptionPicked);
    const bool tutorialP2Wake = playerNum == 2 &&
        triggerType == TRIGGER_ON_WAKEUP &&
        g_tutorialP2WakeToken.load(std::memory_order_acquire) != 0;
    TriggerOption poolPick{};
    if (reuseP1WakePick || reuseP2WakePick) {
        const TriggerOption& wakePick = reuseP1WakePick ? s_p1WakePrePickedOption : s_p2WakePrePickedOption;
        dstate.chosenAction = wakePick.action;
        dstate.chosenStrength = wakePick.strength;
        dstate.chosenMacroSlot = wakePick.macroSlot;
        dstate.chosenCustomId = wakePick.customId;
        dstate.chosenDelay = wakePick.delay;
        dstate.chosenChargeFollowup = CLAMP(wakePick.chargeFollowup, 0, 2);
        delayFrames = ResolvePickedDelayForTrigger(triggerType, delayFrames, wakePick.delay);
        LogOut("[AUTO-ACTION] Wake pre-picked option reused: action=" +
               std::to_string(wakePick.action) +
               " strength=" + std::to_string(wakePick.strength) +
               " macroSlot=" + std::to_string(wakePick.macroSlot) +
               " delayF=" + std::to_string(delayFrames), true);
    } else if (!tutorialP2Wake &&
               PickRandomPoolOption(triggerType, playerNum, poolPick)) {
        dstate.chosenAction = poolPick.action;
        dstate.chosenStrength = poolPick.strength;
        dstate.chosenMacroSlot = poolPick.macroSlot;
        dstate.chosenCustomId = poolPick.customId;
        dstate.chosenDelay = poolPick.delay;
        dstate.chosenChargeFollowup = CLAMP(poolPick.chargeFollowup, 0, 2);
        delayFrames = ResolvePickedDelayForTrigger(triggerType, delayFrames, poolPick.delay);
        LogOut("[AUTO-ACTION] Pool-selected option applied: action=" + std::to_string(poolPick.action) +
               " strength=" + std::to_string(poolPick.strength) +
               " userDelayF=" + std::to_string(poolPick.delay) +
               " resolvedDelayF=" + std::to_string(delayFrames), true);
    } else if (!tutorialP2Wake && HasEnabledRows(triggerType)) {
        TriggerOption picked{};
        if (PickRandomRow(triggerType, playerNum, picked)) {
            dstate.chosenAction    = picked.action;
            dstate.chosenStrength  = picked.strength;
            dstate.chosenMacroSlot = picked.macroSlot;
            dstate.chosenCustomId  = picked.customId;
            dstate.chosenDelay     = picked.delay;
            dstate.chosenChargeFollowup = CLAMP(
                picked.chargeFollowup, 0, 2);
            delayFrames = ResolvePickedDelayForTrigger(triggerType, delayFrames, picked.delay);
            LogOut("[AUTO-ACTION] Row-selected option applied: action=" + std::to_string(picked.action) +
                   " strength=" + std::to_string(picked.strength) +
                   " macroSlot=" + std::to_string(picked.macroSlot) +
                   " userDelayF=" + std::to_string(picked.delay) +
                   " resolvedDelayF=" + std::to_string(delayFrames), true);
        }
    }

    const int selectedAction = dstate.chosenAction >= 0
        ? dstate.chosenAction
        : (triggerType == TRIGGER_AFTER_BLOCK ? triggerAfterBlockAction.load()
           : triggerType == TRIGGER_ON_WAKEUP ? triggerOnWakeupAction.load()
           : triggerType == TRIGGER_AFTER_HITSTUN ? triggerAfterHitstunAction.load()
           : triggerType == TRIGGER_AFTER_AIRTECH ? triggerAfterAirtechAction.load()
           : triggerType == TRIGGER_ON_RG ? triggerOnRGAction.load()
           : ACTION_5A);
    const int selectedMacroSlot = dstate.chosenMacroSlot > 0
        ? dstate.chosenMacroSlot
        : TriggerMacroSlotForType(triggerType);
    if (selectedMacroSlot > 0 ||
        !ActionSupportsChargeFollowup(selectedAction) ||
        tutorialP2Wake) {
        dstate.chosenChargeFollowup = 0;
    }

    if (delayFrames == 0) {
        uintptr_t base = GetEFZBase();
        uintptr_t moveIDAddr = 0;
        if (base) {
            moveIDAddr = (playerNum == 1)
                ? ResolvePointer(base, EFZ_BASE_OFFSET_P1, MOVE_ID_OFFSET)
                : ResolvePointer(base, EFZ_BASE_OFFSET_P2, MOVE_ID_OFFSET);
        }
        {
            int sel = selectedMacroSlot;
            if (sel > 0 && MacroController::GetState() != MacroController::State::Recording) {
                sel = CLAMP(sel, 1, MacroController::GetSlotCount());
                if (!MacroController::IsSlotEmpty(sel)) {
                    MacroController::SetCurrentSlot(sel);
                    if (!(MacroController::GetState() == MacroController::State::Replaying &&
                          MacroController::GetPlaybackPlayer() == playerNum)) {
                        LogOut("[AUTO-ACTION][MACRO] Starting macro playback (slot=" + std::to_string(sel) +
                               ") for P" + std::to_string(playerNum) + " (immediate)", true);
                        const bool started = StartTriggerMacroPlayback(playerNum);
                        if (started &&
                            MacroController::GetState() == MacroController::State::Replaying) {
                            return;
                        }
                        dstate.isDelaying = true;
                        dstate.delayFramesRemaining = 1;
                        dstate.pendingMoveID = 0;
                        LogOut("[AUTO-ACTION][MACRO] Immediate playback owner busy; retaining the same slot", true);
                        return;
                    }
                }
            }
        }
        uint64_t motionGeneration = 0;
        uint64_t normalGeneration = 0;
        uint64_t recipeGeneration = 0;
        const AutoActionApplyResult result =
            ApplyAutoAction(playerNum, moveIDAddr, 0, 0,
                            &motionGeneration, &normalGeneration,
                            &recipeGeneration);
        if (result == AutoActionApplyResult::Accepted) {
            if (recipeGeneration != 0) {
                dstate.isDelaying = true;
                dstate.delayFramesRemaining = 1;
                dstate.pendingMoveID = 0;
                dstate.pendingRecipeGeneration = recipeGeneration;
                dstate.dispatchAttempts = 1;
                LogOut(std::string("[AUTO-ACTION] Immediate P") +
                           std::to_string(playerNum) +
                           " staged recipe admitted gen=" +
                           std::to_string(recipeGeneration) +
                           "; waiting for its exact destination",
                       true);
            } else if (motionGeneration != 0) {
                dstate.isDelaying = true;
                dstate.delayFramesRemaining = 1;
                dstate.pendingMoveID = 0;
                dstate.pendingMotionGeneration = motionGeneration;
                dstate.dispatchAttempts = 1;
                LogOut(std::string("[AUTO-ACTION] Immediate P") +
                           std::to_string(playerNum) +
                           " native transaction admitted gen=" +
                           std::to_string(motionGeneration) +
                           "; waiting for EFZ's command consumer",
                       true);
            } else if (normalGeneration != 0) {
                dstate.isDelaying = true;
                dstate.delayFramesRemaining = 1;
                dstate.pendingMoveID = 0;
                dstate.pendingNormalGeneration = normalGeneration;
                dstate.pendingWaitTicks = 0;
                dstate.dispatchAttempts = 1;
                LogOut(std::string("[AUTO-ACTION] Immediate P") +
                           std::to_string(playerNum) +
                           " normal admitted gen=" +
                           std::to_string(normalGeneration) +
                           "; waiting for EFZ's character consumer",
                       true);
            } else {
                LogOut(std::string("[AUTO-ACTION] Immediate apply accepted for P") +
                           std::to_string(playerNum),
                       detailedLogging.load());
            }
        } else if (result == AutoActionApplyResult::Busy) {
            dstate.isDelaying = true;
            dstate.delayFramesRemaining = 1;
            dstate.pendingMoveID = 0;
            LogOut(std::string("[AUTO-ACTION] Immediate P") +
                       std::to_string(playerNum) +
                       " owner busy; retrying the same option next tick",
                   true);
        } else {
            LogOut(std::string("[AUTO-ACTION] Immediate P") +
                       std::to_string(playerNum) +
                       " option invalid; retiring trigger",
                   true);
            ResetDelayState(dstate);
            if (playerNum == 1) {
                p1TriggerActive = false;
                p1TriggerCooldown = 0;
            } else {
                p2TriggerActive = false;
                p2TriggerCooldown = 0;
            }
        }
    }
    else {
        int internalFrames = VisualFramesToInternalTicks(delayFrames);
        
        if (playerNum == 1) {
            p1DelayState.isDelaying = true;
            p1DelayState.delayFramesRemaining = internalFrames;
            p1DelayState.pendingMoveID = 0;
            LogOut("[DELAY] Armed P1 delay: visualFrames=" + std::to_string(delayFrames) +
                   ", internalFrames=" + std::to_string(internalFrames) +
                   ", triggerType=" + std::to_string(triggerType) +
                   ", expiresAtInternal=" + std::to_string(frameCounter.load() + internalFrames),
                   true);
        } else {
            p2DelayState.isDelaying = true;
            p2DelayState.delayFramesRemaining = internalFrames;
            p2DelayState.pendingMoveID = 0;
            LogOut("[DELAY] Armed P2 delay: visualFrames=" + std::to_string(delayFrames) +
                   ", internalFrames=" + std::to_string(internalFrames) +
                   ", triggerType=" + std::to_string(triggerType) +
                   ", expiresAtInternal=" + std::to_string(frameCounter.load() + internalFrames),
                   true);
        }
    }
}

void ProcessTriggerDelays() {
    const PerFrameSample &sample = GetCurrentPerFrameSample();
    ProcessTriggerDelays(sample.moveID1, sample.moveID2, sample.prevMoveID1, sample.prevMoveID2);
}

void ProcessTriggerCooldowns() {
    std::lock_guard<std::recursive_mutex> stateLock(g_autoActionStateMutex);
    if (detailedLogging.load()) {
        static int s_nextCooldownDiagFrame = 0;
        int now = frameCounter.load();
        if (now >= s_nextCooldownDiagFrame) {
            s_nextCooldownDiagFrame = now + 960;
        }
    }
    if (p1TriggerActive && p1TriggerCooldown > 0) {
        if (!p1DelayState.isDelaying) {
            p1TriggerCooldown--;
            if (p1TriggerCooldown <= 0) {
                p1TriggerActive = false;
                LogOut("[AUTO-ACTION] P1 trigger cooldown expired, new triggers allowed", detailedLogging.load());
            }
        }
    }
    if (p2TriggerActive && p2TriggerCooldown > 0) {
        if (!p2DelayState.isDelaying) {
            p2TriggerCooldown--;
            if (p2TriggerCooldown <= 0) {
                p2TriggerActive = false;
                LogOut("[AUTO-ACTION] P2 trigger cooldown expired, new triggers allowed", detailedLogging.load());
            }
        }
    }
}

static void MonitorAutoActionsImpl(short moveID1, short moveID2, short prevMoveID1, short prevMoveID2) {
    if (GetCurrentGameMode() != GameMode::Practice) return;
    if (IsNetplaySuspendActive()) return;
    
    if (!AutoActionWorkPending()) {
        return;
    }
    // Skip idle work while nothing is pending and both move IDs are unchanged.
    // Never skip during P2 groundtech: wake macro timing counts ticks there.
    bool p2InGroundtech = IsGroundtech(moveID2);
    if (!p1DelayState.isDelaying && !p2DelayState.isDelaying &&
        !g_pendingControlRestore.load() && g_dashDeferred.pendingSel.load() == 0 &&
        !s_p1WakePrearmed && !s_p2WakePrearmed && !s_p1RGPrearmed && !s_p2RGPrearmed &&
        p1TriggerCooldown <= 0 && p2TriggerCooldown <= 0 &&
        !WakeCleanupWorkPending() &&
        !p2InGroundtech &&
        moveID1 == prevMoveID1 && moveID2 == prevMoveID2) {
        return;
    }
    ProcessTriggerCooldowns();

    MaybeBeginFlowSequence(1, prevMoveID1, moveID1);
    MaybeBeginFlowSequence(2, prevMoveID2, moveID2);

    UpdateStunTimersForPlayer(s_p1Timers, prevMoveID1, moveID1);
    UpdateStunTimersForPlayer(s_p2Timers, prevMoveID2, moveID2);

    const bool tutorialP2Wake =
        g_tutorialP2WakeToken.load(std::memory_order_acquire) != 0;
    const int targetPlayer =
        Mission::TutorialEpisodePolicy::ResolveRuntimeAutoActionTarget(
            Mission::TutorialSession::IsActive(), tutorialP2Wake,
            ResolveAutoActionTargetPlayer());
    static int s_nextTrigDiagFrame = 0;
    auto canLogTrigDiag = [&]() -> bool {
        int now = frameCounter.load();
        if (now >= s_nextTrigDiagFrame) {
            s_nextTrigDiagFrame = now + 960;
            return true;
        }
        return false;
    };
    
    if ((targetPlayer == 1 || targetPlayer == 3) && !p1DelayState.isDelaying && !p1ActionApplied) {
        bool shouldTrigger = false;
        int triggerType = TRIGGER_NONE;
        int delay = 0;
        short actionMoveID = 0;        
    if (detailedLogging.load() && canLogTrigDiag()) {
        }
        
    if (!shouldTrigger && triggerAfterAirtechEnabled.load()) {
            bool wasInAirtech = IsAirtech(prevMoveID1);
            bool postAirtechNow = (!IsAirtech(moveID1)) && (IsActionable(moveID1) || moveID1 == FALLING_ID);
            if (detailedLogging.load() && canLogTrigDiag()) {
            }

            if (wasInAirtech && postAirtechNow) {
                if (triggerRandomizeEnabled.load()) { if ((rand() & 1) == 0) { if (detailedLogging.load() && canLogTrigDiag()) LogOut("[AUTO-ACTION] P1 After Airtech skipped by random gate", true); goto p1_after_airtech_done; } }
                LogOut("[AUTO-ACTION] P1 After Airtech trigger activated (from moveID " + 
                       std::to_string(prevMoveID1) + " to " + std::to_string(moveID1) + ")", detailedLogging.load());
                shouldTrigger = true;
                triggerType = TRIGGER_AFTER_AIRTECH;
                delay = triggerAfterAirtechDelay.load();

                int actionType = triggerAfterAirtechAction.load();
                actionMoveID = GetActionMoveID(actionType, TRIGGER_AFTER_AIRTECH, 1);
            }
        p1_after_airtech_done: ;
        }


    if (!shouldTrigger && triggerAfterBlockEnabled.load()) {
            bool wasInBlockstun = IsBlockstun(prevMoveID1);
            bool nowNotInBlockstun = !IsBlockstun(moveID1);
            bool wasNotActionable = !IsActionable(prevMoveID1);
            bool isNowActionable = IsActionable(moveID1);
            bool justBecameActionable = wasNotActionable && isNowActionable;

            if (wasInBlockstun && nowNotInBlockstun && justBecameActionable) {
                if (triggerRandomizeEnabled.load()) { if ((rand() & 1) == 0) { if (detailedLogging.load() && canLogTrigDiag()) LogOut("[AUTO-ACTION] P1 After Block skipped by random gate", true); goto p1_after_block_done; } }
                shouldTrigger = true;
                triggerType = TRIGGER_AFTER_BLOCK;
                delay = triggerAfterBlockDelay.load();
                actionMoveID = GetActionMoveID(triggerAfterBlockAction.load(), TRIGGER_AFTER_BLOCK, 1);
                LogOut("[AUTO-ACTION] P1 After Block trigger activated (just became actionable)", true);
                if (detailedLogging.load()) {
                    LogOut("[AUTO-ACTION] P1 After Block trigger activated (just became actionable)", true);
                    LogOut("[TRIGGER_TIMING] P1 blockstun dur=" + std::to_string(s_p1Timers.lastBlockDuration) +
                           ", sinceEnd=" + std::to_string(s_p1Timers.sinceBlockEnd), true);
                }
            }
        p1_after_block_done: ;
        }

    if (!shouldTrigger && triggerOnRGEnabled.load()) {
            bool enteredRG = (!IsRecoilGuard(prevMoveID1) && IsRecoilGuard(moveID1));
            if (enteredRG) {
        if (triggerRandomizeEnabled.load()) { if ((rand() & 1) == 0) { if (detailedLogging.load() && canLogTrigDiag()) LogOut("[AUTO-ACTION] P1 On RG skipped by random gate", true); goto p1_onrg_done; } }
                int baseDelayF = 20;
                if (moveID1 == RG_STAND_ID) baseDelayF = RG_STAND_FREEZE_DEFENDER;
                else if (moveID1 == RG_CROUCH_ID) baseDelayF = RG_CROUCH_FREEZE_DEFENDER;
                else if (moveID1 == RG_AIR_ID) baseDelayF = RG_AIR_FREEZE_DEFENDER;
                int userDelayF = triggerOnRGDelay.load();
                int totalDelayF = baseDelayF + userDelayF;
                if (totalDelayF < 0) totalDelayF = 0;
                LogOut("[TRIGGER_DIAG] P1 entered RG (" + std::to_string(moveID1) + ") baseDelay=" + std::to_string(baseDelayF) +
                       "F + user=" + std::to_string(userDelayF) + "F => total=" + std::to_string(totalDelayF) + "F", true);
                StartTriggerDelay(1, TRIGGER_ON_RG, 0, totalDelayF);
                const int selectedUserDelayF = (p1DelayState.chosenDelay >= 0)
                    ? p1DelayState.chosenDelay
                    : userDelayF;

                if (selectedUserDelayF == 0 && !s_p1RGPrearmed &&
                    p1DelayState.chosenChargeFollowup == 0) {
                    int at = (p1DelayState.chosenAction >= 0) ? p1DelayState.chosenAction : triggerOnRGAction.load();
                    int selectedStrength = (p1DelayState.chosenStrength >= 0)
                        ? p1DelayState.chosenStrength
                        : GetSpecialMoveStrength(at, TRIGGER_ON_RG);
                    int motion = ConvertTriggerActionToMotion(at, TRIGGER_ON_RG, selectedStrength);
                    int buttonMask = 0;
                    if ((at >= ACTION_5A && at <= ACTION_2D) || (at >= ACTION_6A && at <= ACTION_4D)) {
                        int button;
                        if (at >= ACTION_5A && at <= ACTION_2D) {
                            button = (at - ACTION_5A) % 4;
                        } else {
                            int offset = at - ACTION_6A;
                            button = offset % 4;
                        }
                        buttonMask = (1 << (4 + button));
                    } else if (at >= ACTION_JA && at <= ACTION_JD) {
                        int button = (at - ACTION_JA) % 4;
                        buttonMask = (1 << (4 + button));
                    } else if (IsStrengthButtonAction(at)) {
                        buttonMask = (1 << (4 + CLAMP(selectedStrength, 0, 3)));
                    }
                    bool preOk = false;
                    const bool isDash =
                        motion == MOTION_FORWARD_DASH || motion == MOTION_BACK_DASH;
                    bool isSpec = ((motion >= MOTION_236A) && !isDash) ||
                                  (at == ACTION_FINAL_MEMORY);
                    if (isSpec) {
                        uint64_t prearmGeneration = 0;
                        if (at == ACTION_FINAL_MEMORY) {
                            int charId = displayData.p1CharID;
                            preOk = ExecuteFinalMemory(
                                1, charId, 120, &prearmGeneration);
                        } else {
                            g_injectImmediateOnly[1].store(false);
                            preOk = SubmitAutoActionMotionTransaction(
                                        1, motion, buttonMask, 120,
                                        &prearmGeneration) ==
                                    AutoActionMotionSubmitResult::Accepted;
                        }
                        if (preOk) {
                            s_p1RGPrearmIsSpecial = true;
                            s_p1RGPrearmGeneration = prearmGeneration;
                        } else {
                            s_p1RGPrearmIsSpecial = false;
                            s_p1RGPrearmGeneration = 0;
                        }
                    }
                    if (preOk) {
                        s_p1RGPrearmed = true;
                        s_p1RGPrearmActionType = at;
                        s_p1RGPrearmExpiry = GetEngineTick() + (baseDelayF * 3) + 90;
                        LogOut("[AUTO-ACTION][RG] P1 pre-armed at RG entry: action=" + std::to_string(at) +
                               " motion=" + std::to_string(motion) + " btnMask=" + std::to_string(buttonMask) +
                               " (scoped transaction)", true);
                    } else if (isSpec) {
                        s_p1RGPrearmIsSpecial = false;
                        s_p1RGPrearmGeneration = 0;
                        LogOut("[AUTO-ACTION][RG] P1 pre-arm failed; will attempt on delay expiry", true);
                    }
                }
            }
        p1_onrg_done: ;
        }
        
    if (!shouldTrigger && triggerAfterHitstunEnabled.load()) {
            bool wasInHitstun = IsHitstun(prevMoveID1);
            bool nowNotInHitstun = !IsHitstun(moveID1);
            bool wasNotActionable = !IsActionable(prevMoveID1);
            bool isNowActionable = IsActionable(moveID1);
            bool justBecameActionable = wasNotActionable && isNowActionable;

            if (wasInHitstun && nowNotInHitstun && justBecameActionable) {
                if (triggerRandomizeEnabled.load()) { if ((rand() & 1) == 0) { if (detailedLogging.load() && canLogTrigDiag()) LogOut("[AUTO-ACTION] P1 After Hitstun skipped by random gate", true); goto p1_after_hitstun_done; } }
                shouldTrigger = true;
                triggerType = TRIGGER_AFTER_HITSTUN;
                delay = triggerAfterHitstunDelay.load();
                actionMoveID = GetActionMoveID(triggerAfterHitstunAction.load(), TRIGGER_AFTER_HITSTUN, 1);
                LogOut("[AUTO-ACTION] P1 After Hitstun trigger activated (just became actionable)", true);
                if (detailedLogging.load()) {
                    LogOut("[AUTO-ACTION] P1 After Hitstun trigger activated (just became actionable)", true);
                    LogOut("[TRIGGER_TIMING] P1 hitstun dur=" + std::to_string(s_p1Timers.lastHitDuration) +
                           ", sinceEnd=" + std::to_string(s_p1Timers.sinceHitEnd), true);
                }
            }
        p1_after_hitstun_done: ;
        }
        
        // Pick the wake option on groundtech entry, independent of pre-arm and the Pre-buffer Wakeup
        // toggle; both wake paths consume this pick, so the buffered action cannot diverge from the armed one.
        const bool p1EnteringGroundtech = triggerOnWakeupEnabled.load() &&
            IsGroundtech(moveID1) && !IsGroundtech(prevMoveID1);
        if (p1EnteringGroundtech) {
            s_p1WakeOptionPicked = false;
            s_p1WakePrePickedOption = {};
            if (PickRandomPoolOption(TRIGGER_ON_WAKEUP, 1, s_p1WakePrePickedOption)) {
                s_p1WakeOptionPicked = true;
                TraceAutoActionWakeEvent("p1-wake-pool-picked", 1, prevMoveID1, moveID1,
                                         "delay=" + std::to_string(s_p1WakePrePickedOption.delay) +
                                         " action=" + std::to_string(s_p1WakePrePickedOption.action) +
                                         " strength=" + std::to_string(s_p1WakePrePickedOption.strength));
            } else if (HasEnabledRows(TRIGGER_ON_WAKEUP) &&
                       PickRandomRow(TRIGGER_ON_WAKEUP, 1,
                                     s_p1WakePrePickedOption)) {
                s_p1WakeOptionPicked = true;
                TraceAutoActionWakeEvent("p1-wake-option-picked", 1, prevMoveID1, moveID1,
                                         "macroSlot=" + std::to_string(s_p1WakePrePickedOption.macroSlot) +
                                         " delay=" + std::to_string(s_p1WakePrePickedOption.delay) +
                                         " action=" + std::to_string(s_p1WakePrePickedOption.action) +
                                         " strength=" + std::to_string(s_p1WakePrePickedOption.strength));
            }
        }

    if (triggerOnWakeupEnabled.load() && !s_p1WakePrearmed) {
            if (moveID1 == GROUNDTECH_RECOVERY) {
                if (triggerRandomizeEnabled.load()) { if ((rand() & 1) == 0) { if (detailedLogging.load() && canLogTrigDiag()) LogOut("[AUTO-ACTION] P1 Wake prearm skipped by random gate", true); goto p1_wake_prearm_done; } }

                int userDelayF = s_p1WakeOptionPicked ? s_p1WakePrePickedOption.delay : triggerOnWakeupDelay.load();
                if (s_p1WakeOptionPicked) {
                    userDelayF = (userDelayF <= 1) ? 0 : (userDelayF - 1);
                }
                int actionType = s_p1WakeOptionPicked ? s_p1WakePrePickedOption.action : triggerOnWakeupAction.load();
                int chosenStrength = s_p1WakeOptionPicked ? s_p1WakePrePickedOption.strength : triggerOnWakeupStrength.load();
                chosenStrength = CLAMP(chosenStrength, 0, 3);
                int motionType = ConvertTriggerActionToMotion(actionType, TRIGGER_ON_WAKEUP, chosenStrength);
                int macroSlot = s_p1WakeOptionPicked ? s_p1WakePrePickedOption.macroSlot : TriggerMacroSlotForType(TRIGGER_ON_WAKEUP);
                int macroSel = CLAMP(macroSlot, 1, MacroController::GetSlotCount());
                bool macroHasData = (macroSlot > 0) && MacroController::GetState() != MacroController::State::Recording && !MacroController::IsSlotEmpty(macroSel);
                bool isDash = (motionType == MOTION_FORWARD_DASH || motionType == MOTION_BACK_DASH);
                bool isSpecial = (actionType == ACTION_FINAL_MEMORY) ||
                                 (motionType >= MOTION_236A && !isDash);
                const int chargeFollowup = s_p1WakeOptionPicked
                    ? s_p1WakePrePickedOption.chargeFollowup
                    : triggerOnWakeupCharge.load();
                const bool dedicatedMacroOwnsWake = macroHasData && (userDelayF == 0);
                bool isMacro = dedicatedMacroOwnsWake && g_wakeBufferingEnabled.load();
                bool holdEligible = (userDelayF == 0) && !isSpecial && !dedicatedMacroOwnsWake &&
                                    SupportsImmediateWakeHold(actionType);
                const bool shouldPrearm = (userDelayF == 0) &&
                    (isMacro || (!dedicatedMacroOwnsWake && (isSpecial || holdEligible)));
                if (shouldPrearm) {
                    s_p1WakePrearmed = true;
                    // A wake macro is the complete action: its row's move must not become a second wake producer
                    // or reserve an IC/FIC lane ahead of playback.
                    s_p1WakePrearmIsSpecial = isSpecial && !isMacro;
                    s_p1WakePrearmIsMacro = isMacro;
                    s_p1WakePrearmActionType = actionType;
                    s_p1WakePrearmStrength = chosenStrength;
                    s_p1WakePrearmCharge =
                        s_p1WakePrearmIsSpecial &&
                                ActionSupportsChargeFollowup(actionType)
                            ? CLAMP(chargeFollowup, 0, 2)
                            : 0;
                    s_p1WakePrearmExpiry = GetEngineTick() + 120;
                    s_p1WakeMoveID96FrameCount = 0;
                    s_p1WakeBufferFrozen = false;
                    s_p1WakeMotionGeneration = 0;
                    s_p1WakeChargeReservation = 0;
                    s_p1WakeHoldPrimed = holdEligible;
                    s_p1WakeHoldIssued = false;
                    s_p1WakeNormalGeneration = 0;
                    s_p1WakeNormalAttempts = 0;
                    s_p1WakeMacro96FrameCount = 0;
                    s_p1WakeMacroQueued = false;
                    s_p1WakeMacroTargetFrame = -1;
                    s_p1WakeMacroSlot = -1;
                    s_p1WakeMacroStartTick = 0;
                    const char* tag = isMacro ? "macro" :
                        (s_p1WakePrearmIsSpecial ? "special" : "hold");
                    LogOut(std::string("[AUTO-ACTION] P1 wake ") + tag + " metadata stored", true);
                    TraceAutoActionWakeEvent("p1-prearm-stored", 1, prevMoveID1, moveID1,
                                             "kind=" + std::string(tag) +
                                             " action=" + std::to_string(actionType) +
                                             " strength=" + std::to_string(s_p1WakePrearmStrength) +
                                             " chargeWatcher=" + std::to_string(s_p1WakePrearmCharge) +
                                             " delayF=" + std::to_string(userDelayF));
                    LogFlowSequenceEvent(1, "wake metadata stored", prevMoveID1, moveID1);
                } else if (detailedLogging.load()) {
                    std::string reason;
                    if (dedicatedMacroOwnsWake) {
                        reason = "macro row (generic On-Wakeup trigger plays it on the first actionable frame)";
                    } else if (userDelayF != 0) {
                        reason = "delay>0";
                    } else {
                        reason = "unsupported action";
                    }
                    LogOut("[AUTO-ACTION] P1 wake pre-arm skipped (" + reason + ")", true);
                }
            p1_wake_prearm_done: ;
            }
        }

        bool p1BecameActionableFromGroundtech = IsGroundtech(prevMoveID1) && IsActionable(moveID1);
        if (s_p1WakePrearmed) {
            if (moveID1 == GROUNDTECH_RECOVERY) {
                s_p1WakeMoveID96FrameCount++;
                s_p1WakeMacro96FrameCount++;
                int charID = displayData.p1CharID;
                int risingTicks = GetWakeupRisingTicks(charID);
                int bufferFrame = risingTicks - 1;
                if (s_p1WakePrearmIsSpecial && !s_p1WakeBufferFrozen && 
                    s_p1WakeMoveID96FrameCount >= (bufferFrame - 2) && s_p1WakeMoveID96FrameCount <= bufferFrame) {
                    int at = s_p1WakePrearmActionType;
                    bool p1Facing = GetPlayerFacingDirection(1);
                    uint64_t wakeGeneration = 0;
                    uint64_t chargeReservation = 0;
                    bool ok = ExecuteWakeSpecialWithChargeWatcher(
                        1, at, moveID1, s_p1WakePrearmStrength,
                        s_p1WakePrearmCharge, &wakeGeneration,
                        &chargeReservation);
                    if (ok && at != ACTION_FINAL_MEMORY &&
                        wakeGeneration == 0) {
                        LogOut("[AUTO-ACTION] P1 wake early buffer returned no generation; treating dispatch as rejected",
                               true);
                        if (chargeReservation != 0) {
                            (void)CancelAutoActionChargeFollowupIfReservation(
                                1, chargeReservation,
                                "P1 wake primary admission returned no generation");
                            chargeReservation = 0;
                        }
                        ok = false;
                    }
                    LogOut("[AUTO-ACTION] P1 wake special early buffer (frame " + std::to_string(s_p1WakeMoveID96FrameCount) + "/" + std::to_string(risingTicks) + ", charID=" + std::to_string(charID) + ", facing=" + (p1Facing?"right":"left") + ") " + std::string(ok?"ok":"fail"), true);
                    TraceAutoActionWakeEvent("p1-early-special-freeze", 1, prevMoveID1, moveID1,
                                             "count96=" + std::to_string(s_p1WakeMoveID96FrameCount) +
                                             " risingTicks=" + std::to_string(risingTicks) +
                                             " bufferFrame=" + std::to_string(bufferFrame) +
                                             " charID=" + std::to_string(charID) +
                                             " action=" + std::to_string(at) +
                                             " ok=" + std::to_string(ok));
                    LogFlowSequenceEvent(1, "wake early buffer", prevMoveID1, moveID1);
                    s_p1WakeBufferFrozen = ok;
                    s_p1WakeMotionGeneration = ok ? wakeGeneration : 0;
                    s_p1WakeChargeReservation = ok ? chargeReservation : 0;
                }
                
                int macroSlot = s_p1WakeOptionPicked ? s_p1WakePrePickedOption.macroSlot : TriggerMacroSlotForType(TRIGGER_ON_WAKEUP);
                int userDelayF = s_p1WakeOptionPicked ? s_p1WakePrePickedOption.delay : triggerOnWakeupDelay.load();
                if (s_p1WakeOptionPicked) {
                    userDelayF = (userDelayF <= 1) ? 0 : (userDelayF - 1);
                }
                if (g_wakeBufferingEnabled.load() &&
                    macroSlot > 0 && userDelayF == 0 && !s_p1WakeMacroQueued) {
                    int risingFrames = GetWakeupRisingFrames(charID);
                    
                    int sel = CLAMP(macroSlot, 1, MacroController::GetSlotCount());
                    int firstButtonTick = MacroController::GetFirstButtonTick(sel);
                    
                    if (firstButtonTick < 0) {
                        s_p1WakeMacroTargetFrame = -1;
                        s_p1WakeMacroSlot = -1;
                        s_p1WakeMacroStartTick = 0;
                    } else {
                        int framesToButton = (firstButtonTick + 2) / 3;
                        
                        constexpr int kBufferMargin = 12;
                        int playStartFrame = risingFrames - framesToButton - kBufferMargin;
                        if (playStartFrame < 1) playStartFrame = 1;
                        
                        s_p1WakeMacroStartTick = 0;
                        s_p1WakeMacroTargetFrame = playStartFrame * 3;
                        s_p1WakeMacroSlot = macroSlot;
                        
                        if (detailedLogging.load() && s_p1WakeMacro96FrameCount == 1) {
                            LogOut("[AUTO-ACTION][MACRO-TIMING-INIT] P1 charID=" + std::to_string(charID) +
                                   " risingFrames=" + std::to_string(risingFrames) +
                                   " firstBtnTick=" + std::to_string(firstButtonTick) +
                                   " framesToBtn=" + std::to_string(framesToButton) +
                                   " playStartFrame=" + std::to_string(playStartFrame), true);
                        }
                        
                        if (!s_p1WakeMacroQueued && s_p1WakeMacroSlot > 0) {
                            const bool macroPlaying =
                                (MacroController::GetState() == MacroController::State::Replaying);
                            if (!macroPlaying && s_wakeForcedNeutralToken[1] == 0) {
                                (void)TryPublishWakeForcedNeutral(1);
                            }
                        }

                        if (s_p1WakeMacro96FrameCount >= s_p1WakeMacroTargetFrame) {
                            if (MacroController::GetState() != MacroController::State::Recording) {
                                if (!MacroController::IsSlotEmpty(sel)) {
                                    MacroController::SetCurrentSlot(sel);
                                    LogOut("[AUTO-ACTION][MACRO] P1 Pre-buffering wake macro (slot=" + std::to_string(sel) +
                                           ") at frame " + std::to_string(s_p1WakeMacro96FrameCount) +
                                           " (target=" + std::to_string(playStartFrame) + ")", true);
                                    TraceAutoActionWakeEvent("p1-wake-macro-play", 1, prevMoveID1, moveID1,
                                                             "slot=" + std::to_string(sel) +
                                                             " count96=" + std::to_string(s_p1WakeMacro96FrameCount) +
                                                             " targetTick=" + std::to_string(s_p1WakeMacroTargetFrame));
                                    if (s_wakeForcedNeutralToken[1] != 0) {
                                        ReleaseWakeForcedNeutral(1);
                                    }
                                    const bool started = MacroController::PlayForPlayer(
                                        1, s_p1WakeMacroStartTick, false);
                                    if (started &&
                                        MacroController::GetState() == MacroController::State::Replaying) {
                                        g_macroWakePreservePlayer.store(1, std::memory_order_release);
                                        g_macroWakePreserveBuffer.store(true, std::memory_order_release);
                                        s_p1WakeMacroQueued = true;
                                        s_p1WakeMacroTargetFrame = -1;
                                        s_p1WakeMacroSlot = -1;
                                        s_p1WakeBufferFrozen = true;
                                        s_p1WakeMotionGeneration = 0;
                                    } else {
                                        g_macroWakePreserveBuffer.store(false, std::memory_order_release);
                                        g_macroWakePreservePlayer.store(0, std::memory_order_release);
                                        LogOut("[AUTO-ACTION][MACRO] P1 wake playback owner busy; retaining slot for retry",
                                               true);
                                    }
                                }
                            }
                        }
                    }
                }
            }
            // Must stay outside the moveID==96 block: this detects the 96 to 0 transition.
            if (!s_p1WakePrearmIsSpecial && !s_p1WakePrearmIsMacro &&
                s_p1WakeHoldPrimed && !s_p1WakeHoldIssued) {
                const bool leavingGroundtechThisFrame = (prevMoveID1 == GROUNDTECH_RECOVERY) && (moveID1 != GROUNDTECH_RECOVERY);
                if (leavingGroundtechThisFrame) {
                    if (IssueWakeImmediateHold(1, s_p1WakePrearmActionType)) {
                        s_p1WakeHoldIssued = true;
                        TraceAutoActionWakeEvent("p1-wake-hold-issued", 1, prevMoveID1, moveID1,
                                                 "action=" + std::to_string(s_p1WakePrearmActionType));
                    }
                }
            }
            bool actionableButLostGround = !IsGroundtech(prevMoveID1) && IsActionable(moveID1) && !IsGroundtech(moveID1);
            if (actionableButLostGround && !s_p1WakeActionableWarning) {
                LogOut("[AUTO-ACTION][FLOW] P1 wake actionable while prev state not groundtech (prev=" + std::to_string(prevMoveID1) +
                       ", curr=" + std::to_string(moveID1) + ")", true);
                LogFlowSequenceEvent(1, "wake actionable lost-ground", prevMoveID1, moveID1);
                s_p1WakeActionableWarning = true;
            } else if (!actionableButLostGround) {
                s_p1WakeActionableWarning = false;
            }
        } else {
            s_p1WakeActionableWarning = false;
        }

        if (!shouldTrigger && triggerOnWakeupEnabled.load()) {
            const auto finishP1WakePrearm = [&]() {
                p1TriggerActive = false;
                p1TriggerCooldown = 0;
                s_p1WakePrearmed = false;
                s_p1WakePrearmIsSpecial = false;
                s_p1WakePrearmIsMacro = false;
                s_p1WakePrearmActionType = -1;
                s_p1WakePrearmStrength = -1;
                s_p1WakePrearmCharge = 0;
                s_p1WakeMoveID96FrameCount = 0;
                s_p1WakeBufferFrozen = false;
                s_p1WakeMotionGeneration = 0;
                s_p1WakeChargeReservation = 0;
                s_p1WakeHoldPrimed = false;
                s_p1WakeHoldIssued = false;
                s_p1WakeNormalGeneration = 0;
                s_p1WakeNormalAttempts = 0;
                s_p1WakeMacro96FrameCount = 0;
                s_p1WakeMacroQueued = false;
                s_p1WakeMacroTargetFrame = -1;
                s_p1WakeMacroSlot = -1;
                s_p1WakeOptionPicked = false;
                s_p1WakePrePickedOption = {};
            };
            bool p1WakeMotionAccepted = false;
            bool p1WakeNormalAccepted = false;
            bool p1WakeNormalExhausted = false;
            if (s_p1WakeNormalGeneration != 0) {
                const AutoActionNormalPulseOutcome outcome =
                    GetAutoActionNormalPulseOutcome(
                        1, s_p1WakeNormalGeneration);
                if (outcome == AutoActionNormalPulseOutcome::Accepted) {
                    p1WakeNormalAccepted = true;
                } else if (outcome != AutoActionNormalPulseOutcome::Pending) {
                    LogOut("[AUTO-ACTION] P1 wake normal gen=" +
                               std::to_string(s_p1WakeNormalGeneration) +
                               " was not consumed; retaining wake option for retry",
                           true);
                    s_p1WakeNormalGeneration = 0;
                    s_p1WakeHoldIssued = false;
                    if (s_p1WakeChargeReservation != 0) {
                        (void)CancelAutoActionChargeFollowupIfReservation(
                            1, s_p1WakeChargeReservation,
                            "P1 wake primary normal was not accepted");
                        s_p1WakeChargeReservation = 0;
                    }
                    p1WakeNormalExhausted =
                        s_p1WakeNormalAttempts >=
                        kMaxAutoActionDispatchAttempts;
                }
            }
            if (s_p1WakePrearmed && s_p1WakePrearmIsSpecial &&
                s_p1WakeBufferFrozen && s_p1WakeMotionGeneration != 0) {
                const AutoActionMotionOutcome outcome =
                    GetAutoActionMotionTransactionOutcome(
                        1, s_p1WakeMotionGeneration);
                if (outcome == AutoActionMotionOutcome::Accepted) {
                    p1WakeMotionAccepted = true;
                } else if (outcome != AutoActionMotionOutcome::Pending) {
                    LogOut("[AUTO-ACTION] P1 wake motion generation=" +
                               std::to_string(s_p1WakeMotionGeneration) +
                               " did not execute; cancelling its passive charge watcher before retry",
                           true);
                    if (s_p1WakeChargeReservation != 0) {
                        (void)CancelAutoActionChargeFollowupIfReservation(
                            1, s_p1WakeChargeReservation,
                            "P1 wake primary motion was not accepted");
                        s_p1WakeChargeReservation = 0;
                    }
                    s_p1WakeBufferFrozen = false;
                    s_p1WakeMotionGeneration = 0;
                }
            }

            if (s_p1WakePrearmed && p1WakeNormalAccepted) {
                LogOut("[AUTO-ACTION] P1 wake normal was consumed by EFZ", true);
                s_p1WakeChargeReservation = 0;
                finishP1WakePrearm();
            } else if (s_p1WakePrearmed && p1WakeNormalExhausted) {
                LogOut("[AUTO-ACTION] P1 wake normal exhausted bounded retries", true);
                if (s_p1WakeChargeReservation != 0) {
                    (void)CancelAutoActionChargeFollowupIfReservation(
                        1, s_p1WakeChargeReservation,
                        "P1 wake normal exhausted retries");
                    s_p1WakeChargeReservation = 0;
                }
                finishP1WakePrearm();
            } else if (s_p1WakePrearmed && s_p1WakePrearmIsSpecial &&
                       p1WakeMotionAccepted) {
                LogFlowSequenceEvent(1, "wake exec (native consumer accepted)",
                                     prevMoveID1, moveID1);
                s_p1WakeChargeReservation = 0;
                finishP1WakePrearm();
            } else if (s_p1WakePrearmed && s_p1WakeBufferFrozen &&
                       s_p1WakeMotionGeneration == 0 &&
                       p1BecameActionableFromGroundtech) {
                LogFlowSequenceEvent(1, "wake exec (buffered early)", prevMoveID1, moveID1);
                s_p1WakeChargeReservation = 0;
                finishP1WakePrearm();
            } else if (s_p1WakePrearmed && s_p1WakePrearmIsMacro &&
                       !s_p1WakeMacroQueued &&
                       (p1BecameActionableFromGroundtech || IsActionable(moveID1))) {
                const int slot = s_p1WakeOptionPicked
                    ? s_p1WakePrePickedOption.macroSlot
                    : TriggerMacroSlotForType(TRIGGER_ON_WAKEUP);
                const int sel = CLAMP(slot, 1, MacroController::GetSlotCount());
                if (MacroController::GetState() != MacroController::State::Recording &&
                    !MacroController::IsSlotEmpty(sel)) {
                    MacroController::SetCurrentSlot(sel);
                    if (s_wakeForcedNeutralToken[1] != 0) {
                        ReleaseWakeForcedNeutral(1);
                    }
                    const bool started = MacroController::PlayForPlayer(1, 0, false);
                    if (started &&
                        MacroController::GetState() == MacroController::State::Replaying) {
                        LogOut("[AUTO-ACTION][MACRO] P1 wake macro frame-1 playback (slot=" +
                               std::to_string(sel) + ")", true);
                        TraceAutoActionWakeEvent("p1-wake-macro-frame1", 1, prevMoveID1, moveID1,
                                                 "slot=" + std::to_string(sel));
                        finishP1WakePrearm();
                    } else {
                        LogOut("[AUTO-ACTION][MACRO] P1 wake macro frame-1 playback owner busy; retaining pre-arm for retry",
                               true);
                    }
                }
            } else if (s_p1WakePrearmed && !s_p1WakePrearmIsSpecial &&
                       s_p1WakeHoldIssued &&
                       s_p1WakeNormalGeneration == 0 &&
                       p1BecameActionableFromGroundtech) {
                LogOut("[AUTO-ACTION] P1 wake hold exec (inputs maintained through wake)", true);
                LogFlowSequenceEvent(1, "wake hold exec", prevMoveID1, moveID1);
                finishP1WakePrearm();
            } else if (s_p1WakePrearmed && !s_p1WakePrearmIsMacro &&
                       !s_p1WakeBufferFrozen &&
                       s_p1WakeNormalGeneration == 0 &&
                       (p1BecameActionableFromGroundtech || IsActionable(moveID1))) {
                AutoActionLogScope wakeScope("WakeImmediateExec", 1, TRIGGER_ON_WAKEUP);
                LogOut("[AUTO-ACTION] P1 wake exec (fallback): action=" + std::to_string(s_p1WakePrearmActionType) +
                       " special=" + std::to_string(s_p1WakePrearmIsSpecial) +
                       " frame=" + std::to_string(frameCounter.load()), true);
                    TraceAutoActionWakeEvent("p1-wake-fallback", 1, prevMoveID1, moveID1,
                                             "action=" + std::to_string(s_p1WakePrearmActionType) +
                                             " special=" + std::to_string(s_p1WakePrearmIsSpecial));
                    LogFlowSequenceEvent(1, "wake exec attempt", prevMoveID1, moveID1);
                    g_lastActiveTriggerType.store(TRIGGER_ON_WAKEUP);
                    g_lastActiveTriggerFrame.store(frameCounter.load());
                    bool dispatchAccepted = false;
                    if (s_p1WakePrearmActionType >= 0) {
                    int at = s_p1WakePrearmActionType;
                    if (s_p1WakePrearmIsSpecial) {
                        uint64_t wakeGeneration = 0;
                        uint64_t chargeReservation = 0;
                        bool ok = ExecuteWakeSpecialWithChargeWatcher(
                            1, at, moveID1, s_p1WakePrearmStrength,
                            s_p1WakePrearmCharge, &wakeGeneration,
                            &chargeReservation);
                        if (ok && at != ACTION_FINAL_MEMORY &&
                            wakeGeneration == 0) {
                            if (chargeReservation != 0) {
                                (void)CancelAutoActionChargeFollowupIfReservation(
                                    1, chargeReservation,
                                    "P1 wake fallback returned no primary generation");
                                chargeReservation = 0;
                            }
                            ok = false;
                        }
                        LogOut(std::string("[AUTO-ACTION] P1 wake special deferred inject ") + (ok?"ok":"fail"), true);
                        if (ok) {
                            s_p1WakeBufferFrozen = true;
                            s_p1WakeMotionGeneration = wakeGeneration;
                            s_p1WakeChargeReservation = chargeReservation;
                        }
                        dispatchAccepted = ok;
                    } else {
                        int motionType = ConvertTriggerActionToMotion(
                            at, TRIGGER_ON_WAKEUP, s_p1WakePrearmStrength);
                        int buttonMask = ResolveButtonMaskForAction(at, TRIGGER_ON_WAKEUP, s_p1WakePrearmStrength);
                        if (at == ACTION_JUMP || at == ACTION_BLOCK) {
                            dispatchAccepted = IssueWakeImmediateHold(1, at);
                        } else if (IsDashRecipeAction(at)) {
                            g_injectImmediateOnly[1].store(false);
                            uint64_t generation = 0;
                            dispatchAccepted =
                                SubmitAutoActionMotionTransaction(
                                    1, motionType, 0, 6, &generation) ==
                                AutoActionMotionSubmitResult::Accepted;
                            LogOut("[AUTO-ACTION] P1 wake dash transaction " +
                                       std::string(dispatchAccepted ? "queued" : "rejected") +
                                       " gen=" + std::to_string(generation),
                                   true);
                        } else if (NormalInputPolicy::IsNormalMotion(motionType)) {
                            uint64_t generation = 0;
                            const auto submit = QueueAutoActionNormalPulse(
                                1, motionType,
                                NormalInputPolicy::Timing::WhenActionable,
                                &generation);
                            LogOut("[AUTO-ACTION] P1 wake normal pulse " +
                                   std::string(submit == NormalInputPolicy::SubmitResult::Accepted
                                                   ? "queued" : "rejected") +
                                    " gen=" + std::to_string(generation) +
                                    " motion=" + std::to_string(motionType), true);
                            dispatchAccepted =
                                submit == NormalInputPolicy::SubmitResult::Accepted;
                            if (dispatchAccepted) {
                                s_p1WakeNormalGeneration = generation;
                                ++s_p1WakeNormalAttempts;
                            }
                        } else if (motionType >= MOTION_236A) {
                            if (buttonMask == 0) {
                                int atStrength = (s_p1WakePrearmStrength >= 0) ? s_p1WakePrearmStrength : GetSpecialMoveStrength(at, TRIGGER_ON_WAKEUP);
                                buttonMask = (1 << (4 + atStrength));
                            }
                            uint64_t generation = 0;
                            dispatchAccepted =
                                SubmitAutoActionMotionTransaction(
                                    1, motionType, buttonMask, 6, &generation) ==
                                AutoActionMotionSubmitResult::Accepted;
                            std::stringstream bm;
                            bm << std::hex << buttonMask;
                            LogOut(std::string("[AUTO-ACTION] P1 wake transaction ") +
                                       (dispatchAccepted ? "queued" : "rejected") +
                                       " btnMask=0x" + bm.str() +
                                       " gen=" + std::to_string(generation),
                                   true);
                        }
                    }
                }
                if (s_p1WakePrearmIsSpecial && dispatchAccepted &&
                    s_p1WakeMotionGeneration != 0) {
                    LogOut("[AUTO-ACTION] P1 wake motion admitted; awaiting EFZ consumer",
                           true);
                } else if (dispatchAccepted && s_p1WakeNormalGeneration == 0) {
                    finishP1WakePrearm();
                } else if (s_p1WakeNormalGeneration != 0) {
                    LogOut("[AUTO-ACTION] P1 wake normal admitted; awaiting EFZ consumer",
                           true);
                } else {
                    LogOut("[AUTO-ACTION] P1 wake dispatch was rejected; request remains armed until expiry",
                           true);
                }
            }
            else if (!s_p1WakePrearmed && IsGroundtech(prevMoveID1) && IsActionable(moveID1)) {
                if (triggerRandomizeEnabled.load()) { if ((rand() & 1) == 0) { if (detailedLogging.load() && canLogTrigDiag()) LogOut("[AUTO-ACTION] P1 On Wakeup skipped by random gate", true); goto p1_wakeup_done; } }
                shouldTrigger = true;
                triggerType = TRIGGER_ON_WAKEUP;
                {
                    int userDelay = s_p1WakeOptionPicked ? s_p1WakePrePickedOption.delay : triggerOnWakeupDelay.load();
                    delay = (userDelay <= 1) ? 0 : (userDelay - 1);
                }
                int actionForMove = s_p1WakeOptionPicked ? s_p1WakePrePickedOption.action : triggerOnWakeupAction.load();
                actionMoveID = GetActionMoveID(actionForMove, TRIGGER_ON_WAKEUP, 1);
                
                LogOut("[AUTO-ACTION] P1 On Wakeup trigger activated", true);
                TraceAutoActionWakeEvent("p1-wakeup-trigger", 1, prevMoveID1, moveID1,
                                         "delayF=" + std::to_string(delay) +
                                         " actionMoveID=" + std::to_string(actionMoveID));
                if (detailedLogging.load()) {
                    LogOut("[TRIGGER_TIMING] P1 wake delay=" + std::to_string(s_p1Timers.lastWakeDelay) +
                           ", sinceWake=" + std::to_string(s_p1Timers.sinceWake), true);
                }
            }
        p1_wakeup_done: ;
        }
        
        if (shouldTrigger) {
            LogFlowSequenceEvent(1, "trigger delay", prevMoveID1, moveID1);
            AutoActionLogScope seqScope("TriggerSequence", 1, triggerType);
            StartTriggerDelay(1, triggerType, actionMoveID, delay);
        }
    }
    
    if ((targetPlayer == 2 || targetPlayer == 3) && !p2DelayState.isDelaying && !p2ActionApplied) {
        bool shouldTrigger = false;
        int triggerType = TRIGGER_NONE;
        int delay = 0;
        short actionMoveID = 0;
    if (detailedLogging.load() && canLogTrigDiag()) {
            LogOut("[TRIGGER_DIAG] P2 eval: prev=" + std::to_string(prevMoveID2) + 
                       ", curr=" + std::to_string(moveID2) +
                       ", trigActive=" + std::to_string(p2TriggerActive) +
                       ", cooldown=" + std::to_string(p2TriggerCooldown) +
                   ", actionable(prev/curr)=" + std::to_string(IsActionable(prevMoveID2)) + "/" + std::to_string(IsActionable(moveID2)), true);
        }
        
    if (!shouldTrigger && triggerAfterAirtechEnabled.load()) {
            bool restorePending = g_pendingControlRestore.load();
            bool wasAirtech = IsAirtech(prevMoveID2);
            bool postAirtechNow = (!IsAirtech(moveID2)) && (IsActionable(moveID2) || moveID2 == FALLING_ID);
            if (detailedLogging.load() && canLogTrigDiag()) {
                LogOut("[TRIGGER_DIAG] P2 AfterAirtech check: wasAirtech=" + std::to_string(wasAirtech) +
                       ", postAirtechNow=" + std::to_string(postAirtechNow) +
                       ", restorePending=" + std::to_string(restorePending) +
                       ", targetPlayer=" + std::to_string(targetPlayer), true);
            }

            if (!restorePending && wasAirtech && postAirtechNow) {
                if (triggerRandomizeEnabled.load()) { if ((rand() & 1) == 0) { if (detailedLogging.load() && canLogTrigDiag()) LogOut("[AUTO-ACTION] P2 After Airtech skipped by random gate", true); goto p2_after_airtech_done; } }
                LogOut("[AUTO-ACTION] P2 After Airtech trigger activated", detailedLogging.load());
                shouldTrigger = true;
                triggerType = TRIGGER_AFTER_AIRTECH;
        delay = triggerAfterAirtechDelay.load();
                actionMoveID = GetActionMoveID(triggerAfterAirtechAction.load(), TRIGGER_AFTER_AIRTECH, 2);
            }
        p2_after_airtech_done: ;
        }
    if (!shouldTrigger && triggerAfterBlockEnabled.load()) {
        bool dashFollowPending = (g_dashDeferred.pendingSel.load() > 0);
        bool restorePending = g_pendingControlRestore.load();
        if (dashFollowPending && detailedLogging.load() && canLogTrigDiag()) {
            LogOut("[TRIGGER_DIAG] Suppressing P2 After Block: dash follow-up pending", true);
        }
        if (restorePending && detailedLogging.load() && canLogTrigDiag()) {
            LogOut("[TRIGGER_DIAG] Suppressing P2 After Block: control restore pending", true);
        }
        if (!dashFollowPending && !restorePending && IsBlockstun(prevMoveID2) && IsActionable(moveID2) && !p2TriggerActive && p2TriggerCooldown <= 0) {
            if (triggerRandomizeEnabled.load()) { if ((rand() & 1) == 0) { if (detailedLogging.load() && canLogTrigDiag()) LogOut("[AUTO-ACTION] P2 After Block skipped by random gate", true); goto p2_after_block_done; } }
            shouldTrigger = true;
            triggerType = TRIGGER_AFTER_BLOCK;
            delay = triggerAfterBlockDelay.load();
            actionMoveID = GetActionMoveID(triggerAfterBlockAction.load(), TRIGGER_AFTER_BLOCK, 2);
            LogOut("[AUTO-ACTION] P2 After Block trigger", true);
        }
    p2_after_block_done: ;
    }

    if (!shouldTrigger && triggerOnRGEnabled.load()) {
        bool enteredRG2 = (!IsRecoilGuard(prevMoveID2) && IsRecoilGuard(moveID2));
        if (enteredRG2) {
            if (triggerRandomizeEnabled.load()) { if ((rand() & 1) == 0) { if (detailedLogging.load() && canLogTrigDiag()) LogOut("[AUTO-ACTION] P2 On RG skipped by random gate", true); goto p2_onrg_done; } }
            int baseDelayF = 20;
            if (moveID2 == RG_STAND_ID) baseDelayF = RG_STAND_FREEZE_DEFENDER;
            else if (moveID2 == RG_CROUCH_ID) baseDelayF = RG_CROUCH_FREEZE_DEFENDER;
            else if (moveID2 == RG_AIR_ID) baseDelayF = RG_AIR_FREEZE_DEFENDER;
            int userDelayF = triggerOnRGDelay.load();
            int totalDelayF = baseDelayF + userDelayF;
            if (totalDelayF < 0) totalDelayF = 0;
            LogOut("[TRIGGER_DIAG] P2 entered RG (" + std::to_string(moveID2) + ") baseDelay=" + std::to_string(baseDelayF) +
                   "F + user=" + std::to_string(userDelayF) + "F => total=" + std::to_string(totalDelayF) + "F", true);
            StartTriggerDelay(2, TRIGGER_ON_RG, 0, totalDelayF);
            const int selectedUserDelayF = (p2DelayState.chosenDelay >= 0)
                ? p2DelayState.chosenDelay
                : userDelayF;

            if (selectedUserDelayF == 0 && !s_p2RGPrearmed &&
                p2DelayState.chosenChargeFollowup == 0) {
                int at = (p2DelayState.chosenAction >= 0) ? p2DelayState.chosenAction : triggerOnRGAction.load();
                int selectedStrength = (p2DelayState.chosenStrength >= 0)
                    ? p2DelayState.chosenStrength
                    : GetSpecialMoveStrength(at, TRIGGER_ON_RG);
                int motion = ConvertTriggerActionToMotion(at, TRIGGER_ON_RG, selectedStrength);
                int buttonMask = 0;
                if ((at >= ACTION_5A && at <= ACTION_2D) || (at >= ACTION_6A && at <= ACTION_4D)) {
                    int button = (at >= ACTION_5A && at <= ACTION_2D) ? ((at - ACTION_5A) % 4) : ((at - ACTION_6A) % 4);
                    buttonMask = (1 << (4 + button));
                } else if (at >= ACTION_JA && at <= ACTION_JD) {
                    int button = (at - ACTION_JA) % 4;
                    buttonMask = (1 << (4 + button));
                } else if (IsStrengthButtonAction(at)) {
                    buttonMask = (1 << (4 + CLAMP(selectedStrength, 0, 3)));
                }
                bool preOk = false;
                const bool isDash =
                    motion == MOTION_FORWARD_DASH || motion == MOTION_BACK_DASH;
                bool isSpec = ((motion >= MOTION_236A) && !isDash) ||
                              (at == ACTION_FINAL_MEMORY);
                if (isSpec) {
                    uint64_t prearmGeneration = 0;
                    if (at == ACTION_FINAL_MEMORY) {
                        int charId = displayData.p2CharID;
                        preOk = ExecuteFinalMemory(
                            2, charId, 120, &prearmGeneration);
                    } else {
                        g_injectImmediateOnly[2].store(false);
                        preOk = QueueP2AutoActionMotionTransaction(
                            motion, buttonMask, 120, &prearmGeneration);
                    }
                    if (preOk) {
                        s_p2RGPrearmIsSpecial = true;
                        s_p2RGPrearmGeneration = prearmGeneration;
                    } else {
                        s_p2RGPrearmIsSpecial = false;
                        s_p2RGPrearmGeneration = 0;
                    }
                }
                if (preOk) {
                    s_p2RGPrearmed = true;
                    s_p2RGPrearmActionType = at;
                    s_p2RGPrearmExpiry = GetEngineTick() + (baseDelayF * 3) + 90;
                    LogOut("[AUTO-ACTION][RG] P2 pre-armed at RG entry: action=" + std::to_string(at) +
                           " motion=" + std::to_string(motion) + " btnMask=" + std::to_string(buttonMask) +
                           " (scoped transaction)", true);
                } else if (isSpec) {
                    s_p2RGPrearmIsSpecial = false;
                    s_p2RGPrearmGeneration = 0;
                    LogOut("[AUTO-ACTION][RG] P2 pre-arm failed; will attempt on delay expiry", true);
                }
            }
        }
    p2_onrg_done: ;
    }
        
    if (!shouldTrigger && triggerAfterHitstunEnabled.load()) {
            bool restorePending = g_pendingControlRestore.load();
            if (restorePending && detailedLogging.load() && canLogTrigDiag()) {
                LogOut("[TRIGGER_DIAG] Suppressing P2 After Hitstun: control restore pending", true);
            }
                if (!restorePending && IsHitstun(prevMoveID2) && !IsHitstun(moveID2) && !IsAirtech(moveID2)) {
                if (IsActionable(moveID2)) {
                        if (triggerRandomizeEnabled.load()) { if ((rand() & 1) == 0) { if (detailedLogging.load() && canLogTrigDiag()) LogOut("[AUTO-ACTION] P2 After Hitstun skipped by random gate", true); goto p2_after_hitstun_done; } }
                    shouldTrigger = true;
                    triggerType = TRIGGER_AFTER_HITSTUN;
                    delay = triggerAfterHitstunDelay.load();
                    actionMoveID = GetActionMoveID(triggerAfterHitstunAction.load(), TRIGGER_AFTER_HITSTUN, 2);
                    LogOut("[AUTO-ACTION] P2 after hitstun trigger activated", true);
                    if (detailedLogging.load()) {
                        LogOut("[TRIGGER_TIMING] P2 hitstun dur=" + std::to_string(s_p2Timers.lastHitDuration) +
                               ", sinceEnd=" + std::to_string(s_p2Timers.sinceHitEnd), true);
                    }
                }
            }
        p2_after_hitstun_done: ;
        }
        
    if (detailedLogging.load()) {
    }

    bool isInGroundtech = IsGroundtech(moveID2);
    const bool wakeTriggerEnabled = triggerOnWakeupEnabled.load();
    const bool wakeBlockEnabled = wakeTriggerEnabled && g_wakeBufferingEnabled.load();
    const bool enteringGroundtechThisFrame =
        wakeTriggerEnabled && isInGroundtech &&
        !IsGroundtech(prevMoveID2);

    if (enteringGroundtechThisFrame) {
        s_p2WakeOptionPicked = false;
        s_p2WakePrePickedOption = {};
        const bool tutorialP2Wake =
            g_tutorialP2WakeToken.load(std::memory_order_acquire) != 0;
        if (!tutorialP2Wake &&
            PickRandomPoolOption(TRIGGER_ON_WAKEUP, 2,
                                 s_p2WakePrePickedOption)) {
            s_p2WakeOptionPicked = true;
            TraceAutoActionWakeEvent("p2-wake-pool-picked", 2, prevMoveID2, moveID2,
                                     "delay=" + std::to_string(s_p2WakePrePickedOption.delay) +
                                     " action=" + std::to_string(s_p2WakePrePickedOption.action) +
                                     " strength=" + std::to_string(s_p2WakePrePickedOption.strength));
            if (detailedLogging.load()) {
                LogOut("[AUTO-ACTION] P2 wake pool pre-picked option: action=" +
                       std::to_string(s_p2WakePrePickedOption.action) +
                       " strength=" + std::to_string(s_p2WakePrePickedOption.strength), true);
            }
        } else if (!tutorialP2Wake &&
                   HasEnabledRows(TRIGGER_ON_WAKEUP) &&
                   PickRandomRow(TRIGGER_ON_WAKEUP, 2,
                                 s_p2WakePrePickedOption)) {
            s_p2WakeOptionPicked = true;
            TraceAutoActionWakeEvent("p2-wake-option-picked", 2, prevMoveID2, moveID2,
                                     "macroSlot=" + std::to_string(s_p2WakePrePickedOption.macroSlot) +
                                     " delay=" + std::to_string(s_p2WakePrePickedOption.delay) +
                                     " action=" + std::to_string(s_p2WakePrePickedOption.action) +
                                     " strength=" + std::to_string(s_p2WakePrePickedOption.strength));
            if (detailedLogging.load()) {
                LogOut("[AUTO-ACTION] P2 wake pre-picked option: macroSlot=" +
                       std::to_string(s_p2WakePrePickedOption.macroSlot) +
                       " delay=" + std::to_string(s_p2WakePrePickedOption.delay) +
                       " action=" + std::to_string(s_p2WakePrePickedOption.action), true);
            }
        }
    }
    
    if (wakeBlockEnabled && isInGroundtech) {
        if (enteringGroundtechThisFrame) {
            s_p2WakeMacro96FrameCount = 0;
            s_p2WakeMacroQueued = false;
            s_p2WakeMacroTargetFrame = -1;
            s_p2WakeMacroSlot = -1;
            s_p2WakeMacroStartFrame = -1;
            s_p2WakeMacroStartTick = 0;
            TraceAutoActionWakeEvent("p2-wake-macro-groundtech-enter", 2, prevMoveID2, moveID2);
        } else {
            if (moveID2 == GROUNDTECH_RECOVERY) {
                ++s_p2WakeMacro96FrameCount;
            }
        }

        int macroSlot = s_p2WakeOptionPicked ? s_p2WakePrePickedOption.macroSlot : TriggerMacroSlotForType(TRIGGER_ON_WAKEUP);
        int userDelayF = s_p2WakeOptionPicked ? s_p2WakePrePickedOption.delay : triggerOnWakeupDelay.load();
        if (s_p2WakeOptionPicked) {
            userDelayF = (userDelayF <= 1) ? 0 : (userDelayF - 1);
        }
        s_p2WakeMacroSlot = (userDelayF == 0) ? macroSlot : -1;

        int macroSel = CLAMP(macroSlot, 1, MacroController::GetSlotCount());
        bool macroSlotSelected = (macroSlot > 0);
        bool macroHasData = macroSlotSelected &&
                            MacroController::GetState() != MacroController::State::Recording &&
                            !MacroController::IsSlotEmpty(macroSel);

        if (macroHasData && userDelayF == 0) {
            int charID = displayData.p2CharID;
            int risingFrames = GetWakeupRisingFrames(charID);
            int firstButtonTick = MacroController::GetFirstButtonTick(macroSel);
            
            if (firstButtonTick < 0) {
                s_p2WakeMacroTargetFrame = -1;
                s_p2WakeMacroStartTick = 0;
            } else {
                int framesToButton = (firstButtonTick + 2) / 3;
                
                constexpr int kBufferMargin = 12;
                int playStartFrame = risingFrames - framesToButton - kBufferMargin;
                if (playStartFrame < 1) playStartFrame = 1;
                
                s_p2WakeMacroStartTick = 0;
                s_p2WakeMacroTargetFrame = playStartFrame * 3;
                
                if (detailedLogging.load() && s_p2WakeMacro96FrameCount == 0) {
                    LogOut("[AUTO-ACTION][MACRO-TIMING-INIT] P2 charID=" + std::to_string(charID) +
                           " risingFrames=" + std::to_string(risingFrames) +
                           " firstBtnTick=" + std::to_string(firstButtonTick) +
                           " framesToBtn=" + std::to_string(framesToButton) +
                           " playStartFrame=" + std::to_string(playStartFrame), true);
                }
                if (s_p2WakeMacro96FrameCount == 0) {
                    TraceAutoActionWakeEvent("p2-wake-macro-target", 2, prevMoveID2, moveID2,
                                             "charID=" + std::to_string(charID) +
                                             " risingFrames=" + std::to_string(risingFrames) +
                                             " firstButtonTick=" + std::to_string(firstButtonTick) +
                                             " framesToButton=" + std::to_string(framesToButton) +
                                             " targetTick=" + std::to_string(s_p2WakeMacroTargetFrame) +
                                             " playStartFrame=" + std::to_string(playStartFrame));
                }
            }
        } else if (s_p2WakeMacro96FrameCount == 0) {
            s_p2WakeMacroTargetFrame = -1;
            s_p2WakeMacroStartTick = 0;
        }

        if (!s_p2WakeMacroQueued && s_p2WakeMacroSlot > 0 && macroHasData &&
            s_p2WakeMacroTargetFrame >= 0) {
             bool macroPlaying = (MacroController::GetState() == MacroController::State::Replaying);
             if (!macroPlaying && s_wakeForcedNeutralToken[2] == 0) {
                 (void)TryPublishWakeForcedNeutral(2);
             }
        }

        if (!s_p2WakeMacroQueued && s_p2WakeMacroSlot > 0 && s_p2WakeMacroTargetFrame >= 0) {
            if (s_p2WakeMacro96FrameCount >= s_p2WakeMacroTargetFrame) {
                int sel = CLAMP(s_p2WakeMacroSlot, 1, MacroController::GetSlotCount());
                if (MacroController::GetState() != MacroController::State::Recording &&
                    !MacroController::IsSlotEmpty(sel)) {
                    MacroController::SetCurrentSlot(sel);
                    LogOut("[AUTO-ACTION][MACRO] P2 Pre-buffering wake macro (slot=" + std::to_string(sel) +
                           ") at frame " + std::to_string(s_p2WakeMacro96FrameCount) +
                           " (target=" + std::to_string(s_p2WakeMacroTargetFrame) + ")", true);
                    TraceAutoActionWakeEvent("p2-wake-macro-play", 2, prevMoveID2, moveID2,
                                             "slot=" + std::to_string(sel) +
                                             " count96=" + std::to_string(s_p2WakeMacro96FrameCount) +
                                             " targetTick=" + std::to_string(s_p2WakeMacroTargetFrame) +
                                             " startTick=" + std::to_string(s_p2WakeMacroStartTick));
                    if (s_wakeForcedNeutralToken[2] != 0) {
                        ReleaseWakeForcedNeutral(2);
                    }
                    const bool started = MacroController::PlayForPlayer(
                        2, s_p2WakeMacroStartTick, false);
                    if (started &&
                        MacroController::GetState() == MacroController::State::Replaying) {
                        g_macroWakePreservePlayer.store(2, std::memory_order_release);
                        g_macroWakePreserveBuffer.store(true, std::memory_order_release);
                        s_p2WakeMacroQueued = true;
                        s_p2WakeMacroTargetFrame = -1;
                        s_p2WakeBufferFrozen = true;
                        s_p2WakeMotionGeneration = 0;
                    } else {
                        g_macroWakePreserveBuffer.store(false, std::memory_order_release);
                        g_macroWakePreservePlayer.store(0, std::memory_order_release);
                        LogOut("[AUTO-ACTION][MACRO] P2 wake playback owner busy; retaining exact slot/target for retry",
                               true);
                    }
                }
            }
        }
    }

    if (triggerOnWakeupEnabled.load() && !s_p2WakePrearmed) {
            if (moveID2 == GROUNDTECH_RECOVERY) {
                if (triggerRandomizeEnabled.load()) {
                    if ((rand() & 1) == 0) {
                        if (detailedLogging.load() && canLogTrigDiag()) {
                            LogOut("[AUTO-ACTION] P2 Wake prearm skipped by random gate", true);
                        }
                        goto p2_wake_prearm_done;
                    }
                }

                int userDelayF = s_p2WakeOptionPicked ? s_p2WakePrePickedOption.delay : triggerOnWakeupDelay.load();
                if (s_p2WakeOptionPicked) {
                    userDelayF = (userDelayF <= 1) ? 0 : (userDelayF - 1);
                }
                int actionType = s_p2WakeOptionPicked ? s_p2WakePrePickedOption.action : triggerOnWakeupAction.load();
                int motionType = ConvertTriggerActionToMotion(actionType, TRIGGER_ON_WAKEUP);

                int macroSlot = s_p2WakeOptionPicked ? s_p2WakePrePickedOption.macroSlot : TriggerMacroSlotForType(TRIGGER_ON_WAKEUP);
                int macroSel = CLAMP(macroSlot, 1, MacroController::GetSlotCount());
                bool macroSlotSelected = (macroSlot > 0);
                bool macroHasData = macroSlotSelected &&
                                    MacroController::GetState() != MacroController::State::Recording &&
                                    !MacroController::IsSlotEmpty(macroSel);
                bool isDash = (motionType == MOTION_FORWARD_DASH || motionType == MOTION_BACK_DASH);
                bool isSpecial = (actionType == ACTION_FINAL_MEMORY) || (motionType >= MOTION_236A && !isDash);
                const bool tutorialWakeLeaseActive =
                    g_tutorialP2WakeToken.load(std::memory_order_acquire) != 0;
                const int chargeFollowup = tutorialWakeLeaseActive
                    ? 0
                    : (s_p2WakeOptionPicked
                        ? s_p2WakePrePickedOption.chargeFollowup
                        : triggerOnWakeupCharge.load());

                bool holdEligible = (userDelayF == 0) && !isSpecial &&
                                    SupportsImmediateWakeHold(actionType) && !macroSlotSelected;
                const bool dedicatedMacroOwnsWake =
                    (userDelayF == 0) && macroHasData;
                bool shouldPrearm = (userDelayF == 0) &&
                                     !dedicatedMacroOwnsWake &&
                                     (isSpecial || holdEligible);

                if (shouldPrearm) {
                    s_p2WakePrearmed = true;
                    s_p2WakePrearmIsSpecial = isSpecial;
                    s_p2WakePrearmIsMacro = false;
                    s_p2WakePrearmActionType = actionType;
                    int chosenStrength = triggerOnWakeupStrength.load();
                    if (s_p2WakeOptionPicked) {
                        chosenStrength = s_p2WakePrePickedOption.strength;
                    }
                    chosenStrength = CLAMP(chosenStrength, 0, 3);
                    s_p2WakePrearmStrength = chosenStrength;
                    s_p2WakePrearmCharge =
                        isSpecial && ActionSupportsChargeFollowup(actionType)
                            ? CLAMP(chargeFollowup, 0, 2)
                            : 0;
                    s_p2WakePrearmExpiry = GetEngineTick() + 120;
                    s_p2WakeMoveID96FrameCount = 0;
                    s_p2WakeBufferFrozen = false;
                    s_p2WakeMotionGeneration = 0;
                    s_p2WakeChargeReservation = 0;
                    s_p2WakeHoldPrimed = holdEligible;
                    s_p2WakeHoldIssued = false;
                    s_p2WakeNormalGeneration = 0;
                    s_p2WakeNormalAttempts = 0;
                    const char* tag = isSpecial ? "special" : "hold";
                    if (detailedLogging.load()) {
                        LogOut(std::string("[AUTO-ACTION] P2 wake ") + tag +
                               " metadata stored action=" + std::to_string(actionType) +
                               " strength=" + std::to_string(s_p2WakePrearmStrength) +
                               " chargeWatcher=" + std::to_string(s_p2WakePrearmCharge) +
                               " macroPicked=" + (s_p2WakeOptionPicked ? "true" : "false"), true);
                    }
                    TraceAutoActionWakeEvent("p2-prearm-stored", 2, prevMoveID2, moveID2,
                                             "kind=" + std::string(tag) +
                                             " action=" + std::to_string(actionType) +
                                             " strength=" + std::to_string(s_p2WakePrearmStrength) +
                                             " chargeWatcher=" + std::to_string(s_p2WakePrearmCharge) +
                                             " delayF=" + std::to_string(userDelayF) +
                                             " optionPicked=" + std::to_string(s_p2WakeOptionPicked));
                    LogFlowSequenceEvent(2, "wake metadata stored",
                                         prevMoveID2, moveID2);
                } else if (detailedLogging.load()) {
                    std::string reason;
                    if (macroSlotSelected && macroHasData && userDelayF == 0) {
                        reason = "macro row (handled by dedicated macro pre-buffer path)";
                    } else if (userDelayF != 0) {
                        reason = "delay>0";
                    } else {
                        reason = "unsupported action";
                    }
                    LogOut("[AUTO-ACTION] P2 wake pre-arm skipped (" + reason + ")", true);
                }
            p2_wake_prearm_done: ;
            }
        }

        bool p2BecameActionableFromGroundtech = IsGroundtech(prevMoveID2) && IsActionable(moveID2);
        const bool p2LeavingGroundtechThisFrame = IsGroundtech(prevMoveID2) && !IsGroundtech(moveID2);

        if (g_wakeBufferingEnabled.load() &&
            (p2LeavingGroundtechThisFrame || s_p2WakeMacroTargetFrame == -2) &&
            !s_p2WakeMacroQueued &&
            s_p2WakeMacroSlot > 0) {
            int sel = CLAMP(s_p2WakeMacroSlot, 1, MacroController::GetSlotCount());
            if (MacroController::GetState() != MacroController::State::Recording && !MacroController::IsSlotEmpty(sel)) {
                MacroController::SetCurrentSlot(sel);
                LogOut("[AUTO-ACTION][MACRO] Fallback wake macro queue on 96 exit (slot=" + std::to_string(sel) +
                       ", count=" + std::to_string(s_p2WakeMacro96FrameCount) +
                       ", target=" + std::to_string(s_p2WakeMacroTargetFrame) +
                       ", prevMove=" + std::to_string(prevMoveID2) +
                       ", currMove=" + std::to_string(moveID2) + ")", true);
                TraceAutoActionWakeEvent("p2-wake-macro-fallback-96-exit", 2, prevMoveID2, moveID2,
                                         "slot=" + std::to_string(sel) +
                                         " count96=" + std::to_string(s_p2WakeMacro96FrameCount) +
                                         " targetTick=" + std::to_string(s_p2WakeMacroTargetFrame));
                if (s_wakeForcedNeutralToken[2] != 0) {
                    ReleaseWakeForcedNeutral(2);
                }
                const bool started = MacroController::PlayForPlayer(2, 0, false);
                if (started &&
                    MacroController::GetState() == MacroController::State::Replaying) {
                    g_macroWakePreservePlayer.store(2, std::memory_order_release);
                    g_macroWakePreserveBuffer.store(true, std::memory_order_release);
                    s_p2WakeMacroQueued = true;
                    s_p2WakeMacroTargetFrame = -1;
                    s_p2WakeMacroSlot = -1;
                } else {
                    g_macroWakePreserveBuffer.store(false, std::memory_order_release);
                    g_macroWakePreservePlayer.store(0, std::memory_order_release);
                    s_p2WakeMacroTargetFrame = -2;
                    LogOut("[AUTO-ACTION][MACRO] P2 wake fallback owner busy; retrying the same slot next tick",
                           true);
                }
            }
        }

        if (s_wakeForcedNeutralToken[2] != 0 &&
            moveID2 != GROUNDTECH_RECOVERY) {
             bool macroPlaying = (MacroController::GetState() == MacroController::State::Replaying);
             if (!macroPlaying) {
                 ReleaseWakeForcedNeutral(2);
             }
        }

        if (s_p2WakePrearmed) {
            if (moveID2 == GROUNDTECH_RECOVERY) {
                s_p2WakeMoveID96FrameCount++;
                int charID = displayData.p2CharID;
                int risingTicks = GetWakeupRisingTicks(charID);
                int bufferFrame = risingTicks - 1;
                if (s_p2WakePrearmIsSpecial && !s_p2WakeBufferFrozen && 
                    s_p2WakeMoveID96FrameCount >= (bufferFrame - 2) && s_p2WakeMoveID96FrameCount <= bufferFrame) {
                    int at = s_p2WakePrearmActionType;
                    bool p2Facing = GetPlayerFacingDirection(2);
                    uint64_t wakeGeneration = 0;
                    uint64_t chargeReservation = 0;
                    bool ok = ExecuteWakeSpecialWithChargeWatcher(
                        2, at, moveID2, s_p2WakePrearmStrength,
                        s_p2WakePrearmCharge, &wakeGeneration,
                        &chargeReservation);
                    if (ok && wakeGeneration == 0) {
                        LogOut("[AUTO-ACTION] P2 wake early buffer returned no generation; treating dispatch as rejected",
                               true);
                        if (chargeReservation != 0) {
                            (void)CancelAutoActionChargeFollowupIfReservation(
                                2, chargeReservation,
                                "P2 wake primary admission returned no generation");
                            chargeReservation = 0;
                        }
                        ok = false;
                    }
                    LogOut("[AUTO-ACTION] P2 wake special early buffer (frame " + std::to_string(s_p2WakeMoveID96FrameCount) + "/" + std::to_string(risingTicks) + ", charID=" + std::to_string(charID) + ", facing=" + (p2Facing?"right":"left") + ") " + std::string(ok?"ok":"fail"), true);
                    TraceAutoActionWakeEvent("p2-early-special-freeze", 2, prevMoveID2, moveID2,
                                             "count96=" + std::to_string(s_p2WakeMoveID96FrameCount) +
                                             " risingTicks=" + std::to_string(risingTicks) +
                                             " bufferFrame=" + std::to_string(bufferFrame) +
                                             " charID=" + std::to_string(charID) +
                                             " action=" + std::to_string(at) +
                                             " ok=" + std::to_string(ok));
                    LogFlowSequenceEvent(2, "wake early buffer", prevMoveID2, moveID2);
                    s_p2WakeBufferFrozen = ok;
                    s_p2WakeMotionGeneration = ok ? wakeGeneration : 0;
                    s_p2WakeChargeReservation = ok ? chargeReservation : 0;
                }
            }
            // Must stay outside the moveID==96 block: this detects the 96 to 0 transition.
            if (!s_p2WakePrearmIsSpecial && !s_p2WakePrearmIsMacro && s_p2WakeHoldPrimed && !s_p2WakeHoldIssued) {
                const bool leavingGroundtechThisFrame = (prevMoveID2 == GROUNDTECH_RECOVERY) && (moveID2 != GROUNDTECH_RECOVERY);
                if (leavingGroundtechThisFrame) {
                    if (IssueWakeImmediateHold(2, s_p2WakePrearmActionType)) {
                        s_p2WakeHoldIssued = true;
                        TraceAutoActionWakeEvent("p2-wake-hold-issued", 2, prevMoveID2, moveID2,
                                                 "action=" + std::to_string(s_p2WakePrearmActionType));
                    }
                }
            }
            bool actionableButLostGround = !IsGroundtech(prevMoveID2) && IsActionable(moveID2) && !IsGroundtech(moveID2);
            if (actionableButLostGround && !s_p2WakeActionableWarning) {
                LogOut("[AUTO-ACTION][FLOW] P2 wake actionable while prev state not groundtech (prev=" + std::to_string(prevMoveID2) +
                       ", curr=" + std::to_string(moveID2) + ")", true);
                LogFlowSequenceEvent(2, "wake actionable lost-ground", prevMoveID2, moveID2);
                s_p2WakeActionableWarning = true;
            } else if (!actionableButLostGround) {
                s_p2WakeActionableWarning = false;
            }
        } else {
            s_p2WakeActionableWarning = false;
        }

        if (!shouldTrigger && triggerOnWakeupEnabled.load()) {
            if (p2BecameActionableFromGroundtech && detailedLogging.load()) {
                const char* wakeSrc = "none";
                if (s_p2WakeMacroQueued) {
                    wakeSrc = "macro";
                } else if (s_p2WakePrearmed && s_p2WakePrearmIsSpecial) {
                    wakeSrc = "special";
                } else if (s_p2WakePrearmed && s_p2WakeHoldIssued) {
                    wakeSrc = "hold";
                } else if (s_p2WakePrearmed) {
                    wakeSrc = "prearmed";
                }
                LogOut(std::string("[AUTO-ACTION][WAKE-SUMMARY] P2 actionable-from-96 src=") + wakeSrc +
                       " currMove=" + std::to_string(moveID2) +
                       " prevMove=" + std::to_string(prevMoveID2) +
                       " macroQueued=" + std::to_string(s_p2WakeMacroQueued) +
                       " macroSlot=" + std::to_string(s_p2WakeMacroSlot) +
                       " macroFrameCount=" + std::to_string(s_p2WakeMacro96FrameCount),
                       true);
            }

            const auto finishP2WakePrearm = [&]() {
                p2TriggerActive = false;
                p2TriggerCooldown = 0;
                s_p2WakePrearmed = false;
                s_p2WakePrearmIsSpecial = false;
                s_p2WakePrearmIsMacro = false;
                s_p2WakePrearmActionType = -1;
                s_p2WakePrearmStrength = -1;
                s_p2WakePrearmCharge = 0;
                s_p2WakeMoveID96FrameCount = 0;
                s_p2WakeBufferFrozen = false;
                s_p2WakeMotionGeneration = 0;
                s_p2WakeChargeReservation = 0;
                s_p2WakeHoldPrimed = false;
                s_p2WakeHoldIssued = false;
                s_p2WakeNormalGeneration = 0;
                s_p2WakeNormalAttempts = 0;
            };

            bool wakeMotionAccepted = false;
            bool wakeNormalAccepted = false;
            bool wakeNormalExhausted = false;
            if (s_p2WakeNormalGeneration != 0) {
                const AutoActionNormalPulseOutcome outcome =
                    GetAutoActionNormalPulseOutcome(
                        2, s_p2WakeNormalGeneration);
                if (outcome == AutoActionNormalPulseOutcome::Accepted) {
                    wakeNormalAccepted = true;
                } else if (outcome != AutoActionNormalPulseOutcome::Pending) {
                    LogOut("[AUTO-ACTION] P2 wake normal gen=" +
                               std::to_string(s_p2WakeNormalGeneration) +
                               " was not consumed; retaining wake option for retry",
                           true);
                    s_p2WakeNormalGeneration = 0;
                    s_p2WakeHoldIssued = false;
                    if (s_p2WakeChargeReservation != 0) {
                        (void)CancelAutoActionChargeFollowupIfReservation(
                            2, s_p2WakeChargeReservation,
                            "P2 wake primary normal was not accepted");
                        s_p2WakeChargeReservation = 0;
                    }
                    wakeNormalExhausted =
                        s_p2WakeNormalAttempts >=
                        kMaxAutoActionDispatchAttempts;
                }
            }
            if (s_p2WakePrearmed && s_p2WakePrearmIsSpecial &&
                s_p2WakeBufferFrozen && s_p2WakeMotionGeneration != 0) {
                const P2AutoActionMotionOutcome outcome =
                    GetP2AutoActionMotionTransactionOutcome(
                        s_p2WakeMotionGeneration);
                if (outcome == P2AutoActionMotionOutcome::Accepted) {
                    wakeMotionAccepted = true;
                } else if (outcome != P2AutoActionMotionOutcome::Pending) {
                    LogOut("[AUTO-ACTION] P2 wake motion generation=" +
                               std::to_string(s_p2WakeMotionGeneration) +
                               " did not execute; cancelling its passive charge watcher before retry",
                           true);
                    if (s_p2WakeChargeReservation != 0) {
                        (void)CancelAutoActionChargeFollowupIfReservation(
                            2, s_p2WakeChargeReservation,
                            "P2 wake primary motion was not accepted");
                        s_p2WakeChargeReservation = 0;
                    }
                    s_p2WakeBufferFrozen = false;
                    s_p2WakeMotionGeneration = 0;
                }
            }

            if (s_p2WakePrearmed && wakeNormalAccepted) {
                LogOut("[AUTO-ACTION] P2 wake normal was consumed by EFZ", true);
                s_p2WakeChargeReservation = 0;
                finishP2WakePrearm();
            } else if (s_p2WakePrearmed && wakeNormalExhausted) {
                LogOut("[AUTO-ACTION] P2 wake normal exhausted bounded retries", true);
                if (s_p2WakeChargeReservation != 0) {
                    (void)CancelAutoActionChargeFollowupIfReservation(
                        2, s_p2WakeChargeReservation,
                        "P2 wake normal exhausted retries");
                    s_p2WakeChargeReservation = 0;
                }
                finishP2WakePrearm();
            } else if (s_p2WakePrearmed && s_p2WakePrearmIsSpecial &&
                wakeMotionAccepted) {
                LogFlowSequenceEvent(2, "wake exec (native consumer accepted)",
                                     prevMoveID2, moveID2);
                s_p2WakeChargeReservation = 0;
                finishP2WakePrearm();
            } else if (s_p2WakePrearmed && s_p2WakePrearmIsSpecial &&
                       s_p2WakeBufferFrozen &&
                       p2BecameActionableFromGroundtech) {
                LogOut("[AUTO-ACTION] P2 wake motion is still pending at actionable boundary; keeping generation=" +
                           std::to_string(s_p2WakeMotionGeneration),
                       detailedLogging.load());
            } else if (s_p2WakePrearmed && !s_p2WakePrearmIsSpecial &&
                       s_p2WakeHoldIssued &&
                       s_p2WakeNormalGeneration == 0 &&
                       p2BecameActionableFromGroundtech) {
                LogOut("[AUTO-ACTION] P2 wake hold exec (inputs maintained through wake)", true);
                LogFlowSequenceEvent(2, "wake hold exec", prevMoveID2, moveID2);
                
                s_p2WakeJumpTrackFrame = frameCounter.load();
                
                finishP2WakePrearm();
            } else if (s_p2WakePrearmed && !s_p2WakePrearmIsMacro &&
                       !s_p2WakeBufferFrozen &&
                       s_p2WakeNormalGeneration == 0 &&
                       (p2BecameActionableFromGroundtech ||
                        IsActionable(moveID2))) {
                AutoActionLogScope wakeScope("WakeImmediateExec", 2, TRIGGER_ON_WAKEUP);
                LogOut("[AUTO-ACTION] P2 wake exec (fallback): action=" + std::to_string(s_p2WakePrearmActionType) +
                       " special=" + std::to_string(s_p2WakePrearmIsSpecial) +
                       " frame=" + std::to_string(frameCounter.load()), true);
                    TraceAutoActionWakeEvent("p2-wake-fallback", 2, prevMoveID2, moveID2,
                                             "action=" + std::to_string(s_p2WakePrearmActionType) +
                                             " special=" + std::to_string(s_p2WakePrearmIsSpecial));
                    LogFlowSequenceEvent(2, "wake exec attempt", prevMoveID2, moveID2);
                    g_lastActiveTriggerType.store(TRIGGER_ON_WAKEUP);
                    g_lastActiveTriggerFrame.store(frameCounter.load());
                    bool dispatchAccepted = false;
                    if (s_p2WakePrearmActionType >= 0) {
                    int at = s_p2WakePrearmActionType;
                    if (s_p2WakePrearmIsSpecial) {
                        uint64_t wakeGeneration = 0;
                        uint64_t chargeReservation = 0;
                        bool ok = ExecuteWakeSpecialWithChargeWatcher(
                            2, at, moveID2, s_p2WakePrearmStrength,
                            s_p2WakePrearmCharge, &wakeGeneration,
                            &chargeReservation);
                        if (ok && wakeGeneration == 0) {
                            LogOut("[AUTO-ACTION] P2 wake fallback returned no generation; treating dispatch as rejected",
                                   true);
                            if (chargeReservation != 0) {
                                (void)CancelAutoActionChargeFollowupIfReservation(
                                    2, chargeReservation,
                                    "P2 wake fallback returned no primary generation");
                                chargeReservation = 0;
                            }
                            ok = false;
                        }
                        LogOut(std::string("[AUTO-ACTION] P2 wake special deferred inject ") + (ok?"ok":"fail"), true);
                        if (ok) {
                            s_p2WakeBufferFrozen = true;
                            s_p2WakeMotionGeneration = wakeGeneration;
                            s_p2WakeChargeReservation = chargeReservation;
                            dispatchAccepted = true;
                            p2TriggerActive = false;
                            p2TriggerCooldown = 0;
                        }
                    } else {
                        int motionType = ConvertTriggerActionToMotion(
                            at, TRIGGER_ON_WAKEUP, s_p2WakePrearmStrength);
                        int buttonMask = ResolveButtonMaskForAction(at, TRIGGER_ON_WAKEUP, s_p2WakePrearmStrength);
                        if (at == ACTION_JUMP || at == ACTION_BLOCK) {
                            dispatchAccepted = IssueWakeImmediateHold(2, at);
                        } else if (IsDashRecipeAction(at)) {
                            g_injectImmediateOnly[2].store(false);
                            uint64_t generation = 0;
                            dispatchAccepted = QueueP2AutoActionMotionTransaction(
                                motionType, 0, 6, &generation);
                            LogOut("[AUTO-ACTION] P2 wake dash transaction " +
                                       std::string(dispatchAccepted ? "queued" : "rejected") +
                                       " gen=" + std::to_string(generation),
                                   true);
                        } else if (NormalInputPolicy::IsNormalMotion(motionType)) {
                            uint64_t generation = 0;
                            const auto submit = QueueAutoActionNormalPulse(
                                2, motionType,
                                NormalInputPolicy::Timing::WhenActionable,
                                &generation);
                            LogOut("[AUTO-ACTION] P2 wake normal pulse " +
                                   std::string(submit == NormalInputPolicy::SubmitResult::Accepted
                                                   ? "queued" : "rejected") +
                                   " gen=" + std::to_string(generation) +
                                   " motion=" + std::to_string(motionType), true);
                            dispatchAccepted =
                                submit == NormalInputPolicy::SubmitResult::Accepted;
                            if (dispatchAccepted) {
                                s_p2WakeNormalGeneration = generation;
                                ++s_p2WakeNormalAttempts;
                            }
                        } else if (motionType >= MOTION_236A) {
                            if (buttonMask == 0) {
                                int atStrength = (s_p2WakePrearmStrength >= 0) ? s_p2WakePrearmStrength : GetSpecialMoveStrength(at, TRIGGER_ON_WAKEUP);
                                buttonMask = (1 << (4 + atStrength));
                            }
                            uint64_t generation = 0;
                            dispatchAccepted = QueueP2AutoActionMotionTransaction(
                                motionType, buttonMask, 6, &generation);
                            std::stringstream bm;
                            bm << std::hex << buttonMask;
                            LogOut(std::string("[AUTO-ACTION] P2 wake transaction ") +
                                       (dispatchAccepted ? "queued" : "rejected") +
                                       " btnMask=0x" + bm.str() +
                                       " gen=" + std::to_string(generation),
                                   true);
                        }
                    }
                }
                if (!s_p2WakePrearmIsSpecial && dispatchAccepted &&
                    s_p2WakeNormalGeneration == 0) {
                    finishP2WakePrearm();
                } else if (s_p2WakeNormalGeneration != 0) {
                    LogOut("[AUTO-ACTION] P2 wake normal admitted; awaiting EFZ consumer",
                           true);
                } else if (!dispatchAccepted) {
                    LogOut("[AUTO-ACTION] P2 wake dispatch was rejected; request remains armed until expiry",
                           true);
                }
            } else if (s_p2WakeMacroQueued && IsGroundtech(prevMoveID2) && IsActionable(moveID2)) {
                if (detailedLogging.load()) {
                    LogOut("[AUTO-ACTION] P2 wake macro pre-buffered; skipping normal trigger currMove=" +
                           std::to_string(moveID2) + " prevMove=" + std::to_string(prevMoveID2), true);
                }
                s_p2WakePrearmed = false;
                s_p2WakePrearmIsSpecial = false;
                s_p2WakePrearmIsMacro = false;
                s_p2WakePrearmActionType = -1;
                s_p2WakePrearmCharge = 0;
                s_p2WakeMotionGeneration = 0;
                s_p2WakeChargeReservation = 0;
                s_p2WakeMacroQueued = false;
                s_p2WakeOptionPicked = false;
                s_p2WakeMacro96FrameCount = 0;
                s_p2WakeMacroTargetFrame = -1;
                s_p2WakeMacroSlot = -1;
            } else if (!s_p2WakePrearmed && !s_p2WakeMacroQueued && IsGroundtech(prevMoveID2) && IsActionable(moveID2)) {
                if (detailedLogging.load() && s_p2WakeMacroTargetFrame >= 0) {
                    LogOut("[AUTO-ACTION][MACRO] Wake pre-buffer missed (count=" + std::to_string(s_p2WakeMacro96FrameCount) +
                           ", target=" + std::to_string(s_p2WakeMacroTargetFrame) + ")", true);
                }
                s_p2WakeMacroTargetFrame = -1;
                s_p2WakeMacroSlot = -1;
                if (triggerRandomizeEnabled.load()) { if ((rand() & 1) == 0) { if (detailedLogging.load() && canLogTrigDiag()) LogOut("[AUTO-ACTION] P2 On Wakeup skipped by random gate", true); goto p2_wakeup_done; } }
                shouldTrigger = true;
                triggerType = TRIGGER_ON_WAKEUP;
                {
                    int userDelay = s_p2WakeOptionPicked ? s_p2WakePrePickedOption.delay : triggerOnWakeupDelay.load();
                    delay = (userDelay <= 1) ? 0 : (userDelay - 1);
                }
                int actionForMove = s_p2WakeOptionPicked ? s_p2WakePrePickedOption.action : triggerOnWakeupAction.load();
                actionMoveID = GetActionMoveID(actionForMove, TRIGGER_ON_WAKEUP, 2);
                
                LogOut("[AUTO-ACTION] P2 On Wakeup trigger activated", detailedLogging.load());
                TraceAutoActionWakeEvent("p2-wakeup-trigger", 2, prevMoveID2, moveID2,
                                         "delayF=" + std::to_string(delay) +
                                         " actionMoveID=" + std::to_string(actionMoveID));
                if (detailedLogging.load()) {
                    LogOut("[TRIGGER_TIMING] P2 wake delay=" + std::to_string(s_p2Timers.lastWakeDelay) +
                           ", sinceWake=" + std::to_string(s_p2Timers.sinceWake), true);
                }
            }
        p2_wakeup_done: ;
        }
        
        if (shouldTrigger) {
            LogFlowSequenceEvent(2, "trigger delay", prevMoveID2, moveID2);
            AutoActionLogScope seqScope("TriggerSequence", 2, triggerType);
            StartTriggerDelay(2, triggerType, actionMoveID, delay);
        } else {
        }
    }

    {
        const int completedPlayer =
            g_wakeMacroPlaybackCompleted.load(std::memory_order_acquire);
        if (completedPlayer == 1 || completedPlayer == 2) {
            const short completedMoveID =
                (completedPlayer == 1) ? moveID1 : moveID2;
            if (!IsGroundtech(completedMoveID)) {
                if (TryBeginWakeMacroCleanup(completedPlayer)) {
                    const int kWakeMacroTokenNeutralizeDelay = 20;
                    s_wakeMacroTokenNeutralizeCountdown[completedPlayer] =
                        kWakeMacroTokenNeutralizeDelay;
                    g_wakeMacroPlaybackCompleted.store(
                        0, std::memory_order_release);
                    s_lastWakeMoveID[completedPlayer] = completedMoveID;
                    s_wakeMoveIDChanged[completedPlayer] = false;
                    if (detailedLogging.load()) {
                        LogOut("[AUTO-ACTION][MACRO-CLEANUP] Wake macro playback finished; reserved P" +
                               std::to_string(completedPlayer) +
                               " input and monitoring for buffered move execution (current moveID=" +
                               std::to_string(completedMoveID) + ")", true);
                    }
                }
            }
        }
    }

    for (int cleanupPlayer = 1; cleanupPlayer <= 2; ++cleanupPlayer) {
        if (s_wakeMacroTokenNeutralizeCountdown[cleanupPlayer] <= 0) continue;
        const short cleanupMoveID = (cleanupPlayer == 1) ? moveID1 : moveID2;
        std::lock_guard<std::recursive_mutex> cleanupLock(g_p2ControlMutex);
        if (!ScopedInputReservationMatches(
                cleanupPlayer, ScopedInputOwner::WakeMacroCleanup,
                s_wakeMacroCleanupToken[cleanupPlayer])) {
            // Fail closed.  Never perform an unattributed whole-buffer clear;
            // re-arm the completion signal so admission can be retried.
            s_wakeMacroTokenNeutralizeCountdown[cleanupPlayer] = 0;
            s_wakeMoveIDChanged[cleanupPlayer] = false;
            s_wakeMacroCleanupToken[cleanupPlayer] = 0;
            g_wakeMacroPlaybackCompleted.store(
                cleanupPlayer, std::memory_order_release);
            LogOut("[AUTO-ACTION][MACRO-CLEANUP] Lost cleanup reservation; deferred without touching P" +
                       std::to_string(cleanupPlayer) + " input",
                   detailedLogging.load());
            continue;
        }
        bool moveIDWas96 = (s_lastWakeMoveID[cleanupPlayer] == GROUNDTECH_RECOVERY);
        bool moveIDNowBufferable = IsBufferableAction(cleanupMoveID);

        if (moveIDWas96 && moveIDNowBufferable) {
            bool macroStillPlaying = (MacroController::GetState() == MacroController::State::Replaying);

            if (macroStillPlaying) {
                if (detailedLogging.load()) {
                    LogOut("[AUTO-ACTION][MACRO-CLEANUP] Buffered move executed (96->" + std::to_string(cleanupMoveID) +
                           "), macro still playing - released cleanup reservation", true);
                }
                s_wakeMacroTokenNeutralizeCountdown[cleanupPlayer] = 0;
                s_lastWakeMoveID[cleanupPlayer] = cleanupMoveID;
                g_wakeMacroPlaybackCompleted.store(
                    cleanupPlayer, std::memory_order_release);
                EndWakeMacroCleanup(cleanupPlayer);
            } else {
                (void)NeutralizeMotionToken(cleanupPlayer);
                (void)ClearPlayerInputBuffer(cleanupPlayer);
                s_wakeMacroTokenNeutralizeCountdown[cleanupPlayer] = 0;
                s_wakeMoveIDChanged[cleanupPlayer] = false;
                EndWakeMacroCleanup(cleanupPlayer);
                if (detailedLogging.load()) {
                    LogOut("[AUTO-ACTION][MACRO-CLEANUP] Buffered move executed (96->" + std::to_string(cleanupMoveID) +
                           "), macro finished - immediate cleanup", true);
                }
            }
        } else {
            if (cleanupMoveID != s_lastWakeMoveID[cleanupPlayer]) {
                s_wakeMoveIDChanged[cleanupPlayer] = true;
                s_lastWakeMoveID[cleanupPlayer] = cleanupMoveID;
            }

            s_wakeMacroTokenNeutralizeCountdown[cleanupPlayer]--;

            if (s_wakeMacroTokenNeutralizeCountdown[cleanupPlayer] == 0) {
                (void)NeutralizeMotionToken(cleanupPlayer);
                if (cleanupMoveID >= 200) {
                    (void)ClearPlayerInputBuffer(cleanupPlayer);
                }
                EndWakeMacroCleanup(cleanupPlayer);
                if (detailedLogging.load()) {
                    LogOut("[AUTO-ACTION][MACRO-CLEANUP] Countdown expired (moveID=" + std::to_string(cleanupMoveID) +
                           ") - fallback cleanup", true);
                }
            }
        }
    }
    
    static int p1ActionAppliedFrame = -1;
    static int p2ActionAppliedFrame = -1;
    
    if (p1ActionApplied && p1ActionAppliedFrame == -1) {
        p1ActionAppliedFrame = frameCounter.load();
    }
    if (p2ActionApplied && p2ActionAppliedFrame == -1) {
        p2ActionAppliedFrame = frameCounter.load();
    }
    
    int currentFrame = frameCounter.load();
    
    if (p1ActionApplied && p1ActionAppliedFrame != -1) {
        if (currentFrame - p1ActionAppliedFrame >= 30) {
            p1ActionApplied = false;
            p1ActionAppliedFrame = -1;
        }
    }
    
    if (p2ActionApplied && p2ActionAppliedFrame != -1) {
        if (currentFrame - p2ActionAppliedFrame >= 30) {
            p2ActionApplied = false;
            p2ActionAppliedFrame = -1;
        }
    }
    
    ProcessAutoControlRestore();

    if (MacroController::GetState() == MacroController::State::Replaying) {
        for (int& rolled : s_rolledMacroSlot) rolled = 0;
    }
    ProcessDeferredDashFollowups(moveID1, prevMoveID1, moveID2, prevMoveID2);

    MaybeAutoCloseFlowSequence(1, prevMoveID1, moveID1, s_p1WakePrearmed, false);
    MaybeAutoCloseFlowSequence(2, prevMoveID2, moveID2,
                               s_p2WakePrearmed || s_p2WakeMacroQueued,
                               g_pendingControlRestore.load());

    // Must stay at function level: the P1 trigger gate stops running after a control swap, and a
    // release inside it would strand g_manualInputOverride[1] on the fighter the user just took over.
    if (s_wakeForcedNeutralToken[1] != 0 &&
        moveID1 != GROUNDTECH_RECOVERY) {
        const bool macroPlaying =
            (MacroController::GetState() == MacroController::State::Replaying);
        if (!macroPlaying) {
            ReleaseWakeForcedNeutral(1);
        }
    }

    int now = GetEngineTick();
    if (s_p1WakePrearmed && now > s_p1WakePrearmExpiry) {
        int delta = now - s_p1WakePrearmExpiry;
        LogOut("[AUTO-ACTION] P1 wake pre-arm expired (no execution, delta=" + std::to_string(delta) +
               ", currMove=" + std::to_string(moveID1) + ")", true);
        LogFlowSequenceEvent(1, "wake pre-arm expired", prevMoveID1, moveID1);
        EndFlowSequence(1, "WakePrearmExpired", moveID1);
        if (s_p1WakeChargeReservation != 0) {
            (void)CancelAutoActionChargeFollowupIfReservation(
                1, s_p1WakeChargeReservation,
                "P1 wake request expired before primary acceptance");
            s_p1WakeChargeReservation = 0;
        }
        if (s_p1WakeMotionGeneration != 0 &&
            GetAutoActionMotionTransactionOutcome(
                1, s_p1WakeMotionGeneration) ==
                AutoActionMotionOutcome::Pending) {
            (void)CancelAutoActionMotionTransactionIfGeneration(
                1, s_p1WakeMotionGeneration,
                "P1 wake request expired before native acceptance");
        }
        if (s_p1WakeNormalGeneration != 0 &&
            GetAutoActionNormalPulseOutcome(
                1, s_p1WakeNormalGeneration) ==
                AutoActionNormalPulseOutcome::Pending) {
            CancelAutoActionNormalPulse(1);
        }
        s_p1WakePrearmed = false;
        s_p1WakePrearmIsSpecial = false;
        s_p1WakePrearmIsMacro = false;
        s_p1WakePrearmActionType = -1;
        s_p1WakePrearmStrength = -1;
        s_p1WakePrearmCharge = 0;
        s_p1WakeMoveID96FrameCount = 0;
        s_p1WakeBufferFrozen = false;
        s_p1WakeMotionGeneration = 0;
        s_p1WakeHoldPrimed = false;
        s_p1WakeHoldIssued = false;
        s_p1WakeNormalGeneration = 0;
        s_p1WakeNormalAttempts = 0;
        s_p1WakeOptionPicked = false;
        s_p1WakePrePickedOption = {};
    }
    if (s_p2WakePrearmed && now > s_p2WakePrearmExpiry) {
        int delta = now - s_p2WakePrearmExpiry;
        LogOut("[AUTO-ACTION] P2 wake pre-arm expired (no execution, delta=" + std::to_string(delta) +
               ", currMove=" + std::to_string(moveID2) + ")", true);
        LogFlowSequenceEvent(2, "wake pre-arm expired", prevMoveID2, moveID2);
        EndFlowSequence(2, "WakePrearmExpired", moveID2);
        if (s_p2WakeChargeReservation != 0) {
            (void)CancelAutoActionChargeFollowupIfReservation(
                2, s_p2WakeChargeReservation,
                "P2 wake request expired before primary acceptance");
            s_p2WakeChargeReservation = 0;
        }
        if (s_p2WakeMotionGeneration != 0 &&
            GetP2AutoActionMotionTransactionOutcome(
                s_p2WakeMotionGeneration) ==
                P2AutoActionMotionOutcome::Pending) {
            (void)CancelAutoActionMotionTransactionIfGeneration(
                2, s_p2WakeMotionGeneration,
                "P2 wake request expired before native acceptance");
        }
        if (s_p2WakeNormalGeneration != 0 &&
            GetAutoActionNormalPulseOutcome(
                2, s_p2WakeNormalGeneration) ==
                AutoActionNormalPulseOutcome::Pending) {
            CancelAutoActionNormalPulse(2);
        }
        s_p2WakePrearmed = false;
        s_p2WakePrearmIsSpecial = false;
        s_p2WakePrearmIsMacro = false;
        s_p2WakePrearmActionType = -1;
        s_p2WakePrearmStrength = -1;
        s_p2WakePrearmCharge = 0;
        s_p2WakeMoveID96FrameCount = 0;
        s_p2WakeBufferFrozen = false;
        s_p2WakeMotionGeneration = 0;
        s_p2WakeHoldPrimed = false;
        s_p2WakeHoldIssued = false;
        s_p2WakeNormalGeneration = 0;
        s_p2WakeNormalAttempts = 0;
        s_p2WakeOptionPicked = false;
        s_p2WakePrePickedOption = {};
    }
    if (s_p1RGPrearmed && now > s_p1RGPrearmExpiry) {
        LogOut("[AUTO-ACTION][RG] P1 RG pre-arm expired (no execution)", detailedLogging.load());
        if (s_p1RGPrearmGeneration != 0 &&
            GetAutoActionMotionTransactionOutcome(
                1, s_p1RGPrearmGeneration) ==
                AutoActionMotionOutcome::Pending) {
            (void)CancelAutoActionMotionTransactionIfGeneration(
                1, s_p1RGPrearmGeneration,
                "RG pre-arm expired before native acceptance");
        }
        s_p1RGPrearmed = false; s_p1RGPrearmIsSpecial = false; s_p1RGPrearmActionType = -1;
        s_p1RGPrearmGeneration = 0;
    }
    if (s_p2RGPrearmed && now > s_p2RGPrearmExpiry) {
        LogOut("[AUTO-ACTION][RG] P2 RG pre-arm expired (no execution)", detailedLogging.load());
        if (s_p2RGPrearmGeneration != 0 &&
            GetP2AutoActionMotionTransactionOutcome(
                s_p2RGPrearmGeneration) ==
                P2AutoActionMotionOutcome::Pending) {
            CancelP2AutoActionMotionTransaction(
                "RG pre-arm expired before native acceptance");
        }
        s_p2RGPrearmed = false; s_p2RGPrearmIsSpecial = false; s_p2RGPrearmActionType = -1;
        s_p2RGPrearmGeneration = 0;
    }
}

void MonitorAutoActions() {
    static short s_prevMoveID1 = 0, s_prevMoveID2 = 0;
    if (Mission::Engine::IsPracticeAutomationSuppressed()) return;
    std::unique_lock<std::recursive_mutex> stateLock(
        g_autoActionStateMutex, std::try_to_lock);
    if (!stateLock.owns_lock()) return;
    if (!AutoActionWorkPending()) {
        if (detailedLogging.load()) {
            static int s_lastIdleLogFrame = 0;
            int nowF = frameCounter.load();
            if (nowF - s_lastIdleLogFrame >= 5760) {
                LogOut("[AUTO-ACTION] Idle (disabled/no triggers) - skipping per-frame processing", true);
                s_lastIdleLogFrame = nowF;
            }
        }
        return;
    }
    const PerFrameSample &sample = GetCurrentPerFrameSample();
    short m1 = sample.moveID1;
    short m2 = sample.moveID2;
    MonitorAutoActionsImpl(m1, m2, s_prevMoveID1, s_prevMoveID2);
    s_prevMoveID1 = m1; s_prevMoveID2 = m2;
}

void MonitorAutoActions(short moveID1, short moveID2, short prevMoveID1, short prevMoveID2) {
    if (Mission::Engine::IsPracticeAutomationSuppressed()) return;
    std::unique_lock<std::recursive_mutex> stateLock(
        g_autoActionStateMutex, std::try_to_lock);
    if (!stateLock.owns_lock()) return;
    if (!AutoActionWorkPending()) return;
    const PerFrameSample &sample = GetCurrentPerFrameSample();
    short m1  = (sample.moveID1     == moveID1     ? sample.moveID1     : moveID1);
    short m2  = (sample.moveID2     == moveID2     ? sample.moveID2     : moveID2);
    short pm1 = (sample.prevMoveID1 == prevMoveID1 ? sample.prevMoveID1 : prevMoveID1);
    short pm2 = (sample.prevMoveID2 == prevMoveID2 ? sample.prevMoveID2 : prevMoveID2);
    MonitorAutoActionsImpl(m1, m2, pm1, pm2);
}

bool DropPrearmWorkAtPracticeBoundary() {
    // try_lock: the input hook holds g_p2ControlMutex here, and other paths nest the other way.
    std::unique_lock<std::recursive_mutex> stateLock(g_autoActionStateMutex, std::try_to_lock);
    if (!stateLock.owns_lock()) return false;
    s_p1WakePrearmed = false; s_p1WakePrearmExpiry = 0; s_p1WakePrearmActionType = -1;
    s_p1WakePrearmStrength = -1; s_p1WakePrearmIsSpecial = false; s_p1WakePrearmIsMacro = false;
    s_p1WakePrearmCharge = 0; s_p1WakeHoldPrimed = false; s_p1WakeHoldIssued = false;
    s_p2WakePrearmed = false; s_p2WakePrearmExpiry = 0; s_p2WakePrearmActionType = -1;
    s_p2WakePrearmStrength = -1; s_p2WakePrearmIsSpecial = false; s_p2WakePrearmIsMacro = false;
    s_p2WakePrearmCharge = 0; s_p2WakeHoldPrimed = false; s_p2WakeHoldIssued = false;
    s_p1RGPrearmed = false; s_p1RGPrearmIsSpecial = false; s_p1RGPrearmActionType = -1;
    s_p1RGPrearmExpiry = 0; s_p1RGPrearmGeneration = 0;
    s_p2RGPrearmed = false; s_p2RGPrearmIsSpecial = false; s_p2RGPrearmActionType = -1;
    s_p2RGPrearmExpiry = 0; s_p2RGPrearmGeneration = 0;
    return true;
}

void ResetActionFlags() {
    std::lock_guard<std::recursive_mutex> stateLock(g_autoActionStateMutex);
    p1ActionApplied = false;
    p2ActionApplied = false;
    RestoreP2ControlState();
    LogOut("[AUTO-ACTION] ResetActionFlags invoked (control restored if overridden)", true);
}

void ClearDelayStatesIfNonActionable(short moveID1, short moveID2, short prevMoveID1, short prevMoveID2, const char* source) {
    std::lock_guard<std::recursive_mutex> stateLock(g_autoActionStateMutex);
    if (!p1DelayState.isDelaying && !p2DelayState.isDelaying) return;
    const std::string clearSource = source ? source : "unknown";
    std::string p1Reason;
    std::string p2Reason;
    const bool p1ShouldCancel = ShouldCancelDelayForState(p1DelayState, prevMoveID1, moveID1, p1Reason);
    const bool p2ShouldCancel = ShouldCancelDelayForState(p2DelayState, prevMoveID2, moveID2, p2Reason);
    if (p1DelayState.isDelaying && p1ShouldCancel) {
        const int clearedTriggerType = p1DelayState.triggerType;
        const int remainingInternal = p1DelayState.delayFramesRemaining;
        const int remainingVisual = InternalTicksToVisualFramesCeil(remainingInternal);
        // ResetDelayState only drops our bookkeeping; without this the pulse
        // itself stays live and untracked, and would still press later.
        CancelPendingNormalPulseIfAny(1, p1DelayState, p1Reason.c_str());
        ResetDelayState(p1DelayState);
        LogOut("[AUTO-ACTION][DELAY] Cleared P1 delay source=" + clearSource +
               " trigger=" + TriggerTypeLabel(clearedTriggerType) +
               " reason=" + p1Reason +
               " prevMoveID=" + std::to_string(prevMoveID1) +
               " moveID=" + std::to_string(moveID1) +
               " visualRemaining=" + std::to_string(remainingVisual) +
               " internalRemaining=" + std::to_string(remainingInternal),
               true);
        if (clearedTriggerType == TRIGGER_ON_WAKEUP) {
            TraceAutoActionWakeEvent("p1-delay-cleared", 1, prevMoveID1, moveID1,
                                     "source=" + clearSource +
                                     " reason=" + p1Reason +
                                     " remainingInternal=" + std::to_string(remainingInternal));
        }
        p1TriggerActive = false;
        p1TriggerCooldown = 0;
    }
    if (p2DelayState.isDelaying && p2ShouldCancel) {
        const int clearedTriggerType = p2DelayState.triggerType;
        const int remainingInternal = p2DelayState.delayFramesRemaining;
        const int remainingVisual = InternalTicksToVisualFramesCeil(remainingInternal);
        // ResetDelayState only drops our bookkeeping; without this the pulse
        // itself stays live and untracked, and would still press later.
        CancelPendingNormalPulseIfAny(2, p2DelayState, p2Reason.c_str());
        ResetDelayState(p2DelayState);
        LogOut("[AUTO-ACTION][DELAY] Cleared P2 delay source=" + clearSource +
               " trigger=" + TriggerTypeLabel(clearedTriggerType) +
               " reason=" + p2Reason +
               " prevMoveID=" + std::to_string(prevMoveID2) +
               " moveID=" + std::to_string(moveID2) +
               " visualRemaining=" + std::to_string(remainingVisual) +
               " internalRemaining=" + std::to_string(remainingInternal),
               true);
        if (clearedTriggerType == TRIGGER_ON_WAKEUP) {
            TraceAutoActionWakeEvent("p2-delay-cleared", 2, prevMoveID2, moveID2,
                                     "source=" + clearSource +
                                     " reason=" + p2Reason +
                                     " remainingInternal=" + std::to_string(remainingInternal));
        }
        p2TriggerActive = false;
        p2TriggerCooldown = 0;
    }
}

void ClearDelayStatesIfNonActionable() {
    const PerFrameSample &sample = GetCurrentPerFrameSample();
    ClearDelayStatesIfNonActionable(sample.moveID1, sample.moveID2, sample.prevMoveID1, sample.prevMoveID2, "sample");
}

AutoActionApplyResult ApplyAutoAction(int playerNum, uintptr_t moveIDAddr,
                                      short currentMoveID, short prevMoveID,
                                      uint64_t* motionGenerationOut,
                                      uint64_t* normalGenerationOut,
                                      uint64_t* recipeGenerationOut) {
    if (motionGenerationOut) *motionGenerationOut = 0;
    if (normalGenerationOut) *normalGenerationOut = 0;
    if (recipeGenerationOut) *recipeGenerationOut = 0;
    if (playerNum < 1 || playerNum > 2) {
        return AutoActionApplyResult::Invalid;
    }
    if (currentMoveID == 0 && moveIDAddr != 0) {
        SafeReadMemory(moveIDAddr, &currentMoveID, sizeof(short));
    }
    int triggerType = (playerNum == 1) ? p1DelayState.triggerType : p2DelayState.triggerType;

    AutoActionLogScope applyScope("ApplyAutoAction", playerNum, triggerType);
    (void)applyScope;

    int actionType = 0;
    const TriggerDelayState& dstate = (playerNum == 1) ? p1DelayState : p2DelayState;
    if (dstate.chosenAction >= 0) {
        actionType = dstate.chosenAction;
    } else {
        TriggerOption poolPick{};
        if (PickRandomPoolOption(triggerType, playerNum, poolPick)) {
            actionType = poolPick.action;
        } else {
            switch (triggerType) {
                case TRIGGER_AFTER_BLOCK:
                    actionType = triggerAfterBlockAction.load();
                    break;
                case TRIGGER_ON_WAKEUP:
                    actionType = triggerOnWakeupAction.load();
                    break;
                case TRIGGER_AFTER_HITSTUN:
                    actionType = triggerAfterHitstunAction.load();
                    break;
                case TRIGGER_AFTER_AIRTECH:
                    actionType = triggerAfterAirtechAction.load();
                    break;
                case TRIGGER_ON_RG:
                    actionType = triggerOnRGAction.load();
                    break;
                default:
                    actionType = ACTION_5A;
                    break;
            }
        }
    }

    // Decide from the scoped generation, not queue admission: Failed or Cancelled gets one normal
    // dispatch at delay expiry; Pending or Accepted must not be injected twice.
    if (triggerType == TRIGGER_ON_RG) {
        if (playerNum == 1 && s_p1RGPrearmed && s_p1RGPrearmIsSpecial) {
            const AutoActionMotionOutcome outcome =
                GetAutoActionMotionTransactionOutcome(
                    1, s_p1RGPrearmGeneration);
            if (outcome == AutoActionMotionOutcome::Pending) {
                LogOut("[AUTO-ACTION][RG] P1 pre-arm still pending; deferring duplicate ApplyAutoAction",
                       detailedLogging.load());
                return AutoActionApplyResult::Busy;
            }
            if (outcome == AutoActionMotionOutcome::Accepted) {
                LogOut("[AUTO-ACTION][RG] P1 pre-arm generation=" +
                           std::to_string(s_p1RGPrearmGeneration) +
                           " outcome=" +
                           "accepted" +
                           "; skipping duplicate ApplyAutoAction",
                       true);
                p1TriggerActive = false; p1TriggerCooldown = 0;
                s_p1RGPrearmed = false;
                s_p1RGPrearmIsSpecial = false;
                s_p1RGPrearmActionType = -1;
                s_p1RGPrearmGeneration = 0;
                return AutoActionApplyResult::Accepted;
            }
            LogOut("[AUTO-ACTION][RG] P1 pre-arm did not execute; retrying at delay expiry", true);
            s_p1RGPrearmed = false;
            s_p1RGPrearmIsSpecial = false;
            s_p1RGPrearmActionType = -1;
            s_p1RGPrearmGeneration = 0;
        }
        if (playerNum == 2 && s_p2RGPrearmed && s_p2RGPrearmIsSpecial) {
            const P2AutoActionMotionOutcome outcome =
                GetP2AutoActionMotionTransactionOutcome(
                    s_p2RGPrearmGeneration);
            if (outcome == P2AutoActionMotionOutcome::Pending) {
                LogOut("[AUTO-ACTION][RG] P2 pre-arm still pending; deferring duplicate ApplyAutoAction",
                       detailedLogging.load());
                return AutoActionApplyResult::Busy;
            }
            if (outcome == P2AutoActionMotionOutcome::Accepted) {
                LogOut("[AUTO-ACTION][RG] P2 pre-arm generation=" +
                           std::to_string(s_p2RGPrearmGeneration) +
                           " outcome=" +
                           "accepted" +
                           "; skipping duplicate ApplyAutoAction",
                       true);
                p2TriggerActive = false; p2TriggerCooldown = 0;
                s_p2RGPrearmed = false;
                s_p2RGPrearmIsSpecial = false;
                s_p2RGPrearmActionType = -1;
                s_p2RGPrearmGeneration = 0;
                return AutoActionApplyResult::Accepted;
            }
            LogOut("[AUTO-ACTION][RG] P2 pre-arm did not execute; retrying at delay expiry", true);
            s_p2RGPrearmed = false;
            s_p2RGPrearmIsSpecial = false;
            s_p2RGPrearmActionType = -1;
            s_p2RGPrearmGeneration = 0;
        }
    }

    const int chargeSelection = CLAMP(
        dstate.chosenChargeFollowup, 0, 2);
    // Keep primary admission and follow-up reservation in one controller barrier, so no other input
    // writer can slip in after the move is queued but before its IC/FIC lane is reserved.
    std::unique_lock<std::recursive_mutex> chargeAdmissionLock;
    if (chargeSelection != 0 &&
        ActionSupportsChargeFollowup(actionType)) {
        chargeAdmissionLock =
            std::unique_lock<std::recursive_mutex>(g_p2ControlMutex);
    }

    if (actionType == ACTION_FINAL_MEMORY) {
        int charId = (playerNum == 1) ? displayData.p1CharID : displayData.p2CharID;
        P2AutoActionMotionSubmitResult fmSubmit =
            P2AutoActionMotionSubmitResult::Invalid;
        if (charId < 0) {
            LogOut("[AUTO-ACTION][FM] Aborting: unknown character id", true);
        } else {
            LogOut("[AUTO-ACTION][FM] Attempting Final Memory for player " + std::to_string(playerNum) +
                   " charId=" + std::to_string(charId), true);
            uint64_t fmGeneration = 0;
            bool ok = ExecuteFinalMemory(
                playerNum, charId, 1, &fmGeneration, &fmSubmit);
            if (ok) {
                if (motionGenerationOut) {
                    *motionGenerationOut = fmGeneration;
                }
                LogOut("[AUTO-ACTION][FM] P" + std::to_string(playerNum) +
                       " Final Memory queued as scoped input transaction", true);
                LogOut("[AUTO-ACTION][FM] Final Memory pattern dispatched", true);
                return AutoActionApplyResult::Accepted;
            } else {
                LogOut("[AUTO-ACTION][FM] Gate failed or pattern apply error", true);
            }
        }
        // FM has no motion fallback: a competing owner is retriable; an unknown command, failed gate,
        // or unavailable world ends this trigger instance.
        return fmSubmit == P2AutoActionMotionSubmitResult::Busy
            ? AutoActionApplyResult::Busy
            : AutoActionApplyResult::Invalid;
    }

    int resolvedStrength = (dstate.chosenStrength >= 0) ? dstate.chosenStrength : GetSpecialMoveStrength(actionType, triggerType);
    const int targetChar = playerNum == 1 ? displayData.p1CharID
                                           : displayData.p2CharID;
    if (!CharacterActionCatalog::IsKnownCharacter(targetChar)) {
        LogOut("[AUTO-ACTION][CATALOG] Rejected action because the target character is not resolved",
               true);
        return AutoActionApplyResult::Invalid;
    }
    const int catalogStrength = CatalogStrengthForAction(actionType, resolvedStrength);
    if (!CharacterActionCatalog::IsAvailable(targetChar, actionType,
                                              catalogStrength)) {
        LogOut("[AUTO-ACTION][CATALOG] Rejected unavailable action=" +
                   std::to_string(actionType) + " strength=" +
                   std::to_string(catalogStrength) + " char=" +
                   std::to_string(targetChar),
               true);
        return AutoActionApplyResult::Invalid;
    }
    // Two-command recipe: native 44 enters Kaori's backdash, native 66 only in the frame-4/5 cancel
    // window. Keep it out of the generic dash conversion, where it maps to MOTION_NONE.
    if (actionType == ACTION_KAORI_RECOIL_DUCK) {
        uint64_t generation = 0;
        const AutoActionMotionSubmitResult submit =
            KaoriRecoilDuck::Begin(playerNum, targetChar, &generation);
        if (submit == AutoActionMotionSubmitResult::Accepted &&
            recipeGenerationOut) {
            *recipeGenerationOut = generation;
        }
        return submit == AutoActionMotionSubmitResult::Accepted
            ? AutoActionApplyResult::Accepted
            : submit == AutoActionMotionSubmitResult::Busy
                ? AutoActionApplyResult::Busy
                : AutoActionApplyResult::Invalid;
    }
    int motionType = ConvertTriggerActionToMotion(actionType, triggerType, resolvedStrength);
    int buttonMask = 0;
    
    // Explicit switch because the action ID ranges overlap.
    switch (actionType) {
        case ACTION_5A: buttonMask = GAME_INPUT_A; break;
        case ACTION_5B: buttonMask = GAME_INPUT_B; break;
        case ACTION_5C: buttonMask = GAME_INPUT_C; break;
        case ACTION_5D: buttonMask = GAME_INPUT_D; break;
        
        case ACTION_2A: buttonMask = GAME_INPUT_A; break;
        case ACTION_2B: buttonMask = GAME_INPUT_B; break;
        case ACTION_2C: buttonMask = GAME_INPUT_C; break;
        case ACTION_2D: buttonMask = GAME_INPUT_D; break;
        
        case ACTION_JA: buttonMask = GAME_INPUT_A; break;
        case ACTION_JB: buttonMask = GAME_INPUT_B; break;
        case ACTION_JC: buttonMask = GAME_INPUT_C; break;
        case ACTION_JD: buttonMask = GAME_INPUT_D; break;
        
        case ACTION_6A: buttonMask = GAME_INPUT_A; break;
        case ACTION_6B: buttonMask = GAME_INPUT_B; break;
        case ACTION_6C: buttonMask = GAME_INPUT_C; break;
        case ACTION_6D: buttonMask = GAME_INPUT_D; break;
        
        case ACTION_4A: buttonMask = GAME_INPUT_A; break;
        case ACTION_4B: buttonMask = GAME_INPUT_B; break;
        case ACTION_4C: buttonMask = GAME_INPUT_C; break;
        case ACTION_4D: buttonMask = GAME_INPUT_D; break;

        case ACTION_1X:
        case ACTION_3X:
        case ACTION_J2X:
        case ACTION_J6X:
        case ACTION_66X:
        case ACTION_662X:
        case ACTION_664X:
            buttonMask = (1 << (4 + CLAMP(resolvedStrength, 0, 3)));
            break;
        
        case ACTION_QCF:
        case ACTION_DP:
        case ACTION_QCB:
        case ACTION_421:
        case ACTION_SUPER1:
        case ACTION_SUPER2:
        case ACTION_236236:
        case ACTION_214214:
        case ACTION_641236:
        case ACTION_463214:
        case ACTION_412:
        case ACTION_22:
        case ACTION_4123641236:
        case ACTION_6321463214: {
            int strength = resolvedStrength;
            if (strength < 0) strength = 0;
            if (strength > 3) strength = 3;
            buttonMask = (1 << (4 + strength));  // A=16, B=32, C=64, D=128
            break;
        }
        
        default:
            break;
    }

    LogOut("[AUTO-ACTION] Converting action=" + std::to_string(actionType) +
           " trigger=" + std::to_string(triggerType) +
           " strength=" + std::to_string(resolvedStrength) +
           " => motionType=" + std::to_string(motionType) +
           " prelimButtonMask=" + std::to_string(buttonMask), true);
    if (triggerType == TRIGGER_ON_WAKEUP) {
        const short traceMoveID = static_cast<short>(GetPlayerMoveID(playerNum));
        TraceAutoActionWakeEvent("apply-auto-action:wakeup", playerNum,
                                 (prevMoveID >= 0) ? prevMoveID : traceMoveID,
                                 traceMoveID,
                                 "action=" + std::to_string(actionType) +
                                 " strength=" + std::to_string(resolvedStrength) +
                                 " motion=" + std::to_string(motionType) +
                                 " buttonMask=" + std::to_string(buttonMask) +
                                 " currentArg=" + std::to_string(currentMoveID));
    }

    if (buttonMask == 0) {
        LogOut("[AUTO-ACTION] Button mask was 0; defaulting to A (likely non-button action)", true);
        buttonMask = GAME_INPUT_A;
    }
    else {
        LogOut("[AUTO-ACTION] Final buttonMask=" + std::to_string(buttonMask) +
               " (A=16,B=32,C=64,D=128)", true);
    }
    
    bool success = false;
    AutoActionApplyResult applyResult = AutoActionApplyResult::Invalid;
    bool isRegularMove = NormalInputPolicy::IsNormalMotion(motionType);
    bool isSpecialMove = (motionType >= MOTION_236A);
    if (IsDashRecipeAction(actionType)) {
        isSpecialMove = false;
    }

    if (actionType == ACTION_JUMP) {
        int dir = (dstate.chosenStrength >= 0) ? dstate.chosenStrength : 0;
        if (dstate.chosenStrength < 0) {
            switch (triggerType) {
                case TRIGGER_AFTER_BLOCK: dir = triggerAfterBlockStrength.load(); break;
                case TRIGGER_ON_WAKEUP: dir = triggerOnWakeupStrength.load(); break;
                case TRIGGER_AFTER_HITSTUN: dir = triggerAfterHitstunStrength.load(); break;
                case TRIGGER_AFTER_AIRTECH: dir = triggerAfterAirtechStrength.load(); break;
                case TRIGGER_ON_RG: dir = triggerOnRGStrength.load(); break;
                default: dir = 0; break;
            }
        }

        uint8_t mask = GAME_INPUT_UP;
        if (dir == 1 || dir == 2) {
            bool facingRight = GetPlayerFacingDirection(playerNum);
            if (dir == 1) {
                mask |= (facingRight ? GAME_INPUT_RIGHT : GAME_INPUT_LEFT);
            } else if (dir == 2) {
                mask |= (facingRight ? GAME_INPUT_LEFT : GAME_INPUT_RIGHT);
            }
        }

    LogOut("[AUTO-ACTION] Injecting Jump via immediate writer (dir=" + std::to_string(dir) + ")", true);
    success = ImmediateInput::PressFor(playerNum, mask, 3);
    if (success) {
        if (playerNum == 2) { p2TriggerActive = false; p2TriggerCooldown = 0; }
        else { p1TriggerActive = false; p1TriggerCooldown = 0; }
        LogOut("[AUTO-ACTION] Jump scheduled (cooldown cleared)", true);
    } else {
        LogOut("[AUTO-ACTION] Jump input lane busy; retaining trigger", true);
    }

        applyResult = success ? AutoActionApplyResult::Accepted
                              : (g_onlineModeActive.load(std::memory_order_acquire)
                                     ? AutoActionApplyResult::Invalid
                                     : AutoActionApplyResult::Busy);
    }

    if (actionType == ACTION_BLOCK) {
        const uint8_t mask = GetPlayerFacingDirection(playerNum) ? GAME_INPUT_LEFT : GAME_INPUT_RIGHT;
        success = ImmediateInput::PressFor(playerNum, mask, 3);
        if (success) {
            if (playerNum == 2) { p2TriggerActive = false; p2TriggerCooldown = 0; }
            else { p1TriggerActive = false; p1TriggerCooldown = 0; }
        }
        applyResult = success ? AutoActionApplyResult::Accepted
                              : (g_onlineModeActive.load(std::memory_order_acquire)
                                     ? AutoActionApplyResult::Invalid
                                     : AutoActionApplyResult::Busy);
    }

    if (!success && IsDashRecipeAction(actionType)) {
        LogOut("[AUTO-ACTION] Queuing dash motion via native buffer transaction (" + GetMotionTypeName(motionType) + ")", true);
        g_injectImmediateOnly[playerNum].store(false);
        bool isForwardDash = actionType != ACTION_BACKDASH &&
                             actionType != ACTION_BACK_DASH;
        uint64_t dashMotionGeneration = 0;
        // Both fighters use the generation-owned transaction: it humanizes an AI-flagged target for one
        // producer pass. EFZ's AI branch never scans the ring, so a legacy queued dash never comes out.
        {
            const AutoActionMotionSubmitResult submit =
                SubmitAutoActionMotionTransaction(
                    playerNum, motionType, 0, 1, &dashMotionGeneration);
            success = submit == AutoActionMotionSubmitResult::Accepted;
            if (success && motionGenerationOut) {
                *motionGenerationOut = dashMotionGeneration;
            }
            applyResult = submit == AutoActionMotionSubmitResult::Accepted
                ? AutoActionApplyResult::Accepted
                : submit == AutoActionMotionSubmitResult::Busy
                    ? AutoActionApplyResult::Busy
                    : AutoActionApplyResult::Invalid;
        }
        if (success && isForwardDash) {
            int fdf = 0;
            if (actionType == ACTION_66X) {
                fdf = 1 + CLAMP(resolvedStrength, 0, 2);
            } else if (actionType == ACTION_662X) {
                fdf = 4 + CLAMP(resolvedStrength, 0, 2);
            } else if (actionType == ACTION_664X) {
                fdf = 7 + CLAMP(resolvedStrength, 0, 2);
            } else {
                fdf = forwardDashFollowup.load();
            }
            if (fdf > 0) {
                ScheduleForwardDashFollowup(
                    playerNum, fdf, true, dashMotionGeneration);
            }
        } else if (success && actionType == ACTION_BACKDASH) {
        }
    }
    
    if (!success && isRegularMove) {
        // Human fighters use EFZ's native poll; AI fighters get the pulse after their original producer,
        // before the character consumer. Owned until the consumer confirms it and a neutral pass follows.
        uint64_t generation = 0;
        const auto submit = QueueAutoActionNormalPulse(
            playerNum, motionType,
            NormalInputPolicy::Timing::WhenActionable,
            &generation);
        if (submit == NormalInputPolicy::SubmitResult::Accepted &&
            normalGenerationOut) {
            *normalGenerationOut = generation;
        }
        success = submit == NormalInputPolicy::SubmitResult::Accepted;
        applyResult = submit == NormalInputPolicy::SubmitResult::Accepted
            ? AutoActionApplyResult::Accepted
            : submit == NormalInputPolicy::SubmitResult::Busy
                ? AutoActionApplyResult::Busy
                : AutoActionApplyResult::Invalid;
        LogOut("[AUTO-ACTION] Regular move " + GetMotionTypeName(motionType) +
               (success ? " queued" : " rejected") +
               " as engine normal pulse gen=" + std::to_string(generation),
               true);
    }
    else if (!success && isSpecialMove) {
        LogOut("[AUTO-ACTION] Applying special move " + GetMotionTypeName(motionType) +
               " via native buffer transaction", true);
        g_injectImmediateOnly[playerNum].store(false);
        {
            uint64_t specialMotionGeneration = 0;
            const AutoActionMotionSubmitResult submit =
                SubmitAutoActionMotionTransaction(
                    playerNum, motionType, buttonMask,
                    triggerType == TRIGGER_ON_RG ? 120 : 1,
                    &specialMotionGeneration);
            success = submit == AutoActionMotionSubmitResult::Accepted;
            if (success && motionGenerationOut) {
                *motionGenerationOut = specialMotionGeneration;
            }
            applyResult = submit == AutoActionMotionSubmitResult::Accepted
                ? AutoActionApplyResult::Accepted
                : submit == AutoActionMotionSubmitResult::Busy
                    ? AutoActionApplyResult::Busy
                    : AutoActionApplyResult::Invalid;
        }
    }

    if (success) {
        if (isSpecialMove) {
            LogOut("[AUTO-ACTION] P" + std::to_string(playerNum) +
                   " special transaction armed", true);
        }
        if (chargeSelection != 0 &&
            ActionSupportsChargeFollowup(actionType)) {
            const auto mode = static_cast<AutoActionChargePolicy::Mode>(
                chargeSelection);
            if (!ArmAutoActionChargeFollowup(
                    playerNum, mode, triggerType, actionType)) {
                LogOut("[AUTO-ACTION][CHARGE] Primary action was admitted, "
                       "but its selected IC/FIC follow-up could not reserve "
                       "the input lane",
                       true);
            }
        }
    } else {
        LogOut("[AUTO-ACTION] Failed to apply move " + GetMotionTypeName(motionType), true);
        // Scoped submissions restore only their own AI snapshot. A Busy result may belong to another
        // owner; this caller did not acquire that lease and must never release it.
    }

    return applyResult;
}

bool AcquireTutorialP2Control(uint64_t& tokenOut) {
    tokenOut = 0;
    std::lock_guard<std::recursive_mutex> lk(g_p2ControlMutex);
    if (g_onlineModeActive.load() ||
        g_tutorialP2Token.load(std::memory_order_acquire) != 0 ||
        g_p2ControlOverridden ||
        IsScopedInputReserved(2) ||
        IsP2AutoActionMotionTransactionActive()) return false;

    // Tutorial lease supersedes an in-flight auto normal. Cancel it through the normal consumer lock
    // before the AI flag changes, so an AI post-write cannot race the new owner.
    CancelAutoActionNormalPulse(2);

    const uintptr_t base = GetEFZBase();
    if (!base) return false;
    uintptr_t gameState = 0;
    uintptr_t p2Char = 0;
    if (!SafeReadMemory(base + EFZ_BASE_OFFSET_GAME_STATE, &gameState, sizeof(gameState)) ||
        !gameState ||
        !SafeReadMemory(base + EFZ_BASE_OFFSET_P2, &p2Char, sizeof(p2Char)) ||
        !p2Char) return false;

    TutorialP2Snapshot snapshot;
    snapshot.gameState = gameState;
    snapshot.character = p2Char;
    constexpr uintptr_t kP2CpuFlagOffset = 4932;
    if (!SafeReadMemory(gameState + kP2CpuFlagOffset, &snapshot.cpuFlag,
                        sizeof(snapshot.cpuFlag)) ||
        !SafeReadMemory(p2Char + AI_CONTROL_FLAG_OFFSET, &snapshot.aiFlag,
                        sizeof(snapshot.aiFlag))) return false;

    const uint32_t human = 0;
    uint32_t verify = 0xFFFFFFFFu;
    if (!SafeWriteMemory(p2Char + AI_CONTROL_FLAG_OFFSET, &human, sizeof(human)) ||
        !SafeReadMemory(p2Char + AI_CONTROL_FLAG_OFFSET, &verify, sizeof(verify)) ||
        verify != human) return false;

    uint64_t token = g_nextTutorialP2Token.fetch_add(1, std::memory_order_relaxed);
    if (token == 0) token = g_nextTutorialP2Token.fetch_add(1, std::memory_order_relaxed);
    g_tutorialP2Snapshot = snapshot;
    g_originalP2ControlFlag = snapshot.aiFlag;
    g_p2ControlOverridden = true;
    g_tutorialP2Token.store(token, std::memory_order_release);
    tokenOut = token;
    LogOut("[TUTORIAL][INPUT_LEASE] Acquired P2 control token=" +
           std::to_string(token) + " cpu=" + std::to_string(snapshot.cpuFlag) +
           " ai=" + std::to_string(snapshot.aiFlag), true);
    return true;
}

bool TutorialP2ControlLeaseActive() {
    return g_tutorialP2Token.load(std::memory_order_acquire) != 0;
}

static bool ReleaseTutorialP2ControlImpl(uint64_t token,
                                        const EfzTmEntryV1* heldWorld) {
    if (token == 0) return true;
    std::lock_guard<std::recursive_mutex> lk(g_p2ControlMutex);
    if (g_tutorialP2Token.load(std::memory_order_acquire) != token) return true;

    const bool online = g_onlineModeActive.load(std::memory_order_acquire);
    // End a scoped wake normal's transaction before restoring the saved controller.
    // Once online, teardown must not write local controller state over netplay's human-control setup.
    if (!online && !heldWorld) {
        CancelAutoActionNormalPulse(2);
        if (IsAutoActionNormalPulseActive(2) ||
            IsAutoActionNormalPulseOwningImmediateRegisters(2)) {
            g_tutorialP2ReleaseRequested.store(true, std::memory_order_release);
            return false;
        }
    }

    const TutorialP2Snapshot snapshot = g_tutorialP2Snapshot;
    if (online && heldWorld) return false;
    if (online) {
        LogOut("[TUTORIAL][INPUT_LEASE] Netplay active; discarded saved P2 controller snapshot without writes", true);
        g_pendingControlRestore.store(false, std::memory_order_release);
        g_p2ControlOverridden.store(false, std::memory_order_release);
        g_tutorialP2Snapshot = TutorialP2Snapshot{};
        g_tutorialP2Token.store(0, std::memory_order_release);
        g_tutorialP2ReleaseRequested.store(false, std::memory_order_release);
        return true;
    }

    const uintptr_t base = heldWorld ? heldWorld->efzBase : GetEFZBase();
    uintptr_t gameState = heldWorld ? heldWorld->gameSystem : 0;
    uintptr_t p2Char = heldWorld ? heldWorld->player2 : 0;
    if (!heldWorld && (!base ||
        !SafeReadMemory(base + EFZ_BASE_OFFSET_GAME_STATE, &gameState,
                        sizeof(gameState)) ||
        !SafeReadMemory(base + EFZ_BASE_OFFSET_P2, &p2Char,
                        sizeof(p2Char)) ||
        !gameState || !p2Char)) {
        g_tutorialP2ReleaseRequested.store(true, std::memory_order_release);
        LogOut("[TUTORIAL][INPUT_LEASE] P2 world read failed; retaining restore token",
               true);
        return false;
    }

    if (gameState == snapshot.gameState && p2Char == snapshot.character) {
        constexpr uintptr_t kP2CpuFlagOffset = 4932;
        if (!heldWorld && !FullCleanupAfterToggle(2)) {
            g_tutorialP2ReleaseRequested.store(true, std::memory_order_release);
            LogOut("[TUTORIAL][INPUT_LEASE] P2 cleanup failed; retaining restore token",
                   true);
            return false;
        }
        const bool cpuWrite = SafeWriteMemory(
            gameState + kP2CpuFlagOffset, &snapshot.cpuFlag,
            sizeof(snapshot.cpuFlag));
        const bool aiWrite = SafeWriteMemory(
            p2Char + AI_CONTROL_FLAG_OFFSET, &snapshot.aiFlag,
            sizeof(snapshot.aiFlag));
        uint8_t cpuAfter = 0xFF;
        uint32_t aiAfter = 0xFFFFFFFFu;
        const bool verified = cpuWrite && aiWrite &&
            SafeReadMemory(gameState + kP2CpuFlagOffset, &cpuAfter,
                           sizeof(cpuAfter)) &&
            SafeReadMemory(p2Char + AI_CONTROL_FLAG_OFFSET, &aiAfter,
                           sizeof(aiAfter)) &&
            cpuAfter == snapshot.cpuFlag && aiAfter == snapshot.aiFlag;
        if (!verified) {
            g_tutorialP2ReleaseRequested.store(true, std::memory_order_release);
            LogOut("[TUTORIAL][INPUT_LEASE] P2 flag restore failed verification; retaining token",
                   true);
            return false;
        }
    } else {
        if (heldWorld) return false;
        LogOut("[TUTORIAL][INPUT_LEASE] P2 world changed; stale flags were not restored", true);
    }
    g_pendingControlRestore.store(false, std::memory_order_release);
    g_p2ControlOverridden.store(false, std::memory_order_release);
    g_tutorialP2Snapshot = TutorialP2Snapshot{};
    g_tutorialP2Token.store(0, std::memory_order_release);
    g_tutorialP2ReleaseRequested.store(false, std::memory_order_release);
    LogOut("[TUTORIAL][INPUT_LEASE] Released P2 control token=" +
           std::to_string(token), true);
    return true;
}

bool ReleaseTutorialP2Control(uint64_t token) {
    return ReleaseTutorialP2ControlImpl(token, nullptr);
}

static void ScheduleLegacyP2RestoreRetry(const char* reason) {
    g_pendingControlRestore.store(true, std::memory_order_release);
    g_pendingRestoreTimestamp.store(GetTickCount(), std::memory_order_release);
    LogOut(std::string("[AUTO-ACTION][CONTROL] P2 restore retained for retry: ") +
               (reason ? reason : "unspecified"),
           true);
}

// g_p2ControlMutex must be held. Returns true only when the exact snapshot was
// restored and verified, or when a proven replacement world made it stale.
static bool RestoreLegacyP2ControllerLocked(const char* operation,
                                            bool scrubRuntime,
                                            bool preserveMotionToken,
                                            const EfzTmEntryV1* heldWorld = nullptr) {
    if (!g_p2ControlOverridden.load(std::memory_order_acquire)) return true;
    if (g_tutorialP2Token.load(std::memory_order_acquire) != 0) return false;

    if (g_onlineModeActive.load(std::memory_order_acquire)) {
        if (heldWorld) return false;
        // Netplay publication happens only after the offline drain. Once online,
        // this path is bookkeeping-only and must not write controller memory.
        g_p2ControlOverridden.store(false, std::memory_order_release);
        g_legacyP2ControlSnapshot = LegacyP2ControlSnapshot{};
        g_pendingControlRestore.store(false, std::memory_order_release);
        g_pendingRestoreTimestamp.store(0, std::memory_order_release);
        return true;
    }

    if (!g_legacyP2ControlSnapshot.valid) {
        ScheduleLegacyP2RestoreRetry("missing legacy snapshot");
        return false;
    }

    const uintptr_t base = heldWorld ? heldWorld->efzBase : GetEFZBase();
    uintptr_t gameState = heldWorld ? heldWorld->gameSystem : 0;
    uintptr_t character = heldWorld ? heldWorld->player2 : 0;
    if (!heldWorld && (!base ||
        !SafeReadMemory(base + EFZ_BASE_OFFSET_GAME_STATE, &gameState,
                        sizeof(gameState)) ||
        !SafeReadMemory(base + EFZ_BASE_OFFSET_P2, &character,
                        sizeof(character)) ||
        !gameState || !character)) {
        ScheduleLegacyP2RestoreRetry("world read failed");
        return false;
    }

    if (gameState != g_legacyP2ControlSnapshot.gameState ||
        character != g_legacyP2ControlSnapshot.character) {
        if (heldWorld) return false;
        LogOut(std::string("[AUTO-ACTION][CONTROL] ") + operation +
                   " discarded stale P2 snapshot after world replacement",
               true);
        g_p2ControlOverridden.store(false, std::memory_order_release);
        g_legacyP2ControlSnapshot = LegacyP2ControlSnapshot{};
        g_pendingControlRestore.store(false, std::memory_order_release);
        g_pendingRestoreTimestamp.store(0, std::memory_order_release);
        return true;
    }

    if (scrubRuntime && !FullCleanupAfterToggle(2)) {
        ScheduleLegacyP2RestoreRetry("input cleanup failed");
        return false;
    }
    if (!scrubRuntime && !preserveMotionToken && !NeutralizeMotionToken(2)) {
        ScheduleLegacyP2RestoreRetry("token cleanup failed");
        return false;
    }

    const auto snapshot = g_legacyP2ControlSnapshot;
    const bool cpuWrite = SafeWriteMemory(gameState + 4932, &snapshot.cpuFlag,
                                          sizeof(snapshot.cpuFlag));
    const bool aiWrite = SafeWriteMemory(character + AI_CONTROL_FLAG_OFFSET,
                                         &snapshot.aiFlag,
                                         sizeof(snapshot.aiFlag));
    uint8_t cpuAfter = 0xFF;
    uint32_t aiAfter = 0xFFFFFFFFu;
    const bool verified = cpuWrite && aiWrite &&
        SafeReadMemory(gameState + 4932, &cpuAfter, sizeof(cpuAfter)) &&
        SafeReadMemory(character + AI_CONTROL_FLAG_OFFSET, &aiAfter,
                       sizeof(aiAfter)) &&
        cpuAfter == snapshot.cpuFlag && aiAfter == snapshot.aiFlag;

    std::ostringstream audit;
    audit << "[AUDIT][AI] " << operation
          << " cpu=" << static_cast<int>(snapshot.cpuFlag)
          << "->" << static_cast<int>(cpuAfter)
          << " ai=" << snapshot.aiFlag << "->" << aiAfter
          << " verified=" << (verified ? 1 : 0);
    LogOut(audit.str(), true);
    if (!verified) {
        ScheduleLegacyP2RestoreRetry("flag verification failed");
        return false;
    }

    g_originalP2ControlFlag = snapshot.aiFlag;
    g_p2ControlOverridden.store(false, std::memory_order_release);
    g_legacyP2ControlSnapshot = LegacyP2ControlSnapshot{};
    g_pendingControlRestore.store(false, std::memory_order_release);
    g_pendingRestoreTimestamp.store(0, std::memory_order_release);
    return true;
}

uint32_t RetireAutoActionControllers(const EfzTmEntryV1& world) {
    std::lock_guard<std::recursive_mutex> lock(g_p2ControlMutex);
    if (world.nativeThreadId != GetCurrentThreadId() || !world.gameSystem ||
        !world.player2 || g_onlineModeActive.load(std::memory_order_acquire))
        return EFZ_TM_FAULTED;
    const uint64_t tutorial = g_tutorialP2Token.load(std::memory_order_acquire);
    if (tutorial && (g_tutorialP2Snapshot.gameState != world.gameSystem ||
        g_tutorialP2Snapshot.character != world.player2)) return EFZ_TM_FAULTED;
    if (g_legacyP2ControlSnapshot.valid &&
        (g_legacyP2ControlSnapshot.gameState != world.gameSystem ||
         g_legacyP2ControlSnapshot.character != world.player2)) return EFZ_TM_FAULTED;
    if (IsP2AutoActionMotionTransactionActive() ||
        IsAutoActionNormalPulseActive(2) ||
        IsAutoActionNormalPulseOwningImmediateRegisters(2)) return EFZ_TM_PENDING;
    if (!ReleaseTutorialP2ControlImpl(tutorial, &world)) return EFZ_TM_PENDING;
    return RestoreLegacyP2ControllerLocked("RetirePracticeInput", false, true, &world)
        ? EFZ_TM_READY : EFZ_TM_PENDING;
}

void EnableP2ControlForAutoAction() {
    std::lock_guard<std::recursive_mutex> lk(g_p2ControlMutex);
    CancelP2AutoActionMotionTransaction("legacy macro owner takeover");
    if (IsP2AutoActionMotionTransactionActive()) return;
    if (g_tutorialP2Token.load(std::memory_order_acquire) != 0) return;
    CancelAutoActionNormalPulse(2);
    // Never modify game state during online mode.
    if (g_onlineModeActive.load()) return;

    uintptr_t base = GetEFZBase();
    if (!base) {
        LogOut("[AUTO-ACTION] Failed to get EFZ base address", true);
        return;
    }
    
    uintptr_t gameStatePtr = 0;
    uintptr_t p2CharPtr = 0;
    if (!SafeReadMemory(base + EFZ_BASE_OFFSET_GAME_STATE, &gameStatePtr,
                        sizeof(gameStatePtr)) || !gameStatePtr ||
        !SafeReadMemory(base + EFZ_BASE_OFFSET_P2, &p2CharPtr,
                        sizeof(p2CharPtr)) || !p2CharPtr) {
        LogOut("[AUTO-ACTION] Failed to resolve P2 world for control override", true);
        return;
    }
    
    uint8_t currentCpuFlag = 1;
    uint32_t currentAIFlag = 1;
    if (!SafeReadMemory(gameStatePtr + 4932, &currentCpuFlag,
                        sizeof(currentCpuFlag)) ||
        !SafeReadMemory(p2CharPtr + AI_CONTROL_FLAG_OFFSET, &currentAIFlag,
                        sizeof(currentAIFlag))) {
        LogOut("[AUTO-ACTION] Failed to read P2 control state, aborting override", true);
        return;
    }

    bool alreadyOwned = g_p2ControlOverridden.load(std::memory_order_acquire);
    if (alreadyOwned && g_legacyP2ControlSnapshot.valid &&
        (g_legacyP2ControlSnapshot.gameState != gameStatePtr ||
         g_legacyP2ControlSnapshot.character != p2CharPtr)) {
        LogOut("[AUTO-ACTION][CONTROL] Discarded stale legacy P2 ownership before new takeover",
               true);
        g_p2ControlOverridden.store(false, std::memory_order_release);
        g_legacyP2ControlSnapshot = LegacyP2ControlSnapshot{};
        alreadyOwned = false;
    }
    if (alreadyOwned && !g_legacyP2ControlSnapshot.valid) {
        LogOut("[AUTO-ACTION][CONTROL] Refusing P2 takeover without an exact legacy snapshot",
               true);
        return;
    }

    if (!alreadyOwned) {
        g_legacyP2ControlSnapshot = LegacyP2ControlSnapshot{
            true, gameStatePtr, p2CharPtr, currentCpuFlag, currentAIFlag};
        g_originalP2ControlFlag = currentAIFlag;
        LogOut("[AUTO-ACTION] Saving original P2 control flags: cpu=" +
                   std::to_string(currentCpuFlag) + " ai=" +
                   std::to_string(currentAIFlag),
               true);
    } else if (currentAIFlag != 0) {
        LogOut("[AUTO-ACTION] P2 control flag was reset to " + std::to_string(currentAIFlag) + ", setting back to human control", true);
    }
    
    uint32_t humanControlFlag = 0;
    uint32_t before = 0xFFFFFFFFu, after = 0xFFFFFFFFu;
    SafeReadMemory(p2CharPtr + AI_CONTROL_FLAG_OFFSET, &before, sizeof(uint32_t));
    bool okWrite = SafeWriteMemory(p2CharPtr + AI_CONTROL_FLAG_OFFSET, &humanControlFlag, sizeof(uint32_t));
    SafeReadMemory(p2CharPtr + AI_CONTROL_FLAG_OFFSET, &after, sizeof(uint32_t));
    std::ostringstream oss; oss << "[AUDIT][AI] EnableP2ControlForAutoAction @0x" << std::hex << (p2CharPtr + AI_CONTROL_FLAG_OFFSET)
                                << std::dec << " before=" << before << " write=0 after=" << after
                                << " okWrite=" << (okWrite?"1":"0");
    LogOut(oss.str(), true);
    if (okWrite && after == 0) {
        g_p2ControlOverridden.store(true, std::memory_order_release);
        LogOut("[AUTO-ACTION] P2 control set to human (0) for legacy macro ownership", true);
    } else {
        if (!alreadyOwned) {
            g_legacyP2ControlSnapshot = LegacyP2ControlSnapshot{};
        }
        LogOut("[AUTO-ACTION] P2 control write failed verification, flag still = " + std::to_string(after), true);
    }
}

// Full restore for the legacy P2 owner, with the whole-buffer cleanup macro playback expects.
// Scoped transactions clean up at the native consumer and must never depend on this.
void RestoreP2ControlState() {
    std::lock_guard<std::recursive_mutex> lk(g_p2ControlMutex);
    CancelP2AutoActionMotionTransaction("legacy control restore");
    if (IsP2AutoActionMotionTransactionActive()) return;
    if (g_tutorialP2Token.load(std::memory_order_acquire) != 0) return;
    // Dash follow-ups use the already-human native route: restoring AI or clearing +392..397 before
    // its neutral pass would cut off the normal. Leave the restore monitor armed and retry afterward.
    if (IsAutoActionNormalPulseActive(2)) {
        LogOut("[AUTO-ACTION] Deferred P2 control restore until normal pulse completes",
               detailedLogging.load());
        return;
    }

    if (g_p2ControlOverridden.load(std::memory_order_acquire)) {
        const PerFrameSample &restoreSample = GetCurrentPerFrameSample();
        short restoreMoveID2 = restoreSample.moveID2;
        StopBufferFreezing();
        
        p2TriggerActive = true;
    p2TriggerCooldown = TRIGGER_COOLDOWN_FRAMES;
        LogOut("[AUTO-ACTION] Enforcing extended trigger cooldown after control restore", true);
        
        if (RestoreLegacyP2ControllerLocked("RestoreP2ControlState",
                                            true, false)) {
            LogFlowSequenceEvent(2, "control restore buffer cleared",
                                 restoreMoveID2, restoreMoveID2);
            EndFlowSequence(2, "ControlRestored", restoreMoveID2);
        }
    }
}

// Partial restore: CPU and AI flags only, preserving the input buffer and motion token.
// Use only where buffered inputs must survive, such as the Counter RG fast restore.
static void RestoreP2ControlFlagOnly() {
    std::lock_guard<std::recursive_mutex> lk(g_p2ControlMutex);
    if (g_tutorialP2Token.load(std::memory_order_acquire) != 0) return;
    if (!g_p2ControlOverridden.load(std::memory_order_acquire)) return;
    const PerFrameSample &restoreSample = GetCurrentPerFrameSample();
    short restoreMoveID2 = restoreSample.moveID2;
    if (RestoreLegacyP2ControllerLocked("RestoreP2ControlFlagOnly",
                                        false, false)) {
        EndFlowSequence(2, "ControlFlagOnly", restoreMoveID2);
    }
}

void RestoreP2ControlFlagsPreserveBufferAndTokenForMacro() {
    std::lock_guard<std::recursive_mutex> lk(g_p2ControlMutex);
    if (g_tutorialP2Token.load(std::memory_order_acquire) != 0) return;
    if (!g_p2ControlOverridden.load(std::memory_order_acquire)) return;
    const PerFrameSample &restoreSample = GetCurrentPerFrameSample();
    short restoreMoveID2 = restoreSample.moveID2;
    if (RestoreLegacyP2ControllerLocked(
            "RestoreP2ControlFlagsPreserveBufferAndTokenForMacro",
            false, true)) {
        EndFlowSequence(2, "ControlFlagOnlyWakeMacro", restoreMoveID2);
    }
}

void ProcessAutoControlRestore() {
    if (g_tutorialP2ReleaseRequested.load(std::memory_order_acquire)) {
        const uint64_t token =
            g_tutorialP2Token.load(std::memory_order_acquire);
        if (token != 0 && !ReleaseTutorialP2Control(token)) {
            return;
        }
    }

    // Never process control restoration during online mode.
    if (g_onlineModeActive.load()) {
        if (g_p2ControlOverridden.load(std::memory_order_acquire)) {
            RestoreP2ControlState();
        } else {
            g_pendingControlRestore.store(false, std::memory_order_release);
            g_pendingRestoreTimestamp.store(0, std::memory_order_release);
        }
        return;
    }

    if (!IsMatchPhase()) {
        InvalidateCachedCharacterIDs("phase change");
        if (g_pendingControlRestore.load()) {
            LogOut("[AUTO-ACTION] Phase left MATCH during restore; forcing cleanup", true);
            RestoreP2ControlState();
            // Keeps the obligation live on transient read/write failure; clears it only after verification
            // or a proven replacement world.
            g_restoreGraceCounter = 0;
        }
        return;
    }

    if (MacroController::GetState() == MacroController::State::Idle &&
        g_p2ControlOverridden && !g_pendingControlRestore.load() &&
        !(g_bufferFreezingActive.load() && g_activeFreezePlayer.load() == 2)) {
        LogOut("[AUTO-ACTION][MACRO] Macro inactive but override present; forcing control restore", true);
        RestoreP2ControlState();
        g_restoreGraceCounter = RESTORE_GRACE_PERIOD;
    }
    
    if (g_restoreGraceCounter > 0) {
        g_restoreGraceCounter--;
        if (g_restoreGraceCounter == 0) {
            LogOut("[AUTO-ACTION] Grace period expired, clearing g_pendingControlRestore", true);
            g_pendingControlRestore.store(false);
        }
    }
    
    if (g_pendingControlRestore.load()) {
        if (g_p2ControlOverridden.load(std::memory_order_acquire) &&
            g_legacyP2ControlSnapshot.valid &&
            g_tutorialP2Token.load(std::memory_order_acquire) == 0 &&
            MacroController::GetState() == MacroController::State::Idle) {
            RestoreP2ControlState();
            return;
        }

        if (!g_p2ControlOverridden && !(g_bufferFreezingActive.load() && g_activeFreezePlayer.load() == 2)) {
            if (detailedLogging.load()) {
                LogOut("[AUTO-ACTION] Clearing stale g_pendingControlRestore (no override/buffer-freeze)", true);
            }
            g_pendingControlRestore.store(false);
            return;
        }

        static int s_throttle = 0;
        static short s_prevMoveID2 = 0;
        bool deferThrottleDecision = true;

        uintptr_t base = GetEFZBase();
        if (!base) return;
        const PerFrameSample &dashSample = GetCurrentPerFrameSample();
        short moveID2 = dashSample.moveID2;
        short oppMoveID = dashSample.moveID1;

        EnsureCachedCharacterIDs(base);
        bool isKaori = (g_cachedP2CharID == CHAR_ID_KAORI);

        if (!g_crgFastRestore.load()) {
            bool waitingDash = g_recentDashQueued.load();
            if (!waitingDash) {
                if (!isKaori) {
                    if ((s_throttle++ & 1) != 0) return;
                } else if ((s_throttle % 192) == 0 && detailedLogging.load()) {
                    LogOut("[AUTO-ACTION][DASH][KAORI] Full-rate sampling active (disabling throttle to catch ID 250)", true);
                }
            }
            if (ValidationMetricsEnabled() && g_recentDashQueued.load()) { GetValidationMetrics().dashQueued++; }
        }

    const bool dashStartNow =
        moveID2 == GROUND_FORWARD_DASH_ID || moveID2 == GROUND_BACKWARD_DASH_ID ||
        moveID2 == AIR_FORWARD_DASH_ID || moveID2 == AIR_BACKWARD_DASH_ID ||
        (isKaori && moveID2 == KAORI_FORWARD_DASH_START_ID);
    bool dashStartJustEntered = dashStartNow && (s_prevMoveID2 != moveID2);
    if (ValidationMetricsEnabled() && dashStartJustEntered) { GetValidationMetrics().dashStartDetected++; }
    // Kaori's 250 is a dash state despite being >= 200; a dash normal excludes every dash state.
    bool dashNormalStarted = (moveID2 >= 200);
    bool moveIsDash = dashStartNow ||
               moveID2 == FORWARD_DASH_RECOVERY_SENTINEL_ID;
    if (dashNormalStarted && moveIsDash) dashNormalStarted = false;
    bool dashCancelled = (!moveIsDash) && (IsBlockstun(moveID2) || IsHitstun(moveID2) || IsAirtech(moveID2));
    bool waitingDashStart = g_recentDashQueued.load() && !dashStartNow;
    if (waitingDashStart && detailedLogging.load()) {
        LogOut("[AUTO-ACTION][DASH] Holding restore (pre-start): queuedDash & dashStart not yet observed", true);
    }
    
        if (g_recentDashQueued.load() && !dashStartNow && dashCancelled) {
            LogOut("[AUTO-ACTION][DASH] Pre-start interrupted (hit/block/airtech) - cancelling dash and restoring control", true);
            if (ValidationMetricsEnabled()) { GetValidationMetrics().dashPreStartInterrupts++; }
            if (g_dashDeferred.pendingSel.load() > 0) {
                g_dashDeferred.pendingSel.store(0);
                g_dashDeferred.dashStartLatched.store(-1);
            }
            g_recentDashQueued.store(false);
            RestoreP2ControlState();
            if (ValidationMetricsEnabled()) { GetValidationMetrics().dashRestoreEvents++; }
            p2TriggerActive = false;
            p2TriggerCooldown = 0;
            g_restoreGraceCounter = RESTORE_GRACE_PERIOD;
            g_crgFastRestore.store(false);
            g_lastP2MoveID.store(-1);
            g_pendingControlRestore.store(false);
            s_prevMoveID2 = moveID2;
            return;
        }
        if (g_dashDeferred.pendingSel.load() > 0 && dashCancelled && !g_recentDashQueued.load()) {
            LogOut("[AUTO-ACTION][DASH] Dash follow-up cancelled by interrupt before dash started", true);
            if (ValidationMetricsEnabled()) { GetValidationMetrics().dashFollowupCancelled++; }
            g_dashDeferred.pendingSel.store(0);
            g_dashDeferred.dashStartLatched.store(-1);
        }
    int fdfSel = forwardDashFollowup.load();
        // Do not restore when the dash starts: wait until the whole dash state is left, whatever follows.
        // Start and recovery IDs both count as dashing.
    bool wasDashing = (s_prevMoveID2 == GROUND_FORWARD_DASH_ID ||
                       s_prevMoveID2 == GROUND_BACKWARD_DASH_ID ||
                       s_prevMoveID2 == AIR_FORWARD_DASH_ID ||
                       s_prevMoveID2 == AIR_BACKWARD_DASH_ID ||
                       s_prevMoveID2 == FORWARD_DASH_RECOVERY_SENTINEL_ID ||
                       (isKaori && s_prevMoveID2 == KAORI_FORWARD_DASH_START_ID));
    bool stillDashing = moveIsDash;
        static bool s_dashStartSeenThisAttempt = false;
        if (dashStartJustEntered) {
            s_dashStartSeenThisAttempt = true;
        }
        bool leftDashState = s_dashStartSeenThisAttempt && wasDashing && !stillDashing;
        
        bool doDashRestore = false;
        
        if (leftDashState && !waitingDashStart) {
            LogOut("[AUTO-ACTION][DASH] Restore: left dash state (163/165/250 → " + std::to_string(moveID2) + ")", true);
            doDashRestore = true;
        }
        else if (dashNormalStarted && wasDashing && s_dashStartSeenThisAttempt) {
            LogOut("[AUTO-ACTION][DASH] Restore: dash normal detected (moveID=" + std::to_string(moveID2) + ")", true);
            doDashRestore = true;
        } else if (dashCancelled && wasDashing && s_dashStartSeenThisAttempt) {
            LogOut("[AUTO-ACTION][DASH] Restore: dash cancelled by state (moveID=" + std::to_string(moveID2) + ")", true);
            doDashRestore = true;
        } else if (moveID2 == FORWARD_DASH_RECOVERY_SENTINEL_ID && s_dashStartSeenThisAttempt) {
            LogOut("[AUTO-ACTION][DASH] Restore: sentinel recovery ID detected (178)", true);
            doDashRestore = true;
        }

        if (doDashRestore) {
            static int s_lastDashRestoreFrame = -1;
            int curF = frameCounter.load();
            if (s_lastDashRestoreFrame != curF) {
                RestoreP2ControlState();
                s_lastDashRestoreFrame = curF;
                g_recentDashQueued.store(false);
            }
            g_restoreGraceCounter = RESTORE_GRACE_PERIOD;
            g_pendingControlRestore.store(false);
            g_crgFastRestore.store(false);
            g_lastP2MoveID.store(-1);
            s_prevMoveID2 = moveID2;
            return;
        }

        static bool sawNonZeroMoveID = false;
        static int s_lastDashQueueFrame = -1;
        int dashQueueFrame = g_recentDashQueuedFrame.load();
        if (g_recentDashQueued.load() && dashQueueFrame != s_lastDashQueueFrame) {
            sawNonZeroMoveID = false;
            s_lastDashQueueFrame = dashQueueFrame;
            s_dashStartSeenThisAttempt = false;
            if (detailedLogging.load()) {
                LogOut("[AUTO-ACTION][DASH] New dash attempt detected; resetting sawNonZeroMoveID", true);
            }
        }
    if (g_recentDashQueued.load() && moveID2 == 0 && !sawNonZeroMoveID) {
            int age = frameCounter.load() - g_recentDashQueuedFrame.load();
            const MotionQueueSnapshot queue = GetMotionQueueSnapshot(2);
            int qSize = static_cast<int>(queue.size);
            if (detailedLogging.load()) {
                static int s_nextDashTraceLogFrame = 0;
                int nowF = frameCounter.load();
                if (nowF >= s_nextDashTraceLogFrame) {
                    s_nextDashTraceLogFrame = nowF + 24;
                    LogOut(std::string("[AUTO-ACTION][DASH][TRACE] waiting age=") + std::to_string(age) +
                           " queueActive=" + (queue.active?"1":"0") +
                           " qIdx=" + std::to_string(queue.index) + "/" + std::to_string(qSize) +
                           " frameInStep=" + std::to_string(queue.frameCounter) +
                           " motionType=" + std::to_string(queue.motionType), true);
                    if (queue.hasCurrentMask) {
                        uint8_t mask = queue.currentMask;
                        LogOut(std::string("[AUTO-ACTION][DASH][TRACE] current mask=") + std::to_string((int)mask), true);
                    }
                }
            }
            if (age < 120) {
                s_prevMoveID2 = moveID2; return;
            } else if (age == 120) {
                bool wasBack = (queue.motionType == MOTION_BACK_DASH);
                LogOut(std::string("[AUTO-ACTION][DASH] Requeue ") + (wasBack?"backdash":"forward dash") + " after 120f no-start", true);
                QueueMotionInput(2, wasBack ? MOTION_BACK_DASH : MOTION_FORWARD_DASH, 0);
                g_recentDashQueuedFrame.store(frameCounter.load());
                s_prevMoveID2 = moveID2; return;
            } else if (age < 160) {
                s_prevMoveID2 = moveID2; return;
            } else {
                LogOut("[AUTO-ACTION][DASH] Abandon wait; dash never produced non-zero moveID", true);
                g_recentDashQueued.store(false);
            }
        }

        if (moveID2 > 0) {
            sawNonZeroMoveID = true;
            if (dashStartNow) {
                g_recentDashQueued.store(false);
            }
        }

        if (g_crgFastRestore.load() && g_counterRGEnabled.load()) {
            bool opponentAttacking = (oppMoveID >= 200);
            bool moveStarted = (moveID2 > 0 && moveID2 != RG_STAND_ID && moveID2 != RG_CROUCH_ID && moveID2 != RG_AIR_ID);
            bool p2ActionableNow = IsActionable(moveID2);
            bool isRGPrearmedSpecial = s_p2RGPrearmIsSpecial;
            if (opponentAttacking && p2ActionableNow) {
                if (!isRGPrearmedSpecial) {
                    LogOut("[AUTO-ACTION][CRG] Early restore: opponent entered attack frames", true);
                    RestoreP2ControlFlagOnly();
                    g_restoreGraceCounter = RESTORE_GRACE_PERIOD;
                    g_pendingControlRestore.store(false);
                    g_crgFastRestore.store(false);
                    g_lastP2MoveID.store(-1);
                    sawNonZeroMoveID = false;
                    return;
                } else {
                    if (detailedLogging.load()) {
                        LogOut("[AUTO-ACTION][CRG] Opponent attacking, but special pre-armed: deferring restore until move starts", true);
                    }
                }
            }
            if (moveStarted) {
                LogOut("[AUTO-ACTION][CRG] Early restore: special started after RG", true);
                RestoreP2ControlState();
                g_restoreGraceCounter = RESTORE_GRACE_PERIOD;
                g_pendingControlRestore.store(false);
                g_crgFastRestore.store(false);
                g_lastP2MoveID.store(-1);
                sawNonZeroMoveID = false;
                p2TriggerCooldown = 0; p2TriggerActive = false;
                return;
            }
        }

    bool queuedDashNoStart = waitingDashStart;
    bool dashQueueActiveNow = GetMotionQueueSnapshot(2).active;
    bool moveChangedCandidate = (moveID2 != g_lastP2MoveID.load() && moveID2 == 0 && sawNonZeroMoveID);
    bool dashFollowPending = (g_dashDeferred.pendingSel.load() > 0);
    bool suppressGenericRestore = (moveChangedCandidate && dashFollowPending);
        if (suppressGenericRestore) {
            LogOut("[AUTO-ACTION][DASH] Suppressing generic restore (pending follow-up active)", detailedLogging.load());
        }
        bool moveChanged = moveChangedCandidate && !suppressGenericRestore;
        
        if (g_pendingControlRestore.load() && !g_p2ControlOverridden && g_restoreGraceCounter == 0 &&
            !dashFollowPending && !g_recentDashQueued.load()) {
            LogOut("[AUTO-ACTION] Clearing stale g_pendingControlRestore (no override active)", detailedLogging.load());
            g_pendingControlRestore.store(false);
            s_prevMoveID2 = moveID2;
            return;
        }

        if (waitingDashStart && !dashQueueActiveNow && moveID2 != 0 && !moveIsDash) {
            int ageSinceQueue = frameCounter.load() - g_recentDashQueuedFrame.load();
            if (ageSinceQueue >= 12) { // ~1/16 s at 192 Hz; enough to rule out 1F start IDs
                LogOut("[AUTO-ACTION][DASH] Pre-start cancelled (no start observed, non-dash state persisted) - restoring", true);
                if (g_dashDeferred.pendingSel.load() > 0) {
                    g_dashDeferred.pendingSel.store(0);
                    g_dashDeferred.dashStartLatched.store(-1);
                }
                g_recentDashQueued.store(false);
                RestoreP2ControlState();
                g_restoreGraceCounter = RESTORE_GRACE_PERIOD;
                g_crgFastRestore.store(false);
                g_lastP2MoveID.store(-1);
                g_pendingControlRestore.store(false);
                s_prevMoveID2 = moveID2;
                return;
            } else {
                s_prevMoveID2 = moveID2;
                return;
            }
        }

        bool restorePendingNow = g_pendingControlRestore.load();
        bool allowEarlyRestore = (g_p2ControlOverridden || (g_bufferFreezingActive.load() && g_activeFreezePlayer.load() == 2));
        bool haveTrackableMove = (g_lastP2MoveID.load() != -1);
        bool nonRGState = (moveID2 != RG_STAND_ID && moveID2 != RG_CROUCH_ID && moveID2 != RG_AIR_ID);
        bool specialActiveNow = (moveID2 >= 250 && moveID2 < 300);
        bool superActiveNow   = (moveID2 >= 300);
        bool activeNow = (specialActiveNow || superActiveNow) && nonRGState && !moveIsDash;
        bool stateEnteredNow = (s_prevMoveID2 != moveID2);
        if (restorePendingNow && allowEarlyRestore && stateEnteredNow && activeNow &&
            !dashFollowPending && !queuedDashNoStart && !g_recentDashQueued.load() && !dashQueueActiveNow) {
            LogOut("[AUTO-ACTION] Early restore on active frame (special>=250|super>=300) moveID=" + std::to_string(moveID2), true);
            RestoreP2ControlState();
            g_restoreGraceCounter = RESTORE_GRACE_PERIOD;
            g_crgFastRestore.store(false);
            g_lastP2MoveID.store(-1);
            sawNonZeroMoveID = false;
            s_prevMoveID2 = moveID2;
            g_pendingControlRestore.store(false);
            g_pendingRestoreTimestamp.store(0);
            p2TriggerCooldown = 0; p2TriggerActive = false;
            return;
        }

        bool freezeActiveForP2 = (g_bufferFreezingActive.load() && g_activeFreezePlayer.load() == 2);
        DWORD restoreTimestamp = g_pendingRestoreTimestamp.load();
        DWORD currentTime = GetTickCount();
        bool gracePeriodElapsed = (restoreTimestamp == 0) || ((currentTime - restoreTimestamp) >= 20);
        if (g_pendingControlRestore.load() && g_p2ControlOverridden && !freezeActiveForP2 && !dashQueueActiveNow && gracePeriodElapsed) {
            LogOut("[AUTO-ACTION] Fallback restore: freeze ended, pending restore active", true);
            RestoreP2ControlState();
            g_restoreGraceCounter = RESTORE_GRACE_PERIOD;
            g_crgFastRestore.store(false);
            g_lastP2MoveID.store(-1);
            g_pendingControlRestore.store(false);
            g_pendingRestoreTimestamp.store(0);
            sawNonZeroMoveID = false;
            p2TriggerCooldown = 0; p2TriggerActive = false;
            s_prevMoveID2 = moveID2;
            return;
        }

        restorePendingNow = g_pendingControlRestore.load();
        haveTrackableMove = (g_lastP2MoveID.load() != -1);
    static int s_lastGenericRestoreFrame = -1;
    bool allowAnyRestore = (g_p2ControlOverridden || (g_bufferFreezingActive.load() && g_activeFreezePlayer.load() == 2));
    bool baseRestoreCond = moveChanged && (moveID2 != 0) &&
                !dashFollowPending &&
                !queuedDashNoStart &&
                !g_recentDashQueued.load() &&
                !dashQueueActiveNow &&
                restorePendingNow &&
                allowAnyRestore;
    bool shouldGenericRestore = baseRestoreCond && (frameCounter.load() != s_lastGenericRestoreFrame);

        if (shouldGenericRestore) {
            LogOut("[AUTO-ACTION] Auto-restoring P2 control state after move execution", detailedLogging.load());
         LogOut("[AUTO-ACTION] Reason: Move completed, MoveID: " + std::to_string(moveID2), detailedLogging.load());

            RestoreP2ControlState();
            g_restoreGraceCounter = RESTORE_GRACE_PERIOD;
            g_pendingControlRestore.store(false);
            g_pendingRestoreTimestamp.store(0);
            g_crgFastRestore.store(false);
            g_lastP2MoveID.store(-1);
            sawNonZeroMoveID = false;
            s_lastGenericRestoreFrame = frameCounter.load();

            p2TriggerCooldown = 0;
            p2TriggerActive = false;
            LogOut("[AUTO-ACTION] Cleared P2 trigger cooldown after restore", detailedLogging.load());
        } else {
            if (moveChanged && restorePendingNow && !shouldGenericRestore && detailedLogging.load()) {
                LogOut(std::string("[AUTO-ACTION][TRACE] Suppressed generic restore: pending=") + (restorePendingNow?"1":"0") +
                       " override=" + (g_p2ControlOverridden?"1":"0") +
                       " lastMoveValid=" + (haveTrackableMove?"1":"0") +
                       " dashFollow=" + (dashFollowPending?"1":"0") +
                       " queuedDashNoStart=" + (queuedDashNoStart?"1":"0") +
                       " recentDashQueued=" + (g_recentDashQueued.load()?"1":"0") +
                       " dashQueueActive=" + (dashQueueActiveNow?"1":"0"), true);
            }
            if (moveID2 != 0) {
                g_lastP2MoveID.store(moveID2);
            }
            if (dashFollowPending && moveChangedCandidate) {
                if (detailedLogging.load()) {
                    LogOut("[AUTO-ACTION][DASH] Suppressed restore while follow-up pending (moveID2=0)", true);
                }
            }
            if (queuedDashNoStart && detailedLogging.load()) {
                LogOut("[AUTO-ACTION][DASH] Holding restore: dash queue completed but no dash start MoveID observed yet", true);
            }
        }
        s_prevMoveID2 = moveID2;
    }
}

bool AutoGuard(int playerNum, int opponentPtr) {
    uintptr_t playerPtr = GetPlayerPointer(playerNum);
    if (!playerPtr || !opponentPtr)
        return false;
        
    short moveID = 0;
    if (!SafeReadMemory(opponentPtr + 0x8, &moveID, sizeof(short)) || moveID <= 0)
        return false;
        
    LogOut("[AUTO_GUARD] Attempting to block move ID: " + std::to_string(moveID), true);
    
    AttackHeight height = ATTACK_HEIGHT_MID;
    
    double yPos = 0.0;
    SafeReadMemory(playerPtr + 40, &yPos, sizeof(double));
    bool inAir = (yPos < 0.0);
    
    bool playerFacingRight = GetPlayerFacingDirection(playerNum);
    uint8_t blockInput = 0;
    
    LogOut("[AUTO_GUARD] Attack height: " + std::to_string(height) + 
           ", Player in air: " + (inAir ? "yes" : "no"), true);
    
    switch (height) {
        case ATTACK_HEIGHT_LOW:
            blockInput = GAME_INPUT_DOWN | (playerFacingRight ? GAME_INPUT_LEFT : GAME_INPUT_RIGHT);
            LogOut("[AUTO_GUARD] Using crouch block for low attack", true);
            break;
            
        case ATTACK_HEIGHT_HIGH:
            if (inAir) {
                blockInput = playerFacingRight ? GAME_INPUT_LEFT : GAME_INPUT_RIGHT;
                LogOut("[AUTO_GUARD] Using air block for high attack", true);
            } else {
                blockInput = playerFacingRight ? GAME_INPUT_LEFT : GAME_INPUT_RIGHT;
                LogOut("[AUTO_GUARD] Using stand block for high attack", true);
            }
            break;
            
        case ATTACK_HEIGHT_MID:
            if (inAir) {
                blockInput = playerFacingRight ? GAME_INPUT_LEFT : GAME_INPUT_RIGHT;
                LogOut("[AUTO_GUARD] Using air block for mid attack", true);
            } else {
                blockInput = GAME_INPUT_DOWN | (playerFacingRight ? GAME_INPUT_LEFT : GAME_INPUT_RIGHT);
                LogOut("[AUTO_GUARD] Using crouch block for mid attack", true);
            }
            break;
            
        case ATTACK_HEIGHT_THROW:
            LogOut("[AUTO_GUARD] Cannot block unblockable attack", true);
            return false;
            
        default:
            LogOut("[AUTO_GUARD] Unknown attack height", true);
            return false;
    }
    
    return WritePlayerInput(playerPtr, blockInput);
}

static void ConsumeTutorialP2WakeCancel() {
    const bool hadWakeState =
        s_p2WakePrearmed || s_p2WakeMacroQueued ||
        s_wakeMacroTokenNeutralizeCountdown[2] > 0 ||
        (p2DelayState.isDelaying &&
         p2DelayState.triggerType == TRIGGER_ON_WAKEUP) ||
        (g_bufferFreezingActive.load(std::memory_order_acquire) &&
         g_activeFreezePlayer.load(std::memory_order_acquire) == 2) ||
        IsP2AutoActionMotionTransactionActive() ||
        g_p2ControlOverridden;

    if (g_bufferFreezingActive.load(std::memory_order_acquire) &&
        g_activeFreezePlayer.load(std::memory_order_acquire) == 2) {
        StopBufferFreezing();
    }
    KaoriRecoilDuck::Cancel(2, "tutorial wake producer cancelled");
    CancelAutoActionChargeFollowup(2, "tutorial wake producer cancelled");
    CancelP2AutoActionMotionTransaction("tutorial wake producer cancelled");
    if (g_p2ControlOverridden) RestoreP2ControlState();

    p2DelayState = {false, 0, TRIGGER_NONE, 0, -1, -1, 0, -1, -1, 0, 0, 0};
    p2ActionApplied = false;
    p2TriggerActive = false;
    p2TriggerCooldown = 0;
    g_pendingControlRestore.store(false, std::memory_order_release);
    g_pendingRestoreTimestamp.store(0, std::memory_order_release);
    g_lastP2MoveID.store(-1, std::memory_order_release);
    g_crgFastRestore.store(false, std::memory_order_release);

    s_p2WakePrearmed = false;
    s_p2WakePrearmExpiry = 0;
    s_p2WakePrearmActionType = -1;
    s_p2WakePrearmStrength = -1;
    s_p2WakePrearmCharge = 0;
    s_p2WakePrearmIsSpecial = false;
    s_p2WakePrearmIsMacro = false;
    s_p2WakeActionableWarning = false;
    s_p2WakeMoveID96FrameCount = 0;
    s_p2WakeBufferFrozen = false;
    s_p2WakeChargeReservation = 0;
    s_p2WakeMotionGeneration = 0;
    s_p2WakeHoldPrimed = false;
    s_p2WakeHoldIssued = false;
    s_p2WakeMacro96FrameCount = 0;
    s_p2WakeMacroQueued = false;
    s_p2WakeMacroTargetFrame = -1;
    s_p2WakeMacroSlot = -1;
    s_p2WakeMacroStartFrame = -1;
    s_p2WakeMacroStartTick = 0;
    ReleaseWakeForcedNeutral(2);
    EndWakeMacroCleanup(2);
    s_wakeMacroTokenNeutralizeCountdown[2] = 0;
    s_p2WakeOptionPicked = false;
    s_p2WakePrePickedOption = {};

    extern std::atomic<bool> g_forceBypass[3];
    extern std::atomic<bool> g_pollOverrideActive[3];
    extern std::atomic<uint8_t> g_pollOverrideMask[3];
    g_forceBypass[2].store(false, std::memory_order_release);
    g_pollOverrideMask[2].store(0, std::memory_order_relaxed);
    g_pollOverrideActive[2].store(false, std::memory_order_release);
    g_manualInputOverride[2].store(false, std::memory_order_release);
    g_manualInputMask[2].store(0, std::memory_order_release);
    g_injectImmediateOnly[2].store(false, std::memory_order_release);
    ImmediateInput::Clear(2);
    (void)ClearMotionInputQueue(2);
    (void)FullCleanupAfterToggle(2);

    if (hadWakeState) {
        LogOut("[TUTORIAL][WAKE_PRODUCER] Cancelled P2 native wake runtime state", true);
    }
}

void CancelTutorialP2WakeProducer() {
    std::lock_guard<std::recursive_mutex> stateLock(g_autoActionStateMutex);
    ConsumeTutorialP2WakeCancel();
}

bool AcquireTutorialP2WakeProducer(int action, int strength,
                                   uint64_t& tokenOut) {
    tokenOut = 0;
    if (g_onlineModeActive.load()) return false;
    std::lock_guard<std::recursive_mutex> stateLock(g_autoActionStateMutex);
    if (g_tutorialP2WakeToken.load(std::memory_order_acquire) != 0) return false;

    ConsumeTutorialP2WakeCancel();
    TutorialP2WakeSnapshot snapshot;
    snapshot.enabled = autoActionEnabled.load();
    snapshot.player = autoActionPlayer.load();
    snapshot.wake = triggerOnWakeupEnabled.load();
    snapshot.action = triggerOnWakeupAction.load();
    snapshot.strength = triggerOnWakeupStrength.load();
    snapshot.delay = triggerOnWakeupDelay.load();
    snapshot.macroSlot = triggerOnWakeupMacroSlot.load();
    snapshot.charge = triggerOnWakeupCharge.load();
    snapshot.usePool = triggerOnWakeupUsePool.load();
    snapshot.afterBlock = triggerAfterBlockEnabled.load();
    snapshot.afterHitstun = triggerAfterHitstunEnabled.load();
    snapshot.afterAirtech = triggerAfterAirtechEnabled.load();
    snapshot.onRG = triggerOnRGEnabled.load();
    snapshot.randomize = triggerRandomizeEnabled.load();
    snapshot.wakeBuffering = g_wakeBufferingEnabled.load();

    triggerAfterBlockEnabled.store(false);
    triggerAfterHitstunEnabled.store(false);
    triggerAfterAirtechEnabled.store(false);
    triggerOnRGEnabled.store(false);
    triggerRandomizeEnabled.store(false);
    g_wakeBufferingEnabled.store(true);
    triggerOnWakeupAction.store(action);
    triggerOnWakeupStrength.store(strength);
    triggerOnWakeupDelay.store(0);
    triggerOnWakeupMacroSlot.store(0);
    triggerOnWakeupCharge.store(0);
    triggerOnWakeupUsePool.store(false);
    triggerOnWakeupEnabled.store(true);
    autoActionPlayer.store(2);
    autoActionEnabled.store(true);

    uint64_t token =
        g_nextTutorialP2WakeToken.fetch_add(1, std::memory_order_relaxed);
    if (token == 0) {
        token = g_nextTutorialP2WakeToken.fetch_add(1,
                                                    std::memory_order_relaxed);
    }
    g_tutorialP2WakeSnapshot = snapshot;
    g_tutorialP2WakeAppliedAction = action;
    g_tutorialP2WakeAppliedStrength = strength;
    g_tutorialP2WakeToken.store(token, std::memory_order_release);
    tokenOut = token;
    LogOut("[TUTORIAL][WAKE_PRODUCER] Acquired token=" +
               std::to_string(token) + " action=" + std::to_string(action) +
               " strength=" + std::to_string(strength),
           true);
    return true;
}

void ReleaseTutorialP2WakeProducer(uint64_t token) {
    if (token == 0) return;
    std::lock_guard<std::recursive_mutex> stateLock(g_autoActionStateMutex);
    if (g_tutorialP2WakeToken.load(std::memory_order_acquire) != token) return;

    ConsumeTutorialP2WakeCancel();
    const TutorialP2WakeSnapshot snapshot = g_tutorialP2WakeSnapshot;
    if (triggerOnWakeupEnabled.load())
        triggerOnWakeupEnabled.store(snapshot.wake);
    if (triggerOnWakeupAction.load() == g_tutorialP2WakeAppliedAction)
        triggerOnWakeupAction.store(snapshot.action);
    if (triggerOnWakeupStrength.load() == g_tutorialP2WakeAppliedStrength)
        triggerOnWakeupStrength.store(snapshot.strength);
    if (triggerOnWakeupDelay.load() == 0)
        triggerOnWakeupDelay.store(snapshot.delay);
    if (triggerOnWakeupMacroSlot.load() == 0)
        triggerOnWakeupMacroSlot.store(snapshot.macroSlot);
    if (triggerOnWakeupCharge.load() == 0)
        triggerOnWakeupCharge.store(snapshot.charge);
    if (!triggerOnWakeupUsePool.load())
        triggerOnWakeupUsePool.store(snapshot.usePool);
    if (!triggerAfterBlockEnabled.load())
        triggerAfterBlockEnabled.store(snapshot.afterBlock);
    if (!triggerAfterHitstunEnabled.load())
        triggerAfterHitstunEnabled.store(snapshot.afterHitstun);
    if (!triggerAfterAirtechEnabled.load())
        triggerAfterAirtechEnabled.store(snapshot.afterAirtech);
    if (!triggerOnRGEnabled.load())
        triggerOnRGEnabled.store(snapshot.onRG);
    if (!triggerRandomizeEnabled.load())
        triggerRandomizeEnabled.store(snapshot.randomize);
    if (g_wakeBufferingEnabled.load())
        g_wakeBufferingEnabled.store(snapshot.wakeBuffering);
    if (autoActionPlayer.load() == 2)
        autoActionPlayer.store(snapshot.player);
    if (autoActionEnabled.load())
        autoActionEnabled.store(snapshot.enabled);

    g_tutorialP2WakeSnapshot = TutorialP2WakeSnapshot{};
    g_tutorialP2WakeAppliedAction = 0;
    g_tutorialP2WakeAppliedStrength = 0;
    g_tutorialP2WakeToken.store(0, std::memory_order_release);
    LogOut("[TUTORIAL][WAKE_PRODUCER] Released token=" +
               std::to_string(token),
           true);
}

void CancelAutoActionsAndMacros() {
    std::lock_guard<std::recursive_mutex> stateLock(g_autoActionStateMutex);
    KaoriRecoilDuck::CancelAll("auto-actions and macros cancelled");
    CancelAllAutoActionChargeFollowups("auto-actions and macros cancelled");
    std::ostringstream logStream;
    logStream << "[CANCEL] === BEGIN CancelAutoActionsAndMacros ===";
    LogOut(logStream.str(), true);

    MacroController::State macroSt = MacroController::GetState();
    int macroSlot = MacroController::GetCurrentSlot();
    
    logStream.str(""); logStream.clear();
    logStream << "[CANCEL][MACRO] Current state: "
              << (macroSt == MacroController::State::Idle ? "Idle" :
                  macroSt == MacroController::State::PreRecord ? "PreRecord" :
                  macroSt == MacroController::State::Recording ? "Recording" : "Replaying")
              << " slot=" << macroSlot;
    LogOut(logStream.str(), true);
    
    if (macroSt != MacroController::State::Idle) {
        if (macroSt == MacroController::State::PreRecord || macroSt == MacroController::State::Recording) {
            MacroController::UnswapThenStop();
            logStream.str(""); logStream.clear();
            logStream << "[CANCEL][MACRO] Stopped PreRecord/Recording with UnswapThenStop";
            LogOut(logStream.str(), true);
            DirectDrawHook::AddMessage("Macro: Cancelled", "MACRO", RGB(180, 40, 40), 1200, 0, 120);
        } else {
            MacroController::Stop();
            logStream.str(""); logStream.clear();
            logStream << "[CANCEL][MACRO] Stopped active macro playback";
            LogOut(logStream.str(), true);
            DirectDrawHook::AddMessage("Macro: Cancelled", "MACRO", RGB(180, 40, 40), 1200, 0, 120);
        }
    }

    logStream.str(""); logStream.clear();
    logStream << "[CANCEL][DELAY] P1: isDelaying=" << (p1DelayState.isDelaying ? "true" : "false")
              << " framesRemaining=" << p1DelayState.delayFramesRemaining
              << " triggerType=" << p1DelayState.triggerType;
    LogOut(logStream.str(), true);
    
    logStream.str(""); logStream.clear();
    logStream << "[CANCEL][DELAY] P2: isDelaying=" << (p2DelayState.isDelaying ? "true" : "false")
              << " framesRemaining=" << p2DelayState.delayFramesRemaining
              << " triggerType=" << p2DelayState.triggerType;
    LogOut(logStream.str(), true);
    
    p1DelayState = {false, 0, TRIGGER_NONE, 0, -1, -1, 0, -1, -1, 0, 0, 0};
    p2DelayState = {false, 0, TRIGGER_NONE, 0, -1, -1, 0, -1, -1, 0, 0, 0};
    LogOut("[CANCEL][DELAY] Reset both delay states", true);

    logStream.str(""); logStream.clear();
    logStream << "[CANCEL][FLAGS] p1ActionApplied=" << (p1ActionApplied ? "true" : "false")
              << " p2ActionApplied=" << (p2ActionApplied ? "true" : "false");
    LogOut(logStream.str(), true);
    
    p1ActionApplied = false;
    p2ActionApplied = false;

    int lastTrigger = g_lastActiveTriggerType.load();
    int lastFrame = g_lastActiveTriggerFrame.load();
    logStream.str(""); logStream.clear();
    logStream << "[CANCEL][TRIGGER] lastActiveTriggerType=" << lastTrigger
              << " lastActiveTriggerFrame=" << lastFrame;
    LogOut(logStream.str(), true);
    
    g_lastActiveTriggerType.store(TRIGGER_NONE);
    g_lastActiveTriggerFrame.store(0);
    
    g_triggersCancelledActive.store(true);
    g_triggersCancelledFrame.store(frameCounter.load());
    LogOut("[CANCEL][TRIGGER] Set triggers cancelled flag for red flash feedback", true);

    logStream.str(""); logStream.clear();
    logStream << "[CANCEL][COOLDOWN] p1TriggerActive=" << (p1TriggerActive ? "true" : "false")
              << " p1Cooldown=" << p1TriggerCooldown
              << " p2TriggerActive=" << (p2TriggerActive ? "true" : "false")
              << " p2Cooldown=" << p2TriggerCooldown;
    LogOut(logStream.str(), true);
    
    p1TriggerActive = false; p1TriggerCooldown = 0;
    p2TriggerActive = false; p2TriggerCooldown = 0;
    ResetDeferredDashFollowupState();

    CancelAllAutoActionMotionTransactions("cancel auto-actions and macros");

    // Cancel the normal transaction before restoring controller flags: the restore helper defers
    // while a normal is active, so the opposite order can strand P2 in human control.
    CancelAllAutoActionNormalPulses();

    bool wasOverridden = g_p2ControlOverridden;
    uint32_t origFlag = g_originalP2ControlFlag;
    bool pendingRestore = g_pendingControlRestore.load();
    
    logStream.str(""); logStream.clear();
    logStream << "[CANCEL][CONTROL] g_p2ControlOverridden=" << (wasOverridden ? "true" : "false")
              << " g_originalP2ControlFlag=" << origFlag
              << " g_pendingControlRestore=" << (pendingRestore ? "true" : "false");
    LogOut(logStream.str(), true);
    
    if (g_p2ControlOverridden) {
        RestoreP2ControlState();
        LogOut(std::string("[CANCEL][CONTROL] P2 control restore ") +
                   (g_p2ControlOverridden.load(std::memory_order_acquire)
                        ? "retained for retry" : "completed"),
               true);
    }
    if (!g_p2ControlOverridden.load(std::memory_order_acquire)) {
        g_pendingControlRestore.store(false, std::memory_order_release);
        g_pendingRestoreTimestamp.store(0, std::memory_order_release);
    }
    g_lastP2MoveID.store(-1);
    g_crgFastRestore.store(false);

    extern std::atomic<bool> g_bufferFreezingActive;
    extern std::atomic<int> g_activeFreezePlayer;
    bool freezeActive = g_bufferFreezingActive.load();
    int freezePlayer = g_activeFreezePlayer.load();
    
    logStream.str(""); logStream.clear();
    logStream << "[CANCEL][FREEZE] g_bufferFreezingActive=" << (freezeActive ? "true" : "false")
              << " g_activeFreezePlayer=" << freezePlayer;
    LogOut(logStream.str(), true);
    
    StopBufferFreezing();

    logStream.str(""); logStream.clear();
    logStream << "[CANCEL][RG] P1: pending=" << (s_p1RGPending ? "true" : "false")
              << " prearm=" << (s_p1RGPrearmed ? "true" : "false")
              << " P2: pending=" << (s_p2RGPending ? "true" : "false")
              << " prearm=" << (s_p2RGPrearmed ? "true" : "false");
    LogOut(logStream.str(), true);
    
    s_p1RGPending = false; s_p1RGExpiry = 0;
    s_p2RGPending = false; s_p2RGExpiry = 0;
    s_p1RGPrearmed = false; s_p1RGPrearmIsSpecial = false; s_p1RGPrearmActionType = -1; s_p1RGPrearmExpiry = 0;
    s_p2RGPrearmed = false; s_p2RGPrearmIsSpecial = false; s_p2RGPrearmActionType = -1; s_p2RGPrearmExpiry = 0;
    s_p1RGPrearmGeneration = 0;
    s_p2RGPrearmGeneration = 0;

    logStream.str(""); logStream.clear();
    logStream << "[CANCEL][WAKE] P1: prearm=" << (s_p1WakePrearmed ? "true" : "false")
              << " isMacro=" << (s_p1WakePrearmIsMacro ? "true" : "false")
              << " 96count=" << s_p1WakeMoveID96FrameCount
              << " frozen=" << (s_p1WakeBufferFrozen ? "true" : "false");
    LogOut(logStream.str(), true);
    
    logStream.str(""); logStream.clear();
    logStream << "[CANCEL][WAKE] P2: prearm=" << (s_p2WakePrearmed ? "true" : "false")
              << " isMacro=" << (s_p2WakePrearmIsMacro ? "true" : "false")
              << " 96count=" << s_p2WakeMoveID96FrameCount
              << " frozen=" << (s_p2WakeBufferFrozen ? "true" : "false");
    LogOut(logStream.str(), true);
    
    s_p1WakePrearmed = false; s_p1WakePrearmExpiry = 0; s_p1WakePrearmActionType = -1;
    s_p1WakePrearmStrength = -1; s_p1WakePrearmIsSpecial = false; s_p1WakePrearmIsMacro = false;
    s_p1WakePrearmCharge = 0;
    s_p1WakeActionableWarning = false; s_p1WakeMoveID96FrameCount = 0;
    s_p1WakeBufferFrozen = false; s_p1WakeHoldPrimed = false; s_p1WakeHoldIssued = false;
    s_p1WakeMotionGeneration = 0; s_p1WakeChargeReservation = 0;
    
    s_p2WakePrearmed = false; s_p2WakePrearmExpiry = 0; s_p2WakePrearmActionType = -1;
    s_p2WakePrearmStrength = -1; s_p2WakePrearmIsSpecial = false; s_p2WakePrearmIsMacro = false;
    s_p2WakePrearmCharge = 0;
    s_p2WakeActionableWarning = false; s_p2WakeMoveID96FrameCount = 0;
    s_p2WakeBufferFrozen = false; s_p2WakeHoldPrimed = false; s_p2WakeHoldIssued = false;
    s_p2WakeMotionGeneration = 0; s_p2WakeChargeReservation = 0;

    logStream.str(""); logStream.clear();
    logStream << "[CANCEL][WAKE_MACRO] P1: queued=" << (s_p1WakeMacroQueued ? "true" : "false")
              << " slot=" << s_p1WakeMacroSlot
              << " P2: queued=" << (s_p2WakeMacroQueued ? "true" : "false")
              << " slot=" << s_p2WakeMacroSlot;
    LogOut(logStream.str(), true);
    
    s_p1WakeMacro96FrameCount = 0; s_p1WakeMacroQueued = false;
    s_p1WakeMacroTargetFrame = -1; s_p1WakeMacroSlot = -1; s_p1WakeMacroStartTick = 0;
    s_p2WakeMacro96FrameCount = 0; s_p2WakeMacroQueued = false;
    s_p2WakeMacroTargetFrame = -1; s_p2WakeMacroSlot = -1; s_p2WakeMacroStartFrame = -1; s_p2WakeMacroStartTick = 0;
    s_p1WakeOptionPicked = false;
    s_p1WakePrePickedOption = {};
    s_p2WakeOptionPicked = false;
    s_p2WakePrePickedOption = {};

    bool wakePreserve = g_macroWakePreserveBuffer.load();
    const int wakeCompleted = g_wakeMacroPlaybackCompleted.load();

    logStream.str(""); logStream.clear();
    logStream << "[CANCEL][WAKE_COMPLETE] preserveBuffer=" << (wakePreserve ? "true" : "false")
              << " playbackCompleted=" << (wakeCompleted != 0 ? "true" : "false")
              << " player=" << wakeCompleted
              << " tokenCountdown(P1/P2)=" << s_wakeMacroTokenNeutralizeCountdown[1]
              << "/" << s_wakeMacroTokenNeutralizeCountdown[2];
    LogOut(logStream.str(), true);

    g_macroWakePreserveBuffer.store(false);
    g_macroWakePreservePlayer.store(0);
    g_wakeMacroPlaybackCompleted.store(0);
    s_wakeMacroTokenNeutralizeCountdown[1] = 0;
    s_wakeMacroTokenNeutralizeCountdown[2] = 0;
    ResetWakeRuntimeState();

    extern std::atomic<bool> g_forceBypass[3];
    extern std::atomic<bool> g_pollOverrideActive[3];
    extern std::atomic<uint8_t> g_pollOverrideMask[3];
    extern std::atomic<uint8_t> g_manualInputMask[3];
    extern std::atomic<bool> g_injectImmediateOnly[3];
    
    logStream.str(""); logStream.clear();
    logStream << "[CANCEL][INJECT] P1: pollOverride=" << (g_pollOverrideActive[1].load() ? "true" : "false")
              << " mask=0x" << std::hex << (int)g_pollOverrideMask[1].load() << std::dec
              << " manualOverride=" << (g_manualInputOverride[1].load() ? "true" : "false")
              << " forceBypass=" << (g_forceBypass[1].load() ? "true" : "false");
    LogOut(logStream.str(), true);
    
    logStream.str(""); logStream.clear();
    logStream << "[CANCEL][INJECT] P2: pollOverride=" << (g_pollOverrideActive[2].load() ? "true" : "false")
              << " mask=0x" << std::hex << (int)g_pollOverrideMask[2].load() << std::dec
              << " manualOverride=" << (g_manualInputOverride[2].load() ? "true" : "false")
              << " forceBypass=" << (g_forceBypass[2].load() ? "true" : "false");
    LogOut(logStream.str(), true);
    
    for (int p = 1; p <= 2; ++p) {
        g_forceBypass[p].store(false);
        g_pollOverrideActive[p].store(false);
        g_pollOverrideMask[p].store(0);
        g_manualInputOverride[p].store(false);
        g_manualInputMask[p].store(0);
        g_injectImmediateOnly[p].store(false);
    }

    (void)ClearMotionInputQueue(1);
    (void)ClearMotionInputQueue(2);
    
    ImmediateInput::Clear(1);
    ImmediateInput::Clear(2);
    LogOut("[CANCEL][INJECT] Cleared injection state, motion queues, and immediate registers", true);

    LogOut("[CANCEL] === END CancelAutoActionsAndMacros - Triggers remain enabled ===", true);
}

void ClearAllAutoActionTriggers() {
    std::lock_guard<std::recursive_mutex> stateLock(g_autoActionStateMutex);
    static int s_lastClearFrame = -1000000;
    int nowF = frameCounter.load();
    extern std::atomic<bool> g_suppressAutoActionClearLogging;
    bool logThis = !g_suppressAutoActionClearLogging.load() && (nowF - s_lastClearFrame) >= 480;
    if (logThis) {
        LogOut("[AUTO-ACTION] Forcing full clear of trigger/delay/cooldown state", true);
        s_lastClearFrame = nowF;
    }

    p1DelayState = {false, 0, TRIGGER_NONE, 0, -1, -1, 0, -1, -1, 0, 0, 0};
    p2DelayState = {false, 0, TRIGGER_NONE, 0, -1, -1, 0, -1, -1, 0, 0, 0};

    p1ActionApplied = false;
    p2ActionApplied = false;

    g_lastActiveTriggerType.store(TRIGGER_NONE);
    g_lastActiveTriggerFrame.store(0);

    p1TriggerActive = false; p1TriggerCooldown = 0;
    p2TriggerActive = false; p2TriggerCooldown = 0;
    ResetDeferredDashFollowupState();

    KaoriRecoilDuck::CancelAll("clear all auto-action triggers");
    CancelAllAutoActionChargeFollowups("clear all auto-action triggers");
    CancelAllAutoActionMotionTransactions("clear all auto-action triggers");

    // Controller restore refuses to cut through an active normal consumer.
    // Retire that owner first so the hard reset cannot leave P2 humanized.
    CancelAllAutoActionNormalPulses();

    if (g_p2ControlOverridden) {
        RestoreP2ControlState();
    }
    if (!g_p2ControlOverridden.load(std::memory_order_acquire)) {
        g_pendingControlRestore.store(false, std::memory_order_release);
        g_pendingRestoreTimestamp.store(0, std::memory_order_release);
    }
    g_lastP2MoveID.store(-1);
    g_crgFastRestore.store(false);

    StopBufferFreezing();

    s_p1RGPending = false; s_p1RGExpiry = 0;
    s_p2RGPending = false; s_p2RGExpiry = 0;

    s_p1RGPrearmed = false; s_p1RGPrearmIsSpecial = false; s_p1RGPrearmActionType = -1; s_p1RGPrearmExpiry = 0;
    s_p2RGPrearmed = false; s_p2RGPrearmIsSpecial = false; s_p2RGPrearmActionType = -1; s_p2RGPrearmExpiry = 0;
    s_p1RGPrearmGeneration = 0;
    s_p2RGPrearmGeneration = 0;

    ResetWakeRuntimeState();

    if (logThis) {
        LogOut("[AUTO-ACTION] All trigger states cleared", true);
    }
    InvalidateCachedCharacterIDs("trigger clear");
}

static inline bool HasEnabledRows(int triggerType) {
    auto countEnabled = [](const TriggerOption* arr, int cnt){ int n=0; for (int i=0;i<cnt;i++) if (arr[i].enabled) ++n; return n; };
    bool mainEnabled = false;
    switch (triggerType) {
        case TRIGGER_AFTER_BLOCK:   mainEnabled = triggerAfterBlockEnabled.load(); break;
        case TRIGGER_ON_WAKEUP:     mainEnabled = triggerOnWakeupEnabled.load(); break;
        case TRIGGER_AFTER_HITSTUN: mainEnabled = triggerAfterHitstunEnabled.load(); break;
        case TRIGGER_AFTER_AIRTECH: mainEnabled = triggerAfterAirtechEnabled.load(); break;
        case TRIGGER_ON_RG:         mainEnabled = triggerOnRGEnabled.load(); break;
        default: mainEnabled = false; break;
    }
    switch (triggerType) {
        case TRIGGER_AFTER_BLOCK:   return mainEnabled || countEnabled(g_afterBlockOptions, g_afterBlockOptionCount) > 0;
        case TRIGGER_ON_WAKEUP:     return mainEnabled || countEnabled(g_onWakeupOptions, g_onWakeupOptionCount) > 0;
        case TRIGGER_AFTER_HITSTUN: return mainEnabled || countEnabled(g_afterHitstunOptions, g_afterHitstunOptionCount) > 0;
        case TRIGGER_AFTER_AIRTECH: return mainEnabled || countEnabled(g_afterAirtechOptions, g_afterAirtechOptionCount) > 0;
        case TRIGGER_ON_RG:         return mainEnabled || countEnabled(g_onRGOptions, g_onRGOptionCount) > 0;
        default: return mainEnabled;
    }
}

static inline bool PickRandomRow(int triggerType, int playerNum,
                                 TriggerOption &out) {
    const TriggerOption* arr = nullptr; int cnt = 0;
    switch (triggerType) {
        case TRIGGER_AFTER_BLOCK:   arr = g_afterBlockOptions;   cnt = g_afterBlockOptionCount; break;
        case TRIGGER_ON_WAKEUP:     arr = g_onWakeupOptions;     cnt = g_onWakeupOptionCount; break;
        case TRIGGER_AFTER_HITSTUN: arr = g_afterHitstunOptions; cnt = g_afterHitstunOptionCount; break;
        case TRIGGER_AFTER_AIRTECH: arr = g_afterAirtechOptions; cnt = g_afterAirtechOptionCount; break;
        case TRIGGER_ON_RG:         arr = g_onRGOptions;         cnt = g_onRGOptionCount; break;
        default: return false;
    }

    const int targetChar = playerNum == 1 ? displayData.p1CharID
                                           : displayData.p2CharID;
    auto available = [&](const TriggerOption& option) {
        return option.macroSlot > 0 ||
            CharacterActionCatalog::IsAvailable(
                targetChar, option.action,
                CatalogStrengthForAction(option.action, option.strength));
    };
    auto normalize = [&](TriggerOption option) {
        if (option.macroSlot > 0 ||
            !ActionSupportsChargeFollowup(option.action)) {
            option.chargeFollowup = 0;
        } else {
            option.chargeFollowup = CLAMP(option.chargeFollowup, 0, 2);
        }
        return option;
    };

    TriggerOption cands[MAX_TRIGGER_OPTIONS + 1];
    int n = 0;
    for (int i = 0; i < cnt && i < MAX_TRIGGER_OPTIONS; ++i) {
        if (arr[i].enabled && available(arr[i])) {
            cands[n++] = normalize(arr[i]);
        }
    }

    bool mainEnabled = false;
    TriggerOption mainOpt{};
    switch (triggerType) {
        case TRIGGER_AFTER_BLOCK:
            mainEnabled = triggerAfterBlockEnabled.load();
            mainOpt = { true, triggerAfterBlockAction.load(), triggerAfterBlockStrength.load(), triggerAfterBlockDelay.load(), triggerAfterBlockCustomID.load(), TriggerMacroSlotForType(TRIGGER_AFTER_BLOCK), triggerAfterBlockCharge.load() };
            break;
        case TRIGGER_ON_WAKEUP:
            mainEnabled = triggerOnWakeupEnabled.load();
            mainOpt = { true, triggerOnWakeupAction.load(), triggerOnWakeupStrength.load(), triggerOnWakeupDelay.load(), triggerOnWakeupCustomID.load(), TriggerMacroSlotForType(TRIGGER_ON_WAKEUP), triggerOnWakeupCharge.load() };
            break;
        case TRIGGER_AFTER_HITSTUN:
            mainEnabled = triggerAfterHitstunEnabled.load();
            mainOpt = { true, triggerAfterHitstunAction.load(), triggerAfterHitstunStrength.load(), triggerAfterHitstunDelay.load(), triggerAfterHitstunCustomID.load(), TriggerMacroSlotForType(TRIGGER_AFTER_HITSTUN), triggerAfterHitstunCharge.load() };
            break;
        case TRIGGER_AFTER_AIRTECH:
            mainEnabled = triggerAfterAirtechEnabled.load();
            mainOpt = { true, triggerAfterAirtechAction.load(), triggerAfterAirtechStrength.load(), triggerAfterAirtechDelay.load(), triggerAfterAirtechCustomID.load(), TriggerMacroSlotForType(TRIGGER_AFTER_AIRTECH), triggerAfterAirtechCharge.load() };
            break;
        case TRIGGER_ON_RG:
            mainEnabled = triggerOnRGEnabled.load();
            mainOpt = { true, triggerOnRGAction.load(), triggerOnRGStrength.load(), triggerOnRGDelay.load(), triggerOnRGCustomID.load(), TriggerMacroSlotForType(TRIGGER_ON_RG), triggerOnRGCharge.load() };
            break;
        default: break;
    }
    if (mainEnabled && available(mainOpt)) {
        cands[n++] = normalize(mainOpt);
    }

    if (n <= 0) return false;
    int seed = frameCounter.load() + triggerType * 13;
    int pick = (seed < 0 ? -seed : seed) % n;
    out = cands[pick];
    return true;
}

static bool SupportsImmediateWakeHold(int actionType) {
    if (actionType == ACTION_FINAL_MEMORY) return false;
    if (IsDashRecipeAction(actionType)) {
        return false;
    }
    if (actionType == ACTION_JUMP) return true;
    int motionType = ConvertTriggerActionToMotion(actionType, TRIGGER_ON_WAKEUP);
    if (motionType >= MOTION_236A) return false;
    if (NormalInputPolicy::IsNormalMotion(motionType)) {
        return true;
    }
    if (actionType == ACTION_BLOCK) return true;
    return false;
}

static bool BuildImmediateWakeHoldMask(int playerNum, int actionType, uint8_t &outMask) {
    const bool facingRight = GetPlayerFacingDirection(playerNum);
    const uint8_t forwardDir = facingRight ? GAME_INPUT_RIGHT : GAME_INPUT_LEFT;
    const uint8_t backDir    = facingRight ? GAME_INPUT_LEFT  : GAME_INPUT_RIGHT;
    int motionType = ConvertTriggerActionToMotion(actionType, TRIGGER_ON_WAKEUP);
    int buttonMask = ResolveButtonMaskForAction(actionType, TRIGGER_ON_WAKEUP);
    if (buttonMask == 0) {
        buttonMask = GAME_INPUT_A;
    }

    if (actionType == ACTION_JUMP) {
        int dir = triggerOnWakeupStrength.load();
        if (playerNum == 1 && s_p1WakePrearmStrength >= 0) dir = s_p1WakePrearmStrength;
        if (playerNum == 2 && s_p2WakePrearmStrength >= 0) dir = s_p2WakePrearmStrength;
        dir = CLAMP(dir, 0, 2);
        uint8_t mask = GAME_INPUT_UP;
        if (dir == 1) {
            mask |= forwardDir;
        } else if (dir == 2) {
            mask |= backDir;
        }
        outMask = mask;
        return true;
    }

    if (actionType == ACTION_BLOCK) {
        outMask = backDir;
        return true;
    }

    const NormalInputPolicy::Intent normalIntent =
        NormalInputPolicy::IntentFromMotion(motionType);
    if (normalIntent) {
        outMask = NormalInputPolicy::ResolveMask(normalIntent, facingRight);
        return outMask != 0;
    }

    if (motionType >= MOTION_5A && motionType <= MOTION_5D) {
        outMask = static_cast<uint8_t>(buttonMask);
        return true;
    }
    if (motionType >= MOTION_2A && motionType <= MOTION_2D) {
        outMask = static_cast<uint8_t>(GAME_INPUT_DOWN | buttonMask);
        return true;
    }
    if (motionType >= MOTION_JA && motionType <= MOTION_JD) {
        outMask = static_cast<uint8_t>(buttonMask);
        return true;
    }
    if (motionType >= MOTION_6A && motionType <= MOTION_6D) {
        outMask = static_cast<uint8_t>(forwardDir | buttonMask);
        return true;
    }
    if (motionType >= MOTION_4A && motionType <= MOTION_4D) {
        outMask = static_cast<uint8_t>(backDir | buttonMask);
        return true;
    }

    return false;
}

static bool IssueWakeImmediateHold(int playerNum, int actionType) {
    const int motionType = ConvertTriggerActionToMotion(
        actionType, TRIGGER_ON_WAKEUP,
        playerNum == 1 ? s_p1WakePrearmStrength : s_p2WakePrearmStrength);
    if (NormalInputPolicy::IsNormalMotion(motionType)) {
        uint64_t generation = 0;
        const auto submit = QueueAutoActionNormalPulse(
            playerNum, motionType,
            NormalInputPolicy::Timing::WhenActionable,
            &generation);
        TraceAutoActionWakeEvent("wake-native-normal-pulse", playerNum,
                                 static_cast<short>(GetPlayerMoveID(playerNum)),
                                 static_cast<short>(GetPlayerMoveID(playerNum)),
                                 "action=" + std::to_string(actionType) +
                                 " motion=" + std::to_string(motionType) +
                                 " gen=" + std::to_string(generation) +
                                 " accepted=" + std::to_string(
                                     submit == NormalInputPolicy::SubmitResult::Accepted));
        if (submit == NormalInputPolicy::SubmitResult::Accepted) {
            if (playerNum == 1) {
                s_p1WakeNormalGeneration = generation;
                ++s_p1WakeNormalAttempts;
            } else {
                s_p2WakeNormalGeneration = generation;
                ++s_p2WakeNormalAttempts;
            }
        }
        return submit == NormalInputPolicy::SubmitResult::Accepted;
    }

    uint8_t mask = 0;
    if (!BuildImmediateWakeHoldMask(playerNum, actionType, mask)) {
        LogOut("[AUTO-ACTION] Wake immediate hold skipped (no mask) action=" + std::to_string(actionType), detailedLogging.load());
        return false;
    }
    constexpr int kWakeHoldTicks = 3;
    
    uint16_t currentMoveID = 0;
    uintptr_t playerPtr = GetPlayerPointer(playerNum);
    if (playerPtr) {
        SafeReadMemory(playerPtr + MOVE_ID_OFFSET, &currentMoveID, sizeof(uint16_t));
    }
    
    const bool accepted =
        ImmediateInput::PressFor(playerNum, mask, kWakeHoldTicks);
    TraceAutoActionWakeEvent("wake-immediate-hold-pressfor", playerNum,
                             static_cast<short>(currentMoveID),
                             static_cast<short>(currentMoveID),
                             "action=" + std::to_string(actionType) +
                             " mask=" + std::to_string(mask) +
                             " ticks=" + std::to_string(kWakeHoldTicks) +
                             " accepted=" + std::to_string(accepted));
    LogOut("[AUTO-ACTION] Wake immediate hold " +
           std::string(accepted ? "issued" : "rejected") +
           " (p" + std::to_string(playerNum) +
           ", action=" + std::to_string(actionType) +
           ", mask=" + std::to_string(mask) +
           ", ticks=" + std::to_string(kWakeHoldTicks) +
           ", moveID=" + std::to_string(currentMoveID) + ")", detailedLogging.load());
    return accepted;
}

static int ResolveButtonMaskForAction(int actionType, int triggerType, int strengthOverride) {
    int buttonMask = 0;
    if (strengthOverride >= 0 && IsStrengthButtonAction(actionType)) {
        int strength = CLAMP(strengthOverride, 0, 3);
        buttonMask = (1 << (4 + strength));
        if (detailedLogging.load()) {
            std::stringstream bm;
            bm << std::hex << buttonMask;
            LogOut("[AUTO-ACTION][BTN-RESOLVE] (override-fastpath) actionType=" + std::to_string(actionType) +
                   " triggerType=" + std::to_string(triggerType) +
                   " strengthOverride=" + std::to_string(strengthOverride) +
                   " -> btnMask=0x" + bm.str(), true);
        }
        return buttonMask;
    }
    if ((actionType >= ACTION_5A && actionType <= ACTION_2D) ||
        (actionType >= ACTION_6A && actionType <= ACTION_4D)) {
        int button = 0;
        if (actionType >= ACTION_5A && actionType <= ACTION_2D) {
            button = (actionType - ACTION_5A) % 4;
        } else {
            int offset = actionType - ACTION_6A;
            button = offset % 4;
        }
        buttonMask = (1 << (4 + button));
    } else if (actionType >= ACTION_JA && actionType <= ACTION_JD) {
        int button = (actionType - ACTION_JA) % 4;
        buttonMask = (1 << (4 + button));
    } else if (IsStrengthButtonAction(actionType)) {
        int strength = (strengthOverride >= 0) ? strengthOverride : GetSpecialMoveStrength(actionType, triggerType);
        strength = CLAMP(strength, 0, 3);
        buttonMask = (1 << (4 + strength));
    }
    if (detailedLogging.load()) {
        std::stringstream bm;
        bm << std::hex << buttonMask;
        LogOut("[AUTO-ACTION][BTN-RESOLVE] actionType=" + std::to_string(actionType) +
               " triggerType=" + std::to_string(triggerType) +
               " strengthOverride=" + std::to_string(strengthOverride) +
               " -> btnMask=0x" + bm.str(), true);
    }
    return buttonMask;
}

static bool ExecuteWakeSpecialNow(int playerNum, int actionType,
                                  short currentMoveID,
                                  int strengthOverride,
                                  uint64_t* generationOut) {
    if (generationOut) *generationOut = 0;
    if (actionType == ACTION_FINAL_MEMORY) {
        int charId = (playerNum == 1) ? displayData.p1CharID : displayData.p2CharID;
        if (charId < 0) {
            LogOut("[AUTO-ACTION][FM] Wake special aborted: unknown character", true);
            return false;
        }
        return ExecuteFinalMemory(playerNum, charId, 6, generationOut);
    }

    int motionType = ConvertTriggerActionToMotion(
        actionType, TRIGGER_ON_WAKEUP, strengthOverride);
    if (motionType < MOTION_236A) {
        LogOut("[AUTO-ACTION] Wake special requested for non-special action", true);
        return false;
    }

    int buttonMask = ResolveButtonMaskForAction(actionType, TRIGGER_ON_WAKEUP, strengthOverride);
    if (buttonMask == 0 && motionType != MOTION_FORWARD_DASH && motionType != MOTION_BACK_DASH) {
        int fallbackStrength = (strengthOverride >= 0) ? strengthOverride : 0;
        buttonMask = (1 << (4 + CLAMP(fallbackStrength, 0, 3)));
    }

    g_injectImmediateOnly[playerNum].store(false);
    bool facing = GetPlayerFacingDirection(playerNum);
    std::stringstream btnFmt;
    btnFmt << std::hex << buttonMask;
    LogOut("[AUTO-ACTION] Wake special input request p" + std::to_string(playerNum) +
           " motion=" + std::to_string(motionType) + " btnMask=0x" + btnFmt.str() +
            " currMove=" + std::to_string(currentMoveID) + " facing=" + (facing?"right":"left") +
            " strengthOverride=" + std::to_string(strengthOverride), true);
    TraceAutoActionWakeEvent("wake-special-input-request", playerNum, currentMoveID, currentMoveID,
                             "motion=" + std::to_string(motionType) +
                             " btnMask=" + std::to_string(buttonMask) +
                             " facing=" + std::string(facing ? "right" : "left") +
                             " strengthOverride=" + std::to_string(strengthOverride));
    uint64_t generation = 0;
    const AutoActionMotionSubmitResult submit =
        SubmitAutoActionMotionTransaction(
            playerNum, motionType, buttonMask, 6, &generation);
    const bool ok = submit == AutoActionMotionSubmitResult::Accepted;
    if (ok && generationOut) *generationOut = generation;
    TraceAutoActionWakeEvent("wake-special-input-result", playerNum, currentMoveID, currentMoveID,
                             "motion=" + std::to_string(motionType) +
                             " btnMask=" + std::to_string(buttonMask) +
                             " ok=" + std::to_string(ok));
    if (!ok) {
        LogOut("[AUTO-ACTION] Wake special input dispatch failed for player " + std::to_string(playerNum), true);
    }
    return ok;
}

static bool ExecuteWakeSpecialWithChargeWatcher(
    int playerNum, int actionType, short currentMoveID,
    int strengthOverride, int chargeFollowup,
    uint64_t* generationOut, uint64_t* chargeReservationOut) {
    if (generationOut) *generationOut = 0;
    if (chargeReservationOut) *chargeReservationOut = 0;

    const int normalizedCharge = ActionSupportsChargeFollowup(actionType)
        ? CLAMP(chargeFollowup, 0, 2)
        : 0;

    std::unique_lock<std::recursive_mutex> admissionLock;
    if (normalizedCharge != 0) {
        admissionLock =
            std::unique_lock<std::recursive_mutex>(g_p2ControlMutex);
    }

    uint64_t generation = 0;
    const bool primaryAccepted = ExecuteWakeSpecialNow(
        playerNum, actionType, currentMoveID, strengthOverride, &generation);
    if (!primaryAccepted) return false;
    if (generationOut) *generationOut = generation;

    if (normalizedCharge != 0) {
        uint64_t reservation = 0;
        const auto mode = static_cast<AutoActionChargePolicy::Mode>(
            normalizedCharge);
        if (ArmAutoActionChargeFollowup(
                playerNum, mode, TRIGGER_ON_WAKEUP, actionType,
                &reservation, generation)) {
            if (chargeReservationOut) {
                *chargeReservationOut = reservation;
            }
            LogOut("[AUTO-ACTION][WAKE][CHARGE] P" +
                       std::to_string(playerNum) +
                       " passive watcher reserved after primary admission gen=" +
                       std::to_string(generation) +
                       " reserve=" + std::to_string(reservation) +
                       "; no 22C submitted",
                   true);
        } else {
            // Optional follow-up failure must never demote a frame-one wake
            // reversal. The primary transaction remains authoritative.
            LogOut("[AUTO-ACTION][WAKE][CHARGE] P" +
                       std::to_string(playerNum) +
                       " primary admitted, but passive IC/FIC watcher was unavailable; "
                       "reversal retained without 22C follow-up",
                   true);
        }
    }
    return true;
}
