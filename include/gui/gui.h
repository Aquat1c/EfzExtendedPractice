#pragma once
#include "../utils/utilities.h"

void OpenMenu();

// Single chokepoint for every live opener, so the gameplay-context gate lives here.
// Pass allowOutOfContext=true only from a pause surface that owns the screen during setup/restore.
bool OpenPracticeMenuDirect(bool allowOutOfContext = false);
