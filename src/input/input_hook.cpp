#include "../include/input/input_hook.h"
#include "../include/input/input_motion.h"
#include "../include/core/memory.h"
#include "../include/core/logger.h"
#include "../include/core/constants.h"
#include "../include/utils/utilities.h"

#include "../include/input/input_core.h"

#include "../3rdparty/minhook/include/MinHook.h"
#include "../include/game/practice_patch.h"
#include "../include/game/game_state.h"
#include "../include/input/input_buffer.h" // for g_bufferFreezingActive
#include "../include/input/input_freeze.h"
#include "../include/input/immediate_input.h"
#include "../include/utils/minhook_utils.h"
#include "../include/utils/xinput_shim.h"
#include "../include/game/auto_action.h"
#include "../include/game/auto_action_charge.h"
#include "../include/game/kaori_recoil_duck.h"
#include "../include/game/macro_controller.h"
#include "../include/input/injection_control.h"
#include "../include/input/auto_action_motion_transaction.h"
#include "../include/input/auto_action_motion_policy.h"
#include "../include/input/motion_pattern.h"
#include "../include/input/scoped_input_reservation.h"
#include "../include/input/physical_poll_sample_policy.h"
#include <windows.h>
#include <vector>
#include <atomic>
#include <array>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <mutex>
#include <algorithm>
#include <utility>

#ifndef EFZ_ENABLE_AUTO_ACTION_WAKE_TRACE
#define EFZ_ENABLE_AUTO_ACTION_WAKE_TRACE 0
#endif

static InputHookPolicy::TutorialP2InputSource TutorialP2Source();

static std::string FormatHexByte(uint8_t value) {
    std::ostringstream oss;
    oss << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(value);
    return oss.str();
}

static uint8_t g_lastInjectedMask[3] = {0, 0, 0}; // Index 0 unused, 1 for P1, 2 for P2
static bool g_wasBypassBuffered[3] = { false, false, false };

std::atomic<bool> g_forceBypass[3] = { false, false, false };

std::atomic<bool> g_pollOverrideActive[3] = { false, false, false };
std::atomic<uint8_t> g_pollOverrideMask[3] = { 0, 0, 0 };
static std::atomic<uint32_t> g_pollOverrideHitCount[3] = { 0, 0, 0 };
static std::atomic<uint8_t> g_lastPolledMask[3] = { 0, 0, 0 };
static InputPollAttackEdgeJournalPolicy::Journal<
    kInputPollAttackEdgeJournalCapacity> g_pollAttackEdgeJournal[3];
static std::atomic<uint32_t> g_inputPollSerial[3] = { 0, 0, 0 };
static std::atomic<bool> g_observePhysicalPoll[3] = { false, false, false };
static std::atomic<bool> g_observedPhysicalPollValid[3] = { false, false, false };
static std::atomic<uint32_t> g_observedPhysicalPollSample[3] = { 0, 0, 0 };
// Hook-side cache separate from ImmediateInput's own state; a takeover must retire it.
static std::atomic<uint8_t> g_lastHookImmediateDesired[3] = { 0, 0, 0 };

namespace {

bool InputHooksReadyForTransactions();

constexpr uintptr_t kPlayerCpuFlagOffset[3] = {0, 4931, 4932};
constexpr uintptr_t kNativeHeldButtonBase = 400;
constexpr uintptr_t kNativeAutoBlockTimerOffset = 334;
constexpr uintptr_t kNativeDirectionalHeldOffset = 336;
constexpr uint16_t kNoMotionToken = 99;
constexpr int kMaxBlockedConsumerVisits = 240;
constexpr int kMaxAuthoredConsumerPasses = 240;

enum class AutoActionMotionTxnPhase : uint8_t {
    Idle,
    Prepared,
    AwaitingConsumer,
    CleanupPending,
};

struct AutoActionMotionTxnState {
    std::recursive_mutex mutex;
    AutoActionMotionTxnPhase phase{AutoActionMotionTxnPhase::Idle};
    uint64_t generation{0};
    uintptr_t gameState{0};
    uintptr_t character{0};
    uint8_t cpuFlag{0};
    uint32_t initialAiFlag{0};
    int motionType{MOTION_NONE};
    int buttonMask{0};
    bool genericPattern{false};
    bool patternFacingRight{true};
    std::vector<uint8_t> pattern;
    int maxConsumerPasses{1};
    int consumerPasses{0};
    int blockedConsumerVisits{0};
    bool consumerBoundaryVisited{false};

    bool pollActive{false};
    bool pollObserved{false};
    uint8_t pollMask{0};
    uint16_t headBefore{0};
    uint16_t headAfter{0};
    uint16_t ownedStart{0};
    uint16_t ownedLength{0};
    uint16_t tokenAfterProducer{kNoMotionToken};
    short producerMove{-1};
    short producerFrame{-1};
    bool ownsRuntimeState{false};
    bool aiRestorePending{false};
    bool maintenanceProducerRan{false};
    AutoActionMotionOutcome terminalOutcome{
        AutoActionMotionOutcome::Unknown};

    std::array<uint32_t, 4> heldBefore{};
    uint16_t autoBlockTimerBefore{0};
    uint32_t directionalHeldBefore{0};
};

AutoActionMotionTxnState s_motionTxn[3];
std::atomic<bool> s_motionTxnActive[3] = {false, false, false};
std::atomic<uint64_t> s_nextMotionTxnGeneration[3] = {1, 1, 1};
constexpr size_t kMotionOutcomeHistorySize = 32;
struct MotionOutcomeRecord {
    uint64_t generation{0};
    AutoActionMotionOutcome outcome{
        AutoActionMotionOutcome::Unknown};
};
std::array<std::array<MotionOutcomeRecord, kMotionOutcomeHistorySize>, 3>
    s_motionOutcomeHistory{};
size_t s_motionOutcomeCursor[3] = {0, 0, 0};

bool IsValidTransactionPlayer(int playerNum) {
    return playerNum == 1 || playerNum == 2;
}

void RecordMotionOutcomeLocked(int playerNum, uint64_t generation,
                               AutoActionMotionOutcome outcome) {
    if (!IsValidTransactionPlayer(playerNum)) return;
    if (generation == 0) return;
    auto& history = s_motionOutcomeHistory[playerNum];
    for (auto& record : history) {
        if (record.generation == generation) {
            record.outcome = outcome;
            return;
        }
    }
    history[s_motionOutcomeCursor[playerNum]] =
        MotionOutcomeRecord{generation, outcome};
    s_motionOutcomeCursor[playerNum] =
        (s_motionOutcomeCursor[playerNum] + 1) % kMotionOutcomeHistorySize;
}

AutoActionMotionOutcome FindMotionOutcomeLocked(int playerNum,
                                                uint64_t generation) {
    if (!IsValidTransactionPlayer(playerNum) || generation == 0) {
        return AutoActionMotionOutcome::Unknown;
    }
    for (const auto& record : s_motionOutcomeHistory[playerNum]) {
        if (record.generation == generation) return record.outcome;
    }
    return AutoActionMotionOutcome::Unknown;
}

// A lost AI restore can strand a fighter in human control; payload guarded by g_p2ControlMutex.
struct PendingAiRestore {
    uintptr_t gameState{0};
    uintptr_t character{0};
    uint32_t aiFlag{0};
};

PendingAiRestore s_pendingAiRestore[3];
std::atomic<bool> s_pendingAiRestoreActive[3] = {false, false, false};

const char* MotionTxnPhaseName(AutoActionMotionTxnPhase phase) {
    switch (phase) {
        case AutoActionMotionTxnPhase::Idle: return "idle";
        case AutoActionMotionTxnPhase::Prepared: return "prepared";
        case AutoActionMotionTxnPhase::AwaitingConsumer: return "await-consumer";
        case AutoActionMotionTxnPhase::CleanupPending: return "cleanup-pending";
    }
    return "unknown";
}

bool ReadCurrentWorld(int playerNum, uintptr_t& gameState,
                      uintptr_t& character) {
    gameState = 0;
    character = 0;
    if (!IsValidTransactionPlayer(playerNum)) return false;
    const uintptr_t base = GetEFZBase();
    const uintptr_t playerOffset = playerNum == 1
        ? EFZ_BASE_OFFSET_P1 : EFZ_BASE_OFFSET_P2;
    return base != 0 &&
        SafeReadMemory(base + EFZ_BASE_OFFSET_GAME_STATE,
                       &gameState, sizeof(gameState)) && gameState != 0 &&
        SafeReadMemory(base + playerOffset,
                       &character, sizeof(character)) && character != 0;
}

void ClearPendingAiRestoreLocked(int playerNum) {
    if (!IsValidTransactionPlayer(playerNum)) return;
    s_pendingAiRestore[playerNum] = PendingAiRestore{};
    s_pendingAiRestoreActive[playerNum].store(false,
                                               std::memory_order_release);
}

void RememberPendingAiRestoreLocked(int playerNum, uintptr_t gameState,
                                    uintptr_t character, uint32_t aiFlag) {
    if (!IsValidTransactionPlayer(playerNum)) return;
    PendingAiRestore& pending = s_pendingAiRestore[playerNum];
    pending.gameState = gameState;
    pending.character = character;
    pending.aiFlag = aiFlag;
    s_pendingAiRestoreActive[playerNum].store(true,
                                              std::memory_order_release);
}

bool RetryPendingAiRestoreLocked(int playerNum, const char* context) {
    if (!IsValidTransactionPlayer(playerNum)) return false;
    if (!s_pendingAiRestoreActive[playerNum].load(
            std::memory_order_acquire)) {
        return true;
    }

    // Never replay an offline snapshot once online mode owns controller setup.
    if (g_onlineModeActive.load(std::memory_order_acquire)) {
        LogOut("[AA_INPUT_TXN] P" + std::to_string(playerNum) +
                   " discarded pending offline AI restore during netplay", true);
        ClearPendingAiRestoreLocked(playerNum);
        return true;
    }

    const PendingAiRestore& pending = s_pendingAiRestore[playerNum];
    uintptr_t gameState = 0;
    uintptr_t character = 0;
    if (!ReadCurrentWorld(playerNum, gameState, character) ||
        gameState != pending.gameState || character != pending.character) {
        LogOut("[AA_INPUT_TXN] P" + std::to_string(playerNum) +
                   " discarded stale pending AI restore after world change", true);
        ClearPendingAiRestoreLocked(playerNum);
        return true;
    }

    const uint32_t desired = pending.aiFlag;
    if (!SafeWriteMemory(character + AI_CONTROL_FLAG_OFFSET,
                         &desired, sizeof(desired))) {
        LogOut("[AA_INPUT_TXN] P" + std::to_string(playerNum) +
                   " pending AI restore retry failed context=" +
                   std::string(context ? context : "unspecified"),
               true);
        return false;
    }

    ClearPendingAiRestoreLocked(playerNum);
    LogOut("[AA_INPUT_TXN] P" + std::to_string(playerNum) +
               " pending AI restore completed context=" +
               std::string(context ? context : "unspecified"),
           true);
    return true;
}

bool SameMotionTxnWorldLocked(int playerNum) {
    if (!IsValidTransactionPlayer(playerNum)) return false;
    const AutoActionMotionTxnState& txn = s_motionTxn[playerNum];
    uintptr_t gameState = 0;
    uintptr_t character = 0;
    return ReadCurrentWorld(playerNum, gameState, character) &&
           gameState == txn.gameState && character == txn.character;
}

void ResetMotionTxnLocked(int playerNum) {
    if (!IsValidTransactionPlayer(playerNum)) return;
    AutoActionMotionTxnState& txn = s_motionTxn[playerNum];
    txn.phase = AutoActionMotionTxnPhase::Idle;
    txn.generation = 0;
    txn.gameState = 0;
    txn.character = 0;
    txn.cpuFlag = 0;
    txn.initialAiFlag = 0;
    txn.motionType = MOTION_NONE;
    txn.buttonMask = 0;
    txn.genericPattern = false;
    txn.patternFacingRight = true;
    txn.pattern.clear();
    txn.maxConsumerPasses = 1;
    txn.consumerPasses = 0;
    txn.blockedConsumerVisits = 0;
    txn.consumerBoundaryVisited = false;
    txn.pollActive = false;
    txn.pollObserved = false;
    txn.pollMask = 0;
    txn.headBefore = 0;
    txn.headAfter = 0;
    txn.ownedStart = 0;
    txn.ownedLength = 0;
    txn.tokenAfterProducer = kNoMotionToken;
    txn.producerMove = -1;
    txn.producerFrame = -1;
    txn.ownsRuntimeState = false;
    txn.aiRestorePending = false;
    txn.maintenanceProducerRan = false;
    txn.terminalOutcome = AutoActionMotionOutcome::Unknown;
    txn.heldBefore.fill(0);
    txn.autoBlockTimerBefore = 0;
    txn.directionalHeldBefore = 0;
    s_motionTxnActive[playerNum].store(false, std::memory_order_release);
}

bool WriteRingSpan(uintptr_t character, uint16_t start,
                   const uint8_t* data, uint16_t length) {
    if (length == 0) return true;
    const uint16_t first = static_cast<uint16_t>(
        std::min<uint16_t>(length, INPUT_BUFFER_SIZE - start));
    const uint16_t second = static_cast<uint16_t>(length - first);
    bool ok = SafeWriteMemory(character + INPUT_BUFFER_OFFSET + start,
                              data, first);
    if (second != 0) {
        ok = SafeWriteMemory(character + INPUT_BUFFER_OFFSET,
                             data + first, second) && ok;
    }
    return ok;
}

bool ClearRingSpan(uintptr_t character, uint16_t start, uint16_t length) {
    if (length == 0) return true;
    std::vector<uint8_t> neutral(length, 0);
    return WriteRingSpan(character, start, neutral.data(), length);
}

bool CleanupMotionTxnLocked(
    int playerNum, const char* reason,
    AutoActionMotionOutcome outcome = AutoActionMotionOutcome::Failed,
    const EfzTmEntryV1* heldWorld = nullptr) {
    if (!IsValidTransactionPlayer(playerNum)) return true;
    AutoActionMotionTxnState& txn = s_motionTxn[playerNum];
    const uint64_t generation = txn.generation;
    const AutoActionMotionTxnPhase phase = txn.phase;
    if (txn.terminalOutcome == AutoActionMotionOutcome::Unknown) {
        txn.terminalOutcome = outcome;
    }
    const AutoActionMotionOutcome terminalOutcome = txn.terminalOutcome;
    const auto publishTerminalOutcome = [&]() {
        if (generation != 0) {
            RecordMotionOutcomeLocked(playerNum, generation, terminalOutcome);
        }
    };

    // Netplay is a hard ownership boundary: retire bookkeeping without touching the new owner's world.
    if (heldWorld && g_onlineModeActive.load(std::memory_order_acquire)) return false;
    if (g_onlineModeActive.load(std::memory_order_acquire)) {
        LogOut("[AA_INPUT_TXN] P" + std::to_string(playerNum) +
                   " retired offline gen=" +
                   std::to_string(generation) +
                   " without writes at netplay ownership boundary reason=" +
                   std::string(reason ? reason : "unspecified"),
               true);
        // Release ownership before publishing the terminal outcome; Accepted callers may resubmit immediately.
        ResetMotionTxnLocked(playerNum);
        publishTerminalOutcome();
        ClearPendingAiRestoreLocked(playerNum);
        return true;
    }

    const bool sameWorld = heldWorld
        ? txn.gameState == heldWorld->gameSystem &&
          txn.character == (playerNum == 1 ? heldWorld->player1 : heldWorld->player2)
        : SameMotionTxnWorldLocked(playerNum);
    // A held-world caller must retain a mismatched record; world replacement never discards its obligation.
    if (heldWorld && !sameWorld) return false;
    bool cleanupOk = true;
    if (sameWorld && txn.ownsRuntimeState) {
        const uintptr_t character = txn.character;
        const uint16_t noMotion = kNoMotionToken;
        const std::array<uint8_t, 3> noCommandState = {0, 0, 0};
        const std::array<uint8_t, 6> neutralImmediate = {0, 0, 0, 0, 0, 0};
        const std::array<uint8_t, 4> neutralButtons = {0, 0, 0, 0};
        const std::array<uint32_t, 4> neutralHeld = {0, 0, 0, 0};
        const bool preserveRestoredAiState =
            txn.maintenanceProducerRan && txn.initialAiFlag != 0;

        cleanupOk = ClearRingSpan(character, txn.ownedStart,
                                  txn.ownedLength) && cleanupOk;
        cleanupOk = SafeWriteMemory(character + MOTION_TOKEN_OFFSET,
                                    &noMotion, sizeof(noMotion)) && cleanupOk;
        cleanupOk = SafeWriteMemory(character + COMMAND_BUFFER_OFFSET,
                                    noCommandState.data(), noCommandState.size()) && cleanupOk;
        cleanupOk = preserveRestoredAiState
            ? (SafeWriteMemory(character + INPUT_BUTTON_A_OFFSET,
                               neutralButtons.data(), neutralButtons.size()) && cleanupOk)
            : (SafeWriteMemory(character + INPUT_HORIZONTAL_OFFSET,
                               neutralImmediate.data(), neutralImmediate.size()) && cleanupOk);
        const auto& held = txn.initialAiFlag == 0
            ? txn.heldBefore : neutralHeld;
        cleanupOk = SafeWriteMemory(character + kNativeHeldButtonBase,
                                    held.data(), sizeof(held)) && cleanupOk;
        if (!preserveRestoredAiState) {
            cleanupOk = SafeWriteMemory(character + kNativeAutoBlockTimerOffset,
                                        &txn.autoBlockTimerBefore,
                                        sizeof(txn.autoBlockTimerBefore)) && cleanupOk;
            cleanupOk = SafeWriteMemory(character + kNativeDirectionalHeldOffset,
                                        &txn.directionalHeldBefore,
                                        sizeof(txn.directionalHeldBefore)) && cleanupOk;
        }
        if (txn.aiRestorePending) {
            const bool aiRestored =
                SafeWriteMemory(character + AI_CONTROL_FLAG_OFFSET,
                                &txn.initialAiFlag,
                                sizeof(txn.initialAiFlag));
            if (!aiRestored) {
                RememberPendingAiRestoreLocked(
                    playerNum, txn.gameState, character, txn.initialAiFlag);
            } else {
                txn.aiRestorePending = false;
            }
            cleanupOk = aiRestored && cleanupOk;
        }
    }

    std::ostringstream oss;
    oss << "[AA_INPUT_TXN] P" << playerNum << " end gen=" << generation
        << " phase=" << MotionTxnPhaseName(phase)
        << " reason=" << (reason ? reason : "unspecified")
        << " sameWorld=" << (sameWorld ? 1 : 0)
        << " ownedMemory=" << (txn.ownsRuntimeState ? 1 : 0)
        << " aiMaintenance=" << (txn.maintenanceProducerRan ? 1 : 0)
        << " cleanup=" << (cleanupOk ? 1 : 0)
        << " head=" << txn.headBefore << "->" << txn.headAfter
        << " token=" << txn.tokenAfterProducer
        << " owned=" << txn.ownedStart << "+" << txn.ownedLength;
    LogOut(oss.str(), !cleanupOk || !sameWorld || detailedLogging.load());
    if (cleanupOk || !sameWorld) {
        ResetMotionTxnLocked(playerNum);
        publishTerminalOutcome();
        return true;
    }

    // Do not publish Idle while cleanup is unproven; matching hooks retry and suppress the consumer.
    txn.phase = AutoActionMotionTxnPhase::CleanupPending;
    s_motionTxnActive[playerNum].store(true, std::memory_order_release);
    return false;
}

AutoActionMotionSubmitResult QueueMotionPatternCommon(
                                 int playerNum,
                                 std::vector<uint8_t> pattern,
                                 bool patternFacingRight,
                                 bool genericPattern,
                                 int motionType,
                                 int buttonMask,
                                 int maxConsumerVisits,
                                 ScopedInputOwner reservationOwner,
                                 uint64_t reservationToken,
                                 uint64_t* generationOut) {
    if (generationOut) *generationOut = 0;
    if (!IsValidTransactionPlayer(playerNum) || pattern.empty() ||
        pattern.size() > INPUT_BUFFER_SIZE ||
        !InputHooksReadyForTransactions() ||
        g_onlineModeActive.load(std::memory_order_relaxed) || !IsMatchPhase()) {
        return AutoActionMotionSubmitResult::Invalid;
    }
    if (reservationToken != 0) {
        if (!ScopedInputReservationMatches(
                playerNum, reservationOwner,
                reservationToken)) {
            return AutoActionMotionSubmitResult::Invalid;
        }
    } else if (reservationOwner != ScopedInputOwner::None) {
        return AutoActionMotionSubmitResult::Invalid;
    } else if (IsScopedInputReserved(playerNum)) {
        return AutoActionMotionSubmitResult::Busy;
    }

    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    if (!InputHooksReadyForTransactions() ||
        g_onlineModeActive.load(std::memory_order_acquire) ||
        !IsMatchPhase()) {
        return AutoActionMotionSubmitResult::Invalid;
    }
    if (reservationToken != 0) {
        if (!ScopedInputReservationMatches(
                playerNum, reservationOwner,
                reservationToken)) {
            return AutoActionMotionSubmitResult::Invalid;
        }
    } else if (reservationOwner != ScopedInputOwner::None) {
        return AutoActionMotionSubmitResult::Invalid;
    } else if (IsScopedInputReserved(playerNum)) {
        return AutoActionMotionSubmitResult::Busy;
    }
    if (!RetryPendingAiRestoreLocked(playerNum, "queue preflight")) {
        return AutoActionMotionSubmitResult::Busy;
    }
    AutoActionMotionTxnState& txn = s_motionTxn[playerNum];
    std::lock_guard<std::recursive_mutex> txnLock(txn.mutex);
    if (!InputHooksReadyForTransactions() ||
        g_onlineModeActive.load(std::memory_order_acquire) ||
        !IsMatchPhase()) {
        return AutoActionMotionSubmitResult::Invalid;
    }
    const bool p2LegacyOwner = playerNum == 2 &&
        (g_p2ControlOverridden || TutorialP2ControlLeaseActive());
    if (txn.phase != AutoActionMotionTxnPhase::Idle || p2LegacyOwner ||
        (MacroController::IsExclusivePlayback() &&
         MacroController::GetPlaybackPlayer() == playerNum) ||
        g_pollOverrideActive[playerNum].load(std::memory_order_acquire) ||
        g_manualInputOverride[playerNum].load(std::memory_order_acquire) ||
        ImmediateInput::TutorialLeaseActive(playerNum) ||
        TutorialBufferFreezeLeaseActive() ||
        ImmediateInput::GetCurrentDesired(playerNum) != 0 ||
        ImmediateInput::GetRemainingTicks(playerNum) != 0 ||
        IsAutoActionNormalPulseActive(playerNum) ||
        IsAutoActionNormalPulseOwningImmediateRegisters(playerNum) ||
        GetMotionQueueSnapshot(playerNum).active ||
        TutorialMotionQueueLeaseActive(playerNum) ||
        (g_bufferFreezingActive.load(std::memory_order_acquire) &&
         (g_activeFreezePlayer.load(std::memory_order_acquire) == 0 ||
          g_activeFreezePlayer.load(std::memory_order_acquire) == playerNum))) {
        LogOut("[AA_INPUT_TXN] queue rejected: another P" +
                   std::to_string(playerNum) + " input owner is active", true);
        return AutoActionMotionSubmitResult::Busy;
    }

    uintptr_t gameState = 0;
    uintptr_t character = 0;
    if (!ReadCurrentWorld(playerNum, gameState, character)) {
        return AutoActionMotionSubmitResult::Invalid;
    }

    uint8_t cpuFlag = 0;
    uint32_t aiFlag = 0;
    if (!SafeReadMemory(gameState + kPlayerCpuFlagOffset[playerNum],
                        &cpuFlag, sizeof(cpuFlag)) ||
        !SafeReadMemory(character + AI_CONTROL_FLAG_OFFSET, &aiFlag, sizeof(aiFlag))) {
        return AutoActionMotionSubmitResult::Invalid;
    }

    // Retire the completed worker's latch only after every owner check passes; a failed admission must not neutral-write over other owners.
    if (!ImmediateInput::RetireCompletedBookkeeping(playerNum)) {
        return AutoActionMotionSubmitResult::Busy;
    }
    g_lastHookImmediateDesired[playerNum].store(0, std::memory_order_release);

    uint64_t generation = s_nextMotionTxnGeneration[playerNum].fetch_add(
        1, std::memory_order_acq_rel);
    if (generation == 0) {
        generation = s_nextMotionTxnGeneration[playerNum].fetch_add(
            1, std::memory_order_acq_rel);
    }
    txn.phase = AutoActionMotionTxnPhase::Prepared;
    txn.generation = generation;
    txn.gameState = gameState;
    txn.character = character;
    txn.cpuFlag = cpuFlag;
    txn.initialAiFlag = aiFlag;
    txn.motionType = motionType;
    txn.buttonMask = buttonMask;
    txn.genericPattern = genericPattern;
    txn.patternFacingRight = patternFacingRight;
    txn.pattern = std::move(pattern);
    txn.maxConsumerPasses = (std::max)(
        1, (std::min)(maxConsumerVisits, kMaxAuthoredConsumerPasses));
    txn.consumerPasses = 0;
    txn.blockedConsumerVisits = 0;
    txn.consumerBoundaryVisited = false;
    RecordMotionOutcomeLocked(
        playerNum, generation, AutoActionMotionOutcome::Pending);
    s_motionTxnActive[playerNum].store(true, std::memory_order_release);
    if (generationOut) *generationOut = generation;

    std::ostringstream oss;
    oss << "[AA_INPUT_TXN] P" << playerNum << " queued gen=" << generation
        << " motion=" << motionType
        << " button=0x" << std::hex << buttonMask << std::dec
        << " len=" << txn.pattern.size()
        << " wait=" << txn.maxConsumerPasses
        << " cpu=" << static_cast<int>(cpuFlag)
        << " ai=" << aiFlag;
    LogOut(oss.str(), true);
    return AutoActionMotionSubmitResult::Accepted;
}

} // namespace

