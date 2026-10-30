#pragma once

#include <cstdint>

// Pure DirectInput axis calibration policy.  It intentionally has no Windows
// types so the failure cases can be covered by the host-side test binary.
namespace GenericPadAxisPolicy {

// DirectInput is explicitly configured to -32768..32767, so a usable position
// axis should settle close to zero.  Keeping the initial window narrower than
// the navigation threshold prevents a stick held during enumeration from
// being learned as its centre.
constexpr std::int32_t kPlausibleNeutralLimit = 8192;
constexpr std::int32_t kNeutralStabilityTolerance = 1024;
constexpr std::uint8_t kNeutralSamplesRequired = 4;
constexpr std::int32_t kNavigationThreshold = 6000;
constexpr std::uint32_t kPovMaximumAngle = 35999u;

constexpr std::int32_t Abs(std::int32_t value) {
    return value < 0 ? -value : value;
}

constexpr bool IsPlausibleNeutral(std::int32_t value) {
    return Abs(value) <= kPlausibleNeutralLimit;
}

struct AxisPairCalibration {
    bool valid = false;
    std::int32_t neutralX = 0;
    std::int32_t neutralY = 0;
    std::int32_t candidateX = 0;
    std::int32_t candidateY = 0;
    std::uint8_t stableSamples = 0;
};

inline bool ObserveNeutral(AxisPairCalibration& calibration,
                           std::int32_t x, std::int32_t y) {
    if (calibration.valid) return false;
    // Unused DirectInput axes on several leverless/DualSense modes sit at a
    // signed endpoint while idle.  Never accept that endpoint as a stick
    // centre; wait until a real, central sample exists.
    if (!IsPlausibleNeutral(x) || !IsPlausibleNeutral(y)) {
        calibration.stableSamples = 0;
        return false;
    }
    if (calibration.stableSamples == 0 ||
        Abs(x - calibration.candidateX) > kNeutralStabilityTolerance ||
        Abs(y - calibration.candidateY) > kNeutralStabilityTolerance) {
        calibration.candidateX = x;
        calibration.candidateY = y;
        calibration.stableSamples = 1;
        return false;
    }
    calibration.candidateX += (x - calibration.candidateX) / 2;
    calibration.candidateY += (y - calibration.candidateY) / 2;
    if (calibration.stableSamples < kNeutralSamplesRequired) {
        ++calibration.stableSamples;
    }
    if (calibration.stableSamples < kNeutralSamplesRequired) return false;
    calibration.valid = true;
    calibration.neutralX = calibration.candidateX;
    calibration.neutralY = calibration.candidateY;
    return true;
}

constexpr std::int32_t Delta(std::int32_t value, std::int32_t neutral) {
    return value - neutral;
}

constexpr bool IsActive(std::int32_t value,
                        std::int32_t threshold = kNavigationThreshold) {
    return value >= threshold || value <= -threshold;
}

constexpr bool PairActive(const AxisPairCalibration& calibration,
                          std::int32_t x, std::int32_t y,
                          std::int32_t threshold = kNavigationThreshold) {
    return calibration.valid &&
           (IsActive(Delta(x, calibration.neutralX), threshold) ||
            IsActive(Delta(y, calibration.neutralY), threshold));
}

inline void RelaxNeutral(AxisPairCalibration& calibration,
                         std::int32_t x, std::int32_t y) {
    if (!calibration.valid) return;
    const std::int32_t dx = Delta(x, calibration.neutralX);
    const std::int32_t dy = Delta(y, calibration.neutralY);
    // Track only tiny centre drift.  A held stick must never teach the shim a
    // new neutral and then emit the opposite direction when released.
    if (Abs(dx) <= 1024) calibration.neutralX += dx / 8;
    if (Abs(dy) <= 1024) calibration.neutralY += dy / 8;
}

// DirectInput documents POV neutral as 0xFFFFFFFF, but several HID drivers
// only set the low word to 0xFFFF.  Other drivers leave an out-of-range value
// in the field.  All three forms are neutral, never a direction.
constexpr bool IsDirectionalPov(std::uint32_t pov) {
    return (pov & 0xFFFFu) != 0xFFFFu && pov <= kPovMaximumAngle;
}

enum PovDirection : std::uint8_t {
    PovUp = 1u << 0,
    PovRight = 1u << 1,
    PovDown = 1u << 2,
    PovLeft = 1u << 3,
};

constexpr std::uint8_t PovDirections(std::uint32_t pov) {
    if (!IsDirectionalPov(pov)) return 0;
    std::uint8_t directions = 0;
    if (pov >= 31500u || pov <= 4500u) directions |= PovUp;
    if (pov >= 4500u && pov <= 13500u) directions |= PovRight;
    if (pov >= 13500u && pov <= 22500u) directions |= PovDown;
    if (pov >= 22500u && pov <= 31500u) directions |= PovLeft;
    return directions;
}

// Digital direction fallbacks are not trusted until the device has published
// several fully neutral samples.  This preserves button-only/POV fight sticks
// while permanently-held bogus bits can never become idle menu navigation.
struct DigitalNeutralGate {
    bool armed = false;
    std::uint8_t neutralSamples = 0;
};

inline bool ObserveDigitalNeutral(DigitalNeutralGate& gate,
                                  std::uint8_t heldDirections) {
    if (gate.armed) return true;
    if (heldDirections != 0) {
        gate.neutralSamples = 0;
        return false;
    }
    if (gate.neutralSamples < kNeutralSamplesRequired) {
        ++gate.neutralSamples;
    }
    if (gate.neutralSamples >= kNeutralSamplesRequired) {
        gate.armed = true;
    }
    return gate.armed;
}

constexpr bool ShouldUseButtonDpadFallback(std::uint32_t povObjectCount) {
    return povObjectCount == 0;
}

// Sony DS4/DualSense over DirectInput: Z/Rz are the right stick, Rx/Ry are analog
// L2/R2, and buttons 6/7 are digital L2/R2. Unknown pads keep the generic mapping.
constexpr bool UsesZRzRightStick(std::uint16_t vendor, std::uint16_t product) {
    return vendor == 0x054C &&
           (product == 0x05C4 || product == 0x09CC || product == 0x0BA0 ||
            product == 0x0CE6 || product == 0x0DF2);
}

} // namespace GenericPadAxisPolicy
