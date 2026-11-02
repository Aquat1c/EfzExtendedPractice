#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Mission::SequencePolicy {

constexpr int kMissionMonitorTicksPerSecond = 192;
constexpr int RecorderCountInTicksFromMs(int milliseconds) {
    return milliseconds <= 0
        ? 0
        : (milliseconds * kMissionMonitorTicksPerSecond + 999) / 1000;
}
constexpr int RecorderCountInRemainingMs(int remainingTicks) {
    return remainingTicks <= 0
        ? 0
        : (remainingTicks * 1000 + kMissionMonitorTicksPerSecond - 1) /
              kMissionMonitorTicksPerSecond;
}

constexpr int RecorderMenuNeutralPollCount(bool anyMenuVisible,
                                           bool freshPoll,
                                           unsigned physicalMask,
                                           int currentCount) {
    if (anyMenuVisible || !freshPoll) return anyMenuVisible ? 0 : currentCount;
    return physicalMask == 0 ? (std::min)(2, currentCount + 1) : 0;
}

constexpr bool RecorderMenuReleaseReady(bool anyMenuVisible,
                                        int neutralPollCount) {
    return !anyMenuVisible && neutralPollCount >= 2;
}

// EFZ's Practice pause does not advance input polls, so waiting for neutral polls would deadlock.
// Release after the close transaction; the zero-input lease still suppresses the button.
constexpr bool RecorderPhysicalPauseReleaseReady(bool handoffReady,
                                                  bool anyMenuVisible) {
    return handoffReady && !anyMenuVisible;
}

constexpr bool RecorderAttackBatchIsFresh(int currentFrame, int batchFrame,
                                          int windowTicks) {
    const int age = currentFrame - batchFrame;
    return age >= 0 && age <= windowTicks;
}

constexpr int kRecorderInputCausalityWindowTicks = 90;
constexpr uint8_t kAttackButtonA = 0x10;
constexpr uint8_t kAttackButtonB = 0x20;
constexpr uint8_t kAttackButtonC = 0x40;
constexpr uint8_t kAttackButtonD = 0x80;
constexpr uint8_t kAttackButtonMask =
    kAttackButtonA | kAttackButtonB | kAttackButtonC | kAttackButtonD;

constexpr bool ValidExpectedAttackMask(int mask) {
    return mask >= 0 && mask <= 255 &&
           (mask & ~static_cast<int>(kAttackButtonMask)) == 0;
}

constexpr char UpperAscii(char value) {
    return value >= 'a' && value <= 'z'
        ? static_cast<char>(value - ('a' - 'A'))
        : value;
}

constexpr bool IsNotationButtonBoundary(char value) {
    return value == '\0' || value == ' ' || value == '\t' ||
           value == '/' || value == ')' || value == '(' || value == '~' ||
           value == '-' || value == '+';
}

constexpr uint8_t AttackButtonBit(char value) {
    return UpperAscii(value) == 'A' ? kAttackButtonA
         : UpperAscii(value) == 'B' ? kAttackButtonB
         : UpperAscii(value) == 'C' ? kAttackButtonC
         : UpperAscii(value) == 'D' ? kAttackButtonD
         : 0;
}

constexpr uint8_t ExpectedAttackMaskFromNotationRange(
    const char* notation, std::size_t begin, std::size_t end) {
    uint8_t result = 0;
    for (std::size_t index = begin; index < end; ++index) {
        const char current = UpperAscii(notation[index]);
        const char previous = index == begin ? '\0' : notation[index - 1];
        const char next = index + 1 < end ? notation[index + 1] : '\0';
        const bool icTokenStart = index == begin ||
            IsNotationButtonBoundary(previous) || previous == '.' ||
            ((UpperAscii(previous) == 'B' || UpperAscii(previous) == 'F') &&
             (index == begin + 1 ||
              IsNotationButtonBoundary(notation[index - 2])));
        if (current == 'I' && UpperAscii(next) == 'C' && icTokenStart &&
            IsNotationButtonBoundary(
                index + 2 < end ? notation[index + 2] : '\0')) {
            result = static_cast<uint8_t>(result | kAttackButtonC);
            continue;
        }

        const bool strengthPrefix = index == begin ||
            (previous >= '0' && previous <= '9') || previous == '.' ||
            previous == '/' || previous == '~' || previous == ' ';
        if (!strengthPrefix || !IsNotationButtonBoundary(next)) continue;

        const uint8_t button = AttackButtonBit(current);
        if (button != 0) {
            result = static_cast<uint8_t>(result | button);
        } else if (current == 'S') {
            // EFZ's S notation is the physical D/special button.
            result = static_cast<uint8_t>(result | kAttackButtonD);
        }
    }
    return result;
}

