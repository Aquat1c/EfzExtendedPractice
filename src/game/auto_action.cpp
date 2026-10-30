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
#include "../include/input/input_buffer.h"     // For g_activeFreezePlayer owner check
#include "../include/game/attack_reader.h"
#include "../include/input/immediate_input.h"
#include "../include/input/injection_control.h"
#include "../include/input/scoped_input_reservation.h"
#include "../include/game/fm_commands.h" // Final Memory execution
#include "../include/game/macro_controller.h" // Integrate macros with triggers
#include "../include/game/mission/mission_engine.h"
#include "../include/game/mission/tutorial_episode_policy.h"
#include "../include/game/mission/tutorial_session.h"
#include "../include/game/character_settings.h" // For authoritative character ID mapping
#include "../include/game/character_action_catalog.h"
#include "../include/game/auto_action_charge.h"
#include "../include/game/auto_action_charge_policy.h"
#include "../include/game/kaori_recoil_duck.h"
#include "../include/game/frame_analysis.h" // For IsThrown/IsHitstun/IsLaunched helpers
#include "../include/game/per_frame_sample.h" // Unified per-frame sample accessor
#include "../include/game/validation_metrics.h" // Validation metrics instrumentation
#include "../include/game/validation_metrics.h" // Validation metrics instrumentation
#include "../include/gui/overlay.h" // For DirectDrawHook::AddMessage
#include "../include/utils/switch_players.h"

// Safety forward declarations (in case of include-order differences in some build phases)
bool IsThrown(short moveID);
// Forward declarations for row selection helpers used in StartTriggerDelay
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

// Define the motion input constants if they're not already defined
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

// Wake jump tracking for debugging
static int s_p1WakeJumpTrackFrame = -1;
static int s_p2WakeJumpTrackFrame = -1;

// Helper function: Check if moveID represents an action that could be buffered after wake
// (attacks, walks, dashes, jumps, etc.)
static inline bool IsBufferableAction(short moveID) {
    // Attacks (normals, specials, supers)
    if (IsAttackMove(moveID)) return true;
    
    // Walks
    if (moveID == WALK_FWD_ID || moveID == WALK_BACK_ID) return true;
    
    // Dashes
    if (moveID == FORWARD_DASH_START_ID || moveID == FORWARD_DASH_RECOVERY_ID ||
        moveID == FORWARD_DASH_RECOVERY_SENTINEL_ID ||
        moveID == BACKWARD_DASH_START_ID || moveID == BACKWARD_DASH_RECOVERY_ID ||
        moveID == KAORI_FORWARD_DASH_START_ID) {
        return true;
    }
    
    // Jumps
    if (moveID == STRAIGHT_JUMP_ID || moveID == FORWARD_JUMP_ID || moveID == BACKWARD_JUMP_ID) {
        return true;
    }
    
    // Crouch (can be buffered after wake)
    if (moveID == CROUCH_ID) return true;
    
    return false;
}

// Wakeup timing table: rising frames(moveid 96) (visual @ 64Hz) for each character
// Multiply by 3 to get logical frames (192Hz ticks)
struct WakeupTiming {
    int charID;
    int risingFramesVisual;  // Visual frames at 64Hz
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

// Get rising frame count for character (returns visual frames at 64Hz)
static int GetWakeupRisingFrames(int charID) {
    for (const auto& timing : s_wakeupTimings) {
        if (timing.charID == charID) {
            return timing.risingFramesVisual;  // Visual frames at 64Hz
        }
    }
    // Default fallback: assume mid-range wakeup (20 visual frames)
    return 20;
}

// Get rising frame count for character (returns logical ticks at 192Hz)
static int GetWakeupRisingTicks(int charID) {
    for (const auto& timing : s_wakeupTimings) {
        if (timing.charID == charID) {
            return VisualFramesToInternalTicks(timing.risingFramesVisual);
        }
    }
    // Default fallback: assume mid-range wakeup (20 visual frames × 3 = 60 ticks)
    return VisualFramesToInternalTicks(20);
}

TriggerDelayState p1DelayState = {false, 0, TRIGGER_NONE, 0, -1, -1, 0, -1, -1, 0, 0, 0};
TriggerDelayState p2DelayState = {false, 0, TRIGGER_NONE, 0, -1, -1, 0, -1, -1, 0, 0, 0};
bool p1ActionApplied = false;
bool p2ActionApplied = false;

std::atomic<int> g_lastActiveTriggerType(TRIGGER_NONE);
std::atomic<int> g_lastActiveTriggerFrame(0);

// Globals to track trigger cancelled state (flashes red on cancel)
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
// Log throttles to avoid spamming the console during tight loops
static int p1TriggerCooldown = 0;
static int p2TriggerCooldown = 0;
static constexpr int TRIGGER_COOLDOWN_FRAMES = 6; // base cooldown chosen so triggers re-arm quickly
static constexpr int RESTORE_GRACE_PERIOD = 0; // grace disabled: neutralization prevents immediate re-trigger
static int g_restoreGraceCounter = 0; // counts down from RESTORE_GRACE_PERIOD after control restore
std::atomic<bool> g_p2ControlOverridden{false};
uint32_t g_originalP2ControlFlag = 1; // Default to AI control
std::recursive_mutex g_p2ControlMutex;

namespace {
// Serializes the tick-integrated producer, the legacy frame producer, and
// cancellation/reset entry points that mutate their shared non-atomic state.
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

// Exact controller state owned by the long-lived macro path.  Keep the world
// identity with the flags: restoring a value into a replacement battle object
// is worse than discarding a stale snapshot.
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
// Indexed by playerNum (1..2); index 0 is unused. The wake-macro lease
// machinery serves whichever side is the dummy: P2 by default, P1 after a
// control swap. Every existing P2 caller passes 2 and is unchanged.
uint64_t s_wakeForcedNeutralToken[3] = {0, 0, 0};
uint64_t s_wakeMacroCleanupToken[3] = {0, 0, 0};

static inline bool IsWakeLeasePlayer(int playerNum) {
    return playerNum == 1 || playerNum == 2;
}

// Wake-macro neutralization is a short-lived input owner.  Publish and retire
// it under the same controller barrier as motions, normals, charge follow-ups,
// and detached immediate input so it cannot erase (or be erased by) a newer
// producer.  The exact-value check on release is a final fail-closed guard for
// legacy writers which have not yet migrated to an attributed lease.
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

// The wake-macro epilogue clears EFZ's retained command token and, when
// necessary, its input ring.  Those are shared resources, so the epilogue
// must own that fighter's input lane for its entire monitoring/countdown
// lifetime. If another producer is already active we leave the completion
// signal armed and retry on a later tick instead of deleting that producer's
// input.
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

// Forward dash follow-up deferral: fire on the exact frame the dash window is
// satisfied, even if no new trigger fires.
// mode: 0 = post-dash (inject first frame AFTER queue inactive)
//       1 = dash-normal timing (inject first frame queue becomes active)
// stage (internal): for post-dash, stage advances to 1 once we have seen the
//                   queue active at least one frame; then we wait for inactive.
//                   for dash-normal, stage remains 0 until we fire while active.
struct DashFollowDeferred {
    // We only need: which follow-up (1..6), which player, and whether we've latched dash start.
    std::atomic<int> pendingSel{0};
    std::atomic<int> player{0};
    std::atomic<int> dashStartLatched{0}; // 0 = not yet, 1 = latched & fired, -1 = latched but failed to fire (safety)
    std::atomic<int> isBack{0};           // 0 = forward dash, 1 = backdash
    std::atomic<uint64_t> motionGeneration{0};
    std::atomic<uint64_t> normalGeneration{0};
    std::atomic<int> normalDispatchAttempts{0};
    std::atomic<int> expiryFrame{0};
};
static DashFollowDeferred g_dashDeferred; // single slot (only one forward dash follow-up expected at a time)
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
                                        bool /*dashModeIgnored*/,
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
    // Note: Do NOT arm control-restore here. The actual dash action path will
    // set up override/restore timing when the dash is queued successfully.
}

// Mirror of forward follow-up: schedule for backdash
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
    // Note: Do NOT arm control-restore here. The actual backdash action path will
    // set up override/restore timing when the dash is queued successfully.
}

// Uses authoritative moveID transitions (163->164) instead of queue heuristics;
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
// The dash generation belongs to whichever side is the dummy; both sides
// submit through the same transaction.
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
    
    // Debug: Log MoveID transitions while waiting for dash
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
    
    // Fire exactly on first frame we see the dash start ID (forward or back).
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
                // Air dashes use the same UI follow-up selection, but EFZ has
                // no grounded 66X/662X posture there. Deliver the matching
                // j.X without an accidental down/forward posture rejection.
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
            
            // A scoped transaction already scrubbed exactly its authored
            // history at the dash consumer barrier. Only a legacy-queue dash
            // (no generation) retains the legacy whole-buffer cleanup.
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
// Counter-RG fast restore: when active, restore AI control as soon as the special actually starts
std::atomic<bool> g_crgFastRestore(false);
// Forward dash tracking to prevent premature restore before MoveID 163 appears
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
// Grace period: prevent fallback restore from triggering immediately after freeze starts
static std::atomic<DWORD> g_pendingRestoreTimestamp(0);

// When true, the next macro-driven control restore should preserve the input
// buffer and motion token (used for wake pre-buffered macros). The player
// latch records which side armed it, so the completion signal in
// MacroController only honours it for that playback (P2 by default, P1 after
// a control swap) and never for an unrelated P1 playback.
std::atomic<bool> g_macroWakePreserveBuffer(false);
std::atomic<int> g_macroWakePreservePlayer(0);

// Set to the play player (1 or 2; 0 = none) when a wake-prebuffered macro's
// playback (including finish-guard) has fully completed. Auto-action uses
// this to schedule monitoring for successful buffered move execution on that
// fighter, then cleans up its motion token after the buffered move executes
// (similar to regular macros).
std::atomic<int> g_wakeMacroPlaybackCompleted(0);
// Per-player cleanup bookkeeping, indexed by playerNum (index 0 unused).
static int s_wakeMacroTokenNeutralizeCountdown[3] = {0, 0, 0};
static short s_lastWakeMoveID[3] = {-1, -1, -1}; // Track moveID transitions for cleanup
static bool s_wakeMoveIDChanged[3] = {false, false, false}; // unused but kept for compatibility

// Pre-arm flags for On Wakeup: buffer inputs during GROUNDTECH_RECOVERY to fire on wake
static bool s_p1WakePrearmed = false; // wake pre-arm without early triggerActive
static bool s_p2WakePrearmed = false; // same
static int  s_p1WakePrearmExpiry = 0;
static int  s_p2WakePrearmExpiry = 0;
// Wake pre-arm action metadata (so we can execute normals/dashes at the actionable frame)
static int  s_p1WakePrearmActionType = -1;
static int  s_p2WakePrearmActionType = -1;
static int  s_p1WakePrearmStrength  = -1; // row/button strength chosen for wake pre-arm
static int  s_p2WakePrearmStrength  = -1;
// Charge selection is passive wake metadata. It reserves a watcher alongside
// the primary input, but never submits 22C until that source move has started
// and its later IC/FIC evidence is satisfied.
static int  s_p1WakePrearmCharge = 0;
static int  s_p2WakePrearmCharge = 0;
static bool s_p1WakePrearmIsSpecial = false; // specials rely on buffer freeze; normals/dashes injected later
static bool s_p2WakePrearmIsSpecial = false;
static bool s_p1WakePrearmIsMacro = false; // macro wake has its own timing logic, not treated as special
static bool s_p2WakePrearmIsMacro = false;
static bool s_p1WakeActionableWarning = false;
static bool s_p2WakeActionableWarning = false;
// Wake special early buffer: count logical frames (192Hz) in moveID 96
// Buffer on last rising frame before actionable (character-specific timing)
static int s_p1WakeMoveID96FrameCount = 0;
static int s_p2WakeMoveID96FrameCount = 0;
static bool s_p1WakeBufferFrozen = false;
static bool s_p2WakeBufferFrozen = false;
// Generation of the scoped P2 motion currently carrying a wake special.  A
// successful queue is only pending; the later native consumer decides whether
// it actually became a move.
static uint64_t s_p1WakeMotionGeneration = 0;
static uint64_t s_p2WakeMotionGeneration = 0;
// Exact charge reservation still owned by the wake coordinator. Acceptance of
// the matching primary transfers it to the autonomous charge watcher by
// clearing this local token without cancelling the watcher.
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
// Macro-specific wake tracking: late pre-buffer window for wakeup macros
static int  s_p1WakeMacro96FrameCount = 0;
static bool s_p1WakeMacroQueued       = false;
static int  s_p1WakeMacroTargetFrame  = -1;
static int  s_p1WakeMacroSlot         = -1;
static int  s_p1WakeMacroStartTick    = 0;   // Tick offset to skip to for wake macros
static int  s_p2WakeMacro96FrameCount = 0;
static bool s_p2WakeMacroQueued       = false;
// Pre-picked option for this wake sequence (row selection done early in moveID 96)
static TriggerOption s_p1WakePrePickedOption = {};
static TriggerOption s_p2WakePrePickedOption = {};
static int  s_p2WakeMacroTargetFrame  = -1;
static int  s_p2WakeMacroSlot         = -1;
static int  s_p2WakeMacroStartFrame   = -1;
static int  s_p2WakeMacroStartTick    = 0;   // Tick offset to skip to for wake macros
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
        case FORWARD_DASH_RECOVERY_ID: return "dash-f-rec";
        case FORWARD_DASH_RECOVERY_SENTINEL_ID: return "dash-f-sentinel";
        case BACKWARD_DASH_START_ID: return "dash-b-start";
        case BACKWARD_DASH_RECOVERY_ID: return "dash-b-rec";
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

// Lightweight stun/wakeup timing trackers for diagnostics
struct StunTimers {
    // Start frames for current stuns (-1 if not in that state)
    int blockStart = -1;
    int hitStart   = -1;
    int wakeMarker = -1;   // Last seen GROUNDTECH_RECOVERY frame before wake
    // Last measured durations (frames)
    int lastBlockDuration = 0;
    int lastHitDuration   = 0;
    int lastWakeDelay     = 0; // Frames from recovery marker (96) to actionable
    // Since-end counters (frames since leaving respective states)
    int sinceBlockEnd = -1;
    int sinceHitEnd   = -1;
    int sinceWake     = -1;    // Frames since actionable after wake
};

static StunTimers s_p1Timers;
static StunTimers s_p2Timers;

static inline bool AutoActionWorkPending();
static void ConsumeTutorialP2WakeCancel();

// (Deprecated) Pending window for On RG: retained for state reset only
static bool s_p1RGPending = false;
static int  s_p1RGExpiry = 0;
static bool s_p2RGPending = false;
static int  s_p2RGExpiry = 0;

// Enable tick-integrated auto-actions by default; the frame monitor will defer when this is on.
std::atomic<bool> g_tickIntegratedAutoActions{ true };

