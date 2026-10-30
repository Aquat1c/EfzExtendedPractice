#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>  
#include "../include/core/constants.h"

struct TriggerDelayState {
    bool isDelaying;
    int delayFramesRemaining;
    int triggerType;
    short pendingMoveID;
    int  chosenAction;     // ACTION_* or -1 if not set
    int  chosenStrength;   // 0..2 or -1 if not set
    int  chosenMacroSlot;  // 0=None or slot index
    int  chosenCustomId;   // optional, default -1
    int  chosenDelay;      // visual-frame delay for the chosen option, or -1 if inherited
    int  chosenChargeFollowup; // 0=Off, 1=IC on contact, 2=FIC window
    // P2 command submission is a native producer/consumer transaction; keep the
    // generation pinned to the exact selected option until consumed or rejected.
    uint64_t pendingMotionGeneration;
    uint64_t pendingNormalGeneration;
    // Recipe completion is published by the recipe, not by its first native motion.
    uint64_t pendingRecipeGeneration;
    int      dispatchAttempts;
    // Advanced only by a matching fighter pass, never by time; bounded in
    // internal ticks so an intent whose posture gate can never open cannot pin
    // the trigger for the rest of the round.
    int      pendingWaitTicks;
};

enum class AutoActionApplyResult : uint8_t {
    Accepted,
    Busy,
    Invalid,
};

extern TriggerDelayState p1DelayState;
extern TriggerDelayState p2DelayState;
extern bool p1ActionApplied;
extern bool p2ActionApplied;

extern std::atomic<int> g_lastActiveTriggerType;
extern std::atomic<int> g_lastActiveTriggerFrame;

extern std::atomic<bool> g_triggersCancelledActive;
extern std::atomic<int> g_triggersCancelledFrame;

short GetActionMoveID(int actionType, int triggerType = TRIGGER_NONE, int playerNum = 2);
void ProcessTriggerDelays();
void ProcessTriggerDelays(short moveID1, short moveID2, short prevMoveID1, short prevMoveID2);
void StartTriggerDelay(int playerNum, int triggerType, short moveID, int delayFrames);
void MonitorAutoActions();
void MonitorAutoActions(short moveID1, short moveID2, short prevMoveID1, short prevMoveID2);
void ResetActionFlags();
void ClearDelayStatesIfNonActionable();
void ClearDelayStatesIfNonActionable(short moveID1, short moveID2, short prevMoveID1, short prevMoveID2, const char* source);

AutoActionApplyResult ApplyAutoAction(int playerNum, uintptr_t moveIDAddr,
                                      short currentMoveID, short prevMoveID,
                                      uint64_t* motionGenerationOut = nullptr,
                                      uint64_t* normalGenerationOut = nullptr,
                                      uint64_t* recipeGenerationOut = nullptr);

int GetSpecialMoveStrength(int actionType, int triggerType);
std::string GetTriggerName(int triggerType);

extern std::atomic<bool> g_p2ControlOverridden;
extern uint32_t g_originalP2ControlFlag;
// Serializes every writer of the character-level P2 AI flag.
extern std::recursive_mutex g_p2ControlMutex;

void RestoreP2ControlState();
// Native input transactions must be retired first; the caller retains this world.
struct EfzTmEntryV1;
uint32_t RetireAutoActionControllers(const EfzTmEntryV1& heldWorld);
void EnableP2ControlForAutoAction();

// Transactional P2 ownership for tutorial dummy input: snapshots both live
// control flags and restores them only if the same battle objects still exist;
// while held, legacy auto-actions cannot seize or restore P2 control. Release
// is idempotent; a stale token is ignored.
bool AcquireTutorialP2Control(uint64_t& tokenOut);
bool TutorialP2ControlLeaseActive();
bool ReleaseTutorialP2Control(uint64_t token);
void ProcessAutoControlRestore();
void ProcessTriggerCooldowns();

extern std::atomic<bool> autoActionEnabled;
extern std::atomic<int>  autoActionPlayer;

extern std::atomic<bool>  g_pendingControlRestore;
extern std::atomic<short> g_lastP2MoveID;

void ProcessAutoControlRestore();
void ClearAllAutoActionTriggers();
void InvalidateAutoActionCharacterCaches(const char* reason);
// Mod state only; false when the state lock is busy, so the caller retries.
bool DropPrearmWorkAtPracticeBoundary();
// P1 processCharacterInput, once per engine tick. Strict no-op outside a Practice
// battle; only ever reads game state. Defined in frame_advantage.cpp.
void OnEngineTick();

// Cancels in-progress executions while preserving user-configured triggers.
void CancelAutoActionsAndMacros();

// Acquire snapshots/applies the trigger tuple under the producer mutex;
// Release synchronously cancels runtime state and conditionally restores it.
bool AcquireTutorialP2WakeProducer(int action, int strength, uint64_t& tokenOut);
void ReleaseTutorialP2WakeProducer(uint64_t token);

// Cancels only P2's native wake runtime; P1's menu lease and macros untouched.
void CancelTutorialP2WakeProducer();

// Auto-actions are evaluated once per internal tick from the input hook;
// the frame monitor skips its copy to avoid double-processing.
extern std::atomic<bool> g_tickIntegratedAutoActions;

// Called from the input hook once per sub-tick, before P1 processing.
void AutoActionsTick_Inline(short moveID1, short moveID2);
