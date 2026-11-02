#pragma once

#include <cstdint>

#include "../input/auto_action_motion_transaction.h"

namespace KaoriRecoilDuck {

AutoActionMotionSubmitResult Begin(int playerNum, int characterId,
                                   uint64_t* recipeGenerationOut = nullptr);

// Recipe-level result. Pending spans both native command transactions;
// Accepted is published only after EFZ enters exact move 251.
AutoActionMotionOutcome GetOutcome(int playerNum, uint64_t recipeGeneration);

// Called once per internal battle update; submits native 66 only while
// backdash 164 is in the verified frame-index 4/5 window.
void Tick();

bool IsActive(int playerNum);
void Cancel(int playerNum, const char* reason);
void CancelAll(const char* reason);

}
