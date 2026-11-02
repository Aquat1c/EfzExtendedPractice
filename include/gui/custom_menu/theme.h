#pragma once

#include "../3rdparty/imgui/imgui.h"

namespace CustomMenu::Theme {

// Virtual design resolution; all coordinates are in this space.
constexpr float kCanvasW = 640.0f;
constexpr float kCanvasH = 480.0f;

constexpr ImU32 kBackdrop        = IM_COL32(0,   0,   0,    55);
constexpr ImU32 kPanel           = IM_COL32(0,   0,   0,    55);
constexpr ImU32 kStrip           = IM_COL32(0,   0,   0,   205);
constexpr ImU32 kStripStrong     = IM_COL32(0,   0,   0,   225);
constexpr ImU32 kTextActive      = IM_COL32(255, 255, 255, 255);
constexpr ImU32 kTextInactive    = IM_COL32(150, 150, 150, 235);
constexpr ImU32 kTextHeader      = IM_COL32(235, 235, 235, 255);
constexpr ImU32 kTextDisabled    = IM_COL32( 96,  96,  96, 255);
constexpr ImU32 kTextLockedValue = IM_COL32(205, 205, 205, 235);
constexpr ImU32 kTextStatus      = IM_COL32(170, 170, 170, 255);
constexpr ImU32 kInfoFill        = IM_COL32(  4,  16,  18,  126);
constexpr ImU32 kInfoAccent      = IM_COL32(105, 235, 235,  150);
constexpr ImU32 kSelectedFill    = IM_COL32( 30, 155, 190,  105);
constexpr ImU32 kSelectedLine    = IM_COL32(105, 235, 235,  190);
constexpr ImU32 kRowFocusFill    = IM_COL32(210, 220, 225,  32);
constexpr ImU32 kRowFocusLine    = IM_COL32(255, 255, 255, 210);
constexpr ImU32 kDisabledFill    = IM_COL32(0,   0,   0,   120);
constexpr ImU32 kDisabledFocus   = IM_COL32(255, 255, 255,   18);
constexpr ImU32 kCursorDisabled  = IM_COL32(120, 120, 120, 255);
constexpr ImU32 kRule            = IM_COL32(255, 255, 255,  230);
constexpr ImU32 kRuleDim         = IM_COL32(255, 255, 255,  145);
constexpr ImU32 kSliderTrackOn   = IM_COL32(255, 255, 255, 220);
constexpr ImU32 kSliderTrackOff  = IM_COL32(150, 150, 150, 120);
constexpr ImU32 kButtonActiveBg  = IM_COL32(255, 255, 255,  40);
constexpr ImU32 kButtonHoverBg   = IM_COL32(255, 255, 255,  20);

constexpr ImU32 kBarTop          = IM_COL32( 86,  86,  86, 255);
constexpr ImU32 kBarMid          = IM_COL32(138, 138, 138, 255);
constexpr ImU32 kBarBot          = IM_COL32( 56,  56,  56, 255);
constexpr ImU32 kBarSelTop       = IM_COL32(148, 148, 148, 255);
constexpr ImU32 kBarSelMid       = IM_COL32(216, 216, 216, 255);
constexpr ImU32 kBarSelBot       = IM_COL32(102, 102, 102, 255);
constexpr ImU32 kBarDisTop       = IM_COL32( 50,  50,  50, 255);
constexpr ImU32 kBarDisMid       = IM_COL32( 72,  72,  72, 255);
constexpr ImU32 kBarDisBot       = IM_COL32( 34,  34,  34, 255);
constexpr ImU32 kBarEdgeLight    = IM_COL32(255, 255, 255, 175);
constexpr ImU32 kBarEdgeDark     = IM_COL32(  0,   0,   0, 205);
constexpr ImU32 kBarText         = IM_COL32(176, 176, 176, 255);
constexpr ImU32 kBarTextSel      = IM_COL32(255, 255, 255, 255);
constexpr ImU32 kBarTextDis      = IM_COL32(108, 108, 108, 255);
constexpr ImU32 kTextOutline     = IM_COL32(  0,   0,   0, 235);
constexpr ImU32 kBandFill        = IM_COL32(  0,   0,   0, 238);
constexpr ImU32 kBoxFill         = IM_COL32(  0,   0,   0, 238);
constexpr ImU32 kBoxBorder       = IM_COL32(230, 230, 230, 235);
constexpr float kBarH            = 26.0f;
constexpr float kBandH           = 30.0f;

constexpr float kPanelW          = 640.0f;
constexpr float kPanelH          = 480.0f;
constexpr float kPanelPadX       = 28.0f;
constexpr float kPanelPadY       = 6.0f;

constexpr float kRowHeight       = 22.0f;
constexpr float kRowPadX         = 18.0f;
constexpr float kSectionPadY     = 4.0f;
constexpr float kRuleInsetX      = 0.0f;

constexpr float kTitleRowHeight  = 30.0f;
constexpr float kTitleRuleGapY   = 0.0f;

constexpr float kTabBarHeight    = 28.0f;
constexpr float kTabGapX         = 18.0f;

constexpr float kSliderTrackH    = 4.0f;
constexpr float kSliderTrackW    = 160.0f;

constexpr const char* kDrillGlyph = "\xE2\x96\xB6";

inline ImVec2 PanelTopLeft() {
    return ImVec2(0.0f, 0.0f);
}

inline ImVec2 PanelBottomRight() {
    ImVec2 tl = PanelTopLeft();
    return ImVec2(tl.x + kPanelW, tl.y + kPanelH);
}

}
