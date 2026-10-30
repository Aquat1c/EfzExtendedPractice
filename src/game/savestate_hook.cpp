#include "../../include/game/savestate_hook.h"
#include "../include/game/game_state.h"
#include "../../include/game/efzrevival_addrs.h"
#include "../../include/game/auto_action.h"
#include "../../include/game/character_settings.h"
#include "../../include/game/combo_overlay.h"
#include "../../include/game/macro_controller.h"
#include "../../include/game/mission/mission_engine.h"
#include "../../include/game/mission/tutorial_session.h"   // TutorialSession::IsActive (suppress toasts in lessons)
#include "../../include/game/practice_offsets.h"
#include "../../include/game/practice_hotkey_gate.h"
#include "../../include/input/framestep.h"   // load must reconcile the mod-owned pause
#include "../../include/utils/switch_players.h"
#include "../../include/utils/pause_integration.h"
#include "../../include/core/logger.h"
#include "../../include/core/memory.h"
#include "../../include/core/constants.h"
#include "../../include/gui/overlay.h"
#include "../../include/utils/minhook_utils.h"
#include "../../include/utils/network.h"
#include "../../include/utils/utilities.h"
#include "../../3rdparty/minhook/include/MinHook.h"
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <sstream>

namespace {
    static bool s_loadStartedFromOwnedPause = false;
    std::atomic<bool> s_installed{false};
    std::atomic<unsigned int> s_saveCount{0};
    std::atomic<unsigned int> s_loadCount{0};
    std::atomic<bool> s_inlineDispatcherMode{false};

    uintptr_t s_loadStateAddr = 0;
    uintptr_t s_saveStateAddr = 0;

    using LoadStateFn = bool (__thiscall*)(void* self);
    LoadStateFn oLoadState = nullptr;

    using SaveStateFn = void (__thiscall*)(void* self);
    SaveStateFn oSaveState = nullptr;

    struct SavedModState {
        bool valid = false;
        bool p2ControlWasOverridden = false;
        uint32_t originalP2ControlFlag = 1;
        uint8_t p1CpuFlag = 0;
        uint8_t p2CpuFlag = 1;
        int localSide = 0;
        MacroController::State macroState = MacroController::State::Idle;
        int macroSlot = 1;
    };

    SavedModState s_savedModState{};

    constexpr uint8_t kInlineSaveAction = 1u << 0;
    constexpr uint8_t kInlineLoadAction = 1u << 1;

    // J's dispatcher reaches its save/load branches only in this state
    // (sub_7007CF60: screen 3, [0x790110]+1084 in {2,3}, battle +45 != 2).
    bool InlineSaveLoadBranchReachable() {
        const uintptr_t base = GetEFZBase();
        if (!base) return false;
        uint32_t screen = 0;
        uintptr_t titleContext = 0;
        uintptr_t battleContext = 0;
        uint8_t mode = 0;
        uint8_t exitState = 2;
        return SafeReadMemory(base + 0x390148, &screen, sizeof(screen)) && screen == 3
            && SafeReadMemory(base + 0x390110, &titleContext, sizeof(titleContext)) && titleContext
            && SafeReadMemory(titleContext + 1084, &mode, sizeof(mode)) && (mode == 2 || mode == 3)
            && SafeReadMemory(base + 0x390110 + 4 * screen, &battleContext, sizeof(battleContext)) && battleContext
            && SafeReadMemory(battleContext + 45, &exitState, sizeof(exitState)) && exitState != 2;
    }

    void CanonicalizeSavedControlState(SavedModState& state) {
        if (state.localSide != 0 && state.localSide != 1) {
            state.localSide = 0;
        }
        state.p1CpuFlag = static_cast<uint8_t>((state.localSide == 1) ? 1u : 0u);
        state.p2CpuFlag = static_cast<uint8_t>((state.localSide == 1) ? 0u : 1u);
    }

