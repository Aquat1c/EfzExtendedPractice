#include "../../include/game/auto_action_charge.h"

#include "../../include/core/constants.h"
#include "../../include/core/logger.h"
#include "../../include/core/memory.h"
#include "../../include/game/auto_action.h"
#include "../../include/game/collision_display.h"
#include "../../include/game/collision_hook.h"
#include "../../include/game/game_state.h"
#include "../../include/game/mission/mission_sequence_policy.h"
#include "../../include/input/auto_action_motion_transaction.h"
#include "../../include/input/immediate_input.h"
#include "../../include/input/input_freeze.h"
#include "../../include/input/input_core.h"
#include "../../include/input/motion_constants.h"
#include "../../include/input/motion_system.h"
#include "../../include/input/scoped_input_reservation.h"
#include "../../include/utils/utilities.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <sstream>

namespace {

using AutoActionChargePolicy::Mode;
using AutoActionChargePolicy::NativeGateSnapshot;
using EntityProbe = CollisionDisplay::ProjectileRingSlotProbe;

constexpr int kMaxLifetimeTicks = 384;
constexpr int kMaxSubmitAttempts = 3;
constexpr int kMaxConsumerVisits = 6;
constexpr int kMaxMoveId = 1023;
constexpr int kMaxFrameIndex = 4095;
constexpr uintptr_t kAnimationFrameOffset = 0x0A;
constexpr uintptr_t kFrameIcWindowFlagsOffset = 176;
constexpr uintptr_t kAnimationCancelIntoRankOffset = 2;
constexpr uintptr_t kAnimationMoveRankOffset = 0;
constexpr size_t kContactReadCapacity = 32;
constexpr size_t kMaxSourceMoves = 12;
constexpr size_t kEntitySlots =
    CollisionDisplay::kProjectileRingSlotCapacity;

enum class Phase : uint8_t {
    Idle,
    AwaitSource,
    AwaitWindow,
    Submitting,
    TransactionPending,
};

struct Slot {
    Phase phase = Phase::Idle;
    Mode mode = Mode::Off;
    int player = 0;
    int triggerType = 0;
    int actionType = 0;
    int ageTicks = 0;
    int submitAttempts = 0;
    uint32_t lifecycleGeneration = 0;
    uintptr_t gameState = 0;
    uintptr_t playerPtr = 0;
    short armMove = 0;
    short armFrame = 0;
    short sourceLastFrame = 0;
    int sourceLoopCount = 0;
    uint32_t contactEpoch = 0;
    uint32_t contactCursor = 0;
    bool contactSeen = false;
    bool entityAttributionEnabled = false;
    uint64_t reservationToken = 0;
    uint64_t primaryMotionGeneration = 0;
    uint64_t motionGeneration = 0;
    std::array<short, kMaxSourceMoves> sourceMoves{};
    size_t sourceMoveCount = 0;
    std::array<EntityProbe, kEntitySlots> entityLast{};
    std::array<bool, kEntitySlots> sourceEntities{};
};

struct DeferredWork {
    uint64_t cancelGeneration = 0;
    bool submit = false;
    int player = 0;
    uint64_t reservationToken = 0;
};

std::mutex g_mutex;
Slot g_slots[3];
std::atomic<uint8_t> g_activeMask{0};

const char* ModeName(Mode mode) {
    switch (mode) {
        case Mode::InstantCharge: return "IC on contact";
        case Mode::FlickerInstantCharge: return "FIC window";
        case Mode::Off:
        default: return "off";
    }
}

bool ReadShort(uintptr_t address, short& value) {
    return address != 0 && SafeReadMemory(address, &value, sizeof(value));
}

bool ReadCurrentAction(uintptr_t playerPtr, short& move, short& frame) {
    if (!playerPtr || !ReadShort(playerPtr + MOVE_ID_OFFSET, move) ||
        !ReadShort(playerPtr + kAnimationFrameOffset, frame)) {
        return false;
    }
    return move >= 0 && move <= kMaxMoveId &&
           frame >= 0 && frame <= kMaxFrameIndex;
}

template <typename T>
bool ReadValue(uintptr_t address, T& value) {
    return address != 0 && SafeReadMemory(address, &value, sizeof(value));
}

bool ReadNativeGate(uintptr_t playerPtr, short move, short frame,
                    NativeGateSnapshot& gate) {
    gate = {};
    if (!playerPtr || move < 0 || move > kMaxMoveId ||
        frame < 0 || frame > kMaxFrameIndex) {
        return false;
    }

    uintptr_t animationTable = 0;
    uintptr_t frameTable = 0;
    if (!ReadValue(playerPtr + RF_OFFSET, gate.rf) ||
        !ReadValue(playerPtr + IC_COLOR_OFFSET, gate.blueCharge) ||
        !ReadValue(playerPtr + PLAYER_HIT_STATE_OFFSET, gate.actionPhase) ||
        !ReadValue(playerPtr + YPOS_OFFSET, gate.y) ||
        !ReadValue(playerPtr + ANIM_TABLE_OFFSET, animationTable) ||
        !animationTable ||
        !ReadValue(animationTable +
                       static_cast<uintptr_t>(move) * ANIM_ENTRY_STRIDE +
                       ANIM_ENTRY_FRAMES_PTR_OFFSET,
                   frameTable) ||
        !frameTable ||
        !ReadValue(frameTable +
                       static_cast<uintptr_t>(frame) * FRAME_BLOCK_STRIDE +
                       kFrameIcWindowFlagsOffset,
                   gate.frameFlags) ||
        !ReadValue(animationTable +
                       static_cast<uintptr_t>(move) * ANIM_ENTRY_STRIDE +
                       kAnimationCancelIntoRankOffset,
                   gate.currentRank) ||
        !ReadValue(animationTable +
                       static_cast<uintptr_t>(
                           AutoActionChargePolicy::kGroundInstantChargeMove) *
                           ANIM_ENTRY_STRIDE +
                       kAnimationMoveRankOffset,
                   gate.instantChargeRank)) {
        return false;
    }

    gate.readable = true;
    return true;
}

short LatestSourceMove(const Slot& slot) {
    return slot.sourceMoveCount == 0
        ? 0
        : slot.sourceMoves[slot.sourceMoveCount - 1];
}

bool IsSourceMove(const Slot& slot, short move) {
    return std::find(slot.sourceMoves.begin(),
                     slot.sourceMoves.begin() + slot.sourceMoveCount,
                     move) != slot.sourceMoves.begin() + slot.sourceMoveCount;
}

bool AddSourceMove(Slot& slot, short move, short frame) {
    if (IsSourceMove(slot, move)) {
        slot.sourceLastFrame = frame;
        return true;
    }
    if (slot.sourceMoveCount >= slot.sourceMoves.size()) return false;
    slot.sourceMoves[slot.sourceMoveCount++] = move;
    slot.sourceLastFrame = frame;
    return true;
}

void LogSlot(const Slot& slot, const char* event, const char* reason = nullptr) {
    std::ostringstream out;
    out << "[AUTO-ACTION][CHARGE] P" << slot.player << ' '
        << ModeName(slot.mode) << ' ' << (event ? event : "event")
        << " trigger=" << slot.triggerType
        << " action=" << slot.actionType
        << " source=" << LatestSourceMove(slot) << ':' << slot.sourceLastFrame
        << " stages=" << slot.sourceMoveCount
        << " age=" << slot.ageTicks
        << " attempts=" << slot.submitAttempts;
    if (slot.sourceLoopCount > 0) {
        out << " loops=" << slot.sourceLoopCount;
    }
    if (slot.motionGeneration) out << " gen=" << slot.motionGeneration;
    if (slot.primaryMotionGeneration) {
        out << " primaryGen=" << slot.primaryMotionGeneration;
    }
    if (slot.reservationToken) out << " reserve=" << slot.reservationToken;
    if (reason && *reason) out << " reason=" << reason;
    LogOut(out.str(), true);
}

// Returns the pending 22C generation the caller must cancel after dropping g_mutex;
// never the primary transaction.
uint64_t RetireSlotLocked(Slot& slot, const char* reason,
                          bool cancelTransaction) {
    if (slot.phase == Phase::Idle) return 0;
    const uint64_t transactionGeneration =
        cancelTransaction && slot.phase == Phase::TransactionPending
            ? slot.motionGeneration
            : 0;
    const int player = slot.player;
    LogSlot(slot, "retired", reason);
    if (slot.reservationToken != 0) {
        ReleaseScopedInputReservation(
            slot.player, ScopedInputOwner::AutoActionCharge,
            slot.reservationToken);
    }
    slot = {};
    if (player >= 1 && player <= 2) {
        g_activeMask.fetch_and(
            static_cast<uint8_t>(~(1u << player)),
            std::memory_order_release);
    }
    return transactionGeneration;
}

void CompleteSlotLocked(Slot& slot) {
    const int player = slot.player;
    LogSlot(slot, "accepted");
    if (slot.reservationToken != 0) {
        ReleaseScopedInputReservation(
            slot.player, ScopedInputOwner::AutoActionCharge,
            slot.reservationToken);
    }
    slot = {};
    if (player >= 1 && player <= 2) {
        g_activeMask.fetch_and(
            static_cast<uint8_t>(~(1u << player)),
            std::memory_order_release);
    }
}

bool IdentityStillMatches(const Slot& slot) {
    return slot.lifecycleGeneration == GetRuntimeLifecycleGeneration() &&
           slot.gameState != 0 && slot.gameState == GetGameStatePtr() &&
           slot.playerPtr != 0 && slot.playerPtr == GetPlayerPointer(slot.player) &&
           ScopedInputReservationMatches(
               slot.player, ScopedInputOwner::AutoActionCharge,
               slot.reservationToken);
}

void UpdateEntityLineageLocked(Slot& slot) {
    if (!slot.entityAttributionEnabled || slot.sourceMoveCount == 0) return;

    std::array<EntityProbe, kEntitySlots> current{};
    if (!CollisionDisplay::ProbeProjectileRing(
            slot.player, current.data(), current.size())) {
        return;
    }

    for (size_t i = 0; i < current.size(); ++i) {
        const EntityProbe& now = current[i];
        EntityProbe& prior = slot.entityLast[i];
        if (!now.readable) continue;

        if (AutoActionChargePolicy::EntitySpawnEstablishesTemporalLineage(
                prior.readable, prior.alive, now.readable, now.alive)) {
            slot.sourceEntities[i] = true;
        } else if (AutoActionChargePolicy::ReadableEntityDespawnClearsLineage(
                       now.readable, now.alive)) {
            slot.sourceEntities[i] = false;
        }
        prior = now;
    }
}

bool IsVerifiedAutomaticContinuation(const Slot& slot, short fromMove,
                                     short toMove) {
    const int charId = slot.player == 1 ? displayData.p1CharID
                                         : displayData.p2CharID;
    if (charId == CHAR_ID_AKIKO) {
        return Mission::SequencePolicy::IsAkikoVacuumHitTransition(
            fromMove, toMove);
    }
    if (charId == CHAR_ID_NANASE) {
        return Mission::SequencePolicy::IsRumiCommandThrowSuccessTransition(
            fromMove, toMove);
    }
    return false;
}

bool FixedFicWindowAllowsPriorContact(const Slot& slot) {
    if (slot.mode != Mode::FlickerInstantCharge) return false;
    const int charId = slot.player == 1 ? displayData.p1CharID
                                        : displayData.p2CharID;
    return charId == CHAR_ID_KANO &&
           AutoActionChargePolicy::IsVerifiedKanoRandomMagicFixedFicSource(
               LatestSourceMove(slot));
}

bool SourceActionStillValidLocked(Slot& slot, short move, short frame) {
    const short latest = LatestSourceMove(slot);
    if (move == latest) {
        if (frame < slot.sourceLastFrame) {
            const int charId = slot.player == 1 ? displayData.p1CharID
                                                : displayData.p2CharID;
            if (charId != CHAR_ID_KANO ||
                !AutoActionChargePolicy::IsVerifiedKanoRandomMagicLoop(
                    move, slot.sourceLastFrame, frame,
                    slot.sourceLoopCount)) {
                return false;
            }

            ++slot.sourceLoopCount;
            slot.sourceLastFrame = frame;
            LogSlot(slot, "verified authored frame loop");
            return true;
        }
        slot.sourceLastFrame = frame;
        return true;
    }
    if (!IsVerifiedAutomaticContinuation(slot, latest, move)) return false;
    if (!AddSourceMove(slot, move, frame)) return false;
    slot.sourceLoopCount = 0;
    LogSlot(slot, "verified automatic continuation");
    return true;
}

bool EventBelongsToSource(const Slot& slot,
                          const Mission::Contact::Event& event) {
    if (event.attacker != slot.player ||
        event.result == Mission::Contact::Result::None) {
        return false;
    }
    if (event.source == Mission::Contact::Source::DirectPlayer) {
        return IsSourceMove(slot, event.attackerMove);
    }
    if (event.source != Mission::Contact::Source::Entity ||
        !slot.entityAttributionEnabled) {
        return false;
    }
    const bool attributed = event.entitySlot >= 0 &&
        static_cast<size_t>(event.entitySlot) < slot.sourceEntities.size() &&
        slot.sourceEntities[static_cast<size_t>(event.entitySlot)];
    return AutoActionChargePolicy::EntityContactCountsForMode(
        slot.mode, attributed);
}

bool ConsumeContactJournalLocked(Slot& slot, const char*& failureReason) {
    failureReason = nullptr;
    if (!AutoActionChargePolicy::ContactEvidenceSufficient(
            slot.mode, IsDirectContactHookReady(),
            IsEntityContactHookReady())) {
        failureReason = "required contact hook became unavailable";
        return false;
    }
    if (GetContactEventEpoch() != slot.contactEpoch) {
        failureReason = "contact journal epoch changed";
        return false;
    }

    std::array<Mission::Contact::Event, kContactReadCapacity> events{};
    const auto read = ReadCommittedContactEvents(
        slot.contactCursor, slot.contactEpoch, GetCompletedBattleUpdateBatch(),
        events.data(), events.size());
    slot.contactCursor = read.consumedThrough;
    if (read.overflow) {
        failureReason = "contact journal overflow";
        return false;
    }
    for (size_t i = 0; i < read.count; ++i) {
        if (EventBelongsToSource(slot, events[i])) {
            slot.contactSeen = true;
            break;
        }
    }
    return true;
}

void TickSlotLocked(Slot& slot, DeferredWork& work) {
    if (slot.phase == Phase::Idle || slot.phase == Phase::Submitting) return;
    ++slot.ageTicks;
    if (slot.ageTicks > kMaxLifetimeTicks) {
        work.cancelGeneration =
            RetireSlotLocked(slot, "safety lifetime expired", true);
        return;
    }
    if (!IdentityStillMatches(slot)) {
        work.cancelGeneration =
            RetireSlotLocked(slot, "world, fighter, or reservation changed", true);
        return;
    }

    short move = 0;
    short frame = 0;
    if (!ReadCurrentAction(slot.playerPtr, move, frame)) {
        work.cancelGeneration =
            RetireSlotLocked(slot, "current action became unreadable", true);
        return;
    }

    if (slot.phase == Phase::TransactionPending) {
        const bool fixedFicAllowsContact =
            FixedFicWindowAllowsPriorContact(slot);
        const AutoActionMotionOutcome outcome =
            GetAutoActionMotionTransactionOutcome(
                slot.player, slot.motionGeneration);
        if (AutoActionChargePolicy::ResolvePendingObservation(
                slot.mode,
                outcome == AutoActionMotionOutcome::Accepted,
                slot.contactSeen,
                fixedFicAllowsContact) ==
            AutoActionChargePolicy::PendingResolution::CompleteAccepted) {
            // The exact 22C consumer precedes later collision resolvers in the same battle update.
            CompleteSlotLocked(slot);
            return;
        }

        UpdateEntityLineageLocked(slot);
        const char* journalFailure = nullptr;
        if (!ConsumeContactJournalLocked(slot, journalFailure)) {
            work.cancelGeneration = RetireSlotLocked(
                slot, journalFailure, true);
            return;
        }
        if (AutoActionChargePolicy::ResolvePendingObservation(
                slot.mode, false, slot.contactSeen,
                fixedFicAllowsContact) ==
            AutoActionChargePolicy::PendingResolution::CancelForContact) {
            work.cancelGeneration = RetireSlotLocked(
                slot, "source contacted while native 22C was pending", true);
            return;
        }

        if (outcome == AutoActionMotionOutcome::Pending) {
            if (AutoActionChargePolicy::ExactDestinationAccepted(move)) return;
            if (!SourceActionStillValidLocked(slot, move, frame)) {
                work.cancelGeneration = RetireSlotLocked(
                    slot, "source action exited while native 22C was pending",
                    true);
            }
            return;
        }

        slot.motionGeneration = 0;
        if (outcome == AutoActionMotionOutcome::Failed &&
            slot.submitAttempts < kMaxSubmitAttempts &&
            SourceActionStillValidLocked(slot, move, frame)) {
            slot.phase = Phase::AwaitWindow;
            LogSlot(slot, "retrying native 22C transaction");
        } else {
            work.cancelGeneration = RetireSlotLocked(
                slot,
                outcome == AutoActionMotionOutcome::Cancelled
                    ? "native 22C transaction cancelled"
                    : "native 22C transaction failed",
                false);
        }
        return;
    }

    if (slot.phase == Phase::AwaitSource) {
        if (slot.primaryMotionGeneration != 0) {
            const uint64_t primaryGeneration =
                slot.primaryMotionGeneration;
            const AutoActionMotionOutcome primaryOutcome =
                GetAutoActionMotionTransactionOutcome(
                    slot.player, primaryGeneration);
            if (primaryOutcome == AutoActionMotionOutcome::Pending) {
                return;
            }
            if (primaryOutcome != AutoActionMotionOutcome::Accepted) {
                RetireSlotLocked(
                    slot,
                    primaryOutcome == AutoActionMotionOutcome::Cancelled
                        ? "bound primary motion was cancelled"
                        : "bound primary motion was not accepted",
                    false);
                return;
            }
            slot.primaryMotionGeneration = 0;
            LogSlot(slot, "bound primary accepted; observing source");
        }

        const bool actionChanged = move != slot.armMove || frame < slot.armFrame;
        if (!actionChanged || move < 200 ||
            AutoActionChargePolicy::ExactDestinationAccepted(move)) {
            return;
        }
        if (!AddSourceMove(slot, move, frame)) {
            RetireSlotLocked(slot, "source continuation capacity exceeded", false);
            return;
        }
        slot.phase = Phase::AwaitWindow;
        LogSlot(slot, "source action began");
    }

    if (slot.phase != Phase::AwaitWindow) return;

    if (!SourceActionStillValidLocked(slot, move, frame)) {
        RetireSlotLocked(slot, "source action exited or rewound", false);
        return;
    }

    UpdateEntityLineageLocked(slot);
    const char* journalFailure = nullptr;
    if (!ConsumeContactJournalLocked(slot, journalFailure)) {
        RetireSlotLocked(slot, journalFailure, false);
        return;
    }
    const bool fixedFicAllowsContact =
        FixedFicWindowAllowsPriorContact(slot);
    if (AutoActionChargePolicy::ContactCancels(
            slot.mode, slot.contactSeen, fixedFicAllowsContact)) {
        RetireSlotLocked(slot, "source contacted before the FIC window", false);
        return;
    }

    NativeGateSnapshot gate;
    if (!ReadNativeGate(slot.playerPtr, move, frame, gate) ||
        !AutoActionChargePolicy::ReadyToSubmit(
            slot.mode, slot.contactSeen, gate,
            fixedFicAllowsContact)) {
        return;
    }

    slot.phase = Phase::Submitting;
    work.submit = true;
    work.player = slot.player;
    work.reservationToken = slot.reservationToken;
}

void RunDeferredWork(DeferredWork work) {
    if (work.cancelGeneration != 0 &&
        work.player >= 1 && work.player <= 2) {
        (void)CancelAutoActionMotionTransactionIfGeneration(
            work.player, work.cancelGeneration,
            "charge follow-up retired");
    }
    if (!work.submit) return;

    uint64_t generation = 0;
    const AutoActionMotionSubmitResult submit =
        SubmitReservedAutoActionMotionTransaction(
            work.player, MOTION_22C, GAME_INPUT_C,
            kMaxConsumerVisits, work.reservationToken, &generation);

    uint64_t cancelGeneration = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        Slot& slot = g_slots[work.player];
        if (slot.phase != Phase::Submitting ||
            slot.reservationToken != work.reservationToken) {
            if (submit == AutoActionMotionSubmitResult::Accepted) {
                cancelGeneration = generation;
            }
        } else if (submit == AutoActionMotionSubmitResult::Busy) {
            slot.phase = Phase::AwaitWindow;
        } else {
            ++slot.submitAttempts;
            if (submit != AutoActionMotionSubmitResult::Accepted ||
                generation == 0) {
                cancelGeneration = RetireSlotLocked(
                    slot, "native 22C request was invalid", false);
            } else {
                slot.motionGeneration = generation;
                slot.phase = Phase::TransactionPending;
                LogSlot(slot, "submitted native 22C");
            }
        }
    }
    if (cancelGeneration != 0) {
        (void)CancelAutoActionMotionTransactionIfGeneration(
            work.player, cancelGeneration,
            "orphaned charge follow-up transaction");
    }
}

}

