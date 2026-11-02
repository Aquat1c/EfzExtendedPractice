#pragma once
#include <string>

int GetSpecialMoveStrength(int actionType, int triggerType);
std::string GetTriggerName(int triggerType);
// strengthOverride: -1 = use per-trigger strength, otherwise explicit 0=A,1=B,2=C,3=D
int ConvertTriggerActionToMotion(int actionType, int triggerType, int strengthOverride = -1);