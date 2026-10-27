#pragma once

struct ImFont;

namespace CustomMenu::Fonts {

// Load the embedded ITC Bolt font at two sizes into the current
// ImGui atlas (AddFontFromMemoryTTF, pixel-snap, no oversampling, matching
// EFZ's pixel-aligned bitmap aesthetic). Returns false if the embedded
// resource is missing; the caller falls back to ImGui's default font.
// The menu renders in the fixed 640x480 canvas, so only the user scale
// participates; window/DPI presentation scale is handled elsewhere.
bool Rebuild(float uiScale);

// Clear every non-owning ImFont pointer and cached layout that refers to the
// current ImGui font atlas. Call this immediately before any path invokes
// io.Fonts->Clear(), including atlas rebuilds outside the custom-menu renderer.
void InvalidateAtlasReferences();

// Access the loaded faces. Returns nullptr before Rebuild() succeeds.
// Body is the ~11px row font; Header is the ~16px section-header font.
ImFont* Body();
ImFont* Header();

// Tutorial-only high-density faces, rasterized at roughly twice the logical
// draw size: always pass an explicit logical pixel size to AddText/
// CalcTextSizeA.  TutorialReadable follows the user's UI-font preference;
// TutorialHeader uses the embedded ITC Bolt face.
ImFont* TutorialReadable();
ImFont* TutorialHeader();

// Returns true if the two menu faces and both tutorial faces are loaded.
bool IsLoaded();

} // namespace CustomMenu::Fonts
