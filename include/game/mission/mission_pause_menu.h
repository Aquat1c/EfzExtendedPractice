#pragma once
// Input is polled on the frame-monitor thread (Tick); rendering is per-EndScene (Draw).
#include <string>

struct ImDrawList;

namespace Mission::PauseMenu {

bool IsOpen();

bool Open();
void Close();

void Toggle();

void NotifyRecorderEnteredReview();

void Tick();

// render-thread
bool WantsDraw();
void Draw(ImDrawList* dl);

}
