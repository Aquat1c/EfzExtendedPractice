#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>  
#include "../include/core/constants.h"

enum class WakeMacroTargetPhase { None, WaitingFrame, RetryActionable };
enum class WakeMacroQueuePhase { NotQueued, Queued };
class WakeMacroSchedule {
public:
    bool Queued() const { return queue_ == WakeMacroQueuePhase::Queued; }
    bool HasTarget() const { return target_ == WakeMacroTargetPhase::WaitingFrame; }
    bool ShouldRetryActionable() const { return target_ == WakeMacroTargetPhase::RetryActionable; }
    int TargetFrame() const { return frame_; }
    int TraceTargetFrame() const { return HasTarget() ? frame_ : ShouldRetryActionable() ? -2 : -1; }
    void ArmTarget(int frame) { frame_ = frame; target_ = WakeMacroTargetPhase::WaitingFrame; }
    void ClearTarget() { target_ = WakeMacroTargetPhase::None; frame_ = 0; }
    void RetryActionable() { target_ = WakeMacroTargetPhase::RetryActionable; frame_ = 0; }
    void MarkQueued() { queue_ = WakeMacroQueuePhase::Queued; }
    void ResetQueue() { queue_ = WakeMacroQueuePhase::NotQueued; }
private:
    WakeMacroTargetPhase target_ = WakeMacroTargetPhase::None;
    WakeMacroQueuePhase queue_ = WakeMacroQueuePhase::NotQueued;
    int frame_ = 0;
};

enum class WakePrearmPhase { Idle, Ordinary, Special, Macro };
class WakePrearmState {
public:
    bool Armed() const { return phase_ != WakePrearmPhase::Idle; }
    bool IsSpecial() const { return phase_ == WakePrearmPhase::Special; }
    bool IsMacro() const { return phase_ == WakePrearmPhase::Macro; }
    void Arm(WakePrearmPhase phase) { phase_ = phase; }
    void Disarm() { phase_ = WakePrearmPhase::Idle; }
private:
    WakePrearmPhase phase_ = WakePrearmPhase::Idle;
};

enum class WakeBufferedInputPhase { Empty, BufferedWithoutGeneration, AwaitingConsumer, RetainedGeneration };
class WakeBufferedInputState {
public:
    WakeBufferedInputPhase Phase() const { return phase_; }
    bool Buffered() const { return phase_ == WakeBufferedInputPhase::BufferedWithoutGeneration || phase_ == WakeBufferedInputPhase::AwaitingConsumer; }
    uint64_t Generation() const { return generation_; }
    void SetBuffered(bool buffered) {
        phase_ = buffered
            ? (generation_ != 0 ? WakeBufferedInputPhase::AwaitingConsumer : WakeBufferedInputPhase::BufferedWithoutGeneration)
            : (generation_ != 0 ? WakeBufferedInputPhase::RetainedGeneration : WakeBufferedInputPhase::Empty);
    }
    void TrackMotion(uint64_t generation) {
        const bool buffered = Buffered();
        generation_ = generation;
        SetBuffered(buffered);
    }
private:
    WakeBufferedInputPhase phase_ = WakeBufferedInputPhase::Empty;
    uint64_t generation_ = 0;
};

enum class WakeHoldPhase { Disabled, Primed, Issued };
constexpr WakeHoldPhase WakeHoldAfterFailure(WakeHoldPhase phase) {
    return phase == WakeHoldPhase::Issued ? WakeHoldPhase::Primed : phase;
}

enum class TriggerDelayPhase { Idle, WaitingDispatch, AwaitingMotion, AwaitingNormal, AwaitingRecipe };

struct TriggerDelayState {
    TriggerDelayPhase phase;
    int delayFramesRemaining;
    int triggerType;
    short pendingMoveID;
    int  chosenAction;     // ACTION_* or -1 if not set
    int  chosenStrength;   // 0=A, 1=B, 2=C, 3=D, or -1 if not set
    int  chosenMacroSlot;  // 0=None or slot index
    int  chosenCustomId;
    int  chosenDelay;      // visual-frame delay for the chosen option, or -1 if inherited
    int  chosenChargeFollowup; // 0=Off, 1=IC on contact, 2=FIC window
    uint64_t pendingGeneration;
    int      dispatchAttempts;
    int      pendingWaitTicks;

    bool IsDelaying() const { return phase != TriggerDelayPhase::Idle; }
    void RetryDispatch() {
        phase = TriggerDelayPhase::WaitingDispatch;
        pendingGeneration = 0;
    }
    void WaitFor(TriggerDelayPhase consumer, uint64_t generation) {
        phase = consumer;
        pendingGeneration = generation;
    }
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
struct EfzTmEntryV1;
uint32_t RetireAutoActionControllers(const EfzTmEntryV1& heldWorld);
void EnableP2ControlForAutoAction();

// Tutorial lease on P2 input; while held, legacy auto-actions cannot seize or restore P2 control.
// Release is idempotent and restores the flags only if the same battle objects still exist.
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

void CancelAutoActionsAndMacros();

bool AcquireTutorialP2WakeProducer(int action, int strength, uint64_t& tokenOut);
void ReleaseTutorialP2WakeProducer(uint64_t token);

void CancelTutorialP2WakeProducer();

// Auto-actions are evaluated once per internal tick from the input hook;
// the frame monitor skips its copy to avoid double-processing.
extern std::atomic<bool> g_tickIntegratedAutoActions;

// Called from the input hook once per sub-tick, before P1 processing.
void AutoActionsTick_Inline(short moveID1, short moveID2);
