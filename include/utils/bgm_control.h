#pragma once
#include <cstdint>

inline constexpr char kBgmTrackObserverOwner[] = "[BGM][TRACK]";

bool InstallBgmTrackObserver();

bool StopBGM(uintptr_t gameStatePtr);

bool PlayBGM(uintptr_t gameStatePtr, unsigned short trackNumber);

// Active DirectSound buffer index, NOT the logical track number accepted by PlayBGM.
int GetBGMBufferIndex(uintptr_t gameStatePtr);

int GetBGMVolume(uintptr_t gameStatePtr);

// Last logical track observed at playBackgroundMusic; 0xFFFF = none observed.
unsigned short GetLastBgmTrack();
void SetLastBgmTrack(unsigned short track);