constexpr uint8_t ExpectedAttackMaskFromNotation(const char* notation) {
    if (notation == nullptr) return 0;
    std::size_t length = 0;
    while (notation[length] != '\0') ++length;
    return ExpectedAttackMaskFromNotationRange(notation, 0, length);
}

constexpr uint8_t ExpectedAttackMaskFromFinalNotationStep(
    const char* notation) {
    if (notation == nullptr) return 0;
    std::size_t segmentStart = 0;
    uint8_t latest = 0;
    for (std::size_t index = 0;; ++index) {
        if (notation[index] != '~' && notation[index] != '\0') continue;
        const uint8_t segment = ExpectedAttackMaskFromNotationRange(
            notation, segmentStart, index);
        if (segment != 0) latest = segment;
        if (notation[index] == '\0') return latest;
        segmentStart = index + 1;
    }
}

constexpr uint8_t ExpectedAttackMaskFromMoveId(int moveId) {
    switch (moveId) {
        case 167: // ground IC
        case 171: // air IC
        case 203: case 206: case 209:
        case 232: case 235:
            return kAttackButtonC;
        case 200: case 204: case 207:
        case 230: case 233:
            return kAttackButtonA;
        case 201: case 202: case 205: case 208:
        case 231: case 234:
            return kAttackButtonB;
        default:
            return 0;
    }
}

constexpr uint8_t ExpectedAttackMaskForAction(const char* notation,
                                              int moveId) {
    const uint8_t fromNotation = ExpectedAttackMaskFromNotation(notation);
    return fromNotation != 0 ? fromNotation
                             : ExpectedAttackMaskFromMoveId(moveId);
}

constexpr bool AttackEdgeMatchesExpected(uint8_t observedMask,
                                         uint8_t expectedMask) {
    const uint8_t observedAttack =
        static_cast<uint8_t>(observedMask & kAttackButtonMask);
    return observedAttack != 0 &&
           (expectedMask == 0 || (observedAttack & expectedMask) != 0);
}

constexpr uint8_t BoundCausalAttackMask(uint8_t observedMask,
                                        uint8_t derivedExpectedMask) {
    const uint8_t observedAttack =
        static_cast<uint8_t>(observedMask & kAttackButtonMask);
    return derivedExpectedMask != 0
        ? static_cast<uint8_t>(observedAttack & derivedExpectedMask)
        : observedAttack;
}

struct RecorderAttackEdgeCandidate {
    uint32_t serial = 0;
    uint8_t mask = 0;
    int frame = 0;
};

constexpr int SelectNewestEligibleAttackEdge(
    const RecorderAttackEdgeCandidate* candidates, std::size_t count,
    uint32_t currentPollSerial, int currentFrame, int windowTicks,
    uint8_t expectedMask) {
    if (candidates == nullptr) return -1;
    for (std::size_t index = count; index-- > 0;) {
        const RecorderAttackEdgeCandidate& candidate = candidates[index];
        if (candidate.serial == 0 ||
            static_cast<int32_t>(currentPollSerial - candidate.serial) < 0 ||
            !RecorderAttackBatchIsFresh(currentFrame, candidate.frame,
                                        windowTicks) ||
            !AttackEdgeMatchesExpected(candidate.mask, expectedMask)) {
            continue;
        }
        return static_cast<int>(index);
    }
    return -1;
}

constexpr bool ShouldFoldKnownAutomaticPhase(bool knownAutomaticPhase) {
    return knownAutomaticPhase;
}

constexpr bool DeadlineAttackEdgeMatches(uint8_t observedMask,
                                         uint8_t expectedMask) {
    return AttackEdgeMatchesExpected(observedMask, expectedMask);
}

inline bool IsLegacyUnmatchedInputDiagnostic(const std::string& reason) {
    static const std::string prefix =
        " attack input batch(es) did not become recorded moves:";
    static const std::string suffix =
        "; retake or remove those presses";
    std::size_t countEnd = 0;
    while (countEnd < reason.size() &&
           reason[countEnd] >= '0' && reason[countEnd] <= '9') {
        ++countEnd;
    }
    return countEnd > 0 &&
           reason.compare(countEnd, prefix.size(), prefix) == 0 &&
           reason.size() >= suffix.size() &&
           reason.compare(reason.size() - suffix.size(), suffix.size(),
                          suffix) == 0;
}

