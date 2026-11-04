#pragma once

#include "theme.h"
#include "../3rdparty/imgui/imgui.h"

struct ImDrawList;
struct ImFont;

namespace CustomMenu::Layout {

struct TextSegment {
    const char* text;
    ImU32 color;
};

ImFont* BodyFont();
ImFont* HeaderFont();

void DrawString(ImDrawList* dl, ImFont* font, float px, float x, float y, ImU32 col, const char* text);

float MeasureTextW(ImFont* font, float px, const char* text);

ImVec2 DrawPanel(ImDrawList* dl, const char* title);

void DrawSectionRule(ImDrawList* dl, float panelX, float y, float panelW, ImU32 col = Theme::kRule);

void DrawTabBar(ImDrawList* dl, float x, float y, float w,
                const char* const* labels, int count, int activeIdx,
                int focusedIdx /* -1 = none */);
float TabBarWidth(const char* const* labels, int count);
int TabBarIndexAt(float x, float y, float w, const char* const* labels, int count, const ImVec2& point);

void DrawHeader(ImDrawList* dl, float x, float y, float w, const char* text);

void DrawRowSelectedBg(ImDrawList* dl, float x, float y, float w, float h);

void DrawRowLabelValue(
    ImDrawList* dl, float x, float y, float w,
    const char* label, const char* value,
    bool focused, bool disabled = false);

void DrawRowInlineChoices(
    ImDrawList* dl, float x, float y, float w,
    const char* label,
    const char* const* choices, int choiceCount, int currentIdx,
    bool focused, bool disabled = false);

void DrawRowToggle(
    ImDrawList* dl, float x, float y, float w,
    const char* label, bool value,
    bool focused, bool disabled = false);

void DrawRowNumber(
    ImDrawList* dl, float x, float y, float w,
    const char* label, const char* valueText,
    bool focused, bool disabled = false);

void DrawRowDrill(
    ImDrawList* dl, float x, float y, float w,
    const char* label, const char* valueText,
    bool focused, bool disabled = false);
void DrawRowDrillSegments(
    ImDrawList* dl, float x, float y, float w,
    const char* label,
    const TextSegment* valueSegments, int valueSegmentCount,
    bool focused, bool disabled = false);

// progress is in [0,1]; trackPx is the slider track width.
void DrawRowSlider(
    ImDrawList* dl, float x, float y, float w,
    const char* label, const char* valueText, float progress01,
    bool focused, bool disabled = false);

void DrawButton(
    ImDrawList* dl, float x, float y, float w, float h,
    const char* label, bool focused, bool disabled = false);

// Native Revival-style primitives; coordinates are in 640x480 canvas space.

void DrawOutlinedText(ImDrawList* dl, ImFont* font, float px, float x, float y,
                      ImU32 col, const char* text);

void DrawSessionRowChrome(ImDrawList* dl, float x, float y, float w, float h,
                          bool selected);

void DrawNativeBar(ImDrawList* dl, float x, float y, float w, float h,
                   bool selected, bool disabled = false);

void DrawNativeBarCentered(ImDrawList* dl, float x, float y, float w, float h,
                           const char* label, bool selected, bool disabled = false);

float DrawTitleBand(ImDrawList* dl, const char* title,
                    const char* rightStatus = nullptr, float y = 0.0f);

void DrawInfoBox(ImDrawList* dl, float x, float y, float w, float h);

}
