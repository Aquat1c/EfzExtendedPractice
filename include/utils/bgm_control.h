#pragma once
#include <windows.h>
#include <string>

// DirectSound scale: 0 = max, -10000 = mute
bool SetBGMVolume(uintptr_t gameStatePtr, int volume);

bool MuteBGM(uintptr_t gameStatePtr);

bool UnmuteBGM(uintptr_t gameStatePtr);

bool StopBGM(uintptr_t gameStatePtr);

bool PlayBGM(uintptr_t gameStatePtr, unsigned short trackNumber);

// Active DirectSound buffer index, NOT the logical track number accepted by PlayBGM.
int GetBGMBufferIndex(uintptr_t gameStatePtr);

// Legacy name retained for existing buffer-level callers.
int GetBGMSlot(uintptr_t gameStatePtr);

int GetBGMVolume(uintptr_t gameStatePtr);

void LogBGMState(uintptr_t gameStatePtr);

// Suppression/toggle interfaces are ABI stubs only.
void SetBGMSuppressed(bool suppress); // no-op
bool IsBGMSuppressed();               // always false

// Last logical track observed at playBackgroundMusic; 0xFFFF = none observed.
unsigned short GetLastBgmTrack();
void SetLastBgmTrack(unsigned short track);
bool SetBGMVolumeViaGame(uintptr_t gameSystemPtr, int volumeLevel);
void StartBGMSuppressionPoller(); // no-op
void StopBGMSuppressionPoller();  // no-op
