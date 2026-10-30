#include <windows.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
#include "../../include/core/constants.h"
#include "../../include/core/logger.h"
#include "../../include/game/game_state.h"
#include "../../include/game/timer_freeze_patch.h"
#include "../../include/runtime/native_game_profile.h"
#include "../../include/runtime/patch_ledger.h"
#include "../../include/utils/utilities.h"

namespace {

// Memorial efz.exe is linked at 0x00400000 (relocs stripped); descriptors are module.base + (VA - image base).
constexpr uintptr_t kMemorialImageBase = 0x00400000u;

// Distinct owner keys per timer; FinalMemory uses 0x464D0001 ("FM").
constexpr uint64_t kOwnerKeys[TimerFreeze::kTimerCount] = {
    0x414B0001u, // Akiko curse timer
    0x414B0002u, // Akiko time-slow odometer
    0x4E470001u, // Nagamori Final Memory timer
};

const char* const kTimerNames[TimerFreeze::kTimerCount] = {
    "AkikoCurse",
    "AkikoTimeslow",
    "MizukaFinalMemory",
};

// Same contract as FinalMemory::PatchOwner, keyed by owner.
class KeyedPatchOwner {
public:
    KeyedPatchOwner(Practice::PatchLedger& ledger, uint64_t owner) : ledger_(ledger), owner_(owner) {}
    int Apply(const std::vector<Practice::PatchDescriptor>& descriptors) {
        if (active_) return 0;
        if (ledger_.ObligationsForOwner(owner_)) return 0;
        if (descriptors.empty()) return 0;
        try {
            for (const auto& descriptor : descriptors) {
                if (descriptor.owner != owner_) {
                    ledger_.RestoreOwner(owner_);
                    return 0;
                }
                uint64_t site = 0;
                if (!ledger_.Acquire(descriptor, site).complete) {
                    ledger_.RestoreOwner(owner_);
                    return 0;
                }
            }
        } catch (...) {
            ledger_.RestoreOwner(owner_);
            return 0;
        }
        active_ = true;
        return static_cast<int>(descriptors.size());
    }
    int Restore() {
        active_ = false;
        const auto before = ledger_.ObligationsForOwner(owner_);
        const auto result = ledger_.RestoreOwner(owner_);
        return static_cast<int>(before - result.obligations);
    }
    bool HasObligations() const { return ledger_.ObligationsForOwner(owner_) != 0; }
private:
    Practice::PatchLedger& ledger_;
    uint64_t owner_;
    bool active_ = false;
};

std::mutex g_mutex;
std::atomic<bool> g_requested[TimerFreeze::kTimerCount][2] = {
    {false, false}, {false, false}, {false, false}
};

KeyedPatchOwner& Owner(TimerFreeze::Timer timer) {
    static KeyedPatchOwner* owners[TimerFreeze::kTimerCount] = {nullptr, nullptr, nullptr};
    if (!owners[timer]) owners[timer] = new KeyedPatchOwner(Practice::NativePatchLedger(), kOwnerKeys[timer]);
    return *owners[timer];
}

Practice::PatchDescriptor Site(const Practice::PatchModule& module, TimerFreeze::Timer timer,
                               uintptr_t va, std::vector<uint8_t> before, std::vector<uint8_t> after) {
    return Practice::PatchDescriptor{module, module.base + (va - kMemorialImageBase), before, after, kOwnerKeys[timer]};
}

// Each site is followed by an explicit CMP and rewrites the unchanged value, so substitution is flag-neutral.
std::vector<Practice::PatchDescriptor> QualifiedSites(const Practice::PatchModule& module, TimerFreeze::Timer timer) {
    std::vector<Practice::PatchDescriptor> descriptors;
    switch (timer) {
    case TimerFreeze::AkikoCurse:
        // sub_7256B0: mov edx,[ecx+3148h] / SUB EDX,1 / mov eax,[ebp-4] /
        // mov [eax+3148h],edx / cmp [ecx+3148h],0 / jg ...  ->  sub edx,0
        descriptors.push_back(Site(module, timer, AKIKO_CURSE_TIMER_DECREMENT_VA,
                                   {0x83, 0xEA, 0x01}, {0x83, 0xEA, 0x00}));
        break;
    case TimerFreeze::AkikoTimeslow:
        // sub_7256B0: mov ecx,4 / sub ecx,[eax+160h] / mov edx,[ebp-4] /
        // ADD ECX,[EDX+3154h] / mov eax,[ebp-4] / mov [eax+3154h],ecx /
        // cmp [ecx+3160h],0 ...  ->  mov ecx,[edx+3154h]. The ones digit never
        // moves, so tens/hundreds/thousands never carry and the wind-down
        // (level 4 at 0x00725A89) never arms; the freeze pulses keep running.
        descriptors.push_back(Site(module, timer, AKIKO_TIMESLOW_ODOMETER_ADD_VA,
                                   {0x03, 0x8A, 0x54, 0x31, 0x00, 0x00},
                                   {0x8B, 0x8A, 0x54, 0x31, 0x00, 0x00}));
        break;
    case TimerFreeze::MizukaFinalMemory:
        // entity 417 tick: mov edx,[ecx+3150h] / SUB EDX,1 / mov eax,[ebp-5Ch] /
        // mov [eax+3150h],edx / ... / cmp [ecx+3150h],0 / jne  ->  sub edx,0
        descriptors.push_back(Site(module, timer, NAGAMORI_FM_TIMER_DECREMENT_VA,
                                   {0x83, 0xEA, 0x01}, {0x83, 0xEA, 0x00}));
        break;
    default:
        break;
    }
    return descriptors;
}

bool AnyRequested(TimerFreeze::Timer timer) {
    return g_requested[timer][0].load(std::memory_order_acquire)
        || g_requested[timer][1].load(std::memory_order_acquire);
}

// Caller holds g_mutex.
int ApplyLocked(TimerFreeze::Timer timer) {
    Practice::PatchModule module{};
    if (!Practice::GetQualifiedMemorialImage(module)) {
        static bool reported = false;
        if (!reported) {
            reported = true;
            LogOut("[TIMER_FREEZE] Game image does not match the qualified Memorial profile", true);
        }
        return 0;
    }
    const int applied = Owner(timer).Apply(QualifiedSites(module, timer));
    if (applied) {
        LogOut(std::string("[TIMER_FREEZE] Installed ") + kTimerNames[timer]
               + " freeze sites=" + std::to_string(applied), true);
    }
    return applied;
}

// Caller holds g_mutex.
int RestoreLocked(TimerFreeze::Timer timer) {
    const int restored = Owner(timer).Restore();
    if (Owner(timer).HasObligations()) {
        LogOut(std::string("[TIMER_FREEZE] ") + kTimerNames[timer]
               + " restoration incomplete; owned sites retained for retry", true);
    }
    return restored;
}

} // namespace

