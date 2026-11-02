#include "game/kaori_recoil_duck_policy.h"

#include <cstdlib>
#include <iostream>

namespace {

void Require(bool value, const char* message) {
    if (!value) {
        std::cerr << "kaori_recoil_duck_policy_tests: " << message << '\n';
        std::exit(1);
    }
}

}

int main() {
    using namespace KaoriRecoilDuckPolicy;

    Require(IsBackdash(GROUND_BACKWARD_DASH_ID),
            "verified backdash 164 was rejected");
    Require(!IsBackdash(AIR_BACKWARD_DASH_ID),
            "air backdash was accepted as the ground recipe");
    Require(!IsForwardInputWindow(GROUND_BACKWARD_DASH_ID, 3),
            "forward input opened before frame 4");
    Require(IsForwardInputWindow(GROUND_BACKWARD_DASH_ID, 4),
            "frame 4 window missing");
    Require(IsForwardInputWindow(GROUND_BACKWARD_DASH_ID, 5),
            "frame 5 window missing");
    Require(!IsForwardInputWindow(GROUND_BACKWARD_DASH_ID, 6),
            "forward input stayed open after frame 5");
    Require(!IsForwardInputWindow(KAORI_FORWARD_DASH_START_ID, 4),
            "ordinary Ducking was accepted as Recoil Ducking setup");
    Require(IsExactDestination(KAORI_RECOIL_DUCK_ID),
            "verified destination 251 missing");
    Require(!IsExactDestination(KAORI_FORWARD_DASH_START_ID),
            "ordinary Ducking 250 was credited as Recoil Ducking");

    std::cout << "kaori_recoil_duck_policy_tests: ok\n";
    return 0;
}
