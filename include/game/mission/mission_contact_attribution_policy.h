#pragma once

#include "contact_event.h"

namespace Mission::ContactAttributionPolicy {

constexpr bool IsCommittedResult(::Mission::Contact::Result result) {
    using Result = ::Mission::Contact::Result;
    return result == Result::Hit || result == Result::Block ||
           result == Result::RecoilGuard || result == Result::Throw ||
           result == Result::SpecialHit || result == Result::GuardPoint;
}

constexpr bool IsHitResult(::Mission::Contact::Result result) {
    using Result = ::Mission::Contact::Result;
    return result == Result::Hit || result == Result::SpecialHit ||
           result == Result::Throw;
}

constexpr bool IsDirectLearnerContact(
    const ::Mission::Contact::Event& event) {
    return event.source == ::Mission::Contact::Source::DirectPlayer &&
           event.attacker == 1 && event.defender == 2 &&
           IsCommittedResult(event.result);
}

constexpr bool IsUnresolvedLearnerEntityContact(
    const ::Mission::Contact::Event& event) {
    return event.source == ::Mission::Contact::Source::Entity &&
           event.attacker == 1 && event.defender == 2 &&
           event.result != ::Mission::Contact::Result::None;
}

constexpr int DirectHitContribution(
    const ::Mission::Contact::Event& event) {
    if (!IsDirectLearnerContact(event) || !IsHitResult(event.result)) return 0;
    const int comboDelta = event.comboAfter - event.comboBefore;
    return comboDelta > 0 ? comboDelta : 1;
}

constexpr int DirectDamageContribution(
    const ::Mission::Contact::Event& event) {
    if (!IsDirectLearnerContact(event) || !IsHitResult(event.result)) return 0;
    const int damage = event.defenderHpBefore - event.defenderHpAfter;
    return damage > 0 ? damage : 0;
}

constexpr bool SameIdContactBelongsToPriorInstance(
    bool physicalSameMoveReentry, short contactMove, short liveMove,
    int contactFrame, int liveFrame) {
    return physicalSameMoveReentry && contactMove == liveMove &&
           contactFrame > liveFrame;
}

constexpr bool SameIdContactBelongsToNextInstance(
    bool splitConsecutiveInstances, short contactMove, short liveMove,
    int contactFrame, int liveFrame) {
    return splitConsecutiveInstances && contactMove == liveMove &&
           !SameIdContactBelongsToPriorInstance(
               true, contactMove, liveMove, contactFrame, liveFrame);
}

}
