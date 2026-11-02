#pragma once

namespace CustomMenu::Input {

// Direct key polling is gated by this so the menu cannot consume keys typed into another app.
bool IsGameWindowActive();

// Call once when the menu becomes visible, so inputs already held do not read as a new press.
void ResetEdges();

void SetSuspended(bool suspended);
bool IsSuspended();

// Each returns true once per press. Safe to query several times per frame; results are cached.

bool NavUp();
bool NavDown();
bool NavLeft();
bool NavRight();

bool Activate();

bool Back();

bool SwitchPlayer();

bool TopTabPrev();
bool TopTabNext();

bool SubTabPrev();
bool SubTabNext();

// Returns 0 with no edge this frame, otherwise the 1-based digit pressed (1..9).
int  TopTabNumberEdge();

// Mouse position in the 640x480 virtual canvas; imgui_impl.cpp remaps the OS cursor.
struct MousePos { float x, y; bool valid; };
MousePos GetMouse();
float MouseWheel();

bool MouseHovering(float x, float y, float w, float h);
bool MouseClickedIn(float x, float y, float w, float h);
bool MouseRightClickedIn(float x, float y, float w, float h);

bool MouseLeftEdge();

}
