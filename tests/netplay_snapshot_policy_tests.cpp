#include "utils/netplay_snapshot_policy.h"

#include <cstdio>
#include <cstring>

namespace {
int __cdecl FailCopy(EFZNetplayState* output, uint32_t, uint32_t) {
    output->magic = EFZ_NETPLAY_STATE_MAGIC;
    output->activityPhase = EFZ_ACTIVITY_HOST_IDLE;
    return 0;
}
int __cdecl InvalidCopy(EFZNetplayState* output, uint32_t, uint32_t) {
    output->magic = EFZ_NETPLAY_STATE_MAGIC;
    output->version = 7;
    output->structSize = 12;
    return 1;
}
int __cdecl GoodCopy(EFZNetplayState* output, uint32_t size, uint32_t minimumVersion) {
    if (size != sizeof(*output) || minimumVersion != 4) return 0;
    output->magic = EFZ_NETPLAY_STATE_MAGIC;
    output->version = 7;
    output->structSize = sizeof(*output);
    output->activityPhase = EFZ_ACTIVITY_MATCH;
    return 1;
}
}

int main() {
    int failures = 0;
    const auto expect = [&](bool condition, const char* message) {
        if (!condition) { std::fprintf(stderr, "%s\n", message); ++failures; }
    };
    EFZNetplayState state;
    std::memset(&state, 0xA5, sizeof(state));
    const EFZNetplayState unchanged = state;
    expect(!TryCopyNetplaySnapshot(nullptr, state), "older producer has no trusted snapshot");
    expect(!TryCopyNetplaySnapshot(FailCopy, state), "failed copy never accepts a partial host-idle state");
    expect(!TryCopyNetplaySnapshot(InvalidCopy, state), "truncated successful copy rejected");
    expect(std::memcmp(&state, &unchanged, sizeof(state)) == 0, "failed copy leaves previous state untouched");
    expect(TryCopyNetplaySnapshot(GoodCopy, state) && state.activityPhase == EFZ_ACTIVITY_MATCH, "valid caller-owned copy accepted");
    expect(HoldNetplayStateOnMissingSnapshot(true, OnlineState::Offline, true, 100), "failed coherent producer cannot reopen suspended training");
    expect(!HoldNetplayStateOnMissingSnapshot(true, OnlineState::Netplay, false, 0), "native online entrance takes effect immediately after failed copy");
    expect(!HoldNetplayStateOnMissingSnapshot(false, OnlineState::Netplay, false, 0), "older producer native online entrance takes effect immediately");
    expect(HoldNetplayStateOnMissingSnapshot(false, OnlineState::Offline, true, 1), "first older producer offline poll holds suspension");
    expect(HoldNetplayStateOnMissingSnapshot(false, OnlineState::Offline, true, 2), "second older producer offline poll holds suspension");
    expect(!HoldNetplayStateOnMissingSnapshot(false, OnlineState::Offline, true, 3), "confirmed older producer offline state releases suspension");
    expect(!HoldNetplayStateOnMissingSnapshot(false, OnlineState::Offline, false, 1), "older producer presence alone leaves offline training usable");
    expect(HoldNetplayStateOnMissingSnapshot(false, OnlineState::Unknown, false, 0), "unknown native observation must suspend training at startup");
    expect(HoldNetplayStateOnMissingSnapshot(false, OnlineState::Unknown, true, 100), "unknown native observation never releases suspension");
    int offlinePolls = NextConfirmedOfflinePollCount(OnlineState::Offline, 0);
    expect(offlinePolls == 1, "first confirmed offline observation counted");
    offlinePolls = NextConfirmedOfflinePollCount(OnlineState::Unknown, offlinePolls);
    expect(offlinePolls == 0, "unknown observation resets confirmed offline streak");
    offlinePolls = NextConfirmedOfflinePollCount(OnlineState::Offline, offlinePolls);
    offlinePolls = NextConfirmedOfflinePollCount(OnlineState::Offline, offlinePolls);
    expect(HoldNetplayStateOnMissingSnapshot(false, OnlineState::Offline, true, offlinePolls), "two confirmed offline observations after unknown retain suspension");
    offlinePolls = NextConfirmedOfflinePollCount(OnlineState::Offline, offlinePolls);
    expect(!HoldNetplayStateOnMissingSnapshot(false, OnlineState::Offline, true, offlinePolls), "third confirmed offline observation releases suspension");
    expect(NextConfirmedOfflinePollCount(OnlineState::Netplay, offlinePolls) == 0, "online observation resets offline streak");
    expect(NextConfirmedOfflinePollCount(OnlineState::Offline, offlinePolls) == 3, "offline streak stays bounded");
    return failures == 0 ? 0 : 1;
}
