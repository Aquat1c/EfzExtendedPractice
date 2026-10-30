#pragma once
#include <windows.h>
#include <string>
#include <atomic>

struct FrameAdvantageState {
    bool p1InBlockstun;
    bool p2InBlockstun;
    bool p1InHitstun;
    bool p2InHitstun;
    bool p1Defending;
    bool p2Defending;

    bool p1Attacking;
    bool p2Attacking;
    int p1AttackStartInternalFrame;
    int p2AttackStartInternalFrame;
    short p1AttackMoveID;
    short p2AttackMoveID;

    int p1BlockstunStartInternalFrame;
    int p2BlockstunStartInternalFrame;
    int p1HitstunStartInternalFrame;
    int p2HitstunStartInternalFrame;

    int p1ActionableInternalFrame;
    int p2ActionableInternalFrame;
    int p1DefenderFreeInternalFrame;
    int p2DefenderFreeInternalFrame;

    double p1FrameAdvantage;
    double p2FrameAdvantage;
    bool p1AdvantageCalculated;
    bool p2AdvantageCalculated;

    // Gap: frames between consecutive hits in a string
    int p1GapFrames;
    int p2GapFrames;
    bool p1GapCalculated;
    bool p2GapCalculated;

    short p1InitialBlockstunMoveID;
    short p2InitialBlockstunMoveID;

    int displayUntilInternalFrame;
    int gapDisplayUntilInternalFrame;
};

extern FrameAdvantageState frameAdvState;
// When set to a future internal frame, regular FA overlay updates are suppressed
extern std::atomic<int> g_SkipRegularFAOverlayUntilFrame;

void ResetFrameAdvantageState();
// World/timeline boundary only, after prior measurement readers have drained.
void ResetFrameAdvantageTimeline();
void ClearFrameAdvantageDisplay();
void CancelFrameAdvantageCalculation();
void MonitorFrameAdvantage(short moveID1, short moveID2, short prevMoveID1, short prevMoveID2);
bool IsFrameAdvantageActive();
FrameAdvantageState GetFrameAdvantageState();
void ArmFrameAdvantageDisplayTimer();

// Engine ticks since entering the Practice battle: stops with the engine (pause,
// framestep, hitch) and is 0 outside Practice.
int GetEngineTick();
// Tick of this player's last non-actionable to actionable edge.
int GetActionableEdgeTick(int player);
// Practice boundary: drops the clock and everything timed on it. Mod state only, no
// game memory. Retries on the next call if the auto-action state lock is busy.
void DropEngineTickState();

int GetCurrentInternalFrame();
int GetDisplayDurationInternalFrames();
std::string FormatFrameAdvantage(int advantageInternal);
bool IsAttackMove(short moveID);
bool IsRecoveryFromAttack(short currentMoveID, short prevMoveID);
