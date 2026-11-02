#pragma once

#include <cstddef>
#include <cstdint>

#include "motion_constants.h"
#include "../core/constants.h"

namespace AutoActionMotionPolicy {

// updateEntityState returns before the character's slot-4 command consumer
// while either of these native freeze lanes is non-zero.
inline bool GenericConsumerGateOpen(uint16_t ownBlockOrHitstop,
                                    uint16_t ownSuperflash,
                                    uint16_t opponentSuperflash) {
    return ownBlockOrHitstop == 0 && ownSuperflash == 0 &&
           opponentSuperflash == 0;
}

struct RingPlacement {
    bool valid{false};
    uint16_t prefixStart{0};
    uint16_t prefixLength{0};
    uint16_t ownedStart{0};
    uint16_t ownedLength{0};
    uint16_t expectedHeadAfterPoll{0};
};

// EFZ's head is the next native write slot.  The prefix is placed immediately
// behind it; the final sample is supplied by the native poll at the head.
inline RingPlacement PlanRingPlacement(uint16_t head,
                                       size_t patternLength,
                                       uint16_t ringSize) {
    RingPlacement result;
    if (ringSize == 0 || head >= ringSize || patternLength == 0 ||
        patternLength > ringSize) {
        return result;
    }

    result.valid = true;
    result.prefixLength = static_cast<uint16_t>(patternLength - 1);
    result.prefixStart = static_cast<uint16_t>(
        (head + ringSize - result.prefixLength) % ringSize);
    // Detectors scan up to 135 of the 180 samples. Owning the whole ring ties the token to this
    // generation and stops old commands firing again after cleanup; the live head is preserved.
    result.ownedStart = 0;
    result.ownedLength = ringSize;
    result.expectedHeadAfterPoll = static_cast<uint16_t>((head + 1) % ringSize);
    return result;
}

inline bool ActionInstanceChanged(short beforeMove, short beforeFrame,
                                  short afterMove, short afterFrame) {
    return afterMove != beforeMove ||
           (beforeFrame >= 0 && afterFrame >= 0 && afterFrame < beforeFrame);
}

inline bool OwnsCancellationGeneration(bool transactionActive,
                                       uint64_t activeGeneration,
                                       uint64_t requestedGeneration) {
    return transactionActive && requestedGeneration != 0 &&
           activeGeneration == requestedGeneration;
}

inline bool ConsumerAccepted(int motionType,
                             short beforeMove, short beforeFrame,
                             short afterMove, short afterFrame,
                             uint16_t tokenBefore,
                             uint16_t noMotionToken) {
    if (!ActionInstanceChanged(beforeMove, beforeFrame,
                               afterMove, afterFrame)) {
        return false;
    }

    if (motionType == MOTION_FORWARD_DASH ||
        motionType == MOTION_BACK_DASH) {
        if (tokenBefore > 1) return false;
        if (motionType == MOTION_FORWARD_DASH) {
            if (afterMove == KAORI_RECOIL_DUCK_ID) {
                // Kaori enters move 251 only by consuming 66 from ground backdash 164 on frame-index 4/5.
                return beforeMove == GROUND_BACKWARD_DASH_ID &&
                       beforeFrame >= 4 && beforeFrame <= 5;
            }
            return afterMove == GROUND_FORWARD_DASH_ID ||
                   afterMove == AIR_FORWARD_DASH_ID ||
                   afterMove == KAORI_FORWARD_DASH_START_ID;
        }
        return afterMove == GROUND_BACKWARD_DASH_ID ||
               afterMove == AIR_BACKWARD_DASH_ID;
    }

    if (motionType == MOTION_22C) {
        // Only the real IC actions (167/171) prove 22C was consumed, not a token plus any attack.
        return tokenBefore != noMotionToken &&
               (afterMove == GROUND_IC_ID || afterMove == AIR_IC_ID);
    }

    return tokenBefore != noMotionToken && afterMove >= 200;
}

}
