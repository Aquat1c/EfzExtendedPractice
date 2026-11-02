#pragma once

#include <cstdint>
#include <vector>

#include "scoped_input_reservation.h"

enum class AutoActionMotionOutcome : uint8_t {
    Unknown,
    Pending,
    Accepted,
    Failed,
    Cancelled,
};

enum class AutoActionMotionSubmitResult : uint8_t {
    Accepted,
    Busy,
    Invalid,
};

using P2AutoActionMotionOutcome = AutoActionMotionOutcome;
using P2AutoActionMotionSubmitResult = AutoActionMotionSubmitResult;

// Admission snapshots the fighter and its controller state. The native-poll handoff is restored
// before collision; Accepted is published only after that fighter's consumer starts the move.
AutoActionMotionSubmitResult SubmitAutoActionMotionTransaction(
    int playerNum, int motionType, int buttonMask,
    int maxConsumerVisits = 1, uint64_t* generationOut = nullptr);
AutoActionMotionSubmitResult SubmitReservedAutoActionMotionTransaction(
    int playerNum, int motionType, int buttonMask, int maxConsumerVisits,
    uint64_t reservationToken, uint64_t* generationOut = nullptr);
AutoActionMotionSubmitResult SubmitScopedAutoActionMotionTransaction(
    int playerNum, int motionType, int buttonMask, int maxConsumerVisits,
    ScopedInputOwner reservationOwner, uint64_t reservationToken,
    uint64_t* generationOut = nullptr);
AutoActionMotionSubmitResult SubmitAutoActionPatternTransaction(
    int playerNum, const std::vector<uint8_t>& pattern,
    bool patternFacingRight, int maxConsumerVisits = 1,
    uint64_t* generationOut = nullptr);
bool IsAutoActionMotionTransactionActive(int playerNum);
AutoActionMotionOutcome GetAutoActionMotionTransactionOutcome(
    int playerNum, uint64_t generation);
// Cancels only if `generation` is still active. Delayed owners must use this form so old cleanup
// can never retire a successor.
bool CancelAutoActionMotionTransactionIfGeneration(
    int playerNum, uint64_t generation, const char* reason);
void CancelAutoActionMotionTransaction(int playerNum, const char* reason);
void CancelAllAutoActionMotionTransactions(const char* reason);

P2AutoActionMotionSubmitResult SubmitP2AutoActionMotionTransaction(
    int motionType, int buttonMask, int consumerWaitPasses = 1,
    uint64_t* generationOut = nullptr);
P2AutoActionMotionSubmitResult SubmitP2AutoActionPatternTransaction(
    const std::vector<uint8_t>& pattern, bool patternFacingRight,
    int consumerWaitPasses = 1, uint64_t* generationOut = nullptr);

bool QueueP2AutoActionMotionTransaction(int motionType, int buttonMask,
                                        int consumerWaitPasses = 1,
                                        uint64_t* generationOut = nullptr);

// patternFacingRight is the facing the encoded pattern represents, so a cross-up can be mirrored.
bool QueueP2AutoActionPatternTransaction(const std::vector<uint8_t>& pattern,
                                         bool patternFacingRight,
                                         int consumerWaitPasses = 1,
                                         uint64_t* generationOut = nullptr);

bool IsP2AutoActionMotionTransactionActive();
P2AutoActionMotionOutcome GetP2AutoActionMotionTransactionOutcome(
    uint64_t generation);
void CancelP2AutoActionMotionTransaction(const char* reason);