    void CaptureModState() {
        s_savedModState.valid = true;
        s_savedModState.p2ControlWasOverridden = g_p2ControlOverridden;
        s_savedModState.originalP2ControlFlag = g_originalP2ControlFlag;

        s_savedModState.localSide = SwitchPlayers::GetLocalSide();
        if (s_savedModState.localSide < 0) {
            s_savedModState.localSide = 0;
        }

        uintptr_t base = GetEFZBase();
        if (base) {
            uintptr_t gameStatePtr = 0;
            if (SafeReadMemory(base + EFZ_BASE_OFFSET_GAME_STATE, &gameStatePtr, sizeof(gameStatePtr)) && gameStatePtr) {
                SafeReadMemory(gameStatePtr + GAMESTATE_OFF_P1_CPU_FLAG, &s_savedModState.p1CpuFlag, sizeof(uint8_t));
                SafeReadMemory(gameStatePtr + GAMESTATE_OFF_P2_CPU_FLAG, &s_savedModState.p2CpuFlag, sizeof(uint8_t));
            }
        }
        CanonicalizeSavedControlState(s_savedModState);

        s_savedModState.macroState = MacroController::GetState();
        s_savedModState.macroSlot = MacroController::GetCurrentSlot();

        std::ostringstream oss;
        oss << "[SAVESTATE][REVIVAL] Captured mod state: p2Override=" << (s_savedModState.p2ControlWasOverridden ? "true" : "false")
            << " origP2Flag=" << s_savedModState.originalP2ControlFlag
            << " localSide=" << s_savedModState.localSide
            << " p1Cpu=" << static_cast<int>(s_savedModState.p1CpuFlag)
            << " p2Cpu=" << static_cast<int>(s_savedModState.p2CpuFlag)
            << " macroState=" << static_cast<int>(s_savedModState.macroState)
            << " macroSlot=" << s_savedModState.macroSlot;
        LogOut(oss.str(), true);
    }

    void RestoreModState() {
        if (!s_savedModState.valid) {
            LogOut("[SAVESTATE][REVIVAL] No valid mod state to restore", true);
            return;
        }

        CanonicalizeSavedControlState(s_savedModState);
        g_p2ControlOverridden = s_savedModState.p2ControlWasOverridden;
        g_originalP2ControlFlag = s_savedModState.originalP2ControlFlag;

        int currentSide = SwitchPlayers::GetLocalSide();
        if (s_savedModState.localSide >= 0) {
            LogOut("[SAVESTATE][REVIVAL] Restoring local side from " + std::to_string(currentSide) + " to " + std::to_string(s_savedModState.localSide), true);
            if (!SwitchPlayers::ReapplyLocalSide(s_savedModState.localSide)
                && !SwitchPlayers::RestoreEngineControlState(s_savedModState.localSide,
                                                             s_savedModState.p1CpuFlag,
                                                             s_savedModState.p2CpuFlag)) {
                if (s_savedModState.localSide == 1) {
                    SwitchPlayers::MarkSwapped();
                } else {
                    SwitchPlayers::ClearSwapFlag();
                }
            }
        }

        uintptr_t base = GetEFZBase();
        if (base) {
            uintptr_t gameStatePtr = 0;
            if (SafeReadMemory(base + EFZ_BASE_OFFSET_GAME_STATE, &gameStatePtr, sizeof(gameStatePtr)) && gameStatePtr) {
                const uint8_t activePlayer = static_cast<uint8_t>(s_savedModState.localSide);
                SafeWriteMemory(gameStatePtr + GAMESTATE_OFF_ACTIVE_PLAYER, &activePlayer, sizeof(activePlayer));
                SafeWriteMemory(gameStatePtr + GAMESTATE_OFF_P1_CPU_FLAG, &s_savedModState.p1CpuFlag, sizeof(uint8_t));
                SafeWriteMemory(gameStatePtr + GAMESTATE_OFF_P2_CPU_FLAG, &s_savedModState.p2CpuFlag, sizeof(uint8_t));
            }
        }

        std::ostringstream oss;
        oss << "[SAVESTATE][REVIVAL] Restored mod state: p2Override=" << (g_p2ControlOverridden ? "true" : "false")
            << " origP2Flag=" << g_originalP2ControlFlag
            << " localSide=" << s_savedModState.localSide
            << " p1Cpu=" << static_cast<int>(s_savedModState.p1CpuFlag)
            << " p2Cpu=" << static_cast<int>(s_savedModState.p2CpuFlag);
        LogOut(oss.str(), true);
    }

