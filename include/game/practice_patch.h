#pragma once
#include <cstdint>
#include <string>

std::string FormatHexAddress(uintptr_t address);

bool EnablePlayer2InPracticeMode();

bool DisablePlayer2InPracticeMode();

void MonitorAndPatchPracticeMode();

void DumpPracticeModeState();

void LogPlayerInputsInPracticeMode();

void ScanForPotentialAIFlags();

void ResetP2Character();

void EnsureDefaultControlFlagsOnMatchStart();

// Auto-Block toggle (game state +4936); 'reason' is logged whenever the flag changes.
bool SetPracticeAutoBlockEnabled(bool enabled, const char* reason = nullptr);
bool GetPracticeAutoBlockEnabled(bool &enabledOut);
uint64_t GetPracticeAutoBlockWriteGeneration();
bool RestorePracticeAutoBlockIfUnchanged(bool enabled,
                                         uint64_t expectedGeneration,
                                         bool expectedCurrent,
                                         const char* reason = nullptr);

bool SyncAutoBlockModeFromGameFlag(bool clearOverride);

// Block Mode (game state +4934): 0=stand, 1=first, 2=crouch
bool SetPracticeBlockMode(int mode /*0..2*/);
bool GetPracticeBlockMode(int &modeOut);
bool CyclePracticeBlockMode();

enum DummyAutoBlockMode : int {
	DAB_None = 0,
	DAB_All = 1,
	DAB_FirstHitThenOff = 2,
	DAB_EnableAfterFirstHit = 3,
	DAB_Adaptive = 4
};

void SetDummyAutoBlockMode(int mode);
int  GetDummyAutoBlockMode();
void ResetDummyAutoBlockState();
void MonitorDummyAutoBlock(short p1MoveID, short p2MoveID, short prevP1MoveID, short prevP2MoveID);
struct PerFrameSample;
void MonitorDummyAutoBlock(const PerFrameSample& sample);

void SetAdaptiveStanceEnabled(bool enabled);
bool GetAdaptiveStanceEnabled();

bool GetCurrentDesiredAutoBlockOn(bool &onOut);

// An external controller (Random Block) owns the +4936 write, so MonitorDummyAutoBlock skips its
// own write and native-F7 sync; held for Random Block's whole enabled lifetime.
void SetExternalAutoBlockController(bool enabled);

bool AcquireTutorialAutoBlockController(uint64_t& tokenOut);
void ReleaseTutorialAutoBlockController(uint64_t token);
