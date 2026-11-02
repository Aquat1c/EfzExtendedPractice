#pragma once

#include <cstdint>

enum class ScopedInputOwner : uint8_t {
    None = 0,
    AutoActionCharge = 1,
    KaoriRecoilDucking = 2,
    WakeForcedNeutral = 3,
    WakeMacroCleanup = 4,
};

// Claims future mod-owned input for one fighter; new input producers must wait for release.
bool TryReserveScopedInput(int playerNum, ScopedInputOwner owner,
                           uint64_t& tokenOut);
bool ScopedInputReservationMatches(int playerNum, ScopedInputOwner owner,
                                   uint64_t token);
bool IsScopedInputReserved(int playerNum);
bool IsScopedInputReservedByOther(int playerNum, uint64_t allowedToken = 0);
void ReleaseScopedInputReservation(int playerNum, ScopedInputOwner owner,
                                   uint64_t token);
void ReleaseAllScopedInputReservations(ScopedInputOwner owner);
