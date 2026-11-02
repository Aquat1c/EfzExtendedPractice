#pragma once

#include <cstdint>

#include "motion_constants.h"

namespace NormalInputPolicy {

constexpr uint8_t kInputRight = 0x01;
constexpr uint8_t kInputLeft  = 0x02;
constexpr uint8_t kInputDown  = 0x04;
constexpr uint8_t kInputA     = 0x10;
constexpr uint8_t kInputB     = 0x20;
constexpr uint8_t kInputC     = 0x40;
constexpr uint8_t kInputD     = 0x80;
constexpr uint8_t kAttackButtons = kInputA | kInputB | kInputC | kInputD;

enum class RelativeDirection : uint8_t {
    Neutral,
    Down,
    Forward,
    Back,
    DownForward,
    DownBack,
};

struct Intent {
    RelativeDirection direction{RelativeDirection::Neutral};
    uint8_t button{0};
    bool airborne{false};
    short expectedMove{0};

    constexpr explicit operator bool() const {
        const uint8_t attack = static_cast<uint8_t>(button & kAttackButtons);
        return attack != 0 && (attack & static_cast<uint8_t>(attack - 1)) == 0;
    }
};

constexpr uint8_t ButtonForGroupIndex(int index) {
    return index == 0 ? kInputA
         : index == 1 ? kInputB
         : index == 2 ? kInputC
         : index == 3 ? kInputD
         : 0;
}

constexpr Intent IntentFromMotion(int motionType) {
    if (motionType >= MOTION_5A && motionType <= MOTION_5D) {
        return {RelativeDirection::Neutral,
                ButtonForGroupIndex(motionType - MOTION_5A), false};
    }
    if (motionType >= MOTION_2A && motionType <= MOTION_2D) {
        return {RelativeDirection::Down,
                ButtonForGroupIndex(motionType - MOTION_2A), false};
    }
    if (motionType >= MOTION_JA && motionType <= MOTION_JD) {
        return {RelativeDirection::Neutral,
                ButtonForGroupIndex(motionType - MOTION_JA), true};
    }
    if (motionType >= MOTION_6A && motionType <= MOTION_6D) {
        return {RelativeDirection::Forward,
                ButtonForGroupIndex(motionType - MOTION_6A), false};
    }
    if (motionType >= MOTION_4A && motionType <= MOTION_4D) {
        return {RelativeDirection::Back,
                ButtonForGroupIndex(motionType - MOTION_4A), false};
    }
    if (motionType >= MOTION_1A && motionType <= MOTION_1D) {
        return {RelativeDirection::DownBack,
                ButtonForGroupIndex(motionType - MOTION_1A), false};
    }
    if (motionType >= MOTION_3A && motionType <= MOTION_3D) {
        return {RelativeDirection::DownForward,
                ButtonForGroupIndex(motionType - MOTION_3A), false};
    }
    if (motionType >= MOTION_J2A && motionType <= MOTION_J2D) {
        return {RelativeDirection::Down,
                ButtonForGroupIndex(motionType - MOTION_J2A), true};
    }
    if (motionType >= MOTION_J6A && motionType <= MOTION_J6D) {
        return {RelativeDirection::Forward,
                ButtonForGroupIndex(motionType - MOTION_J6A), true};
    }
    return {};
}

constexpr bool IsNormalMotion(int motionType) {
    return static_cast<bool>(IntentFromMotion(motionType));
}

constexpr uint8_t ResolveMask(const Intent& intent, bool facingRight) {
    if (!intent) return 0;

    const uint8_t forward = facingRight ? kInputRight : kInputLeft;
    const uint8_t back = facingRight ? kInputLeft : kInputRight;
    uint8_t direction = 0;
    switch (intent.direction) {
        case RelativeDirection::Neutral:     direction = 0; break;
        case RelativeDirection::Down:        direction = kInputDown; break;
        case RelativeDirection::Forward:     direction = forward; break;
        case RelativeDirection::Back:        direction = back; break;
        case RelativeDirection::DownForward: direction = kInputDown | forward; break;
        case RelativeDirection::DownBack:    direction = kInputDown | back; break;
    }
    return static_cast<uint8_t>(direction | intent.button);
}

// processCharacterInput records directions plus button rising edges; held buttons are omitted.
constexpr uint8_t NativeHistorySample(uint8_t pollMask,
                                      uint8_t selectedButton,
                                      bool selectedButtonWasHeld) {
    const uint8_t direction = static_cast<uint8_t>(pollMask & 0x0F);
    const uint8_t risingEdge =
        !selectedButtonWasHeld && (pollMask & selectedButton) != 0
            ? selectedButton : 0;
    return static_cast<uint8_t>(direction | risingEdge);
}

enum class Timing : uint8_t {
    WhenActionable,
    Immediate,
};

constexpr bool PostureEligible(const Intent& intent,
                               bool groundActionable,
                               bool airActionable) {
    return static_cast<bool>(intent) &&
        (intent.airborne ? airActionable : groundActionable);
}

// Ground normals start from the PAT row's cancel bit plus a rank comparison, not a move-ID list.
// destinationRankKnown is false with no cast-wide anchor (6X/4X/1X/3X, D/S): cancel bit only.
constexpr bool RecoilGuardCancelEligible(bool inRecoilGuard,
                                         bool patCancelBitSet,
                                         int currentCancelRank,
                                         int destinationRank,
                                         bool destinationRankKnown) {
    if (!inRecoilGuard || !patCancelBitSet) return false;
    return !destinationRankKnown || currentCancelRank <= destinationRank;
}

// Cast-wide ground destination anchors, mirroring DidConsumerStartRequestedNormal
// (input_hook.cpp): neutral A/B/C -> 200/201/203, down A/B/C -> 204/205/206.
constexpr int GroundNormalRankAnchor(const Intent& intent) {
    if (intent.airborne) return -1;
    const int button = intent.button == kInputA ? 0
                     : intent.button == kInputB ? 1
                     : intent.button == kInputC ? 2
                     : -1;
    if (button < 0) return -1;
    if (intent.direction == RelativeDirection::Neutral) {
        return button == 0 ? 200 : button == 1 ? 201 : 203;
    }
    if (intent.direction == RelativeDirection::Down) {
        return 204 + button;
    }
    return -1;
}

enum class Phase : uint8_t {
    Idle,
    AwaitingStart,
    PreNeutral,
    Press,
    ReleaseNeutral,
};

enum class SubmitResult : uint8_t {
    Accepted,
    Busy,
    Invalid,
};

struct Snapshot {
    uint64_t generation{0};
    Phase phase{Phase::Idle};
    Intent intent{};
    Timing timing{Timing::WhenActionable};

