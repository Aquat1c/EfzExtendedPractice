#pragma once

#include "framebar.h"

#include <algorithm>

namespace FrameBar::Presentation {

struct View {
    int samples = 0;
    int cells = 0;
    int stride = 1;
    int firstSlot = 0;
    int capacity = 1;
    bool clipped = false;

    int Begin(int column) const { return column * stride; }
    int End(int column) const { return (std::min)(Begin(column) + stride, samples); }
    int Slot(int sample) const { return (firstSlot + sample) % capacity; }
    int NewestSlot(int column) const { return Slot(End(column) - 1); }
};

inline View MakeView(int filled, int write, int capacity, int visible, int stride) {
    View view;
    view.samples = (std::min)((std::min)(filled, capacity), visible * stride);
    view.cells = (view.samples + stride - 1) / stride;
    view.stride = stride;
    view.capacity = capacity;
    view.firstSlot = (write - view.samples + capacity) % capacity;
    view.clipped = filled > view.samples;
    return view;
}

struct Layout {
    float x, y, width, rowHeight, rowGap;
    int columns;
};

inline int PickColumn(const View& view, const Layout& layout, float mouseX, float mouseY,
                      float originX, float originY, float scale) {
    if (view.cells == 0 || !(scale > 0.0f)) return -1;
    const float x = (mouseX - originX) / scale - layout.x;
    const float y = (mouseY - originY) / scale - layout.y;
    const float pitch = layout.rowHeight + layout.rowGap;
    const float cellWidth = layout.width / layout.columns;
    if (!(x >= 0.0f && x < view.cells * cellWidth)) return -1;
    if (!((y >= 0.0f && y < layout.rowHeight) ||
          (y >= pitch && y < pitch + layout.rowHeight))) return -1;
    return static_cast<int>(x / cellWidth);
}

template<class Cell>
int RunEnd(const Cell* cells, int count, int first) {
    int end = first + 1;
    while (end < count && cells[end].cat == cells[first].cat &&
           cells[end].moveID == cells[first].moveID) ++end;
    return end;
}

}
