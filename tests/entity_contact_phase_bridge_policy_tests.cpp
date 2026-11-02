#include "game/mission/entity_contact_phase_bridge_policy.h"
#include "game/mission/snowbunny_transition_policy.h"

#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::size_t CountRulesFor(const char* resource) {
    using namespace Mission::EntityContactPhaseBridgePolicy;
    std::size_t count = 0;
    for (const Rule& rule : kRules) {
        if (ResourceEquals(rule.resource, resource)) ++count;
    }
    return count;
}

}

int main() {
    namespace Policy = Mission::EntityContactPhaseBridgePolicy;
    using Policy::BridgeKind;

    Require(Policy::kRuleCount == 83,
            "the fully audited literal bridge table changed unexpectedly");
    Require(CountRulesFor("akane") == 5 &&
                CountRulesFor("akiko") == 4 &&
                CountRulesFor("ayu") == 1 &&
                CountRulesFor("ikumi") == 3 &&
                CountRulesFor("ikumi2") == 3 &&
                CountRulesFor("kanna") == 2 &&
                CountRulesFor("kano") == 2 &&
                CountRulesFor("mai") == 4 &&
                CountRulesFor("minagi") == 30 &&
                CountRulesFor("misaki") == 2 &&
                CountRulesFor("misuzu") == 3 &&
                CountRulesFor("mizukab") == 5 &&
                CountRulesFor("nanase") == 5 &&
                CountRulesFor("nanase2") == 5 &&
                CountRulesFor("nayukib") == 4 &&
                CountRulesFor("shiori") == 5,
            "a resource-specific bridge group is incomplete");

    std::size_t directCount = 0;
    std::size_t collapsedCount = 0;
    for (const Policy::Rule& rule : Policy::kRules) {
        const BridgeKind classified = Policy::ClassifyBridge(
            rule.resource, rule.sampledPriorPattern,
            rule.sampledCurrentPattern, rule.exactContactPattern);
        Require(classified == rule.kind,
                std::string("literal bridge row does not classify for ") +
                    rule.resource);

        Require(Policy::ClassifyBridge(
                    "unrelated", rule.sampledPriorPattern,
                    rule.sampledCurrentPattern,
                    rule.exactContactPattern) == BridgeKind::None,
                "a bridge leaked across character resources");
        Require(Policy::ClassifyBridge(
                    rule.resource, 0, rule.sampledCurrentPattern,
                    rule.exactContactPattern) == BridgeKind::None &&
                    Policy::ClassifyBridge(
                        rule.resource, rule.sampledPriorPattern, 0,
                        rule.exactContactPattern) == BridgeKind::None &&
                    Policy::ClassifyBridge(
                        rule.resource, rule.sampledPriorPattern,
                        rule.sampledCurrentPattern, 0) == BridgeKind::None,
                "a partial bridge key was accepted");

        if (rule.kind == BridgeKind::DirectPostContactRetirement) {
            ++directCount;
        } else if (rule.kind == BridgeKind::CollapsedIntermediateContact) {
            ++collapsedCount;
        } else {
            Require(false, "a bridge table row has kind None");
        }
    }
    Require(directCount == 73 && collapsedCount == 10,
            "direct/collapsed bridge coverage changed unexpectedly");

    Require(Policy::CanSuppressSampledEndpointObjective(
                BridgeKind::DirectPostContactRetirement,
                false,
                true) &&
                !Policy::CanSuppressSampledEndpointObjective(
                    BridgeKind::DirectPostContactRetirement,
                    true,
                    true) &&
                !Policy::CanSuppressSampledEndpointObjective(
                    BridgeKind::None,
                    false,
                    true),
            "contact-capable or unbridged endpoints must remain objectives");

    // Aliases are not canonicalized: both retail frame-table resources must be literal rows.
    Require(Policy::ClassifyBridge("nanase", 400, 403, 400) ==
                BridgeKind::DirectPostContactRetirement &&
                Policy::ClassifyBridge("nanase2", 400, 403, 400) ==
                    BridgeKind::DirectPostContactRetirement &&
                Policy::ClassifyBridge("Nanase", 400, 403, 400) ==
                    BridgeKind::None,
            "resource identity was normalized or an alternate table was lost");

    using Snow = Mission::SnowbunnyTransitionPolicy::BridgeKind;
    Require(Mission::SnowbunnyTransitionPolicy::ClassifyBridge(
                404, 406, 404) == Snow::DirectPostContactRetirement &&
                Mission::SnowbunnyTransitionPolicy::ClassifyBridge(
                    408, 406, 404) == Snow::CollapsedControllerToRecovery &&
                Mission::SnowbunnyTransitionPolicy::ClassifyBridge(
                    400, 404, 400) == Snow::None,
            "the Snowbunny compatibility policy changed semantics");

    std::cout << "entity_contact_phase_bridge_policy_tests passed\n";
    return 0;
}
