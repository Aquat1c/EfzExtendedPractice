#pragma once

#include <windows.h>
#include <vector>
#include <string>
#include <atomic>
#include <thread>
#include "motion_constants.h"
#include "input_core.h"
#include "motion_system.h"

#define MOTION_INPUT_NEUTRAL 0x00
#define MOTION_INPUT_RIGHT   0x01
#define MOTION_INPUT_LEFT    0x02
#define MOTION_INPUT_DOWN    0x04
#define MOTION_INPUT_UP      0x08
#define MOTION_BUTTON_A      0x10
#define MOTION_BUTTON_B      0x20
#define MOTION_BUTTON_C      0x40
#define MOTION_BUTTON_D      0x80
#define MOTION_INPUT_BUTTON  (MOTION_BUTTON_A | MOTION_BUTTON_B | MOTION_BUTTON_C | MOTION_BUTTON_D)

#define MOTION_INPUT_DOWNRIGHT (MOTION_INPUT_DOWN | MOTION_INPUT_RIGHT)
#define MOTION_INPUT_DOWNLEFT  (MOTION_INPUT_DOWN | MOTION_INPUT_LEFT)
#define MOTION_INPUT_UPRIGHT   (MOTION_INPUT_UP | MOTION_INPUT_RIGHT)
#define MOTION_INPUT_UPLEFT    (MOTION_INPUT_UP | MOTION_INPUT_LEFT)

extern std::atomic<bool> g_forceHumanControlActive;
extern std::atomic<bool> g_manualInputOverride[3];
extern std::atomic<uint8_t> g_manualInputMask[3];

bool GetPlayerFacingDirection(int playerNum);
void SetAIControlFlag(int playerNum, bool human);
bool IsAIControlFlagHuman(int playerNum);
void RestoreAIControlIfNeeded(int playerNum);
void DumpInputBuffer(int playerNum);
std::string GetInputBufferVisualization(int playerNum, int window = 16);
bool WriteSequentialInputs(int playerNum, const std::vector<InputFrame>& frames);
bool InjectMotionToBuffer(int playerNum, const std::vector<uint8_t>& motionSequence, int offset = 0);
void LogNextBufferValue(int playerNum);

bool HoldUp(int playerNum);
bool HoldBackCrouch(int playerNum);
bool ReleaseInputs(int playerNum);
