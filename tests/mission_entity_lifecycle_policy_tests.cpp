#include "game/mission/mission_entity_lifecycle_policy.h"

#include <iostream>
#include <stdexcept>

namespace {

void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

}

int main() {
    using namespace Mission::EntityLifecyclePolicy;

    Check(ExactWhiffEvidenceAvailable(
              true, true, true, true, true, true, true, true),
          "complete ring and resolver evidence can prove a whiff");
    Check(!ExactWhiffEvidenceAvailable(
              true, true, false, true, true, true, true, true),
          "an intermittently unavailable entity-contact hook cannot prove a whiff");
    Check(!ExactWhiffEvidenceAvailable(
              true, true, true, true, false, true, true, true),
          "an overflowing contact journal cannot prove a whiff");
    Check(!ExactWhiffEvidenceAvailable(
              true, true, true, false, true, true, true, true),
          "a contact-world epoch discontinuity cannot prove a whiff");
    Check(!ExactWhiffEvidenceAvailable(
              true, true, true, true, true, false, true, true),
          "an overflowing entity trace cannot prove a whiff");
    Check(!ExactWhiffEvidenceAvailable(
              true, true, true, true, true, true, false, true),
          "an incomplete ring probe cannot prove a whiff");
    Check(!ExactWhiffEvidenceAvailable(
              true, true, true, true, true, true, true, false),
          "an ambiguous allocation cannot prove a whiff");

    Mission::EntityLifecycleRequirement requirement;
    requirement.slot = 4;
    requirement.generation = 7;
    requirement.lifecycle = "morph";
    requirement.pattern = 406;
    requirement.priorPattern = 400;
    requirement.opensAfterAction = 5;

    const ProducerObservation exact{
        4, 7, LifecycleKind::Morph, 406, 400, 5};
    Check(ExactIdentityMatches(requirement, exact),
          "the exact lifecycle producer satisfies its objective");
    requirement.segment = 2;
    Check(SegmentForExactObjective(1, requirement, exact) == 2,
          "an exact same-snapshot producer retains the authored next segment");
    Check(SegmentForExactObjective(
              1, requirement,
              ProducerObservation{4, 7, LifecycleKind::Morph,
                                  405, 400, 5}) == 1,
          "an unrelated producer cannot borrow an authored future segment");

    std::vector<Mission::EntityLifecycleRequirement> objectives{
        requirement, requirement};
    objectives[1].notation = "different presentation";
    objectives[1].maxDelay = 999;
    Check(HasPriorDuplicateObjective(objectives, 1),
          "presentation/timing changes cannot duplicate one strict producer");
    objectives[1].segment = 3;
    Check(!HasPriorDuplicateObjective(objectives, 1),
          "the same persistent producer in another combo part is distinct");
    objectives[1].opensAfterAction = 6;
    Check(!HasPriorDuplicateObjective(objectives, 1),
          "a later producer action is a distinct lifecycle objective");
    objectives[1] = objectives[0];
    objectives[1].generation = 8;
    Check(!HasPriorDuplicateObjective(objectives, 1),
          "a reused slot generation is a distinct lifecycle objective");
    Check(!HasPriorDuplicateObjective(objectives, objectives.size()),
          "an out-of-range validation index fails closed");

    objectives[0] = requirement;
    objectives[0].segment = 1;
    objectives[1] = requirement;
    objectives[1].segment = 2;
    std::vector<ObjectiveState> objectiveStates(2);
    Check(SegmentForPendingExactObjective(
              1, objectives, objectiveStates, exact) == 1,
          "a pending exact objective in the sampled segment has priority");
    objectiveStates[0].satisfied = true;
    Check(SegmentForPendingExactObjective(
              1, objectives, objectiveStates, exact) == 2,
          "a repeated producer can bind to the immediately next combo part");
    Check(SegmentForPendingExactObjective(
              2, objectives, objectiveStates, exact) == 2,
          "an ordinary exact producer retains its current combo segment");
    objectives[1].segment = 3;
    Check(SegmentForPendingExactObjective(
              1, objectives, objectiveStates, exact) == 1,
          "one transition cannot be banked across multiple combo boundaries");
    Check(SegmentForPendingExactObjective(
              1, objectives, objectiveStates,
              ProducerObservation{4, 7, LifecycleKind::Morph,
                                  405, 400, 5}) == 1,
          "an unrelated transition retains its sampled segment");

    Check(!ExactIdentityMatches(
              requirement,
              ProducerObservation{3, 7, LifecycleKind::Morph, 406, 400, 5}),
          "another entity slot cannot satisfy the objective");
    Check(!ExactIdentityMatches(
              requirement,
              ProducerObservation{4, 8, LifecycleKind::Morph, 406, 400, 5}),
          "a newer reused-slot generation cannot satisfy the objective");
    Check(!ExactIdentityMatches(
              requirement,
              ProducerObservation{4, 6, LifecycleKind::Morph, 406, 400, 5}),
          "an old generation cannot satisfy the current objective");
    Check(!ExactIdentityMatches(
              requirement,
              ProducerObservation{4, 7, LifecycleKind::Spawn, 406, -1, 5}),
          "the lifecycle kind is part of the exact identity");
    Check(!ExactIdentityMatches(
              requirement,
              ProducerObservation{4, 7, LifecycleKind::Morph, 405, 400, 5}),
          "the resulting pattern is part of the exact identity");
    Check(!ExactIdentityMatches(
              requirement,
              ProducerObservation{4, 7, LifecycleKind::Morph, 406, 401, 5}),
          "a morph must have the exact prior pattern");
    Check(!ExactIdentityMatches(
              requirement,
              ProducerObservation{4, 7, LifecycleKind::Morph, 406, 400, 4}),
          "a producer from another authored action gate cannot satisfy");

    ObjectiveState progress;
    Check(!TrySatisfyOnce(
              progress, requirement,
              ProducerObservation{4, 6, LifecycleKind::Morph, 406, 400, 5}),
          "a mismatch does not consume the pending objective");
    Check(!progress.satisfied,
          "a mismatching observation leaves objective progress pending");
    Check(TrySatisfyOnce(progress, requirement, exact),
          "the exact observation newly satisfies the objective");
    Check(progress.satisfied, "the objective records satisfaction");
    Check(!TrySatisfyOnce(progress, requirement, exact),
          "the persistent producer observation cannot satisfy twice");

    Mission::EntityLifecycleRequirement baseline;
    baseline.slot = 1;
    baseline.generation = 1;
    baseline.lifecycle = "baseline";
    baseline.pattern = 400;
    baseline.priorPattern = -1;
    baseline.opensAfterAction = -1;
    Check(ExactIdentityMatches(
              baseline,
              ProducerObservation{
                  1, 1, LifecycleKind::Baseline, 400, -1, -1}),
          "a restored baseline has an exact generation-one identity");

    Mission::EntityLifecycleRequirement despawn;
    despawn.slot = 1;
    despawn.generation = 1;
    despawn.lifecycle = "despawn";
    despawn.pattern = -1;
    despawn.priorPattern = 406;
    despawn.opensAfterAction = 5;
    Check(ExactIdentityMatches(
              despawn,
              ProducerObservation{
                  1, 1, LifecycleKind::Despawn, -1, 406, 5}),
          "despawn retains the exact final live pattern as its prior pattern");

    Mission::EntityLifecycleRequirement invalid = requirement;
    invalid.lifecycle = "";
    Check(!ExactIdentityMatches(invalid, exact),
          "an omitted lifecycle kind fails closed");
    invalid = requirement;
    invalid.generation = 0;
    Check(!ExactIdentityMatches(invalid, exact),
          "generation zero is not an exact restored lineage");
    invalid = requirement;
    invalid.priorPattern = invalid.pattern;
    Check(!ExactIdentityMatches(invalid, exact),
          "a morph cannot claim the same prior and resulting pattern");

    std::cout << "mission_entity_lifecycle_policy_tests passed\n";
    return 0;
}
