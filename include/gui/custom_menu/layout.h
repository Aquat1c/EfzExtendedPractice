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

// Font accessors
// Returns the requested face, or nullptr if not yet loaded.
ImFont* BodyFont();
ImFont* HeaderFont();

// Convenience wrappers that pick a sensible pixel size (0 = use font's default
// SizePixels). Safe to call with nullptr font - then falls back to ImGui's
// default font.
// Named DrawString to sidestep the winuser.h `DrawText` macro collision.
void DrawString(ImDrawList* dl, ImFont* font, float px, float x, float y, ImU32 col, const char* text);

// Compute text width using the given font, falling back to ImGui::CalcTextSize.
float MeasureTextW(ImFont* font, float px, const char* text);

// Panel + title
// Draws the full-screen backdrop and the centered panel frame. Returns the
// content origin (top-left of the inner content area, after the title block).
ImVec2 DrawPanel(ImDrawList* dl, const char* title);

// Draws a horizontal rule inset by kRuleInsetX from panel edges.
void DrawSectionRule(ImDrawList* dl, float panelX, float y, float panelW, ImU32 col = Theme::kRule);

// Tab bar
// Draws a horizontal tab strip centered inside the given width. activeIdx is
// 0-based. Labels should be uppercase short strings.
void DrawTabBar(ImDrawList* dl, float x, float y, float w,
                const char* const* labels, int count, int activeIdx,
                int focusedIdx /* -1 = none */);
float TabBarWidth(const char* const* labels, int count);
// Tab under pointX for a strip DrawTabBar laid out in the same x and w, or -1.
int TabBarIndexAt(float x, float w, const char* const* labels, int count, float pointX);

// Rows
// Each row primitive takes its own rect and a `focused` flag. They draw both
// the subtle selected-row background (if focused) and the row content. No
// hit-testing, no input - caller owns focus state.

// Section header (uppercase, dim, top padding). Adds kSectionPadY above and
// kRuleInsetX-indented content.
void DrawHeader(ImDrawList* dl, float x, float y, float w, const char* text);

// Selected-row background.
void DrawRowSelectedBg(ImDrawList* dl, float x, float y, float w, float h);

// LABEL ........................ VALUE
// Generic two-column row used by multiple row types. Draws focus bg,
// left-aligned label, right-aligned value text (may be nullptr).
void DrawRowLabelValue(
    ImDrawList* dl, float x, float y, float w,
    const char* label, const char* value,
    bool focused, bool disabled = false);

// LABEL ............... OPT1  OPT2  OPT3  (selected option drawn bright)
void DrawRowInlineChoices(
    ImDrawList* dl, float x, float y, float w,
    const char* label,
    const char* const* choices, int choiceCount, int currentIdx,
    bool focused, bool disabled = false);

// LABEL ....................... [ON] / [OFF]
void DrawRowToggle(
    ImDrawList* dl, float x, float y, float w,
    const char* label, bool value,
    bool focused, bool disabled = false);

// LABEL ................. ‹ 9999 ›
// valueText is rendered between angle brackets. focused row gets brighter brackets.
void DrawRowNumber(
    ImDrawList* dl, float x, float y, float w,
    const char* label, const char* valueText,
    bool focused, bool disabled = false);

// LABEL ................. VALUE   ▶
// Drill-down row: right-side value + right-arrow glyph.
void DrawRowDrill(
    ImDrawList* dl, float x, float y, float w,
    const char* label, const char* valueText,
    bool focused, bool disabled = false);
void DrawRowDrillSegments(
    ImDrawList* dl, float x, float y, float w,
    const char* label,
    const TextSegment* valueSegments, int valueSegmentCount,
    bool focused, bool disabled = false);

// LABEL ..... [====----]  3/8
// Slider row. progress is [0,1]. trackPx gives the slider track width.
void DrawRowSlider(
    ImDrawList* dl, float x, float y, float w,
    const char* label, const char* valueText, float progress01,
    bool focused, bool disabled = false);

// Single button centered in its own rect. Used for button rows (Apply /
// Refresh / Exit). Caller draws multiple side-by-side.
void DrawButton(
    ImDrawList* dl, float x, float y, float w, float h,
    const char* label, bool focused, bool disabled = false);

// Native (in-game) menu primitives
// These reproduce the Revival menu language: beveled metal strips, hard black
// outlines on uppercase text, a solid title band, and the bottom description
// box. Coordinates are 640x480 canvas space.

// Uppercase menu text with a hard black outline (readable over anything).
void DrawOutlinedText(ImDrawList* dl, ImFont* font, float px, float x, float y,
                      ImU32 col, const char* text);

// Black ruled row used by in-session Mission/Tutorial surfaces. This draws
// chrome only so callers can supply either plain centered labels or rich,
// wrapped tutorial text without duplicating the focus treatment.
void DrawSessionRowChrome(ImDrawList* dl, float x, float y, float w, float h,
                          bool selected);

// Full-width beveled bar (selected = bright steel, disabled = dark).
void DrawNativeBar(ImDrawList* dl, float x, float y, float w, float h,
                   bool selected, bool disabled = false);

// Convenience: bar + centered outlined label (the netplay-menu row).
void DrawNativeBarCentered(ImDrawList* dl, float x, float y, float w, float h,
                           const char* label, bool selected, bool disabled = false);

// Solid black title band with centered header text; returns the band bottom.
float DrawTitleBand(ImDrawList* dl, const char* title,
                    const char* rightStatus = nullptr, float y = 0.0f);

// Black description box with a thin white border (the game's bottom info box).
void DrawInfoBox(ImDrawList* dl, float x, float y, float w, float h);

} // namespace CustomMenu::Layout
