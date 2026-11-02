#pragma once
#include <cstdint>

namespace SwitchPlayers {
    bool ToggleLocalSide();
    // Force set local side to 0 (P1) or 1 (P2)
    bool SetLocalSide(int sideIdx);
    bool ReapplyLocalSide(int sideIdx);
    bool RestoreEngineControlState(int sideIdx, uint8_t p1CpuFlag, uint8_t p2CpuFlag, bool armInputCleanup = true);
    // Current local side (0=P1, 1=P2); -1 if unable to read
    int GetLocalSide();
    int GetLocalPlayerIndex();
    int GetRemotePlayerIndex();
    bool ResetControlMappingForMenusToP1();
    void ClearSwapFlag();
    void MarkSwapped();
}
