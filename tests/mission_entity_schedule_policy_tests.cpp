#include "game/mission/mission_entity_schedule_policy.h"

#include <cstdint>
#include <stdexcept>

namespace {

void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

}

int main() {
    using namespace Mission::EntitySchedulePolicy;

    Check(!Satisfied({3, 3}, 4, 4), "partial projectile run stays pending");
    Check(Satisfied({4, 4}, 4, 4), "exact projectile run satisfies");
    Check(Overshot({5, 4}, 4, 4), "extra contact is rejected");
    Check(Overshot({1, 2}, 1, 1), "one resolver event cannot overshoot hits");
    Check(!Overshot({4, 4}, 4, 4), "exact counts do not overshoot");
    Check(FanoutSatisfied({1, 1}, 1, 1),
          "one valid sibling can satisfy a flexible fanout minimum");
    Check(!FanoutSatisfied({0, 0}, 1, 1),
          "a fanout still requires one recorded hit");
    Check(FanoutAggregateAllowed({15, 15}, 1, 1),
          "fifteen exact sibling hits remain valid when only one hit was recorded");
    Check(FanoutMemberContactMatches(true, true),
          "any exact sibling may be the fanout's first contact");
    Check(!FanoutMemberContactMatches(false, true),
          "wrong result cannot satisfy an exact fanout member");
    Check(!FanoutMemberContactMatches(true, false),
          "wrong pattern, generation, or cast cannot satisfy a fanout member");
    Check(OptionalFanoutContactCanBeConsumed(true, true, true, true),
          "a later exact sibling remains consumable after the minimum");
    Check(!OptionalFanoutContactCanBeConsumed(true, false, true, true),
          "a sibling cannot be consumed after the fanout due window");
    Check(OrderedHitContribution(true, 2, 1) == 1,
          "counter rollover contact still contributes one combo hit");
    Check(OrderedHitContribution(true, 3, 5) == 2,
          "ordinary resolver combo delta is retained");
    Check(OrderedHitContribution(false, 3, 3) == 0,
          "block/RG contacts do not invent combo hits");

    Check(RuntimeMarkerSupportsOrdering(1, false),
          "v1 marker retains legacy whole-step schedules");
    Check(!RuntimeMarkerSupportsOrdering(1, true),
          "v1 marker cannot silently ignore v2 ordering fields");
    Check(RuntimeMarkerSupportsOrdering(2, true),
          "v2 marker advertises ordinal and action ordering");
    Check(RuntimeMarkerSupportsOrdering(3, true),
          "v3 marker retains v2 ordering");
    Check(RuntimeMarkerSupportsLineage(2, false),
          "v2 schedule keeps its existing pattern-only runtime meaning");
    Check(!RuntimeMarkerSupportsLineage(2, true),
          "v2 cannot silently ignore v3 producer fields");
    Check(RuntimeMarkerSupportsLineage(3, true),
          "v3 advertises exact producer lineage");
    Check(ValidProducerDescriptor(LifecycleKind::Spawn, 401, -1),
          "spawn producer descriptor is valid");
    Check(ValidProducerDescriptor(LifecycleKind::Morph, 403, 401),
          "morph producer retains its prior pattern");
    Check(!ValidProducerDescriptor(LifecycleKind::Morph, 403, -1),
          "morph without its prior pattern fails closed");
    Check(RequiresV2Ordering(2, -1, -1),
          "separate contact-time action gate requires v2");
    Check(RequiresV2Ordering(-1, 1, -1),
          "after-contact ordinal requires v2");
    Check(!RequiresV2Ordering(-1, -1, -1),
          "omitted ordering fields remain a v1 schedule");

    Check(GatesOpen(2, 1, 0, 2, 1, 0),
          "concurrent run opens after its action and direct barrier");
    Check(!GatesOpen(2, 0, 0, 2, 1, 0),
          "entity before its direct barrier is rejected");
    Check(!GatesOpen(2, 1, 1, 2, 1, 0),
          "wrong combo segment is rejected");
    Check(GatesOpen(10, 7, 1, 10, 7, 1),
          "movement action can open a delayed projectile run");

    Check(!DirectBarrierSatisfied(false, 2, true, true, 1),
          "first hit of a two-hit direct step keeps its barrier closed");
    Check(DirectBarrierSatisfied(false, 2, true, true, 2),
          "second hit opens a two-hit direct barrier");
    Check(!DirectBarrierSatisfied(false, 1, false, true, 0),
          "blocked Land does not open a direct barrier");
    Check(DirectBarrierSatisfied(true, 0, true, true, 0),
          "typed Connect opens on its recorded contact result");

    Check(EntityPrecedesDirect(10, 11),
          "entity then direct preserves recorded order");
    Check(!EntityPrecedesDirect(11, 10),
          "direct then entity violates due-before order");
    Check(EntityPrecedesDirect(11, 0),
          "terminal entity has no following direct barrier");

    Check(!DamageDiverged(0, 9000),
          "zero recorded entity damage is not a damage contract");
    Check(!DamageDiverged(1200, 1200 + kEntityDamageTolerance),
          "entity damage accepts the tolerance boundary");
    Check(DamageDiverged(1200, 1200 + kEntityDamageTolerance + 1),
          "entity damage rejects a materially different hit");

    Check(CompiledSegment(1, 2, 0) == 3,
          "step and entity owned boundaries both contribute to segment");
    Check(CompiledSegment(0, 1, 3) == 3,
          "coarse action anchors cannot move a later contact backwards");
    Check(SameRecordedProducerLineage(4, 7, 2, 4, 7, 2),
          "exact producer lineage and setter gate match");
    Check(!SameRecordedProducerLineage(4, 7, 2, 4, 7, 3),
          "same entity generation reactivated by a later action is distinct");
    Check(CanJoinContactRun(false, 2, 2, 4, 7, 4, 7,
                            5, 5, 3, 1, 3, 1),
          "same-lineage same-gate contacts may join one run");
    Check(!CanJoinContactRun(true, 2, 2, 4, 7, 4, 7,
                             5, 5, 3, 1, 3, 1),
          "a combo-ending contact seals its run");
    Check(!CanJoinContactRun(false, 2, 3, 4, 7, 4, 7,
                             5, 5, 3, 1, 3, 1),
          "contacts from different segments never merge");
    Check(!CanJoinContactRun(false, 2, 2, 4, 7, 5, 7,
                             5, 5, 3, 1, 3, 1),
          "different entity slots never merge");
    Check(!CanJoinContactRun(false, 2, 2, 4, 7, 4, 8,
                             5, 5, 3, 1, 3, 1),
          "reused slots with a new generation never merge");
    Check(!CanJoinContactRun(false, 2, 2, 4, 7, 4, 7,
                             5, 6, 3, 1, 3, 1),
          "contacts armed by different setter actions never merge");
    Check(!CanJoinContactRun(false, 2, 2, 4, 7, 4, 7,
                             5, 5, 3, 1, 3, 2),
          "contacts separated by a direct ordinal never merge");

    Check(StrictLineageMatches(
              4, 7, LifecycleKind::Morph, 403, 401, 5,
              4, 403, 4, 7, LifecycleKind::Morph, 403, 401, 5),
          "contact binds to its exact recorded producer instance");
    Check(!StrictLineageMatches(
              4, 7, LifecycleKind::Morph, 403, 401, 5,
              6, 403, 6, 7, LifecycleKind::Morph, 403, 401, 5),
          "concurrent identical-pattern entity in another slot cannot satisfy");
    Check(!StrictLineageMatches(
              4, 7, LifecycleKind::Morph, 403, 401, 5,
              4, 403, 4, 8, LifecycleKind::Morph, 403, 401, 5),
          "reused slot generation cannot satisfy an earlier producer run");
    Check(!StrictLineageMatches(
              4, 7, LifecycleKind::Morph, 403, 401, 5,
              4, 403, 4, 7, LifecycleKind::Spawn, 403, -1, 5),
          "same slot/pattern still requires the recorded lifecycle phase");
    Check(FanoutMemberContactMatches(
              true,
              StrictLineageMatches(
                  6, 9, LifecycleKind::Spawn, 435, -1, 12,
                  6, 435, 6, 9, LifecycleKind::Spawn, 435, -1, 12)),
          "a later-listed sibling may connect before the first-listed sibling");
    Check(!FanoutMemberContactMatches(
               true,
               StrictLineageMatches(
                   6, 9, LifecycleKind::Spawn, 435, -1, 12,
                   6, 435, 6, 10, LifecycleKind::Spawn, 435, -1, 12)),
          "fanout rejects a reused slot generation");
    Check(!FanoutMemberContactMatches(
               true,
               StrictLineageMatches(
                   6, 9, LifecycleKind::Spawn, 435, -1, 12,
                   6, 435, 6, 9, LifecycleKind::Spawn, 435, -1, 13)),
          "fanout rejects a sibling allocated by another cast");
    Check(!FanoutMemberContactMatches(
               true,
               StrictLineageMatches(
                   6, 9, LifecycleKind::Spawn, 435, -1, 12,
                   6, 436, 6, 9, LifecycleKind::Spawn, 435, -1, 12)),
          "fanout rejects a controller/non-attacking pattern");

    Check(!AfterDirectBarrierSatisfied(1, 2, 0, 2, -1),
          "legacy barrier waits for the whole direct step");
    Check(AfterDirectBarrierSatisfied(1, 2, 1, 2, 1),
          "ordinal barrier opens after its exact direct contact");
    Check(!AfterDirectBarrierSatisfied(1, 2, 1, 2, 2),
          "later ordinal remains closed");
    Check(OrderedGatesOpen(2, 1, 2, 1, 0, 1, 2, 2, 1, 0),
          "entity can open between contacts of one direct action");

    Check(RecordedActionFollowsContact(11, 8, 10, 7),
          "later battle batch action follows the contact");
    Check(!RecordedActionFollowsContact(10, 8, 10, 7),
          "same-batch action is not compiled into an impossible start barrier");
    Check(!RecordedActionFollowsContact(10, 7, 10, 7),
          "action already observed at contact is not a due barrier");
    Check(!RecordedActionFollowsContact(9, 8, 10, 7),
          "older battle batch cannot be ordered after the contact");
    Check(RecordedActionCanPrecedeContact(10, 10),
          "same Battle batch action can precede collision resolution");
    Check(RecordedActionCanPrecedeContact(9, 10),
          "older Battle batch action precedes the contact");
    Check(!RecordedActionCanPrecedeContact(11, 10),
          "later Battle batch action cannot be an observed contact gate");

    const DueBarrier movementDue = SelectDueBarrier(4, 6, 1);
    Check(movementDue.step == 4 && movementDue.contact == 0,
          "same-tick movement/setup action is the earlier due barrier");
    const DueBarrier interleavedDue = SelectDueBarrier(6, 4, 2);
    Check(interleavedDue.step == 4 && interleavedDue.contact == 2,
          "already-running direct action uses its next contact ordinal");
    Check(DueDirectContactReached(4, 2, 4, 2),
          "the exact due ordinal is enforced");
    Check(!DueDirectContactReached(4, 1, 4, 2),
          "a direct contact before the ordinal remains legal");
    Check(OptionalFanoutWindowOpen(3, 2, 3, 0, 4, 0),
          "optional siblings remain open before the next action starts");
    Check(!OptionalFanoutWindowOpen(4, 3, 4, 0, 4, 0),
          "action-start due barrier closes optional siblings");
    Check(OptionalFanoutWindowOpen(4, 3, 4, 1, 4, 2),
          "optional siblings remain open before a due contact ordinal");
    Check(!OptionalFanoutWindowOpen(4, 3, 4, 2, 4, 2),
          "due contact ordinal closes optional siblings");
    Check(OptionalFanoutWindowOpen(4, 3, 4, 3, 4, -1),
          "legacy whole-step due stays open before step completion");
    Check(!OptionalFanoutWindowOpen(4, 4, 4, 3, 4, -1),
          "legacy whole-step completion closes optional siblings");
    Check(DirectStepForOrderedEvent(4, 5, 81, 80) == 4,
          "events before the preserved hand-off stay on the armed step");
    Check(DirectStepForOrderedEvent(4, 5, 81, 81) == 5 &&
              DirectStepForOrderedEvent(4, 5, 81, 82) == 5,
          "the hand-off event and later events use the authored successor");
    Check(DirectStepForOrderedEvent(4, 5, 0, 82) == 4,
          "a missing exact hand-off sequence never guesses a step switch");
    Check(DueBarrierDoesNotRegress(4, 0, 4, 2),
          "action start then contact ordinal is ordered");
    Check(!DueBarrierDoesNotRegress(4, 2, 4, 1),
          "contact ordinals cannot regress");
    Check(DueBarrierDoesNotRegress(4, 2, 4, -1),
          "legacy completion is last within a step");
    Check(BarrierWindowValid(4, 1, 4, 2),
          "same-step interleaving has a non-empty ordinal window");
    Check(!BarrierWindowValid(4, 2, 4, 2),
          "equal after/due ordinals are impossible");
    Check(BarrierWindowValid(4, 2, 4, -1),
          "whole-step completion may follow an after ordinal");
    Check(AuthoredDirectContactCount(false, 99) == 1,
          "land/connect author one direct contact");
    Check(AuthoredDirectContactCount(true, 3) == 3,
          "hits authors its explicit direct contact count");
    Check(ContactOrdinalWithinAuthoredRequirement(-1, 2),
          "legacy whole-step sentinel remains valid");
    Check(ContactOrdinalWithinAuthoredRequirement(2, 2),
          "last authored direct contact is valid");
    Check(!ContactOrdinalWithinAuthoredRequirement(3, 2),
          "contact beyond authored direct requirement is rejected");
    Check(!ContactOrdinalWithinAuthoredRequirement(0, 2),
          "action start is not accepted as a contact ordinal");

    Check(SegmentForOrderedContact(0, true, true, 5, 6) == 0,
          "late Part-1 contact remains in old segment");
    Check(SegmentForOrderedContact(0, true, true, 0, 1) == 1,
          "first Part-2 contact advances the local segment");
    Check(SegmentForOrderedContact(0, true, true, 0, 0) == 1,
          "first blocked or RG Part-2 contact advances the local segment");
    Check(SegmentForOrderedContact(0, true, false, 0, 0) == 0,
          "uncommitted journal entry does not advance the segment");
    Check(SegmentForOrderedContact(0, true, true, 2, 1) == 1,
          "sampled positive counter rollover advances to Part 2");
    Check(SegmentForOrderedContact(1, false, true, 0, 1) == 1,
          "settled segment does not advance twice");

    Check(ExactOrderedContactConsumesAwaitedBoundary(true, true, 0, 1),
          "exact 0-to-1 contact consumes an awaited unsampled boundary");
    Check(ExactOrderedContactConsumesAwaitedBoundary(true, true, 2, 1),
          "exact positive counter rollover consumes an awaited boundary");
    Check(!ExactOrderedContactConsumesAwaitedBoundary(false, true, 0, 1),
          "exact contact cannot invent an unauthored boundary");
    Check(!ExactOrderedContactConsumesAwaitedBoundary(true, false, 0, 1),
          "uncommitted contact cannot consume an awaited boundary");
    Check(!ExactOrderedContactConsumesAwaitedBoundary(true, true, 1, 2),
          "ordinary combo continuation is not a boundary");

    Check(!RequiresRestoreBoundReset(1, true),
          "v1 schedule preserves logical reset behavior");
    Check(!RequiresRestoreBoundReset(2, true),
          "v2 schedule preserves logical reset behavior");
    Check(RequiresRestoreBoundReset(3, true),
          "v3 entity schedule requires a world restore");
    Check(RequiresRestoreBoundReset(4, true),
          "v4 lifecycle schedule requires a world restore");
    Check(!RequiresRestoreBoundReset(3, false),
          "v3 marker without entity obligations needs no lineage restore");
    Check(RuntimeMarkerSupportsOrdering(4, true) &&
              RuntimeMarkerSupportsLineage(4, true),
          "v4 retains exact contact ordering and lineage capabilities");
    Check(RuntimeMarkerSupportsOrdering(5, true) &&
              RuntimeMarkerSupportsLineage(5, true),
          "v5 retains exact ordering and lineage for flexible fanout");
    return 0;
}