// Lightweight per-tick entry from input hook: evaluates auto-actions once per internal tick
void AutoActionsTick_Inline(short moveID1, short moveID2) {
    // Online mode hard stop (never operate in netplay)
    if (g_onlineModeActive.load()) return;
    if (Mission::Engine::IsPracticeAutomationSuppressed()) return;
    // IC/FIC remains alive after the trigger delay itself completes. Tick it
    // before the general fast path so the reserved native 22C can still fire.
    TickAutoActionChargeFollowups();
    // Kaori's 44~66 recipe likewise outlives the ordinary delay after native
    // 44 is accepted: it must observe move 164 frame 4/5 before submitting 66.
    KaoriRecoilDuck::Tick();
    // This runs in EFZ's input hook. Never stall that hook behind a reset or
    // tutorial lease transition, and never inspect the producer's non-atomic
    // delay/cooldown state without owning its mutex.
    std::unique_lock<std::recursive_mutex> stateLock(
        g_autoActionStateMutex, std::try_to_lock);
    if (!stateLock.owns_lock()) return;
    if (!AutoActionWorkPending()) return;

    // Static prevs to compute edges without extra memory traffic
    static short prevMoveID1 = -1;
    static short prevMoveID2 = -1;

    TraceAutoActionWakePhase("tick:begin", moveID1, moveID2, prevMoveID1, prevMoveID2);
    
    // Check if jump state reached after wake hold
    if (s_p1WakeJumpTrackFrame >= 0) {
        int elapsed = frameCounter.load() - s_p1WakeJumpTrackFrame;
        if (elapsed <= 10) {  // Track for 10 frames
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
        if (elapsed <= 10) {  // Track for 10 frames
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

    // Validate an existing delay against the new fighter state before its
    // countdown can expire. Otherwise a delay reaching zero on the same tick as
    // fresh hit/block/freeze starts queues a stale normal that fires much later.
    ClearDelayStatesIfNonActionable(moveID1, moveID2, prevMoveID1, prevMoveID2, "tick");
    TraceAutoActionWakePhase("tick:after-clear", moveID1, moveID2, prevMoveID1, prevMoveID2);
    ProcessTriggerDelays(moveID1, moveID2, prevMoveID1, prevMoveID2);
    TraceAutoActionWakePhase("tick:after-delays", moveID1, moveID2, prevMoveID1, prevMoveID2);
    // Monitor last so a new edge on this tick may arm a fresh delay after the
    // previous one was either validated/applied or canceled.
    MonitorAutoActions(moveID1, moveID2, prevMoveID1, prevMoveID2);
    TraceAutoActionWakePhase("tick:after-monitor", moveID1, moveID2, prevMoveID1, prevMoveID2);

    TraceAutoActionWakePhase("tick:prev-update", moveID1, moveID2, moveID1, moveID2);
    prevMoveID1 = moveID1;
    prevMoveID2 = moveID2;
}

// Pre-arm specials/FM at RG entry when user delay is 0
static bool s_p1RGPrearmed = false;
static bool s_p2RGPrearmed = false;
static bool s_p1RGPrearmIsSpecial = false; // true when we early-froze a special/FM
static bool s_p2RGPrearmIsSpecial = false;
static int  s_p1RGPrearmActionType = -1;   // store action to help skip duplicate inject
static int  s_p2RGPrearmActionType = -1;
static int  s_p1RGPrearmExpiry = 0;        // safety expiry (internal frames)
static int  s_p2RGPrearmExpiry = 0;
// Generation of the scoped native transaction carrying a pre-armed RG
// special/FM. Both sides use the same transaction; the P1 slot matters after
// a control swap (the dummy is P1).
static uint64_t s_p1RGPrearmGeneration = 0;
static uint64_t s_p2RGPrearmGeneration = 0;

static inline void UpdateStunTimersForPlayer(StunTimers& t, short prevMoveID, short currMoveID) {
    int now = frameCounter.load();

    // Blockstun entry/exit
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

    // Hitstun entry/exit
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

    // Wakeup: remember last recovery marker (96), and when we become actionable from groundtech, compute delay
    bool wasGtech = IsGroundtech(prevMoveID);
    bool isGtech  = IsGroundtech(currMoveID);
    bool actionableNow = IsActionable(currMoveID);
    if (currMoveID == GROUNDTECH_RECOVERY) {
        t.wakeMarker = now;
        t.sinceWake = -1;
    }
    if (wasGtech && actionableNow && !isGtech) {
        // Became actionable after groundtech
        if (t.wakeMarker >= 0) t.lastWakeDelay = now - t.wakeMarker;
        t.sinceWake = 0;
        t.wakeMarker = -1;
    } else if (t.sinceWake >= 0 && actionableNow) {
        t.sinceWake++;
    }
}
// Lightweight check to skip all auto-action work when nothing can or should run
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
    // If macro playback is active, suppress P2 auto-action work to avoid conflicts.
    if (MacroController::GetState() == MacroController::State::Replaying) {
        return false;
    }
    // A tutorial owns the authored dummy. User-configured auto-actions remain
    // saved but dormant for the whole session; the scoped native wake producer
    // is the one deliberate exception.
    const bool tutorialWake =
        g_tutorialP2WakeToken.load(std::memory_order_acquire) != 0;
    const bool tutorialSuppressesTriggers =
        Mission::TutorialEpisodePolicy::SuppressUserAutoActions(
            Mission::TutorialSession::IsActive(), tutorialWake);
    const bool triggersEnabled =
        !tutorialSuppressesTriggers && HasAnyAutoActionTriggerEnabled();
    // If a delay is active, wakeup is pre-armed, cooldowns are running, or restore is pending, keep running
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
    if (!base) return true; // Default to true if can't check
    
    uintptr_t playerOffset = (playerNum == 1) ? EFZ_BASE_OFFSET_P1 : EFZ_BASE_OFFSET_P2;
    uintptr_t yAddr = ResolvePointer(base, playerOffset, YPOS_OFFSET);
    uintptr_t yVelAddr = ResolvePointer(base, playerOffset, YVEL_OFFSET);
    
    if (!yAddr || !yVelAddr) return true; // Default to true if can't check
    
    double yPos = 0.0, yVel = 0.0;
    SafeReadMemory(yAddr, &yPos, sizeof(double));
    SafeReadMemory(yVelAddr, &yVel, sizeof(double));
    
    // Character is considered grounded when very close to y=0 and not moving vertically
    return (yPos <= 0.1 && fabs(yVel) < 0.1);
}

short GetActionMoveID(int actionType, int triggerType, int playerNum) {
    // Custom action deprecated; proceed directly to contextual resolution
    
    // Check character ground/air state for context-specific moves
    bool isGrounded = IsCharacterGrounded(playerNum);
    LogOut("[AUTO-ACTION] Player " + std::to_string(playerNum) + 
           " ground state: " + (isGrounded ? "grounded" : "airborne") + 
           ", action type: " + std::to_string(actionType), detailedLogging.load());
    
    // Air-specific moves - only process these if character is in the air
    if (!isGrounded) {
        switch (actionType) {
            case ACTION_JA:
                return BASE_ATTACK_JA;  // Air A
            case ACTION_JB:
                return BASE_ATTACK_JB;  // Air B
            case ACTION_JC:
                return BASE_ATTACK_JC;  // Air C
            case ACTION_JD:
                return BASE_ATTACK_JD;  // Air D
            default:
                // If in air and trying to do a ground move, use a safe fallback
                return STRAIGHT_JUMP_ID;
        }
    }
    else { // Character is on the ground
        // Air move attempted on ground - check if it's a jump-related action
        if (actionType == ACTION_JA || actionType == ACTION_JB || actionType == ACTION_JC || actionType == ACTION_JD) {
            LogOut("[AUTO-ACTION] Cannot perform air move on ground, using JUMP instead", true);
            return STRAIGHT_JUMP_ID; // If air move selected on ground, do a jump instead
        }
        
        // Ground moves - only available when grounded
        switch (actionType) {
            case ACTION_5A:
                return BASE_ATTACK_5A;  // 200
            case ACTION_5B:
                return BASE_ATTACK_5B;  // 201
            case ACTION_5C:
                return BASE_ATTACK_5C;  // 203
            case ACTION_5D:
                return BASE_ATTACK_5D;  // D button standing
            case ACTION_2A:
                return BASE_ATTACK_2A;  // 204
            case ACTION_2B:
                return BASE_ATTACK_2B;  // 205
            case ACTION_2C:
                return BASE_ATTACK_2C;  // 206
            case ACTION_2D:
                return BASE_ATTACK_2D;  // D button crouching
            case ACTION_JUMP:
                return STRAIGHT_JUMP_ID; // 4
            case ACTION_BACKDASH:
                return GROUND_BACKWARD_DASH_ID; // 164
            case ACTION_BLOCK:
                return STAND_GUARD_ID; // 151
            default:
                return BASE_ATTACK_5A; // Default to 5A for unknown actions
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

// Upper bound on how long a submitted-but-unconsumed normal pulse may hold a
// trigger. 180 internal ticks = 60 visual frames, comfortably longer than the
// slowest legitimate wait (Recoil Guard's whole 42-frame state, or an
// after-airtech ground normal falling to the ground) and far shorter than the
// unbounded stall it replaces.
static constexpr int kMaxPendingNormalWaitTicks = 180;

// Retire a queued pulse that is still Pending. Mirrors the wake pre-arm's
// expiry handling: only cancel while the generation has not resolved, so an
// already-accepted normal is never disturbed.
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
           moveID == FORWARD_DASH_RECOVERY_ID ||
           moveID == FORWARD_DASH_RECOVERY_SENTINEL_ID ||
           moveID == BACKWARD_DASH_START_ID ||
           moveID == BACKWARD_DASH_RECOVERY_ID ||
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

static inline int TriggerMacroSlotForType(int triggerType) {
    switch (triggerType) {
        case TRIGGER_AFTER_BLOCK:   return triggerAfterBlockMacroSlot.load();
        case TRIGGER_ON_WAKEUP:     return triggerOnWakeupMacroSlot.load();
        case TRIGGER_AFTER_HITSTUN: return triggerAfterHitstunMacroSlot.load();
        case TRIGGER_AFTER_AIRTECH: return triggerAfterAirtechMacroSlot.load();
        case TRIGGER_ON_RG:         return triggerOnRGMacroSlot.load();
        default:                    return 0;
    }
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
        // Keep stale bits and their per-entry delays intact, but never roll a
        // move the current target character cannot perform.
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

    // Once a P2 native transaction has been admitted, its resulting attack or
    // dash state is evidence to be consumed, not a reason to erase the delay.
    // The generation publishes its own bounded Accepted/Failed/Cancelled result.
    //
    // The exception is a fighter that has been *interrupted*. Hitstun, a throw,
    // a launch, a freeze or a fresh blockstun is never the queued action's own
    // result, so holding the delay through one is what let an admitted normal
    // survive a knockdown and fire later as a stale ghost action on the first
    // actionable frame of the new state.
    if (state.pendingMotionGeneration != 0 ||
        state.pendingRecipeGeneration != 0 ||
        state.pendingNormalGeneration != 0) {
        // Deliberately narrow: only unambiguous "this fighter was hit" states.
        // Airtech and groundtech are recovery states that AFTER_AIRTECH and
        // ON_WAKEUP legitimately span, and a knockdown is already caught at its
        // launch/throw/hitstun edge before it ever reaches a tech.
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

// MacroController::Play() is hardwired to P2, so the trigger-driven macro
// starts below call PlayForPlayer(playerNum, ...) instead - that is what lets a
// P1 dummy play a macro row at all. Play()'s user-facing handling is part of
// the shipped P2 behaviour, though (cancelling a PreRecord, refusing to run
// during a recording, and the rejection toast), so keep it here for both sides
// rather than dropping it. With state == Idle this is exactly PlayForPlayer().
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

    // P1 delay processing
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

            // Mirror of the P2 native-transaction wait below: submission only
            // admits a generation-owned producer/consumer transaction, so the
            // exact selected option stays alive until EFZ's command consumer
            // publishes a terminal outcome. After a control swap the dummy is
            // P1 and its specials/supers/FM/dashes take this same path.
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

            // Queue admission is not execution. Keep the delayed RG action
            // alive until the pre-armed native transaction publishes a
            // terminal result (mirror of the P2 gate below).
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

            // If a macro is selected for this trigger and has data, prefer
            // playing it (mirror of the P2 block below). Busy playback keeps
            // the same slot for the next tick, like every other P1 wait here.
            if (dispatchThisTick) {
                int sel = TriggerMacroSlotForType(p1DelayState.triggerType);
                // Prefer row-specific macro slot if set
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
    
    // P2 delay processing
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

            // Submission only reserves a producer/consumer generation. Keep
            // the exact selected option alive until EFZ's later slot-4 consumer
            // reports a terminal outcome, then either complete it or retry that
            // same tuple with a small bound.
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

            // Queue admission is not execution.  Keep the delayed RG action
            // alive until the native producer/consumer transaction publishes
            // a terminal result; otherwise this block would reset the delay
            // immediately after ApplyAutoAction observed Pending.
            if (p2DelayState.triggerType == TRIGGER_ON_RG &&
                s_p2RGPrearmed && s_p2RGPrearmIsSpecial &&
                s_p2RGPrearmGeneration != 0 &&
                GetP2AutoActionMotionTransactionOutcome(
                    s_p2RGPrearmGeneration) ==
                    P2AutoActionMotionOutcome::Pending) {
                p2DelayState.delayFramesRemaining = 1;
                return;
            }

            // If a macro is selected for this trigger and has data, prefer playing it
            int sel = 0;
            switch (p2DelayState.triggerType) {
                case TRIGGER_AFTER_BLOCK: sel = triggerAfterBlockMacroSlot.load(); break;
                case TRIGGER_ON_WAKEUP: sel = triggerOnWakeupMacroSlot.load(); break;
                case TRIGGER_AFTER_HITSTUN: sel = triggerAfterHitstunMacroSlot.load(); break;
                case TRIGGER_AFTER_AIRTECH: sel = triggerAfterAirtechMacroSlot.load(); break;
                case TRIGGER_ON_RG: sel = triggerOnRGMacroSlot.load(); break;
                default: sel = 0; break;
            }
            // Prefer row-specific macro slot if set
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

            // Fallback: apply the configured single action via input system using unified sample
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
    // Do not queue trigger delays for a player whose macro is playing; they
    // would fight the macro. Keyed on the playback player so the dummy slot
    // (P2 by default, P1 after a control swap) is treated the same way.
    if (MacroController::GetState() == MacroController::State::Replaying &&
        MacroController::GetPlaybackPlayer() == playerNum) {
        LogOut("[AUTO-ACTION][MACRO] Suppressing P" + std::to_string(playerNum) +
               " StartTriggerDelay during macro playback", detailedLogging.load());
        return;
    }
    // Check if the player is already in a trigger cooldown period
    if (playerNum == 1 && p1TriggerActive) {
    LogOut("[AUTO-ACTION] P1 StartTriggerDelay ignored - trigger already active/cooldown=" + std::to_string(p1TriggerCooldown), detailedLogging.load());
        return;
    }
    if (playerNum == 2 && p2TriggerActive) {
    LogOut("[AUTO-ACTION] P2 StartTriggerDelay ignored - trigger already active/cooldown=" + std::to_string(p2TriggerCooldown), detailedLogging.load());
        return;
    }

    // Set the last active trigger type and frame for overlay feedback
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
    // NOTE: ApplyAutoAction never overrides the AI flag itself. Specials/supers/FM/dashes go through
    // the generation-owned motion/pattern transaction (SubmitAutoActionMotionTransaction /
    // SubmitAutoActionPatternTransaction), which humanizes the target for one producer pass and
    // restores it; normals go through QueueAutoActionNormalPulse, which reads the live AI flag to
    // pick NativePoll vs AiPostWrite. Both work for whichever slot is the dummy
    // (SwitchPlayers::GetRemotePlayerIndex()).
    // Set trigger cooldown to prevent rapid re-triggering
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
    
    // Random Pool is picked at trigger-arm time so wakeup pre-buffering and
    // delayed actions both consume the same concrete action. P2 wakeup may
    // have already rolled an option while entering groundtech; reuse that
    // result so the buffered action cannot diverge from the armed action.
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
        // If there are per-trigger option rows configured, pick one now and
        // override both the chosen action parameters and the delay as needed.
        TriggerOption picked{};
        if (PickRandomRow(triggerType, playerNum, picked)) {
            // Stash chosen params on the appropriate delay state so ApplyAutoAction can consume them
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

    // If delay is 0, apply immediately
    if (delayFrames == 0) {
        // Immediate apply path; ApplyAutoAction uses input system, so addr is informational only
        uintptr_t base = GetEFZBase();
        uintptr_t moveIDAddr = 0;
        if (base) {
            moveIDAddr = (playerNum == 1)
                ? ResolvePointer(base, EFZ_BASE_OFFSET_P1, MOVE_ID_OFFSET)
                : ResolvePointer(base, EFZ_BASE_OFFSET_P2, MOVE_ID_OFFSET);
        }
        // If a macro slot is selected for this trigger, play it instead. The
        // slot was resolved above (row-specific slot first, else the trigger's
        // default slot) and the same path serves whichever side is the dummy:
        // P2 by default, P1 after a control swap.
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
    // For delayed actions, use the existing delay system
    else {
        int internalFrames = VisualFramesToInternalTicks(delayFrames);
        
        if (playerNum == 1) {
            p1DelayState.isDelaying = true;
            p1DelayState.delayFramesRemaining = internalFrames;
            // triggerType already set above
            p1DelayState.pendingMoveID = 0;
            LogOut("[DELAY] Armed P1 delay: visualFrames=" + std::to_string(delayFrames) +
                   ", internalFrames=" + std::to_string(internalFrames) +
                   ", triggerType=" + std::to_string(triggerType) +
                   ", expiresAtInternal=" + std::to_string(frameCounter.load() + internalFrames),
                   true);
        } else {
            p2DelayState.isDelaying = true;
            p2DelayState.delayFramesRemaining = internalFrames;
            // triggerType already set above
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
    // Avoid building strings unless diagnostic logging is enabled, and throttle to ~5s
    if (detailedLogging.load()) {
        static int s_nextCooldownDiagFrame = 0; // 5s throttle at 192 fps => 960 frames
        int now = frameCounter.load();
        if (now >= s_nextCooldownDiagFrame) {
            s_nextCooldownDiagFrame = now + 960;
        }
    }
    // Simplified cooldown progression (no pinning) to avoid deadlocks
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

// Core implementation that uses caller-provided move IDs for better cache locality
static void MonitorAutoActionsImpl(short moveID1, short moveID2, short prevMoveID1, short prevMoveID2) {
    // Only operate in offline Practice mode
    if (GetCurrentGameMode() != GameMode::Practice) return;
    if (IsNetplaySuspendActive()) return;
    
    // Fast-path out when there's definitively no work to do this frame
    if (!AutoActionWorkPending()) {
        return;
    }
    // Quiescent fast-path: if nothing is pending and both move IDs are unchanged, skip all work
    // This keeps idle CPU low when triggers are enabled but no transitions occur.
    // EXCEPTION: Never skip while P2 is in groundtech - we need to count ticks for wake macro timing!
    bool p2InGroundtech = IsGroundtech(moveID2);
    if (!p1DelayState.isDelaying && !p2DelayState.isDelaying &&
        !g_pendingControlRestore.load() && g_dashDeferred.pendingSel.load() == 0 &&
        !s_p1WakePrearmed && !s_p2WakePrearmed && !s_p1RGPrearmed && !s_p2RGPrearmed &&
        p1TriggerCooldown <= 0 && p2TriggerCooldown <= 0 &&
        !WakeCleanupWorkPending() &&
        !p2InGroundtech && // Don't skip during groundtech - need to count wake timing ticks
        moveID1 == prevMoveID1 && moveID2 == prevMoveID2) {
        return;
    }
    ProcessTriggerCooldowns();
    // (Deferred dash follow-ups handled after we have moveID context below)

    MaybeBeginFlowSequence(1, prevMoveID1, moveID1);
    MaybeBeginFlowSequence(2, prevMoveID2, moveID2);

    // Update timing trackers once per tick for both players
    UpdateStunTimersForPlayer(s_p1Timers, prevMoveID1, moveID1);
    UpdateStunTimersForPlayer(s_p2Timers, prevMoveID2, moveID2);

    // Note: Do NOT restore on RG exit; we must keep buffer-freeze active until the special actually begins.
    
    // Switch-player routing is correct for normal Practice use, but a tutorial
    // wake lease explicitly authors P2 and must not follow that global mapping.
    // All other user auto-actions stay dormant while a tutorial owns the dummy.
    const bool tutorialP2Wake =
        g_tutorialP2WakeToken.load(std::memory_order_acquire) != 0;
    const int targetPlayer =
        Mission::TutorialEpisodePolicy::ResolveRuntimeAutoActionTarget(
            Mission::TutorialSession::IsActive(), tutorialP2Wake,
            ResolveAutoActionTargetPlayer());
    // Throttle trigger diagnostics to ~5s intervals
    static int s_nextTrigDiagFrame = 0; // shared across P1/P2 logs
    auto canLogTrigDiag = [&]() -> bool {
        int now = frameCounter.load();
        if (now >= s_nextTrigDiagFrame) {
            s_nextTrigDiagFrame = now + 960; // 960 internal frames ~= 5 seconds @192fps
            return true;
        }
        return false;
    };
    
    // P1 triggers
    if ((targetPlayer == 1 || targetPlayer == 3) && !p1DelayState.isDelaying && !p1ActionApplied) {
        bool shouldTrigger = false;
        int triggerType = TRIGGER_NONE;
        int delay = 0;
        short actionMoveID = 0;        
        // Extra diagnostics for P1 trigger evaluation
    if (detailedLogging.load() && canLogTrigDiag()) {
        }
        
    if (!shouldTrigger && triggerAfterAirtechEnabled.load()) {
            // Check if player was in airtech last frame
            bool wasInAirtech = IsAirtech(prevMoveID1);
            // Post-airtech actionable: allow either general actionable states or explicit FALLING
            bool postAirtechNow = (!IsAirtech(moveID1)) && (IsActionable(moveID1) || moveID1 == FALLING_ID);
            if (detailedLogging.load() && canLogTrigDiag()) {
            }

            // P1 After-Airtech trigger condition
            if (wasInAirtech && postAirtechNow) {
                if (triggerRandomizeEnabled.load()) { if ((rand() & 1) == 0) { if (detailedLogging.load() && canLogTrigDiag()) LogOut("[AUTO-ACTION] P1 After Airtech skipped by random gate", true); goto p1_after_airtech_done; } }
                LogOut("[AUTO-ACTION] P1 After Airtech trigger activated (from moveID " + 
                       std::to_string(prevMoveID1) + " to " + std::to_string(moveID1) + ")", detailedLogging.load());
                shouldTrigger = true;
                triggerType = TRIGGER_AFTER_AIRTECH;
                delay = triggerAfterAirtechDelay.load();

                // Get the appropriate action moveID for After Airtech trigger
                int actionType = triggerAfterAirtechAction.load();
                actionMoveID = GetActionMoveID(actionType, TRIGGER_AFTER_AIRTECH, 1);
            }
        p1_after_airtech_done: ;
        }


        // After Block trigger
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

        // On RG trigger: arm at RG entry and schedule for RG stun end (stand/crouch/air specific)
    if (!shouldTrigger && triggerOnRGEnabled.load()) {
            bool enteredRG = (!IsRecoilGuard(prevMoveID1) && IsRecoilGuard(moveID1));
            if (enteredRG) {
        if (triggerRandomizeEnabled.load()) { if ((rand() & 1) == 0) { if (detailedLogging.load() && canLogTrigDiag()) LogOut("[AUTO-ACTION] P1 On RG skipped by random gate", true); goto p1_onrg_done; } }
                int baseDelayF = 20; // fallback in visual frames
                if (moveID1 == RG_STAND_ID) baseDelayF = RG_STAND_FREEZE_DEFENDER; // 20F
                else if (moveID1 == RG_CROUCH_ID) baseDelayF = RG_CROUCH_FREEZE_DEFENDER; // 22F
                else if (moveID1 == RG_AIR_ID) baseDelayF = RG_AIR_FREEZE_DEFENDER; // 22F
                int userDelayF = triggerOnRGDelay.load();
                int totalDelayF = baseDelayF + userDelayF;
                if (totalDelayF < 0) totalDelayF = 0; // do not allow negative
                LogOut("[TRIGGER_DIAG] P1 entered RG (" + std::to_string(moveID1) + ") baseDelay=" + std::to_string(baseDelayF) +
                       "F + user=" + std::to_string(userDelayF) + "F => total=" + std::to_string(totalDelayF) + "F", true);
                // Schedule execution exactly at RG stun end (converted to internal frames in StartTriggerDelay)
                StartTriggerDelay(1, TRIGGER_ON_RG, 0, totalDelayF);
                const int selectedUserDelayF = (p1DelayState.chosenDelay >= 0)
                    ? p1DelayState.chosenDelay
                    : userDelayF;

                // If user requested 0F delay and action is a special/FM, pre-arm now so motion exists on the first actionable frame
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
                        } else { // directional normals contiguous groups 6A..6D then 4A..4D
                            int offset = at - ACTION_6A;
                            button = offset % 4; // 0=A,1=B,2=C,3=D
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
                        // Mirror of the P2 pre-arm: the scoped transaction is
                        // the only transport that humanizes an AI-flagged P1
                        // dummy for the producer pass.
                        uint64_t prearmGeneration = 0;
                        if (at == ACTION_FINAL_MEMORY) {
                            int charId = displayData.p1CharID;
                            preOk = ExecuteFinalMemory(
                                1, charId, 120, &prearmGeneration);
                        } else {
                            // Ensure buffer writes allowed
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
                        s_p1RGPrearmExpiry = GetEngineTick() + (baseDelayF * 3) + 90; // small guard window after actionable
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
        
        // After Hitstun trigger
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
        
        // On Wakeup: pick the configured wake option as soon as groundtech
        // starts (any of the 97/98/99/96 states), independent of the pre-arm
        // gate below and of the Pre-buffer Wakeup toggle, exactly like the P2
        // path. The pre-arm block and the generic On-Wakeup trigger both
        // consume this same pick, so the buffered action cannot diverge from
        // the armed action.
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

        // On Wakeup pre-arm: record metadata when delay == 0 and action is a special/FM.
        // IMPORTANT: With Pre-buffer Wakeup OFF a 0F macro row is NOT pre-armed; the generic
        // On-Wakeup trigger fires StartTriggerDelay(1, ...) which plays the macro on the first
        // actionable frame (same as the P2 path). With it ON the macro is pre-buffered during 96.
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
                // If a macro slot is selected for On Wakeup, avoid pre-arm so that
                // StartTriggerDelay can fire a macro (including 0F) instead. Allow
                // pre-arm to proceed if the configured slot is empty to prevent
                // silent no-ops.
                int macroSlot = s_p1WakeOptionPicked ? s_p1WakePrePickedOption.macroSlot : triggerOnWakeupMacroSlot.load();
                int macroSel = CLAMP(macroSlot, 1, MacroController::GetSlotCount());
                bool macroHasData = (macroSlot > 0) && MacroController::GetState() != MacroController::State::Recording && !MacroController::IsSlotEmpty(macroSel);
                bool isDash = (motionType == MOTION_FORWARD_DASH || motionType == MOTION_BACK_DASH);
                bool isSpecial = (actionType == ACTION_FINAL_MEMORY) ||
                                 (motionType >= MOTION_236A && !isDash);
                const int chargeFollowup = s_p1WakeOptionPicked
                    ? s_p1WakePrePickedOption.chargeFollowup
                    : triggerOnWakeupCharge.load();
                // A populated 0F macro row owns this wakeup on both sides
                // (mirror of P2's dedicatedMacroOwnsWake). With Pre-buffer
                // Wakeup ON the macro is pre-armed here and started during
                // state 96; with it OFF nothing is pre-armed and the generic
                // On-Wakeup trigger plays it on the first actionable frame.
                const bool dedicatedMacroOwnsWake = macroHasData && (userDelayF == 0);
                bool isMacro = dedicatedMacroOwnsWake && g_wakeBufferingEnabled.load();
                // A valid macro owns this wakeup.  Never prime the row's normal
                // action (or its special) as a second producer behind the macro.
                bool holdEligible = (userDelayF == 0) && !isSpecial && !dedicatedMacroOwnsWake &&
                                    SupportsImmediateWakeHold(actionType);
                const bool shouldPrearm = (userDelayF == 0) &&
                    (isMacro || (!dedicatedMacroOwnsWake && (isSpecial || holdEligible)));
                if (shouldPrearm) {
                    s_p1WakePrearmed = true;
                    // A populated wake macro is the complete authored action;
                    // its row's ordinary move must not become a second wake
                    // producer (or reserve an IC/FIC lane ahead of playback).
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
                    // Initialize macro state
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

        // On Wakeup trigger (fallback if not pre-armed or for normals/jumps)
        bool p1BecameActionableFromGroundtech = IsGroundtech(prevMoveID1) && IsActionable(moveID1);
        if (s_p1WakePrearmed) {
            // Count logical frames (192Hz) in moveID 96, buffer on last rising frame
            if (moveID1 == GROUNDTECH_RECOVERY) {
                s_p1WakeMoveID96FrameCount++;
                s_p1WakeMacro96FrameCount++;  // Also increment macro frame counter (64Hz equivalent)
                // Get character-specific rising frame count
                int charID = displayData.p1CharID;
                int risingTicks = GetWakeupRisingTicks(charID);
                int bufferFrame = risingTicks - 1;  // Buffer on last frame before actionable
                // Allow 3-frame window for safety (buffer-3 to buffer)
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
                
                // Macro pre-buffering for P1: calculate optimal playback start frame
                int macroSlot = s_p1WakeOptionPicked ? s_p1WakePrePickedOption.macroSlot : triggerOnWakeupMacroSlot.load();
                int userDelayF = s_p1WakeOptionPicked ? s_p1WakePrePickedOption.delay : triggerOnWakeupDelay.load();
                if (s_p1WakeOptionPicked) {
                    userDelayF = (userDelayF <= 1) ? 0 : (userDelayF - 1);
                }
                if (g_wakeBufferingEnabled.load() &&
                    macroSlot > 0 && userDelayF == 0 && !s_p1WakeMacroQueued) {
                    int risingFrames = GetWakeupRisingFrames(charID); // Visual frames at 64Hz (13-39)
                    
                    // Find the FIRST attack button in the macro
                    int sel = CLAMP(macroSlot, 1, MacroController::GetSlotCount());
                    int firstButtonTick = MacroController::GetFirstButtonTick(sel);
                    
                    if (firstButtonTick < 0) {
                        // No attack button in macro, nothing to buffer
                        s_p1WakeMacroTargetFrame = -1;
                        s_p1WakeMacroSlot = -1;
                        s_p1WakeMacroStartTick = 0;
                    } else {
                        // Convert ticks to frames (ceil division to be safe)
                        int framesToButton = (firstButtonTick + 2) / 3;
                        
                    // We want the attack button pressed before actionable (inside buffer window)
                    // playStartFrame = when to start the macro during state 96
                    // kBufferMargin accounts for the buffer window + timing margin
                        constexpr int kBufferMargin = 12;
                        int playStartFrame = risingFrames - framesToButton - kBufferMargin;
                        if (playStartFrame < 1) playStartFrame = 1;
                        
                        // NOTE: Counter increments at 192Hz (ticks), so convert target to ticks
                        s_p1WakeMacroStartTick = 0; // Play from beginning
                        s_p1WakeMacroTargetFrame = playStartFrame * 3; // Convert visual frames to ticks
                        s_p1WakeMacroSlot = macroSlot;
                        
                        if (detailedLogging.load() && s_p1WakeMacro96FrameCount == 1) {
                            LogOut("[AUTO-ACTION][MACRO-TIMING-INIT] P1 charID=" + std::to_string(charID) +
                                   " risingFrames=" + std::to_string(risingFrames) +
                                   " firstBtnTick=" + std::to_string(firstButtonTick) +
                                   " framesToBtn=" + std::to_string(framesToButton) +
                                   " playStartFrame=" + std::to_string(playStartFrame), true);
                        }
                        
                        // Force Neutral while waiting for the macro so AI
                        // inputs (e.g. holding Down) cannot cause accidental
                        // transitions before it starts (mirror of the P2 lease).
                        if (!s_p1WakeMacroQueued && s_p1WakeMacroSlot > 0) {
                            const bool macroPlaying =
                                (MacroController::GetState() == MacroController::State::Replaying);
                            if (!macroPlaying && s_wakeForcedNeutralToken[1] == 0) {
                                (void)TryPublishWakeForcedNeutral(1);
                            }
                        }

                        // Start macro playback at calculated frame
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
                                    // Forced neutral is this wake path's own
                                    // short lease. Drop it before macro
                                    // admission so it is not mistaken for an
                                    // unrelated owner; the waiting branch above
                                    // re-arms it if the takeover is busy.
                                    if (s_wakeForcedNeutralToken[1] != 0) {
                                        ReleaseWakeForcedNeutral(1);
                                    }
                                    const bool started = MacroController::PlayForPlayer(
                                        1, s_p1WakeMacroStartTick, false);
                                    if (started &&
                                        MacroController::GetState() == MacroController::State::Replaying) {
                                        // Publish preserve/queued state atomically
                                        // with verified playback admission (mirror
                                        // of the P2 path); the player latch keeps
                                        // the preserve request bound to P1.
                                        g_macroWakePreservePlayer.store(1, std::memory_order_release);
                                        g_macroWakePreserveBuffer.store(true, std::memory_order_release);
                                        s_p1WakeMacroQueued = true;
                                        s_p1WakeMacroTargetFrame = -1;
                                        s_p1WakeMacroSlot = -1;
                                        s_p1WakeBufferFrozen = true; // Mark as buffered to skip fallback path
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
            // Check for 96→0 transition (must be outside the moveID==96 block!)
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
                // The passive watcher now owns its exact reservation until the
                // source reaches contact/FIC evidence or retires. Clearing this
                // local token is a transfer, not a cancellation or a 22C press.
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
                // Pre-buffer Wakeup is ON but the macro never started during
                // state 96 (target missed or playback owner busy): play it on
                // the first actionable frame instead, the P1 counterpart of
                // P2's 96-exit retry. A busy owner leaves the pre-arm armed so
                // the next tick retries until it expires.
                const int slot = s_p1WakeOptionPicked
                    ? s_p1WakePrePickedOption.macroSlot
                    : triggerOnWakeupMacroSlot.load();
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
                            // Mirror of the P2 wake dash: the scoped
                            // transaction humanizes an AI-flagged P1 dummy
                            // for its producer pass; the legacy queue did not.
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
        
        // Apply the trigger if any condition was met
        if (shouldTrigger) {
            LogFlowSequenceEvent(1, "trigger delay", prevMoveID1, moveID1);
            AutoActionLogScope seqScope("TriggerSequence", 1, triggerType);
            StartTriggerDelay(1, triggerType, actionMoveID, delay);
        }
    }
    
    // P2 triggers
    if ((targetPlayer == 2 || targetPlayer == 3) && !p2DelayState.isDelaying && !p2ActionApplied) {
        bool shouldTrigger = false;
        int triggerType = TRIGGER_NONE;
        int delay = 0;
        short actionMoveID = 0;
        // Extra diagnostics for P2 trigger evaluation
    if (detailedLogging.load() && canLogTrigDiag()) {
            LogOut("[TRIGGER_DIAG] P2 eval: prev=" + std::to_string(prevMoveID2) + 
                       ", curr=" + std::to_string(moveID2) +
                       ", trigActive=" + std::to_string(p2TriggerActive) +
                       ", cooldown=" + std::to_string(p2TriggerCooldown) +
                   ", actionable(prev/curr)=" + std::to_string(IsActionable(prevMoveID2)) + "/" + std::to_string(IsActionable(moveID2)), true);
        }
        
         // Complete After Airtech trigger implementation for P2
    if (!shouldTrigger && triggerAfterAirtechEnabled.load()) {
            // CRITICAL: Don't fire if control restore is pending from previous action
            bool restorePending = g_pendingControlRestore.load();
            // Check for transition from airtech to actionable state
            bool wasAirtech = IsAirtech(prevMoveID2);
            // Post-airtech actionable: allow either general actionable states or explicit FALLING
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
        // Pass visual frames; StartTriggerDelay converts to internal frames
        delay = triggerAfterAirtechDelay.load();
                actionMoveID = GetActionMoveID(triggerAfterAirtechAction.load(), TRIGGER_AFTER_AIRTECH, 2);
            }
        p2_after_airtech_done: ;
        }
    // Simplified P2 After Block trigger
    if (!shouldTrigger && triggerAfterBlockEnabled.load()) {
        // Suppress After Block trigger if a forward dash follow-up is already pending but not yet fired
        bool dashFollowPending = (g_dashDeferred.pendingSel.load() > 0);
        // CRITICAL: Also suppress if a control restore is pending (we're still executing previous action)
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

    // P2: On RG trigger at RG entry (independent of After Block): schedule for RG stun end
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

            // If user delay is 0 and action is special/FM, pre-arm now; ensure P2 control override for buffer progression
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
        
        // After Hitstun trigger
    if (!shouldTrigger && triggerAfterHitstunEnabled.load()) {
            // CRITICAL: Don't fire if control restore is pending from previous action
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
        
        // On Wakeup handling for P2.
    if (detailedLogging.load()) {
    }

    // Dedicated path for pre-armed wake macros (uses wake-buffering timing only).
    // This runs independently of the generic wake pre-arm machinery so that
    // macros never interfere with or depend on the special/hold pre-arm flags.
    // Note: We track the entire groundtech sequence (PRE/START/END/RECOVERY),
    // not just the final GROUNDTECH_RECOVERY state, so that longer macros
    // have enough time to be pre-buffered before the character becomes
    // actionable.
    bool isInGroundtech = IsGroundtech(moveID2);
    const bool wakeTriggerEnabled = triggerOnWakeupEnabled.load();
    const bool wakeBlockEnabled = wakeTriggerEnabled && g_wakeBufferingEnabled.load();
    const bool enteringGroundtechThisFrame =
        wakeTriggerEnabled && isInGroundtech &&
        !IsGroundtech(prevMoveID2);

    // Pick the configured wake option as soon as groundtech starts, regardless
    // of whether macro pre-buffering is enabled.  The generic normal/special
    // wake path consumes this same choice when the character becomes
    // actionable; tying the pick to g_wakeBufferingEnabled silently replaced
    // configured rows with the main wake action whenever macro buffering was
    // disabled.
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
        // Track frames during any groundtech state for potential macro pre-buffering.
        if (enteringGroundtechThisFrame) {
            s_p2WakeMacro96FrameCount = 0;
            s_p2WakeMacroQueued = false;
            s_p2WakeMacroTargetFrame = -1;
            s_p2WakeMacroSlot = -1;
            s_p2WakeMacroStartFrame = -1;
            s_p2WakeMacroStartTick = 0;
            TraceAutoActionWakeEvent("p2-wake-macro-groundtech-enter", 2, prevMoveID2, moveID2);
        } else {
            // Only count ticks when in GROUNDTECH_RECOVERY (state 96), not the
            // earlier groundtech states (97=PRE, 98=START, 99=END). The wakeup
            // timing calculation uses the state 96 duration specifically.
            if (moveID2 == GROUNDTECH_RECOVERY) {
                ++s_p2WakeMacro96FrameCount;
            }
        }

        // Determine macro slot and delay: prefer pre-picked option, fallback to global.
        int macroSlot = s_p2WakeOptionPicked ? s_p2WakePrePickedOption.macroSlot : triggerOnWakeupMacroSlot.load();
        int userDelayF = s_p2WakeOptionPicked ? s_p2WakePrePickedOption.delay : triggerOnWakeupDelay.load();
        // For wake trigger, delay mapping: 0/1 => 0, else (n-1)
        if (s_p2WakeOptionPicked) {
            userDelayF = (userDelayF <= 1) ? 0 : (userDelayF - 1);
        }
        // Only a 0F row is a pre-buffer macro (as on P1); a delayed row must
        // reach the generic On-Wakeup trigger, not the 96-exit fallback.
        s_p2WakeMacroSlot = (userDelayF == 0) ? macroSlot : -1;

        // Only care about macros in this path.
        int macroSel = CLAMP(macroSlot, 1, MacroController::GetSlotCount());
        bool macroSlotSelected = (macroSlot > 0);
        bool macroHasData = macroSlotSelected &&
                            MacroController::GetState() != MacroController::State::Recording &&
                            !MacroController::IsSlotEmpty(macroSel);

        // When wake buffering is enabled and a macro slot is configured with 0F delay,
        // compute a target frame during GROUNDTECH_RECOVERY so that the first attack
        // button lands in the wake buffer window.
        //
        // State 96 (GROUNDTECH_RECOVERY) lasts the full wakeup duration (13-39 visual frames
        // depending on character, as defined in s_wakeupTimings).
        //
        // Strategy: We want the first attack button to be pressed ~3 frames BEFORE actionable
        // so it lands inside the ~10 frame buffer window and executes immediately on wakeup.
        //
        // Formula: playStartFrame = risingFrames - framesToButton - bufferMargin
        // where framesToButton = ceil(firstButtonTick / 3) = how many frames until button in macro
        //       bufferMargin = 3 frames (ensures button lands inside buffer window)
        if (macroHasData && userDelayF == 0) {
            int charID = displayData.p2CharID;
            int risingFrames = GetWakeupRisingFrames(charID); // Visual frames at 64Hz (13-39)
            int firstButtonTick = MacroController::GetFirstButtonTick(macroSel);
            
            if (firstButtonTick < 0) {
                // No attack button in macro, nothing to buffer
                s_p2WakeMacroTargetFrame = -1;
                s_p2WakeMacroStartTick = 0;
            } else {
                // Convert ticks to frames (ceil division to be safe)
                int framesToButton = (firstButtonTick + 2) / 3;
                
                // We want the attack button pressed before actionable (inside buffer window)
                // playStartFrame = when to start the macro during state 96
                // kBufferMargin accounts for the buffer window + timing margin
                constexpr int kBufferMargin = 12;
                int playStartFrame = risingFrames - framesToButton - kBufferMargin;
                if (playStartFrame < 1) playStartFrame = 1;
                
                // For this approach, we play from tick 0 (full macro) at the calculated frame
                // NOTE: Counter increments at 192Hz (ticks), so convert target to ticks
                s_p2WakeMacroStartTick = 0;
                s_p2WakeMacroTargetFrame = playStartFrame * 3; // Convert visual frames to ticks
                
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
            // No valid wake pre-buffer for this 96 cycle
            s_p2WakeMacroTargetFrame = -1;
            s_p2WakeMacroStartTick = 0;
        }

        // Force Neutral while waiting for macro to prevent AI inputs (e.g. holding Down)
        // from causing accidental transitions (e.g. Crouch) before the macro starts.
        if (!s_p2WakeMacroQueued && s_p2WakeMacroSlot > 0 && macroHasData &&
            s_p2WakeMacroTargetFrame >= 0) {
             bool macroPlaying = (MacroController::GetState() == MacroController::State::Replaying);
             if (!macroPlaying && s_wakeForcedNeutralToken[2] == 0) {
                 (void)TryPublishWakeForcedNeutral(2);
             }
        }

        // Macro pre-buffering for P2: if we have a computed target frame,
        // start playback once the counter reaches that frame.
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
                    // Forced neutral is this wake path's own short lease. Drop
                    // it before macro admission so it is not mistaken for an
                    // unrelated owner; the waiting branch will re-arm it if the
                    // takeover is temporarily busy.
                    if (s_wakeForcedNeutralToken[2] != 0) {
                        ReleaseWakeForcedNeutral(2);
                    }
                    const bool started = MacroController::PlayForPlayer(
                        2, s_p2WakeMacroStartTick, false);
                    if (started &&
                        MacroController::GetState() == MacroController::State::Replaying) {
                        // Publish preserve/queued state atomically with verified
                        // playback admission; a rejected macro must not poison a
                        // later unrelated playback.
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

    // Standard wake pre-arm path for specials/holds (non-macro).
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

                // Determine the selected action row (if any) for wake pre-arm.
                int userDelayF = s_p2WakeOptionPicked ? s_p2WakePrePickedOption.delay : triggerOnWakeupDelay.load();
                if (s_p2WakeOptionPicked) {
                    userDelayF = (userDelayF <= 1) ? 0 : (userDelayF - 1);
                }
                int actionType = s_p2WakeOptionPicked ? s_p2WakePrePickedOption.action : triggerOnWakeupAction.load();
                int motionType = ConvertTriggerActionToMotion(actionType, TRIGGER_ON_WAKEUP);

                int macroSlot = s_p2WakeOptionPicked ? s_p2WakePrePickedOption.macroSlot : triggerOnWakeupMacroSlot.load();
                int macroSel = CLAMP(macroSlot, 1, MacroController::GetSlotCount());
                bool macroSlotSelected = (macroSlot > 0);
                bool macroHasData = macroSlotSelected &&
                                    MacroController::GetState() != MacroController::State::Recording &&
                                    !MacroController::IsSlotEmpty(macroSel);
                bool isDash = (motionType == MOTION_FORWARD_DASH || motionType == MOTION_BACK_DASH);
                bool isSpecial = (actionType == ACTION_FINAL_MEMORY) || (motionType >= MOTION_236A && !isDash);
                // Tutorial wake leases own exactly one scripted reversal. Even
                // if the practice GUI changes while that lease is live, it may
                // not attach the user's IC/FIC option to the tutorial action.
                const bool tutorialWakeLeaseActive =
                    g_tutorialP2WakeToken.load(std::memory_order_acquire) != 0;
                const int chargeFollowup = tutorialWakeLeaseActive
                    ? 0
                    : (s_p2WakeOptionPicked
                        ? s_p2WakePrePickedOption.chargeFollowup
                        : triggerOnWakeupCharge.load());

                // If a macro slot is selected for On Wakeup we do NOT want to
                // treat this as a hold/special pre-arm. That case is handled
                // entirely by the dedicated macro pre-buffer path above.
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

        // On Wakeup trigger (fallback when not pre-armed)
        bool p2BecameActionableFromGroundtech = IsGroundtech(prevMoveID2) && IsActionable(moveID2);
        const bool p2LeavingGroundtechThisFrame = IsGroundtech(prevMoveID2) && !IsGroundtech(moveID2);

        // If groundtech recovery ended and we never started the macro in-state,
        // queue it on exit so the wake buffer still fires as early as possible.
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
                    s_p2WakeMacroTargetFrame = -2; // actionable-side retry sentinel
                    LogOut("[AUTO-ACTION][MACRO] P2 wake fallback owner busy; retrying the same slot next tick",
                           true);
                }
            }
        }

        // Cleanup Forced Neutral if we exited 96
        if (s_wakeForcedNeutralToken[2] != 0 &&
            moveID2 != GROUNDTECH_RECOVERY) {
             bool macroPlaying = (MacroController::GetState() == MacroController::State::Replaying);
             if (!macroPlaying) {
                 ReleaseWakeForcedNeutral(2);
             }
        }

        if (s_p2WakePrearmed) {
            // Count logical frames (192Hz) in moveID 96, buffer on last rising frame
            if (moveID2 == GROUNDTECH_RECOVERY) {
                s_p2WakeMoveID96FrameCount++;
                s_p2WakeMacro96FrameCount++;  // Also increment macro frame counter (64Hz equivalent)
                // Get character-specific rising frame count
                int charID = displayData.p2CharID;
                int risingTicks = GetWakeupRisingTicks(charID);
                int bufferFrame = risingTicks - 1;  // Buffer on last frame before actionable
                // Allow 3-frame window for safety (buffer-3 to buffer)
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
            // Check for 96→0 transition (must be outside the moveID==96 block!)
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
            // Diagnostic summary: when we actually become actionable from groundtech,
            // log what wake source we think is active (macro/special/hold/none).
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

            // Admission only means pending.  The wake request completes after
            // the later native slot-4 consumer proves that the move started.
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
                // Transfer the passive watcher to the charge state machine. It
                // still has not submitted 22C; it now waits for source/contact
                // or the authored FIC window independently of wake cleanup.
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
                
                // Enable jump tracking
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
                // Macro was pre-buffered during moveID 96, just clear state
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
            // Don't spam repetitive diagnostics; throttling handled above
        }
    }

    // Post-wake cleanup for wake-prebuffered macros: once playback has
    // finished and the dummy has left any groundtech state, monitor for
    // successful buffered move execution. When we detect moveID transition
    // from 96 (standing after wake) to a bufferable action
    // (attack/walk/dash/jump), we know the buffered move executed
    // successfully. At that point:
    // - If macro has finished: neutralize token immediately
    // - If macro still playing: wait for macro end signal before neutralizing
    // Everything here is keyed off the macro's play player latched at the
    // completion signal, so a P1 dummy (after a control swap) is cleaned up
    // on P1 and the human's P2 lane is never touched.
    {
        const int completedPlayer =
            g_wakeMacroPlaybackCompleted.load(std::memory_order_acquire);
        if (completedPlayer == 1 || completedPlayer == 2) {
            const short completedMoveID =
                (completedPlayer == 1) ? moveID1 : moveID2;
            if (!IsGroundtech(completedMoveID)) {
                if (TryBeginWakeMacroCleanup(completedPlayer)) {
                    // Macro playback finished; start monitoring for buffered
                    // move execution while the cleanup reservation prevents a
                    // newer producer from entering that fighter's command lane.
                    const int kWakeMacroTokenNeutralizeDelay = 20;
                    s_wakeMacroTokenNeutralizeCountdown[completedPlayer] =
                        kWakeMacroTokenNeutralizeDelay;
                    g_wakeMacroPlaybackCompleted.store(
                        0, std::memory_order_release);
                    // Seed tracking with current moveID (normally 96 = standing
                    // after wake).
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

    // Wake macro cleanup countdown: monitor for moveID transition from 96 to bufferable action
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
        // Check if moveID transitioned from 96 (standing) to a bufferable action
        bool moveIDWas96 = (s_lastWakeMoveID[cleanupPlayer] == GROUNDTECH_RECOVERY);
        bool moveIDNowBufferable = IsBufferableAction(cleanupMoveID);

        if (moveIDWas96 && moveIDNowBufferable) {
            // Buffered move executed! Check if macro is still playing
            bool macroStillPlaying = (MacroController::GetState() == MacroController::State::Replaying);

            if (macroStillPlaying) {
                // Defensive race handling: do not hold the cleanup lane across
                // active replay. Release it and let the completion signal arm
                // a fresh cleanup after the macro really becomes idle.
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
                // Single-move macro: buffered move executed and macro finished
                // Clean up immediately
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
            // Update tracking
            if (cleanupMoveID != s_lastWakeMoveID[cleanupPlayer]) {
                s_wakeMoveIDChanged[cleanupPlayer] = true;
                s_lastWakeMoveID[cleanupPlayer] = cleanupMoveID;
            }

            // Decrement countdown
            s_wakeMacroTokenNeutralizeCountdown[cleanupPlayer]--;

            if (s_wakeMacroTokenNeutralizeCountdown[cleanupPlayer] == 0) {
                // Fallback: countdown expired without detecting buffered move
                // This shouldn't happen in normal cases, but clean up anyway
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
    
    // Note: prev handled by caller when using the optimized path
    
    // Process auto control restore at the end of every monitor cycle
    ProcessAutoControlRestore();

    // Now that all moveID-based transitions processed, handle any deferred dash follow-ups
    ProcessDeferredDashFollowups(moveID1, prevMoveID1, moveID2, prevMoveID2);

    MaybeAutoCloseFlowSequence(1, prevMoveID1, moveID1, s_p1WakePrearmed, false);
    // For P2, also treat a queued wake macro as keeping the sequence open.
    MaybeAutoCloseFlowSequence(2, prevMoveID2, moveID2,
                               s_p2WakePrearmed || s_p2WakeMacroQueued,
                               g_pendingControlRestore.load());

    // Cleanup Forced Neutral if P1 exited 96 (mirror of the P2 block).
    // This sits at function level, beside the pre-arm expiry blocks below,
    // instead of inside the P1 trigger gate: that gate stops running the moment
    // the auto-action target moves off P1 (a control swap hands P1 back to the
    // human), and a release that lived inside it would strand
    // g_manualInputOverride[1] with mask 0 - plus a WakeForcedNeutral scoped
    // reservation - on the fighter the user just took control of.
    if (s_wakeForcedNeutralToken[1] != 0 &&
        moveID1 != GROUNDTECH_RECOVERY) {
        const bool macroPlaying =
            (MacroController::GetState() == MacroController::State::Replaying);
        if (!macroPlaying) {
            ReleaseWakeForcedNeutral(1);
        }
    }

    // Clear pre-arm flags when window passes to avoid sticking. Engine ticks: pause,
    // framestep or a hitch during state 96 must not cancel a wake reversal.
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
    // Clear RG pre-arm flags if window passed without execution
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

// Back-compat wrapper using the unified per-frame sample to avoid extra memory reads
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
            if (nowF - s_lastIdleLogFrame >= 5760) { // ~30s at 192Hz
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
    // If caller didn't provide current move id, try to read it for better restore tracking
    if (currentMoveID == 0 && moveIDAddr != 0) {
        SafeReadMemory(moveIDAddr, &currentMoveID, sizeof(short));
    }
    // Get the trigger type from the player's delay state
    int triggerType = (playerNum == 1) ? p1DelayState.triggerType : p2DelayState.triggerType;

    AutoActionLogScope applyScope("ApplyAutoAction", playerNum, triggerType);
    (void)applyScope;

    // Get the appropriate action for this trigger
    int actionType = 0;
    // Prefer row-based chosen action if one was set at arming time
    const TriggerDelayState& dstate = (playerNum == 1) ? p1DelayState : p2DelayState;
    if (dstate.chosenAction >= 0) {
        actionType = dstate.chosenAction;
    } else {
        // Else prefer multi-pool selection if enabled and configured
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
                    actionType = ACTION_5A; // Default to 5A
                    break;
            }
        }
    }

    // If this RG-triggered action was pre-armed, decide from the scoped
    // generation rather than treating queue admission as execution. A failed
    // or cancelled generation falls through and gets one normal dispatch at
    // delay expiry; Pending/Accepted must not be injected twice.
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
    // Keep primary admission and follow-up reservation inside one controller
    // publication barrier.  Every input producer uses this recursive lock, so
    // no airtech/macro/immediate writer can slip into the small interval after
    // the selected move is queued but before its IC/FIC lane is reserved.
    std::unique_lock<std::recursive_mutex> chargeAdmissionLock;
    if (chargeSelection != 0 &&
        ActionSupportsChargeFollowup(actionType)) {
        chargeAdmissionLock =
            std::unique_lock<std::recursive_mutex>(g_p2ControlMutex);
    }

    // Special early handling: Final Memory bypasses motion mapping and injects bespoke pattern
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
                // Both sides queue FM as a scoped transaction; the caller's
                // delay state waits for its generation like any special.
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
        // FM has no ordinary motion fallback. A competing owner is retriable;
        // an unknown command, failed gate, or unavailable world is terminal for
        // this trigger instance.
        return fmSubmit == P2AutoActionMotionSubmitResult::Busy
            ? AutoActionApplyResult::Busy
            : AutoActionApplyResult::Invalid;
    }

    // Determine strength BEFORE converting to motion; strength drives motionType selection
    int resolvedStrength = (dstate.chosenStrength >= 0) ? dstate.chosenStrength : GetSpecialMoveStrength(actionType, triggerType); // 0=A 1=B 2=C 3=D
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
    // This is a two-command, state-gated recipe rather than a single motion:
    // native 44 must enter Kaori's backdash, then native 66 is submitted only
    // during the verified frame-4/5 cancel window.  Keep it out of the generic
    // dash conversion path (where it intentionally maps to MOTION_NONE).
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
    // Convert action to motion type and determine button mask. Use resolvedStrength so row-specific
    // strength overrides (per-option rows) correctly influence which super variant is chosen.
    int motionType = ConvertTriggerActionToMotion(actionType, triggerType, resolvedStrength);
    int buttonMask = 0;
    
    // Determine button mask based on action type
    // Use explicit switch to avoid overlapping ID ranges
    switch (actionType) {
        // Neutral normals
        case ACTION_5A: buttonMask = GAME_INPUT_A; break;
        case ACTION_5B: buttonMask = GAME_INPUT_B; break;
        case ACTION_5C: buttonMask = GAME_INPUT_C; break;
        case ACTION_5D: buttonMask = GAME_INPUT_D; break;
        
        // Crouch normals
        case ACTION_2A: buttonMask = GAME_INPUT_A; break;
        case ACTION_2B: buttonMask = GAME_INPUT_B; break;
        case ACTION_2C: buttonMask = GAME_INPUT_C; break;
        case ACTION_2D: buttonMask = GAME_INPUT_D; break;
        
        // Jump normals
        case ACTION_JA: buttonMask = GAME_INPUT_A; break;
        case ACTION_JB: buttonMask = GAME_INPUT_B; break;
        case ACTION_JC: buttonMask = GAME_INPUT_C; break;
        case ACTION_JD: buttonMask = GAME_INPUT_D; break;
        
        // Forward normals
        case ACTION_6A: buttonMask = GAME_INPUT_A; break;
        case ACTION_6B: buttonMask = GAME_INPUT_B; break;
        case ACTION_6C: buttonMask = GAME_INPUT_C; break;
        case ACTION_6D: buttonMask = GAME_INPUT_D; break;
        
        // Back normals
        case ACTION_4A: buttonMask = GAME_INPUT_A; break;
        case ACTION_4B: buttonMask = GAME_INPUT_B; break;
        case ACTION_4C: buttonMask = GAME_INPUT_C; break;
        case ACTION_4D: buttonMask = GAME_INPUT_D; break;

        // Appended command/dash-normal groups keep their button in strength.
        case ACTION_1X:
        case ACTION_3X:
        case ACTION_J2X:
        case ACTION_J6X:
        case ACTION_66X:
        case ACTION_662X:
        case ACTION_664X:
            buttonMask = (1 << (4 + CLAMP(resolvedStrength, 0, 3)));
            break;
        
        // Special moves - use strength from config / row override
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
            int strength = resolvedStrength; // 0=A 1=B 2=C 3=D
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

    // If buttonMask is zero, use default
    if (buttonMask == 0) {
        LogOut("[AUTO-ACTION] Button mask was 0; defaulting to A (likely non-button action)", true);
        buttonMask = GAME_INPUT_A; // Default to A button (16)
    }
    else {
        LogOut("[AUTO-ACTION] Final buttonMask=" + std::to_string(buttonMask) +
               " (A=16,B=32,C=64,D=128)", true);
    }
    
    bool success = false;
    AutoActionApplyResult applyResult = AutoActionApplyResult::Invalid;
    bool isRegularMove = NormalInputPolicy::IsNormalMotion(motionType);
    bool isSpecialMove = (motionType >= MOTION_236A);
    // Exclude dashes from being treated as specials for restore/control tracking
    if (IsDashRecipeAction(actionType)) {
        isSpecialMove = false;
    }

    // Special handling: Jump must be injected via immediate input register with direction
    if (actionType == ACTION_JUMP) {
        // Determine jump direction: 0=neutral, 1=forward, 2=backward from strength slot
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

        // Build immediate mask: UP plus optional left/right based on facing and dir
        uint8_t mask = GAME_INPUT_UP;
        if (dir == 1 || dir == 2) {
            bool facingRight = GetPlayerFacingDirection(playerNum);
            if (dir == 1) {
                // forward
                mask |= (facingRight ? GAME_INPUT_RIGHT : GAME_INPUT_LEFT);
            } else if (dir == 2) {
                // backward
                mask |= (facingRight ? GAME_INPUT_LEFT : GAME_INPUT_RIGHT);
            }
        }

    LogOut("[AUTO-ACTION] Injecting Jump via immediate writer (dir=" + std::to_string(dir) + ")", true);
    success = ImmediateInput::PressFor(playerNum, mask, 3);
    // Jump is transient only after its immediate lane actually accepted the
    // press. A Busy result retains the authored trigger for the next tick.
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
    // No control override needed for jumps
    }

    // Block holds back through the immediate writer, as the wake path does; it has no motion.
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

    // Special handling: Dashes should be executed by writing to the buffer via the queue
    if (!success && IsDashRecipeAction(actionType)) {
        LogOut("[AUTO-ACTION] Queuing dash motion via native buffer transaction (" + GetMotionTypeName(motionType) + ")", true);
        g_injectImmediateOnly[playerNum].store(false);
        bool isForwardDash = actionType != ACTION_BACKDASH &&
                             actionType != ACTION_BACK_DASH;
        uint64_t dashMotionGeneration = 0;
        // Both fighters use the generation-owned transaction. It humanizes an
        // AI-flagged target for exactly one producer pass, which the legacy
        // P1 queue never did: after a control swap the dummy is P1 and EFZ's
        // AI branch never scans the ring, so a queued dash silently never
        // came out.
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
                // Always treat as dash-normal; the dashMode toggle is ignored.
                ScheduleForwardDashFollowup(
                    playerNum, fdf, true, dashMotionGeneration);
            }
        } else if (success && actionType == ACTION_BACKDASH) {
            // Forward Dash is the only UI option with a configured follow-up.
            // Reusing that global for Backdash would inject an unconfigured normal.
        }
    }
    
    if (!success && isRegularMove) {
        // Keep the fighter's current controller mode. Human/tutorial fighters
        // use EFZ's native poll; AI fighters run their original producer first,
        // then receive the requested pulse before EFZ's character consumer.
        // The pulse remains owned until that consumer confirms the normal and
        // a following neutral pass is delivered.
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
        // Same generation-owned transaction for both fighters (see the dash
        // path above); the legacy P1 buffer freeze never humanized an
        // AI-flagged P1 dummy.
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

    // (Deferred dash follow-ups are processed centrally each frame)

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
        // Scoped submissions restore only their own captured AI snapshot. A
        // Busy result may belong to an existing macro/tutorial legacy owner;
        // this caller did not acquire that lease and must never release it.
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

    // The tutorial lease supersedes an in-flight auto normal. Cancellation is
    // serialized through the normal consumer lock before the AI flag changes,
    // so an AI post-write cannot race this new native owner.
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
    // A scoped wake normal may still own P2's native/raw release transaction.
    // End that transaction before restoring the tutorial's saved controller.
    // Netplay suspend already drained offline input owners before publishing
    // online mode; teardown after that point must not write local controller
    // state back over netplay's human-control setup.
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
    // The native-input participant already restored command/token/raw ownership.
    return RestoreLegacyP2ControllerLocked("RetirePracticeInput", false, true, &world)
        ? EFZ_TM_READY : EFZ_TM_PENDING;
}

// Enable P2 human control for the legacy long-lived macro owner and save its
// original state. Scoped auto-action motions use the transaction API instead.
void EnableP2ControlForAutoAction() {
    // This legacy long-lived owner is used only by macro playback/recording.
    // A macro has priority over a short auto-action motion, but takeover must
    // synchronously retire the older token and its authored history first.
    std::lock_guard<std::recursive_mutex> lk(g_p2ControlMutex);
    CancelP2AutoActionMotionTransaction("legacy macro owner takeover");
    if (IsP2AutoActionMotionTransactionActive()) return;
    if (g_tutorialP2Token.load(std::memory_order_acquire) != 0) return;
    // Macro playback is a higher-priority long-lived input owner. Finish any
    // normal hand-off before switching the producer branch.
    CancelAutoActionNormalPulse(2);
    // CRITICAL: Never modify game state during online mode
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
    
    // IMPORTANT: Always verify the ACTUAL control flag value
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

    // Save the exact world/controller state only on the first acquisition.
    if (!alreadyOwned) {
        g_legacyP2ControlSnapshot = LegacyP2ControlSnapshot{
            true, gameStatePtr, p2CharPtr, currentCpuFlag, currentAIFlag};
        g_originalP2ControlFlag = currentAIFlag;
        LogOut("[AUTO-ACTION] Saving original P2 control flags: cpu=" +
                   std::to_string(currentCpuFlag) + " ai=" +
                   std::to_string(currentAIFlag),
               true);
    } else if (currentAIFlag != 0) {
        // If flag was reset by the game, log it
        LogOut("[AUTO-ACTION] P2 control flag was reset to " + std::to_string(currentAIFlag) + ", setting back to human control", true);
    }
    
    // Always set to human control (0) regardless of our tracking variable, with audit logging
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

// Full restore for the legacy long-lived P2 controller owner. This retains the
// whole-buffer cleanup expected by macro playback. Scoped dash/special/FM
// transactions restore and scrub their own generation at the native consumer
// boundary and must never depend on this function.
void RestoreP2ControlState() {
    std::lock_guard<std::recursive_mutex> lk(g_p2ControlMutex);
    CancelP2AutoActionMotionTransaction("legacy control restore");
    if (IsP2AutoActionMotionTransactionActive()) return;
    if (g_tutorialP2Token.load(std::memory_order_acquire) != 0) return;
    // Dash follow-ups use the already-human native route. Restoring AI or
    // clearing +392..397 before its neutral pass would cut off the normal, so
    // leave the existing pending-restore monitor armed and retry afterward.
    if (IsAutoActionNormalPulseActive(2)) {
        LogOut("[AUTO-ACTION] Deferred P2 control restore until normal pulse completes",
               detailedLogging.load());
        return;
    }

    if (g_p2ControlOverridden.load(std::memory_order_acquire)) {
        const PerFrameSample &restoreSample = GetCurrentPerFrameSample();
        short restoreMoveID2 = restoreSample.moveID2;
        // Make sure to stop any buffer freezing when restoring control
        StopBufferFreezing();
        
        // IMPORTANT: Force a longer cooldown period to prevent immediate re-triggering
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

// Partial restore for Counter RG: restores the CPU flag (gameStatePtr + 4931)
// and AI flag (p2CharPtr + 0x42C) but preserves the input buffer and motion
// token. Use only where buffered inputs must survive (Counter RG fast restore,
// wake macros); dashes, regular specials/supers and normals use
// RestoreP2ControlState, which clears the buffer.
static void RestoreP2ControlFlagOnly() {
    std::lock_guard<std::recursive_mutex> lk(g_p2ControlMutex);
    if (g_tutorialP2Token.load(std::memory_order_acquire) != 0) return;
    if (!g_p2ControlOverridden.load(std::memory_order_acquire)) return;
    const PerFrameSample &restoreSample = GetCurrentPerFrameSample();
    short restoreMoveID2 = restoreSample.moveID2;
    // Preserve history, but neutralize a half-parsed token before restoring the
    // exact controller flags captured at takeover.
    if (RestoreLegacyP2ControllerLocked("RestoreP2ControlFlagOnly",
                                        false, false)) {
        EndFlowSequence(2, "ControlFlagOnly", restoreMoveID2);
    }
}

// Specialized helper for wake macros: restore control flags but
// explicitly preserve both the input buffer and motion token so
// pre-buffered motions (e.g. DP) can still be recognized on wake.
void RestoreP2ControlFlagsPreserveBufferAndTokenForMacro() {
    std::lock_guard<std::recursive_mutex> lk(g_p2ControlMutex);
    if (g_tutorialP2Token.load(std::memory_order_acquire) != 0) return;
    if (!g_p2ControlOverridden.load(std::memory_order_acquire)) return;
    const PerFrameSample &restoreSample = GetCurrentPerFrameSample();
    short restoreMoveID2 = restoreSample.moveID2;
    // Wake playback intentionally keeps both history and token; only the exact
    // controller snapshot is restored.
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

    // CRITICAL: Never process control restoration during online mode
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
        // Likely transitioning to character select / menu; invalidate caches
        InvalidateCachedCharacterIDs("phase change");
        if (g_pendingControlRestore.load()) {
            LogOut("[AUTO-ACTION] Phase left MATCH during restore; forcing cleanup", true);
            RestoreP2ControlState();
            // RestoreP2ControlState keeps the obligation live on transient
            // read/write failure and clears it only after verification or a
            // proven replacement world.
            g_restoreGraceCounter = 0;
        }
        return;
    }

    // If a macro just ended and we still have human override but no restore pending,
    // force a cleanup to avoid lingering override.
    if (MacroController::GetState() == MacroController::State::Idle &&
        g_p2ControlOverridden && !g_pendingControlRestore.load() &&
        !(g_bufferFreezingActive.load() && g_activeFreezePlayer.load() == 2)) {
        LogOut("[AUTO-ACTION][MACRO] Macro inactive but override present; forcing control restore", true);
        RestoreP2ControlState();
        g_restoreGraceCounter = RESTORE_GRACE_PERIOD;
    }
    
    // CRITICAL: Handle grace period countdown. This prevents triggers from firing
    // immediately after control restore, giving character time to stabilize.
    if (g_restoreGraceCounter > 0) {
        g_restoreGraceCounter--;
        if (g_restoreGraceCounter == 0) {
            LogOut("[AUTO-ACTION] Grace period expired, clearing g_pendingControlRestore", true);
            g_pendingControlRestore.store(false);
        }
        // Don't return yet - still process restore logic in case of new interrupts
    }
    
    if (g_pendingControlRestore.load()) {
        // A failed long-lived macro restore carries an exact world-bound
        // snapshot. Retry that obligation before the older move-heuristic
        // restore code can clear its marker as "stale."
        if (g_p2ControlOverridden.load(std::memory_order_acquire) &&
            g_legacyP2ControlSnapshot.valid &&
            g_tutorialP2Token.load(std::memory_order_acquire) == 0 &&
            MacroController::GetState() == MacroController::State::Idle) {
            RestoreP2ControlState();
            return;
        }

        // If there's no active override and no active buffer-freeze for P2,
        // treat this as a stale pending state and clear it without restoring.
        // This prevents redundant restores that would re-neutralize inputs
        // when AI control is already in effect.
        if (!g_p2ControlOverridden && !(g_bufferFreezingActive.load() && g_activeFreezePlayer.load() == 2)) {
            if (detailedLogging.load()) {
                LogOut("[AUTO-ACTION] Clearing stale g_pendingControlRestore (no override/buffer-freeze)", true);
            }
            g_pendingControlRestore.store(false);
            return;
        }

        // Use unified per-frame sample instead of raw address caching for MOVE_ID
        static int s_throttle = 0;
        static short s_prevMoveID2 = 0; // track previous for dash entry detection
        // Kaori's forward dash start (ID 250) is extremely short-lived (often 1 frame);
        // sample every frame for Kaori until dash start is detected. The throttle decision
        // happens below because the character is unknown until base & cached IDs resolve.
        bool deferThrottleDecision = true; // placeholder flag so later logic can decide to early-return

        uintptr_t base = GetEFZBase();
        if (!base) return;
        const PerFrameSample &dashSample = GetCurrentPerFrameSample();
        short moveID2 = dashSample.moveID2;
        short oppMoveID = dashSample.moveID1;

        // Ensure cached character IDs (once per MATCH phase) BEFORE deciding on throttle
        EnsureCachedCharacterIDs(base);
        bool isKaori = (g_cachedP2CharID == CHAR_ID_KAORI);

        if (!g_crgFastRestore.load()) {
            // While a dash was just queued and we're waiting for dash start, sample every frame to avoid missing
            // short-lived start IDs (e.g., immediate transitions). Kaori also remains unthrottled.
            bool waitingDash = g_recentDashQueued.load();
            if (!waitingDash) {
                // Only apply throttle if NOT Kaori. Kaori remains unthrottled due to ID 250 being very brief.
                if (!isKaori) {
                    if ((s_throttle++ & 1) != 0) return; // 96 Hz sampling for non-Kaori normal path
                } else if ((s_throttle % 192) == 0 && detailedLogging.load()) {
                    LogOut("[AUTO-ACTION][DASH][KAORI] Full-rate sampling active (disabling throttle to catch ID 250)", true);
                }
            }
            if (ValidationMetricsEnabled() && g_recentDashQueued.load()) { GetValidationMetrics().dashQueued++; }
        }

        // DASH RESTORE RULES:
        // 1. If we just entered forward/back dash start and no follow-up configured -> immediate restore
        // 2. If any normal (>=200) starts after a dash-follow sequence -> restore
        // 3. If dash cancelled into blockstun/hitstun/airtech -> restore
        // 4. Fallback to existing timeout / move completion logic
    // isKaori already computed above
    const bool dashStartNow =
        moveID2 == GROUND_FORWARD_DASH_ID || moveID2 == GROUND_BACKWARD_DASH_ID ||
        moveID2 == AIR_FORWARD_DASH_ID || moveID2 == AIR_BACKWARD_DASH_ID ||
        (isKaori && moveID2 == KAORI_FORWARD_DASH_START_ID);
    bool dashStartJustEntered = dashStartNow && (s_prevMoveID2 != moveID2);
    if (ValidationMetricsEnabled() && dashStartJustEntered) { GetValidationMetrics().dashStartDetected++; }
    // Treat Kaori's forward dash start (250) as a dash state, NOT a normal. Only consider a "dash normal" when the new move
    // ID is a normal/special (>=200) AND it is not part of any dash start/recovery sequence.
    bool dashNormalStarted = (moveID2 >= 200);
    bool moveIsDash = dashStartNow ||
               moveID2 == FORWARD_DASH_RECOVERY_SENTINEL_ID;
    // Recompute dashNormalStarted excluding any dash state (prevents Kaori 250 from being misclassified)
    if (dashNormalStarted && moveIsDash) dashNormalStarted = false;
    bool dashCancelled = (!moveIsDash) && (IsBlockstun(moveID2) || IsHitstun(moveID2) || IsAirtech(moveID2));
        // HARD GUARD (informational): If we just queued a dash and have NOT yet observed the dash start ID
    // (163 forward, 165 back, or 250 Kaori), we will suppress generic restore later. Keep timeout >=90.
    bool waitingDashStart = g_recentDashQueued.load() && !dashStartNow;
    if (waitingDashStart && detailedLogging.load()) {
        LogOut("[AUTO-ACTION][DASH] Holding restore (pre-start): queuedDash & dashStart not yet observed", true);
    }
    
        // CRITICAL: If we queued a dash and got INTERRUPTED BEFORE any dash start ID (163/165/250) appears,
        // immediately restore control and clear the queued-dash guard. This avoids an infinite pre-start hold
        // when we are put into blockstun/hitstun/airtech or other non-dash states by the opponent.
        if (g_recentDashQueued.load() && !dashStartNow && dashCancelled) {
            LogOut("[AUTO-ACTION][DASH] Pre-start interrupted (hit/block/airtech) - cancelling dash and restoring control", true);
            if (ValidationMetricsEnabled()) { GetValidationMetrics().dashPreStartInterrupts++; }
            // Cancel any pending follow-up as it will never fire now
            if (g_dashDeferred.pendingSel.load() > 0) {
                g_dashDeferred.pendingSel.store(0);
                g_dashDeferred.dashStartLatched.store(-1);
            }
            g_recentDashQueued.store(false);
            RestoreP2ControlState();
            if (ValidationMetricsEnabled()) { GetValidationMetrics().dashRestoreEvents++; }
            // Allow immediate re-arming on the next actionable window (second hit scenario):
            // clear trigger active/cooldown so After Block/Hitstun can fire even if the gap is small (3-4F).
            p2TriggerActive = false;
            p2TriggerCooldown = 0;
            g_restoreGraceCounter = RESTORE_GRACE_PERIOD;
            g_crgFastRestore.store(false);
            g_lastP2MoveID.store(-1);
            // Clear pending restore so we don't keep sampling in a dead loop
            g_pendingControlRestore.store(false);
            s_prevMoveID2 = moveID2;
            return;
        }
        // If we had only scheduled a follow-up but no longer waiting a queued dash, clear follow-up quietly.
        if (g_dashDeferred.pendingSel.load() > 0 && dashCancelled && !g_recentDashQueued.load()) {
            LogOut("[AUTO-ACTION][DASH] Dash follow-up cancelled by interrupt before dash started", true);
            if (ValidationMetricsEnabled()) { GetValidationMetrics().dashFollowupCancelled++; }
            g_dashDeferred.pendingSel.store(0);
            g_dashDeferred.dashStartLatched.store(-1);
        }
    int fdfSel = forwardDashFollowup.load();
        // Don't restore immediately when dash starts (163/165).
        // Wait until we've LEFT the dash state entirely, regardless of what state we transition to:
        // - Natural completion: 163 → 0/1/2 (idle/walk)
        // - Dash normal: 163 → attack moveID (200+)
        // - Interrupted by hit: 163 → hitstun (50-71)
        // - Canceled into special: 163 → special moveID
    // Consider both start and recovery IDs as "dashing" so we can detect leaving the entire dash state cleanly
    bool wasDashing = (s_prevMoveID2 == GROUND_FORWARD_DASH_ID ||
                       s_prevMoveID2 == GROUND_BACKWARD_DASH_ID ||
                       s_prevMoveID2 == AIR_FORWARD_DASH_ID ||
                       s_prevMoveID2 == AIR_BACKWARD_DASH_ID ||
                       s_prevMoveID2 == FORWARD_DASH_RECOVERY_SENTINEL_ID ||
                       (isKaori && s_prevMoveID2 == KAORI_FORWARD_DASH_START_ID));
    bool stillDashing = moveIsDash;
        // Track dash start observation per attempt to avoid stale prev-move causing false "left dash" restores
        static bool s_dashStartSeenThisAttempt = false;
        // If a new dash attempt was just queued (tracked below), we'll reset this flag
        if (dashStartJustEntered) {
            s_dashStartSeenThisAttempt = true;
        }
        bool leftDashState = s_dashStartSeenThisAttempt && wasDashing && !stillDashing;
        
        bool doDashRestore = false;
        
        // PRIORITY 1: If we left the dash state entirely (163/165 → anything else), restore immediately.
        // This handles all transitions: natural completion, dash normals, cancels, interrupts, etc.
        if (leftDashState && !waitingDashStart) {
            LogOut("[AUTO-ACTION][DASH] Restore: left dash state (163/165/250 → " + std::to_string(moveID2) + ")", true);
            doDashRestore = true;
        }
        // PRIORITY 2: Legacy paths for edge cases
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
            // Per-frame debounce to avoid multiple restores & log spam when logic re-enters in the same frame
            static int s_lastDashRestoreFrame = -1;
            int curF = frameCounter.load();
            if (s_lastDashRestoreFrame != curF) {
                RestoreP2ControlState();
                s_lastDashRestoreFrame = curF;
                // Clear any leftover dash queued flag so we don't immediately re-arm
                g_recentDashQueued.store(false);
            }
            // Grace period disabled; clear pending immediately after restore.
            g_restoreGraceCounter = RESTORE_GRACE_PERIOD;
            g_pendingControlRestore.store(false);
            g_crgFastRestore.store(false);
            g_lastP2MoveID.store(-1);
            s_prevMoveID2 = moveID2; // update before return
            return;
        }

        // PRE-DASH START GUARD: if we queued a forward dash but haven't seen any non-zero move yet, delay restore
        static bool sawNonZeroMoveID = false; // Tracks if ANY non-zero move id observed during the CURRENT pending restore attempt
        // Tag dash attempts by the frame they were queued so stale state from a previous attempt
        // (where we already saw a non-zero move) cannot leak into a fresh attempt and cause an
        // immediate generic restore (moveChangedCandidate) before the dash even starts.
        static int s_lastDashQueueFrame = -1;
        int dashQueueFrame = g_recentDashQueuedFrame.load();
        if (g_recentDashQueued.load() && dashQueueFrame != s_lastDashQueueFrame) {
            // New dash attempt detected -> reset observation flag
            sawNonZeroMoveID = false;
            s_lastDashQueueFrame = dashQueueFrame;
            s_dashStartSeenThisAttempt = false;
            if (detailedLogging.load()) {
                LogOut("[AUTO-ACTION][DASH] New dash attempt detected; resetting sawNonZeroMoveID", true);
            }
        }
        // No timeout-based fallback: rely on dash age and state only
    if (g_recentDashQueued.load() && moveID2 == 0 && !sawNonZeroMoveID) {
            int age = frameCounter.load() - g_recentDashQueuedFrame.load();
            const MotionQueueSnapshot queue = GetMotionQueueSnapshot(2);
            int qSize = static_cast<int>(queue.size);
            // Throttle noisy trace logs behind detailedLogging to reduce idle overhead
            if (detailedLogging.load()) {
                static int s_nextDashTraceLogFrame = 0; // ~8 logs/sec at 192Hz
                int nowF = frameCounter.load();
                if (nowF >= s_nextDashTraceLogFrame) {
                    s_nextDashTraceLogFrame = nowF + 24; // 24 internal frames ~125ms
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
                s_prevMoveID2 = moveID2; return; // hold restore
            } else if (age == 120) {
                // Requeue the same dash type that was attempted (forward or back)
                bool wasBack = (queue.motionType == MOTION_BACK_DASH);
                LogOut(std::string("[AUTO-ACTION][DASH] Requeue ") + (wasBack?"backdash":"forward dash") + " after 120f no-start", true);
                QueueMotionInput(2, wasBack ? MOTION_BACK_DASH : MOTION_FORWARD_DASH, 0);
                g_recentDashQueuedFrame.store(frameCounter.load());
                s_prevMoveID2 = moveID2; return;
            } else if (age < 160) {
                s_prevMoveID2 = moveID2; return; // continue holding
            } else {
                LogOut("[AUTO-ACTION][DASH] Abandon wait; dash never produced non-zero moveID", true);
                g_recentDashQueued.store(false);
            }
        }
        // Do not add extra pre-start requeue/abandon paths here.

        // No timeout countdown; rely on move/state transitions only

        if (moveID2 > 0) {
            sawNonZeroMoveID = true;
            // Only clear the queued-dash guard once we actually see a dash start ID.
            if (dashStartNow) {
                g_recentDashQueued.store(false);
            }
        }

        // Counter RG fast-restore: if opponent enters attack frames (>=200) AND we are actionable,
        // restore immediately with control-flag-only to preserve the buffered special. Fallback: if
        // our move starts (non-RG state), perform full restore.
        if (g_crgFastRestore.load() && g_counterRGEnabled.load()) {
            bool opponentAttacking = (oppMoveID >= 200);
            bool moveStarted = (moveID2 > 0 && moveID2 != RG_STAND_ID && moveID2 != RG_CROUCH_ID && moveID2 != RG_AIR_ID);
            bool p2ActionableNow = IsActionable(moveID2);
            bool isRGPrearmedSpecial = s_p2RGPrearmIsSpecial; // true when we pre-armed a special at RG entry
            if (opponentAttacking && p2ActionableNow) {
                if (!isRGPrearmedSpecial) {
                    LogOut("[AUTO-ACTION][CRG] Early restore: opponent entered attack frames", true);
                    RestoreP2ControlFlagOnly();
                    // Grace period disabled; clear pending immediately.
                    g_restoreGraceCounter = RESTORE_GRACE_PERIOD;
                    g_pendingControlRestore.store(false);
                    g_crgFastRestore.store(false);
                    g_lastP2MoveID.store(-1);
                    sawNonZeroMoveID = false;
                    return;
                } else {
                    // For specials, defer restore until the move actually starts to avoid losing the final button press
                    if (detailedLogging.load()) {
                        LogOut("[AUTO-ACTION][CRG] Opponent attacking, but special pre-armed: deferring restore until move starts", true);
                    }
                }
            }
            if (moveStarted) {
                LogOut("[AUTO-ACTION][CRG] Early restore: special started after RG", true);
                RestoreP2ControlState();
                // Grace period disabled; clear pending immediately.
                g_restoreGraceCounter = RESTORE_GRACE_PERIOD;
                g_pendingControlRestore.store(false);
                g_crgFastRestore.store(false);
                g_lastP2MoveID.store(-1);
                sawNonZeroMoveID = false;
                p2TriggerCooldown = 0; p2TriggerActive = false;
                return;
            }
        }

    // A dash-specific guard: if we queued a forward/back dash and never observed ANY non-zero move id yet,
    // treat the transition back to 0 as non-completion (prevents premature restore destroying dash attempt).
    // Treat any queued-dash prior to actual dash start as a no-restore condition,
    // even if the engine briefly shows non-zero idle/actionable states.
    bool queuedDashNoStart = waitingDashStart;
    // Also suppress generic restore while the dash motion queue is actively writing frames.
    bool dashQueueActiveNow = GetMotionQueueSnapshot(2).active;
    bool moveChangedCandidate = (moveID2 != g_lastP2MoveID.load() && moveID2 == 0 && sawNonZeroMoveID);
    bool dashFollowPending = (g_dashDeferred.pendingSel.load() > 0);
    bool suppressGenericRestore = (moveChangedCandidate && dashFollowPending);
        if (suppressGenericRestore) {
            // Diagnostic: show we deliberately ignored a generic completion while a dash follow-up is pending
            LogOut("[AUTO-ACTION][DASH] Suppressing generic restore (pending follow-up active)", detailedLogging.load());
        }
        bool moveChanged = moveChangedCandidate && !suppressGenericRestore;
    // No timeout path; only state-change driven restores
        
        // If pending restore flag is set but we are no longer actually overriding control (and not in grace),
        // clear it defensively to avoid indefinite sampling spam.
        if (g_pendingControlRestore.load() && !g_p2ControlOverridden && g_restoreGraceCounter == 0 &&
            !dashFollowPending && !g_recentDashQueued.load()) {
            LogOut("[AUTO-ACTION] Clearing stale g_pendingControlRestore (no override active)", detailedLogging.load());
            g_pendingControlRestore.store(false);
            s_prevMoveID2 = moveID2;
            return;
        }

        // PRE-START CANCEL FALLBACK (covers Kaori and brief dash-start cases):
        // If we queued a dash, the queue is now inactive, and we've seen a sustained non-dash, non-zero state
        // for a short window without ever observing a dash start ID, cancel and restore to avoid indefinite hold.
        if (waitingDashStart && !dashQueueActiveNow && moveID2 != 0 && !moveIsDash) {
            int ageSinceQueue = frameCounter.load() - g_recentDashQueuedFrame.load();
            if (ageSinceQueue >= 12) { // ~1/6 sec at 192 Hz; enough to rule out 1F start IDs
                LogOut("[AUTO-ACTION][DASH] Pre-start cancelled (no start observed, non-dash state persisted) - restoring", true);
                // Cancel any pending follow-up as it will never fire now
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
                // Briefly hold to see if dash start appears
                s_prevMoveID2 = moveID2;
                return;
            }
        }

        // Early restore: as soon as the move actually starts (first non-zero, non-RG, non-dash state), restore immediately.
        // This covers specials/supers even if their first startup IDs are outside the >=200 attack range.
        bool restorePendingNow = g_pendingControlRestore.load();
        // Only allow early restore when we actually overrode control or P2 is in an active freeze.
        bool allowEarlyRestore = (g_p2ControlOverridden || (g_bufferFreezingActive.load() && g_activeFreezePlayer.load() == 2));
        // keep trackable move only for diagnostics; don't use it to authorize restore
        bool haveTrackableMove = (g_lastP2MoveID.load() != -1);
        bool nonRGState = (moveID2 != RG_STAND_ID && moveID2 != RG_CROUCH_ID && moveID2 != RG_AIR_ID);
        // Early-restore only when the move becomes ACTIVE:
        // - Specials: first active frame assumed at moveID >= 250
        // - Supers:   first active frame assumed at moveID >= 300
        // Dashes are handled elsewhere (dash start IDs), so keep excluding dash here.
        bool specialActiveNow = (moveID2 >= 250 && moveID2 < 300);
        bool superActiveNow   = (moveID2 >= 300);
        bool activeNow = (specialActiveNow || superActiveNow) && nonRGState && !moveIsDash;
        bool stateEnteredNow = (s_prevMoveID2 != moveID2);
        // Tighten gating: require actual override or active freeze to avoid spurious restores
        if (restorePendingNow && allowEarlyRestore && stateEnteredNow && activeNow &&
            !dashFollowPending && !queuedDashNoStart && !g_recentDashQueued.load() && !dashQueueActiveNow) {
            LogOut("[AUTO-ACTION] Early restore on active frame (special>=250|super>=300) moveID=" + std::to_string(moveID2), true);
            RestoreP2ControlState();
            g_restoreGraceCounter = RESTORE_GRACE_PERIOD;
            g_crgFastRestore.store(false);
            g_lastP2MoveID.store(-1);
            sawNonZeroMoveID = false;
            s_prevMoveID2 = moveID2;
            // Grace disabled; pending restore no longer needed.
            g_pendingControlRestore.store(false);
            g_pendingRestoreTimestamp.store(0);
            p2TriggerCooldown = 0; p2TriggerActive = false;
            return;
        }

        // Fallback: if we still have a pending restore and human override, and the buffer-freeze is no longer
        // active for P2 (owner released), perform an immediate restore. This covers cases where the active-frame
        // early-restore window was missed while the freeze thread was still pushing frames, and avoids waiting
        // for moveID to return to 0 (which we no longer use as a restore signal).
        // Grace period: Don't trigger fallback restore within 20ms of setting g_pendingControlRestore to allow
        // buffer freeze thread time to initialize and execute the motion.
        bool freezeActiveForP2 = (g_bufferFreezingActive.load() && g_activeFreezePlayer.load() == 2);
        DWORD restoreTimestamp = g_pendingRestoreTimestamp.load();
        DWORD currentTime = GetTickCount();
        bool gracePeriodElapsed = (restoreTimestamp == 0) || ((currentTime - restoreTimestamp) >= 20);
        if (g_pendingControlRestore.load() && g_p2ControlOverridden && !freezeActiveForP2 && !dashQueueActiveNow && gracePeriodElapsed) {
            LogOut("[AUTO-ACTION] Fallback restore: freeze ended, pending restore active", true);
            RestoreP2ControlState();
            g_restoreGraceCounter = RESTORE_GRACE_PERIOD; // 0, kept for symmetry/logging
            g_crgFastRestore.store(false);
            g_lastP2MoveID.store(-1);
            g_pendingControlRestore.store(false);
            g_pendingRestoreTimestamp.store(0);
            sawNonZeroMoveID = false;
            p2TriggerCooldown = 0; p2TriggerActive = false;
            s_prevMoveID2 = moveID2;
            return;
        }

        // Generic restore: require pending flag + valid context to avoid repeated restores when already clean.
        restorePendingNow = g_pendingControlRestore.load();
        haveTrackableMove = (g_lastP2MoveID.load() != -1);
    static int s_lastGenericRestoreFrame = -1; // dedupe generic restore per frame
    bool allowAnyRestore = (g_p2ControlOverridden || (g_bufferFreezingActive.load() && g_activeFreezePlayer.load() == 2));
    // Never use generic restore when the move returns to 0; we restore earlier on active frames or via dash/CRG paths.
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
            // Grace disabled; clear pending immediately.
            g_restoreGraceCounter = RESTORE_GRACE_PERIOD;
            g_pendingControlRestore.store(false);
            g_pendingRestoreTimestamp.store(0);
            g_crgFastRestore.store(false);
            g_lastP2MoveID.store(-1);
            sawNonZeroMoveID = false;
            s_lastGenericRestoreFrame = frameCounter.load();

            // If we just restored after a dash and a forward dash follow-up is still pending,
            // keep a minimal cooldown (1 *internal* frame => ~instant visually) so we do not
            // immediately retrigger the same After Block while the dash follow-up logic runs.
            p2TriggerCooldown = 0; // allow prompt re-trigger after restoring
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
        s_prevMoveID2 = moveID2; // remember last sampled
    }
}

bool AutoGuard(int playerNum, int opponentPtr) {
    uintptr_t playerPtr = GetPlayerPointer(playerNum);
    if (!playerPtr || !opponentPtr)
        return false;
        
    // Get opponent's current move ID
    short moveID = 0;
    if (!SafeReadMemory(opponentPtr + 0x8, &moveID, sizeof(short)) || moveID <= 0)
        return false;
        
    // Log what move we're trying to block
    LogOut("[AUTO_GUARD] Attempting to block move ID: " + std::to_string(moveID), true);
    
    // Get attack height (disabled AttackReader use: assume mid as a safe default)
    AttackHeight height = ATTACK_HEIGHT_MID;
    
    // Check if in air
    double yPos = 0.0;
    SafeReadMemory(playerPtr + 40, &yPos, sizeof(double));
    bool inAir = (yPos < 0.0);
    
    // Determine block stance
    bool playerFacingRight = GetPlayerFacingDirection(playerNum);
    uint8_t blockInput = 0;
    
    LogOut("[AUTO_GUARD] Attack height: " + std::to_string(height) + 
           ", Player in air: " + (inAir ? "yes" : "no"), true);
    
    switch (height) {
        case ATTACK_HEIGHT_LOW:
            // Must crouch block
            blockInput = GAME_INPUT_DOWN | (playerFacingRight ? GAME_INPUT_LEFT : GAME_INPUT_RIGHT);
            LogOut("[AUTO_GUARD] Using crouch block for low attack", true);
            break;
            
        case ATTACK_HEIGHT_HIGH:
            if (inAir) {
                // Air block
                blockInput = playerFacingRight ? GAME_INPUT_LEFT : GAME_INPUT_RIGHT;
                LogOut("[AUTO_GUARD] Using air block for high attack", true);
            } else {
                // Stand block
                blockInput = playerFacingRight ? GAME_INPUT_LEFT : GAME_INPUT_RIGHT;
                LogOut("[AUTO_GUARD] Using stand block for high attack", true);
            }
            break;
            
        case ATTACK_HEIGHT_MID:
            if (inAir) {
                // Air block
                blockInput = playerFacingRight ? GAME_INPUT_LEFT : GAME_INPUT_RIGHT;
                LogOut("[AUTO_GUARD] Using air block for mid attack", true);
            } else {
                // Either stand or crouch block works, prefer crouch
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
    
    // Apply block input
    return WritePlayerInput(playerPtr, blockInput);
}

// Scoped cleanup for a tutorial-owned native P2 wake producer. Unlike the
// general cancel path below, this never alters P1's poll override or macro
// recording/playback state.
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

    // Retire the native P2 producer without touching P1 poll overrides,
    // macro recording/playback, or the user's trigger configuration.
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
    // Tutorial-owned P2 cleanup: this scoped path never alters P1 state.
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

    // Exactly one deterministic trigger is allowed during this lease.
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
    // Tutorial wake scripts own only the reversal.  They must never inherit a
    // user's practice-mode IC/FIC follow-up and submit 22C after that move.
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
    // Each field is restored only while it still has the exact value applied
    // by this lease, so a concurrent GUI/config edit wins for that field.
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
        // Use UnswapThenStop for PreRecord/Recording to properly restore default mapping
        // (same pattern as backing to Character Select) 
        if (macroSt == MacroController::State::PreRecord || macroSt == MacroController::State::Recording) {
            MacroController::UnswapThenStop();
            logStream.str(""); logStream.clear();
            logStream << "[CANCEL][MACRO] Stopped PreRecord/Recording with UnswapThenStop";
            LogOut(logStream.str(), true);
            // Show "Macro: Cancelled" with darker red than Recording color (255,80,80)
            DirectDrawHook::AddMessage("Macro: Cancelled", "MACRO", RGB(180, 40, 40), 1200, 0, 120);
        } else {
            MacroController::Stop();
            logStream.str(""); logStream.clear();
            logStream << "[CANCEL][MACRO] Stopped active macro playback";
            LogOut(logStream.str(), true);
            // Show cancellation message for replay as well
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
    
    // Set the cancelled flag to trigger red flash on triggers for ~0.5 seconds
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

    // Retire the scoped P2 producer before any broader controller/buffer
    // cleanup.  This removes only the ring span owned by its generation and
    // waits out an in-progress native consumer under the transaction mutex.
    CancelAllAutoActionMotionTransactions("cancel auto-actions and macros");

    // Cancel the normal transaction before restoring controller flags. The
    // restore helper deliberately defers while a normal is active; doing this
    // in the opposite order can strand P2 in human control after cancellation.
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
    
    // Clear all input injection state for both players
    for (int p = 1; p <= 2; ++p) {
        g_forceBypass[p].store(false);
        g_pollOverrideActive[p].store(false);
        g_pollOverrideMask[p].store(0);
        g_manualInputOverride[p].store(false);
        g_manualInputMask[p].store(0);
        g_injectImmediateOnly[p].store(false);
    }

    // Queued motion injection is a separate owner from the atomics above. If it
    // survives cancellation, input_hook can still overwrite immediate inputs
    // (and can contaminate an exclusive mission demonstration).
    (void)ClearMotionInputQueue(1);
    (void)ClearMotionInputQueue(2);
    
    // Clear immediate input registers
    ImmediateInput::Clear(1);
    ImmediateInput::Clear(2);
    LogOut("[CANCEL][INJECT] Cleared injection state, motion queues, and immediate registers", true);

    LogOut("[CANCEL] === END CancelAutoActionsAndMacros - Triggers remain enabled ===", true);
}

// Hard reset of all auto-action trigger related runtime state
void ClearAllAutoActionTriggers() {
    std::lock_guard<std::recursive_mutex> stateLock(g_autoActionStateMutex);
    // Throttle repeated full-clear logs (can fire rapidly during Character Select stability scans)
    static int s_lastClearFrame = -1000000;
    int nowF = frameCounter.load();
    extern std::atomic<bool> g_suppressAutoActionClearLogging; // declared in frame_monitor.cpp
    bool logThis = !g_suppressAutoActionClearLogging.load() && (nowF - s_lastClearFrame) >= 480; // ~2.5s at 192fps
    if (logThis) {
        LogOut("[AUTO-ACTION] Forcing full clear of trigger/delay/cooldown state", true);
        s_lastClearFrame = nowF;
    }

    // Reset delay states (including chosen row fields)
    p1DelayState = {false, 0, TRIGGER_NONE, 0, -1, -1, 0, -1, -1, 0, 0, 0};
    p2DelayState = {false, 0, TRIGGER_NONE, 0, -1, -1, 0, -1, -1, 0, 0, 0};

    // Reset action applied markers
    p1ActionApplied = false;
    p2ActionApplied = false;

    // Reset last active trigger overlay feedback
    g_lastActiveTriggerType.store(TRIGGER_NONE);
    g_lastActiveTriggerFrame.store(0);

    // Reset internal cooldown / active guards (file-scope statics in this TU)
    p1TriggerActive = false; p1TriggerCooldown = 0;
    p2TriggerActive = false; p2TriggerCooldown = 0;
    ResetDeferredDashFollowupState();

    KaoriRecoilDuck::CancelAll("clear all auto-action triggers");
    CancelAllAutoActionChargeFollowups("clear all auto-action triggers");
    CancelAllAutoActionMotionTransactions("clear all auto-action triggers");

    // Controller restore refuses to cut through an active normal consumer.
    // Retire that owner first so the hard reset cannot leave P2 humanized.
    CancelAllAutoActionNormalPulses();

    // Cancel any pending restore logic & control overrides
    if (g_p2ControlOverridden) {
        RestoreP2ControlState();
    }
    if (!g_p2ControlOverridden.load(std::memory_order_acquire)) {
        g_pendingControlRestore.store(false, std::memory_order_release);
        g_pendingRestoreTimestamp.store(0, std::memory_order_release);
    }
    g_lastP2MoveID.store(-1);
    g_crgFastRestore.store(false);

    // Ensure input buffer freeze (for special motions) is lifted
    StopBufferFreezing();

    // Clear RG pending windows
    s_p1RGPending = false; s_p1RGExpiry = 0;
    s_p2RGPending = false; s_p2RGExpiry = 0;

    // Clear RG pre-arm state
    s_p1RGPrearmed = false; s_p1RGPrearmIsSpecial = false; s_p1RGPrearmActionType = -1; s_p1RGPrearmExpiry = 0;
    s_p2RGPrearmed = false; s_p2RGPrearmIsSpecial = false; s_p2RGPrearmActionType = -1; s_p2RGPrearmExpiry = 0;
    s_p1RGPrearmGeneration = 0;
    s_p2RGPrearmGeneration = 0;

    // Hard/session resets must not leave a wake request that can fire on a
    // later, unrelated state-96 transition.
    ResetWakeRuntimeState();

    if (logThis) {
        LogOut("[AUTO-ACTION] All trigger states cleared", true);
    }
    // Invalidate cached character IDs since a full clear typically aligns with character select transitions
    InvalidateCachedCharacterIDs("trigger clear");
}

// Row selection helpers
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

    // Build candidates only from enabled, usable rows. Character changes leave
    // the saved rows intact, but an unavailable move is never rolled.
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
            mainOpt = { true, triggerAfterBlockAction.load(), triggerAfterBlockStrength.load(), triggerAfterBlockDelay.load(), triggerAfterBlockCustomID.load(), triggerAfterBlockMacroSlot.load(), triggerAfterBlockCharge.load() };
            break;
        case TRIGGER_ON_WAKEUP:
            mainEnabled = triggerOnWakeupEnabled.load();
            mainOpt = { true, triggerOnWakeupAction.load(), triggerOnWakeupStrength.load(), triggerOnWakeupDelay.load(), triggerOnWakeupCustomID.load(), triggerOnWakeupMacroSlot.load(), triggerOnWakeupCharge.load() };
            break;
        case TRIGGER_AFTER_HITSTUN:
            mainEnabled = triggerAfterHitstunEnabled.load();
            mainOpt = { true, triggerAfterHitstunAction.load(), triggerAfterHitstunStrength.load(), triggerAfterHitstunDelay.load(), triggerAfterHitstunCustomID.load(), triggerAfterHitstunMacroSlot.load(), triggerAfterHitstunCharge.load() };
            break;
        case TRIGGER_AFTER_AIRTECH:
            mainEnabled = triggerAfterAirtechEnabled.load();
            mainOpt = { true, triggerAfterAirtechAction.load(), triggerAfterAirtechStrength.load(), triggerAfterAirtechDelay.load(), triggerAfterAirtechCustomID.load(), triggerAfterAirtechMacroSlot.load(), triggerAfterAirtechCharge.load() };
            break;
        case TRIGGER_ON_RG:
            mainEnabled = triggerOnRGEnabled.load();
            mainOpt = { true, triggerOnRGAction.load(), triggerOnRGStrength.load(), triggerOnRGDelay.load(), triggerOnRGCustomID.load(), triggerOnRGMacroSlot.load(), triggerOnRGCharge.load() };
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
    constexpr int kWakeHoldTicks = 3; // 3 visual frames covers last rising + 2 neutral frames
    
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
    // If an explicit strength override is provided for a special, honor it immediately.
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
            button = (actionType - ACTION_5A) % 4;  // Changed from % 3 to % 4 to handle A/B/C/D
        } else {
            int offset = actionType - ACTION_6A;
            button = offset % 4;  // Changed from % 3 to % 4 to handle A/B/C/D
        }
        buttonMask = (1 << (4 + button));
    } else if (actionType >= ACTION_JA && actionType <= ACTION_JD) {
        int button = (actionType - ACTION_JA) % 4;  // Changed from % 3 to % 4 to handle A/B/C/D
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
        // Both sides dispatch FM through the scoped pattern transaction,
        // so the authored wake window applies to P1 as well.
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

    // Primary admission and passive watcher publication are one transaction
    // boundary. The primary must be admitted first: the watcher reservation is
    // intentionally allowed to coexist with that already-admitted command but
    // blocks every later producer until IC/FIC completes or retires.
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
