#pragma once

#include "game/mission/mission_data.h"
#include "game/mission/mission_entity_schedule_policy.h"

#include <cstdint>
#include <string>

namespace Mission::EntityLifecyclePolicy {

using LifecycleKind = EntitySchedulePolicy::LifecycleKind;

// Slot + generation keep concurrent or reused ring entries with the same
// pattern from satisfying one another's objective.
struct ProducerObservation {
    int slot = -1;
    uint32_t generation = 0;
    LifecycleKind kind = LifecycleKind::None;
    int pattern = -1;
    int priorPattern = -1;
    int actionGate = -1;
};

struct ObjectiveState {
    bool satisfied = false;
};

constexpr bool ExactWhiffEvidenceAvailable(
    bool hasExactSavestate,
    bool entityContactHookObserved,
    bool entityContactHookComplete,
    bool contactEpochContinuous,
    bool contactJournalComplete,
    bool entityTraceComplete,
    bool entityProbeComplete,
    bool entityAllocationUnambiguous) {
    return hasExactSavestate && entityContactHookObserved &&
           entityContactHookComplete && contactEpochContinuous &&
           contactJournalComplete && entityTraceComplete &&
           entityProbeComplete && entityAllocationUnambiguous;
}

inline LifecycleKind KindFromName(const std::string& value) {
    if (value == "baseline") return LifecycleKind::Baseline;
    if (value == "spawn") return LifecycleKind::Spawn;
    if (value == "morph") return LifecycleKind::Morph;
    if (value == "despawn") return LifecycleKind::Despawn;
    return LifecycleKind::None;
}

constexpr bool ValidIdentity(int slot, int generation, LifecycleKind kind,
                             int pattern, int priorPattern,
                             int actionGate) {
    if (slot < 0 || generation <= 0 || kind == LifecycleKind::None ||
        actionGate < -1) {
        return false;
    }
    switch (kind) {
    case LifecycleKind::Baseline:
        return actionGate == -1 && pattern > 0 && priorPattern < 0;
    case LifecycleKind::Spawn:
        return pattern > 0 && priorPattern < 0;
    case LifecycleKind::Morph:
        return pattern > 0 && priorPattern > 0 &&
               priorPattern != pattern;
    case LifecycleKind::Despawn:
        return pattern < 0 && priorPattern > 0;
    case LifecycleKind::None:
        break;
    }
    return false;
}

constexpr bool ExactIdentityMatches(
    int requiredSlot, int requiredGeneration, LifecycleKind requiredKind,
    int requiredPattern, int requiredPriorPattern, int requiredActionGate,
    const ProducerObservation& observed) {
    return ValidIdentity(requiredSlot, requiredGeneration, requiredKind,
                         requiredPattern, requiredPriorPattern,
                         requiredActionGate) &&
           observed.slot == requiredSlot &&
           observed.generation ==
               static_cast<uint32_t>(requiredGeneration) &&
           observed.kind == requiredKind &&
           observed.pattern == requiredPattern &&
           observed.priorPattern == requiredPriorPattern &&
           observed.actionGate == requiredActionGate;
}

inline bool ExactIdentityMatches(
    const EntityLifecycleRequirement& requirement,
    const ProducerObservation& observed) {
    return ExactIdentityMatches(
        requirement.slot, requirement.generation,
        KindFromName(requirement.lifecycle), requirement.pattern,
        requirement.priorPattern, requirement.opensAfterAction, observed);
}

inline int SegmentForExactObjective(
    int sampledSegment,
    const EntityLifecycleRequirement& requirement,
    const ProducerObservation& observed) {
    return ExactIdentityMatches(requirement, observed)
        ? requirement.segment : sampledSegment;
}

inline bool SameObjectiveIdentity(
    const EntityLifecycleRequirement& left,
    const EntityLifecycleRequirement& right) {
    return left.owner == right.owner && left.slot == right.slot &&
           left.generation == right.generation &&
           left.lifecycle == right.lifecycle &&
           left.pattern == right.pattern &&
           left.priorPattern == right.priorPattern &&
           left.opensAfterAction == right.opensAfterAction &&
           left.segment == right.segment;
}

inline bool HasPriorDuplicateObjective(
    const std::vector<EntityLifecycleRequirement>& requirements,
    std::size_t index) {
    if (index >= requirements.size()) return false;
    for (std::size_t prior = 0; prior < index; ++prior) {
        if (SameObjectiveIdentity(requirements[prior], requirements[index])) {
            return true;
        }
    }
    return false;
}

inline int SegmentForPendingExactObjective(
    int sampledSegment,
    const std::vector<EntityLifecycleRequirement>& requirements,
    const std::vector<ObjectiveState>& states,
    const ProducerObservation& observed) {
    int nextSegment = sampledSegment;
    for (std::size_t i = 0; i < requirements.size(); ++i) {
        if (i < states.size() && states[i].satisfied) continue;
        const EntityLifecycleRequirement& requirement = requirements[i];
        if (!ExactIdentityMatches(requirement, observed)) continue;
        if (requirement.segment == sampledSegment) return sampledSegment;
        if (requirement.segment == sampledSegment + 1) {
            nextSegment = requirement.segment;
        }
    }
    return nextSegment;
}

constexpr bool TrySatisfyOnce(
    ObjectiveState& state,
    int requiredSlot, int requiredGeneration, LifecycleKind requiredKind,
    int requiredPattern, int requiredPriorPattern, int requiredActionGate,
    const ProducerObservation& observed) {
    if (state.satisfied ||
        !ExactIdentityMatches(
            requiredSlot, requiredGeneration, requiredKind,
            requiredPattern, requiredPriorPattern, requiredActionGate,
            observed)) {
        return false;
    }
    state.satisfied = true;
    return true;
}

inline bool TrySatisfyOnce(
    ObjectiveState& state,
    const EntityLifecycleRequirement& requirement,
    const ProducerObservation& observed) {
    if (state.satisfied || !ExactIdentityMatches(requirement, observed)) {
        return false;
    }
    state.satisfied = true;
    return true;
}

}