    void BeginTrackedLoad() {
        LogOut("[SAVESTATE][REVIVAL] === LOAD STATE BEGIN ===", true);
        CancelAutoActionsAndMacros();
        // Revival's load-init restores +1400 and runs its own unfreeze toggler; a load must clear our framestep pause (covers the 1.02j inline path and Revival's own F-key).
        s_loadStartedFromOwnedPause = Framestep::OwnsPauseState();
        Framestep::CancelActiveState("revival savestate load");
    }

    void FinishTrackedLoad(bool result) {
        s_loadCount.fetch_add(1, std::memory_order_relaxed);
        RestoreModState();
        Framestep::FinishSavestateRestore(s_loadStartedFromOwnedPause);
        s_loadStartedFromOwnedPause = false;
        ComboOverlay::ResetState("revival savestate load");
        Mission::Engine::NotifyStateLoaded();
        if (uintptr_t base = GetEFZBase()) {
            CharacterSettings::TickCharacterEnforcements(base, displayData);
        }

        LogOut("[SAVESTATE][REVIVAL] === LOAD STATE END (result=" + std::string(result ? "true" : "false") + ") ===", true);
        // No toasts while missions/tutorials own their checkpoint churn.
        if (!::Mission::Engine::Runner::IsActive() &&
            !::Mission::TutorialSession::IsActive() &&
            !::Mission::Engine::Recorder::IsSessionActive() &&
            !::Mission::Engine::Demo::IsActive())
            DirectDrawHook::AddMessage("Revival State Loaded", "savestate", RGB(100, 255, 100), 1500, 0, 100);
    }

    void BeginTrackedSave() {
        LogOut("[SAVESTATE][REVIVAL] === SAVE STATE BEGIN ===", true);
        CaptureModState();
    }

    void FinishTrackedSave() {
        s_saveCount.fetch_add(1, std::memory_order_relaxed);
        LogOut("[SAVESTATE][REVIVAL] === SAVE STATE END ===", true);
        if (!::Mission::Engine::Runner::IsActive() &&
            !::Mission::TutorialSession::IsActive() &&
            !::Mission::Engine::Recorder::IsSessionActive() &&
            !::Mission::Engine::Demo::IsActive())
            DirectDrawHook::AddMessage("Revival State Saved", "savestate", RGB(255, 255, 100), 1500, 0, 100);
    }

    bool __fastcall HookedLoadState(void* self, void* /*edx*/) {
        auto execution=MinHookUtils::EnterExecution(MinHookUtils::TicketFor<&HookedLoadState>());
        if (execution && !execution.Admitted()) return oLoadState ? oLoadState(self) : false;
        if (!PracticeBattleHooksOpen()) {
            return oLoadState ? oLoadState(self) : false;
        }

        BeginTrackedLoad();
        bool result = oLoadState ? oLoadState(self) : false;
        FinishTrackedLoad(result);
        return result;
    }

    void __fastcall HookedSaveState(void* self, void* /*edx*/) {
        auto execution=MinHookUtils::EnterExecution(MinHookUtils::TicketFor<&HookedSaveState>());
        if (execution && !execution.Admitted()) {if (oSaveState) oSaveState(self);return;}
        if (!PracticeBattleHooksOpen()) {
            if (oSaveState) {
                oSaveState(self);
            }
            return;
        }

        BeginTrackedSave();
        if (oSaveState) {
            oSaveState(self);
        }
        FinishTrackedSave();
    }
}

