#pragma once
#include <windows.h>
#include <cstdint>

uint8_t GetPlayerInputs(int playerNum);

bool SafeReadMemory(uintptr_t address, void* buffer, size_t size);
bool SafeWriteMemory(uintptr_t address, const void* data, size_t size);

uintptr_t ResolvePointer(uintptr_t base, uintptr_t baseOffset, uintptr_t offset);
void WriteGameMemory(uintptr_t address, const void* data, size_t size);
bool TrySetPlayerPosition(uintptr_t base, uintptr_t playerOffset, double x, double y,
                          bool updateMoveID = true);
void SetPlayerPosition(uintptr_t base, uintptr_t playerOffset, double x, double y, bool updateMoveID = true);
void UpdatePlayerValuesExceptRF(uintptr_t base, uintptr_t baseOffsetP1, uintptr_t baseOffsetP2);
void ApplyRFValues(double p1RF, double p2RF);
bool PatchMemory(uintptr_t address, const char* bytes, size_t length);
bool NopMemory(uintptr_t address, size_t length);
bool SetRFValuesDirect(double p1RF, double p2RF);
bool SetICColorDirect(bool p1BlueIC, bool p2BlueIC);
bool IsRFFreezeColorManaging(int player);

enum class EngineRegenMode {
	Unknown = 0,
	Normal,
	F5_FullOrPreset,   // A==1000/2000 or B==3332
	F4_FineTuneActive  // B==9999 and A stepping by 5, away from 1000/2000
};

bool ReadEngineRegenParams(uint16_t& outParamA, uint16_t& outParamB);

EngineRegenMode InferEngineRegenMode(uint16_t paramA, uint16_t paramB);

bool IsEngineRegenLikelyActive();
bool GetEngineRegenStatus(EngineRegenMode& outMode, uint16_t& outParamA, uint16_t& outParamB);
bool DebugScanRegenParamWindow(uintptr_t playerBase, uint32_t& outAOffset, uint16_t& outAVal, uint32_t& outBOffset, uint16_t& outBVal);
bool DeriveRfFromParamA(uint16_t paramA, float& rfValue, bool& isBlueIC);

bool WriteEngineRegenParams(uint16_t paramA, uint16_t paramB);
bool ForceEngineF5Preset(uint16_t presetA);
bool ForceEngineF5Full();
bool ForceEngineF4Value(uint16_t targetA);

void SavePlayerPositions(uintptr_t base);
void LoadPlayerPositions(uintptr_t base);

void StartRFFreeze(double p1Value, double p2Value);
void StartRFFreezeOne(int player, double value);
void StopRFFreeze();
void StopRFFreezePlayer(int player);
void SetRFFreezeNeutralOnly(bool enabled);
void UpdateRFFreezeTick();

bool SetICColorPlayer(int player, bool blueIC);

void SetRFFreezeColorDesired(int player, bool enabled, bool blueIC);

enum class RFFreezeOrigin {
	None = 0,
	ManualUI = 1,
	ContinuousRecovery = 2,
	Other = 3
};
void StartRFFreezeOneFromUI(int player, double value);
void StartRFFreezeOneFromCR(int player, double value);
bool GetRFFreezeStatus(int player, bool& isActive, double& value, bool& colorManaged, bool& colorBlue);
RFFreezeOrigin GetRFFreezeOrigin(int player);

#define INPUT_UP     0x01
#define INPUT_DOWN   0x02
#define INPUT_LEFT   0x04
#define INPUT_RIGHT  0x08
#define INPUT_A      0x10
#define INPUT_B      0x20
#define INPUT_C      0x40
#define INPUT_D      0x80

#define P1_INPUT_OFFSET 0xB8

bool GetPlayerFacingDirection(int playerNum);  // true = facing right