    constexpr explicit operator bool() const {
        return generation != 0 && phase != Phase::Idle && static_cast<bool>(intent);
    }
};

// Only a matching target-character ProcessCharacterInput pass may acknowledge a phase.
class PulseState {
public:
    SubmitResult Submit(const Intent& intent, Timing timing,
                        uint64_t* generationOut = nullptr) {
        if (generationOut) *generationOut = 0;
        if (!intent) return SubmitResult::Invalid;

        if (phase_ != Phase::Idle &&
            (timing == Timing::Immediate || timing_ == Timing::Immediate ||
             pendingGeneration_ != 0)) {
            return SubmitResult::Busy;
        }

        ++nextGeneration_;
        if (nextGeneration_ == 0) ++nextGeneration_;
        const uint64_t generation = nextGeneration_;

        if (phase_ != Phase::Idle) {
            pendingGeneration_ = generation;
            pendingIntent_ = intent;
            pendingTiming_ = timing;
            if (generationOut) *generationOut = pendingGeneration_;
            return SubmitResult::Accepted;
        }

        generation_ = generation;
        intent_ = intent;
        timing_ = timing;
        phase_ = Phase::AwaitingStart;
        if (generationOut) *generationOut = generation_;
        return SubmitResult::Accepted;
    }

    bool Prepare(bool requestedButtonAlreadyHeld) {
        if (phase_ != Phase::AwaitingStart) return false;
        phase_ = requestedButtonAlreadyHeld ? Phase::PreNeutral : Phase::Press;
        return true;
    }

    Snapshot Current() const {
        if (phase_ == Phase::Idle) return {};
        return {generation_, phase_, intent_, timing_};
    }

    bool Acknowledge(const Snapshot& consumed) {
        if (!consumed || consumed.generation != generation_ ||
            consumed.phase != phase_) {
            return false;
        }

        switch (phase_) {
            case Phase::PreNeutral:
                phase_ = Phase::Press;
                break;
            case Phase::Press:
                phase_ = Phase::ReleaseNeutral;
                break;
            case Phase::ReleaseNeutral:
                if (pendingGeneration_ != 0) {
                    generation_ = pendingGeneration_;
                    intent_ = pendingIntent_;
                    timing_ = pendingTiming_;
                    phase_ = Phase::AwaitingStart;
                    pendingGeneration_ = 0;
                    pendingIntent_ = {};
                    pendingTiming_ = Timing::WhenActionable;
                } else {
                    phase_ = Phase::Idle;
                    generation_ = 0;
                    intent_ = {};
                    timing_ = Timing::WhenActionable;
                }
                break;
            case Phase::Idle:
            case Phase::AwaitingStart:
                return false;
        }
        return true;
    }

    bool Active() const { return phase_ != Phase::Idle; }
    bool NeedsReleaseNeutral() const { return phase_ == Phase::ReleaseNeutral; }
    bool HasPending() const { return pendingGeneration_ != 0; }
    uint64_t PendingGeneration() const { return pendingGeneration_; }

    bool RestartAwaiting() {
        if (phase_ != Phase::PreNeutral && phase_ != Phase::Press) return false;
        phase_ = Phase::AwaitingStart;
        return true;
    }

    bool RetireCurrent() {
        if (phase_ == Phase::Idle) return false;
        if (pendingGeneration_ != 0) {
            generation_ = pendingGeneration_;
            intent_ = pendingIntent_;
            timing_ = pendingTiming_;
            phase_ = Phase::AwaitingStart;
            pendingGeneration_ = 0;
            pendingIntent_ = {};
            pendingTiming_ = Timing::WhenActionable;
            return true;
        }
        phase_ = Phase::Idle;
        generation_ = 0;
        intent_ = {};
        timing_ = Timing::WhenActionable;
        return false;
    }

    void Reset() {
        phase_ = Phase::Idle;
        generation_ = 0;
        intent_ = {};
        timing_ = Timing::WhenActionable;
        pendingGeneration_ = 0;
        pendingIntent_ = {};
        pendingTiming_ = Timing::WhenActionable;
    }

private:
    uint64_t nextGeneration_{0};
    uint64_t generation_{0};
    Phase phase_{Phase::Idle};
    Intent intent_{};
    Timing timing_{Timing::WhenActionable};
    uint64_t pendingGeneration_{0};
    Intent pendingIntent_{};
    Timing pendingTiming_{Timing::WhenActionable};
};

}
