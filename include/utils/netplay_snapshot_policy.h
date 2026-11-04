#pragma once

#include "network.h"
#include <cstddef>

using NetplayCopyStateFn = int (__cdecl*)(EFZNetplayState*, uint32_t, uint32_t);

inline bool TryCopyNetplaySnapshot(NetplayCopyStateFn copy, EFZNetplayState& output) {
    EFZNetplayState snapshot{};
    if (copy == nullptr || copy(&snapshot, sizeof(snapshot), 4u) == 0
        || snapshot.magic != EFZ_NETPLAY_STATE_MAGIC || snapshot.version < 4u
        || snapshot.structSize < offsetof(EFZNetplayState, stateSeq) + sizeof(uint32_t)) {
        return false;
    }
    output = snapshot;
    return true;
}

inline int NextConfirmedOfflinePollCount(OnlineState nativeState, int previousCount) {
    return nativeState == OnlineState::Offline ? (previousCount < 3 ? previousCount + 1 : 3) : 0;
}

inline bool HoldNetplayStateOnMissingSnapshot(
    bool coherentCopyAvailable, OnlineState nativeState, bool previousSuspend, int offlinePolls) {
    return (nativeState == OnlineState::Offline || nativeState == OnlineState::Unknown)
        && (coherentCopyAvailable || nativeState == OnlineState::Unknown
            || (previousSuspend && offlinePolls < 3));
}
