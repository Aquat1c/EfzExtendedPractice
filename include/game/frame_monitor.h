#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include "../include/core/constants.h"
#include "../include/game/game_state.h"

struct FrameSnapshot {
    unsigned long long tickMs;
    GamePhase phase;
    GameMode mode;
    short p1Move;
    short p2Move;
    short prevP1Move;
    short prevP2Move;
    bool p2BlockEdge;
    bool p2HitstunEdge;
    double p1X;
    double p2X;
    double p1Y;
    double p2Y;
    int p1Hp;
    int p2Hp;
    int p1Meter;
    int p2Meter;
    double p1RF;
    double p2RF;
    int p1CharId;
    int p2CharId;
};

bool TryGetLatestSnapshot(FrameSnapshot &out, unsigned int maxAgeMs);

enum MonitorState {
    Idle,
    Monitoring,
    RGMonitoring,
    SuperflashMonitoring
};

extern MonitorState state;

extern bool p1InAirHitstun;
extern bool p2InAirHitstun;
extern int p1LastHitstunFrame;
extern int p2LastHitstunFrame;

struct TriggerDelayState;
extern TriggerDelayState p1DelayState;
extern TriggerDelayState p2DelayState;

void ApplyAirtechPatches();
void RemoveAirtechPatches();
void ApplyAirtech(uintptr_t moveIDAddr, int playerNum, int frameNum);
void MonitorAutoAirtech(short moveID1, short moveID2);
void MonitorAutoJump();

void FrameDataMonitor();

struct FrameStepDebugInfo {
    bool active;
    int steps;   // step advances since the FA start edge while paused; -1 if inactive
};

FrameStepDebugInfo GetFrameStepDebugInfo();

extern short initialBlockstunMoveID;
bool IsBlockstunState(short moveID);
int GetAttackLevel(short blockstunMoveID);
bool IsDashState(short moveID);

enum class AutoActionApplyResult : uint8_t;
extern short GetActionMoveID(int actionType, int triggerType, int playerNum);
AutoActionApplyResult ApplyAutoAction(int playerNum, uintptr_t moveIDAddr,
                                      short currentMoveID, short prevMoveID,
                                      uint64_t* motionGenerationOut,
                                      uint64_t* normalGenerationOut,
                                      uint64_t* recipeGenerationOut);
extern bool p1ActionApplied;
extern bool p2ActionApplied;

void ProcessTriggerDelays();
void ProcessTriggerDelays(short moveID1, short moveID2, short prevMoveID1, short prevMoveID2);

void MonitorAutoActions();
void ResetActionFlags();
void ClearDelayStatesIfNonActionable();
void ClearDelayStatesIfNonActionable(short moveID1, short moveID2, short prevMoveID1, short prevMoveID2, const char* source);
void UpdateTriggerOverlay();
bool CheckAndHandleInvalidGameState(GameMode currentMode);
void ReinitializeOverlays();

bool AreCharactersInitialized();
bool IsValidGameMode(GameMode mode);
void UpdateStatsDisplay();

// Requires closed monitor work and restored input/feature ownership.
void ResetFrameMonitorTimeline();