bool ArmAutoActionChargeFollowup(
    int playerNum, Mode mode, int triggerType, int actionType,
    uint64_t* reservationTokenOut, uint64_t primaryMotionGeneration) {
    if (reservationTokenOut) *reservationTokenOut = 0;
    if (playerNum < 1 || playerNum > 2 || mode == Mode::Off) return false;

    const bool directContactReady = IsDirectContactHookReady();
    const bool entityContactReady = IsEntityContactHookReady();
    if (!AutoActionChargePolicy::ContactEvidenceSufficient(
            mode, directContactReady, entityContactReady)) {
        Slot diagnostic;
        diagnostic.player = playerNum;
        diagnostic.mode = mode;
        diagnostic.triggerType = triggerType;
        diagnostic.actionType = actionType;
        LogSlot(diagnostic, "not armed",
                !directContactReady
                    ? "direct contact hook is unavailable"
                    : "FIC requires the entity contact hook");
        return false;
    }

    uint64_t cancelPriorGeneration = 0;
    bool armed = false;
    {
        // g_p2ControlMutex is the publication barrier shared by all mod-owned input producers.
        std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
        std::lock_guard<std::mutex> lock(g_mutex);
        Slot& slot = g_slots[playerNum];
        cancelPriorGeneration =
            RetireSlotLocked(slot, "replaced by a new auto-action", true);

        uint64_t reservationToken = 0;
        if (ImmediateInput::TutorialLeaseActive(playerNum) ||
            TutorialBufferFreezeLeaseActive() ||
            TutorialMotionQueueLeaseActive(playerNum)) {
            Slot diagnostic;
            diagnostic.player = playerNum;
            diagnostic.mode = mode;
            diagnostic.triggerType = triggerType;
            diagnostic.actionType = actionType;
            LogSlot(diagnostic, "not armed",
                    "a tutorial input lease is still active");
        } else if (g_manualInputOverride[playerNum].load(
                std::memory_order_acquire)) {
            Slot diagnostic;
            diagnostic.player = playerNum;
            diagnostic.mode = mode;
            diagnostic.triggerType = triggerType;
            diagnostic.actionType = actionType;
            LogSlot(diagnostic, "not armed",
                    "a manual-input owner is still active");
        } else if (ImmediateInput::GetCurrentDesired(playerNum) != 0 ||
            ImmediateInput::GetRemainingTicks(playerNum) != 0) {
            Slot diagnostic;
            diagnostic.player = playerNum;
            diagnostic.mode = mode;
            diagnostic.triggerType = triggerType;
            diagnostic.actionType = actionType;
            LogSlot(diagnostic, "not armed",
                    "an immediate-input owner is still active");
        } else if (!TryReserveScopedInput(
                playerNum, ScopedInputOwner::AutoActionCharge,
                reservationToken)) {
            Slot diagnostic;
            diagnostic.player = playerNum;
            diagnostic.mode = mode;
            diagnostic.triggerType = triggerType;
            diagnostic.actionType = actionType;
            LogSlot(diagnostic, "not armed", "input lane is already reserved");
        } else {
            const uintptr_t playerPtr = GetPlayerPointer(playerNum);
            const uintptr_t gameState = GetGameStatePtr();
            short move = 0;
            short frame = 0;
            if (!playerPtr || !gameState ||
                !ReadCurrentAction(playerPtr, move, frame)) {
                ReleaseScopedInputReservation(
                    playerNum, ScopedInputOwner::AutoActionCharge,
                    reservationToken);
            } else {
                std::array<EntityProbe, kEntitySlots> entityBaseline{};
                const bool entityBaselineReadable = entityContactReady &&
                    CollisionDisplay::ProbeProjectileRing(
                        playerNum, entityBaseline.data(),
                        entityBaseline.size());
                slot.phase = Phase::AwaitSource;
                slot.mode = mode;
                slot.player = playerNum;
                slot.triggerType = triggerType;
                slot.actionType = actionType;
                slot.lifecycleGeneration = GetRuntimeLifecycleGeneration();
                slot.gameState = gameState;
                slot.playerPtr = playerPtr;
                slot.armMove = move;
                slot.armFrame = frame;
                slot.contactEpoch = GetContactEventEpoch();
                slot.contactCursor = GetContactEventWatermark();
                slot.reservationToken = reservationToken;
                slot.primaryMotionGeneration = primaryMotionGeneration;
                slot.entityAttributionEnabled = entityContactReady &&
                    (mode == Mode::FlickerInstantCharge ||
                     entityBaselineReadable);
                slot.entityLast = entityBaseline;
                g_activeMask.fetch_or(
                    static_cast<uint8_t>(1u << playerNum),
                    std::memory_order_release);
                if (reservationTokenOut) {
                    *reservationTokenOut = reservationToken;
                }
                LogSlot(slot, "watcher reserved",
                        slot.entityAttributionEnabled
                            ? nullptr
                            : "entity attribution unavailable; direct contact only");
                armed = true;
            }
        }
    }

    if (cancelPriorGeneration != 0) {
        (void)CancelAutoActionMotionTransactionIfGeneration(
            playerNum, cancelPriorGeneration,
            "charge follow-up replaced");
    }
    return armed;
}

