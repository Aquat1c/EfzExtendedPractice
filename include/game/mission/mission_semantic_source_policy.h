#pragma once

namespace Mission::SemanticSourcePolicy {

// Persisted tri-state: -2 = older mission, -1 = explicitly unresolved, otherwise an exact pair.
// Never persist -1 as -2: the UI would fold a later identical setter over it.
constexpr int kLegacyAbsent = -2;
constexpr int kExplicitUnresolved = -1;

constexpr int kAmbiguousSelection = -3;

struct CandidateSelection {
    int action = kExplicitUnresolved;
    int move = kExplicitUnresolved;
    int distinctCandidates = 0;
    bool ambiguous = false;
};

constexpr bool IsLegacyAbsent(int action, int move) {
    return action == kLegacyAbsent && move == kLegacyAbsent;
}

constexpr bool IsExplicitUnresolved(int action, int move) {
    return action == kExplicitUnresolved && move == kExplicitUnresolved;
}

constexpr bool IsExact(int action, int move) {
    return action >= 0 && move > 0;
}

constexpr bool ValidPersistedPair(int action, int move) {
    return IsLegacyAbsent(action, move) ||
           IsExplicitUnresolved(action, move) || IsExact(action, move);
}

constexpr int AccumulateCandidateAction(int selectedAction,
                                        int candidateAction) {
    if (candidateAction < 0 || selectedAction == kAmbiguousSelection) {
        return selectedAction;
    }
    if (selectedAction == kExplicitUnresolved) return candidateAction;
    return selectedAction == candidateAction
        ? selectedAction : kAmbiguousSelection;
}

constexpr int FinalizeCandidateAction(int selectedAction) {
    return selectedAction >= 0 ? selectedAction : kExplicitUnresolved;
}

constexpr int ResolveOrdinaryBirthAction(bool observedSpawn,
                                         bool baselineOrigin,
                                         int birthAction,
                                         int contactAfterAction,
                                         bool actionOrderMatches) {
    return observedSpawn && !baselineOrigin && birthAction >= 0 &&
                   birthAction <= contactAfterAction && actionOrderMatches
        ? birthAction : kExplicitUnresolved;
}

constexpr bool CandidateLifecycleEligible(bool baselineGeneration,
                                          bool observedGenerationBirth,
                                          bool candidateIsSpawn,
                                          bool candidateIsMorph,
                                          bool entersExactContactPattern,
                                          bool exactActionOrder) {
    if (!exactActionOrder) return false;
    if (baselineGeneration) {
        return candidateIsMorph && entersExactContactPattern;
    }
    if (!observedGenerationBirth) return false;
    return candidateIsSpawn ||
           (candidateIsMorph && entersExactContactPattern);
}

constexpr CandidateSelection AccumulateCandidate(
    CandidateSelection selected, int candidateAction, int candidateMove) {
    if (candidateAction < 0 || candidateMove <= 0) return selected;
    if (selected.distinctCandidates == 0) {
        selected.action = candidateAction;
        selected.move = candidateMove;
        selected.distinctCandidates = 1;
        return selected;
    }
    if (selected.action == candidateAction &&
        selected.move == candidateMove) {
        return selected;
    }
    selected.ambiguous = true;
    selected.distinctCandidates = 2;
    return selected;
}

constexpr CandidateSelection FinalizeCandidate(
    CandidateSelection selected) {
    if (selected.distinctCandidates != 1 || selected.ambiguous) {
        selected.action = kExplicitUnresolved;
        selected.move = kExplicitUnresolved;
    }
    return selected;
}

constexpr CandidateSelection AccumulateStepMoveCandidate(
    CandidateSelection selected, int action, int primaryMove,
    bool primaryMatches, int aliasMove, bool aliasMatches) {
    if (primaryMatches) {
        return AccumulateCandidate(selected, action, primaryMove);
    }
    return aliasMatches
        ? AccumulateCandidate(selected, action, aliasMove)
        : selected;
}

static_assert(ValidPersistedPair(kLegacyAbsent, kLegacyAbsent) &&
                  ValidPersistedPair(kExplicitUnresolved,
                                     kExplicitUnresolved) &&
                  ValidPersistedPair(4, 253) &&
                  !ValidPersistedPair(kLegacyAbsent,
                                      kExplicitUnresolved) &&
                  !ValidPersistedPair(4, kExplicitUnresolved),
              "semantic source provenance must retain all three states");
static_assert(AccumulateCandidateAction(kExplicitUnresolved, 3) == 3 &&
                  AccumulateCandidateAction(3, 3) == 3 &&
                  AccumulateCandidateAction(3, 7) ==
                      kAmbiguousSelection &&
                  FinalizeCandidateAction(kAmbiguousSelection) ==
                      kExplicitUnresolved,
              "two source actions must fail closed instead of guessing");
static_assert(ResolveOrdinaryBirthAction(true, false, 2, 8, true) == 2 &&
                  ResolveOrdinaryBirthAction(false, true, -1, 8, true) ==
                      kExplicitUnresolved &&
                  ResolveOrdinaryBirthAction(true, false, 9, 8, true) ==
                      kExplicitUnresolved,
              "ordinary bullets retain their observed birth action");
static_assert(CandidateLifecycleEligible(true, false, false, true, true,
                                         true) &&
                  !CandidateLifecycleEligible(true, false, true, false, true,
                                              true) &&
                  CandidateLifecycleEligible(false, true, true, false, false,
                                             true) &&
                  !CandidateLifecycleEligible(false, true, false, true, false,
                                              true),
              "baseline morphs and newly spawned contact states need distinct provenance rules");
static_assert(FinalizeCandidate(AccumulateCandidate({}, 3, 253)).action == 3 &&
                  FinalizeCandidate(AccumulateCandidate({}, 3, 253)).move ==
                      253 &&
                  FinalizeCandidate(AccumulateCandidate(
                      AccumulateCandidate({}, 3, 253), 3, 253)).action == 3 &&
                  FinalizeCandidate(AccumulateCandidate(
                      AccumulateCandidate({}, 3, 253), 3, 254)).action ==
                      kExplicitUnresolved,
              "semantic source selection must compare the complete action/move pair");
static_assert(FinalizeCandidate(AccumulateStepMoveCandidate(
                      {}, 4, 314, true, 315, true)).move == 314 &&
                  FinalizeCandidate(AccumulateStepMoveCandidate(
                      {}, 4, 250, false, 273, true)).move == 273,
              "primary inputs take precedence over automatic phase aliases");

}