constexpr bool RecorderStopWaitExpired(bool macroTickAdvanced, bool paused,
                                       int waitedTicks, int maxWaitTicks) {
    return !macroTickAdvanced && !paused && waitedTicks > maxWaitTicks;
}

constexpr bool PracticeAutomationSuppressed(bool demoActive,
                                             bool recorderCaptureActive) {
    return demoActive || recorderCaptureActive;
}

enum class DemoBaselineSource {
    None = 0,
    RunnerCheckpoint,
    EmbeddedDump,
};

constexpr bool EmbeddedSavestateCanLoad(bool hasEmbeddedSavestate,
                                        bool stateDumpAvailable) {
    return !hasEmbeddedSavestate || stateDumpAvailable;
}

constexpr DemoBaselineSource DecideDemoBaselineSource(
    bool recorderPreview, bool runnerCheckpointReady, bool embeddedDumpReady) {
    if (!recorderPreview && runnerCheckpointReady) {
        return DemoBaselineSource::RunnerCheckpoint;
    }
    if (embeddedDumpReady) {
        return DemoBaselineSource::EmbeddedDump;
    }
    return DemoBaselineSource::None;
}

constexpr bool DemoTrialRecipeActive(bool demoPlaying, bool recorderPreview,
                                     bool runnerActive, bool hasSteps) {
    return demoPlaying && !recorderPreview && runnerActive && hasSteps;
}

constexpr int kDemoTerminalRecipeHoldTicks = 12;

constexpr int kDemoRecipeSettleTimeoutTicks = 768; // four seconds at 192 Hz

enum class DemoRecipeEndAction {
    ContinuePlayback = 0,
    SettleFinalContacts,
    HoldTerminalFrame,
    FailThenHold,
};

constexpr DemoRecipeEndAction DecideDemoRecipeEnd(
    bool macroIdle, bool runnerTerminal, int settleTicksRemaining) {
    if (!macroIdle) return DemoRecipeEndAction::ContinuePlayback;
    if (runnerTerminal) return DemoRecipeEndAction::HoldTerminalFrame;
    return settleTicksRemaining == 1
        ? DemoRecipeEndAction::FailThenHold
        : DemoRecipeEndAction::SettleFinalContacts;
}

constexpr int RunnerDropAttemptDelta(bool demoPresentation) {
    return demoPresentation ? 0 : 1;
}

constexpr int RunnerDropRetryDelay(bool demoPresentation,
                                   int ordinaryRetryDelay) {
    return demoPresentation ? 0 : ordinaryRetryDelay;
}

enum class DemoRoundEventAction {
    Continue = 0,
    NormalizeRestoreTransient,
    RestoreBaseline,
};

constexpr int kDemoRoundRestoreSettleTicks = 12;
constexpr DemoRoundEventAction DecideDemoRoundEvent(
    bool roundEventActive, int restoreSettleTicksRemaining,
    bool bothFightersAlive) {
    if (!roundEventActive) return DemoRoundEventAction::Continue;
    return restoreSettleTicksRemaining > 0 && bothFightersAlive
        ? DemoRoundEventAction::NormalizeRestoreTransient
        : DemoRoundEventAction::RestoreBaseline;
}

constexpr bool SnapshotCanResumeAfterRestore(bool restoredWorldThisTick,
                                             bool demoActive,
                                             bool terminalHandoffThisTick) {
    return !restoredWorldThisTick && !demoActive &&
           !terminalHandoffThisTick;
}

// A same-ID cancel resets the frame counter (+0xA) to the head, but looping animations (air IC
// float, air dashes, falling tails) also rewind, so a same-ID rewind counts only at the head.
constexpr short kMoveRestartFrameMax = 2;
constexpr bool IsMoveInstanceEdge(short currentMove, short currentFrame,
                                  short previousMove, short previousFrame) {
    return currentMove != previousMove ||
           (currentFrame < previousFrame && currentFrame <= kMoveRestartFrameMax);
}

