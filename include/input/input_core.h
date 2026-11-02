#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include "../core/constants.h"

// Unified input mask, matching the game's buffer encoding. WritePlayerInputImmediate translates it
// into per-axis and per-button register values.
constexpr uint8_t GAME_INPUT_NEUTRAL = 0x00;
constexpr uint8_t GAME_INPUT_RIGHT = 0x01;
constexpr uint8_t GAME_INPUT_LEFT  = 0x02;
constexpr uint8_t GAME_INPUT_DOWN  = 0x04;
constexpr uint8_t GAME_INPUT_UP    = 0x08;
constexpr uint8_t GAME_INPUT_A     = 0x10;
constexpr uint8_t GAME_INPUT_B     = 0x20;
constexpr uint8_t GAME_INPUT_C     = 0x40;
constexpr uint8_t GAME_INPUT_D     = 0x80;

constexpr uintptr_t AI_CONTROL_FLAG_OFFSET = 0xA4;

const uint8_t GAME_INPUT_DOWNRIGHT = GAME_INPUT_DOWN | GAME_INPUT_RIGHT;
const uint8_t GAME_INPUT_DOWNLEFT = GAME_INPUT_DOWN | GAME_INPUT_LEFT;
const uint8_t GAME_INPUT_UPRIGHT = GAME_INPUT_UP | GAME_INPUT_RIGHT;
const uint8_t GAME_INPUT_UPLEFT = GAME_INPUT_UP | GAME_INPUT_LEFT;

// Only motion queues and buffer freezes write the circular buffer directly; writing normals or
// jumps there can misalign the buffer and drop presses.
constexpr uintptr_t INPUT_HORIZONTAL_OFFSET = HORIZONTAL_INPUT_OFFSET;
constexpr uintptr_t INPUT_VERTICAL_OFFSET   = VERTICAL_INPUT_OFFSET;
constexpr uintptr_t INPUT_BUTTON_A_OFFSET   = BUTTON_A_OFFSET;
constexpr uintptr_t INPUT_BUTTON_B_OFFSET   = BUTTON_B_OFFSET;
constexpr uintptr_t INPUT_BUTTON_C_OFFSET   = BUTTON_C_OFFSET;
constexpr uintptr_t INPUT_BUTTON_D_OFFSET   = BUTTON_D_OFFSET;
#define BUTTON_A    GAME_INPUT_A
#define BUTTON_B    GAME_INPUT_B
#define BUTTON_C    GAME_INPUT_C
#define BUTTON_D    GAME_INPUT_D

uintptr_t GetPlayerPointer(int playerNum);
std::string DecodeInputMask(uint8_t inputMask);
bool WritePlayerInput(int playerNum, uint8_t inputMask);
bool WritePlayerInputImmediate(int playerNum, uint8_t inputMask);
// Caller owns the captured fighter's lifetime and input producer boundary.
bool WritePlayerInputImmediateAt(uintptr_t capturedPlayer, uint8_t inputMask);
bool WritePlayerInputToBuffer(int playerNum, uint8_t inputMask);
bool ResetPlayerInputBufferIndex(int playerNum);
bool ClearPlayerInputBuffer(int playerNum, bool resetIndex);
void DumpInputBuffer(int playerNum, const std::string& context);
uint16_t GetPlayerMoveID(int playerNum);

bool ClearPlayerCommandFlags(int playerNum);
bool ClearPlayerCommandFlagsAt(uintptr_t capturedPlayer);

bool NeutralizeMotionToken(int playerNum);

bool FullCleanupAfterToggle(int playerNum);
// Caller owns and retains this exact fighter; no current-player resolution.
bool FullCleanupAfterToggleAt(uintptr_t capturedPlayer);
