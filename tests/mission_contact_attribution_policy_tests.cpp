#include "game/mission/mission_contact_attribution_policy.h"

#include <cstdlib>
#include <iostream>

namespace {

void Check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}

Mission::Contact::Event Event(Mission::Contact::Source source,
                              Mission::Contact::Result result,
                              short move, int comboBefore, int comboAfter) {
    Mission::Contact::Event event;
    event.source = source;
    event.result = result;
    event.attacker = 1;
    event.defender = 2;
    event.attackerMove = move;
    event.comboBefore = comboBefore;
    event.comboAfter = comboAfter;
    event.defenderHpBefore = 10000;
    event.defenderHpAfter = 9400;
    return event;
}

}

int main() {
    namespace Policy = Mission::ContactAttributionPolicy;
    using Mission::Contact::Result;
    using Mission::Contact::Source;

    const auto directJa = Event(Source::DirectPlayer, Result::Hit, 207, 0, 1);
    const auto sayuri401 = Event(Source::Entity, Result::Hit, 401, 1, 2);
    Check(Policy::IsDirectLearnerContact(directJa) &&
          Policy::DirectHitContribution(directJa) == 1 &&
          Policy::DirectDamageContribution(directJa) == 600,
          "one direct j.A resolver contact contributes one j.A hit");
    Check(!Policy::IsDirectLearnerContact(sayuri401) &&
          Policy::DirectHitContribution(sayuri401) == 0 &&
          Policy::IsUnresolvedLearnerEntityContact(sayuri401),
          "Sayuri #401 remains an entity obligation and cannot inflate j.A");

    const auto blocked = Event(Source::DirectPlayer, Result::Block, 250, 0, 0);
    Check(Policy::IsDirectLearnerContact(blocked) &&
          Policy::DirectHitContribution(blocked) == 0,
          "a resolver-proven block satisfies connect but never land");

    auto ficLatchOnly = Event(Source::DirectPlayer, Result::None, 250, 0, 0);
    Check(!Policy::IsDirectLearnerContact(ficLatchOnly),
          "an uncommitted/FIC latch cannot become direct connect evidence");

    auto wrongSide = directJa;
    wrongSide.attacker = 2;
    wrongSide.defender = 1;
    Check(!Policy::IsDirectLearnerContact(wrongSide) &&
          !Policy::IsUnresolvedLearnerEntityContact(wrongSide),
          "dummy contacts never enter the learner action lane");

    const auto multi = Event(Source::DirectPlayer, Result::SpecialHit, 260, 4, 6);
    Check(Policy::DirectHitContribution(multi) == 2,
          "one resolver transaction retains its exact positive combo delta");
    Check(Policy::SameIdContactBelongsToPriorInstance(
              true, 200, 200, 6, 1),
          "same-snapshot 5A contact at the old frame stays on the prior 5A");
    Check(!Policy::SameIdContactBelongsToPriorInstance(
               true, 200, 200, 1, 3) &&
          !Policy::SameIdContactBelongsToPriorInstance(
               false, 200, 200, 6, 1) &&
          !Policy::SameIdContactBelongsToPriorInstance(
               true, 201, 200, 6, 1),
          "new-instance, non-reentry, and different-move contacts remain deferred");
    Check(Policy::SameIdContactBelongsToNextInstance(
              true, 200, 200, 1, 1) &&
          !Policy::SameIdContactBelongsToNextInstance(
              true, 200, 200, 6, 1) &&
          !Policy::SameIdContactBelongsToNextInstance(
              false, 200, 200, 1, 1) &&
          !Policy::SameIdContactBelongsToNextInstance(
              true, 201, 200, 1, 1),
          "only a causally split new same-ID instance is deferred to the next step");
    return 0;
}
