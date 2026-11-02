#include "game/mission/entity_notation_tables.h"
#include "game/mission/snowbunny_transition_policy.h"

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

}

int main() {
    using Mission::SnowbunnyTransitionPolicy::BridgeKind;
    using Mission::SnowbunnyTransitionPolicy::ClassifyBridge;

    Require(ClassifyBridge(404, 406, 404) ==
                BridgeKind::DirectPostContactRetirement &&
            ClassifyBridge(405, 407, 405) ==
                BridgeKind::DirectPostContactRetirement,
            "direct contact-to-recovery samples must retain both lanes");
    Require(ClassifyBridge(408, 406, 404) ==
                BridgeKind::CollapsedControllerToRecovery &&
            ClassifyBridge(409, 407, 405) ==
                BridgeKind::CollapsedControllerToRecovery,
            "collapsed controller-to-recovery samples must recover the exact hit");
    Require(ClassifyBridge(408, 407, 404) == BridgeKind::None &&
                ClassifyBridge(409, 406, 405) == BridgeKind::None &&
                ClassifyBridge(410, 406, 404) == BridgeKind::None &&
                ClassifyBridge(408, 404, 404) == BridgeKind::None,
            "wrong-lane, unknown, and ordinary transitions must not bridge");

    using Mission::EntityNames::LifecycleDisposition;
    using Mission::EntityNames::LookupSemantic;
    using Mission::EntityNames::PresentationRole;
    const auto* laneOneHit = LookupSemantic("nayukib", 404);
    const auto* laneTwoHit = LookupSemantic("nayukib", 405);
    const auto* laneOneRecovery = LookupSemantic("nayukib", 406);
    const auto* laneTwoRecovery = LookupSemantic("nayukib", 407);
    const auto* laneOneController = LookupSemantic("nayukib", 408);
    const auto* laneTwoController = LookupSemantic("nayukib", 409);
    const auto* unresolved410 = LookupSemantic("nayukib", 410);
    const auto* trail = LookupSemantic("nayukib", 411);

    Require(laneOneHit && laneTwoHit && laneOneRecovery && laneTwoRecovery &&
                laneOneController && laneTwoController && unresolved410 && trail,
            "generated Snowbunny lifecycle rows are incomplete");
    Require(laneOneHit->role == PresentationRole::Setplay &&
                laneTwoHit->role == PresentationRole::Setplay &&
                laneOneHit->disposition == LifecycleDisposition::ContactEffect &&
                laneTwoHit->disposition == LifecycleDisposition::ContactEffect,
            "only Snowbunny #404/#405 may promise contact");
    Require(laneOneRecovery->disposition ==
                LifecycleDisposition::PostContactRecovery &&
                laneTwoRecovery->disposition ==
                LifecycleDisposition::PostContactRecovery,
            "Snowbunny #406/#407 must be post-contact recovery");
    Require(laneOneController->disposition == LifecycleDisposition::Controller &&
                laneTwoController->disposition == LifecycleDisposition::Controller,
            "Snowbunny #408/#409 must remain controllers");
    Require(trail->disposition == LifecycleDisposition::VisualEffect,
            "Snowbunny #411 trail must not become an objective");
    Require(std::strcmp(unresolved410->family, "nayukib.entity_410") == 0 &&
                std::strstr(unresolved410->label, "UNRESOLVED") != nullptr,
            "unproven #410 must not be folded into the Snowbunny family");

    std::cout << "snowbunny_transition_policy_tests passed\n";
    return 0;
}
