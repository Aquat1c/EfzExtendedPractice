#pragma once

struct ImFont;

namespace CustomMenu::Fonts {

bool Rebuild(float uiScale);

// Clears every non-owning ImFont pointer and cached layout tied to the current atlas.
// Call immediately before any path invokes io.Fonts->Clear(), including rebuilds outside this menu.
void InvalidateAtlasReferences();

ImFont* Body();
ImFont* Header();

// Rasterized at about twice the logical draw size: always pass an explicit logical pixel size
// to AddText and CalcTextSizeA.
ImFont* TutorialReadable();
ImFont* TutorialHeader();

bool IsLoaded();

}
