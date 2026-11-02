#pragma once
#include <cstdint>
#include <atomic>
#include <thread>
#include <vector>

extern const uint16_t INPUT_BUFFER_SIZE;
extern const uintptr_t INPUT_BUFFER_OFFSET;
extern const uintptr_t INPUT_BUFFER_INDEX_OFFSET;

extern std::atomic<bool> g_bufferFreezingActive;
extern std::atomic<bool> g_indexFreezingActive;
extern std::vector<uint8_t> g_frozenBufferValues;
extern uint16_t g_frozenBufferStartIndex;
extern uint16_t g_frozenBufferLength;
extern uint16_t g_frozenIndexValue;