constexpr bool IsCausallyCredibleMoveInstanceEdge(
    short currentMove, short currentFrame,
    short previousMove, short previousFrame,
    bool matchingAttackEdge) {
    if (currentMove != previousMove) return true;
    return matchingAttackEdge &&
           IsMoveInstanceEdge(currentMove, currentFrame,
                              previousMove, previousFrame);
}

constexpr bool FlexibleMultiHitCanFinalize(bool allowPartialHits,
                                           int observedHits,
                                           bool moveEnded,
                                           bool expectedNextActionStarted,
                                           bool comboBoundaryObserved) {
    return allowPartialHits && observedHits > 0 &&
           (moveEnded || expectedNextActionStarted || comboBoundaryObserved);
}

enum class StrictActionInstanceDecision {
    Ignore,
    AcceptExpected,
    AcceptAutomaticPhase,
    AcceptNextOverlap,
    SkipOptional,
    RejectUnexpected,
};

constexpr StrictActionInstanceDecision DecideStrictActionInstance(
    bool instanceEdge,
    bool comboStepMove,
    bool currentStepArmed,
    bool matchesCurrentStep,
    bool previousMoveMatchesCurrentStep,
    bool matchesPreviousStep,
    bool previousMoveMatchesPreviousStep,
    bool moveIdChanged,
    bool matchesNextOverlap,
    bool matchesOptionalAhead) {
    if (!instanceEdge || !comboStepMove) {
        return StrictActionInstanceDecision::Ignore;
    }

    const bool currentAutomaticPhase = currentStepArmed && moveIdChanged &&
        matchesCurrentStep && previousMoveMatchesCurrentStep;
    const bool previousAutomaticPhase = moveIdChanged &&
        matchesPreviousStep && previousMoveMatchesPreviousStep;
    if (currentAutomaticPhase || previousAutomaticPhase) {
        return StrictActionInstanceDecision::AcceptAutomaticPhase;
    }
    if (currentStepArmed && matchesNextOverlap) {
        return StrictActionInstanceDecision::AcceptNextOverlap;
    }
    if (matchesCurrentStep && !currentStepArmed) {
        return StrictActionInstanceDecision::AcceptExpected;
    }
    if (matchesOptionalAhead) {
        return StrictActionInstanceDecision::SkipOptional;
    }
    return StrictActionInstanceDecision::RejectUnexpected;
}

constexpr bool DirectTransitionSatisfied(bool destinationInstanceEdge,
                                         bool sourceRequired,
                                         bool previousSourceMatches) {
    return destinationInstanceEdge &&
           (!sourceRequired || previousSourceMatches);
}

constexpr bool FreshBlockReactionSatisfied(bool currentBlockstun,
                                           bool currentRecoilGuard,
                                           short currentMove,
                                           short currentFrame,
                                           short previousMove,
                                           short previousFrame) {
    return currentBlockstun && !currentRecoilGuard &&
           IsMoveInstanceEdge(currentMove, currentFrame,
                              previousMove, previousFrame);
}

// Akiko 41236A/B/C is one authored input with two consecutive move IDs:
// startup 263/265/267 then automatic hit phase 264/266/268 (one recipe step).
constexpr short AkikoVacuumHitPhase(short startupMove) {
    return startupMove == 263 ? 264
         : startupMove == 265 ? 266
         : startupMove == 267 ? 268
         : 0;
}

constexpr bool IsAkikoVacuumHitTransition(short startupMove, short currentMove) {
    const short hitPhase = AkikoVacuumHitPhase(startupMove);
    return hitPhase != 0 && currentMove == hitPhase;
}

constexpr bool IsRumiCommandThrowSuccessTransition(short startupMove,
                                                   short currentMove) {
    return currentMove == 251 &&
           (startupMove == 250 || startupMove == 263 || startupMove == 264);
}

enum class ComboEndDecision {
    None,
    ConsumeRequired,
    UnexpectedDrop,
};

enum class SampledComboBoundary {
    None,
    VisibleRecovery,
    CounterRollover,
};

constexpr SampledComboBoundary DetectSampledComboBoundary(
    bool wasAlive, bool isAlive, int previousCombo, int currentCombo) {
    if (wasAlive && !isAlive) return SampledComboBoundary::VisibleRecovery;
    if (wasAlive && isAlive && previousCombo > 0 && currentCombo > 0 &&
        currentCombo < previousCombo) {
        return SampledComboBoundary::CounterRollover;
    }
    return SampledComboBoundary::None;
}

