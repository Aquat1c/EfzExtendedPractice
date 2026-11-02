#pragma once
#include <windows.h>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <cstdint>
#include "input_core.h"

extern const uint16_t INPUT_BUFFER_SIZE;
extern const uintptr_t INPUT_BUFFER_OFFSET;
extern const uintptr_t INPUT_BUFFER_INDEX_OFFSET;

extern std::atomic<bool> g_bufferFreezingActive;
extern std::atomic<bool> g_indexFreezingActive;
extern std::vector<uint8_t> g_frozenBufferValues;
extern uint16_t g_frozenBufferStartIndex;
extern uint16_t g_frozenBufferLength;
extern uint16_t g_frozenIndexValue;
// Which player currently owns an active buffer-freeze session (0 = none)
extern std::atomic<int> g_activeFreezePlayer;

// Serializes lease ownership, freeze publication, and stop/reset. The owner joins the worker;
// stale generations cannot write or clean up a newer session.
extern std::recursive_mutex g_bufferFreezeControlMutex;

uint64_t StartBufferFreezeWorker(int playerNum);
bool CaptureAndFreezeBuffer(int playerNum, uint16_t startIndex, uint16_t length, int motionType = -1, int buttonMask = 0);
bool FreezeBufferIndex(int playerNum, uint16_t indexValue);
void StopBufferFreezing();
// Internal token-validated tutorial path; ordinary callers must use
// StopBufferFreezing(), which will not tear down a tutorial-owned freeze.
void StopBufferFreezingIgnoringTutorialLease();

struct EfzTmEntryV1;
bool BindBufferFreezeWorld(const EfzTmEntryV1& world);
void CancelBufferFreezeWork();
uint32_t RetireBufferFreezeWorld(const EfzTmEntryV1& heldWorld);