void TickAutoActionChargeFollowups() {
    if (g_activeMask.load(std::memory_order_acquire) == 0) return;
    for (int playerNum = 1; playerNum <= 2; ++playerNum) {
        DeferredWork work;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            work.player = playerNum;
            TickSlotLocked(g_slots[playerNum], work);
        }
        RunDeferredWork(work);
    }
}

bool IsAutoActionChargeFollowupActive(int playerNum) {
    if (playerNum < 1 || playerNum > 2) return false;
    return (g_activeMask.load(std::memory_order_acquire) &
            static_cast<uint8_t>(1u << playerNum)) != 0;
}

void CancelAutoActionChargeFollowup(int playerNum, const char* reason) {
    if (playerNum < 1 || playerNum > 2) return;
    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    uint64_t cancelGeneration = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        cancelGeneration = RetireSlotLocked(
            g_slots[playerNum], reason ? reason : "cancelled", true);
    }
    if (cancelGeneration != 0) {
        (void)CancelAutoActionMotionTransactionIfGeneration(
            playerNum, cancelGeneration,
            reason ? reason : "charge follow-up cancelled");
    }
}

bool CancelAutoActionChargeFollowupIfReservation(
    int playerNum, uint64_t reservationToken, const char* reason) {
    if (playerNum < 1 || playerNum > 2 || reservationToken == 0) {
        return false;
    }

    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    uint64_t cancelGeneration = 0;
    bool cancelled = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        Slot& slot = g_slots[playerNum];
        if (slot.phase != Phase::Idle &&
            slot.player == playerNum &&
            slot.reservationToken == reservationToken) {
            cancelGeneration = RetireSlotLocked(
                slot, reason ? reason : "cancelled", true);
            cancelled = true;
        }
    }
    // Transaction cleanup takes the controller barrier but never g_mutex; keep this ordering to avoid deadlock.
    if (cancelGeneration != 0) {
        (void)CancelAutoActionMotionTransactionIfGeneration(
            playerNum, cancelGeneration,
            reason ? reason : "charge follow-up cancelled");
    }
    return cancelled;
}

void CancelAllAutoActionChargeFollowups(const char* reason) {
    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    std::array<uint64_t, 3> cancelGeneration{};
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (int playerNum = 1; playerNum <= 2; ++playerNum) {
            cancelGeneration[playerNum] = RetireSlotLocked(
                g_slots[playerNum], reason ? reason : "cancelled", true);
        }
    }
    for (int playerNum = 1; playerNum <= 2; ++playerNum) {
        if (cancelGeneration[playerNum] != 0) {
            (void)CancelAutoActionMotionTransactionIfGeneration(
                playerNum, cancelGeneration[playerNum],
                reason ? reason : "charge follow-up cancelled");
        }
    }
    ReleaseAllScopedInputReservations(ScopedInputOwner::AutoActionCharge);
}
