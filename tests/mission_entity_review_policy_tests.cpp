#include "game/mission/mission_entity_review_policy.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

Mission::EntityReviewPolicy::LegacyLifecycleMigrationEvidence
ExactMinagiWhiffEvidence() {
    using Kind = Mission::EntitySchedulePolicy::LifecycleKind;
    Mission::EntityReviewPolicy::LegacyLifecycleMigrationEvidence evidence;
    evidence.metadataSeen = true;
    evidence.schemaVersion =
        Mission::EntityReviewPolicy::kLifecycleMigrationSidecarSchemaVersion;
    evidence.slotCapacity = 64;
    evidence.exactSavestate = true;
    evidence.entityContactHookComplete = true;
    evidence.contactEpochDiscontinuity = false;
    evidence.contactJournalOverflow = false;
    evidence.droppedEvents = 0;
    evidence.p1EntityProbeIncomplete = false;
    evidence.p1AllocationCursorAmbiguous = false;
    evidence.requiresEntityAttributionReview = true;
    evidence.containsUnclassifiedContact = false;
    evidence.usedLegacyContactAttribution = false;
    evidence.metadataP1Resource = "minagi";
    evidence.sampled = true;
    evidence.eventResource = "minagi";
    evidence.owner = 1;
    evidence.slot = 0;
    evidence.generation = 1;
    evidence.kind = Kind::Morph;
    evidence.pattern = 404;
    evidence.priorPattern = 400;
    evidence.opensAfterAction = 11;
    evidence.authoredActionCount = 12;
    evidence.actionStartEffectiveFrame = 193;
    evidence.lifecycleEffectiveFrame = 195;
    evidence.attack = true;
    evidence.semanticPromisesContact = true;
    evidence.hasLinkedCommittedContact = false;
    return evidence;
}

bool Accepts(
    const Mission::EntityReviewPolicy::LegacyLifecycleMigrationEvidence& e,
    const std::string& resource = "minagi", int pattern = 404,
    int slot = 0, int generation = 1) {
    return Mission::EntityReviewPolicy::CanPromoteLegacyUngradedLifecycle(
        resource, pattern, slot, generation, e);
}

}

