#pragma once

#include "tutorial_color_policy.h"

#include <cstdint>

namespace Mission::TutorialLayoutPolicy {

struct Typography {
    float prosePx;
    float taskPx;
    float metaPx;
    float controlsPx;
    float bannerPx;
    float pageTitlePx;
};

struct Rect {
    float x;
    float y;
    float w;
    float h;
};

enum class HudFocus : unsigned char {
    None,
    Top,
    Bottom,
    Life,      // Life bars, round marks, and the centre FPS readout
    Meters,
    Sp,
    Rf,
    FinalMemory, // P1 Life plus P1 SP: both conditions for the last-resort Super
    MeterStates, // P1 one/red versus P2 multi-level/light-blue presentation
    RfStates,    // P1 Red RF versus P2 Light Blue RF
    RedIc,
    BlueIc,
    BlueIcMeters,
    Juggle,
    JuggleYellow,
    JuggleRed,
};

constexpr bool HudFocusTextEquals(const char* lhs, const char* rhs) {
    if (!lhs || !rhs) return lhs == rhs;
    while (*lhs && *rhs) {
        if (*lhs != *rhs) return false;
        ++lhs;
        ++rhs;
    }
    return *lhs == *rhs;
}

constexpr HudFocus ParseHudFocus(const char* value) {
    return HudFocusTextEquals(value, "top") ? HudFocus::Top
        : HudFocusTextEquals(value, "bottom") ? HudFocus::Bottom
        : HudFocusTextEquals(value, "life") ? HudFocus::Life
        : HudFocusTextEquals(value, "meters") ? HudFocus::Meters
        : HudFocusTextEquals(value, "sp") ? HudFocus::Sp
        : HudFocusTextEquals(value, "rf") ? HudFocus::Rf
        : HudFocusTextEquals(value, "final_memory") ? HudFocus::FinalMemory
        : HudFocusTextEquals(value, "meter_states") ? HudFocus::MeterStates
        : HudFocusTextEquals(value, "meters_compare") ? HudFocus::MeterStates
        : HudFocusTextEquals(value, "rf_states") ? HudFocus::RfStates
        : HudFocusTextEquals(value, "rf_compare") ? HudFocus::RfStates
        : HudFocusTextEquals(value, "red_ic") ? HudFocus::RedIc
        : HudFocusTextEquals(value, "rf_red") ? HudFocus::RedIc
        : HudFocusTextEquals(value, "blue_ic") ? HudFocus::BlueIc
        : HudFocusTextEquals(value, "rf_blue") ? HudFocus::BlueIc
        : HudFocusTextEquals(value, "blue_ic_meters") ? HudFocus::BlueIcMeters
        : HudFocusTextEquals(value, "meters_blue") ? HudFocus::BlueIcMeters
        : HudFocusTextEquals(value, "juggle") ? HudFocus::Juggle
        : HudFocusTextEquals(value, "juggle_warnings") ? HudFocus::JuggleYellow
        : HudFocusTextEquals(value, "juggle_yellow") ? HudFocus::JuggleYellow
        : HudFocusTextEquals(value, "juggle_red") ? HudFocus::JuggleRed
        : HudFocus::None;
}

constexpr bool IsJuggleFocus(HudFocus focus) {
    return focus == HudFocus::Juggle ||
           focus == HudFocus::JuggleYellow ||
           focus == HudFocus::JuggleRed;
}

constexpr bool KeepsTopHudVisible(HudFocus focus) {
    return focus == HudFocus::Top || focus == HudFocus::Life ||
           focus == HudFocus::FinalMemory;
}

constexpr bool KeepsBottomHudVisible(HudFocus focus) {
    return focus == HudFocus::Bottom || focus == HudFocus::Meters ||
           focus == HudFocus::Sp || focus == HudFocus::Rf ||
           focus == HudFocus::FinalMemory ||
           focus == HudFocus::MeterStates || focus == HudFocus::RfStates ||
           focus == HudFocus::RedIc || focus == HudFocus::BlueIc ||
           focus == HudFocus::BlueIcMeters;
}

// EFZ draws at 320x240 and nearest-neighbour upscales to this 640x480 canvas.
// The middle eight-pixel gap keeps page chrome clear of the native HUD bands.
constexpr Rect PageBackdropDim(HudFocus focus) {
    return IsJuggleFocus(focus) ? Rect{}
        : KeepsTopHudVisible(focus) && KeepsBottomHudVisible(focus)
        ? Rect{0.0f, 92.0f, 640.0f, 320.0f}
        : KeepsTopHudVisible(focus) ? Rect{0.0f, 92.0f, 640.0f, 388.0f}
        : KeepsBottomHudVisible(focus) ? Rect{0.0f, 0.0f, 640.0f, 412.0f}
        : Rect{0.0f, 0.0f, 640.0f, 480.0f};
}

constexpr Rect TopHudBounds() { return {0.0f, 0.0f, 640.0f, 92.0f}; }
constexpr Rect BottomHudBounds() { return {0.0f, 412.0f, 640.0f, 66.0f}; }
constexpr Rect P1LifeBounds() { return {34.0f, 34.0f, 244.0f, 16.0f}; }
constexpr Rect P2LifeBounds() { return {362.0f, 34.0f, 244.0f, 16.0f}; }
constexpr Rect FpsBounds() { return {278.0f, 6.0f, 84.0f, 60.0f}; }
constexpr Rect P1RoundsBounds() { return {244.0f, 64.0f, 34.0f, 30.0f}; }
constexpr Rect P2RoundsBounds() { return {362.0f, 64.0f, 34.0f, 30.0f}; }
constexpr Rect P1SpIconBounds() { return {10.0f, 416.0f, 52.0f, 52.0f}; }
constexpr Rect P1SpBarBounds() { return {62.0f, 446.0f, 220.0f, 12.0f}; }
constexpr Rect P2SpBarBounds() { return {358.0f, 446.0f, 220.0f, 12.0f}; }
constexpr Rect P2SpIconBounds() { return {578.0f, 416.0f, 52.0f, 52.0f}; }
constexpr Rect P1RfBounds() { return {156.0f, 462.0f, 124.0f, 8.0f}; }
constexpr Rect P2RfBounds() { return {360.0f, 462.0f, 124.0f, 8.0f}; }

constexpr int HudFocusRectCount(HudFocus focus) {
    return focus == HudFocus::Top || focus == HudFocus::Bottom ? 1
        : focus == HudFocus::Life ? 5
        : focus == HudFocus::Meters ? 6
        : focus == HudFocus::Sp ? 4
        : focus == HudFocus::Rf ? 2
        : focus == HudFocus::FinalMemory ? 3
        : focus == HudFocus::MeterStates ? 6
        : focus == HudFocus::RfStates ? 2
        : focus == HudFocus::RedIc || focus == HudFocus::BlueIc ? 1
        : focus == HudFocus::BlueIcMeters ? 3
        : 0;
}

constexpr Rect HudFocusRect(HudFocus focus, int index) {
    if (focus == HudFocus::Top) return index == 0 ? TopHudBounds() : Rect{};
    if (focus == HudFocus::Bottom) return index == 0 ? BottomHudBounds() : Rect{};
    if (focus == HudFocus::Life) {
        return index == 0 ? P1LifeBounds()
            : index == 1 ? P2LifeBounds()
            : index == 2 ? FpsBounds()
            : index == 3 ? P1RoundsBounds()
            : index == 4 ? P2RoundsBounds() : Rect{};
    }
    if (focus == HudFocus::Meters || focus == HudFocus::Sp ||
        focus == HudFocus::MeterStates) {
        if (index == 0) return P1SpIconBounds();
        if (index == 1) return P1SpBarBounds();
        if (index == 2) return P2SpBarBounds();
        if (index == 3) return P2SpIconBounds();
        if ((focus == HudFocus::Meters || focus == HudFocus::MeterStates) &&
            index == 4) return P1RfBounds();
        if ((focus == HudFocus::Meters || focus == HudFocus::MeterStates) &&
            index == 5) return P2RfBounds();
        return {};
    }
    if (focus == HudFocus::Rf || focus == HudFocus::RfStates) {
        return index == 0 ? P1RfBounds()
            : index == 1 ? P2RfBounds() : Rect{};
    }
    if (focus == HudFocus::RedIc || focus == HudFocus::BlueIc) {
        return index == 0 ? P1RfBounds() : Rect{};
    }
    if (focus == HudFocus::BlueIcMeters) {
        return index == 0 ? P1SpIconBounds()
            : index == 1 ? P1SpBarBounds()
            : index == 2 ? P1RfBounds() : Rect{};
    }
    if (focus == HudFocus::FinalMemory) {
        return index == 0 ? P1LifeBounds()
            : index == 1 ? P1SpIconBounds()
            : index == 2 ? P1SpBarBounds() : Rect{};
    }
    return {};
}

constexpr TutorialColorPolicy::Tone HudFocusTone(HudFocus focus, int index) {
    using Tone = TutorialColorPolicy::Tone;
    if (focus == HudFocus::Life) {
        return index <= 1 ? Tone::Life
            : index == 2 ? Tone::Fps : Tone::Rounds;
    }
    if (focus == HudFocus::Sp) return Tone::Sp;
    if (focus == HudFocus::MeterStates) {
        return index < 4 ? Tone::Sp
            : index == 4 ? Tone::RedIc : Tone::BlueIc;
    }
    if (focus == HudFocus::RfStates) {
        return index == 0 ? Tone::RedIc : Tone::BlueIc;
    }
    if (focus == HudFocus::RedIc) return Tone::RedIc;
    if (focus == HudFocus::BlueIc) return Tone::BlueIc;
    if (focus == HudFocus::BlueIcMeters) {
        return index < 2 ? Tone::Sp : Tone::BlueIc;
    }
    if (focus == HudFocus::FinalMemory) {
        return index == 0 ? Tone::Life : Tone::Sp;
    }
    return Tone::Accent;
}

constexpr short JugglePreviewUntech(HudFocus focus) {
    return focus == HudFocus::Juggle ? 100
        : focus == HudFocus::JuggleYellow ? 60
        : focus == HudFocus::JuggleRed ? 20 : 0;
}

// A staged hit reaction needs two battle calls after the thaw before EFZ publishes its animation.
// Monitor ticks do not count: they continue at 192 Hz while Practice is paused.
constexpr std::uint32_t JugglePreviewRefreshBattleUpdates() { return 2u; }
constexpr int JugglePreviewRefreshWatchdogTicks() { return 96; }
constexpr bool JugglePreviewRefreshComplete(std::uint32_t startBatch,
                                            std::uint32_t completedBatch) {
    // `completedBatch` may legitimately be one call behind `startBatch`; a raw
    // unsigned compare would turn that -1 into UINT_MAX and finish immediately.
    return static_cast<std::int32_t>(completedBatch - startBatch) >=
           static_cast<std::int32_t>(
               JugglePreviewRefreshBattleUpdates());
}
constexpr bool BattleBatchReached(std::uint32_t targetBatch,
                                  std::uint32_t completedBatch) {
    return static_cast<std::int32_t>(completedBatch - targetBatch) >= 0;
}

constexpr TutorialColorPolicy::Tone JugglePreviewTone(HudFocus focus) {
    using Tone = TutorialColorPolicy::Tone;
    return focus == HudFocus::JuggleYellow ? Tone::JuggleYellow
        : focus == HudFocus::JuggleRed ? Tone::JuggleRed : Tone::Juggle;
}

enum class JuggleBand : unsigned char { Hidden, Red, Yellow, Normal };

constexpr JuggleBand JuggleBandForUntech(int untech) {
    return untech <= 0 ? JuggleBand::Hidden
        : untech <= 30 ? JuggleBand::Red
        : untech <= 60 ? JuggleBand::Yellow : JuggleBand::Normal;
}

constexpr int JuggleWidth(int untech) {
    return untech <= 0 ? 0 : (untech > 100 ? 100 : untech);
}

// Facing is a signed byte read through unsigned call sites: +1 (0x01) right, -1 (0xFF) left.
// A 0/1 reading rejects every ordinary P2-facing-left state.
constexpr bool IsKnownFacingByte(std::uint8_t facing) {
    return facing == 0x01u || facing == 0xFFu;
}

constexpr bool FacesRight(std::uint8_t facing) {
    return facing == 0x01u;
}

enum class ActionStepVisualState {
    Future,
    Current,
    Armed,
    Failed,
    Done,
};

constexpr ActionStepVisualState ResolveActionStepVisualState(
    int actionIndex, int currentIndex, bool currentArmed,
    bool currentFailed, bool taskDone) {
    if (taskDone || actionIndex < currentIndex) {
        return ActionStepVisualState::Done;
    }
    if (actionIndex > currentIndex) {
        return ActionStepVisualState::Future;
    }
    if (currentFailed) {
        return ActionStepVisualState::Failed;
    }
    return currentArmed ? ActionStepVisualState::Armed
                        : ActionStepVisualState::Current;
}

constexpr float Clamp(float value, float low, float high) {
    return value < low ? low : (value > high ? high : value);
}

constexpr Typography TypographyFor(float uiScale) {
    return {
        Clamp(13.0f * uiScale, 12.0f, 16.0f),
        Clamp(12.0f * uiScale, 11.0f, 15.0f),
        Clamp(10.0f * uiScale, 9.0f, 12.0f),
        Clamp(12.0f * uiScale, 11.0f, 14.0f),
        Clamp(14.0f * uiScale, 13.0f, 18.0f),
        Clamp(16.0f * uiScale, 14.0f, 20.0f),
    };
}

constexpr Rect PageBanner() { return {48.0f, 92.0f, 544.0f, 32.0f}; }
constexpr Rect ActiveRequirements() { return {8.0f, 108.0f, 372.0f, 252.0f}; }
constexpr Rect BelowStatsRequirements() { return {8.0f, 206.0f, 372.0f, 206.0f}; }
constexpr Rect RequirementBounds(bool belowStats) {
    return belowStats ? BelowStatsRequirements() : ActiveRequirements();
}
constexpr int MaxVisibleRequirements() { return 8; }
constexpr Rect PageCard() { return {48.0f, 124.0f, 544.0f, 236.0f}; }
constexpr Rect PageActions() { return {48.0f, 368.0f, 544.0f, 36.0f}; }

constexpr Rect JugglePageBanner() { return {16.0f, 92.0f, 608.0f, 32.0f}; }
constexpr Rect JugglePageCard() { return {16.0f, 124.0f, 320.0f, 236.0f}; }
constexpr Rect JugglePageActions() { return {16.0f, 368.0f, 608.0f, 36.0f}; }
constexpr Rect JugglePreviewRail() { return {344.0f, 124.0f, 280.0f, 236.0f}; }

constexpr Rect PageBannerFor(HudFocus focus) {
    return IsJuggleFocus(focus) ? JugglePageBanner() : PageBanner();
}
constexpr Rect PageCardFor(HudFocus focus) {
    return IsJuggleFocus(focus) ? JugglePageCard() : PageCard();
}
constexpr Rect PageActionsFor(HudFocus focus) {
    return IsJuggleFocus(focus) ? JugglePageActions() : PageActions();
}

constexpr Rect ModalTitleBand() { return {0.0f, 0.0f, 640.0f, 30.0f}; }
constexpr Rect ModalMetaBand() { return {0.0f, 30.0f, 640.0f, 26.0f}; }
constexpr float ModalFooterHeight(float uiScale) {
    return Clamp(52.0f * uiScale, 48.0f, 68.0f);
}
constexpr Rect ModalFooter(float uiScale) {
    const float h = ModalFooterHeight(uiScale);
    return {12.0f, 472.0f - h, 616.0f, h};
}
constexpr Rect ModalContent(float uiScale) {
    const Rect footer = ModalFooter(uiScale);
    return {70.0f, 68.0f, 500.0f, footer.y - 76.0f};
}

constexpr bool WithinCanvas(const Rect& rect) {
    return rect.x >= 0.0f && rect.y >= 0.0f &&
           rect.x + rect.w <= 640.0f && rect.y + rect.h <= 480.0f;
}

constexpr bool SafeBandsDoNotOverlap() {
    return ActiveRequirements().y >= 96.0f &&
           ActiveRequirements().y + ActiveRequirements().h <= 412.0f &&
           BelowStatsRequirements().y >= 206.0f &&
           BelowStatsRequirements().y + BelowStatsRequirements().h <= 412.0f &&
           PageBanner().y + PageBanner().h <= PageCard().y &&
           PageCard().y + PageCard().h < PageActions().y &&
           PageActions().y + PageActions().h <= 412.0f &&
           JugglePageBanner().y + JugglePageBanner().h <= JugglePageCard().y &&
           JugglePageCard().x + JugglePageCard().w < JugglePreviewRail().x &&
           JugglePageCard().y + JugglePageCard().h < JugglePageActions().y &&
           JugglePageActions().y + JugglePageActions().h <= 412.0f;
}

constexpr int RequirementWindowStart(int taskCount, int visibleCount,
                                     int currentIndex) {
    if (taskCount <= 0 || visibleCount <= 0 ||
        visibleCount >= taskCount) return 0;
    int start = currentIndex - visibleCount / 2;
    if (start < 0) start = 0;
    const int lastStart = taskCount - visibleCount;
    return start > lastStart ? lastStart : start;
}

constexpr int ActionWindowStart(int actionCount, int visibleCount,
                                int currentIndex) {
    if (actionCount <= 0 || visibleCount <= 0 ||
        visibleCount >= actionCount) return 0;
    int start = currentIndex - visibleCount / 2;
    if (start < 0) start = 0;
    const int lastStart = actionCount - visibleCount;
    return start > lastStart ? lastStart : start;
}

}
