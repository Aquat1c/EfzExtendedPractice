#pragma once

#include <cstdint>

namespace Mission::TutorialColorPolicy {

enum class Tone : std::uint8_t {
    Default,
    Accent,
    Life,
    Rounds,
    Fps,
    Sp,
    RedIc,
    BlueIc,
    Power,
    Juggle,
    JuggleYellow,
    JuggleRed,
};

struct Rgba {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
    std::uint8_t a;
};

constexpr bool TextEquals(const char* lhs, const char* rhs) {
    if (!lhs || !rhs) return lhs == rhs;
    while (*lhs && *rhs) {
        if (*lhs != *rhs) return false;
        ++lhs;
        ++rhs;
    }
    return *lhs == *rhs;
}

constexpr Tone ParseTone(const char* value) {
    return TextEquals(value, "accent") ? Tone::Accent
        : TextEquals(value, "life") ? Tone::Life
        : TextEquals(value, "rounds") ? Tone::Rounds
        : TextEquals(value, "fps") ? Tone::Fps
        : TextEquals(value, "sp") ? Tone::Sp
        : TextEquals(value, "red_ic") ? Tone::RedIc
        : TextEquals(value, "blue_ic") ? Tone::BlueIc
        : TextEquals(value, "power") ? Tone::Power
        : TextEquals(value, "juggle") ? Tone::Juggle
        : TextEquals(value, "juggle_yellow") ? Tone::JuggleYellow
        : TextEquals(value, "juggle_red") ? Tone::JuggleRed
        : Tone::Default;
}

constexpr Rgba Color(Tone tone) {
    return tone == Tone::Life ? Rgba{110, 235, 135, 255}
        : tone == Tone::Rounds ? Rgba{255, 205, 80, 255}
        : tone == Tone::Fps ? Rgba{130, 190, 255, 255}
        : tone == Tone::Sp ? Rgba{255, 190, 75, 255}
        : tone == Tone::RedIc ? Rgba{255, 95, 105, 255}
        : tone == Tone::BlueIc ? Rgba{105, 225, 255, 255}
        : tone == Tone::Power ? Rgba{200, 145, 255, 255}
        : tone == Tone::Juggle ? Rgba{0, 177, 171, 255}
        : tone == Tone::JuggleYellow ? Rgba{255, 232, 13, 255}
        : tone == Tone::JuggleRed ? Rgba{255, 80, 80, 255}
        : Rgba{130, 235, 235, 255};
}

}
