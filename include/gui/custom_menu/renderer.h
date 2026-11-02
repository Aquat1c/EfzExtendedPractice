#pragma once

namespace CustomMenu {

// Call before ImGui::NewFrame(): this may rebuild the shared font atlas, and doing it before any
// draw-list commands are queued keeps font texture IDs stable for the whole frame.
void PrepareFrame();

void Render();

bool RequestAnimatedClose();

}
