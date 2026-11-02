#pragma once
#include <windows.h>

struct TriggerDelayState;
extern TriggerDelayState p1DelayState;
extern TriggerDelayState p2DelayState;
extern bool p1ActionApplied;
extern bool p2ActionApplied;

void ApplyJump(uintptr_t moveIDAddr, int playerNum, int jumpType);
void MonitorAutoJump();
bool IsAutoActionActiveForPlayer(int playerNum); 
void AutoJumpReleaseForPlayer(int playerNum);