#include "../../include/game/kaori_recoil_duck.h"

#include "../../include/core/constants.h"
#include "../../include/core/logger.h"
#include "../../include/core/memory.h"
#include "../../include/game/auto_action.h"
#include "../../include/game/game_state.h"
#include "../../include/game/kaori_recoil_duck_policy.h"
#include "../../include/input/input_core.h"
#include "../../include/input/motion_constants.h"
#include "../../include/input/scoped_input_reservation.h"
#include "../../include/utils/network.h"
#include "../../include/utils/utilities.h"

#include <atomic>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <sstream>

namespace KaoriRecoilDuck {
namespace {

constexpr int kMaxLifetimeTicks = 192;
constexpr int kMaxForwardSubmitAttempts = 3;
constexpr int kPrimaryConsumerVisits = 1;
constexpr int kForwardConsumerVisits = 2;
constexpr size_t kOutcomeHistorySize = 8;

enum class Phase : uint8_t {
    Idle,
    BackdashPending,
    AwaitForwardWindow,
    ForwardPending,
};

struct Slot {
    Phase phase = Phase::Idle;
    int player = 0;
    int ageTicks = 0;
    int forwardSubmitAttempts = 0;
    uint32_t lifecycleGeneration = 0;
    uintptr_t gameState = 0;
    uintptr_t playerPtr = 0;
    uint64_t reservationToken = 0;
    uint64_t backdashGeneration = 0;
    uint64_t forwardGeneration = 0;
    uint64_t recipeGeneration = 0;
};

struct OutcomeRecord {
    uint64_t generation = 0;
    AutoActionMotionOutcome outcome = AutoActionMotionOutcome::Unknown;
};

std::mutex g_mutex;
Slot g_slots[3];
std::atomic<uint8_t> g_activeMask{0};
std::atomic<uint64_t> g_nextRecipeGeneration[3]{{1}, {1}, {1}};
std::array<OutcomeRecord, kOutcomeHistorySize> g_outcomeHistory[3]{};
size_t g_outcomeCursor[3]{};

void RecordOutcomeLocked(int playerNum, uint64_t generation,
                         AutoActionMotionOutcome outcome) {
    if (playerNum < 1 || playerNum > 2 || generation == 0) return;
    for (OutcomeRecord& record : g_outcomeHistory[playerNum]) {
        if (record.generation == generation) {
            record.outcome = outcome;
            return;
        }
    }
    OutcomeRecord& record = g_outcomeHistory[playerNum][
        g_outcomeCursor[playerNum]++ % kOutcomeHistorySize];
    record = {generation, outcome};
}

AutoActionMotionOutcome FindOutcomeLocked(int playerNum,
                                          uint64_t generation) {
    if (playerNum < 1 || playerNum > 2 || generation == 0) {
        return AutoActionMotionOutcome::Unknown;
    }
    for (const OutcomeRecord& record : g_outcomeHistory[playerNum]) {
        if (record.generation == generation) return record.outcome;
    }
    return AutoActionMotionOutcome::Unknown;
}

const char* PhaseName(Phase phase) {
    switch (phase) {
        case Phase::Idle: return "idle";
        case Phase::BackdashPending: return "44 pending";
        case Phase::AwaitForwardWindow: return "awaiting frame 4/5";
        case Phase::ForwardPending: return "66 pending";
    }
    return "unknown";
}

bool ReadAction(const Slot& slot, short& move, short& frame) {
    return slot.playerPtr != 0 &&
           SafeReadMemory(slot.playerPtr + MOVE_ID_OFFSET,
                          &move, sizeof(move)) &&
           SafeReadMemory(slot.playerPtr + CURRENT_FRAME_INDEX_OFFSET,
                          &frame, sizeof(frame));
}

bool IdentityMatches(const Slot& slot) {
    return slot.player >= 1 && slot.player <= 2 &&
           slot.lifecycleGeneration == GetRuntimeLifecycleGeneration() &&
           slot.gameState != 0 && slot.gameState == GetGameStatePtr() &&
           slot.playerPtr != 0 &&
           slot.playerPtr == GetPlayerPointer(slot.player) &&
           ScopedInputReservationMatches(
               slot.player, ScopedInputOwner::KaoriRecoilDucking,
               slot.reservationToken);
}

void LogSlot(const Slot& slot, const char* event, const char* reason = nullptr,
             short move = -1, short frame = -1) {
    std::ostringstream out;
    out << "[AUTO-ACTION][KAORI 44~66] P" << slot.player << ' '
        << (event ? event : "event")
        << " phase=" << PhaseName(slot.phase)
        << " age=" << slot.ageTicks
        << " attempts=" << slot.forwardSubmitAttempts;
    if (move >= 0) out << " action=" << move << ':' << frame;
    if (slot.backdashGeneration) out << " backGen=" << slot.backdashGeneration;
    if (slot.forwardGeneration) out << " fwdGen=" << slot.forwardGeneration;
    if (slot.recipeGeneration) out << " recipeGen=" << slot.recipeGeneration;
    if (slot.reservationToken) out << " reserve=" << slot.reservationToken;
    if (reason && *reason) out << " reason=" << reason;
    LogOut(out.str(), true);
}

uint64_t PendingGeneration(const Slot& slot) {
    if (slot.phase == Phase::BackdashPending) {
        return slot.backdashGeneration;
    }
    if (slot.phase == Phase::ForwardPending) {
        return slot.forwardGeneration;
    }
    return 0;
}

uint64_t RetireLocked(Slot& slot, const char* reason,
                      bool cancelPendingTransaction,
                      short move = -1, short frame = -1,
                      AutoActionMotionOutcome recipeOutcome =
                          AutoActionMotionOutcome::Failed) {
    if (slot.phase == Phase::Idle) return 0;
    const int player = slot.player;
    const uint64_t pendingGeneration =
        cancelPendingTransaction ? PendingGeneration(slot) : 0;
    LogSlot(slot, "retired", reason, move, frame);
    RecordOutcomeLocked(player, slot.recipeGeneration, recipeOutcome);
    if (slot.reservationToken != 0) {
        ReleaseScopedInputReservation(
            player, ScopedInputOwner::KaoriRecoilDucking,
            slot.reservationToken);
    }
    slot = {};
    if (player >= 1 && player <= 2) {
        g_activeMask.fetch_and(
            static_cast<uint8_t>(~(1u << player)),
            std::memory_order_release);
    }
    return pendingGeneration;
}

void CompleteLocked(Slot& slot, short move, short frame) {
    const int player = slot.player;
    LogSlot(slot, "accepted exact recoil duck", nullptr, move, frame);
    RecordOutcomeLocked(player, slot.recipeGeneration,
                        AutoActionMotionOutcome::Accepted);
    if (slot.reservationToken != 0) {
        ReleaseScopedInputReservation(
            player, ScopedInputOwner::KaoriRecoilDucking,
            slot.reservationToken);
    }
    slot = {};
    g_activeMask.fetch_and(
        static_cast<uint8_t>(~(1u << player)),
        std::memory_order_release);
}

uint64_t TickSlotLocked(Slot& slot) {
    if (slot.phase == Phase::Idle) return 0;
    ++slot.ageTicks;
    if (slot.ageTicks > kMaxLifetimeTicks) {
        return RetireLocked(slot, "bounded recipe lifetime expired", true);
    }
    if (!IdentityMatches(slot) ||
        g_onlineModeActive.load(std::memory_order_acquire) ||
        !IsMatchPhase()) {
        return RetireLocked(
            slot, "world, fighter, reservation, or ownership changed", true);
    }

    short move = -1;
    short frame = -1;
    if (!ReadAction(slot, move, frame)) {
        return RetireLocked(slot, "current action became unreadable", true);
    }

    if (slot.phase == Phase::BackdashPending) {
        const AutoActionMotionOutcome outcome =
            GetAutoActionMotionTransactionOutcome(
                slot.player, slot.backdashGeneration);
        if (outcome == AutoActionMotionOutcome::Pending) return 0;
        if (outcome != AutoActionMotionOutcome::Accepted ||
            !KaoriRecoilDuckPolicy::IsBackdash(move)) {
            return RetireLocked(
                slot,
                outcome == AutoActionMotionOutcome::Accepted
                    ? "native 44 entered the wrong movement state"
                    : "native 44 transaction was not accepted",
                false, move, frame);
        }
        slot.phase = Phase::AwaitForwardWindow;
        LogSlot(slot, "native 44 accepted", nullptr, move, frame);
    }

    if (slot.phase == Phase::AwaitForwardWindow) {
        if (!KaoriRecoilDuckPolicy::IsBackdash(move)) {
            return RetireLocked(
                slot, "backdash ended before its native 66 window",
                false, move, frame);
        }
        if (!KaoriRecoilDuckPolicy::IsForwardInputWindow(move, frame)) {
            return 0;
        }

        ++slot.forwardSubmitAttempts;
        uint64_t generation = 0;
        const AutoActionMotionSubmitResult submit =
            SubmitScopedAutoActionMotionTransaction(
                slot.player, MOTION_FORWARD_DASH, 0,
                kForwardConsumerVisits,
                ScopedInputOwner::KaoriRecoilDucking,
                slot.reservationToken, &generation);
        if (submit == AutoActionMotionSubmitResult::Accepted && generation != 0) {
            slot.forwardGeneration = generation;
            slot.phase = Phase::ForwardPending;
            LogSlot(slot, "submitted native 66 in verified window", nullptr,
                    move, frame);
            return 0;
        }
        if (submit == AutoActionMotionSubmitResult::Busy &&
            slot.forwardSubmitAttempts < kMaxForwardSubmitAttempts) {
            LogSlot(slot, "native 66 lane busy; retaining exact window",
                    nullptr, move, frame);
            return 0;
        }
        return RetireLocked(
            slot,
            submit == AutoActionMotionSubmitResult::Busy
                ? "native 66 exhausted bounded admission retries"
                : "native 66 request was invalid",
            false, move, frame);
    }

    if (slot.phase == Phase::ForwardPending) {
        const AutoActionMotionOutcome outcome =
            GetAutoActionMotionTransactionOutcome(
                slot.player, slot.forwardGeneration);
        if (outcome == AutoActionMotionOutcome::Pending) {
            if (KaoriRecoilDuckPolicy::IsBackdash(move) ||
                KaoriRecoilDuckPolicy::IsExactDestination(move)) {
                return 0;
            }
            return RetireLocked(
                slot, "fighter left the owned 44~66 sequence", true,
                move, frame);
        }
        if (outcome == AutoActionMotionOutcome::Accepted &&
            KaoriRecoilDuckPolicy::IsExactDestination(move)) {
            CompleteLocked(slot, move, frame);
            return 0;
        }
        return RetireLocked(
            slot,
            outcome == AutoActionMotionOutcome::Accepted
                ? "native 66 did not enter exact move 251"
                : "native 66 transaction was not accepted",
            false, move, frame);
    }

    return 0;
}

}

AutoActionMotionSubmitResult Begin(int playerNum, int characterId,
                                   uint64_t* recipeGenerationOut) {
    if (recipeGenerationOut) *recipeGenerationOut = 0;
    if ((playerNum != 1 && playerNum != 2) ||
        characterId != CHAR_ID_KAORI ||
        g_onlineModeActive.load(std::memory_order_acquire) ||
        !IsMatchPhase()) {
        return AutoActionMotionSubmitResult::Invalid;
    }

    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    std::lock_guard<std::mutex> stateLock(g_mutex);
    Slot& slot = g_slots[playerNum];
    if (slot.phase != Phase::Idle) {
        return AutoActionMotionSubmitResult::Busy;
    }

    const uintptr_t gameState = GetGameStatePtr();
    const uintptr_t playerPtr = GetPlayerPointer(playerNum);
    double y = 0.0;
    if (!gameState || !playerPtr ||
        !SafeReadMemory(playerPtr + YPOS_OFFSET, &y, sizeof(y)) ||
        !std::isfinite(y)) {
        return AutoActionMotionSubmitResult::Invalid;
    }
    if (y < -0.001) return AutoActionMotionSubmitResult::Invalid;

    uint64_t reservation = 0;
    if (!TryReserveScopedInput(
            playerNum, ScopedInputOwner::KaoriRecoilDucking,
            reservation)) {
        return AutoActionMotionSubmitResult::Busy;
    }

    slot.phase = Phase::BackdashPending;
    slot.player = playerNum;
    slot.lifecycleGeneration = GetRuntimeLifecycleGeneration();
    slot.gameState = gameState;
    slot.playerPtr = playerPtr;
    slot.reservationToken = reservation;
    uint64_t recipeGeneration =
        g_nextRecipeGeneration[playerNum].fetch_add(
            1, std::memory_order_acq_rel);
    if (recipeGeneration == 0) {
        recipeGeneration = g_nextRecipeGeneration[playerNum].fetch_add(
            1, std::memory_order_acq_rel);
    }
    slot.recipeGeneration = recipeGeneration;
    RecordOutcomeLocked(playerNum, recipeGeneration,
                        AutoActionMotionOutcome::Pending);

    uint64_t generation = 0;
    const AutoActionMotionSubmitResult submit =
        SubmitScopedAutoActionMotionTransaction(
            playerNum, MOTION_BACK_DASH, 0,
            kPrimaryConsumerVisits,
            ScopedInputOwner::KaoriRecoilDucking,
            reservation, &generation);
    if (submit != AutoActionMotionSubmitResult::Accepted || generation == 0) {
        (void)RetireLocked(
            slot,
            submit == AutoActionMotionSubmitResult::Busy
                ? "native 44 lane was busy"
                : "native 44 request was invalid",
            false);
        return submit;
    }

    slot.backdashGeneration = generation;
    g_activeMask.fetch_or(
        static_cast<uint8_t>(1u << playerNum),
        std::memory_order_release);
    if (recipeGenerationOut) *recipeGenerationOut = recipeGeneration;
    LogSlot(slot, "staged recipe armed");
    return AutoActionMotionSubmitResult::Accepted;
}

AutoActionMotionOutcome GetOutcome(int playerNum,
                                   uint64_t recipeGeneration) {
    std::lock_guard<std::mutex> stateLock(g_mutex);
    return FindOutcomeLocked(playerNum, recipeGeneration);
}

void Tick() {
    if (g_activeMask.load(std::memory_order_acquire) == 0) return;

    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    for (int playerNum = 1; playerNum <= 2; ++playerNum) {
        if ((g_activeMask.load(std::memory_order_acquire) &
             static_cast<uint8_t>(1u << playerNum)) == 0) {
            continue;
        }
        uint64_t cancelGeneration = 0;
        {
            std::lock_guard<std::mutex> stateLock(g_mutex);
            cancelGeneration = TickSlotLocked(g_slots[playerNum]);
        }
        if (cancelGeneration != 0) {
            CancelAutoActionMotionTransactionIfGeneration(
                playerNum, cancelGeneration,
                "Kaori 44~66 staged recipe retired");
        }
    }
}

bool IsActive(int playerNum) {
    return (playerNum == 1 || playerNum == 2) &&
           (g_activeMask.load(std::memory_order_acquire) &
            static_cast<uint8_t>(1u << playerNum)) != 0;
}

void Cancel(int playerNum, const char* reason) {
    if (playerNum != 1 && playerNum != 2) return;
    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    uint64_t cancelGeneration = 0;
    {
        std::lock_guard<std::mutex> stateLock(g_mutex);
        cancelGeneration = RetireLocked(
            g_slots[playerNum], reason ? reason : "cancelled", true,
            -1, -1, AutoActionMotionOutcome::Cancelled);
    }
    if (cancelGeneration != 0) {
        CancelAutoActionMotionTransactionIfGeneration(
            playerNum, cancelGeneration,
            reason ? reason : "Kaori 44~66 cancelled");
    }
}

void CancelAll(const char* reason) {
    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    for (int playerNum = 1; playerNum <= 2; ++playerNum) {
        uint64_t cancelGeneration = 0;
        {
            std::lock_guard<std::mutex> stateLock(g_mutex);
            cancelGeneration = RetireLocked(
                g_slots[playerNum], reason ? reason : "cancelled", true,
                -1, -1, AutoActionMotionOutcome::Cancelled);
        }
        if (cancelGeneration != 0) {
            CancelAutoActionMotionTransactionIfGeneration(
                playerNum, cancelGeneration,
                reason ? reason : "Kaori 44~66 cancelled");
        }
    }
    ReleaseAllScopedInputReservations(
        ScopedInputOwner::KaoriRecoilDucking);
}

}