AutoActionMotionSubmitResult SubmitAutoActionMotionTransaction(
    int playerNum, int motionType, int buttonMask, int maxConsumerVisits,
    uint64_t* generationOut) {
    if (!IsValidTransactionPlayer(playerNum)) {
        if (generationOut) *generationOut = 0;
        return AutoActionMotionSubmitResult::Invalid;
    }
    const bool facingRight = GetPlayerFacingDirection(playerNum);
    std::vector<uint8_t> pattern;
    if (!MotionPattern::Build(motionType, static_cast<uint8_t>(buttonMask),
                              facingRight, pattern)) {
        if (generationOut) *generationOut = 0;
        return AutoActionMotionSubmitResult::Invalid;
    }
    return QueueMotionPatternCommon(playerNum, std::move(pattern), facingRight,
                                    false, motionType, buttonMask,
                                    maxConsumerVisits, ScopedInputOwner::None,
                                    0, generationOut);
}

AutoActionMotionSubmitResult SubmitReservedAutoActionMotionTransaction(
    int playerNum, int motionType, int buttonMask, int maxConsumerVisits,
    uint64_t reservationToken, uint64_t* generationOut) {
    return SubmitScopedAutoActionMotionTransaction(
        playerNum, motionType, buttonMask, maxConsumerVisits,
        ScopedInputOwner::AutoActionCharge, reservationToken, generationOut);
}

AutoActionMotionSubmitResult SubmitScopedAutoActionMotionTransaction(
    int playerNum, int motionType, int buttonMask, int maxConsumerVisits,
    ScopedInputOwner reservationOwner, uint64_t reservationToken,
    uint64_t* generationOut) {
    if (!IsValidTransactionPlayer(playerNum) ||
        reservationOwner == ScopedInputOwner::None ||
        reservationToken == 0 ||
        !ScopedInputReservationMatches(
            playerNum, reservationOwner, reservationToken)) {
        if (generationOut) *generationOut = 0;
        return AutoActionMotionSubmitResult::Invalid;
    }
    const bool facingRight = GetPlayerFacingDirection(playerNum);
    std::vector<uint8_t> pattern;
    if (!MotionPattern::Build(motionType, static_cast<uint8_t>(buttonMask),
                              facingRight, pattern)) {
        if (generationOut) *generationOut = 0;
        return AutoActionMotionSubmitResult::Invalid;
    }
    return QueueMotionPatternCommon(playerNum, std::move(pattern), facingRight,
                                    false, motionType, buttonMask,
                                    maxConsumerVisits, reservationOwner,
                                    reservationToken,
                                    generationOut);
}

AutoActionMotionSubmitResult SubmitAutoActionPatternTransaction(
    int playerNum, const std::vector<uint8_t>& pattern,
    bool patternFacingRight, int maxConsumerVisits,
    uint64_t* generationOut) {
    return QueueMotionPatternCommon(playerNum, pattern, patternFacingRight,
                                    true, MOTION_NONE, 0,
                                    maxConsumerVisits, ScopedInputOwner::None,
                                    0, generationOut);
}

bool IsAutoActionMotionTransactionActive(int playerNum) {
    return IsValidTransactionPlayer(playerNum) &&
        s_motionTxnActive[playerNum].load(std::memory_order_acquire);
}

AutoActionMotionOutcome GetAutoActionMotionTransactionOutcome(
    int playerNum, uint64_t generation) {
    if (!IsValidTransactionPlayer(playerNum)) {
        return AutoActionMotionOutcome::Unknown;
    }
    std::lock_guard<std::recursive_mutex> lock(s_motionTxn[playerNum].mutex);
    return FindMotionOutcomeLocked(playerNum, generation);
}

bool CancelAutoActionMotionTransactionIfGeneration(
    int playerNum, uint64_t generation, const char* reason) {
    if (!IsValidTransactionPlayer(playerNum) || generation == 0) return false;

    // Match the producer/queue lock order; compare the generation under the transaction mutex.
    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    AutoActionMotionTxnState& txn = s_motionTxn[playerNum];
    std::lock_guard<std::recursive_mutex> lock(txn.mutex);
    if (!AutoActionMotionPolicy::OwnsCancellationGeneration(
            txn.phase != AutoActionMotionTxnPhase::Idle,
            txn.generation, generation)) {
        return false;
    }

    (void)CleanupMotionTxnLocked(
        playerNum, reason ? reason : "generation cancelled",
        AutoActionMotionOutcome::Cancelled);
    (void)RetryPendingAiRestoreLocked(
        playerNum, reason ? reason : "generation cancelled");
    if (txn.phase == AutoActionMotionTxnPhase::CleanupPending &&
        txn.generation == generation) {
        (void)CleanupMotionTxnLocked(
            playerNum, "generation cancellation cleanup retry",
            AutoActionMotionOutcome::Cancelled);
    }
    return true;
}

void CancelAutoActionMotionTransaction(int playerNum, const char* reason) {
    if (!IsValidTransactionPlayer(playerNum)) return;
    // Match the producer/queue lock order; cancellation can write controller memory.
    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    AutoActionMotionTxnState& txn = s_motionTxn[playerNum];
    std::lock_guard<std::recursive_mutex> lock(txn.mutex);
    if (txn.phase != AutoActionMotionTxnPhase::Idle) {
        CleanupMotionTxnLocked(
            playerNum, reason ? reason : "cancelled",
            AutoActionMotionOutcome::Cancelled);
    }
    (void)RetryPendingAiRestoreLocked(
        playerNum, reason ? reason : "cancelled");
    if (txn.phase == AutoActionMotionTxnPhase::CleanupPending) {
        (void)CleanupMotionTxnLocked(
            playerNum, "immediate cancellation cleanup retry");
    }
}

void CancelAllAutoActionMotionTransactions(const char* reason) {
    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    CancelAutoActionMotionTransaction(1, reason);
    CancelAutoActionMotionTransaction(2, reason);
}

P2AutoActionMotionSubmitResult SubmitP2AutoActionMotionTransaction(
    int motionType, int buttonMask, int consumerWaitPasses,
    uint64_t* generationOut) {
    return SubmitAutoActionMotionTransaction(
        2, motionType, buttonMask, consumerWaitPasses, generationOut);
}

P2AutoActionMotionSubmitResult SubmitP2AutoActionPatternTransaction(
    const std::vector<uint8_t>& pattern, bool patternFacingRight,
    int consumerWaitPasses, uint64_t* generationOut) {
    return SubmitAutoActionPatternTransaction(
        2, pattern, patternFacingRight, consumerWaitPasses, generationOut);
}

bool QueueP2AutoActionMotionTransaction(int motionType, int buttonMask,
                                        int consumerWaitPasses,
                                        uint64_t* generationOut) {
    return SubmitP2AutoActionMotionTransaction(
               motionType, buttonMask, consumerWaitPasses, generationOut) ==
           P2AutoActionMotionSubmitResult::Accepted;
}

bool QueueP2AutoActionPatternTransaction(const std::vector<uint8_t>& pattern,
                                         bool patternFacingRight,
                                         int consumerWaitPasses,
                                         uint64_t* generationOut) {
    return SubmitP2AutoActionPatternTransaction(
               pattern, patternFacingRight, consumerWaitPasses, generationOut) ==
           P2AutoActionMotionSubmitResult::Accepted;
}

bool IsP2AutoActionMotionTransactionActive() {
    return IsAutoActionMotionTransactionActive(2);
}

P2AutoActionMotionOutcome GetP2AutoActionMotionTransactionOutcome(
    uint64_t generation) {
    return GetAutoActionMotionTransactionOutcome(2, generation);
}

