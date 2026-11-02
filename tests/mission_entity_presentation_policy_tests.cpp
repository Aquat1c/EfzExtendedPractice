#include "game/mission/mission_entity_presentation_policy.h"
#include "game/mission/entity_notation_tables.h"

#include <cstdio>
#include <stdexcept>

namespace {

void Check(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "mission entity presentation policy: %s\n",
                     message);
        throw std::runtime_error(message);
    }
}

}

int main() try {
    using namespace Mission::EntityPresentationPolicy;

    Timing immediate;
    immediate.opensAfterAction = 3;
    immediate.contactAfterAction = 3;
    immediate.afterStep = 3;
    immediate.dueBeforeStep = 4;
    Check(!IsStructurallyStandalone(immediate),
          "an immediate ordinary fireball belongs to its cast cell");
    Check(!ShouldDisplay(true, false, false, immediate),
          "a mapped inline projectile does not get a redundant row");
    Check(ShouldDisplay(true, true, false, immediate),
          "curated setplay always has its own row");
    Check(ShouldDisplay(false, false, false, immediate),
          "an unknown committed entity remains visibly raw");
    Check(!ShouldDisplay(true, false, true, immediate),
          "failure must tint an inline owner without reflowing the recipe");

    ExactProducerHit exactHit;
    exactHit.action = 3;
    exactHit.exactSource = true;
    exactHit.generated = true;
    exactHit.resolvedContact = true;
    exactHit.actionEligible = true;
    exactHit.ordinaryProjectile = false;
    exactHit.producerMoveMatches = true;
    exactHit.sourceSegmentMatches = true;
    exactHit.sourceComboEndBeforeContact = false;
    Timing exactTiming = immediate;
    exactTiming.afterStep = 2;
    Check(CanInlineExactProducerHit(exactHit, exactTiming),
          "Shiori's immediate producer-qualified fan should share its cast token");

    const auto* minagi236C = ::Mission::EntityNames::LookupSemantic(
        "minagi", 406, 255);
    Check(minagi236C &&
              minagi236C->role ==
                  ::Mission::EntityNames::PresentationRole::Summon &&
              ::Mission::EntityNames::HasResolvedContactPresentation(
                  minagi236C->disposition) &&
              minagi236C->producerMove == 255,
          "Minagi #406 lost its exact 236C presentation mapping");
    Check(::Mission::EntityNames::IsGeneratedContactNotation(
              "minagi", 406, "HIT", 1, "236C (HIT)", 255),
          "Minagi's recorded 236C hit was mistaken for custom author text");
    ExactProducerHit minagiImmediate = exactHit;
    minagiImmediate.action = 2;
    minagiImmediate.ordinaryProjectile = false;
    minagiImmediate.producerMoveMatches = true;
    Timing minagiTiming;
    minagiTiming.opensAfterAction = 2;
    minagiTiming.contactAfterAction = 2;
    minagiTiming.afterStep = 1;
    minagiTiming.afterStepContact = -1;
    minagiTiming.dueBeforeStep = 3;
    minagiTiming.dueBeforeStepContact = 0;
    Check(CanInlineExactProducerHit(minagiImmediate, minagiTiming) &&
              !ShouldDisplay(true, true, false, minagiTiming, true),
          "Minagi's immediate 236C entity hit grew a duplicate recipe cell");

    ExactProducerHit exactCommand = exactHit;
    exactCommand.commandSource = true;
    exactCommand.ordinaryProjectile = false;
    exactCommand.producerMoveMatches = false;
    Check(CanInlineExactProducerHit(exactCommand, exactTiming),
          "an exact immediate input-only command contact should share its command token");

    ExactProducerHit rejected = exactHit;
    rejected.producerMoveMatches = false;
    Check(!CanInlineExactProducerHit(rejected, exactTiming),
          "matching presentation text cannot prove a persistent entity's owner");
    Check(ShouldDisplay(true, false, false, exactTiming, false),
          "mapped immediate timing cannot hide a producer mismatch");
    rejected.ordinaryProjectile = true;
    Check(CanInlineExactProducerHit(rejected, exactTiming),
          "an ordinary immediate projectile uses exact sampled action timing");
    rejected = exactHit;
    rejected.generated = false;
    Check(!CanInlineExactProducerHit(rejected, exactTiming),
          "custom author wording must never be folded away");
    rejected = exactHit;
    rejected.actionEligible = false;
    Check(!CanInlineExactProducerHit(rejected, exactTiming),
          "a missing, optional, or move-less owner cannot absorb a required hit");
    rejected = exactHit;
    rejected.exactSource = false;
    Check(!CanInlineExactProducerHit(rejected, exactTiming) &&
              ShouldDisplay(true, false, false, exactTiming, false),
          "legacy timing or explicitly unresolved provenance keeps a strict contact visible");
    rejected = exactHit;
    rejected.sourceSegmentMatches = false;
    Check(!CanInlineExactProducerHit(rejected, exactTiming),
          "an exact source from another combo segment cannot absorb the contact");
    rejected = exactHit;
    rejected.sourceComboEndBeforeContact = true;
    Check(!CanInlineExactProducerHit(rejected, exactTiming),
          "a combo-end boundary on the source step keeps the contact visible");

    Check(PersistedSemanticMatchesSource(true, false, 312, 313) &&
              PersistedSemanticMatchesSource(true, true, 312, 312) &&
              !PersistedSemanticMatchesSource(true, true, 312, 313) &&
              !PersistedSemanticMatchesSource(false, true, 312, 312),
          "persisted generated notation ignored its exact producer identity");

    Check(SelectOwnerAction(true, false, 3, 7, 10) == 3 &&
              SelectOwnerAction(false, true, -2, 7, 10) == 7 &&
              SelectOwnerAction(false, false, -1, 7, 10) == -1 &&
              SelectOwnerAction(true, false, 12, 7, 10) == -1,
          "exact, legacy, unresolved, and out-of-range source states were conflated");

    ExactProducerHit directAndEntity = exactHit;
    directAndEntity.ordinaryProjectile = true;
    Check(CanInlineExactProducerHit(directAndEntity, exactTiming),
          "one move's immediate direct and entity hits remain one visible action");
    Check(!ShouldDisplay(true, false, false, exactTiming, true),
          "an exact immediate producer relationship hides only the duplicate row");
    Timing insideProducer = exactTiming;
    insideProducer.afterStep = 3;
    insideProducer.afterStepContact = 1;
    insideProducer.dueBeforeStep = 3;
    insideProducer.dueBeforeStepContact = 2;
    Check(CanInlineExactProducerHit(directAndEntity, insideProducer),
          "Shiori's same-action contact ordinal remains one multihit action");

    Timing delayed = exactTiming;
    delayed.contactAfterAction = 5;
    Check(IsStructurallyStandalone(delayed) &&
              ShouldDisplay(true, false, false, delayed) &&
              !CanInlineExactProducerHit(exactHit, delayed),
          "Nagamori's delayed note explosion remains a standalone hit token");
    Timing repeatedSetter = exactTiming;
    repeatedSetter.opensAfterAction = 3;
    repeatedSetter.contactAfterAction = 4;
    repeatedSetter.afterStep = 4;
    repeatedSetter.dueBeforeStep = 5;
    Check(!CanInlineExactProducerHit(exactHit, repeatedSetter),
          "a prior entity cannot be swallowed by a repeated identical setter");
    Timing baseline = immediate;
    baseline.opensAfterAction = -1;
    Check(IsStructurallyStandalone(baseline),
          "a baseline summon/trap contact cannot attach to a cast");
    Timing interleaved = exactTiming;
    interleaved.afterStepContact = 1;
    interleaved.afterStep = 2;
    Check(IsStructurallyStandalone(interleaved) &&
              !CanInlineExactProducerHit(exactHit, interleaved),
          "an entity interleaved with an earlier move stays visible");
    Timing longLived = exactTiming;
    longLived.dueBeforeStep = 6;
    Check(IsStructurallyStandalone(longLived) &&
              !CanInlineExactProducerHit(exactHit, longLived),
          "an effect surviving across multiple actions stays visible");
    Timing boundary = exactTiming;
    boundary.comboEndAfter = true;
    Check(CanInlineExactProducerHit(exactHit, boundary) &&
              IsStructurallyStandalone(boundary) &&
              ShouldDisplay(true, false, false, boundary) &&
              !ShouldDisplay(true, false, false, boundary, true),
          "an exact immediate combo-ending hit shares its producer while preserving boundary metadata");
    Timing delayedBoundary = boundary;
    delayedBoundary.contactAfterAction = 5;
    Check(!CanInlineExactProducerHit(exactHit, delayedBoundary) &&
              ShouldDisplay(true, false, false, delayedBoundary, false),
          "a delayed combo-ending projectile remains independently visible");
    Timing terminal = exactTiming;
    terminal.dueBeforeStep = -1;
    Check(CanInlineExactProducerHit(exactHit, terminal),
          "a terminal hit with no intervening action still shares its producer");

    Identity noteA;
    noteA.family = "nagamori.note";
    noteA.owner = 1;
    noteA.target = 2;
    noteA.segment = 0;
    noteA.slot = 4;
    noteA.generation = 7;
    noteA.producerAction = 5;
    noteA.dueStep = 8;
    noteA.dueContact = 0;

    Identity noteChild = noteA;
    noteChild.slot = 9;
    noteChild.generation = 2;
    Check(CanFold(noteA, noteChild, "HIT", "HIT"),
          "separate children from one semantic producer episode fold in UI");

    Identity sameInstanceLater = noteA;
    sameInstanceLater.producerAction = 6;
    Check(CanFold(noteA, sameInstanceLater, "HIT", "HIT"),
          "same-slot morph phases can fold despite a later sampled producer gate");

    Identity anotherCast = noteChild;
    anotherCast.producerAction = 6;
    Check(!CanFold(noteA, anotherCast, "HIT", "HIT"),
          "another cast with another instance remains a separate placement");

    Identity unresolvedA = noteA;
    unresolvedA.producerAction = -1;
    Identity unresolvedChild = noteChild;
    unresolvedChild.producerAction = -1;
    Check(!CanFold(unresolvedA, unresolvedChild, "HIT", "HIT"),
          "unresolved children cannot infer a shared cast from timing");
    unresolvedChild.slot = unresolvedA.slot;
    unresolvedChild.generation = unresolvedA.generation;
    Check(CanFold(unresolvedA, unresolvedChild, "HIT", "HIT"),
          "unresolved phases of one exact instance may still fold");

    Identity treble = noteChild;
    treble.family = "nagamori.treble";
    Check(!CanFold(noteA, treble, "HIT", "HIT"),
          "ordinary note and Treble remain distinct logical projectiles");
    Identity laterDeadline = noteChild;
    laterDeadline.dueStep = 9;
    Check(!CanFold(noteA, laterDeadline, "HIT", "HIT"),
          "contacts separated by another grading barrier remain distinct tokens");
    Identity nextSegment = noteChild;
    nextSegment.segment = 1;
    Check(!CanFold(noteA, nextSegment, "HIT", "HIT"),
          "different combo parts never collapse");
    Check(!CanFold(noteA, noteChild, "HIT", "BLOCK"),
          "different contact results never collapse");
    Identity separatedByDirectContact = noteChild;
    separatedByDirectContact.afterStep = 6;
    separatedByDirectContact.afterStepContact = 1;
    Check(!CanFold(noteA, separatedByDirectContact, "HIT", "HIT"),
          "different activation barriers never collapse into one token");
    Identity separatedByAction = noteChild;
    separatedByAction.contactAfterAction = 7;
    Check(!CanFold(noteA, separatedByAction, "HIT", "HIT"),
          "different contact actions never collapse into one token");

    Check(AllSatisfied(3, 3) && !AllSatisfied(2, 3),
          "a logical row completes only when all raw requirements complete");
    Check(CompactCountSuffix(4, 4) == " x4",
          "an untouched multi-hit contact should use compact recipe notation");
    Check(CompactCountSuffix(1, 1).empty(),
          "a single contact should not carry a redundant x1 suffix");
    RequirementCounts shioriFan;
    shioriFan.recordedContacts = 15;
    shioriFan.recordedComboHits = 15;
    shioriFan.minimumContacts = 1;
    shioriFan.minimumComboHits = 1;
    shioriFan.fanout = true;
    Check(CompactCountSuffix(shioriFan).empty(),
          "a flexible fanout should show its minimum, not the recorded maximum");
    Check(RequirementSatisfied(shioriFan, 1, 1) &&
              !RequirementSatisfied(shioriFan, 0, 0),
          "a fanout token should complete at its authored minimum");
    shioriFan.fanout = false;
    Check(CompactCountSuffix(shioriFan) == " x15" &&
              !RequirementSatisfied(shioriFan, 1, 1),
          "ordinary exact multi-hit presentation lost its strict count");
    Check(DisplayAnchor(12, 12, 9, 27) == 12 &&
              DisplayAnchor(-1, 12, 9, 27) == 12 &&
              DisplayAnchor(26, -1, 24, 27) == 27,
          "contact tokens are not anchored to their visible chronology");
    Check(FriendlyOutcomeLabel("214B (HIT)", "HIT") ==
              "214B (hit)" &&
              FriendlyOutcomeLabel("NOTE (SPECIAL HIT)", "SPECIAL HIT") ==
                  "NOTE (special hit)",
          "technical result vocabulary leaked into the player-facing token");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
