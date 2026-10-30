#pragma once

namespace CustomMenu::Input {

// True only while EFZ is the foreground window. Direct key polling is gated by
// this so the custom menu cannot consume keys typed into another app.
bool IsGameWindowActive();

// Call once when the menu becomes visible: snapshots the current physical
// state of all tracked keys/buttons so already-held inputs do NOT register
// as a rising edge on the first frame.
void ResetEdges();

// While suspended every keyboard, pad and mouse query reads as idle. Used while
// the close animation plays.
void SetSuspended(bool suspended);
bool IsSuspended();

// Edge-detected navigation queries: each returns true exactly once per
// "press" (foreground-window-gated physical state sampled in input.cpp).
// Safe to query multiple times per frame - internally cached.

bool NavUp();
bool NavDown();
bool NavLeft();
bool NavRight();

// Activate / Accept: Enter, A/C (gameplay buttons), mouse left click on a
// focused row. Returns edge.
bool Activate();

// Back / Cancel: Escape, B (gamepad face-right). Returns edge.
bool Back();

// Switch active player column on the Values screen: EFZ D button / gamepad Y.
bool SwitchPlayer();

// Tab navigation - cycles top-level tabs. LB/RB on controller; PageUp/PageDown
// on keyboard.
bool TopTabPrev();
bool TopTabNext();

// Sub-tab navigation - cycles sub-tabs within the active top tab.
// LT/RT trigger bits on controller; '[' / ']' on keyboard.
bool SubTabPrev();
bool SubTabNext();

// Direct top-tab jump via the 1..9 number-row keys. Returns 0 if no edge this
// frame, or the 1-based index pressed (1..9). The caller decides whether the
// index is valid for the current top-tab count.
int  TopTabNumberEdge();

// Mouse
// Current mouse position in the 640x480 virtual canvas space; ImGui::
// GetMousePos() is authoritative (imgui_impl.cpp remaps the OS cursor during
// PreNewFrameInputs).
struct MousePos { float x, y; bool valid; };
MousePos GetMouse();
float MouseWheel();

// Single-shot hit test helpers (rising-edge button queries).
bool MouseHovering(float x, float y, float w, float h);
bool MouseClickedIn(float x, float y, float w, float h);      // left button
bool MouseRightClickedIn(float x, float y, float w, float h); // right button

// Edge query for the left mouse button anywhere (row-level hit tests do not
// fit variable-width labels like a tab bar).
bool MouseLeftEdge();

} // namespace CustomMenu::Input