namespace TimerFreeze {

void SetRequested(Timer timer, int player, bool enabled) {
    if (timer < 0 || timer >= kTimerCount) return;
    if (player != 1 && player != 2) return;
    const bool before = AnyRequested(timer);
    g_requested[timer][player - 1].store(enabled, std::memory_order_release);
    const bool after = AnyRequested(timer);
    if (before != after) {
        LogOut(std::string("[TIMER_FREEZE] ") + kTimerNames[timer] + " request "
               + (after ? "enabled" : "disabled") + " by local training runtime", true);
        SyncForCurrentMode("SetRequested");
    }
}

bool IsRequested(Timer timer) {
    if (timer < 0 || timer >= kTimerCount) return false;
    return AnyRequested(timer);
}

bool IsInstalled(Timer timer) {
    if (timer < 0 || timer >= kTimerCount) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    return Owner(timer).HasObligations();
}

int SyncForCurrentMode(const char* reason) {
    // Gates under the mutex: a sync that loses it to ForceRestore must see the published boundary.
    std::lock_guard<std::mutex> lock(g_mutex);
    const bool featuresActive = g_featuresEnabled.load(std::memory_order_acquire);
    const bool practiceContext = IsPracticeContext();
    const GameMode currentMode = GetCurrentGameMode();
    const GamePhase currentPhase = GetCurrentGamePhase();
    // Raw screen too: the debounced phase still reads Match just after a battle exit.
    const bool gatesOpen = featuresActive && practiceContext
        && (currentPhase == GamePhase::Match) && IsInGameplayState();

    int changes = 0;
    for (int i = 0; i < kTimerCount; ++i) {
        const Timer timer = static_cast<Timer>(i);
        const bool requested = AnyRequested(timer);
        if (requested && gatesOpen) {
            const int applied = ApplyLocked(timer);
            if (applied > 0) {
                std::ostringstream oss;
                oss << "[TIMER_FREEZE] Sync applied " << kTimerNames[timer]
                    << " features=" << (featuresActive ? "1" : "0")
                    << " practice=" << (practiceContext ? "1" : "0")
                    << " mode=" << GetGameModeName(currentMode)
                    << " phase=" << static_cast<int>(currentPhase);
                if (reason && *reason) oss << " reason=" << reason;
                LogOut(oss.str(), true);
            }
            changes += applied;
            continue;
        }
        const int reverted = RestoreLocked(timer);
        if (reverted > 0) {
            std::ostringstream oss;
            oss << "[TIMER_FREEZE] Sync restored " << kTimerNames[timer]
                << " requested=" << (requested ? "1" : "0")
                << " features=" << (featuresActive ? "1" : "0")
                << " practice=" << (practiceContext ? "1" : "0")
                << " mode=" << GetGameModeName(currentMode)
                << " phase=" << static_cast<int>(currentPhase);
            if (reason && *reason) oss << " reason=" << reason;
            LogOut(oss.str(), true);
        } else if (requested && detailedLogging.load()) {
            std::ostringstream oss;
            oss << "[TIMER_FREEZE] Request held but " << kTimerNames[timer] << " not installed"
                << " features=" << (featuresActive ? "1" : "0")
                << " practice=" << (practiceContext ? "1" : "0")
                << " mode=" << GetGameModeName(currentMode)
                << " phase=" << static_cast<int>(currentPhase);
            if (reason && *reason) oss << " reason=" << reason;
            LogOut(oss.str(), true);
        }
        changes += reverted;
    }
    return changes;
}

int ForceRestore(const char* reason) {
    int reverted = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (int i = 0; i < kTimerCount; ++i) reverted += RestoreLocked(static_cast<Timer>(i));
    }
    if (reverted > 0 || detailedLogging.load()) {
        std::ostringstream oss;
        oss << "[TIMER_FREEZE] Forced restore"
            << " requested=" << (IsRequested(AkikoCurse) ? "1" : "0")
            << (IsRequested(AkikoTimeslow) ? "1" : "0")
            << (IsRequested(MizukaFinalMemory) ? "1" : "0")
            << " reverted=" << reverted;
        if (reason && *reason) oss << " reason=" << reason;
        LogOut(oss.str(), true);
    }
    return reverted;
}

} // namespace TimerFreeze