namespace SavestateHook {
    bool Install() {
        if (s_installed.load()) return true;

        HMODULE mod = GetModuleHandleA("EfzRevival.dll");
        if (!mod) {
            LogOut("[SAVESTATE][REVIVAL] EfzRevival.dll not loaded; cannot install hooks", true);
            return false;
        }

        uintptr_t loadRva = EFZ_RVA_LoadState();
        uintptr_t saveRva = EFZ_RVA_SaveState();

        if (!loadRva || !saveRva) {
            LogOut("[SAVESTATE][REVIVAL] Unsupported EfzRevival version; hooks not available", true);
            return false;
        }

        uintptr_t base = reinterpret_cast<uintptr_t>(mod);
        if ((s_loadStateAddr!=base+loadRva && MinHookUtils::HasOwnedTarget(reinterpret_cast<void*>(s_loadStateAddr))) ||
            (s_saveStateAddr!=base+saveRva && MinHookUtils::HasOwnedTarget(reinterpret_cast<void*>(s_saveStateAddr)))) return false;
        s_loadStateAddr = base + loadRva;
        s_saveStateAddr = base + saveRva;

        if (GetEfzRevivalVersion() == EfzRevivalVersion::Revival102j) {
            // 1.02j keeps callable bodies for explicit commands; its hotkey dispatcher inlines save/load (bracketed via Begin/EndInlinePracticeHotkey).
            if (!PracticeHotkeyGate::Install()) {
                s_loadStateAddr = 0;
                s_saveStateAddr = 0;
                LogOut("[SAVESTATE][REVIVAL] J dispatcher integration requires the verified hotkey gate", true);
                return false;
            }

            oLoadState = reinterpret_cast<LoadStateFn>(s_loadStateAddr);
            oSaveState = reinterpret_cast<SaveStateFn>(s_saveStateAddr);
            s_inlineDispatcherMode.store(true, std::memory_order_release);
            s_installed.store(true, std::memory_order_release);

            std::ostringstream oss;
            oss << "[SAVESTATE][REVIVAL] J dispatcher integration ready"
                << " - callable Load RVA=0x" << std::hex << loadRva
                << " Save RVA=0x" << saveRva;
            LogOut(oss.str(), true);
            return true;
        }

        bool loadHookOk = false;
        bool saveHookOk = false;

        loadHookOk = MinHookUtils::CreateAndEnableHook(reinterpret_cast<LPVOID>(s_loadStateAddr),
                                                       reinterpret_cast<LPVOID>(&HookedLoadState),
                                                       reinterpret_cast<void**>(&oLoadState),
                                                       "[SAVESTATE][REVIVAL]",
                                                       "LoadState", nullptr, nullptr, &MinHookUtils::TicketFor<&HookedLoadState>());

        saveHookOk = MinHookUtils::CreateAndEnableHook(reinterpret_cast<LPVOID>(s_saveStateAddr),
                                                       reinterpret_cast<LPVOID>(&HookedSaveState),
                                                       reinterpret_cast<void**>(&oSaveState),
                                                       "[SAVESTATE][REVIVAL]",
                                                       "SaveState", nullptr, nullptr, &MinHookUtils::TicketFor<&HookedSaveState>());

        if (!loadHookOk && !saveHookOk) {
            LogOut("[SAVESTATE][REVIVAL] Failed to install any hooks", true);
            return false;
        }

        s_installed.store(true);
        std::ostringstream oss;
        oss << "[SAVESTATE][REVIVAL] Hooks installed - Load: " << (loadHookOk ? "OK" : "FAILED")
            << " (RVA=0x" << std::hex << loadRva << ")"
            << ", Save: " << (saveHookOk ? "OK" : "FAILED")
            << " (RVA=0x" << std::hex << saveRva << ")";
        LogOut(oss.str(), true);
        return true;
    }

    void Uninstall() {
        if (!s_installed.load() &&
            !MinHookUtils::HasOwnedTarget(reinterpret_cast<void*>(s_loadStateAddr)) &&
            !MinHookUtils::HasOwnedTarget(reinterpret_cast<void*>(s_saveStateAddr))) return;

        if (s_inlineDispatcherMode.exchange(false, std::memory_order_acq_rel)) {
            s_loadStateAddr = 0;
            s_saveStateAddr = 0;
            oLoadState = nullptr;
            oSaveState = nullptr;
            s_installed.store(false);
            LogOut("[SAVESTATE][REVIVAL] J dispatcher integration uninstalled", true);
            return;
        }

        if (s_loadStateAddr) {
            (void)MinHookUtils::DisableHook(reinterpret_cast<LPVOID>(s_loadStateAddr), "[SAVESTATE][REVIVAL]", "LoadState");
            (void)MinHookUtils::RemoveHook(reinterpret_cast<LPVOID>(s_loadStateAddr), "[SAVESTATE][REVIVAL]", "LoadState");
            if (PracticeHooks::ReleaseRetiredOriginal(MinHookUtils::OwnedHooks(), s_loadStateAddr, oLoadState)) s_loadStateAddr=0;
        }

        if (s_saveStateAddr) {
            (void)MinHookUtils::DisableHook(reinterpret_cast<LPVOID>(s_saveStateAddr), "[SAVESTATE][REVIVAL]", "SaveState");
            (void)MinHookUtils::RemoveHook(reinterpret_cast<LPVOID>(s_saveStateAddr), "[SAVESTATE][REVIVAL]", "SaveState");
            if (PracticeHooks::ReleaseRetiredOriginal(MinHookUtils::OwnedHooks(), s_saveStateAddr, oSaveState)) s_saveStateAddr=0;
        }

        s_installed.store(false);
        LogOut("[SAVESTATE][REVIVAL] Hooks uninstalled", true);
    }