int main() {
    using Evidence =
        Mission::EntityReviewPolicy::LegacyLifecycleMigrationEvidence;
    using Kind = Mission::EntitySchedulePolicy::LifecycleKind;

    int pattern = -1;
    int slot = -1;
    int generation = 0;
    Check(Mission::EntityReviewPolicy::ParseLegacyUngradedLifecycleReason(
              "attack entity setup #404 (slot 0, generation 1) had no "
              "gradeable contact; author a lifecycle objective or retake it",
              pattern, slot, generation),
          "the current #404 review reason remains parseable");
    Check(pattern == 404 && slot == 0 && generation == 1,
          "the current #404 review identity parsed incorrectly");

    const Evidence exact = ExactMinagiWhiffEvidence();
    Check(Accepts(exact),
          "reliable exact Minagi 400 -> 404 whiff evidence was rejected");

    Evidence changed = exact;
    changed.metadataSeen = false;
    Check(!Accepts(changed), "missing metadata was accepted");
    changed = exact;
    --changed.schemaVersion;
    Check(!Accepts(changed), "an older sidecar schema was accepted");
    changed = exact;
    changed.exactSavestate = false;
    Check(!Accepts(changed), "value-only state was accepted");
    changed = exact;
    changed.entityContactHookComplete = false;
    Check(!Accepts(changed), "an incomplete contact journal was accepted");
    changed = exact;
    changed.contactEpochDiscontinuity = true;
    Check(!Accepts(changed), "an epoch discontinuity was accepted");
    changed = exact;
    changed.contactJournalOverflow = true;
    Check(!Accepts(changed), "contact journal overflow was accepted");
    changed = exact;
    changed.droppedEvents = 1;
    Check(!Accepts(changed), "a dropped lifecycle sample was accepted");
    changed = exact;
    changed.p1EntityProbeIncomplete = true;
    Check(!Accepts(changed), "an incomplete P1 ring probe was accepted");
    changed = exact;
    changed.p1AllocationCursorAmbiguous = true;
    Check(!Accepts(changed), "ambiguous P1 allocation lineage was accepted");
    changed = exact;
    changed.requiresEntityAttributionReview = false;
    Check(Accepts(changed),
          "the recorder attribution-routing flag changed migration safety");
    changed = exact;
    changed.containsUnclassifiedContact = true;
    Check(!Accepts(changed), "an unclassified contact trace was accepted");
    changed = exact;
    changed.usedLegacyContactAttribution = true;
    Check(!Accepts(changed), "legacy contact attribution was accepted");

    changed = exact;
    changed.metadataP1Resource = "mai";
    Check(!Accepts(changed), "a mismatching metadata resource was accepted");
    changed = exact;
    changed.eventResource = "mai";
    Check(!Accepts(changed), "a mismatching event resource was accepted");
    Check(!Accepts(exact, "mai"),
          "sidecar evidence crossed the mission character resource");
    changed = exact;
    changed.owner = 2;
    Check(!Accepts(changed), "a P2 lifecycle was promoted for P1");
    changed = exact;
    changed.sampled = false;
    Check(!Accepts(changed), "a non-sampled lifecycle was accepted");
    changed = exact;
    changed.attack = false;
    Check(!Accepts(changed), "a non-attack lifecycle was accepted");
    changed = exact;
    changed.semanticPromisesContact = false;
    Check(!Accepts(changed), "an inert lifecycle phase was accepted");
    changed = exact;
    changed.hasLinkedCommittedContact = true;
    Check(!Accepts(changed),
          "a lifecycle already represented by a contact was duplicated");

    Check(!Accepts(exact, "minagi", 405),
          "the review pattern did not bind the exact lifecycle");
    Check(!Accepts(exact, "minagi", 404, 1),
          "the review slot did not bind the exact lifecycle");
    Check(!Accepts(exact, "minagi", 404, 0, 2),
          "the review generation did not bind the exact lifecycle");
    changed = exact;
    changed.slotCapacity = 0;
    Check(!Accepts(changed), "missing ring capacity was accepted");
    changed = exact;
    changed.slot = changed.slotCapacity;
    Check(!Accepts(changed, "minagi", 404, changed.slot, 1),
          "an out-of-ring slot was accepted");

    changed = exact;
    changed.kind = Kind::Despawn;
    changed.pattern = -1;
    changed.priorPattern = 404;
    Check(!Accepts(changed, "minagi", -1),
          "a despawn was promoted as an attack setup");
    changed = exact;
    changed.priorPattern = -1;
    Check(!Accepts(changed), "a morph without a prior phase was accepted");
    changed = exact;
    changed.priorPattern = changed.pattern;
    Check(!Accepts(changed), "a no-op morph was accepted");
    changed = exact;
    changed.kind = Kind::Spawn;
    changed.priorPattern = -1;
    Check(Accepts(changed), "a valid contact-capable spawn was rejected");
    changed.priorPattern = 400;
    Check(!Accepts(changed), "a spawn with a prior live phase was accepted");

    changed = exact;
    changed.opensAfterAction = -1;
    Check(!Accepts(changed), "an unbound action gate was accepted");
    changed = exact;
    changed.opensAfterAction = changed.authoredActionCount;
    Check(!Accepts(changed), "an out-of-range action gate was accepted");
    changed = exact;
    changed.actionStartEffectiveFrame = -1;
    Check(!Accepts(changed), "a missing action-order anchor was accepted");
    changed = exact;
    changed.lifecycleEffectiveFrame =
        changed.actionStartEffectiveFrame - 1;
    Check(!Accepts(changed),
          "a lifecycle sampled before its claimed action was accepted");

    std::cout << "mission_entity_review_policy_tests passed\n";
    return 0;
}
