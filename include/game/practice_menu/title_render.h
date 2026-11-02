#pragma once
// Draws from a private surface so the title's own title_ob surface stays untouched for
// InGameNetplay. Coordinates are native 320x240.

#include <cstdint>

namespace PracticeMenu::Render {

// The enum index is both the sheet row and the screen row, so this order must match the labels
// baked into assets/title_ob2.dat.
enum Row : int {
    ROW_PRACTICE = 0,
    ROW_MISSION,
    ROW_TUTORIAL,
    ROW_COUNT
};

bool EnsureLoaded(uint32_t screenContext);

void Release(uint32_t screenContext);

bool IsLoaded();

// Must be called on every submenu entry: leaving restores the vanilla palette, so a re-entry
// without re-applying renders garbled glyphs.
void ApplyPalette(uint32_t screenContext);

void DrawBackground(uint32_t screenContext);

void DrawRows(uint32_t screenContext, int selectedRow, int slideOffsetX);

}