    uint8_t BeginInlinePracticeHotkey(void* practiceController, int key) {
        if (!s_installed.load(std::memory_order_acquire)
            || !s_inlineDispatcherMode.load(std::memory_order_acquire)
            || !practiceController
            || key == 0
            || GetCurrentGameMode() != GameMode::Practice
            || !IsMatchPhase()
            || g_onlineModeActive.load(std::memory_order_relaxed)
            || !InlineSaveLoadBranchReachable()) {
            return 0;
        }

        const uintptr_t saveOffset = EFZ_Practice_SaveHotkeyOffset();
        const uintptr_t loadOffset = EFZ_Practice_LoadHotkeyOffset();
        if (!saveOffset || !loadOffset) return 0;

        int saveKey = -1;
        int loadKey = -1;
        const uintptr_t practice = reinterpret_cast<uintptr_t>(practiceController);
        if (!SafeReadMemory(practice + saveOffset, &saveKey, sizeof(saveKey))
            || !SafeReadMemory(practice + loadOffset, &loadKey, sizeof(loadKey))) {
            return 0;
        }

        uint8_t actions = 0;
        if (key == saveKey) {
            actions |= kInlineSaveAction;
            BeginTrackedSave();
        }
        if (key == loadKey) {
            actions |= kInlineLoadAction;
            BeginTrackedLoad();
        }
        return actions;
    }

    void EndInlinePracticeHotkey(uint8_t actionMask) {
        if (!s_inlineDispatcherMode.load(std::memory_order_acquire)) return;

        // The J dispatcher executes its save branch before its load branch.
        if ((actionMask & kInlineSaveAction) != 0) {
            FinishTrackedSave();
        }
        if ((actionMask & kInlineLoadAction) != 0) {
            // The inlined load path has no meaningful boolean result at the dispatcher boundary.
            FinishTrackedLoad(true);
        }
    }

    bool TriggerSave() {
        if (!s_installed.load() || !oSaveState) return false;
        void* ptr = PauseIntegration::GetPracticeControllerPtr();
        if (!ptr) ptr = PauseIntegration::ResolvePracticeControllerPtrNow(false, true, "savestate hotkey save");
        if (!ptr) {
            LogOut("[SAVESTATE][REVIVAL] TriggerSave: Practice controller unavailable", true);
            return false;
        }
        HookedSaveState(ptr, nullptr);
        return true;
    }

    bool TriggerLoad() {
        if (!s_installed.load() || !oLoadState) return false;
        void* ptr = PauseIntegration::GetPracticeControllerPtr();
        if (!ptr) ptr = PauseIntegration::ResolvePracticeControllerPtrNow(false, true, "savestate hotkey load");
        if (!ptr) {
            LogOut("[SAVESTATE][REVIVAL] TriggerLoad: Practice controller unavailable", true);
            return false;
        }
        HookedLoadState(ptr, nullptr);
        return true;
    }

    bool IsInstalled() {
        return s_installed.load();
    }

    void EnsureInstalled() {
        if (s_installed.load(std::memory_order_acquire)) return;
        if (!GetModuleHandleA("EfzRevival.dll") || !EFZ_RVA_LoadState() || !EFZ_RVA_SaveState()) return;
        static DWORD s_nextAttemptTick = 0; // game thread only
        const DWORD now = GetTickCount();
        if (s_nextAttemptTick != 0 && static_cast<LONG>(now - s_nextAttemptTick) < 0) return;
        s_nextAttemptTick = now + 5000;
        (void)Install();
    }

    unsigned int GetSaveCount() {
        return s_saveCount.load();
    }

    unsigned int GetLoadCount() {
        return s_loadCount.load();
    }
}
