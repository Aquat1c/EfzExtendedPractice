#include <windows.h>
#include <vector>
#include <cstdint>
#include <string>
#include <mutex>
#include <sstream>
#include "../../include/core/logger.h"
#include "../../include/game/final_memory_patch.h"
#include "../../include/game/final_memory_patch_owner.h"
#include "../../include/game/game_state.h"
#include "../../include/runtime/native_game_profile.h"
#include "../../include/utils/utilities.h"

namespace {
std::mutex g_fmMutex;
std::atomic<bool> g_fmBypassRequested{false};

FinalMemory::PatchOwner& Owner() {
    static auto* owner = new FinalMemory::PatchOwner(Practice::NativePatchLedger());
    return *owner;
}

// Exact 10-byte CMP instructions from the qualified Memorial image; the
// ledger verifies all ten bytes. Never adopt bypass-looking live bytes.
std::vector<Practice::PatchDescriptor> QualifiedSites(const Practice::PatchModule& module) {
    struct Site { uint32_t rva; uint8_t modrm; };
    static constexpr Site sites[] = {
        {0x232B6u, 0xB9u},
        {0x4696Cu, 0xB9u},
        {0x615CBu, 0xB9u},
        {0x77973u, 0xB8u},
        {0x8AE20u, 0xB8u},
        {0xAE45Eu, 0xBAu},
        {0xD2D3Cu, 0xBAu},
        {0x11A4BAu, 0xB9u},
        {0x146A56u, 0xB9u},
        {0x167DBAu, 0xB9u},
        {0x188806u, 0xBAu},
        {0x18AD30u, 0xB8u},
        {0x1AA55Au, 0xB9u},
        {0x1DE2C2u, 0xB9u},
        {0x1F725Au, 0xB9u},
        {0x2168F2u, 0xBAu},
        {0x2169EEu, 0xB9u},
        {0x239252u, 0xB9u},
        {0x253FF6u, 0xB9u},
        {0x276553u, 0xBAu},
        {0x2A64CFu, 0xBAu},
        {0x2E8642u, 0xB9u},
        {0x303EB2u, 0xB9u},
        {0x321F32u, 0xB9u},
        {0x33ECDAu, 0xB9u},
    };
    std::vector<Practice::PatchDescriptor> descriptors;
    descriptors.reserve(sizeof(sites) / sizeof(sites[0]));
    for (const auto& site : sites) {
        std::vector<uint8_t> before{0x81, site.modrm, 0x08, 0x01, 0, 0, 0x05, 0x0D, 0, 0};
        auto after = before;
        after[6] = 0x10; after[7] = 0x27; // 3333 -> 10000, only the owned immediate
        descriptors.push_back({module, module.base + site.rva, before, after, FinalMemory::kPatchOwner});
    }
    return descriptors;
}
}

int ApplyFinalMemoryHPBypass() {
    std::lock_guard<std::mutex> lock(g_fmMutex);
    // Gates under the mutex: an apply that loses it to ForceRestore must see the published boundary.
    // Raw screen too: the debounced phase still reads Match just after a battle exit.
    if (!g_featuresEnabled.load(std::memory_order_acquire) || !IsPracticeContext()
        || GetCurrentGamePhase() != GamePhase::Match || !IsInGameplayState()) {
        return 0;
    }
    Practice::PatchModule module{};
    if (!Practice::GetQualifiedMemorialImage(module)) {
        static bool reported = false;
        if (!reported) {
            reported = true;
            LogOut("[FM_PATCH] Game image does not match the qualified Memorial profile", true);
        }
        return 0;
    }
    const int applied = Owner().Apply(QualifiedSites(module));
    if (applied) LogOut("[FM_PATCH] Acquired FM compare sites: " + std::to_string(applied), true);
    return applied;
}

int RevertFinalMemoryHPBypass() {
    std::lock_guard<std::mutex> lock(g_fmMutex);
    const int restored = Owner().Restore();
    if (Owner().HasObligations())
        LogOut("[FM_PATCH] Restoration incomplete; owned sites/protections retained for retry", true);
    return restored;
}

int SetFinalMemoryBypass(bool enabled) {
    const bool previous = g_fmBypassRequested.exchange(enabled, std::memory_order_release);
    if (previous != enabled) {
        LogOut(
            std::string("[FM_PATCH] Request ")
            + (enabled ? "enabled" : "disabled")
            + " by local training runtime",
            true);
    }
    return SyncFinalMemoryBypassForCurrentMode("SetFinalMemoryBypass");
}

bool IsFinalMemoryBypassEnabled() {
    return g_fmBypassRequested.load(std::memory_order_acquire);
}

bool IsFinalMemoryBypassInstalled() {
    std::lock_guard<std::mutex> lock(g_fmMutex);
    return Owner().HasObligations();
}

int SyncFinalMemoryBypassForCurrentMode(const char* reason) {
    const bool requested = g_fmBypassRequested.load(std::memory_order_acquire);
    const bool featuresActive = g_featuresEnabled.load(std::memory_order_acquire);
    const bool onlineSuspended = g_onlineModeActive.load(std::memory_order_acquire);
    const GameMode currentMode = GetCurrentGameMode();
    const GamePhase currentPhase = GetCurrentGamePhase();
    const bool inPracticeMatch = IsPracticeContext() && (currentPhase == GamePhase::Match);
    const bool shouldInstall = requested && featuresActive && inPracticeMatch;

    if (shouldInstall) {
        const int applied = ApplyFinalMemoryHPBypass();
        if (applied > 0) {
            std::ostringstream oss;
            oss << "[FM_PATCH] Sync applied"
                << " requested=1 features=" << (featuresActive ? "1" : "0")
                << " online=" << (onlineSuspended ? "1" : "0")
                << " mode=" << GetGameModeName(currentMode)
                << " phase=" << static_cast<int>(currentPhase);
            if (reason && *reason) {
                oss << " reason=" << reason;
            }
            LogOut(oss.str(), true);
        }
        return applied;
    }

    const int reverted = RevertFinalMemoryHPBypass();
    if (requested && reverted == 0 && detailedLogging.load()) {
        std::ostringstream oss;
        oss << "[FM_PATCH] Request held but patch not installed"
            << " requested=1 features=" << (featuresActive ? "1" : "0")
            << " online=" << (onlineSuspended ? "1" : "0")
            << " mode=" << GetGameModeName(currentMode)
            << " phase=" << static_cast<int>(currentPhase);
        if (reason && *reason) {
            oss << " reason=" << reason;
        }
        LogOut(oss.str(), true);
    }
    if (!requested && reverted > 0) {
        std::ostringstream oss;
        oss << "[FM_PATCH] Sync restored original FM checks";
        if (reason && *reason) {
            oss << " reason=" << reason;
        }
        LogOut(oss.str(), true);
    }
    return reverted;
}

int ForceRestoreFinalMemoryHPBypass(const char* reason) {
    const int reverted = RevertFinalMemoryHPBypass();
    if (reverted > 0 || detailedLogging.load()) {
        std::ostringstream oss;
        oss << "[FM_PATCH] Forced restore"
            << " requested=" << (IsFinalMemoryBypassEnabled() ? "1" : "0")
            << " reverted=" << reverted;
        if (reason && *reason) {
            oss << " reason=" << reason;
        }
        LogOut(oss.str(), true);
    }
    return reverted;
}
