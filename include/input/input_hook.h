#pragma once

#include <cstddef>
#include <cstdint>
#include "input_poll_attack_edge_journal.h"
#include "normal_input_policy.h"

namespace InputHookPolicy {

enum class TutorialP2InputSource : uint8_t {
    Neutral,
    MotionQueue,
    ImmediatePress,
    BufferFreeze,
};

enum class ProcessRoute : uint8_t {
    NativePoll,
    BufferedBypass,
};

// Tutorial P2 input always goes through HookedPoll with EFZ's original processor running: it owns
// command/dash recognition and the character update, so a bypass route is never valid.
constexpr ProcessRoute TutorialP2Route(TutorialP2InputSource) {
    return ProcessRoute::NativePoll;
}

// A normal pulse may share a tutorial P2 lease only while that lease is neutral. Actionable normals
// wait; an exact-tick dash normal is retired instead of firing late.
constexpr bool TutorialP2AllowsNormalPulse(TutorialP2InputSource source) {
    return source == TutorialP2InputSource::Neutral;
}

constexpr int LogicalPollPlayer(int processPlayer, int polledPlayer) {
    return processPlayer == 1 || processPlayer == 2
        ? processPlayer
        : polledPlayer;
}

constexpr bool P1StartupNeutralApplies(bool gateActive,
                                       int processPlayer,
                                       int) {
    return gateActive && processPlayer == 1;
}

}

void InstallInputHook();

// Mission/tutorial startup owner. Survives character-select cleanup and savestate restore; keep it
// active until the runner baseline is ready and the tutorial owns its input.
void SetP1StartupNeutralGate(bool active);
bool IsP1StartupNeutralGateActive();

// AI fighters keep their AI flag and get a post-AI raw pulse; others use the native poll. Both hold
// the press through the matching consumer, then release-neutral through the next matching pass.
enum class AutoActionNormalPulseOutcome : uint8_t {
    Unknown,
    Pending,
    Accepted,
    Failed,
    Cancelled,
};

NormalInputPolicy::SubmitResult QueueAutoActionNormalPulse(
    int playerNum,
    const NormalInputPolicy::Intent& intent,
    NormalInputPolicy::Timing timing = NormalInputPolicy::Timing::WhenActionable,
    uint64_t* generationOut = nullptr);
NormalInputPolicy::SubmitResult QueueAutoActionNormalPulse(
    int playerNum,
    int motionType,
    NormalInputPolicy::Timing timing = NormalInputPolicy::Timing::WhenActionable,
    uint64_t* generationOut = nullptr);
bool IsAutoActionNormalPulseActive(int playerNum);
// Queue admission is not execution. This generation result becomes Accepted
// only after EFZ's later character consumer starts the requested normal.
AutoActionNormalPulseOutcome GetAutoActionNormalPulseOutcome(
    int playerNum, uint64_t generation);
// True while either normal transport owns +392..397 through EFZ's later character consumer.
// Immediate and stance writers pause in that window.
bool IsAutoActionNormalPulseOwningImmediateRegisters(int playerNum);
// Serializes ImmediateInput against the AI transport, scoped motions and netplay publication.
// Pair a successful acquire with Release; on false the writer must retry without touching memory.
bool TryAcquireImmediateInputWriteLease(int playerNum);
void ReleaseImmediateInputWriteLease(int playerNum);
void CancelAutoActionNormalPulse(int playerNum);
void CancelAllAutoActionNormalPulses();
// Scrubs both normal lanes and motion transactions; the caller holds the offline->netplay barrier.
// Returns false only when same-world cleanup is unproven after the bounded retry.
bool DrainAutoActionNormalPulsesForOwnershipBoundary();
struct EfzTmEntryV1;
uint32_t RetireNativeInputOwners(const EfzTmEntryV1& world);

void SetInputHookActive(bool active);

void RemoveInputHook();

// Swaps EFZ's two 16-byte live control maps, as Revival's Practice SwitchPlayers does, without
// invoking netplay state. Returns false if the maps could not be updated and verified.
bool SetVanillaSwapInputRouting(bool enable);

void ResetInputPollOverrideHitCount(int playerNum);
uint32_t GetInputPollOverrideHitCount(int playerNum);

// Attack-button rising edges with their poll serial, in a bounded SPSC queue. Callers supply drain
// storage, so neither the poll hook nor the consumer allocates or locks.
using InputPollAttackEdgeEvent = InputPollAttackEdgeJournalPolicy::Event;
using InputPollAttackEdgeDrainResult =
    InputPollAttackEdgeJournalPolicy::DrainResult;
constexpr std::size_t kInputPollAttackEdgeJournalCapacity = 128;

InputPollAttackEdgeDrainResult DrainInputPollAttackEdgeJournal(
    int playerNum,
    InputPollAttackEdgeEvent* eventsOut,
    std::size_t eventCapacity);
void ResetInputPollAttackEdgeJournal(int playerNum);

// These merge queued events into one mask; recording code should use the ordered drain above.
uint8_t ConsumeInputPollAttackEdges(int playerNum);
uint8_t ConsumeInputPollAttackEdgeBatch(int playerNum, uint32_t& serialOut);
uint32_t GetInputPollSerial(int playerNum);

void SetPollOverridePhysicalObservation(int playerNum, bool enabled);
bool IsPollOverridePhysicalObservationEnabled(int playerNum);
uint8_t GetLastObservedPhysicalPollMask(int playerNum);
bool TryGetLastObservedPhysicalPoll(int playerNum, uint8_t& maskOut,
                                    uint32_t* serialOut = nullptr);

void InputHook_ArmTokenNeutralize(int playerNum, bool alsoDoFullCleanup);
