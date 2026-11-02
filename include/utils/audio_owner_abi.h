#pragma once
#include <cstdint>

// Retain the table's provider for callback lifetime. Every callback and direct transform uses this
// recursive boundary; no IO while held. Negotiation commits only after the prior transform drains.
constexpr uint32_t EfzAudioLaneBgm = 1, EfzAudioLaneSe = 2, EfzAudioAllLanes = 3;
struct EfzAudioOwnerV1 {
    uint32_t size = sizeof(EfzAudioOwnerV1);
    uint32_t version = 1;
    uint64_t providerIncarnation = 0;
    uint32_t ownedLaneMask = 0;
    uint32_t inputsAreRawLaneMask = EfzAudioAllLanes;
    uint64_t acknowledgementGeneration = 0;
};
using EfzAudioOwnerCommitV1 = void (__cdecl*)(const EfzAudioOwnerV1*, void*);
struct EfzAudioOwnerApiV1 {
    uint32_t size;
    uint32_t version;
    uint64_t providerIncarnation;
    void (__cdecl* enter)();
    void (__cdecl* leave)();
    bool (__cdecl* query)(EfzAudioOwnerV1*);
    // Expected incarnation+generation must match exactly. The provider retires
    // requested lanes, acknowledges, and calls commit before unlocking.
    bool (__cdecl* retire)(const EfzAudioOwnerV1*, uint32_t lanes,
                          EfzAudioOwnerCommitV1 commit, void* context);
};
using EfzGetAudioOwnerApiV1 = const EfzAudioOwnerApiV1* (__cdecl*)();