void CancelP2AutoActionMotionTransaction(const char* reason) {
    CancelAutoActionMotionTransaction(2, reason);
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

static short TraceReadMoveIDFromPtr(uintptr_t playerPtr) {
    short moveID = -1;
    if (playerPtr) {
        (void)SafeReadMemory(playerPtr + MOVE_ID_OFFSET, &moveID, sizeof(moveID));
    }
    return moveID;
}

static bool TraceInputHookWakeWindowActive(short p1MoveID, short p2MoveID) {
    static int s_traceWindow[3] = { 0, 0, 0 };
    static int s_lastUpdatedFrame = -1;
    const int now = frameCounter.load();

    if (s_lastUpdatedFrame != now) {
        s_lastUpdatedFrame = now;
        if (IsGroundtech(p1MoveID)) {
            s_traceWindow[1] = 90;
        } else if (s_traceWindow[1] > 0) {
            --s_traceWindow[1];
        }
        if (IsGroundtech(p2MoveID)) {
            s_traceWindow[2] = 90;
        } else if (s_traceWindow[2] > 0) {
            --s_traceWindow[2];
        }
    }

    return s_traceWindow[1] > 0 || s_traceWindow[2] > 0;
}

static void TraceAppendQueueState(std::ostringstream& oss, int playerNum) {
    const MotionQueueSnapshot queue = GetMotionQueueSnapshot(playerNum);
    oss << " p" << playerNum << "Queue=" << TraceBool(queue.active)
        << ":" << queue.index << "/" << queue.size
        << " qFrame=" << queue.frameCounter
        << " qMotion=" << queue.motionType;
}

static void TraceAppendInputState(std::ostringstream& oss, int playerNum) {
    oss << " p" << playerNum
        << " desired=" << static_cast<int>(ImmediateInput::GetCurrentDesired(playerNum))
        << " remTicks=" << ImmediateInput::GetRemainingTicks(playerNum)
        << " manual=" << TraceBool(g_manualInputOverride[playerNum].load(std::memory_order_relaxed))
        << " manualMask=" << static_cast<int>(g_manualInputMask[playerNum].load(std::memory_order_relaxed))
        << " immediateOnly=" << TraceBool(g_injectImmediateOnly[playerNum].load(std::memory_order_relaxed))
        << " forceBypass=" << TraceBool(g_forceBypass[playerNum].load(std::memory_order_relaxed))
        << " poll=" << TraceBool(g_pollOverrideActive[playerNum].load(std::memory_order_relaxed))
        << " pollMask=" << static_cast<int>(g_pollOverrideMask[playerNum].load(std::memory_order_relaxed))
        << " lastInjected=" << static_cast<int>(g_lastInjectedMask[playerNum]);
    TraceAppendQueueState(oss, playerNum);
}

static void TraceInputHookWakePhase(const char* phase, int playerNum, uintptr_t characterPtr, short entryMoveID, const char* exitPath) {
    const short p1MoveID = static_cast<short>(GetPlayerMoveID(1));
    const short p2MoveID = static_cast<short>(GetPlayerMoveID(2));
    if (!TraceInputHookWakeWindowActive(p1MoveID, p2MoveID)) return;

    const short charMoveID = TraceReadMoveIDFromPtr(characterPtr);
    const int internalFrame = frameCounter.load();
    std::ostringstream oss;
    oss << "[AA_WAKE_TRACE][INPUT_HOOK]"
        << " phase=" << (phase ? phase : "unknown")
        << " IF=" << internalFrame
        << " VF=" << (internalFrame / 3) << "." << TraceSubframeSuffix(internalFrame)
        << " hookP=" << playerNum
        << " entryMove=" << entryMoveID << "(" << TraceMoveClass(entryMoveID) << ")"
        << " charMove=" << charMoveID << "(" << TraceMoveClass(charMoveID) << ")"
        << " p1Move=" << p1MoveID << "(" << TraceMoveClass(p1MoveID) << ")"
        << " p2Move=" << p2MoveID << "(" << TraceMoveClass(p2MoveID) << ")"
        << " exitPath=" << (exitPath ? exitPath : "")
        << " freeze=" << TraceBool(g_bufferFreezingActive.load(std::memory_order_relaxed))
        << " freezeOwner=" << g_activeFreezePlayer.load(std::memory_order_relaxed)
        << " pendingRestore=" << TraceBool(g_pendingControlRestore.load(std::memory_order_relaxed))
        << " p2Override=" << TraceBool(g_p2ControlOverridden);
    TraceAppendInputState(oss, 1);
    TraceAppendInputState(oss, 2);
    LogOut(oss.str(), false);
}

struct InputHookWakeTraceScope {
    InputHookWakeTraceScope(int player, uintptr_t ptr)
        : playerNum(player), characterPtr(ptr), entryMoveID(TraceReadMoveIDFromPtr(ptr)) {
        TraceInputHookWakePhase("entry", playerNum, characterPtr, entryMoveID, exitLabel);
    }

    ~InputHookWakeTraceScope() {
        TraceInputHookWakePhase("exit", playerNum, characterPtr, entryMoveID, exitLabel);
    }

    void Mark(const char* phase) {
        TraceInputHookWakePhase(phase, playerNum, characterPtr, entryMoveID, exitLabel);
    }

    void SetExit(const char* label) {
        exitLabel = label;
    }

    int playerNum;
    uintptr_t characterPtr;
    short entryMoveID;
    const char* exitLabel = "exit";
};
#endif

void ResetInputPollOverrideHitCount(int playerNum) {
    if (playerNum != 1 && playerNum != 2) return;
    g_pollOverrideHitCount[playerNum].store(0, std::memory_order_release);
}

uint32_t GetInputPollOverrideHitCount(int playerNum) {
    if (playerNum != 1 && playerNum != 2) return 0;
    return g_pollOverrideHitCount[playerNum].load(std::memory_order_acquire);
}

uint8_t ConsumeInputPollAttackEdges(int playerNum) {
    uint32_t ignoredSerial = 0;
    return ConsumeInputPollAttackEdgeBatch(playerNum, ignoredSerial);
}

InputPollAttackEdgeDrainResult DrainInputPollAttackEdgeJournal(
    int playerNum,
    InputPollAttackEdgeEvent* eventsOut,
    std::size_t eventCapacity) {
    if (playerNum != 1 && playerNum != 2) {
        return InputPollAttackEdgeDrainResult{};
    }
    return g_pollAttackEdgeJournal[playerNum].Drain(eventsOut, eventCapacity);
}

void ResetInputPollAttackEdgeJournal(int playerNum) {
    if (playerNum != 1 && playerNum != 2) return;
    g_pollAttackEdgeJournal[playerNum].Reset();
}

uint8_t ConsumeInputPollAttackEdgeBatch(int playerNum, uint32_t& serialOut) {
    serialOut = 0;
    if (playerNum != 1 && playerNum != 2) return 0;

    InputPollAttackEdgeEvent events[kInputPollAttackEdgeJournalCapacity]{};
    const InputPollAttackEdgeDrainResult drain =
        DrainInputPollAttackEdgeJournal(playerNum, events,
                                        kInputPollAttackEdgeJournalCapacity);
    uint8_t aggregate = 0;
    for (std::size_t index = 0; index < drain.count; ++index) {
        aggregate = static_cast<uint8_t>(aggregate | events[index].mask);
        serialOut = events[index].serial;
    }
    return aggregate;
}

uint32_t GetInputPollSerial(int playerNum) {
    if (playerNum != 1 && playerNum != 2) return 0;
    return g_inputPollSerial[playerNum].load(std::memory_order_acquire);
}

void SetPollOverridePhysicalObservation(int playerNum, bool enabled) {
    if (playerNum != 1 && playerNum != 2) return;
    const bool wasEnabled = g_observePhysicalPoll[playerNum].exchange(
        enabled, std::memory_order_acq_rel);
    if (enabled && !wasEnabled) {
        g_observedPhysicalPollValid[playerNum].store(false, std::memory_order_release);
    }
}

bool IsPollOverridePhysicalObservationEnabled(int playerNum) {
    if (playerNum != 1 && playerNum != 2) return false;
    return g_observePhysicalPoll[playerNum].load(std::memory_order_acquire);
}

uint8_t GetLastObservedPhysicalPollMask(int playerNum) {
    if (playerNum != 1 && playerNum != 2) return 0xFF;
    if (!g_observedPhysicalPollValid[playerNum].load(std::memory_order_acquire)) {
        return 0xFF; // compatibility for older handshake policy callers
    }
    return PhysicalPollSamplePolicy::Mask(
        g_observedPhysicalPollSample[playerNum].load(
            std::memory_order_acquire));
}

bool TryGetLastObservedPhysicalPoll(int playerNum, uint8_t& maskOut,
                                    uint32_t* serialOut) {
    maskOut = 0;
    if (serialOut) *serialOut = 0;
    if (playerNum != 1 && playerNum != 2) return false;
    if (!g_observedPhysicalPollValid[playerNum].load(std::memory_order_acquire)) {
        return false;
    }
    const uint32_t sample = g_observedPhysicalPollSample[playerNum].load(
        std::memory_order_acquire);
    maskOut = PhysicalPollSamplePolicy::Mask(sample);
    if (serialOut) {
        *serialOut = PhysicalPollSamplePolicy::Serial(sample);
    }
    return true;
}

static std::atomic<bool> s_armedNeutralize[3] = { false, false, false };
static std::atomic<bool> s_doFullCleanup[3] = { false, false, false };
static std::atomic<uint16_t> s_lastHead[3] = { 0xFFFF, 0xFFFF, 0xFFFF };
static std::atomic<int> s_headStable[3] = { 0, 0, 0 };

void InputHook_ArmTokenNeutralize(int playerNum, bool alsoDoFullCleanup) {
    if (playerNum != 1 && playerNum != 2) return;
    s_armedNeutralize[playerNum].store(true, std::memory_order_relaxed);
    s_doFullCleanup[playerNum].store(alsoDoFullCleanup, std::memory_order_relaxed);
    s_lastHead[playerNum].store(0xFFFF, std::memory_order_relaxed);
    s_headStable[playerNum].store(0, std::memory_order_relaxed);
    if (detailedLogging.load()) {
        LogOut(std::string("[INPUT_HOOK] Armed token neutralize for P") + std::to_string(playerNum) +
               (alsoDoFullCleanup?" + full cleanup":""), true);
    }
}

static void MaybePerformTailCleanup(int playerNum) {
    if (playerNum != 1 && playerNum != 2) return;
    if (g_bufferFreezingActive.load()) {
        int owner = g_activeFreezePlayer.load();
        if (owner == playerNum || owner == 0) return;
    }
    if (s_armedNeutralize[playerNum].load(std::memory_order_relaxed)) {
        (void)NeutralizeMotionToken(playerNum);
        s_armedNeutralize[playerNum].store(false, std::memory_order_relaxed);
    }
    if (s_doFullCleanup[playerNum].load(std::memory_order_relaxed)) {
        uintptr_t pPtr = GetPlayerPointer(playerNum);
        if (!pPtr) return;
        uint16_t head = 0;
        if (!SafeReadMemory(pPtr + INPUT_BUFFER_INDEX_OFFSET, &head, sizeof(head))) return;
        uint16_t last = s_lastHead[playerNum].load(std::memory_order_relaxed);
        if (last == 0xFFFF || head != last) {
            s_lastHead[playerNum].store(head, std::memory_order_relaxed);
            s_headStable[playerNum].store(0, std::memory_order_relaxed);
            return;
        }
        int stable = s_headStable[playerNum].fetch_add(1, std::memory_order_relaxed) + 1;
        if (stable >= 2) {
            (void)FullCleanupAfterToggle(playerNum);
            s_doFullCleanup[playerNum].store(false, std::memory_order_relaxed);
            s_lastHead[playerNum].store(0xFFFF, std::memory_order_relaxed);
            s_headStable[playerNum].store(0, std::memory_order_relaxed);
        }
    }
}

typedef int(__thiscall* tProcessCharacterInput)(int characterPtr);
tProcessCharacterInput oProcessCharacterInput = nullptr;

// updateEntityState's vtable+16 call is the exact normal consumer, before later per-character updates replace the move.
typedef int(__thiscall* tUpdateEntityState)(int characterPtr);
tUpdateEntityState oUpdateEntityState = nullptr;

// processCharacterInput entry, RVA 0x11BE0 from the efz.exe base.
const uintptr_t PROCESS_INPUTS_FUNC_OFFSET = 0x11BE0;
const uintptr_t UPDATE_ENTITY_STATE_FUNC_OFFSET = 0x0F230;

// pollPlayerInputState(inputManager, playerIndex) returns the 8-bit unified mask; RVA 0x0CD00.
typedef int(__thiscall* tPollPlayerInputState)(int inputManagerPtr, unsigned int playerIndex);
static tPollPlayerInputState oPollPlayerInputState = nullptr;
static const uintptr_t POLL_INPUT_STATE_FUNC_OFFSET = 0x0CD00;

namespace {
std::atomic<bool> s_inputHooksCreated{false};
std::atomic<bool> s_inputHooksEnabled{false};
// Mission startup's poll owner is retired only by Mission::Engine after its handoff.
std::atomic<bool> s_p1StartupNeutralGate{false};

bool InputHooksReadyForTransactions() {
    return s_inputHooksCreated.load(std::memory_order_acquire) &&
           s_inputHooksEnabled.load(std::memory_order_acquire);
}

std::atomic<int> s_processInputPlayerContext{0};
std::atomic<bool> s_loggedPollContextReroute[3] = { false, false, false };
std::atomic<bool> s_loggedPollOverrideHumanForce[3] = { false, false, false };
uintptr_t s_processTargetAddr = 0;
uintptr_t s_pollTargetAddr = 0;
uintptr_t s_stateUpdateTargetAddr = 0;

enum class NormalPulseTransport : uint8_t {
    Unselected,
    NativePoll,
    AiPostWrite,
};

struct NormalPulseRuntimeSlot {
    std::recursive_mutex mutex;
    NormalInputPolicy::PulseState state;
    NormalPulseTransport transport{NormalPulseTransport::Unselected};
    uintptr_t gameState{0};
    uintptr_t character{0};
    bool deliveredAnyPhase{false};
    // Keep the raw-register lane reserved until the consumer hook releases it.
    bool rawReleaseGuard{false};
    // A successful poll/post-write only proves hand-off; confirm Press after updateEntityState's consumer.
    bool pressAwaitingConfirmation{false};
    short pressBeforeMove{-1};
    short pressBeforeFrame{-1};
    uint8_t pressAttempts{0};
    bool retryNeutralPending{false};
    bool abortAfterRetryNeutral{false};
    // A failed same-world scrub remains an exclusive raw-lane obligation.
    bool cleanupPending{false};
    NormalInputPolicy::Intent cleanupIntent{};
    struct OutcomeRecord {
        uint64_t generation{0};
        AutoActionNormalPulseOutcome outcome{
            AutoActionNormalPulseOutcome::Unknown};
    };
    std::array<OutcomeRecord, 32> outcomeHistory{};
    size_t outcomeCursor{0};
};

NormalPulseRuntimeSlot s_normalPulse[3];
std::atomic<bool> s_normalRawRegisterOwner[3] = { false, false, false };

void RecordNormalPulseOutcomeLocked(
    NormalPulseRuntimeSlot& slot,
    uint64_t generation,
    AutoActionNormalPulseOutcome outcome) {
    if (generation == 0 || outcome == AutoActionNormalPulseOutcome::Unknown) {
        return;
    }
    for (auto& record : slot.outcomeHistory) {
        if (record.generation != generation) continue;
        // Published terminal results are never rewritten.
        if (record.outcome != AutoActionNormalPulseOutcome::Pending &&
            record.outcome != AutoActionNormalPulseOutcome::Unknown) {
            return;
        }
        record.outcome = outcome;
        return;
    }
    slot.outcomeHistory[slot.outcomeCursor] = {generation, outcome};
    slot.outcomeCursor =
        (slot.outcomeCursor + 1) % slot.outcomeHistory.size();
}

AutoActionNormalPulseOutcome FindNormalPulseOutcomeLocked(
    const NormalPulseRuntimeSlot& slot, uint64_t generation) {
    if (generation == 0) return AutoActionNormalPulseOutcome::Unknown;
    for (const auto& record : slot.outcomeHistory) {
        if (record.generation == generation) return record.outcome;
    }
    return AutoActionNormalPulseOutcome::Unknown;
}

void CancelNormalPulseGenerationsLocked(NormalPulseRuntimeSlot& slot) {
    const auto current = slot.state.Current();
    if (current) {
        RecordNormalPulseOutcomeLocked(
            slot, current.generation,
            AutoActionNormalPulseOutcome::Cancelled);
    }
    RecordNormalPulseOutcomeLocked(
        slot, slot.state.PendingGeneration(),
        AutoActionNormalPulseOutcome::Cancelled);
}

struct NormalProcessRoute {
    bool active{false};
    bool observed{false};
    bool nativePollObserved{false};
    int player{0};
    uint8_t mask{0};
    bool retryNeutral{false};
    bool abortAfterWrite{false};
    NormalPulseTransport transport{NormalPulseTransport::Unselected};
    NormalInputPolicy::Snapshot snapshot{};
};

// HookedPoll runs synchronously inside the matching ProcessCharacterInput call; thread-local context is an exact acknowledgement.
// Explicit Win32 TLS: implicit TLS is not allocated for LoadLibrary'd DLLs on XP. Slot holds a caller-stack route or null; never freed.
const DWORD s_nativeNormalPollRouteTls = TlsAlloc();

NormalProcessRoute* ActiveNativeNormalPollRoute() {
    if (s_nativeNormalPollRouteTls == TLS_OUT_OF_INDEXES) return nullptr;
    const DWORD lastError = GetLastError();
    auto* route = static_cast<NormalProcessRoute*>(
        TlsGetValue(s_nativeNormalPollRouteTls));
    SetLastError(lastError);
    return route;
}

void BindNativeNormalPollRoute(NormalProcessRoute* route) {
    if (s_nativeNormalPollRouteTls == TLS_OUT_OF_INDEXES) return;
    const DWORD lastError = GetLastError();
    (void)TlsSetValue(s_nativeNormalPollRouteTls, route);
    SetLastError(lastError);
}

constexpr uintptr_t kNativeHeldButtonOffsets[4] = {400u, 404u, 408u, 412u};
constexpr uintptr_t kReplayIoModeOffset = 82563u;

bool ResolveInputHookTargets(uintptr_t& targetAddr,
                             uintptr_t& pollAddr,
                             uintptr_t& stateUpdateAddr) {
    uintptr_t base = GetEFZBase();
    if (!base) {
        LogOut("[INPUT_HOOK] Failed to get game base address. Hook not installed.", true);
        return false;
    }

    targetAddr = base + PROCESS_INPUTS_FUNC_OFFSET;
    pollAddr = base + POLL_INPUT_STATE_FUNC_OFFSET;
    stateUpdateAddr = base + UPDATE_ENTITY_STATE_FUNC_OFFSET;
    return true;
}

bool ValidateStateUpdateTarget(uintptr_t target) {
    // Retail/Memorial 1.20 updateEntityState @ 0x0040F230; never detour an unvalidated body.
    constexpr std::array<uint8_t, 12> expected = {
        0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C,
        0x89, 0x4D, 0xFC, 0x8B, 0x45, 0xFC,
    };
    std::array<uint8_t, expected.size()> actual{};
    if (!target ||
        !SafeReadMemory(target, actual.data(), actual.size()) ||
        actual != expected) {
        LogOut("[INPUT_HOOK] updateEntityState signature mismatch at " +
               FormatHexAddress(target) +
               "; authoritative normal delivery disabled", true);
        return false;
    }
    return true;
}

} // namespace

// The same adjacent 16-byte control maps Revival's SwitchPlayers swaps in EFZ's input-manager object.
static std::atomic<bool> g_loggedRoutingStateOnce{false};
namespace {

constexpr uintptr_t kInputManagerObjectRva = 0x003B0968u;
constexpr uintptr_t kP1BindingOffset = 448u;
constexpr size_t kBindingCount = 8u;

using BindingBlock = std::array<uint16_t, kBindingCount>;

struct BindingPair {
    BindingBlock p1{};
    BindingBlock p2{};
};
static_assert(sizeof(BindingPair) == 32, "EFZ control-map pair must remain exactly 32 bytes");

struct BindingSwapState {
    bool active{false};
    BindingPair baseline{};
};

std::mutex g_bindingSwapMutex;
BindingSwapState g_bindingSwapState;

bool BindingPairsEqual(const BindingPair& lhs, const BindingPair& rhs) {
    return std::memcmp(&lhs, &rhs, sizeof(BindingPair)) == 0;
}

BindingPair MakeSwappedBindingPair(const BindingPair& source) {
    BindingPair result{};
    result.p1 = source.p2;
    result.p2 = source.p1;
    return result;
}

uintptr_t GetBindingPairAddress() {
    const uintptr_t base = GetEFZBase();
    return base ? base + kInputManagerObjectRva + kP1BindingOffset : 0;
}

bool ReadBindingPair(BindingPair& outPair) {
    const uintptr_t address = GetBindingPairAddress();
    return address != 0 && SafeReadMemory(address, &outPair, sizeof(outPair));
}

bool WriteAndVerifyBindingPair(const BindingPair& pair) {
    const uintptr_t address = GetBindingPairAddress();
    if (!address || !SafeWriteMemory(address, &pair, sizeof(pair))) {
        return false;
    }

    BindingPair verify{};
    return SafeReadMemory(address, &verify, sizeof(verify))
        && BindingPairsEqual(verify, pair);
}

std::string FormatBindingBlock(const BindingBlock& block) {
    std::ostringstream oss;
    oss << std::hex << std::uppercase << std::setfill('0');
    for (size_t i = 0; i < block.size(); ++i) {
        if (i != 0) oss << ' ';
        oss << std::setw(4) << static_cast<unsigned int>(block[i]);
    }
    return oss.str();
}

struct ScopedProcessInputPlayerContext {
    explicit ScopedProcessInputPlayerContext(int playerNum)
        : previous(s_processInputPlayerContext.load(std::memory_order_relaxed)) {
        s_processInputPlayerContext.store(playerNum, std::memory_order_relaxed);
    }

    ~ScopedProcessInputPlayerContext() {
        s_processInputPlayerContext.store(previous, std::memory_order_relaxed);
    }

    int previous;
};

int CallOriginalProcessCharacterInputWithContext(int characterPtr, int playerNum) {
    ScopedProcessInputPlayerContext context(playerNum);
    return oProcessCharacterInput ? oProcessCharacterInput(characterPtr) : 0;
}

bool TryRunMotionTransactionProducer(int playerNum, int characterPtr,
                                     int& resultOut) {
    if (!IsValidTransactionPlayer(playerNum) ||
        !s_motionTxnActive[playerNum].load(std::memory_order_acquire) ||
        characterPtr == 0) {
        return false;
    }

    // Recursive txn mutex: HookedPoll re-enters it from the original processCharacterInput on this thread.
    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    const bool pendingAiReady =
        RetryPendingAiRestoreLocked(playerNum, "producer preflight");
    AutoActionMotionTxnState& txn = s_motionTxn[playerNum];
    std::lock_guard<std::recursive_mutex> txnLock(txn.mutex);
    if (static_cast<uintptr_t>(characterPtr) != txn.character ||
        !SameMotionTxnWorldLocked(playerNum) ||
        g_onlineModeActive.load(std::memory_order_relaxed)) {
        CleanupMotionTxnLocked(
            playerNum, "producer world/online mismatch");
        return false;
    }

    if (!pendingAiReady) {
        (void)CleanupMotionTxnLocked(
            playerNum, "pending controller restore retry");
        resultOut = characterPtr;
        return true;
    }

    if (txn.phase == AutoActionMotionTxnPhase::CleanupPending) {
        const bool cleaned =
            CleanupMotionTxnLocked(
                playerNum, "next producer cleanup retry");
        if (!cleaned) {
            resultOut = characterPtr;
            return true;
        }
        return false;
    }

    if (txn.phase == AutoActionMotionTxnPhase::AwaitingConsumer) {
        // Keep the real AI flag so Practice keeps its block direction; restore the token after the AI producer, keep buttons/commands neutral.
        resultOut = CallOriginalProcessCharacterInputWithContext(
            characterPtr, playerNum);
        txn.maintenanceProducerRan = true;
        const uintptr_t character = txn.character;
        const std::array<uint8_t, 4> noButtons = {0, 0, 0, 0};
        const std::array<uint8_t, 3> noCommandState = {0, 0, 0};
        const bool maintained =
            SafeWriteMemory(character + MOTION_TOKEN_OFFSET,
                            &txn.tokenAfterProducer,
                            sizeof(txn.tokenAfterProducer)) &&
            SafeWriteMemory(character + INPUT_BUTTON_A_OFFSET,
                            noButtons.data(), noButtons.size()) &&
            SafeWriteMemory(character + COMMAND_BUFFER_OFFSET,
                            noCommandState.data(), noCommandState.size());
        if (!maintained) {
            CleanupMotionTxnLocked(
                playerNum, "pending token maintenance failed");
        } else if (detailedLogging.load()) {
            LogOut("[AA_INPUT_TXN] P" + std::to_string(playerNum) +
                       " maintained gen=" + std::to_string(txn.generation) +
                       " token=" +
                       std::to_string(txn.tokenAfterProducer) +
                       " with native controller restored",
                   true);
        }
        return true;
    }
    if (txn.phase != AutoActionMotionTxnPhase::Prepared) return false;

    std::vector<uint8_t> pattern;
    const bool facingRight = GetPlayerFacingDirection(playerNum);
    if (txn.genericPattern) {
        pattern = txn.pattern;
        if (facingRight != txn.patternFacingRight) {
            MotionPattern::MirrorHorizontally(pattern);
        }
    } else if (!MotionPattern::Build(
                   txn.motionType,
                   static_cast<uint8_t>(txn.buttonMask),
                   facingRight, pattern)) {
        CleanupMotionTxnLocked(playerNum, "pattern rebuild failed");
        return false;
    }
    if (pattern.empty() || pattern.size() > INPUT_BUFFER_SIZE) {
        CleanupMotionTxnLocked(playerNum, "invalid pattern length");
        return false;
    }

    const uintptr_t character = txn.character;
    uint16_t head = 0;
    uint8_t cpuBefore = 0;
    uint32_t aiBefore = 0;
    bool preflightOk =
        SafeReadMemory(character + INPUT_BUFFER_INDEX_OFFSET, &head, sizeof(head)) &&
        SafeReadMemory(txn.gameState + kPlayerCpuFlagOffset[playerNum],
                       &cpuBefore, sizeof(cpuBefore)) &&
        SafeReadMemory(character + AI_CONTROL_FLAG_OFFSET, &aiBefore, sizeof(aiBefore)) &&
        SafeReadMemory(character + kNativeHeldButtonBase,
                       txn.heldBefore.data(), sizeof(txn.heldBefore)) &&
        SafeReadMemory(character + kNativeAutoBlockTimerOffset,
                       &txn.autoBlockTimerBefore,
                       sizeof(txn.autoBlockTimerBefore)) &&
        SafeReadMemory(character + kNativeDirectionalHeldOffset,
                       &txn.directionalHeldBefore,
                       sizeof(txn.directionalHeldBefore));
    if (!preflightOk || head >= INPUT_BUFFER_SIZE) {
        CleanupMotionTxnLocked(playerNum, "producer preflight failed");
        return false;
    }
    // Capture controller state at the write boundary; queue-time values are diagnostics only.
    txn.cpuFlag = cpuBefore;
    txn.initialAiFlag = aiBefore;
    txn.ownsRuntimeState = true;

    const AutoActionMotionPolicy::RingPlacement placement =
        AutoActionMotionPolicy::PlanRingPlacement(
            head, pattern.size(), INPUT_BUFFER_SIZE);
    if (!placement.valid) {
        CleanupMotionTxnLocked(playerNum, "ring placement failed");
        return false;
    }
    const uint16_t prefixLength = placement.prefixLength;
    const uint16_t start = placement.prefixStart;
    const uint16_t noMotion = kNoMotionToken;
    const std::array<uint8_t, 3> noCommandState = {0, 0, 0};
    const std::array<uint32_t, 4> noHeld = {0, 0, 0, 0};
    txn.headBefore = head;
    txn.headAfter = head;
    txn.ownedStart = placement.ownedStart;
    txn.ownedLength = placement.ownedLength;
    bool staged =
        ClearRingSpan(character, placement.ownedStart,
                      placement.ownedLength) &&
        SafeWriteMemory(character + MOTION_TOKEN_OFFSET,
                        &noMotion, sizeof(noMotion)) &&
        SafeWriteMemory(character + COMMAND_BUFFER_OFFSET,
                        noCommandState.data(), noCommandState.size()) &&
        SafeWriteMemory(character + kNativeHeldButtonBase,
                        noHeld.data(), sizeof(noHeld));
    if (prefixLength != 0) {
        staged = WriteRingSpan(character, start, pattern.data(), prefixLength) && staged;
    }
    if (!staged) {
        CleanupMotionTxnLocked(playerNum, "history staging failed");
        return false;
    }

    txn.pollMask = pattern.back();
    txn.pollObserved = false;
    txn.pollActive = true;
    (void)SafeReadMemory(character + MOVE_ID_OFFSET,
                         &txn.producerMove, sizeof(txn.producerMove));
    (void)SafeReadMemory(character + CURRENT_FRAME_INDEX_OFFSET,
                         &txn.producerFrame, sizeof(txn.producerFrame));

    const uint32_t human = 0;
    const bool humanized = aiBefore == 0 ||
        SafeWriteMemory(character + AI_CONTROL_FLAG_OFFSET,
                        &human, sizeof(human));
    if (!humanized) {
        txn.pollActive = false;
        CleanupMotionTxnLocked(playerNum, "temporary human switch failed");
        return false;
    }
    txn.aiRestorePending = aiBefore != 0;

    resultOut = CallOriginalProcessCharacterInputWithContext(
        characterPtr, playerNum);
    txn.pollActive = false;

    // Restore the AI flag immediately, before updateEntityState/collision; keep the token/direction until the consumer.
    bool restored = SafeWriteMemory(character + AI_CONTROL_FLAG_OFFSET,
                                    &aiBefore, sizeof(aiBefore));
    if (restored) {
        txn.aiRestorePending = false;
    }
    const auto& heldRestore = aiBefore == 0 ? txn.heldBefore : noHeld;
    restored = SafeWriteMemory(character + kNativeHeldButtonBase,
                               heldRestore.data(), sizeof(heldRestore)) && restored;
    restored = SafeWriteMemory(character + kNativeAutoBlockTimerOffset,
                               &txn.autoBlockTimerBefore,
                               sizeof(txn.autoBlockTimerBefore)) && restored;
    restored = SafeWriteMemory(character + kNativeDirectionalHeldOffset,
                               &txn.directionalHeldBefore,
                               sizeof(txn.directionalHeldBefore)) && restored;

    (void)SafeReadMemory(character + INPUT_BUFFER_INDEX_OFFSET,
                         &txn.headAfter, sizeof(txn.headAfter));
    (void)SafeReadMemory(character + MOTION_TOKEN_OFFSET,
                         &txn.tokenAfterProducer,
                         sizeof(txn.tokenAfterProducer));

    // Specials consume +610; clear +424/+425 button edges or a rejected special becomes 5C.
    const std::array<uint8_t, 4> noButtons = {0, 0, 0, 0};
    const bool fallbacksCleared =
        SafeWriteMemory(character + INPUT_BUTTON_A_OFFSET,
                        noButtons.data(), noButtons.size()) &&
        SafeWriteMemory(character + COMMAND_BUFFER_OFFSET,
                        noCommandState.data(), noCommandState.size());

    const uint16_t expectedHead = placement.expectedHeadAfterPoll;
    if (!restored || !fallbacksCleared || !txn.pollObserved ||
        txn.headAfter != expectedHead) {
        std::ostringstream failure;
        failure << "producer acknowledgement failed restored=" << (restored ? 1 : 0)
                << " scrub=" << (fallbacksCleared ? 1 : 0)
                << " poll=" << (txn.pollObserved ? 1 : 0)
                << " head=" << head << "->" << txn.headAfter
                << " expected=" << expectedHead;
        const std::string reason = failure.str();
        CleanupMotionTxnLocked(playerNum, reason.c_str());
        return true;
    }

    txn.phase = AutoActionMotionTxnPhase::AwaitingConsumer;
    std::ostringstream oss;
    oss << "[AA_INPUT_TXN] P" << playerNum
        << " producer gen=" << txn.generation
        << " cpu=" << static_cast<int>(cpuBefore)
        << " ai=" << aiBefore << "->0->" << aiBefore
        << " facing=" << (facingRight ? "right" : "left")
        << " poll=0x" << std::hex << static_cast<int>(txn.pollMask)
        << std::dec << " head=" << head << "->" << txn.headAfter
        << " token=" << txn.tokenAfterProducer
        << " owned=" << start << "+" << txn.ownedLength;
    LogOut(oss.str(), true);
    return true;
}

bool MotionConsumerAcceptedLocked(int playerNum,
                                  short beforeMove, short beforeFrame,
                                  short afterMove, short afterFrame,
                                  uint16_t tokenBefore) {
    if (!IsValidTransactionPlayer(playerNum)) return false;
    return AutoActionMotionPolicy::ConsumerAccepted(
        s_motionTxn[playerNum].motionType,
        beforeMove, beforeFrame, afterMove, afterFrame,
        tokenBefore, kNoMotionToken);
}

void EnsureHumanControlForActivePollOverride(int characterPtr, int playerNum) {
    if ((playerNum != 1 && playerNum != 2) || characterPtr == 0) {
        return;
    }
    if (!g_pollOverrideActive[playerNum].load(std::memory_order_relaxed)) {
        return;
    }

    std::unique_lock<std::recursive_mutex> controlLock(g_p2ControlMutex);
    if (IsAutoActionMotionTransactionActive(playerNum) ||
        !g_pollOverrideActive[playerNum].load(std::memory_order_acquire)) {
        return;
    }

    uint32_t aiFlag = 0;
    if (!SafeReadMemory(static_cast<uintptr_t>(characterPtr) + AI_CONTROL_FLAG_OFFSET,
                        &aiFlag,
                        sizeof(aiFlag))) {
        return;
    }
    if (aiFlag == 0) {
        return;
    }

    const uint32_t humanControlFlag = 0;
    const bool okWrite = SafeWriteMemory(static_cast<uintptr_t>(characterPtr) + AI_CONTROL_FLAG_OFFSET,
                                         &humanControlFlag,
                                         sizeof(humanControlFlag));
    uint32_t after = aiFlag;
    SafeReadMemory(static_cast<uintptr_t>(characterPtr) + AI_CONTROL_FLAG_OFFSET,
                   &after,
                   sizeof(after));

    if (!s_loggedPollOverrideHumanForce[playerNum].exchange(true, std::memory_order_relaxed)
        || !okWrite
        || after != 0) {
        std::ostringstream oss;
        oss << "[INPUT_HOOK][POLL_OVERRIDE] Forced P" << playerNum
            << " AI flag human for engine poll path @0x" << std::hex
            << (static_cast<uintptr_t>(characterPtr) + AI_CONTROL_FLAG_OFFSET)
            << std::dec
            << " before=" << aiFlag
            << " after=" << after
            << " okWrite=" << (okWrite ? "1" : "0");
        LogOut(oss.str(), true);
    }
}

} // namespace

namespace {

const char* NormalPulsePhaseName(NormalInputPolicy::Phase phase) {
    switch (phase) {
        case NormalInputPolicy::Phase::Idle:           return "idle";
        case NormalInputPolicy::Phase::AwaitingStart:  return "await";
        case NormalInputPolicy::Phase::PreNeutral:     return "pre-neutral";
        case NormalInputPolicy::Phase::Press:          return "press";
        case NormalInputPolicy::Phase::ReleaseNeutral: return "release-neutral";
    }
    return "unknown";
}

uintptr_t ReadLiveGameState() {
    const uintptr_t base = GetEFZBase();
    uintptr_t gameState = 0;
    if (!base || !SafeReadMemory(base + EFZ_BASE_OFFSET_GAME_STATE,
                                 &gameState, sizeof(gameState))) {
        return 0;
    }
    return gameState;
}

const char* NormalPulseTransportName(NormalPulseTransport transport) {
    switch (transport) {
        case NormalPulseTransport::Unselected: return "unselected";
        case NormalPulseTransport::NativePoll: return "native-poll";
        case NormalPulseTransport::AiPostWrite:return "ai-postwrite";
    }
    return "unknown";
}

int NormalButtonIndex(const NormalInputPolicy::Intent& intent) {
    const uint8_t buttons[4] = {
        NormalInputPolicy::kInputA,
        NormalInputPolicy::kInputB,
        NormalInputPolicy::kInputC,
        NormalInputPolicy::kInputD,
    };
    for (int index = 0; index < 4; ++index) {
        if (intent.button == buttons[index]) return index;
    }
    return -1;
}

bool ReadCharacterResourceName(uintptr_t character, char (&name)[16]) {
    std::memset(name, 0, sizeof(name));
    return character != 0 &&
        SafeReadMemory(character + CHARACTER_NAME_OFFSET,
                       name, sizeof(name) - 1);
}

bool NameEquals(const char* name, const char* expected) {
    return name && expected && std::strcmp(name, expected) == 0;
}

bool CharacterSupportsDIntent(uintptr_t character,
                              const NormalInputPolicy::Intent& intent) {
    if (intent.button != NormalInputPolicy::kInputD) return true;

    char name[16] = {};
    if (!ReadCharacterResourceName(character, name)) return false;
    if (intent.airborne) {
        // UNKNOWN's resource name is case-sensitive: "mizukaB" is regular, "mizuka" is the boss variant.
        return NameEquals(name, "mio") || NameEquals(name, "ayu") ||
               NameEquals(name, "mizuka") || NameEquals(name, "mizukaB");
    }

    if (NameEquals(name, "mai") &&
        intent.direction == NormalInputPolicy::RelativeDirection::Down) {
        return false; // Mai has ground S, but no 2S branch.
    }
    return NameEquals(name, "shiori") || NameEquals(name, "nayuki") ||
           NameEquals(name, "mizuka") || NameEquals(name, "mizukaB") ||
           NameEquals(name, "mio") || NameEquals(name, "minagi") ||
           NameEquals(name, "mai") || NameEquals(name, "kano") ||
           NameEquals(name, "nanase");
}

bool DidConsumerStartRequestedNormal(uintptr_t character,
                                     const NormalInputPolicy::Intent& intent,
                                     short beforeMove,
                                     short beforeFrame,
                                     short afterMove,
                                     short afterFrame) {
    const bool actionInstanceChanged = afterMove != beforeMove ||
        (afterFrame >= 0 && beforeFrame >= 0 && afterFrame < beforeFrame);
    if (!actionInstanceChanged) return false;

    if (intent.expectedMove != 0) return afterMove == intent.expectedMove;

    // Prove the exact tier; a surviving AI command token can start a special at this consumer.
    if (intent.button != NormalInputPolicy::kInputD) {
        const int button = NormalButtonIndex(intent);
        if (button < 0 || button > 2) return false;

        if (intent.airborne) {
            if (intent.direction == NormalInputPolicy::RelativeDirection::Neutral) {
                return afterMove == static_cast<short>(207 + button);
            }
            // Air command normals share the 210..229 band; specials begin at 250+.
            return afterMove >= 210 && afterMove < 230;
        }

        switch (intent.direction) {
            case NormalInputPolicy::RelativeDirection::Neutral:
                if (button == 0) return afterMove == 200;
                if (button == 1) return afterMove == 201 || afterMove == 202;
                return afterMove == 203;
            case NormalInputPolicy::RelativeDirection::Down:
                return afterMove == static_cast<short>(204 + button);
            case NormalInputPolicy::RelativeDirection::DownForward:
            case NormalInputPolicy::RelativeDirection::DownBack:
                // 662X uses 233..235; diagonal command normals live in 210..229.
                return afterMove == static_cast<short>(233 + button) ||
                       (afterMove >= 210 && afterMove < 230);
            case NormalInputPolicy::RelativeDirection::Forward:
            case NormalInputPolicy::RelativeDirection::Back: {
                const bool matchingBase = button == 0 ? afterMove == 200
                    : button == 1 ? (afterMove == 201 || afterMove == 202)
                                  : afterMove == 203;
                const bool matchingDash =
                    afterMove == static_cast<short>(230 + button);
                // Command normals share the 210..229 band; AI specials begin at 250+.
                const bool commandNormal = afterMove >= 210 && afterMove < 230;
                return matchingBase || matchingDash || commandNormal;
            }
        }
        return false;
    }

    char name[16] = {};
    if (!ReadCharacterResourceName(character, name)) return false;
    if (NameEquals(name, "shiori")) return afterMove == 309;
    if (NameEquals(name, "nayuki")) return afterMove == 259 || afterMove == 280;
    if (NameEquals(name, "mizuka") || NameEquals(name, "mizukaB")) {
        return afterMove == 179;
    }
    if (NameEquals(name, "mio")) return afterMove == 270 || afterMove == 17;
    if (NameEquals(name, "minagi")) {
        return afterMove >= 250 && afterMove <= 252;
    }
    if (NameEquals(name, "mai")) return afterMove == 260 || afterMove == 269;
    if (NameEquals(name, "kano")) return afterMove == 250;
    if (NameEquals(name, "ayu")) return afterMove == 40;
    if (NameEquals(name, "nanase")) {
        return afterMove == 248 || afterMove == 249;
    }
    return false;
}

enum class NormalRegisterWriteResult : uint8_t {
    Written,
    ClearedAfterFailure,
    Failed,
};

enum class NormalCleanupResult : uint8_t {
    Complete,
    RetiredWithoutWrites,
    Failed,
};

std::array<uint8_t, 6> EncodeImmediateRegisterBlock(uint8_t mask) {
    const bool up = (mask & GAME_INPUT_UP) != 0;
    const bool down = (mask & GAME_INPUT_DOWN) != 0;
    const bool left = (mask & GAME_INPUT_LEFT) != 0;
    const bool right = (mask & GAME_INPUT_RIGHT) != 0;
    return {
        static_cast<uint8_t>(right ? 1 : (left ? 0xFF : 0)),
        static_cast<uint8_t>(down ? 1 : (up ? 0xFF : 0)),
        static_cast<uint8_t>((mask & GAME_INPUT_A) != 0),
        static_cast<uint8_t>((mask & GAME_INPUT_B) != 0),
        static_cast<uint8_t>((mask & GAME_INPUT_C) != 0),
        static_cast<uint8_t>((mask & GAME_INPUT_D) != 0),
    };
}

bool ClearOwnedNormalRegisters(uintptr_t character) {
    if (!character) return false;
    const uint16_t noMotion = 0x0063;
    const std::array<uint8_t, 2> noCommands = {0, 0};
    const std::array<uint8_t, 6> neutral = {0, 0, 0, 0, 0, 0};
    bool ok = true;
    // Best-effort: evaluate every write; these are the complete lanes the later consumer reads.
    ok = SafeWriteMemory(character + MOTION_TOKEN_OFFSET,
                         &noMotion, sizeof(noMotion)) && ok;
    ok = SafeWriteMemory(character + COMMAND_BUFFER_OFFSET,
                         noCommands.data(), noCommands.size()) && ok;
    ok = SafeWriteMemory(character + INPUT_HORIZONTAL_OFFSET,
                         neutral.data(), neutral.size()) && ok;
    return ok;
}

NormalRegisterWriteResult WriteOwnedNormalRegisters(uintptr_t character,
                                                     uint8_t mask) {
    if (!character) return NormalRegisterWriteResult::Failed;
    const uint16_t noMotion = 0x0063;
    const std::array<uint8_t, 2> noCommands = {0, 0};
    const auto raw = EncodeImmediateRegisterBlock(mask);

    // Commit regions as units, raw pulse last; on failure neutralize all owned lanes.
    const bool written =
        SafeWriteMemory(character + MOTION_TOKEN_OFFSET,
                        &noMotion, sizeof(noMotion)) &&
        SafeWriteMemory(character + COMMAND_BUFFER_OFFSET,
                        noCommands.data(), noCommands.size()) &&
        SafeWriteMemory(character + INPUT_HORIZONTAL_OFFSET,
                        raw.data(), raw.size());
    if (written) return NormalRegisterWriteResult::Written;
    return ClearOwnedNormalRegisters(character)
        ? NormalRegisterWriteResult::ClearedAfterFailure
        : NormalRegisterWriteResult::Failed;
}

bool ClearNativeNormalHeldLatch(uintptr_t character,
                                const NormalInputPolicy::Intent& intent) {
    const int buttonIndex = NormalButtonIndex(intent);
    if (!character || buttonIndex < 0) return false;
    const uint32_t zero = 0;
    return SafeWriteMemory(character + kNativeHeldButtonOffsets[buttonIndex],
                           &zero, sizeof(zero));
}

NormalCleanupResult ClearDeliveredNormalState(
    int playerNum,
    NormalPulseTransport transport,
    uintptr_t expectedGameState,
    uintptr_t expectedCharacter,
    const NormalInputPolicy::Intent& intent,
    const EfzTmEntryV1* heldWorld = nullptr) {
    // Never touch a replacement fighter or battle memory after online mode takes ownership.
    if (heldWorld) {
        if (g_onlineModeActive.load(std::memory_order_acquire) ||
            expectedGameState != heldWorld->gameSystem ||
            expectedCharacter != (playerNum == 1 ? heldWorld->player1 : heldWorld->player2))
            return NormalCleanupResult::Failed;
    } else if (g_onlineModeActive.load(std::memory_order_relaxed) ||
        ReadLiveGameState() != expectedGameState ||
        GetPlayerPointer(playerNum) != expectedCharacter) {
        return NormalCleanupResult::RetiredWithoutWrites;
    }

    if (transport != NormalPulseTransport::AiPostWrite &&
        transport != NormalPulseTransport::NativePoll) {
        return NormalCleanupResult::Complete;
    }

    // Clear the complete hand-off (direction, pulses, latches, WORD token), not just the button byte.
    bool ok = ClearOwnedNormalRegisters(expectedCharacter);
    if (transport == NormalPulseTransport::NativePoll) {
        // Native polling also updates one 32-bit held latch; clear only that synthetic latch.
        ok = ClearNativeNormalHeldLatch(expectedCharacter, intent) && ok;
    }
    return ok ? NormalCleanupResult::Complete
              : NormalCleanupResult::Failed;
}

void RetainFailedNormalCleanupLocked(
    int playerNum,
    NormalPulseRuntimeSlot& slot,
    const NormalInputPolicy::Intent& intent) {
    slot.cleanupPending = true;
    slot.cleanupIntent = intent;
    slot.rawReleaseGuard = true;
    s_normalRawRegisterOwner[playerNum].store(
        true, std::memory_order_release);
    LogOut("[AUTO-NORMAL] Retaining failed same-world cleanup for P" +
               std::to_string(playerNum),
           true);
}

bool RetryPendingNormalCleanupLocked(int playerNum,
                                     NormalPulseRuntimeSlot& slot) {
    if (!slot.cleanupPending) return true;
    const NormalCleanupResult result = ClearDeliveredNormalState(
        playerNum, slot.transport, slot.gameState, slot.character,
        slot.cleanupIntent);
    if (result == NormalCleanupResult::Failed) return false;

    slot.cleanupPending = false;
    slot.cleanupIntent = {};
    slot.rawReleaseGuard = false;
    s_normalRawRegisterOwner[playerNum].store(
        false, std::memory_order_release);
    slot.transport = NormalPulseTransport::Unselected;
    if (!slot.state.Active()) {
        slot.gameState = 0;
        slot.character = 0;
    }
    LogOut("[AUTO-NORMAL] Completed retained cleanup for P" +
               std::to_string(playerNum),
           detailedLogging.load());
    return true;
}

} // namespace

NormalInputPolicy::SubmitResult QueueAutoActionNormalPulse(
    int playerNum,
    const NormalInputPolicy::Intent& intent,
    NormalInputPolicy::Timing timing,
    uint64_t* generationOut) {
    if (generationOut) *generationOut = 0;
    if (playerNum < 1 || playerNum > 2 || !intent ||
        !s_inputHooksEnabled.load(std::memory_order_acquire) ||
        g_onlineModeActive.load(std::memory_order_relaxed) || !IsMatchPhase()) {
        return NormalInputPolicy::SubmitResult::Invalid;
    }

    if (IsScopedInputReserved(playerNum)) {
        return NormalInputPolicy::SubmitResult::Busy;
    }

    if (MacroController::IsExclusivePlayback() &&
        MacroController::GetPlaybackPlayer() == playerNum) {
        return NormalInputPolicy::SubmitResult::Busy;
    }

    // Motion and normal delivery share the native producer/consumer; submission is serialized with motion queueing.
    std::unique_lock<std::recursive_mutex> p2ControlLock(g_p2ControlMutex);
    if (IsScopedInputReserved(playerNum)) {
        return NormalInputPolicy::SubmitResult::Busy;
    }
    if (IsAutoActionMotionTransactionActive(playerNum) ||
        ImmediateInput::TutorialLeaseActive(playerNum) ||
        TutorialMotionQueueLeaseActive(playerNum) ||
        TutorialBufferFreezeLeaseActive() ||
        g_manualInputOverride[playerNum].load(std::memory_order_acquire) ||
        g_pollOverrideActive[playerNum].load(std::memory_order_acquire) ||
        (MacroController::IsExclusivePlayback() &&
         MacroController::GetPlaybackPlayer() == playerNum) ||
        GetMotionQueueSnapshot(playerNum).active ||
        (g_bufferFreezingActive.load(std::memory_order_acquire) &&
         (g_activeFreezePlayer.load(std::memory_order_acquire) == 0 ||
          g_activeFreezePlayer.load(std::memory_order_acquire) == playerNum))) {
        return NormalInputPolicy::SubmitResult::Busy;
    }
    if (ImmediateInput::GetCurrentDesired(playerNum) != 0 ||
        ImmediateInput::GetRemainingTicks(playerNum) != 0) {
        return NormalInputPolicy::SubmitResult::Busy;
    }

    const uintptr_t gameState = ReadLiveGameState();
    const uintptr_t character = GetPlayerPointer(playerNum);
    if (!gameState || !character) {
        return NormalInputPolicy::SubmitResult::Invalid;
    }
    if (!CharacterSupportsDIntent(character, intent)) {
        LogOut("[AUTO-NORMAL] Rejected unsupported character-specific S/D intent for P" +
               std::to_string(playerNum), detailedLogging.load());
        return NormalInputPolicy::SubmitResult::Invalid;
    }

    uint64_t generation = 0;
    NormalInputPolicy::SubmitResult result;
    {
        NormalPulseRuntimeSlot& slot = s_normalPulse[playerNum];
        std::lock_guard<std::recursive_mutex> lock(slot.mutex);
        if (!s_inputHooksEnabled.load(std::memory_order_acquire) ||
            g_onlineModeActive.load(std::memory_order_relaxed)) {
            return NormalInputPolicy::SubmitResult::Invalid;
        }
        if (!RetryPendingNormalCleanupLocked(playerNum, slot)) {
            return NormalInputPolicy::SubmitResult::Busy;
        }
        if (slot.state.Active() &&
            (slot.gameState != gameState || slot.character != character)) {
            CancelNormalPulseGenerationsLocked(slot);
            slot.state.Reset();
            slot.transport = NormalPulseTransport::Unselected;
            slot.gameState = 0;
            slot.character = 0;
            slot.deliveredAnyPhase = false;
            slot.rawReleaseGuard = false;
            slot.pressAwaitingConfirmation = false;
            slot.pressBeforeMove = -1;
            slot.pressBeforeFrame = -1;
            slot.pressAttempts = 0;
            slot.retryNeutralPending = false;
            slot.abortAfterRetryNeutral = false;
            slot.cleanupPending = false;
            slot.cleanupIntent = {};
            s_normalRawRegisterOwner[playerNum].store(
                false, std::memory_order_release);
        }

        const bool alreadyActive = slot.state.Active();
        result = slot.state.Submit(intent, timing, &generation);
        if (result == NormalInputPolicy::SubmitResult::Accepted) {
            RecordNormalPulseOutcomeLocked(
                slot, generation, AutoActionNormalPulseOutcome::Pending);
        }
        if (result == NormalInputPolicy::SubmitResult::Accepted &&
            !alreadyActive) {
            slot.transport = NormalPulseTransport::Unselected;
            slot.gameState = gameState;
            slot.character = character;
            slot.deliveredAnyPhase = false;
            slot.cleanupPending = false;
            slot.cleanupIntent = {};
        }
    }
    if (generationOut) *generationOut = generation;

    if (detailedLogging.load() || result != NormalInputPolicy::SubmitResult::Accepted) {
        LogOut("[AUTO-NORMAL] Queue P" + std::to_string(playerNum) +
               " gen=" + std::to_string(generation) +
               " button=0x" + FormatHexByte(intent.button) +
               " timing=" + (timing == NormalInputPolicy::Timing::Immediate
                                   ? "immediate" : "actionable") +
               " result=" +
               (result == NormalInputPolicy::SubmitResult::Accepted ? "accepted" :
                result == NormalInputPolicy::SubmitResult::Busy ? "busy" : "invalid"),
               true);
    }
    return result;
}

NormalInputPolicy::SubmitResult QueueAutoActionNormalPulse(
    int playerNum,
    int motionType,
    NormalInputPolicy::Timing timing,
    uint64_t* generationOut) {
    return QueueAutoActionNormalPulse(playerNum,
        NormalInputPolicy::IntentFromMotion(motionType), timing, generationOut);
}

bool IsAutoActionNormalPulseActive(int playerNum) {
    if (playerNum < 1 || playerNum > 2) return false;
    std::lock_guard<std::recursive_mutex> lock(s_normalPulse[playerNum].mutex);
    return s_normalPulse[playerNum].state.Active();
}

AutoActionNormalPulseOutcome GetAutoActionNormalPulseOutcome(
    int playerNum, uint64_t generation) {
    if (playerNum < 1 || playerNum > 2 || generation == 0) {
        return AutoActionNormalPulseOutcome::Unknown;
    }
    NormalPulseRuntimeSlot& slot = s_normalPulse[playerNum];
    std::lock_guard<std::recursive_mutex> lock(slot.mutex);
    return FindNormalPulseOutcomeLocked(slot, generation);
}

bool IsAutoActionNormalPulseOwningImmediateRegisters(int playerNum) {
    return playerNum >= 1 && playerNum <= 2 &&
        s_normalRawRegisterOwner[playerNum].load(std::memory_order_acquire);
}

bool TryAcquireImmediateInputWriteLease(int playerNum) {
    if (playerNum < 1 || playerNum > 2) return false;
    if (g_onlineModeActive.load(std::memory_order_acquire)) return false;

    // No raw input write may cross from an offline pre-check into online play.
    const bool ownershipLocked = g_p2ControlMutex.try_lock();
    if (!ownershipLocked) return false;
    if (g_onlineModeActive.load(std::memory_order_acquire) ||
        IsScopedInputReserved(playerNum) ||
        IsAutoActionMotionTransactionActive(playerNum) ||
        g_manualInputOverride[playerNum].load(std::memory_order_acquire) ||
        g_pollOverrideActive[playerNum].load(std::memory_order_acquire) ||
        GetMotionQueueSnapshot(playerNum).active ||
        TutorialMotionQueueLeaseActive(playerNum) ||
        TutorialBufferFreezeLeaseActive() ||
        (g_bufferFreezingActive.load(std::memory_order_acquire) &&
         (g_activeFreezePlayer.load(std::memory_order_acquire) == 0 ||
          g_activeFreezePlayer.load(std::memory_order_acquire) == playerNum)) ||
        (MacroController::IsExclusivePlayback() &&
         MacroController::GetPlaybackPlayer() == playerNum)) {
        g_p2ControlMutex.unlock();
        return false;
    }
    NormalPulseRuntimeSlot& slot = s_normalPulse[playerNum];
    if (!slot.mutex.try_lock()) {
        g_p2ControlMutex.unlock();
        return false;
    }
    if (g_onlineModeActive.load(std::memory_order_acquire) ||
        IsScopedInputReserved(playerNum) ||
        s_normalRawRegisterOwner[playerNum].load(
            std::memory_order_acquire) || slot.state.Active() ||
        IsAutoActionMotionTransactionActive(playerNum) ||
        g_manualInputOverride[playerNum].load(std::memory_order_acquire) ||
        g_pollOverrideActive[playerNum].load(std::memory_order_acquire) ||
        GetMotionQueueSnapshot(playerNum).active ||
        TutorialMotionQueueLeaseActive(playerNum) ||
        TutorialBufferFreezeLeaseActive() ||
        (g_bufferFreezingActive.load(std::memory_order_acquire) &&
         (g_activeFreezePlayer.load(std::memory_order_acquire) == 0 ||
          g_activeFreezePlayer.load(std::memory_order_acquire) == playerNum)) ||
        (MacroController::IsExclusivePlayback() &&
         MacroController::GetPlaybackPlayer() == playerNum)) {
        slot.mutex.unlock();
        g_p2ControlMutex.unlock();
        return false;
    }
    return true;
}

void ReleaseImmediateInputWriteLease(int playerNum) {
    if (playerNum < 1 || playerNum > 2) return;
    s_normalPulse[playerNum].mutex.unlock();
    g_p2ControlMutex.unlock();
}

void CancelAutoActionNormalPulse(int playerNum) {
    if (playerNum < 1 || playerNum > 2) return;

    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    NormalPulseRuntimeSlot& slot = s_normalPulse[playerNum];
    std::lock_guard<std::recursive_mutex> lock(slot.mutex);
    if (!RetryPendingNormalCleanupLocked(playerNum, slot)) {
        return;
    }
    const NormalInputPolicy::Snapshot snapshot = slot.state.Current();
    const bool ownsRawLane = s_normalRawRegisterOwner[playerNum].load(
        std::memory_order_acquire);
    const bool hasSelectedTransport = snapshot &&
        slot.transport != NormalPulseTransport::Unselected;

    // Cleanup while the slot mutex and raw ownership are held; releasing first lets a detached worker take the lane.
    NormalCleanupResult cleanupResult = NormalCleanupResult::Complete;
    if (hasSelectedTransport && (slot.deliveredAnyPhase || ownsRawLane)) {
        cleanupResult = ClearDeliveredNormalState(
            playerNum, slot.transport, slot.gameState,
            slot.character, snapshot.intent);
        if (cleanupResult == NormalCleanupResult::Failed) {
            RetainFailedNormalCleanupLocked(playerNum, slot, snapshot.intent);
        }
    }

    // Keep competing raw writers out until this tick's consumer if any owned phase was delivered.
    const bool cleanupFailed =
        cleanupResult == NormalCleanupResult::Failed;
    const bool deferRawRelease = cleanupFailed || slot.rawReleaseGuard ||
        (hasSelectedTransport && ownsRawLane);

    CancelNormalPulseGenerationsLocked(slot);
    slot.state.Reset();
    if (!cleanupFailed) {
        slot.transport = NormalPulseTransport::Unselected;
    }
    if (!deferRawRelease) {
        slot.gameState = 0;
        slot.character = 0;
    }
    slot.deliveredAnyPhase = false;
    slot.rawReleaseGuard = deferRawRelease;
    slot.pressAwaitingConfirmation = false;
    slot.pressBeforeMove = -1;
    slot.pressBeforeFrame = -1;
    slot.pressAttempts = 0;
    slot.retryNeutralPending = false;
    slot.abortAfterRetryNeutral = false;
    if (!deferRawRelease) {
        s_normalRawRegisterOwner[playerNum].store(
            false, std::memory_order_release);
    }
}

void CancelAllAutoActionNormalPulses() {
    CancelAutoActionNormalPulse(1);
    CancelAutoActionNormalPulse(2);
}

bool DrainAutoActionNormalPulsesForOwnershipBoundary() {
    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    // The synchronous point before online ownership publication; drain both fighters' transactions here.
    CancelAllAutoActionMotionTransactions(
        "offline ownership boundary");
    CancelAllAutoActionNormalPulses();
    bool allClean = true;
    for (int playerNum = 1; playerNum <= 2; ++playerNum) {
        AutoActionMotionTxnState& txn = s_motionTxn[playerNum];
        {
            std::lock_guard<std::recursive_mutex> txnLock(txn.mutex);
            for (int retry = 0;
                 retry < 2 && txn.phase != AutoActionMotionTxnPhase::Idle;
                 ++retry) {
                (void)CleanupMotionTxnLocked(
                    playerNum, "ownership-boundary cleanup retry",
                    AutoActionMotionOutcome::Cancelled);
            }
            for (int retry = 0;
                 retry < 2 && s_pendingAiRestoreActive[playerNum].load(
                     std::memory_order_acquire);
                 ++retry) {
                (void)RetryPendingAiRestoreLocked(
                    playerNum, "ownership-boundary AI restore retry");
            }
            if (txn.phase != AutoActionMotionTxnPhase::Idle ||
                s_pendingAiRestoreActive[playerNum].load(
                    std::memory_order_acquire)) {
                allClean = false;
            }
        }

        NormalPulseRuntimeSlot& slot = s_normalPulse[playerNum];
        std::lock_guard<std::recursive_mutex> lock(slot.mutex);
        for (int retry = 0; retry < 2 && slot.cleanupPending; ++retry) {
            (void)RetryPendingNormalCleanupLocked(playerNum, slot);
        }
        if (slot.cleanupPending) {
            // Lifecycle-only: this boundary may scrub the complete offline input state.
            (void)FullCleanupAfterToggle(playerNum);
            (void)RetryPendingNormalCleanupLocked(playerNum, slot);
        }
        if (slot.cleanupPending) {
            allClean = false;
            continue;
        }
        slot.rawReleaseGuard = false;
        s_normalRawRegisterOwner[playerNum].store(
            false, std::memory_order_release);
        if (!slot.state.Active()) {
            slot.transport = NormalPulseTransport::Unselected;
            slot.gameState = 0;
            slot.character = 0;
        }
    }
    return allClean;
}

namespace {

bool NativeNormalInputGateOpen() {
    if (!IsMatchPhase()) return false;
    const uintptr_t gameState = ReadLiveGameState();
    if (!gameState) return false;

    uint8_t roundEvent = 0;
    uint32_t roundGate = 0;
    uint8_t replayIo = 0;
    return SafeReadMemory(gameState + SYS_FLAG_4944,
                          &roundEvent, sizeof(roundEvent)) &&
           SafeReadMemory(gameState + SYS_FLAG_4948,
                          &roundGate, sizeof(roundGate)) &&
           SafeReadMemory(gameState + kReplayIoModeOffset,
                          &replayIo, sizeof(replayIo)) &&
           roundEvent == 0 && roundGate == 0 && replayIo == 0;
}

bool RequestedNativeNormalButtonHeld(
    uintptr_t character,
    const NormalInputPolicy::Intent& intent) {
    const int index = NormalButtonIndex(intent);
    if (index < 0) return true;
    uint32_t held = 0;
    if (!SafeReadMemory(character + kNativeHeldButtonOffsets[index],
                        &held, sizeof(held))) {
        // A conservative neutral pass beats losing a repeated same-button edge.
        return true;
    }
    return held != 0;
}

bool CurrentPatAllowsAirNormal(uintptr_t character,
                               const NormalInputPolicy::Intent& intent,
                               short moveID) {
    int destinationMove = -1;
    switch (intent.button) {
        case NormalInputPolicy::kInputA: destinationMove = 207; break;
        case NormalInputPolicy::kInputB: destinationMove = 208; break;
        case NormalInputPolicy::kInputC: destinationMove = 209; break;
        default: return false;
    }

    // Air-normal availability comes from the live PAT row, not a move-ID
    // allowlist; 207/208/209 are the cast-wide A/B/C tier anchors. Consumers
    // compare the current row's WORD at +2 against the destination anchor at
    // +0 (deliberately asymmetric).
    constexpr uintptr_t kCurrentAnimRankOffset = 2u;
    constexpr uintptr_t kDestinationAnimRankOffset = 0u;
    uint16_t frameIndex = 0;
    uintptr_t animTable = 0;
    uintptr_t frameTable = 0;
    uint16_t frameFlags = 0;
    int16_t currentRank = 0;
    int16_t destinationRank = 0;
    int32_t contactState = 0;
    if (moveID < 0 || moveID > 1023 ||
        !SafeReadMemory(character + CURRENT_FRAME_INDEX_OFFSET,
                        &frameIndex, sizeof(frameIndex)) ||
        frameIndex > 4095 ||
        !SafeReadMemory(character + ANIM_TABLE_OFFSET,
                        &animTable, sizeof(animTable)) ||
        !animTable ||
        !SafeReadMemory(animTable + ANIM_ENTRY_STRIDE *
                            static_cast<uintptr_t>(moveID) +
                            ANIM_ENTRY_FRAMES_PTR_OFFSET,
                        &frameTable, sizeof(frameTable)) ||
        !frameTable ||
        !SafeReadMemory(frameTable + FRAME_BLOCK_STRIDE *
                            static_cast<uintptr_t>(frameIndex) +
                            FRAME_HIT_PROPS_OFFSET,
                        &frameFlags, sizeof(frameFlags)) ||
        !SafeReadMemory(animTable + ANIM_ENTRY_STRIDE *
                            static_cast<uintptr_t>(moveID) +
                            kCurrentAnimRankOffset,
                        &currentRank, sizeof(currentRank)) ||
        !SafeReadMemory(animTable + ANIM_ENTRY_STRIDE *
                            static_cast<uintptr_t>(destinationMove) +
                            kDestinationAnimRankOffset,
                        &destinationRank, sizeof(destinationRank)) ||
        !SafeReadMemory(character + PLAYER_HIT_STATE_OFFSET,
                        &contactState, sizeof(contactState))) {
        return false;
    }

    return (frameFlags & 0x0008u) != 0 &&
           currentRank <= destinationRank &&
           (moveID < 200 || (contactState >= 2 && contactState != 6));
}

// RG's second PAT row is the cancel window (move ID still 168/169/170), which
// IsActionable() rejects; consult the live PAT row, scoped strictly to RG.
bool CurrentPatAllowsGroundCancelFromRecoilGuard(
        uintptr_t character,
        const NormalInputPolicy::Intent& intent,
        short moveID) {
    // Cheap short-circuit: nothing else in the game may reach this relaxation.
    if (!IsRecoilGuard(moveID)) return false;

    const int destinationMove = NormalInputPolicy::GroundNormalRankAnchor(intent);
    const bool destinationRankKnown = destinationMove >= 0;

    // Same asymmetric offsets: current rank at anim-entry +2, destination tier at +0.
    constexpr uintptr_t kCurrentAnimRankOffset = 2u;
    constexpr uintptr_t kDestinationAnimRankOffset = 0u;
    uint16_t frameIndex = 0;
    uintptr_t animTable = 0;
    uintptr_t frameTable = 0;
    uint16_t frameFlags = 0;
    int16_t currentRank = 0;
    int16_t destinationRank = 0;
    // Fail closed on unreadable PAT fields.
    if (moveID < 0 || moveID > 1023 ||
        !SafeReadMemory(character + CURRENT_FRAME_INDEX_OFFSET,
                        &frameIndex, sizeof(frameIndex)) ||
        frameIndex > 4095 ||
        !SafeReadMemory(character + ANIM_TABLE_OFFSET,
                        &animTable, sizeof(animTable)) ||
        !animTable ||
        !SafeReadMemory(animTable + ANIM_ENTRY_STRIDE *
                            static_cast<uintptr_t>(moveID) +
                            ANIM_ENTRY_FRAMES_PTR_OFFSET,
                        &frameTable, sizeof(frameTable)) ||
        !frameTable ||
        !SafeReadMemory(frameTable + FRAME_BLOCK_STRIDE *
                            static_cast<uintptr_t>(frameIndex) +
                            FRAME_HIT_PROPS_OFFSET,
                        &frameFlags, sizeof(frameFlags)) ||
        !SafeReadMemory(animTable + ANIM_ENTRY_STRIDE *
                            static_cast<uintptr_t>(moveID) +
                            kCurrentAnimRankOffset,
                        &currentRank, sizeof(currentRank))) {
        return false;
    }
    if (destinationRankKnown &&
        !SafeReadMemory(animTable + ANIM_ENTRY_STRIDE *
                            static_cast<uintptr_t>(destinationMove) +
                            kDestinationAnimRankOffset,
                        &destinationRank, sizeof(destinationRank))) {
        return false;
    }

    return NormalInputPolicy::RecoilGuardCancelEligible(
        true, (frameFlags & 0x0008u) != 0,
        static_cast<int>(currentRank), static_cast<int>(destinationRank),
        destinationRankKnown);
}

bool NormalPostureReady(uintptr_t character,
                        const NormalInputPolicy::Snapshot& snapshot) {
    short moveID = -1;
    if (!SafeReadMemory(character + MOVE_ID_OFFSET,
                        &moveID, sizeof(moveID))) {
        return false;
    }
    double y = 0.0;
    if (!SafeReadMemory(character + YPOS_OFFSET, &y, sizeof(y))) {
        return false;
    }
    const bool physicallyAirborne = y < 0.0;

    // Exact-tick ground follow-ups must never become an air normal after a launch.
    if (snapshot.timing == NormalInputPolicy::Timing::Immediate &&
        !snapshot.intent.airborne) {
        return !physicallyAirborne;
    }

    const bool landing = moveID == LANDING_ID || moveID == LANDING_1_ID ||
                         moveID == LANDING_2_ID || moveID == LANDING_3_ID;
    // IsActionable() rejects RG 168/169/170; the cancellable second PAT row is the RG window.
    const bool recoilGuardCancel =
        CurrentPatAllowsGroundCancelFromRecoilGuard(character, snapshot.intent,
                                                    moveID);
    const bool groundReady = !physicallyAirborne &&
                             (IsActionable(moveID) || recoilGuardCancel) &&
                             moveID != FALLING_ID && !landing;
    const bool universalAirButton =
        snapshot.intent.button != NormalInputPolicy::kInputD;
    const bool airReady = physicallyAirborne &&
        (universalAirButton
             ? CurrentPatAllowsAirNormal(character, snapshot.intent, moveID)
             // No cast-wide j.S state; fall back to a freely actionable fall.
             : moveID == FALLING_ID);

    // Strict posture: no ground-to-j.X after a launch, no air-to-5X after landing.
    return NormalInputPolicy::PostureEligible(
        snapshot.intent, groundReady, airReady);
}

bool NormalConsumerGateOpen(int playerNum, uintptr_t character) {
    const int opponentNum = playerNum == 1 ? 2 : 1;
    const uintptr_t opponent = GetPlayerPointer(opponentNum);
    if (!character || !opponent) return false;

    short targetStateTimer = -1;
    short targetSuperFreeze = -1;
    short opponentSuperFreeze = -1;
    return SafeReadMemory(character + BLOCKSTUN_OFFSET,
                          &targetStateTimer, sizeof(targetStateTimer)) &&
           SafeReadMemory(character + SUPERFLASH_FREEZE_OFFSET,
                          &targetSuperFreeze, sizeof(targetSuperFreeze)) &&
           SafeReadMemory(opponent + SUPERFLASH_FREEZE_OFFSET,
                          &opponentSuperFreeze, sizeof(opponentSuperFreeze)) &&
           targetStateTimer == 0 && targetSuperFreeze == 0 &&
           opponentSuperFreeze == 0;
}

bool PredictFacingRightForConsumer(int playerNum, uintptr_t character) {
    const uintptr_t opponent = GetPlayerPointer(playerNum == 1 ? 2 : 1);
    double x = 0.0;
    double opponentX = 0.0;
    if (character && opponent &&
        SafeReadMemory(character + XPOS_OFFSET, &x, sizeof(x)) &&
        SafeReadMemory(opponent + XPOS_OFFSET, &opponentX, sizeof(opponentX))) {
        if (x < opponentX) return true;
        if (x > opponentX) return false;
    }
    // Equal positions preserve the engine's current +0x50 direction.
    return GetPlayerFacingDirection(playerNum);
}

void AbortNormalPulseLocked(NormalPulseRuntimeSlot& slot,
                            int playerNum,
                            const char* reason,
                            bool deferRawRelease = false,
                            bool preserveAcceptedSuccessor = false,
                            bool clearOwnedDelivery = true,
                            AutoActionNormalPulseOutcome terminalOutcome =
                                AutoActionNormalPulseOutcome::Cancelled) {
    const NormalInputPolicy::Snapshot retiring = slot.state.Current();
    NormalCleanupResult cleanupResult = NormalCleanupResult::Complete;
    if (clearOwnedDelivery && retiring &&
        slot.transport != NormalPulseTransport::Unselected &&
        (slot.deliveredAnyPhase ||
         s_normalRawRegisterOwner[playerNum].load(std::memory_order_acquire))) {
        cleanupResult = ClearDeliveredNormalState(
            playerNum, slot.transport, slot.gameState, slot.character,
            retiring.intent);
    }
    const bool cleanupFailed =
        cleanupResult == NormalCleanupResult::Failed;
    const bool keepRawLease = (cleanupFailed || deferRawRelease) &&
        s_normalRawRegisterOwner[playerNum].load(std::memory_order_acquire);
    if (retiring) {
        RecordNormalPulseOutcomeLocked(
            slot, retiring.generation, terminalOutcome);
    }
    if (!preserveAcceptedSuccessor) {
        RecordNormalPulseOutcomeLocked(
            slot, slot.state.PendingGeneration(),
            AutoActionNormalPulseOutcome::Cancelled);
    }
    bool successorPromoted = false;
    if (preserveAcceptedSuccessor) {
        successorPromoted = slot.state.RetireCurrent();
    } else {
        slot.state.Reset();
    }
    if (!cleanupFailed) {
        slot.transport = NormalPulseTransport::Unselected;
    }
    if (!cleanupFailed && !keepRawLease && !successorPromoted) {
        slot.gameState = 0;
        slot.character = 0;
    }
    slot.deliveredAnyPhase = false;
    slot.rawReleaseGuard = keepRawLease;
    slot.pressAwaitingConfirmation = false;
    slot.pressBeforeMove = -1;
    slot.pressBeforeFrame = -1;
    slot.pressAttempts = 0;
    slot.retryNeutralPending = false;
    slot.abortAfterRetryNeutral = false;
    if (cleanupFailed && retiring) {
        RetainFailedNormalCleanupLocked(playerNum, slot, retiring.intent);
    } else {
        slot.cleanupPending = false;
        slot.cleanupIntent = {};
    }
    if (!slot.cleanupPending && !keepRawLease) {
        s_normalRawRegisterOwner[playerNum].store(false,
                                                   std::memory_order_release);
    }
    LogOut("[AUTO-NORMAL] Aborted P" + std::to_string(playerNum) +
           " pulse: " + reason +
           (successorPromoted ? "; accepted successor promoted" : ""), true);
}

bool AbortExpectedImmediateNormal(int playerNum,
                                  const NormalInputPolicy::Snapshot& expected,
                                  uintptr_t expectedGameState,
                                  uintptr_t expectedCharacter,
                                  const char* reason) {
    if (playerNum < 1 || playerNum > 2) return false;
    NormalPulseRuntimeSlot& slot = s_normalPulse[playerNum];
    std::lock_guard<std::recursive_mutex> lock(slot.mutex);
    const auto current = slot.state.Current();
    if (!current || !expected || current.generation != expected.generation ||
        current.timing != NormalInputPolicy::Timing::Immediate ||
        slot.gameState != expectedGameState ||
        slot.character != expectedCharacter) {
        return false;
    }
    const bool deferRawRelease =
        s_normalRawRegisterOwner[playerNum].load(std::memory_order_acquire);
    AbortNormalPulseLocked(
        slot, playerNum, reason, deferRawRelease, false, true,
        AutoActionNormalPulseOutcome::Failed);
    return true;
}

void ReleaseRawGuardAfterConsumer(int playerNum) {
    if (playerNum < 1 || playerNum > 2) return;
    NormalPulseRuntimeSlot& slot = s_normalPulse[playerNum];
    std::lock_guard<std::recursive_mutex> lock(slot.mutex);
    if (!RetryPendingNormalCleanupLocked(playerNum, slot)) return;
    if (!slot.rawReleaseGuard) return;
    slot.rawReleaseGuard = false;
    s_normalRawRegisterOwner[playerNum].store(false,
                                               std::memory_order_release);
    if (!slot.state.Active()) {
        slot.gameState = 0;
        slot.character = 0;
    }
}

void RelinquishPreparedNormalForConflict(
    int playerNum,
    uint64_t expectedGeneration = 0,
    const char* immediateReason = "exact-tick normal conflicted with another input owner",
    bool clearOwnedDelivery = true) {
    if (playerNum < 1 || playerNum > 2) return;
    NormalPulseRuntimeSlot& slot = s_normalPulse[playerNum];
    std::lock_guard<std::recursive_mutex> lock(slot.mutex);
    const auto current = slot.state.Current();
    if (!current ||
        (expectedGeneration != 0 && current.generation != expectedGeneration)) {
        return;
    }
    // After a rejected Immediate press the next operation is cleanup, not a fresh deferred attack.
    if (slot.retryNeutralPending) {
        NormalCleanupResult cleanupResult = NormalCleanupResult::Complete;
        if (clearOwnedDelivery) {
            cleanupResult = ClearDeliveredNormalState(
                playerNum, slot.transport, slot.gameState,
                slot.character, current.intent);
            if (cleanupResult == NormalCleanupResult::Failed) {
                RetainFailedNormalCleanupLocked(
                    playerNum, slot, current.intent);
            }
        }
        if (current.timing == NormalInputPolicy::Timing::Immediate) {
            AbortNormalPulseLocked(
                slot, playerNum,
                "exact-tick retry-neutral conflicted with another input owner",
                true, false, clearOwnedDelivery);
        } else if (slot.state.RestartAwaiting()) {
            if (cleanupResult != NormalCleanupResult::Failed) {
                slot.transport = NormalPulseTransport::Unselected;
            }
            slot.deliveredAnyPhase = false;
            slot.rawReleaseGuard =
                cleanupResult == NormalCleanupResult::Failed;
            slot.pressAwaitingConfirmation = false;
            slot.pressBeforeMove = -1;
            slot.pressBeforeFrame = -1;
            slot.pressAttempts = 0;
            slot.retryNeutralPending = false;
            slot.abortAfterRetryNeutral = false;
            if (cleanupResult != NormalCleanupResult::Failed) {
                s_normalRawRegisterOwner[playerNum].store(
                    false, std::memory_order_release);
            }
        }
        return;
    }
    // Immediate is an exact dash tick; retire rather than restart (a late 66X/662X must not fire).
    if (current.timing == NormalInputPolicy::Timing::Immediate) {
        const bool deferRawRelease =
            s_normalRawRegisterOwner[playerNum].load(std::memory_order_acquire);
        AbortNormalPulseLocked(slot, playerNum, immediateReason,
                               deferRawRelease, false,
                               clearOwnedDelivery);
        return;
    }
    if (current.phase == NormalInputPolicy::Phase::ReleaseNeutral) {
        // Remove residual raw state and yield; keeping the lease starves a tutorial ImmediateInput owner.
        AbortNormalPulseLocked(slot, playerNum,
                               "normal release yielded to another input owner",
                               false, true, clearOwnedDelivery);
        return;
    }
    if ((current.phase != NormalInputPolicy::Phase::Press &&
         current.phase != NormalInputPolicy::Phase::PreNeutral) ||
        !slot.state.RestartAwaiting()) {
        return;
    }
    slot.transport = NormalPulseTransport::Unselected;
    slot.deliveredAnyPhase = false;
    slot.pressAwaitingConfirmation = false;
    slot.pressBeforeMove = -1;
    slot.pressBeforeFrame = -1;
    slot.pressAttempts = 0;
    slot.retryNeutralPending = false;
    slot.abortAfterRetryNeutral = false;
    s_normalRawRegisterOwner[playerNum].store(false,
                                               std::memory_order_release);
}

bool PreparedNormalHasHigherPriorityOwner(int playerNum) {
    if (g_pollOverrideActive[playerNum].load(std::memory_order_relaxed) ||
        (MacroController::IsExclusivePlayback() &&
         MacroController::GetPlaybackPlayer() == playerNum)) {
        return true;
    }
    if (IsAutoActionMotionTransactionActive(playerNum)) {
        return true;
    }
    return playerNum == 2 && TutorialP2ControlLeaseActive() &&
        !InputHookPolicy::TutorialP2AllowsNormalPulse(TutorialP2Source());
}

// Controller mode can change between phases; keep the route paired with the producer actually executed.
bool ReconcileNormalTransport(int playerNum,
                              uintptr_t character,
                              const NormalInputPolicy::Snapshot& expected,
                              NormalPulseTransport& transport) {
    if (!expected || transport == NormalPulseTransport::Unselected) return false;

    uint32_t aiFlag = 0xFFFFFFFFu;
    if (!SafeReadMemory(character + AI_CONTROL_FLAG_OFFSET,
                        &aiFlag, sizeof(aiFlag))) {
        (void)AbortExpectedImmediateNormal(
            playerNum, expected, ReadLiveGameState(), character,
            "exact-tick normal could not revalidate controller mode");
        return false;
    }
    const NormalPulseTransport desired = aiFlag == 0
        ? NormalPulseTransport::NativePoll
        : NormalPulseTransport::AiPostWrite;
    if (desired == transport) return true;

    NormalPulseRuntimeSlot& slot = s_normalPulse[playerNum];
    std::lock_guard<std::recursive_mutex> lock(slot.mutex);
    const auto current = slot.state.Current();
    if (!current || current.generation != expected.generation ||
        current.phase != expected.phase || slot.transport != transport) {
        return false;
    }

    if (transport == NormalPulseTransport::AiPostWrite &&
        desired == NormalPulseTransport::NativePoll &&
        !slot.retryNeutralPending &&
        (current.phase == NormalInputPolicy::Phase::Press ||
         current.phase == NormalInputPolicy::Phase::PreNeutral)) {
        // AI branch has no held latch; native does, so re-enter AwaitingStart and sample the live latch.
        RelinquishPreparedNormalForConflict(
            playerNum, current.generation,
            "exact-tick normal changed from AI to native control");
        return false;
    }

    if (transport == NormalPulseTransport::NativePoll &&
        desired == NormalPulseTransport::AiPostWrite &&
        !ClearNativeNormalHeldLatch(character, current.intent)) {
        LogOut("[AUTO-NORMAL] Could not clear native held latch during AI hand-off P" +
               std::to_string(playerNum) + " gen=" +
               std::to_string(current.generation), true);
    }
    slot.transport = desired;
    transport = desired;
    LogOut("[AUTO-NORMAL] P" + std::to_string(playerNum) +
           " controller changed; pulse continues via " +
           NormalPulseTransportName(desired), detailedLogging.load());
    return true;
}

bool TryPrepareNormalRoute(int playerNum,
                           uintptr_t character,
                           NormalProcessRoute& outRoute) {
    outRoute = {};
    if (playerNum < 1 || playerNum > 2 || !character ||
        g_onlineModeActive.load(std::memory_order_relaxed)) {
        return false;
    }

    NormalPulseRuntimeSlot& slot = s_normalPulse[playerNum];
    NormalInputPolicy::Snapshot snapshot;
    NormalPulseTransport transport = NormalPulseTransport::Unselected;
    uintptr_t expectedGameState = 0;
    uintptr_t expectedCharacter = 0;
    bool retryNeutral = false;
    bool abortAfterRetryNeutral = false;
    {
        std::lock_guard<std::recursive_mutex> lock(slot.mutex);
        if (!RetryPendingNormalCleanupLocked(playerNum, slot)) {
            return false;
        }
        // First matching pass after a raw release: the consumer has run, the 64-Hz writer may resume.
        if (slot.rawReleaseGuard) {
            slot.rawReleaseGuard = false;
            s_normalRawRegisterOwner[playerNum].store(
                false, std::memory_order_release);
            if (!slot.state.Active()) {
                slot.gameState = 0;
                slot.character = 0;
            }
        }
        snapshot = slot.state.Current();
        transport = slot.transport;
        expectedGameState = slot.gameState;
        expectedCharacter = slot.character;
        retryNeutral = slot.retryNeutralPending;
        abortAfterRetryNeutral = slot.abortAfterRetryNeutral;
    }
    if (!snapshot) return false;

    const uintptr_t gameState = ReadLiveGameState();
    if (!gameState || gameState != expectedGameState ||
        character != expectedCharacter || character != GetPlayerPointer(playerNum)) {
        std::lock_guard<std::recursive_mutex> lock(slot.mutex);
        const auto current = slot.state.Current();
        if (current && current.generation == snapshot.generation) {
            AbortNormalPulseLocked(slot, playerNum,
                                   "world/character identity changed");
        }
        return false;
    }

    const bool nativeGateOpen = NativeNormalInputGateOpen();
    const bool consumerGateOpen = NormalConsumerGateOpen(playerNum, character);

    if (!nativeGateOpen && !retryNeutral) {
        (void)AbortExpectedImmediateNormal(
            playerNum, snapshot, expectedGameState, expectedCharacter,
            "exact-tick normal reached a closed round/input gate");
        return false;
    }

    // Poll/tutorial owners are authoritative; an exact-tick dash normal is retired, not deferred.
    if (PreparedNormalHasHigherPriorityOwner(playerNum)) {
        RelinquishPreparedNormalForConflict(
            playerNum, snapshot.generation,
            "exact-tick normal conflicted with an input owner");
        return false;
    }
    if (snapshot.phase != NormalInputPolicy::Phase::AwaitingStart &&
        !ReconcileNormalTransport(
            playerNum, character, snapshot, transport)) {
        return false;
    }

    if (retryNeutral) {
        outRoute.active = true;
        outRoute.player = playerNum;
        outRoute.mask = 0;
        outRoute.retryNeutral = true;
        outRoute.abortAfterWrite = abortAfterRetryNeutral;
        outRoute.transport = transport;
        outRoute.snapshot = snapshot;
        return true;
    }

    // updateEntityState returns before the consumer during contact/RG/super freeze; press waits for a real consumer tick.
    const bool releasePhase =
        snapshot.phase == NormalInputPolicy::Phase::ReleaseNeutral;
    const bool startWindowOpen = releasePhase ||
        (consumerGateOpen &&
         NormalPostureReady(character, snapshot));
    if (!startWindowOpen) {
        if (AbortExpectedImmediateNormal(
                playerNum, snapshot, expectedGameState, expectedCharacter,
                "exact-tick normal had no consumer/action window")) {
            return false;
        }
        // A launch/landing/freeze can follow a native pre-neutral; relinquish and wait from AwaitingStart.
        if ((snapshot.phase == NormalInputPolicy::Phase::Press ||
             snapshot.phase == NormalInputPolicy::Phase::PreNeutral) &&
            transport != NormalPulseTransport::Unselected) {
            std::lock_guard<std::recursive_mutex> lock(slot.mutex);
            const auto current = slot.state.Current();
            if (current && current.generation == snapshot.generation &&
                (current.phase == NormalInputPolicy::Phase::Press ||
                 current.phase == NormalInputPolicy::Phase::PreNeutral) &&
                slot.state.RestartAwaiting()) {
                slot.transport = NormalPulseTransport::Unselected;
                slot.deliveredAnyPhase = false;
                slot.pressAwaitingConfirmation = false;
                slot.pressBeforeMove = -1;
                slot.pressBeforeFrame = -1;
                slot.pressAttempts = 0;
                slot.retryNeutralPending = false;
                slot.abortAfterRetryNeutral = false;
                s_normalRawRegisterOwner[playerNum].store(
                    false, std::memory_order_release);
            }
        }
        return false;
    }

    if (snapshot.phase == NormalInputPolicy::Phase::AwaitingStart) {
        uint32_t aiFlag = 0xFFFFFFFFu;
        if (!SafeReadMemory(character + AI_CONTROL_FLAG_OFFSET,
                            &aiFlag, sizeof(aiFlag))) {
            (void)AbortExpectedImmediateNormal(
                playerNum, snapshot, expectedGameState, expectedCharacter,
                "exact-tick normal could not read controller mode");
            return false;
        }
        transport = aiFlag == 0
            ? NormalPulseTransport::NativePoll
            : NormalPulseTransport::AiPostWrite;
        // AI branch clears +394..397 every pass; native control uses the real +400..412 latches.
        const bool held = transport == NormalPulseTransport::NativePoll &&
            RequestedNativeNormalButtonHeld(character, snapshot.intent);

        // An exact dash-normal cannot manufacture a neutral edge; if the button is held, fail closed.
        if (held && snapshot.timing == NormalInputPolicy::Timing::Immediate) {
            (void)AbortExpectedImmediateNormal(
                playerNum, snapshot, expectedGameState, expectedCharacter,
                "exact-tick normal required a pre-neutral edge");
            return false;
        }

        std::lock_guard<std::recursive_mutex> lock(slot.mutex);
        const NormalInputPolicy::Snapshot current = slot.state.Current();
        if (!current || current.generation != snapshot.generation ||
            current.phase != NormalInputPolicy::Phase::AwaitingStart ||
            !slot.state.Prepare(held)) {
            return false;
        }
        slot.transport = transport;
        slot.deliveredAnyPhase = false;
        slot.pressAwaitingConfirmation = false;
        slot.pressBeforeMove = -1;
        slot.pressBeforeFrame = -1;
        slot.pressAttempts = 0;
        slot.retryNeutralPending = false;
        slot.abortAfterRetryNeutral = false;
        // Both routes populate +392..397; keep competing writers out until the consumer and next neutral pass.
        s_normalRawRegisterOwner[playerNum].store(
            true, std::memory_order_release);
        snapshot = slot.state.Current();
    }

    if (!snapshot || snapshot.phase == NormalInputPolicy::Phase::AwaitingStart ||
        transport == NormalPulseTransport::Unselected) {
        return false;
    }

    outRoute.active = true;
    outRoute.player = playerNum;
    outRoute.transport = transport;
    outRoute.snapshot = snapshot;
    outRoute.mask = snapshot.phase == NormalInputPolicy::Phase::Press
        ? NormalInputPolicy::ResolveMask(
              snapshot.intent,
              PredictFacingRightForConsumer(playerNum, character))
        : 0;
    return true;
}

void AcknowledgeNormalRoute(const NormalProcessRoute& route,
                            short beforeMove,
                            short afterMove) {
    if (!route.active || !route.observed || route.player < 1 || route.player > 2) {
        return;
    }

    NormalPulseRuntimeSlot& slot = s_normalPulse[route.player];
    bool releasePosted = false;
    bool acknowledged = false;
    NormalInputPolicy::Phase nextPhase = NormalInputPolicy::Phase::Idle;
    {
        std::lock_guard<std::recursive_mutex> lock(slot.mutex);
        const NormalInputPolicy::Snapshot current = slot.state.Current();
        if (current && current.generation == route.snapshot.generation &&
            current.phase == route.snapshot.phase &&
            slot.transport == route.transport) {
            releasePosted = current.phase ==
                NormalInputPolicy::Phase::ReleaseNeutral;
            acknowledged = slot.state.Acknowledge(route.snapshot);
            nextPhase = slot.state.Current().phase;
            if (acknowledged) {
                slot.deliveredAnyPhase = true;
            }
            if (acknowledged && releasePosted) {
                const bool successorPromoted = slot.state.Active();
                slot.transport = NormalPulseTransport::Unselected;
                slot.deliveredAnyPhase = false;
                slot.rawReleaseGuard = true;
                // Leave raw ownership asserted until the updateEntityState hook returns.
                (void)successorPromoted;
            }
        }
    }

    if (!acknowledged) {
        LogOut("[AUTO-NORMAL] Ignored stale pass acknowledgement P" +
               std::to_string(route.player) + " gen=" +
               std::to_string(route.snapshot.generation), true);
        return;
    }

    LogOut("[AUTO-NORMAL] Consumed P" + std::to_string(route.player) +
           " gen=" + std::to_string(route.snapshot.generation) +
           " transport=" + NormalPulseTransportName(route.transport) +
           " phase=" + NormalPulsePhaseName(route.snapshot.phase) +
           " mask=0x" + FormatHexByte(route.mask) +
           " move=" + std::to_string(beforeMove) + "->" +
           std::to_string(afterMove) +
           " next=" + NormalPulsePhaseName(nextPhase),
           detailedLogging.load());

}

NormalRegisterWriteResult WriteAuthoritativeNormalMask(int playerNum,
                                                       uintptr_t character,
                                                       uint8_t mask) {
    if (!character || GetPlayerPointer(playerNum) != character) {
        return NormalRegisterWriteResult::Failed;
    }

    // Scrub the producer's command lanes, then write all four pulse bytes plus our exact direction.
    return WriteOwnedNormalRegisters(character, mask);
}

bool NeutralizeNativeNormalHistorySample(uintptr_t character,
                                         uint16_t headBefore,
                                         uint8_t expectedSample) {
    if (!character || headBefore >= INPUT_BUFFER_SIZE) return false;
    uint16_t headAfter = 0;
    uint8_t sample = 0;
    const uint16_t expectedAfter = static_cast<uint16_t>(
        (headBefore + 1u) % INPUT_BUFFER_SIZE);
    if (!SafeReadMemory(character + INPUT_BUFFER_INDEX_OFFSET,
                        &headAfter, sizeof(headAfter)) ||
        headAfter != expectedAfter ||
        !SafeReadMemory(character + INPUT_BUFFER_OFFSET + headBefore,
                        &sample, sizeof(sample)) ||
        sample != expectedSample) {
        return false;
    }

    // Remove only our just-written history byte; physical history stays untouched.
    const uint8_t neutral = 0;
    return SafeWriteMemory(character + INPUT_BUFFER_OFFSET + headBefore,
                           &neutral, sizeof(neutral));
}

int RunNormalRoute(int characterPtr,
                   NormalProcessRoute route) {
    NormalPulseRuntimeSlot& slot = s_normalPulse[route.player];
    std::unique_lock<std::recursive_mutex> processLock(slot.mutex);
    const NormalInputPolicy::Snapshot current = slot.state.Current();
    if (!current || current.generation != route.snapshot.generation ||
        current.phase != route.snapshot.phase ||
        slot.transport != route.transport) {
        processLock.unlock();
        return CallOriginalProcessCharacterInputWithContext(
            characterPtr, route.player);
    }

    // Recheck controller mode and owners with the slot locked; preflight and producer are separate sync points.
    if (PreparedNormalHasHigherPriorityOwner(route.player)) {
        RelinquishPreparedNormalForConflict(
            route.player, route.snapshot.generation,
            "exact-tick normal lost its producer pass to an input owner");
        processLock.unlock();
        EnsureHumanControlForActivePollOverride(characterPtr, route.player);
        return CallOriginalProcessCharacterInputWithContext(
            characterPtr, route.player);
    }
    if (!ReconcileNormalTransport(
            route.player, static_cast<uintptr_t>(characterPtr),
            route.snapshot, route.transport)) {
        processLock.unlock();
        return CallOriginalProcessCharacterInputWithContext(
            characterPtr, route.player);
    }

    short beforeMove = -1;
    short beforeFrame = -1;
    (void)SafeReadMemory(static_cast<uintptr_t>(characterPtr) + MOVE_ID_OFFSET,
                         &beforeMove, sizeof(beforeMove));
    (void)SafeReadMemory(static_cast<uintptr_t>(characterPtr) +
                             CURRENT_FRAME_INDEX_OFFSET,
                         &beforeFrame, sizeof(beforeFrame));

    int ret = 0;
    if (route.transport == NormalPulseTransport::NativePoll) {
        uint16_t historyHeadBefore = 0;
        uint32_t selectedHeldBefore = 0;
        const int selectedButtonIndex = NormalButtonIndex(route.snapshot.intent);
        const bool capturedNativeState = selectedButtonIndex >= 0 &&
            SafeReadMemory(
            static_cast<uintptr_t>(characterPtr) + INPUT_BUFFER_INDEX_OFFSET,
            &historyHeadBefore, sizeof(historyHeadBefore)) &&
            SafeReadMemory(
                static_cast<uintptr_t>(characterPtr) +
                    kNativeHeldButtonOffsets[selectedButtonIndex],
                &selectedHeldBefore, sizeof(selectedHeldBefore));
        NormalProcessRoute pollRoute = route;
        BindNativeNormalPollRoute(&pollRoute);
        ret = CallOriginalProcessCharacterInputWithContext(
            characterPtr, route.player);
        BindNativeNormalPollRoute(nullptr);
        route.nativePollObserved = pollRoute.observed;
        // Keep native edge/held bookkeeping but make the consumer hand-off authoritative.
        if (route.nativePollObserved) {
            // EFZ stores directions plus button rising edges, not the held mask; derive from the captured DWORD latch.
            const uint8_t expectedHistorySample =
                NormalInputPolicy::NativeHistorySample(
                    route.mask, route.snapshot.intent.button,
                    selectedHeldBefore != 0);
            const bool historyNeutralized = capturedNativeState &&
                NeutralizeNativeNormalHistorySample(
                    static_cast<uintptr_t>(characterPtr), historyHeadBefore,
                    expectedHistorySample);
            const NormalRegisterWriteResult writeResult = historyNeutralized
                ? WriteAuthoritativeNormalMask(
                      route.player, static_cast<uintptr_t>(characterPtr), route.mask)
                : (ClearOwnedNormalRegisters(
                       static_cast<uintptr_t>(characterPtr))
                       ? NormalRegisterWriteResult::ClearedAfterFailure
                       : NormalRegisterWriteResult::Failed);
            route.observed =
                writeResult == NormalRegisterWriteResult::Written ||
                (route.mask == 0 &&
                 writeResult == NormalRegisterWriteResult::ClearedAfterFailure);
            if (!route.observed) {
                // Our poll ran, so its held latch is ours to remove.
                (void)ClearNativeNormalHeldLatch(
                    static_cast<uintptr_t>(characterPtr), route.snapshot.intent);
                if (!historyNeutralized) {
                    LogOut("[AUTO-NORMAL] Native history hand-off could not be neutralized P" +
                           std::to_string(route.player) + " gen=" +
                           std::to_string(route.snapshot.generation), true);
                }
            }
        }
    } else {
        // The consumer reads +394..397 after this pass; post the pulse only after AI generation finishes.
        ret = CallOriginalProcessCharacterInputWithContext(
            characterPtr, route.player);
        uint32_t aiFlagAfterProducer = 0;
        const bool controllerStillAi =
            SafeReadMemory(static_cast<uintptr_t>(characterPtr) +
                               AI_CONTROL_FLAG_OFFSET,
                           &aiFlagAfterProducer, sizeof(aiFlagAfterProducer)) &&
            aiFlagAfterProducer != 0;
        if (PreparedNormalHasHigherPriorityOwner(route.player) ||
            !controllerStillAi) {
            // Another owner may already own the raw lane; retire without clearing those bytes.
            RelinquishPreparedNormalForConflict(
                route.player, route.snapshot.generation,
                "exact-tick normal lost AI ownership during its producer pass",
                false);
            return ret;
        } else {
            const NormalRegisterWriteResult writeResult =
                WriteAuthoritativeNormalMask(
                    route.player, static_cast<uintptr_t>(characterPtr), route.mask);
            route.observed =
                writeResult == NormalRegisterWriteResult::Written ||
                (route.mask == 0 &&
                 writeResult == NormalRegisterWriteResult::ClearedAfterFailure);
        }
    }

    short afterMove = beforeMove;
    (void)SafeReadMemory(static_cast<uintptr_t>(characterPtr) + MOVE_ID_OFFSET,
                         &afterMove, sizeof(afterMove));
    // Clear held/pulse only if our HookedPoll won; a later owner's edge must stay untouched.
    const bool mayClearThisRouteDelivery =
        route.transport == NormalPulseTransport::AiPostWrite ||
        route.nativePollObserved;
    if (route.retryNeutral) {
        if (route.observed) {
            slot.retryNeutralPending = false;
            slot.abortAfterRetryNeutral = false;
        }
        if (route.observed && route.abortAfterWrite) {
            const char* reason =
                route.snapshot.timing == NormalInputPolicy::Timing::Immediate
                    ? "exact-tick normal was rejected by the character consumer"
                    : "normal action did not start after 3 consumer attempts";
            AbortNormalPulseLocked(
                slot, route.player, reason, true,
                route.snapshot.timing == NormalInputPolicy::Timing::WhenActionable,
                true, AutoActionNormalPulseOutcome::Failed);
        } else if (!route.observed && route.abortAfterWrite) {
            AbortNormalPulseLocked(
                slot, route.player,
                "final retry-neutral could not be delivered", true,
                route.snapshot.timing == NormalInputPolicy::Timing::WhenActionable,
                mayClearThisRouteDelivery,
                AutoActionNormalPulseOutcome::Failed);
        } else if (!route.observed) {
            LogOut("[AUTO-NORMAL] Retry-neutral delivery failed P" +
                   std::to_string(route.player) + " gen=" +
                   std::to_string(route.snapshot.generation), true);
        }
    } else if (route.observed &&
               route.snapshot.phase == NormalInputPolicy::Phase::Press) {
        // Not consumed yet: updateEntityState confirms the attack transition later in this tick.
        slot.pressAwaitingConfirmation = true;
        slot.pressBeforeMove = beforeMove;
        slot.pressBeforeFrame = beforeFrame;
        if (slot.pressAttempts < 0xFF) ++slot.pressAttempts;
        slot.deliveredAnyPhase = true;
        LogOut("[AUTO-NORMAL] Posted P" + std::to_string(route.player) +
               " gen=" + std::to_string(route.snapshot.generation) +
               " transport=" + NormalPulseTransportName(route.transport) +
               " phase=press mask=0x" + FormatHexByte(route.mask) +
               " attempt=" + std::to_string(slot.pressAttempts) +
               " awaiting-consumer",
               detailedLogging.load());
    } else if (route.observed) {
        AcknowledgeNormalRoute(route, beforeMove, afterMove);
    } else if (route.snapshot.timing == NormalInputPolicy::Timing::Immediate) {
        // No hand-off on the only valid dash tick; remove partial state and never retry.
        AbortNormalPulseLocked(
            slot, route.player,
            "exact-tick normal was not delivered on its matching fighter pass",
            true, false, mayClearThisRouteDelivery,
            AutoActionNormalPulseOutcome::Failed);
    } else {
        LogOut("[AUTO-NORMAL] Input pass did not consume/write P" +
               std::to_string(route.player) + " gen=" +
               std::to_string(route.snapshot.generation) +
               " transport=" + NormalPulseTransportName(route.transport) +
               " phase=" + NormalPulsePhaseName(route.snapshot.phase),
               true);
    }

    g_lastInjectedMask[route.player] = route.mask;
    g_wasBypassBuffered[route.player] = false;
    if (route.snapshot.phase == NormalInputPolicy::Phase::ReleaseNeutral) {
        MaybePerformTailCleanup(route.player);
    }
    return ret;
}

int __fastcall HookedUpdateEntityState(int characterPtr, int /*edx*/) {
    auto hookExecution = MinHookUtils::EnterExecution(MinHookUtils::TicketFor<&HookedUpdateEntityState>());
    if (!oUpdateEntityState) return characterPtr;
    if (!hookExecution.Admitted() || !XInputShim::IsPollingActive() || !s_inputHooksEnabled.load(std::memory_order_acquire) ||
        g_onlineModeActive.load(std::memory_order_acquire)) {
        return oUpdateEntityState(characterPtr);
    }

    int playerNum = 0;
    if (characterPtr != 0 &&
        static_cast<uintptr_t>(characterPtr) == GetPlayerPointer(1)) {
        playerNum = 1;
    } else if (characterPtr != 0 &&
               static_cast<uintptr_t>(characterPtr) == GetPlayerPointer(2)) {
        playerNum = 2;
    }

    std::unique_lock<std::recursive_mutex> inputConsumerControlLock;
    if (playerNum != 0) {
        inputConsumerControlLock = std::unique_lock<std::recursive_mutex>(
            g_p2ControlMutex);
        if (!s_inputHooksEnabled.load(std::memory_order_acquire) ||
            g_onlineModeActive.load(std::memory_order_acquire)) {
            return oUpdateEntityState(characterPtr);
        }
    }

    std::unique_lock<std::recursive_mutex> motionControlLock;
    std::unique_lock<std::recursive_mutex> motionConsumerLock;
    AutoActionMotionTxnState* motionTxn = nullptr;
    bool motionPosted = false;
    bool suppressMotionConsumerForCleanup = false;
    short motionBeforeMove = -1;
    short motionBeforeFrame = -1;
    uint16_t motionTokenBefore = kNoMotionToken;
    bool motionConsumerGateKnown = false;
    bool motionConsumerGateOpen = false;
    uint16_t motionOwnBlockOrHitstop = 0;
    uint16_t motionOwnSuperflash = 0;
    uint16_t motionOpponentSuperflash = 0;
    if (IsValidTransactionPlayer(playerNum) &&
        s_motionTxnActive[playerNum].load(std::memory_order_acquire)) {
        // Match the queue/producer lock order; cleanup writes the same fields as other owners.
        motionControlLock = std::unique_lock<std::recursive_mutex>(
            g_p2ControlMutex);
        motionConsumerLock = std::unique_lock<std::recursive_mutex>(
            s_motionTxn[playerNum].mutex);
        motionTxn = &s_motionTxn[playerNum];
        if (motionTxn->phase == AutoActionMotionTxnPhase::AwaitingConsumer &&
            motionTxn->character == static_cast<uintptr_t>(characterPtr) &&
            SameMotionTxnWorldLocked(playerNum)) {
            motionPosted = true;
            (void)SafeReadMemory(static_cast<uintptr_t>(characterPtr) + MOVE_ID_OFFSET,
                                 &motionBeforeMove, sizeof(motionBeforeMove));
            (void)SafeReadMemory(static_cast<uintptr_t>(characterPtr) +
                                     CURRENT_FRAME_INDEX_OFFSET,
                                 &motionBeforeFrame, sizeof(motionBeforeFrame));
            (void)SafeReadMemory(static_cast<uintptr_t>(characterPtr) +
                                     MOTION_TOKEN_OFFSET,
                                 &motionTokenBefore, sizeof(motionTokenBefore));
            const uintptr_t opponent = GetPlayerPointer(
                playerNum == 1 ? 2 : 1);
            motionConsumerGateKnown = opponent != 0 &&
                SafeReadMemory(static_cast<uintptr_t>(characterPtr) +
                                   BLOCKSTUN_OFFSET,
                               &motionOwnBlockOrHitstop,
                               sizeof(motionOwnBlockOrHitstop)) &&
                SafeReadMemory(static_cast<uintptr_t>(characterPtr) +
                                   SUPERFLASH_FREEZE_OFFSET,
                               &motionOwnSuperflash,
                               sizeof(motionOwnSuperflash)) &&
                SafeReadMemory(opponent + SUPERFLASH_FREEZE_OFFSET,
                               &motionOpponentSuperflash,
                               sizeof(motionOpponentSuperflash));
            motionConsumerGateOpen = motionConsumerGateKnown &&
                AutoActionMotionPolicy::GenericConsumerGateOpen(
                    motionOwnBlockOrHitstop, motionOwnSuperflash,
                    motionOpponentSuperflash);
        } else if (motionTxn->phase ==
                   AutoActionMotionTxnPhase::CleanupPending) {
            suppressMotionConsumerForCleanup =
                !CleanupMotionTxnLocked(
                    playerNum, "consumer cleanup retry");
        } else if (motionTxn->phase != AutoActionMotionTxnPhase::Idle &&
                   motionTxn->phase != AutoActionMotionTxnPhase::Prepared) {
            suppressMotionConsumerForCleanup =
                !CleanupMotionTxnLocked(
                    playerNum, "consumer world/phase mismatch");
        }
    }

    NormalInputPolicy::Snapshot posted;
    NormalPulseTransport transport = NormalPulseTransport::Unselected;
    short beforeMove = -1;
    short beforeFrame = -1;
    // Hold the slot lock across the consumer call so a reset cannot expose torn command/button state.
    std::unique_lock<std::recursive_mutex> consumerLock;
    if (!suppressMotionConsumerForCleanup && playerNum != 0 &&
        s_inputHooksEnabled.load(std::memory_order_acquire)) {
        NormalPulseRuntimeSlot& slot = s_normalPulse[playerNum];
        consumerLock = std::unique_lock<std::recursive_mutex>(slot.mutex);
        const auto current = slot.state.Current();
        if (current && current.phase == NormalInputPolicy::Phase::Press &&
            slot.pressAwaitingConfirmation &&
            slot.character == static_cast<uintptr_t>(characterPtr)) {
            posted = current;
            transport = slot.transport;
            beforeMove = slot.pressBeforeMove;
            beforeFrame = slot.pressBeforeFrame;
        }
    }
    if (posted) {
        (void)SafeReadMemory(static_cast<uintptr_t>(characterPtr) + MOVE_ID_OFFSET,
                             &beforeMove, sizeof(beforeMove));
        (void)SafeReadMemory(static_cast<uintptr_t>(characterPtr) +
                                 CURRENT_FRAME_INDEX_OFFSET,
                             &beforeFrame, sizeof(beforeFrame));
    }

    const int result = suppressMotionConsumerForCleanup
        ? characterPtr : oUpdateEntityState(characterPtr);

    if (motionPosted) {
        AutoActionMotionTxnState& txn = *motionTxn;
        short motionAfterMove = -1;
        short motionAfterFrame = -1;
        uint16_t motionTokenAfter = kNoMotionToken;
        (void)SafeReadMemory(static_cast<uintptr_t>(characterPtr) + MOVE_ID_OFFSET,
                             &motionAfterMove, sizeof(motionAfterMove));
        (void)SafeReadMemory(static_cast<uintptr_t>(characterPtr) +
                                 CURRENT_FRAME_INDEX_OFFSET,
                             &motionAfterFrame, sizeof(motionAfterFrame));
        (void)SafeReadMemory(static_cast<uintptr_t>(characterPtr) +
                                 MOTION_TOKEN_OFFSET,
                             &motionTokenAfter, sizeof(motionTokenAfter));

        if (!motionConsumerGateKnown) {
            CleanupMotionTxnLocked(
                playerNum, "consumer gate read failed");
        } else if (!motionConsumerGateOpen) {
            // Returned before vtable slot 4 = not a consumer attempt; clear the first direction, bound the wait.
            const bool firstBoundaryVisit =
                !txn.consumerBoundaryVisited;
            txn.consumerBoundaryVisited = true;
            ++txn.blockedConsumerVisits;
            if (firstBoundaryVisit) {
                const std::array<uint8_t, 6> neutralImmediate =
                    {0, 0, 0, 0, 0, 0};
                (void)SafeWriteMemory(static_cast<uintptr_t>(characterPtr) +
                                          INPUT_HORIZONTAL_OFFSET,
                                      neutralImmediate.data(),
                                      neutralImmediate.size());
            }

            const bool expired = txn.blockedConsumerVisits >=
                kMaxBlockedConsumerVisits;
            if (firstBoundaryVisit || expired || detailedLogging.load()) {
                LogOut("[AA_INPUT_TXN] P" + std::to_string(playerNum) +
                           " consumer gated gen=" +
                           std::to_string(txn.generation) +
                           " blocked=" +
                           std::to_string(txn.blockedConsumerVisits) +
                           "/" + std::to_string(kMaxBlockedConsumerVisits) +
                           " own330=" +
                           std::to_string(motionOwnBlockOrHitstop) +
                           " opponent332=" +
                           std::to_string(motionOpponentSuperflash),
                       true);
            }
            if (expired) {
                CleanupMotionTxnLocked(
                    playerNum, "consumer gate remained closed");
            }
        } else {
            const bool firstBoundaryVisit =
                !txn.consumerBoundaryVisited;
            txn.consumerBoundaryVisited = true;

            const bool accepted = MotionConsumerAcceptedLocked(
                playerNum,
                motionBeforeMove, motionBeforeFrame,
                motionAfterMove, motionAfterFrame, motionTokenBefore);
            ++txn.consumerPasses;

            std::ostringstream motionLog;
            motionLog << "[AA_INPUT_TXN] P" << playerNum
                      << " consumer gen=" << txn.generation
                      << " pass=" << txn.consumerPasses
                      << "/" << txn.maxConsumerPasses
                      << " blocked=" << txn.blockedConsumerVisits
                      << " move=" << motionBeforeMove << ':' << motionBeforeFrame
                      << "->" << motionAfterMove << ':' << motionAfterFrame
                      << " token=" << motionTokenBefore << "->" << motionTokenAfter
                      << " accepted=" << (accepted ? 1 : 0);
            const bool importantConsumerPass = accepted ||
                txn.consumerPasses == 1 ||
                txn.consumerPasses >= txn.maxConsumerPasses;
            if (importantConsumerPass || detailedLogging.load()) {
                LogOut(motionLog.str(), true);
            }

            if (accepted) {
                CleanupMotionTxnLocked(
                    playerNum, "consumer accepted",
                    AutoActionMotionOutcome::Accepted);
            } else {
                // Clear the synthetic final input only on the first visit; later AI passes keep their block direction.
                if (firstBoundaryVisit) {
                    const std::array<uint8_t, 6> neutralImmediate =
                        {0, 0, 0, 0, 0, 0};
                    (void)SafeWriteMemory(static_cast<uintptr_t>(characterPtr) +
                                              INPUT_HORIZONTAL_OFFSET,
                                          neutralImmediate.data(),
                                          neutralImmediate.size());
                }
                const bool mayWait = motionTokenBefore != kNoMotionToken &&
                    txn.consumerPasses < txn.maxConsumerPasses;
                if (!mayWait) {
                    CleanupMotionTxnLocked(
                        playerNum,
                        motionTokenBefore == kNoMotionToken
                            ? "detector produced no command"
                            : "consumer rejected/expired");
                }
            }
        }
    }
    // The consumer barrier has passed; competing 64-Hz writers may resume.
    if (playerNum != 0) {
        ReleaseRawGuardAfterConsumer(playerNum);
    }
    if (!posted || playerNum == 0) return result;

    short afterMove = -1;
    short afterFrame = -1;
    (void)SafeReadMemory(static_cast<uintptr_t>(characterPtr) + MOVE_ID_OFFSET,
                         &afterMove, sizeof(afterMove));
    (void)SafeReadMemory(static_cast<uintptr_t>(characterPtr) +
                             CURRENT_FRAME_INDEX_OFFSET,
                         &afterFrame, sizeof(afterFrame));

    bool started = false;
    bool retry = false;
    bool finalRejection = false;
    {
        NormalPulseRuntimeSlot& slot = s_normalPulse[playerNum];
        const auto current = slot.state.Current();
        if (!current || current.generation != posted.generation ||
            current.phase != NormalInputPolicy::Phase::Press ||
            slot.transport != transport ||
            !slot.pressAwaitingConfirmation ||
            slot.character != static_cast<uintptr_t>(characterPtr)) {
            return result;
        }

        started = DidConsumerStartRequestedNormal(
            static_cast<uintptr_t>(characterPtr), current.intent,
            beforeMove, beforeFrame, afterMove, afterFrame);
        slot.pressAwaitingConfirmation = false;
        slot.pressBeforeMove = -1;
        slot.pressBeforeFrame = -1;
        if (started && slot.state.Acknowledge(current)) {
            RecordNormalPulseOutcomeLocked(
                slot, posted.generation,
                AutoActionNormalPulseOutcome::Accepted);
            slot.pressAttempts = 0;
            slot.retryNeutralPending = false;
            slot.abortAfterRetryNeutral = false;
        } else {
            retry = true;
            finalRejection = current.timing == NormalInputPolicy::Timing::Immediate ||
                             slot.pressAttempts >= 3;
            slot.retryNeutralPending = true;
            slot.abortAfterRetryNeutral = finalRejection;
        }
    }

    LogOut("[AUTO-NORMAL] Consumer " +
           std::string(started ? "confirmed" :
               (finalRejection ? "rejected-final" : "not-started")) +
           " P" + std::to_string(playerNum) +
           " gen=" + std::to_string(posted.generation) +
           " transport=" + NormalPulseTransportName(transport) +
           " move=" + std::to_string(beforeMove) + ":" +
           std::to_string(beforeFrame) + "->" +
           std::to_string(afterMove) + ":" + std::to_string(afterFrame) +
           (retry ? " retry-neutral" : " release-neutral"),
           detailedLogging.load() || finalRejection);
    return result;
}

} // namespace

uint32_t RetireNativeInputOwners(const EfzTmEntryV1& world) {
    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    if (world.nativeThreadId != GetCurrentThreadId() || !world.id.battleWorld ||
        g_onlineModeActive.load(std::memory_order_acquire)) return EFZ_TM_FAULTED;
    bool pending = false;
    for (int player = 1; player <= 2; ++player) {
        std::lock_guard<std::recursive_mutex> transactionLock(s_motionTxn[player].mutex);
        auto& transaction = s_motionTxn[player];
        const uintptr_t character = player == 1 ? world.player1 : world.player2;
        if (transaction.phase != AutoActionMotionTxnPhase::Idle) {
            if (transaction.gameState != world.gameSystem || transaction.character != character)
                return EFZ_TM_FAULTED;
            if (!CleanupMotionTxnLocked(player, "Practice old-world retirement",
                                        AutoActionMotionOutcome::Cancelled, &world)) pending = true;
        }
        if (s_pendingAiRestoreActive[player].load(std::memory_order_acquire)) {
            const auto& restore = s_pendingAiRestore[player];
            if (restore.gameState != world.gameSystem || restore.character != character)
                return EFZ_TM_FAULTED;
            if (SafeWriteMemory(character + AI_CONTROL_FLAG_OFFSET, &restore.aiFlag, sizeof(restore.aiFlag)))
                ClearPendingAiRestoreLocked(player);
            else pending = true;
        }
    }
    for (int player = 1; player <= 2; ++player) {
        auto& slot = s_normalPulse[player];
        std::lock_guard<std::recursive_mutex> normalLock(slot.mutex);
        const bool delivered = slot.cleanupPending || slot.deliveredAnyPhase || slot.rawReleaseGuard ||
            s_normalRawRegisterOwner[player].load(std::memory_order_acquire);
        if (delivered) {
            const uintptr_t character = player == 1 ? world.player1 : world.player2;
            if (slot.gameState != world.gameSystem || slot.character != character) return EFZ_TM_FAULTED;
            const auto snapshot = slot.state.Current();
            const auto& intent = slot.cleanupPending ? slot.cleanupIntent : snapshot.intent;
            if (ClearDeliveredNormalState(player, slot.transport, slot.gameState, slot.character,
                                          intent, &world) == NormalCleanupResult::Failed) {
                RetainFailedNormalCleanupLocked(player, slot, intent);
                pending = true;
                continue;
            }
        }
        // Past this world's last consumer: no queued generation or raw guard survives.
        CancelNormalPulseGenerationsLocked(slot);
        slot.state.Reset();
        slot.transport = NormalPulseTransport::Unselected;
        slot.gameState = 0;
        slot.character = 0;
        slot.deliveredAnyPhase = false;
        slot.rawReleaseGuard = false;
        slot.pressAwaitingConfirmation = false;
        slot.pressBeforeMove = -1;
        slot.pressBeforeFrame = -1;
        slot.pressAttempts = 0;
        slot.retryNeutralPending = false;
        slot.abortAfterRetryNeutral = false;
        slot.cleanupPending = false;
        slot.cleanupIntent = {};
        s_normalRawRegisterOwner[player].store(false, std::memory_order_release);
    }
    return pending ? EFZ_TM_PENDING : EFZ_TM_READY;
}

bool SetVanillaSwapInputRouting(bool enable) {
    std::lock_guard<std::mutex> lock(g_bindingSwapMutex);
    const bool previous = g_bindingSwapState.active;

    if (previous == enable) {
        if (!g_loggedRoutingStateOnce.exchange(true, std::memory_order_relaxed)) {
            LogOut(std::string("[INPUT_HOOK] Engine control-map swap already ")
                + (enable ? "ENABLED" : "DISABLED"), true);
        }
        return true;
    }

    BindingPair current{};
    if (!ReadBindingPair(current)) {
        LogOut("[INPUT_HOOK][ERROR] Cannot read EFZ P1/P2 live control maps", true);
        return false;
    }

    if (enable) {
        const BindingPair swapped = MakeSwappedBindingPair(current);
        if (!WriteAndVerifyBindingPair(swapped)) {
            LogOut("[INPUT_HOOK][ERROR] Failed to write/verify EFZ P1/P2 live control-map swap", true);
            return false;
        }

        g_bindingSwapState.baseline = current;
        g_bindingSwapState.active = true;
        g_loggedRoutingStateOnce.store(false, std::memory_order_relaxed);

        std::ostringstream oss;
        oss << "[INPUT_HOOK] Verified engine live control-map swap at 0x" << std::hex
            << std::uppercase << GetBindingPairAddress()
            << " | P1<-oldP2 [" << FormatBindingBlock(swapped.p1)
            << "] P2<-oldP1 [" << FormatBindingBlock(swapped.p2) << ']';
        LogOut(oss.str(), true);
        return true;
    }

    // Restore the captured baseline; if EFZ already did, leave memory untouched.
    const bool alreadyRestored = BindingPairsEqual(current, g_bindingSwapState.baseline);
    if (!alreadyRestored && !WriteAndVerifyBindingPair(g_bindingSwapState.baseline)) {
        LogOut("[INPUT_HOOK][ERROR] Failed to restore/verify EFZ P1/P2 live control maps", true);
        return false;
    }

    g_bindingSwapState = BindingSwapState{};
    g_loggedRoutingStateOnce.store(false, std::memory_order_relaxed);
    LogOut("[INPUT_HOOK] Restored verified default P1/P2 live control maps", true);
    return true;
}

static int RecordInputPollResult(unsigned int player, int result) {
    if (player == 1 || player == 2) {
        constexpr uint8_t kAttackButtons = 0xF0; // A/B/C/D
        const uint8_t mask = static_cast<uint8_t>(result);
        const uint8_t previous =
            g_lastPolledMask[player].exchange(mask, std::memory_order_acq_rel);
        const uint8_t rising = static_cast<uint8_t>(
            mask & static_cast<uint8_t>(~previous) & kAttackButtons);
        const uint32_t serial =
            g_inputPollSerial[player].fetch_add(1, std::memory_order_acq_rel) + 1;
        if (rising != 0) {
            (void)g_pollAttackEdgeJournal[player].Push(
                InputPollAttackEdgeEvent{serial, rising});
        }
    }
    return result;
}

// A tutorial lease must still run EFZ's native processor, which advances the history head and recognizes commands.
static uint8_t TutorialP2PollMask() {
    const MotionQueueSnapshot queue = GetMotionQueueSnapshot(2);
    if (queue.hasCurrentMask) {
        return queue.currentMask;
    }
    const uint8_t immediate = ImmediateInput::GetCurrentDesired(2);
    if (immediate != 0) {
        // An authored press keeps priority; the native normal relinquishes/retries.
        return immediate;
    }
    NormalProcessRoute* const nativeRoute = ActiveNativeNormalPollRoute();
    if (nativeRoute && nativeRoute->active && nativeRoute->player == 2) {
        nativeRoute->observed = true;
        return nativeRoute->mask;
    }
    // Zero is authoritative between motions; physical/CPU input must not leak into a scripted episode.
    return 0;
}

static InputHookPolicy::TutorialP2InputSource TutorialP2Source() {
    if (g_bufferFreezingActive.load(std::memory_order_relaxed) &&
        (g_activeFreezePlayer.load(std::memory_order_relaxed) == 0 ||
         g_activeFreezePlayer.load(std::memory_order_relaxed) == 2)) {
        return InputHookPolicy::TutorialP2InputSource::BufferFreeze;
    }
    if (GetMotionQueueSnapshot(2).active) {
        return InputHookPolicy::TutorialP2InputSource::MotionQueue;
    }
    return ImmediateInput::GetCurrentDesired(2) != 0
        ? InputHookPolicy::TutorialP2InputSource::ImmediatePress
        : InputHookPolicy::TutorialP2InputSource::Neutral;
}

static void ObservePhysicalPollIfRequested(unsigned int logicalPlayer,
                                           int inputManagerPtr,
                                           unsigned int playerIndex) {
    if (logicalPlayer < 1 || logicalPlayer > 2 ||
        !g_observePhysicalPoll[logicalPlayer].load(std::memory_order_acquire)) {
        return;
    }
    const int raw = oPollPlayerInputState
        ? oPollPlayerInputState(inputManagerPtr, playerIndex) : 0;
    const uint32_t previous = g_observedPhysicalPollSample[logicalPlayer].load(
        std::memory_order_relaxed);
    const uint32_t sample = PhysicalPollSamplePolicy::Next(
        previous, static_cast<uint8_t>(raw));
    g_observedPhysicalPollSample[logicalPlayer].store(
        sample, std::memory_order_release);
    g_observedPhysicalPollValid[logicalPlayer].store(
        true, std::memory_order_release);
}

// __fastcall matches the __thiscall trampoline signature.
static int __fastcall HookedPollPlayerInputState(int inputManagerPtr, int /*edx*/, unsigned int playerIndex)
{
    auto hookExecution = MinHookUtils::EnterExecution(MinHookUtils::TicketFor<&HookedPollPlayerInputState>());
    if (!hookExecution.Admitted() || !XInputShim::IsPollingActive() || !s_inputHooksEnabled.load(std::memory_order_acquire)
        || g_onlineModeActive.load(std::memory_order_relaxed)) {
        return oPollPlayerInputState ? oPollPlayerInputState(inputManagerPtr, playerIndex) : 0;
    }

    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    if (!s_inputHooksEnabled.load(std::memory_order_acquire) ||
        g_onlineModeActive.load(std::memory_order_acquire)) {
        return oPollPlayerInputState
            ? oPollPlayerInputState(inputManagerPtr, playerIndex) : 0;
    }

    // Engine uses 0 for P1 and 1 for P2; our globals use 1=P1, 2=P2.
    const unsigned int idxFromPollArg = (playerIndex <= 1) ? (playerIndex + 1) : 0;

    // Prefer process context over the raw poll arg; side-switching can poll P1's binding for the P2 character.
    const int processContext = s_processInputPlayerContext.load(std::memory_order_relaxed);
    const unsigned int idxFromProcessContext =
        (processContext == 1 || processContext == 2) ? static_cast<unsigned int>(processContext) : 0;

    // The mission startup gate precedes every ordinary producer; none may leak a first input into the authored state.
    if (InputHookPolicy::P1StartupNeutralApplies(
            s_p1StartupNeutralGate.load(std::memory_order_acquire),
            static_cast<int>(idxFromProcessContext),
            static_cast<int>(idxFromPollArg))) {
        return RecordInputPollResult(1, 0);
    }

    // A scoped transaction owns this fighter's poll and precedes the general override lanes.
    if (IsValidTransactionPlayer(
            static_cast<int>(idxFromProcessContext)) &&
        s_motionTxnActive[idxFromProcessContext].load(
            std::memory_order_acquire)) {
        AutoActionMotionTxnState& txn = s_motionTxn[idxFromProcessContext];
        std::lock_guard<std::recursive_mutex> lock(txn.mutex);
        if (txn.phase == AutoActionMotionTxnPhase::Prepared &&
            txn.pollActive) {
            txn.pollObserved = true;
            return RecordInputPollResult(
                idxFromProcessContext, static_cast<int>(txn.pollMask));
        }
    }

    // Process context is authoritative under side routing; count the override hit.
    if (idxFromProcessContext == 2 && TutorialP2ControlLeaseActive()) {
        g_pollOverrideHitCount[2].fetch_add(1, std::memory_order_relaxed);
        return RecordInputPollResult(2, static_cast<int>(TutorialP2PollMask()));
    }

    if (idxFromProcessContext <= 2 && idxFromProcessContext != 0
        && g_pollOverrideActive[idxFromProcessContext].load(std::memory_order_relaxed)) {
        if (idxFromPollArg != 0 && idxFromPollArg != idxFromProcessContext
            && !s_loggedPollContextReroute[idxFromProcessContext].exchange(true, std::memory_order_relaxed)) {
            std::ostringstream oss;
            oss << "[INPUT_HOOK][POLL_OVERRIDE] Context-routed poll override: process=P"
                << idxFromProcessContext
                << " rawPollIndex=" << playerIndex
                << " rawPollPlayer=P" << idxFromPollArg;
            LogOut(oss.str(), true);
        }
        g_pollOverrideHitCount[idxFromProcessContext].fetch_add(1, std::memory_order_relaxed);
        ObservePhysicalPollIfRequested(idxFromProcessContext, inputManagerPtr, playerIndex);
        return RecordInputPollResult(
            idxFromProcessContext,
            static_cast<int>(g_pollOverrideMask[idxFromProcessContext].load(std::memory_order_relaxed)));
    }

    if (idxFromPollArg <= 2 && idxFromPollArg != 0
        && g_pollOverrideActive[idxFromPollArg].load(std::memory_order_relaxed)) {
        g_pollOverrideHitCount[idxFromPollArg].fetch_add(1, std::memory_order_relaxed);
        ObservePhysicalPollIfRequested(idxFromPollArg, inputManagerPtr, playerIndex);
        return RecordInputPollResult(
            idxFromPollArg,
            static_cast<int>(g_pollOverrideMask[idxFromPollArg].load(std::memory_order_relaxed)));
    }

    NormalProcessRoute* const nativeRoute =
        idxFromProcessContext != 0 ? ActiveNativeNormalPollRoute() : nullptr;
    if (nativeRoute && nativeRoute->active &&
        nativeRoute->player == static_cast<int>(idxFromProcessContext)) {
        nativeRoute->observed = true;
        return RecordInputPollResult(
            idxFromProcessContext,
            static_cast<int>(nativeRoute->mask));
    }

    const int result = oPollPlayerInputState
        ? oPollPlayerInputState(inputManagerPtr, playerIndex) : 0;
    const unsigned int logicalPlayer = idxFromProcessContext != 0
        ? idxFromProcessContext : idxFromPollArg;
    return RecordInputPollResult(logicalPlayer, result);
}

// __fastcall for __thiscall hooks from MinHook.
int __fastcall HookedProcessCharacterInput(int characterPtr, int edx) {
    auto hookExecution = MinHookUtils::EnterExecution(MinHookUtils::TicketFor<&HookedProcessCharacterInput>());
    if (!hookExecution.Admitted() || !XInputShim::IsPollingActive() || !s_inputHooksEnabled.load(std::memory_order_acquire)
        || g_onlineModeActive.load(std::memory_order_relaxed)) {
        return oProcessCharacterInput(characterPtr);
    }

    uintptr_t p1Ptr = GetPlayerPointer(1);
    uintptr_t p2Ptr = GetPlayerPointer(2);

    int playerNum = 0;
    if (characterPtr != 0 && characterPtr == p1Ptr) playerNum = 1;
    else if (characterPtr != 0 && characterPtr == p2Ptr) playerNum = 2;

    if (playerNum == 0) {
        return oProcessCharacterInput(characterPtr);
    }

    // Serialize the producer with controller changes, raw writers, transactions, and netplay publication.
    std::unique_lock<std::recursive_mutex> p2ProducerControlLock;
    if (playerNum != 0) {
        p2ProducerControlLock = std::unique_lock<std::recursive_mutex>(g_p2ControlMutex);
        // If this producer waited behind netplay publication, bypass all mod writers.
        if (!s_inputHooksEnabled.load(std::memory_order_acquire) ||
            g_onlineModeActive.load(std::memory_order_acquire)) {
            return oProcessCharacterInput(characterPtr);
        }
    }

    if (playerNum == 1 &&
        s_p1StartupNeutralGate.load(std::memory_order_acquire)) {
        // The startup owner beats every producer; neutralize immediate registers on both sides of the native processor.
        (void)WritePlayerInputImmediate(1, 0);
        const int result =
            CallOriginalProcessCharacterInputWithContext(characterPtr, 1);
        (void)WritePlayerInputImmediate(1, 0);
        g_lastInjectedMask[1] = 0;
        g_wasBypassBuffered[1] = false;
        return result;
    }

    if (s_pendingAiRestoreActive[playerNum].load(
            std::memory_order_acquire)) {
        (void)RetryPendingAiRestoreLocked(
            playerNum, "next native input pass");
    }

#if EFZ_ENABLE_AUTO_ACTION_WAKE_TRACE
    InputHookWakeTraceScope wakeTrace(playerNum, static_cast<uintptr_t>(characterPtr));
#endif

    // Run once per internal sub-tick before P1 processing.
    if (g_tickIntegratedAutoActions.load() && playerNum == 1) {
        short move1 = 0, move2 = 0;
        (void)SafeReadMemory((uintptr_t)characterPtr + MOVE_ID_OFFSET, &move1, sizeof(move1));
        uintptr_t otherPtr = (characterPtr == (int)p1Ptr) ? p2Ptr : p1Ptr;
        if (otherPtr) {
            (void)SafeReadMemory(otherPtr + MOVE_ID_OFFSET, &move2, sizeof(move2));
        } else {
            move2 = -1;
        }
        AutoActionsTick_Inline(move1, move2);
#if EFZ_ENABLE_AUTO_ACTION_WAKE_TRACE
        wakeTrace.Mark("after-auto-tick");
#endif
    }

    // The helper restores the AI flag; only the token and owned history survive until the consumer.
    if (IsAutoActionMotionTransactionActive(playerNum)) {
        int transactionResult = 0;
        if (TryRunMotionTransactionProducer(
                playerNum, characterPtr, transactionResult)) {
            g_lastInjectedMask[playerNum] = 0;
            g_wasBypassBuffered[playerNum] = false;
#if EFZ_ENABLE_AUTO_ACTION_WAKE_TRACE
            wakeTrace.SetExit("exit:auto-motion-transaction");
#endif
            return transactionResult;
        }
    }

    // Never inject during this player's buffer-freeze; let the original logic read the frozen state.
    if (g_bufferFreezingActive.load()) {
        int freezeOwner = g_activeFreezePlayer.load();
        if (freezeOwner == playerNum || freezeOwner == 0) {
            RelinquishPreparedNormalForConflict(
                playerNum, 0,
                "exact-tick normal conflicted with buffer freeze");
            if (freezeOwner == playerNum) {
                static std::chrono::steady_clock::time_point s_lastSkipLogAt[3] = { {}, {}, {} };
                auto now = std::chrono::steady_clock::now();
                bool timeOk = (s_lastSkipLogAt[playerNum].time_since_epoch().count() == 0) ||
                              ((now - s_lastSkipLogAt[playerNum]) >= std::chrono::milliseconds(250));
                if (detailedLogging.load() && timeOk) {
                    LogOut(std::string("[INPUT_HOOK] Skipping injection for P") + std::to_string(playerNum) +
                           " due to active buffer-freeze (owner=P" + std::to_string(freezeOwner) + ")", true);
                    s_lastSkipLogAt[playerNum] = now;
                }
            }
            g_lastInjectedMask[playerNum] = 0;
            // Intentionally skip tail cleanup during freeze
#if EFZ_ENABLE_AUTO_ACTION_WAKE_TRACE
            wakeTrace.Mark("before-original:freeze-skip");
            wakeTrace.SetExit("exit:freeze-skip");
#endif
            return CallOriginalProcessCharacterInputWithContext(characterPtr, playerNum);
        }
    }

    NormalProcessRoute normalRoute;
    const bool normalRouteReady = TryPrepareNormalRoute(
        playerNum, static_cast<uintptr_t>(characterPtr), normalRoute);

    // A tutorial P2 lease must call the original processor; bypassing leaves a valid 66 pattern inert.
    const bool tutorialOwnsP2 = playerNum == 2 && TutorialP2ControlLeaseActive();
    if (tutorialOwnsP2 &&
        InputHookPolicy::TutorialP2Route(TutorialP2Source()) ==
            InputHookPolicy::ProcessRoute::NativePoll) {
        if (normalRouteReady) {
#if EFZ_ENABLE_AUTO_ACTION_WAKE_TRACE
            wakeTrace.Mark("before-original:tutorial-normal-pulse");
            wakeTrace.SetExit("exit:tutorial-normal-pulse");
#endif
            return RunNormalRoute(characterPtr, normalRoute);
        }
        const uint8_t currentMask = TutorialP2PollMask();
        g_lastInjectedMask[2] = currentMask;
        g_wasBypassBuffered[2] = false;
#if EFZ_ENABLE_AUTO_ACTION_WAKE_TRACE
        wakeTrace.Mark("before-original:tutorial-poll");
#endif
        const int ret = CallOriginalProcessCharacterInputWithContext(characterPtr, 2);
        MaybePerformTailCleanup(2);
#if EFZ_ENABLE_AUTO_ACTION_WAKE_TRACE
        wakeTrace.SetExit("exit:tutorial-poll");
#endif
        return ret;
    }

    // Trigger normals precede the legacy bypass; macro/poll overrides and freezes remain authoritative.
    if (normalRouteReady) {
#if EFZ_ENABLE_AUTO_ACTION_WAKE_TRACE
        wakeTrace.Mark("before-original:normal-pulse");
        wakeTrace.SetExit("exit:normal-pulse");
#endif
        return RunNormalRoute(characterPtr, normalRoute);
    }

    bool shouldInject = false;
    const MotionQueueSnapshot legacyQueue =
        playerNum > 0 ? GetMotionQueueSnapshot(playerNum)
                      : MotionQueueSnapshot{};
    const bool exclusiveMacroOwnsPlayer = playerNum > 0 &&
        MacroController::IsExclusivePlayback() &&
        MacroController::GetPlaybackPlayer() == playerNum;
    if (playerNum > 0 && !exclusiveMacroOwnsPlayer) {
        if (g_manualInputOverride[playerNum].load()) {
            shouldInject = true;
        } else if (legacyQueue.active) {
            shouldInject = true;
        }
    }

    if (shouldInject) {
        uint8_t currentMask = 0;
        if (g_manualInputOverride[playerNum].load()) {
            currentMask = g_manualInputMask[playerNum].load();
        } else if (legacyQueue.hasCurrentMask) {
            currentMask = legacyQueue.currentMask;
        }
        if (g_lastInjectedMask[playerNum] != currentMask) {
            static std::chrono::steady_clock::time_point lastLogAt[3] = { {}, {}, {} };
            auto now = std::chrono::steady_clock::now();
            bool timeOk = (lastLogAt[playerNum].time_since_epoch().count() == 0) || ((now - lastLogAt[playerNum]) >= std::chrono::seconds(2));
            if (detailedLogging.load() && timeOk) {
                LogOut(std::string("[INPUT_HOOK] Injecting for P") + std::to_string(playerNum) +
                       " mask=0x" + FormatHexByte(currentMask) +
                       (g_injectImmediateOnly[playerNum].load() ? " (immediate-only)" : " (buffered)"), true);
                lastLogAt[playerNum] = now;
            }
        }

        // immediate-only: original first, then override regs; buffered: bypass
        // original and write both immediate+buffer. Forced bypass is always buffered.
        if (g_forceBypass[playerNum].load() || !g_injectImmediateOnly[playerNum].load()) {
            WritePlayerInputImmediate(playerNum, currentMask);
            
            // Dash patterns are written in bulk when queued; skip per-frame buffer writes.
            int currentMotion = legacyQueue.motionType;
            bool isDashMotion = (currentMotion == MOTION_FORWARD_DASH || currentMotion == MOTION_BACK_DASH);
            if (!isDashMotion) {
                // Split injection: with a poll override active, prefer it for the buffer write.
                uint8_t bufferMask = currentMask;
                if (g_pollOverrideActive[playerNum].load(std::memory_order_relaxed)) {
                    bufferMask = g_pollOverrideMask[playerNum].load(std::memory_order_relaxed);
                }
                WritePlayerInputToBuffer(playerNum, bufferMask);
            }
            
            g_lastInjectedMask[playerNum] = currentMask;
            g_wasBypassBuffered[playerNum] = true;
            MaybePerformTailCleanup(playerNum);
#if EFZ_ENABLE_AUTO_ACTION_WAKE_TRACE
            wakeTrace.SetExit("exit:bypass-buffered");
#endif
            return 0;
        } else {
            // immediate-only: write before and after; the original can overwrite registers mid-frame.
            WritePlayerInputImmediate(playerNum, currentMask);
            EnsureHumanControlForActivePollOverride(characterPtr, playerNum);
#if EFZ_ENABLE_AUTO_ACTION_WAKE_TRACE
            wakeTrace.Mark("before-original:immediate-only");
#endif
            int ret = CallOriginalProcessCharacterInputWithContext(characterPtr, playerNum);
            WritePlayerInputImmediate(playerNum, currentMask);
            g_lastInjectedMask[playerNum] = currentMask;
            g_wasBypassBuffered[playerNum] = false;
            MaybePerformTailCleanup(playerNum);
#if EFZ_ENABLE_AUTO_ACTION_WAKE_TRACE
            wakeTrace.SetExit("exit:immediate-only");
#endif
            return ret;
        }
    } 
    
    // Enforce ImmediateInput's desired mask before and after the processor so short presses survive.
    if (!exclusiveMacroOwnsPlayer) {
        uint8_t desired = ImmediateInput::GetCurrentDesired(playerNum);
        const uint8_t previousDesired =
            g_lastHookImmediateDesired[playerNum].load(std::memory_order_acquire);
        bool haveDesired = (desired != 0) || (previousDesired != 0);
        if (haveDesired) {
            WritePlayerInputImmediate(playerNum, desired);
            EnsureHumanControlForActivePollOverride(characterPtr, playerNum);
#if EFZ_ENABLE_AUTO_ACTION_WAKE_TRACE
            wakeTrace.Mark("before-original:immediate-desired");
#endif
            int ret = CallOriginalProcessCharacterInputWithContext(characterPtr, playerNum);
            WritePlayerInputImmediate(playerNum, desired);
            g_lastHookImmediateDesired[playerNum].store(
                desired, std::memory_order_release);
            g_lastInjectedMask[playerNum] = desired;
            g_wasBypassBuffered[playerNum] = false;
            MaybePerformTailCleanup(playerNum);
#if EFZ_ENABLE_AUTO_ACTION_WAKE_TRACE
            wakeTrace.SetExit("exit:immediate-desired");
#endif
            return ret;
        }
    } else {
        // Exclusive playback owns poll and registers; never let a stale ImmediateInput desire replace the macro byte.
        g_lastHookImmediateDesired[playerNum].store(0,
                                                     std::memory_order_release);
    }
    
    // After a bypassed frame, clear once so no immediate state stays latched.
    if (g_wasBypassBuffered[playerNum]) {
        WritePlayerInputImmediate(playerNum, 0);
        g_wasBypassBuffered[playerNum] = false;
    }
    uint8_t desired = exclusiveMacroOwnsPlayer
        ? 0
        : ImmediateInput::GetCurrentDesired(playerNum);
    if (desired != 0) {
        WritePlayerInputImmediate(playerNum, desired);
        g_lastInjectedMask[playerNum] = desired;
    } else {
        g_lastInjectedMask[playerNum] = 0;
    }
    {
        EnsureHumanControlForActivePollOverride(characterPtr, playerNum);
#if EFZ_ENABLE_AUTO_ACTION_WAKE_TRACE
        wakeTrace.Mark("before-original:normal");
#endif
        int ret = CallOriginalProcessCharacterInputWithContext(characterPtr, playerNum);
        MaybePerformTailCleanup(playerNum);
#if EFZ_ENABLE_AUTO_ACTION_WAKE_TRACE
        wakeTrace.SetExit("exit:normal");
#endif
        return ret;
    }
}

void SetP1StartupNeutralGate(bool active) {
    const bool previous =
        s_p1StartupNeutralGate.exchange(active, std::memory_order_acq_rel);
    if (active && !previous) {
        // Retire any continuous raw P1 source before new fighters can consume it.
        ImmediateInput::Clear(1);
    }
    if (previous != active) {
        LogOut(std::string("[INPUT_HOOK][STARTUP_GATE] P1 neutral ") +
                   (active ? "acquired" : "released"),
               true);
    }
}

bool IsP1StartupNeutralGateActive() {
    return s_p1StartupNeutralGate.load(std::memory_order_acquire);
}

void InstallInputHook() {
    uintptr_t targetAddr = 0;
    uintptr_t pollAddr = 0;
    uintptr_t stateUpdateAddr = 0;
    if (!ResolveInputHookTargets(targetAddr, pollAddr, stateUpdateAddr)) {
        return;
    }
    if ((s_processTargetAddr != targetAddr && MinHookUtils::HasOwnedTarget(reinterpret_cast<void*>(s_processTargetAddr))) ||
        (s_pollTargetAddr != pollAddr && MinHookUtils::HasOwnedTarget(reinterpret_cast<void*>(s_pollTargetAddr))) ||
        (s_stateUpdateTargetAddr != stateUpdateAddr && MinHookUtils::HasOwnedTarget(reinterpret_cast<void*>(s_stateUpdateTargetAddr)))) {
        LogOut("[INPUT_HOOK] Target replacement pending retirement of previous incarnation", true);
        return;
    }
    s_processTargetAddr = targetAddr;
    s_pollTargetAddr = pollAddr;
    s_stateUpdateTargetAddr = stateUpdateAddr;

    if (!s_inputHooksCreated.load(std::memory_order_acquire)) {
        if (!MinHookUtils::HasOwnedTarget(reinterpret_cast<void*>(stateUpdateAddr)) && !ValidateStateUpdateTarget(stateUpdateAddr)) {
            return;
        }
        if (!MinHookUtils::CreateHook(reinterpret_cast<LPVOID>(targetAddr),
                                      reinterpret_cast<void*>(&HookedProcessCharacterInput),
                                      reinterpret_cast<void**>(&oProcessCharacterInput),
                                      "[INPUT_HOOK]",
                                      "processCharacterInput", nullptr, &MinHookUtils::TicketFor<&HookedProcessCharacterInput>())) {
            return;
        }

        if (!MinHookUtils::CreateHook(reinterpret_cast<LPVOID>(pollAddr),
                                      reinterpret_cast<void*>(&HookedPollPlayerInputState),
                                      reinterpret_cast<void**>(&oPollPlayerInputState),
                                      "[INPUT_HOOK]",
                                      "pollPlayerInputState", nullptr, &MinHookUtils::TicketFor<&HookedPollPlayerInputState>())) {
            (void)MinHookUtils::RemoveHook(reinterpret_cast<LPVOID>(targetAddr), "[INPUT_HOOK]", "processCharacterInput");
            return;
        }

        if (!MinHookUtils::CreateHook(reinterpret_cast<LPVOID>(stateUpdateAddr),
                                      reinterpret_cast<void*>(&HookedUpdateEntityState),
                                      reinterpret_cast<void**>(&oUpdateEntityState),
                                      "[INPUT_HOOK]",
                                      "updateEntityState", nullptr, &MinHookUtils::TicketFor<&HookedUpdateEntityState>())) {
            (void)MinHookUtils::RemoveHook(reinterpret_cast<LPVOID>(pollAddr), "[INPUT_HOOK]", "pollPlayerInputState");
            (void)MinHookUtils::RemoveHook(reinterpret_cast<LPVOID>(targetAddr), "[INPUT_HOOK]", "processCharacterInput");
            return;
        }

        if (!MinHookUtils::EnableHook(reinterpret_cast<LPVOID>(targetAddr), "[INPUT_HOOK]", "processCharacterInput")
            || !MinHookUtils::EnableHook(reinterpret_cast<LPVOID>(pollAddr), "[INPUT_HOOK]", "pollPlayerInputState")
            || !MinHookUtils::EnableHook(reinterpret_cast<LPVOID>(stateUpdateAddr), "[INPUT_HOOK]", "updateEntityState")) {
            (void)MinHookUtils::DisableHook(reinterpret_cast<LPVOID>(stateUpdateAddr), "[INPUT_HOOK]", "updateEntityState");
            (void)MinHookUtils::DisableHook(reinterpret_cast<LPVOID>(pollAddr), "[INPUT_HOOK]", "pollPlayerInputState");
            (void)MinHookUtils::DisableHook(reinterpret_cast<LPVOID>(targetAddr), "[INPUT_HOOK]", "processCharacterInput");
            (void)MinHookUtils::RemoveHook(reinterpret_cast<LPVOID>(stateUpdateAddr), "[INPUT_HOOK]", "updateEntityState");
            (void)MinHookUtils::RemoveHook(reinterpret_cast<LPVOID>(pollAddr), "[INPUT_HOOK]", "pollPlayerInputState");
            (void)MinHookUtils::RemoveHook(reinterpret_cast<LPVOID>(targetAddr), "[INPUT_HOOK]", "processCharacterInput");
            return;
        }

        s_inputHooksCreated.store(true, std::memory_order_release);
        LogOut("[INPUT_HOOK] Hooked processCharacterInput at " + FormatHexAddress(targetAddr), true);
        LogOut("[INPUT_HOOK] Hooked pollPlayerInputState at " + FormatHexAddress(pollAddr), true);
        LogOut("[INPUT_HOOK] Hooked updateEntityState consumer at " + FormatHexAddress(stateUpdateAddr), true);
    }

    if (g_onlineModeActive.load(std::memory_order_relaxed)) {
        SetInputHookActive(false);
        return;
    }

    SetInputHookActive(true);
}

void SetInputHookActive(bool active) {
    const bool wasEnabled =
        s_inputHooksEnabled.load(std::memory_order_acquire);
    if (!active) {
        MinHookUtils::CloseAdmission("[INPUT_HOOK]");
        (void)MinHookUtils::DisableOwnedTargets("[INPUT_HOOK]", {});
        (void)MinHookUtils::ReclaimDrainedTargets("[INPUT_HOOK]", {});
        // Idempotent shutdown: no producer may remain waiting for a consumer that cannot run.
        s_inputHooksEnabled.store(false, std::memory_order_release);
        KaoriRecoilDuck::CancelAll("input hooks disabled");
        CancelAllAutoActionChargeFollowups("input hooks disabled");
        CancelAllAutoActionMotionTransactions("input hooks disabled");
        CancelAllAutoActionNormalPulses();
    }
    if (!s_inputHooksCreated.load(std::memory_order_acquire)) {
        if (active) {
            InstallInputHook();
        }
        return;
    }

    if (wasEnabled == active) {
        return;
    }

    if (active) {
        if (!MinHookUtils::EnableOwnedTargets("[INPUT_HOOK]")) return;
        s_inputHooksEnabled.store(true, std::memory_order_release);
    }
    LogOut(std::string("[INPUT_HOOK] Input hooks ") + (active ? "admission enabled" : "admission closed; physical retirement pending"), true);
}

void RemoveInputHook() {
    s_inputHooksEnabled.store(false, std::memory_order_release);
    KaoriRecoilDuck::CancelAll("input hooks removed");
    CancelAllAutoActionChargeFollowups("input hooks removed");
    CancelAllAutoActionMotionTransactions("input hooks removed");
    CancelAllAutoActionNormalPulses();
    (void)SetVanillaSwapInputRouting(false);
    if (s_processTargetAddr) {
        (void)MinHookUtils::DisableHook((LPVOID)s_processTargetAddr, "[INPUT_HOOK]", "processCharacterInput");
        (void)MinHookUtils::RemoveHook((LPVOID)s_processTargetAddr, "[INPUT_HOOK]", "processCharacterInput");
    }
    if (s_pollTargetAddr) {
        (void)MinHookUtils::DisableHook((LPVOID)s_pollTargetAddr, "[INPUT_HOOK]", "pollPlayerInputState");
        (void)MinHookUtils::RemoveHook((LPVOID)s_pollTargetAddr, "[INPUT_HOOK]", "pollPlayerInputState");
    }
    if (s_stateUpdateTargetAddr) {
        (void)MinHookUtils::DisableHook((LPVOID)s_stateUpdateTargetAddr, "[INPUT_HOOK]", "updateEntityState");
        (void)MinHookUtils::RemoveHook((LPVOID)s_stateUpdateTargetAddr, "[INPUT_HOOK]", "updateEntityState");
    }
    if (MinHookUtils::HasOwnedTarget(reinterpret_cast<void*>(s_processTargetAddr)) ||
        MinHookUtils::HasOwnedTarget(reinterpret_cast<void*>(s_pollTargetAddr)) ||
        MinHookUtils::HasOwnedTarget(reinterpret_cast<void*>(s_stateUpdateTargetAddr))) {
        LogOut("[INPUT_HOOK] Retirement pending; retaining target addresses, originals and consumer state", true);
        return;
    }
    s_inputHooksEnabled.store(false, std::memory_order_release);
    s_inputHooksCreated.store(false, std::memory_order_release);
    s_processTargetAddr = 0;
    s_pollTargetAddr = 0;
    s_stateUpdateTargetAddr = 0;
    // No consumer detour remains; force-release writer leases at this final barrier.
    for (int playerNum = 1; playerNum <= 2; ++playerNum) {
        NormalPulseRuntimeSlot& slot = s_normalPulse[playerNum];
        std::lock_guard<std::recursive_mutex> lock(slot.mutex);
        CancelNormalPulseGenerationsLocked(slot);
        slot.state.Reset();
        slot.transport = NormalPulseTransport::Unselected;
        slot.gameState = 0;
        slot.character = 0;
        slot.deliveredAnyPhase = false;
        slot.rawReleaseGuard = false;
        slot.pressAwaitingConfirmation = false;
        slot.pressBeforeMove = -1;
        slot.pressBeforeFrame = -1;
        slot.pressAttempts = 0;
        slot.retryNeutralPending = false;
        slot.abortAfterRetryNeutral = false;
        s_normalRawRegisterOwner[playerNum].store(
            false, std::memory_order_release);
    }
    LogOut("[INPUT_HOOK] Input hook removed.", true);
}
