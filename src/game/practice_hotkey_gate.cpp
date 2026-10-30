#include "utils/minhook_utils.h"
#include "../include/game/game_state.h"
#include "../../include/game/practice_hotkey_gate.h"
#include "../../include/game/practice_offsets.h"
#include "../../include/game/efzrevival_addrs.h" // version-aware RVAs
#include "../../include/game/collision_display.h"
#include "../../include/game/savestate_hook.h"
#include "../../include/game/mission/mission_engine.h"
#include "../../include/game/mission/mission_pause_menu.h"
#include "../../include/game/mission/tutorial_session.h"
#include "../../include/core/logger.h"
#include "../../include/core/constants.h"
#include "../../include/core/memory.h"
#include "../../include/input/framestep.h"
#include "../../include/utils/pause_integration.h"
#include "../../include/utils/network.h" // IsNetplaySuspendActive
#include "../../3rdparty/minhook/include/MinHook.h"
#include <windows.h>
#include <atomic>
#include <mutex>
#include <sstream>
#include <string>

static std::atomic<bool> s_menuVisibleForGate{false};
static bool Gate_IsMenuVisible() { return s_menuVisibleForGate.load(std::memory_order_relaxed); }

namespace {
    static std::string ToHex(uint32_t v){ std::ostringstream oss; oss<<std::hex<<v; return oss.str(); }
    // Original is __thiscall(this, int key); detoured as __fastcall with EDX dummy.
    // Return the full EAX: 1.02j consumes a pointer-sized value, legacy builds read AL only.
    using HotkeyEvalFn = uintptr_t (__fastcall*)(void* self, void* edxValue, int a2);
    HotkeyEvalFn oHotkeyEval = nullptr;
    std::atomic<bool> s_installed{false};
    std::atomic<uint64_t> s_suppressedFrames{0};
    uintptr_t s_evalAddr = 0;

    uintptr_t ScanForHotkeyEvaluator();

    enum class RunnerCheckpointKey : uint8_t {
        Other,
        SaveOrLoad,
        Unreadable,
    };

    RunnerCheckpointKey ClassifyRunnerCheckpointKey(void* self, int key) {
        if (!self || key == 0) return RunnerCheckpointKey::Other;
        const uintptr_t saveOffset = EFZ_Practice_SaveHotkeyOffset();
        const uintptr_t loadOffset = EFZ_Practice_LoadHotkeyOffset();
        if (!saveOffset || !loadOffset) {
            return RunnerCheckpointKey::Unreadable;
        }

        int saveKey = -1;
        int loadKey = -1;
        const uintptr_t practice = reinterpret_cast<uintptr_t>(self);
        if (!SafeReadMemory(practice + saveOffset, &saveKey, sizeof(saveKey)) ||
            !SafeReadMemory(practice + loadOffset, &loadKey, sizeof(loadKey))) {
            return RunnerCheckpointKey::Unreadable;
        }
        return key == saveKey || key == loadKey
            ? RunnerCheckpointKey::SaveOrLoad
            : RunnerCheckpointKey::Other;
    }

    uintptr_t __fastcall HookedHotkeyEval(void* self, void* edxValue, int a2) {
    auto hookExecution = MinHookUtils::EnterExecution(MinHookUtils::TicketFor<&HookedHotkeyEval>());
    if (!hookExecution.Admitted()) return oHotkeyEval ? oHotkeyEval(self, edxValue, a2) : 0;
        if (!PracticeBattleHooksOpen()) {
            // Netplay calls this on a Practice object it is about to delete; never gate it.
            return oHotkeyEval ? oHotkeyEval(self, edxValue, a2) : 0;
        }
        PauseIntegration::NotePracticeControllerCandidate(self, "PracticeDispatcher");
        if (Gate_IsMenuVisible() || Mission::PauseMenu::IsOpen() ||
            Mission::TutorialSession::IsActive() ||
            Mission::Engine::Demo::IsActive() ||
            Mission::Engine::Recorder::OwnsCaptureHotkeys() ||
            Mission::Engine::Recorder::IsMenuInputHandoffActive()) {
            s_suppressedFrames.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }
        if (Mission::Engine::Runner::IsActive()) {
            const RunnerCheckpointKey checkpointKey =
                ClassifyRunnerCheckpointKey(self, a2);
            if (checkpointKey != RunnerCheckpointKey::Other) {
                // The runner owns Revival's single Practice checkpoint; a manual save would silently replace it.
                s_suppressedFrames.fetch_add(1, std::memory_order_relaxed);
                return 0;
            }
        }
        if (Framestep::ShouldSuppressRevivalHotkey(self, a2)) {
            s_suppressedFrames.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }
        if (CollisionDisplay::ShouldSuppressRevivalHotkey(self, a2)) {
            s_suppressedFrames.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }
        const uint8_t inlineActions = SavestateHook::BeginInlinePracticeHotkey(self, a2);
        // Preserve EDX: J's MinGW body reads it; legacy __thiscall dispatchers ignore it.
        const uintptr_t result = oHotkeyEval ? oHotkeyEval(self, edxValue, a2) : 0;
        SavestateHook::EndInlinePracticeHotkey(inlineActions);
        return result;
    }

