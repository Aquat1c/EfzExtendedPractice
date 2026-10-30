#pragma once

namespace CustomMenu {

// Called before ImGui::NewFrame() when the custom menu may render. This is
// where shared ImGui device objects/font atlas may be rebuilt; doing it before
// any draw-list commands are queued keeps font texture IDs stable for the
// entire frame.
void PrepareFrame();

// Called once per EndScene frame from HookedEndScene (gated by
// Config::useCustomMenu). Queries ImGuiImpl::IsVisible() internally - only
// draws when the menu is open.
//
// Uses ImGui::GetBackgroundDrawList() on the 640x480 render target and does
// not call ImGui::Begin/End.
void Render();

// Asks the render thread to play the close animation. False when it can't run
// (animations off, menu not rendering) and the caller must hide the menu itself.
bool RequestAnimatedClose();

} // namespace CustomMenu
