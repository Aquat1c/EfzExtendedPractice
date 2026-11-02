#pragma once

#include <cstdint>

namespace AutoActionChargePolicy {

enum class Mode : uint8_t {
    Off = 0,
    InstantCharge = 1,
    FlickerInstantCharge = 2,
};

enum class PendingResolution : uint8_t {
    Continue,
    CompleteAccepted,
    CancelForContact,
};

constexpr int kGroundInstantChargeMove = 167;
constexpr int kAirInstantChargeMove = 171;
constexpr uint16_t kInstantChargeFrameFlag = 0x0008u;
constexpr int kKanoRandomMagicFirstMove = 306;
constexpr int kKanoRandomMagicLastMove = 308;
constexpr int kKanoRandomMagicLoopFromFrame = 18;
constexpr int kKanoRandomMagicLoopToFrame = 16;
constexpr int kKanoRandomMagicLoopCount = 6;

struct NativeGateSnapshot {
    bool readable = false;
    double rf = 0.0;
    int8_t blueCharge = 0;
    int32_t actionPhase = 0;
    uint16_t frameFlags = 0;
    int16_t currentRank = 0;
    int16_t instantChargeRank = 0;
    double y = 0.0;
};

// Mirrors the character-consumer gate before token 52 is submitted.
// The +0x168 field is actionPhase, not contact: authored FIC scripts may set it without a collision.
constexpr bool NativeGateOpen(const NativeGateSnapshot& gate) {
    // Retail 0x423265: red RF accepts every action phase above 1; only light-blue excludes phase 6.
    // Moving that exclusion outside the OR would reject a valid red IC during the counter phase.
    const bool resourceGate =
        (gate.rf >= 500.0 && gate.actionPhase > 1) ||
        (gate.blueCharge > 0 && gate.actionPhase > 1 &&
         gate.actionPhase != 6);
    return gate.readable && resourceGate &&
           (gate.frameFlags & kInstantChargeFrameFlag) != 0 &&
           gate.currentRank <= gate.instantChargeRank;
}

constexpr int ExpectedDestination(const NativeGateSnapshot& gate) {
    return gate.y < 0.0 ? kAirInstantChargeMove
                        : kGroundInstantChargeMove;
}

constexpr bool ReadyToSubmit(Mode mode, bool committedContactSeen,
                             const NativeGateSnapshot& gate,
                             bool fixedFicWindowAllowsPriorContact = false) {
    if (!NativeGateOpen(gate)) return false;
    switch (mode) {
        case Mode::InstantCharge:
            return committedContactSeen;
        case Mode::FlickerInstantCharge:
            return !committedContactSeen ||
                   fixedFicWindowAllowsPriorContact;
        case Mode::Off:
        default:
            return false;
    }
}

constexpr bool ContactCancels(Mode mode, bool committedContactSeen,
                              bool fixedFicWindowAllowsPriorContact = false) {
    return mode == Mode::FlickerInstantCharge && committedContactSeen &&
           !fixedFicWindowAllowsPriorContact;
}

// IC needs the direct contact hook; FIC needs both contact hooks.
// Missing hook evidence fails closed instead of manufacturing a connection.
constexpr bool ContactEvidenceSufficient(Mode mode, bool directReady,
                                         bool entityReady) {
    switch (mode) {
        case Mode::InstantCharge:
            return directReady;
        case Mode::FlickerInstantCharge:
            return directReady && entityReady;
        case Mode::Off:
        default:
            return false;
    }
}

constexpr bool EntityContactCountsForMode(Mode mode,
                                          bool sourceEntityAttributed) {
    return mode == Mode::FlickerInstantCharge || sourceEntityAttributed;
}

// EFZ consumes 22C in the character update, before the later collision resolvers run, so exact
// acceptance wins over a later contact from a lingering source-owned projectile.
constexpr PendingResolution ResolvePendingObservation(
    Mode mode, bool exactTransactionAccepted, bool committedContactSeen,
    bool fixedFicWindowAllowsPriorContact = false) {
    if (exactTransactionAccepted) {
        return PendingResolution::CompleteAccepted;
    }
    if (ContactCancels(mode, committedContactSeen,
                       fixedFicWindowAllowsPriorContact)) {
        return PendingResolution::CancelForContact;
    }
    return PendingResolution::Continue;
}

constexpr bool EntitySpawnEstablishesTemporalLineage(
    bool priorReadable, bool priorAlive,
    bool currentReadable, bool currentAlive) {
    return priorReadable && !priorAlive && currentReadable && currentAlive;
}

constexpr bool ReadableEntityDespawnClearsLineage(bool currentReadable,
                                                  bool currentAlive) {
    return currentReadable && !currentAlive;
}

constexpr bool ExactDestinationAccepted(int moveId) {
    return moveId == kGroundInstantChargeMove ||
           moveId == kAirInstantChargeMove;
}

constexpr bool SameSourceInstance(int sourceMove, int sourceFirstFrame,
                                  int currentMove, int currentFrame) {
    return sourceMove == currentMove && currentFrame >= sourceFirstFrame;
}

constexpr bool IsVerifiedKanoRandomMagicFixedFicSource(int moveId) {
    return moveId >= kKanoRandomMagicFirstMove &&
           moveId <= kKanoRandomMagicLastMove;
}

// Kano's random-magic supers loop records 16..18 six times before records 20..23 set the FIC flag.
// The 18 -> 16 edge is part of one source action, not a new same-ID action.
constexpr bool IsVerifiedKanoRandomMagicLoop(int moveId,
                                             int previousFrame,
                                             int currentFrame,
                                             int completedLoops) {
    return IsVerifiedKanoRandomMagicFixedFicSource(moveId) &&
           previousFrame == kKanoRandomMagicLoopFromFrame &&
           currentFrame == kKanoRandomMagicLoopToFrame &&
           completedLoops >= 0 &&
           completedLoops < kKanoRandomMagicLoopCount;
}

}
