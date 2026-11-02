#include "game/mission/entity_command_origin_policy.h"

#include <cstring>
#include <stdexcept>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

}

int main() {
    using namespace Mission::EntityCommandOriginPolicy;

    Require(kOriginCount == 4,
            "the four command-origin Mini-Mai attacks must remain explicit");
    for (std::size_t index = 0; index < kOriginCount; ++index) {
        const Origin& origin = kOrigins[index];
        Require(FindByTransition(origin.resource, origin.rootPattern,
                                 origin.activationPattern) == &origin,
                "a command transition did not resolve exactly");
        Require(FindByContact(origin.resource, origin.contactPattern) == &origin,
                "a command contact did not resolve exactly");
        Require(origin.attackMask == kAttackButtonD && origin.notation &&
                    origin.notation[0],
                "a command origin lost its causal button or notation");
    }
    Require(FindByTransition("mai", 401, 402) == nullptr &&
                FindByTransition("mai", 402, 431) == nullptr &&
                FindByTransition("minagi", 401, 431) == nullptr,
            "ordinary summon state changes must not become command origins");
    Require(FindByContact("mai", 436) == nullptr &&
                FindByContact("mai", 401) == nullptr &&
                FindByContact("Mai", 431) == nullptr,
            "controllers, roots, and resource aliases must fail closed");
    Require(std::strcmp(FindByContact("mai", 454)->notation,
                        "(J.)412S") == 0,
            "Blitz child did not retain its command notation");
    const Origin* bound = ValidateBoundCommand(
        "mai", 12, 1, 401, 436, kAttackButtonD);
    Require(bound && ContactBelongsToBoundCommand(bound, 454) &&
                RuntimeTransitionMatches(bound, 12, 1, 12, 1, 401, 436),
            "an exact command binding did not retain its runtime identity");
    Require(!ValidateBoundCommand("mai", -1, 1, 401, 436,
                                  kAttackButtonD) &&
                !ValidateBoundCommand("mai", kEntityRingSlotCapacity, 1,
                                      401, 436, kAttackButtonD) &&
                !ValidateBoundCommand("mai", 12, 0, 401, 436,
                                      kAttackButtonD) &&
                !ValidateBoundCommand("mai", 12, 1, 0, 436,
                                      kAttackButtonD) &&
                !ValidateBoundCommand("mai", 12, 1, -401, 436,
                                      kAttackButtonD) &&
                !ValidateBoundCommand("mai", 12, 1, 401, 436, 0x40) &&
                !ValidateBoundCommand("mai", 12, 1, 401, 431,
                                      kAttackButtonD | 0x40) &&
                !ContactBelongsToBoundCommand(bound, 436) &&
                !RuntimeTransitionMatches(bound, 12, 1, 11, 1, 401, 436) &&
                !RuntimeTransitionMatches(bound, 12, 1, 12, 2, 401, 436) &&
                !RuntimeTransitionMatches(bound, 12, 1, 12, 1, 402, 436),
            "malformed or mismatched command identity did not fail closed");

    const Origin* rush = ValidateBoundCommand(
        "mai", 7, 3, 401, 431, kAttackButtonD);
    Require(LifecycleObjectiveBindsCommand(
                rush, 7, 3, 7, 3, true, 431, 401) &&
                !LifecycleObjectiveBindsCommand(
                    rush, 7, 3, 8, 3, true, 431, 401) &&
                !LifecycleObjectiveBindsCommand(
                    rush, 7, 3, 7, 3, true, 451, 401),
            "command lifecycle objective did not require exact identity");
    Require(ContactObjectiveBindsCommand(
                rush, 7, 3, 7, 3, true, 431,
                true, false, 431, 401) &&
                !ContactObjectiveBindsCommand(
                    rush, 7, 3, 8, 3, true, 431,
                    true, false, 431, 401) &&
                !ContactObjectiveBindsCommand(
                    rush, 7, 3, 7, 3, true, 431,
                    false, true, 431, -1),
            "same-pattern command contact accepted an unrelated producer");
    Require(ContactObjectiveBindsCommand(
                bound, 12, 1, 18, 2, true, 454,
                false, true, 454, -1) &&
                !ContactObjectiveBindsCommand(
                    bound, 12, 1, 18, 2, true, 454,
                    true, false, 454, 401) &&
                !ContactObjectiveBindsCommand(
                    bound, 12, 1, 18, 2, false, 454,
                    false, true, 454, -1),
            "Blitz child binding escaped its literal curated relationship");
    return 0;
}
