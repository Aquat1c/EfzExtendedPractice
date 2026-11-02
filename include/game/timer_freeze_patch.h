// Character timer freezes as ledger-owned code patches: each no-ops one instruction of the tick.
// Installed only in a local Practice match; force-restored on disable, netplay suspend and detach.
#pragma once

namespace TimerFreeze {

enum Timer {
    AkikoCurse        = 0,
    AkikoTimeslow     = 1,
    MizukaFinalMemory = 2,
    kTimerCount       = 3
};

// player names the side whose menu row was toggled. The patch is code-level, so the live
// request is P1 OR P2; per-side state only lets each menu clear its own toggle.
void SetRequested(Timer timer, int player, bool enabled);

bool IsRequested(Timer timer);

bool IsInstalled(Timer timer);

int SyncForCurrentMode(const char* reason = nullptr);

// Restores every owned site regardless of requests (suspend, disable,
// shutdown); requests are preserved for the next Sync.
int ForceRestore(const char* reason = nullptr);

}
