#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// The two 1.02j packages share a version and Practice ABI but are different compilations.
// Keep this identity below EfzRevivalVersion so persisted savestates identify both as J.
enum class EfzRevival102jBuild : int {
    Unknown = 0,
    Original20260620,
    Updated20260802,
};

struct EfzRevival102jProfile {
    EfzRevival102jBuild build;
    uint32_t peTimestamp;
    uint32_t sizeOfImage;

    uint32_t roleRva;
    uint32_t sessionPtrRva;
    // Indexed by the native J role value: rollback, spectator, practice, compact/tournament.
    std::array<uint32_t, 4> roleVtableRvas;

    uint32_t practiceVtableRva;
    std::array<uint32_t, 10> practiceVtableSlots;

    uint32_t patchTogglerRva;
    uint32_t patchContextRva;
    uint32_t togglePauseRva;
    uint32_t practiceStepRenderRva;
    uint32_t practiceMainTickRva;
    uint32_t practiceDispatcherRva;
    uint32_t loadStateRva;
    uint32_t saveStateRva;
    uint32_t renderContextGlobalRva;
};

constexpr EfzRevival102jProfile kEfzRevival102jOriginal = {
    EfzRevival102jBuild::Original20260620,
    0x6A36A6AEu,
    0x001D9000u,
    0x0014EC40u,
    0x0014E980u,
    {{0x0016FEF0u, 0x0016FF20u, 0x0016FF80u, 0x0016FEB0u}},
    0x0016FF80u,
    {{
        0x00080650u, 0x00080630u, 0x0007DE40u, 0x0007E140u,
        0x0007CF60u, 0x0007DBC0u, 0x00064680u, 0x0007CCC0u,
        0x0007CDA0u, 0x0007D780u,
    }},
    0x00077F40u,
    0x0014E8C0u,
    0x0007DB60u,
    0x0007D6B0u,
    0x0007E140u,
    0x0007CF60u,
    0x0007E040u,
    0x0007E0F0u,
    0x0014E8D8u,
};

constexpr EfzRevival102jProfile kEfzRevival102jUpdated = {
    EfzRevival102jBuild::Updated20260802,
    0x6A6F0F7Bu,
    0x001D8000u,
    0x0014EC40u,
    0x0014E980u,
    {{0x0016FBD0u, 0x0016FC00u, 0x0016FC60u, 0x0016FB90u}},
    0x0016FC60u,
    {{
        0x00081890u, 0x00081870u, 0x0007F080u, 0x0007F380u,
        0x0007E1A0u, 0x0007EE00u, 0x00065580u, 0x0007DCD0u,
        0x0007DEB0u, 0x0007E9C0u,
    }},
    0x00078D00u,
    0x0014E8C0u,
    0x0007EDA0u,
    0x0007E8F0u,
    0x0007F380u,
    0x0007E1A0u,
    0x0007F280u,
    0x0007F330u,
    0u,
};

constexpr const EfzRevival102jProfile* FindEfzRevival102jProfileByBuild(
    EfzRevival102jBuild build) noexcept {
    return build == EfzRevival102jBuild::Original20260620
        ? &kEfzRevival102jOriginal
        : build == EfzRevival102jBuild::Updated20260802
            ? &kEfzRevival102jUpdated
            : nullptr;
}

constexpr const EfzRevival102jProfile* FindEfzRevival102jProfileByPeIdentity(
    uint32_t timestamp,
    uint32_t sizeOfImage) noexcept {
    return timestamp == kEfzRevival102jOriginal.peTimestamp
            && sizeOfImage == kEfzRevival102jOriginal.sizeOfImage
        ? &kEfzRevival102jOriginal
        : timestamp == kEfzRevival102jUpdated.peTimestamp
            && sizeOfImage == kEfzRevival102jUpdated.sizeOfImage
            ? &kEfzRevival102jUpdated
            : nullptr;
}
