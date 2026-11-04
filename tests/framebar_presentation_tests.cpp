#include "gui/framebar_presentation.h"

#include <cstdio>

namespace {
int failures = 0;
void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}
}

int main() {
    using namespace FrameBar;
    using namespace FrameBar::Presentation;
    const auto empty = MakeView(0, 0, 8, 4, 1);
    Check(empty.cells == 0 && empty.samples == 0, "empty history has no displayed cells");

    const auto raw = MakeView(5, 5, 8, 4, 1);
    Check(raw.cells == 4 && raw.samples == 4 && raw.clipped, "raw view clips to visible cells");
    Check(raw.NewestSlot(0) == 1 && raw.NewestSlot(3) == 4, "raw cells inspect their own samples");

    const auto visual = MakeView(5, 5, 8, 4, 3);
    Check(visual.cells == 2 && visual.samples == 5 && !visual.clipped, "partial visual group stays visible");
    Check(visual.Begin(0) == 0 && visual.End(0) == 3 && visual.End(1) == 5,
          "visual grouping retains the short newest group");
    Check(visual.NewestSlot(0) == 2 && visual.NewestSlot(1) == 4,
          "inspection uses the newest raw sample of each visual cell");

    const auto wrapped = MakeView(11, 3, 8, 4, 3);
    Check(wrapped.samples == 8 && wrapped.cells == 3 && wrapped.clipped, "wrapped history retains only stored samples");
    Check(wrapped.Slot(0) == 3 && wrapped.NewestSlot(0) == 5 &&
          wrapped.NewestSlot(1) == 0 && wrapped.NewestSlot(2) == 2,
          "visual selection crosses the ring seam without selecting an overwritten slot");
    const auto clippedVisual = MakeView(11, 3, 8, 2, 3);
    Check(clippedVisual.samples == 6 && clippedVisual.NewestSlot(0) == 7 &&
          clippedVisual.NewestSlot(1) == 2, "visible clipping happens before grouping the wrapped ring");

    const Layout layout{80.0f, 360.0f, 480.0f, 9.0f, 2.0f, 80};
    const auto full = MakeView(240, 0, 240, 80, 3);
    Check(PickColumn(full, layout, 80.0f, 360.0f, 0, 0, 1) == 0, "top row begins at the first cell");
    Check(PickColumn(full, layout, 559.0f, 379.0f, 0, 0, 1) == 79, "bottom row reaches the final cell");
    Check(PickColumn(full, layout, 80.0f, 369.0f, 0, 0, 1) == -1, "row gap does not inspect");
    Check(PickColumn(full, layout, 80.0f, 380.0f, 0, 0, 1) == -1, "bottom boundary is outside");
    Check(PickColumn(full, layout, 560.0f, 360.0f, 0, 0, 1) == -1, "right boundary is outside");
    Check(PickColumn(visual, layout, 92.0f, 360.0f, 0, 0, 1) == -1, "unfilled columns do not inspect");
    Check(PickColumn(empty, layout, 80.0f, 360.0f, 0, 0, 1) == -1, "empty history cannot be hovered");
    Check(PickColumn(full, layout, 272.5f, 963.5f, 50, 30, 2.5f) == 1,
          "scaled and offset bottom-row coordinates pick the same column");
    Check(PickColumn(full, layout, 80.0f, 360.0f, 0, 0, 0) == -1, "zero scale is not pickable");

    struct RecordedCell { Cat cat; short moveID; };
    const RecordedCell cells[] = {
        {Cat::AttackStartup, 200}, {Cat::AttackStartup, 200},
        {Cat::AttackActive, 200}, {Cat::AttackActive, 201},
        {Cat::AttackActive, 201}, {Cat::AttackRecovery, 201}
    };
    Check(RunEnd(cells, 6, 0) == 2, "run ends when the category changes");
    Check(RunEnd(cells, 6, 2) == 3, "run ends when the move changes");
    Check(RunEnd(cells, 6, 3) == 5, "same state and move share a duration label");
    Check(RunEnd(cells, 6, 5) == 6, "last run stops at available history");
    if (failures == 0) std::puts("framebar_presentation_tests: all checks passed");
    return failures ? 1 : 0;
}
