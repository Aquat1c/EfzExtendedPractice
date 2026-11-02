#pragma once

#include <cstdint>

namespace PhysicalPollSamplePolicy {

// Mask and serial are published as one 32-bit atomic word so readers never observe a mismatched pair.
constexpr std::uint32_t kMaskBits = 0xFFu;
constexpr std::uint32_t kSerialBits = 0x00FFFFFFu;

constexpr std::uint32_t Pack(std::uint32_t serial, std::uint8_t mask) {
    return ((serial & kSerialBits) << 8) |
           static_cast<std::uint32_t>(mask);
}

constexpr std::uint8_t Mask(std::uint32_t sample) {
    return static_cast<std::uint8_t>(sample & kMaskBits);
}

constexpr std::uint32_t Serial(std::uint32_t sample) {
    return (sample >> 8) & kSerialBits;
}

constexpr std::uint32_t Next(std::uint32_t previous,
                             std::uint8_t mask) {
    std::uint32_t serial = (Serial(previous) + 1u) & kSerialBits;
    // Serial 0 is the never-observed baseline; skip it on wrap.
    if (serial == 0) serial = 1;
    return Pack(serial, mask);
}

}