    uintptr_t ResolveHotkeyEvaluatorRva() {
        HMODULE mod = GetModuleHandleA("EfzRevival.dll");
        if (!mod) return 0;
        uintptr_t rva = EFZ_RVA_PracticeDispatcher();
        if (rva) {
            uintptr_t candidate = reinterpret_cast<uintptr_t>(mod) + rva;
            uint8_t firstBytes[5] = {0};
            if (SafeReadMemory(candidate, firstBytes, sizeof(firstBytes))) {
                return candidate;
            }
        }
        // Never guess a legacy RVA; a wrong fast path can overlap unrelated code in split 1.02f builds.
        return ScanForHotkeyEvaluator();
    }

    uintptr_t ScanForHotkeyEvaluator() {
        // TODO: implement a signature scan if the RVA drifts.
        return 0;
    }
}

namespace PracticeHotkeyGate {
    bool Install() {
        if (s_installed.load()) return true;
        HMODULE mod = GetModuleHandleA("EfzRevival.dll");
        if (!mod) {
            LogOut("[HOTKEY] EfzRevival not yet loaded; cannot install gate", true);
            return false;
        }
        const uintptr_t candidate=ResolveHotkeyEvaluatorRva();
        if (candidate!=s_evalAddr && MinHookUtils::HasOwnedTarget(reinterpret_cast<void*>(s_evalAddr))) return false;
        s_evalAddr=candidate;
        if (!s_evalAddr) {
            LogOut("[HOTKEY] Failed to resolve Practice hotkey evaluator; gate inactive", true);
            return false;
        }
        if (!MinHookUtils::CreateHook(reinterpret_cast<LPVOID>(s_evalAddr), reinterpret_cast<LPVOID>(&HookedHotkeyEval), reinterpret_cast<void**>(&oHotkeyEval), "[HOTKEY]", "evaluator", nullptr, &MinHookUtils::TicketFor<&HookedHotkeyEval>())) {
            LogOut("[HOTKEY] CreateHook failed for evaluator", true);
            return false;
        }
        if (!MinHookUtils::EnableHook(reinterpret_cast<LPVOID>(s_evalAddr), "[HOTKEY]", "evaluator")) {
            LogOut("[HOTKEY] EnableHook failed for evaluator", true);
            MinHookUtils::RemoveHook(reinterpret_cast<LPVOID>(s_evalAddr), "[HOTKEY]", "evaluator");
            return false;
        }
        s_installed.store(true);
        {
            std::ostringstream oss; oss << "[HOTKEY] Practice hotkey gate installed at RVA=0x" 
                << std::hex << static_cast<uint32_t>(s_evalAddr - reinterpret_cast<uintptr_t>(mod));
            LogOut(oss.str(), true);
        }
        return true;
    }

    void Uninstall() {
        if (!s_installed.load() && !MinHookUtils::HasOwnedTarget(reinterpret_cast<void*>(s_evalAddr))) return;
        if (s_evalAddr) {
            MinHookUtils::DisableHook(reinterpret_cast<LPVOID>(s_evalAddr), "[HOTKEY]", "evaluator");
            MinHookUtils::RemoveHook(reinterpret_cast<LPVOID>(s_evalAddr), "[HOTKEY]", "evaluator");
        }
        s_installed.store(false);
    }

    uint64_t GetSuppressedFrameCount() { return s_suppressedFrames.load(); }
    void NotifyMenuVisibility(bool visible) { s_menuVisibleForGate.store(visible, std::memory_order_relaxed); }
}
