#pragma once

#include <windows.h>
#include <vector>
#include <atomic>
#include <thread>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cstdint>
#include <string_view>

#include "../include/input/input_motion.h"
#include "../include/core/memory.h"
#include "../include/core/logger.h"
#include "../include/core/constants.h"
#include "../include/utils/utilities.h"

extern std::atomic<bool> g_bufferFreezingActive;
extern std::atomic<bool> g_indexFreezingActive;
extern std::thread g_bufferFreezeThread;
extern std::vector<uint8_t> g_frozenBufferValues;
extern uint16_t g_frozenBufferStartIndex;
extern uint16_t g_frozenBufferLength;
extern uint16_t g_frozenIndexValue;

extern const uint16_t INPUT_BUFFER_SIZE;
extern const uintptr_t INPUT_BUFFER_OFFSET;
extern const uintptr_t INPUT_BUFFER_INDEX_OFFSET;

uint64_t StartBufferFreezeWorker(int playerNum);
bool FreezeBufferIndex(int playerNum, uint16_t indexValue);
void StopBufferFreezing();

bool FreezePerfectDragonPunch(int playerNum);

bool FreezeBufferForMotion(int playerNum, int motionType, int buttonMask, int optimalIndex = -1);

// Exclusive tutorial lease: acquisition fails rather than stopping an existing freeze. Other
// starts are rejected while held; stop/release affect only the matching token.
bool AcquireTutorialBufferFreeze(uint64_t& tokenOut);
bool TutorialBufferFreezeLeaseActive();
bool FreezeBufferForTutorial(uint64_t token, int playerNum, int motionType,
                             int buttonMask, int optimalIndex = -1);
void StopTutorialBufferFreeze(uint64_t token);
void ReleaseTutorialBufferFreeze(uint64_t token);

// Pattern is already-encoded GAME_INPUT_* bytes from buffer index 0; the index freezes on the last
// element so the terminating button press stays resident for recognition.
bool FreezeBufferWithPattern(int playerNum, const std::vector<uint8_t>& pattern);
bool FreezeBufferWithPattern(int playerNum, const std::vector<uint8_t>& pattern, int extraNeutralFrames);

std::string GetInputBufferVisualization(int playerNum, int window);

void BeginBufferFreezeSession(int playerNum, std::string_view label);
void EndBufferFreezeSession(int playerNum, const char* reason, bool clearGlobals = true);

void ClearPlayerInputBuffer(int playerNum);
