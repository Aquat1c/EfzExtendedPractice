#pragma once

#include "../3rdparty/imgui/imgui.h"

namespace CustomMenu::Scale {

constexpr float kUiScaleMin = 0.70f;
constexpr float kUiScaleMax = 1.50f;

struct Metrics {
    float uiScale = 1.0f;
    float fontScale = 1.0f;
    float layoutScale = 1.0f;

    float panelPadX = 28.0f;
    float panelPadY = 6.0f;
    float rowHeight = 22.0f;
    float rowPadX = 18.0f;
    float sectionPadY = 4.0f;
    float titleRowHeight = 30.0f;
    float tabBarHeight = 28.0f;
    float tabGapX = 18.0f;
    float sliderTrackH = 4.0f;
    float sliderTrackW = 160.0f;

    float bodyPx = 11.0f;
    float headerPx = 16.0f;
};

float NormalizeUiScale(float scale);
void Update(float uiScale);
const Metrics& Get();

float Snap(float v);
float SnapDown(float v);
float SnapUp(float v);
ImVec2 Snap(const ImVec2& v);

}