constexpr bool ContactBeginsNewComboBaseline(int comboBefore,
                                             int comboAfter) {
    return comboBefore == 0 ||
           (comboBefore > 0 && comboAfter > 0 && comboAfter < comboBefore);
}

constexpr bool ExactContactBeginsNewCombo(bool anyPriorLanded,
                                          bool committedHit,
                                          int comboBefore,
                                          int comboAfter,
                                          unsigned int sequence,
                                          unsigned int priorLandedSequence) {
    return anyPriorLanded && committedHit &&
           ContactBeginsNewComboBaseline(comboBefore, comboAfter) &&
           sequence != 0 && sequence > priorLandedSequence;
}

enum class StepSatisfactionDecision {
    Wait,
    Advance,
    PrematureContactDrop,
};

constexpr StepSatisfactionDecision DecideStepSatisfaction(
    bool satisfied, bool awaitingComboEnd, bool moveOnly) {
    if (!satisfied) return StepSatisfactionDecision::Wait;
    if (awaitingComboEnd && !moveOnly) {
        return StepSatisfactionDecision::PrematureContactDrop;
    }
    return StepSatisfactionDecision::Advance;
}

constexpr int RecordedActionBaselineAfterBoundary(
    int priorBaseline, int activeStep, int boundaryStep, bool activeStepLanded) {
    return activeStep > boundaryStep && !activeStepLanded ? 0 : priorBaseline;
}

constexpr int RunnerArmBaseline(int currentCombo, bool boundaryConsumedThisTick) {
    return boundaryConsumedThisTick ? 0 : currentCombo;
}

struct DelayedActionWindow {
    static constexpr int kCommitGraceTicks = 90;

    int elapsed = 0;
    int commitGrace = 0;

    void Reset() {
        elapsed = 0;
        commitGrace = 0;
    }

    bool Tick(bool frozen, bool actionArmed, bool attackPressedEdge, int limit) {
        if (actionArmed) {
            Reset();
            return false;
        }
        if (limit <= 0) return false;

        if (attackPressedEdge && elapsed >= (std::max)(0, limit - 1)) {
            commitGrace = kCommitGraceTicks;
        }
        if (frozen) return false;

        ++elapsed;
        if (attackPressedEdge && elapsed >= (std::max)(0, limit - 1)) {
            commitGrace = kCommitGraceTicks;
        }
        if (elapsed <= limit) return false;
        if (commitGrace > 0) {
            --commitGrace;
            return false;
        }
        return true;
    }
};

constexpr ComboEndDecision DecideComboEnd(bool endedEdge, bool required) {
    if (!endedEdge) return ComboEndDecision::None;
    return required ? ComboEndDecision::ConsumeRequired
                    : ComboEndDecision::UnexpectedDrop;
}

constexpr int kStepDamageTolerance = 500;
constexpr bool StepDamageDiverged(int recordedDelta, int observedDelta) {
    if (recordedDelta <= 0) return false;
    const int diff = observedDelta - recordedDelta;
    return diff > kStepDamageTolerance || diff < -kStepDamageTolerance;
}

// 192-Hz internal-tick unit; allowance = twice the observed gap plus one fail
// window, ceiling 5760 (30 s).
constexpr int RecordedGapAllowance(int observedTicks) {
    if (observedTicks <= 0) return 0;
    const long long generous = static_cast<long long>(observedTicks) * 2 + 60;
    return static_cast<int>(generous > 5760 ? 5760 : generous);
}

struct SegmentScore {
    int finalizedHits = 0;
    int finalizedDamage = 0;
    int segmentMaxHits = 0;
    int segmentMaxDamage = 0;

    void Reset() {
        finalizedHits = 0;
        finalizedDamage = 0;
        segmentMaxHits = 0;
        segmentMaxDamage = 0;
    }

    void Observe(int comboHits, int comboDamage) {
        if (comboHits <= 0) return;
        segmentMaxHits = (std::max)(segmentMaxHits, comboHits);
        segmentMaxDamage = (std::max)(segmentMaxDamage, comboDamage);
    }

    void FinalizeSegment() {
        finalizedHits += segmentMaxHits;
        finalizedDamage += segmentMaxDamage;
        segmentMaxHits = 0;
        segmentMaxDamage = 0;
    }

    int TotalHits() const { return finalizedHits + segmentMaxHits; }
    int TotalDamage() const { return finalizedDamage + segmentMaxDamage; }
};

}
