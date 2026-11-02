#include "../include/gui/custom_menu/scale.h"

#include <algorithm>
#include <cmath>

namespace CustomMenu::Scale {

namespace {

Metrics g_metrics{};
float g_appliedScale = 0.0f;

float Round(float v) {
    return std::floor(v + 0.5f);
}

float RoundClamped(float base, float scale, float minValue, float maxValue) {
    return (std::max)(minValue, (std::min)(maxValue, Round(base * scale)));
}

}

float NormalizeUiScale(float scale) {
    if (scale < kUiScaleMin) return kUiScaleMin;
    if (scale > kUiScaleMax) return kUiScaleMax;
    return scale;
}

void Update(float uiScale) {
    const float normalized = NormalizeUiScale(uiScale);
    if (std::fabs(normalized - g_appliedScale) < 0.001f) return;

    Metrics m{};
    m.uiScale = normalized;
    m.fontScale = normalized;
    m.layoutScale = normalized;

    m.panelPadX = RoundClamped(28.0f, normalized, 18.0f, 42.0f);
    m.panelPadY = RoundClamped( 6.0f, normalized,  4.0f, 10.0f);
    m.rowHeight = RoundClamped(22.0f, normalized, 16.0f, 34.0f);
    m.rowPadX = RoundClamped(18.0f, normalized, 10.0f, 28.0f);
    m.sectionPadY = RoundClamped(4.0f, normalized, 2.0f, 8.0f);
    m.titleRowHeight = RoundClamped(30.0f, normalized, 24.0f, 45.0f);
    m.tabBarHeight = RoundClamped(28.0f, normalized, 22.0f, 42.0f);
    m.tabGapX = RoundClamped(18.0f, normalized, 10.0f, 28.0f);
    m.sliderTrackH = RoundClamped(4.0f, normalized, 3.0f, 6.0f);
    m.sliderTrackW = RoundClamped(160.0f, normalized, 112.0f, 240.0f);

    m.bodyPx = RoundClamped(11.0f, normalized, 8.0f, 18.0f);
    m.headerPx = RoundClamped(16.0f, normalized, 11.0f, 24.0f);

    g_metrics = m;
    g_appliedScale = normalized;
}

const Metrics& Get() {
    if (g_appliedScale <= 0.0f) {
        Update(1.0f);
    }
    return g_metrics;
}

float Snap(float v) {
    return Round(v);
}

float SnapDown(float v) {
    return std::floor(v);
}

float SnapUp(float v) {
    return std::ceil(v);
}

ImVec2 Snap(const ImVec2& v) {
    return ImVec2(Snap(v.x), Snap(v.y));
}

}
