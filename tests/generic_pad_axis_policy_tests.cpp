#include "input/generic_pad_axis_policy.h"

#include <cstdlib>
#include <iostream>

namespace {

void Require(bool value, const char* message) {
    if (value) return;
    std::cerr << "generic_pad_axis_policy_tests: " << message << '\n';
    std::exit(1);
}

} // namespace

int main() {
    using namespace GenericPadAxisPolicy;

    Require(UsesZRzRightStick(0x054C, 0x09CC) && UsesZRzRightStick(0x054C, 0x0CE6),
            "DS4/DualSense not recognized as Z/Rz right stick pads");
    Require(!UsesZRzRightStick(0x054C, 0x0268) && !UsesZRzRightStick(0x045E, 0x09CC),
            "unknown pad took the Sony axis layout");

    AxisPairCalibration parked;
    Require(!ObserveNeutral(parked, -32768, -32768),
            "parked endpoint was accepted as an idle stick centre");
    Require(!parked.valid && !PairActive(parked, -32768, -32768),
            "uncalibrated endpoint produced navigation");
    Require(!ObserveNeutral(parked, 0, 0) &&
            !ObserveNeutral(parked, 100, -100) &&
            !ObserveNeutral(parked, 50, -50) &&
            ObserveNeutral(parked, 0, 0) && parked.valid,
            "later central sample did not calibrate the stick");
    Require(!PairActive(parked, 400, -500),
            "ordinary centre noise produced navigation");
    Require(PairActive(parked, -7000, 0) &&
            PairActive(parked, 0, 7000),
            "real horizontal/vertical motion was not retained");

    AxisPairCalibration offset;
    Require(!ObserveNeutral(offset, 3200, -2400) &&
            !ObserveNeutral(offset, 3200, -2400) &&
            !ObserveNeutral(offset, 3200, -2400) &&
            ObserveNeutral(offset, 3200, -2400),
            "valid offset centre was rejected");
    Require(!PairActive(offset, 5000, -1000),
            "offset centre was interpreted as raw absolute motion");
    Require(PairActive(offset, 10000, -2400),
            "delta from an offset centre was not recognized");

    const std::int32_t beforeX = offset.neutralX;
    RelaxNeutral(offset, beforeX + 800, offset.neutralY - 800);
    Require(offset.neutralX > beforeX,
            "small centre drift was not relaxed");
    const std::int32_t relaxedX = offset.neutralX;
    RelaxNeutral(offset, relaxedX + 12000, offset.neutralY);
    Require(offset.neutralX == relaxedX,
            "held motion incorrectly rewrote the neutral centre");

    AxisPairCalibration moving;
    Require(!ObserveNeutral(moving, -7000, 0) &&
            !ObserveNeutral(moving, -3000, 0) &&
            !ObserveNeutral(moving, 2000, 0) &&
            !moving.valid,
            "a moving axis was mistaken for a stable neutral centre");

    Require(!IsDirectionalPov(0xFFFFFFFFu) &&
            !IsDirectionalPov(0x1234FFFFu) &&
            !IsDirectionalPov(36000u) &&
            PovDirections(0u) == PovUp &&
            PovDirections(9000u) == PovRight &&
            PovDirections(18000u) == PovDown &&
            PovDirections(27000u) == PovLeft &&
            PovDirections(4500u) == (PovUp | PovRight),
            "POV neutral/range sanitization or octant mapping regressed");

    DigitalNeutralGate buttonDpad;
    Require(!ObserveDigitalNeutral(buttonDpad, PovLeft) &&
            !ObserveDigitalNeutral(buttonDpad, 0) &&
            !ObserveDigitalNeutral(buttonDpad, 0) &&
            !ObserveDigitalNeutral(buttonDpad, 0) &&
            ObserveDigitalNeutral(buttonDpad, 0) &&
            ObserveDigitalNeutral(buttonDpad, PovLeft),
            "digital dpad was accepted before a stable neutral observation");
    Require(ShouldUseButtonDpadFallback(0) &&
            !ShouldUseButtonDpadFallback(1) &&
            !ShouldUseButtonDpadFallback(4),
            "button 12..15 fallback was allowed on a real POV device");

    return 0;
}
