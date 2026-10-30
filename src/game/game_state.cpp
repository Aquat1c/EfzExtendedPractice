#include "utils/minhook_utils.h"
#include "game/battle_frontend_result.h"
#include "game/character_hotswap.h"
#include "runtime/practice_battle_gates.h"
#include "../include/game/game_state.h"
#include "../include/game/game_thread_queue.h"
#include "../include/game/final_memory_patch.h"
#include "../include/game/timer_freeze_patch.h"
#include "../include/input/framestep.h"
#include "../include/game/mission/mission_engine.h"
#include "../include/game/mission/mission_pause_menu.h"
#include "../include/game/mission/tutorial_session.h"
#include "../include/game/menu_key_policy.h"
#include "../include/utils/config.h"

#include "../include/core/memory.h"
#include "../include/core/constants.h"
#include "../include/utils/utilities.h"
#include "../include/utils/xinput_shim.h"
#include "../include/utils/network.h"
#include "../include/utils/pause_integration.h"
#include "../include/utils/switch_players.h"
#include "../include/utils/bgm_control.h"
#include "../include/gui/gui.h"
#include "../include/gui/imgui_impl.h"
#include "frame_monitor.h"
#include "../include/core/logger.h"
#include "../3rdparty/minhook/include/MinHook.h"
#include <windows.h>
#include <atomic>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <mutex>

static std::atomic<GamePhase> g_phaseCache{ GamePhase::Unknown };
static std::atomic<int> g_phaseStableFrames{0};
static std::atomic<uint8_t> g_lastRawScreenState{ 255 };

namespace {
    constexpr uintptr_t RVA_GAME_MODE_ARRAY = 0x00390110;
    constexpr uintptr_t RVA_BATTLE_UPDATE = 0x00363C20;      // efz.exe 0x00763C20
    constexpr uintptr_t RVA_BATTLE_HOTKEYS = 0x00365660;     // efz.exe 0x00765660
    constexpr uint8_t SIG_BATTLE_UPDATE[] =
        {0x55,0x8B,0xEC,0x83,0xEC,0x40,0x89,0x4D,0xD0,0xFF,0x15,0xE0};
    constexpr uint8_t SIG_BATTLE_HOTKEYS[] =
        {0x55,0x8B,0xEC,0x81,0xEC,0x10,0x01,0x00,0x00,0xA1,0x50,0xF0};
    constexpr uint8_t SCREEN_TITLE = 0;
    constexpr uint8_t SCREEN_CHARACTER_SELECT = 1;
    constexpr uint8_t SCREEN_LOADING = 2;
    constexpr uint8_t SCREEN_BATTLE = 3;
    constexpr uintptr_t SCREEN_EXIT_FLAG_OFFSET = 45;
    constexpr uintptr_t BATTLE_ENGINE_PAUSE_OFFSET = 1416;

    using PendingExitOverride = GameFrontend::ExitRoute;

    using BattleUpdateFn = char (__thiscall *)(void* battleContext);
    using BattleHotkeysFn = int (__thiscall *)(void* battleContext);

    BattleUpdateFn oBattleUpdate = nullptr;
    BattleHotkeysFn oBattleHotkeys = nullptr;
    std::atomic<bool> s_frontendHooksInstalled{false};
    std::atomic<bool> s_frontendHooksAttempted{false};
    std::atomic<bool> s_frontendHooksPartial{false};
    std::mutex s_frontendHookMutex;
    std::atomic<BattleUpdateCallback> s_beforeBattleUpdate{nullptr};
    std::atomic<BattleUpdateCallback> s_afterBattleUpdate{nullptr};
    std::atomic<uint32_t> s_currentBattleBatch{0};
    std::atomic<uint32_t> s_completedBattleBatch{0};
    GameFrontend::BattleExitRouting s_exitRouting;
    std::atomic<bool> s_practiceEscHeld{false};   // demo/recorder edges only
    MenuKeyLatch s_menuLatch;                      // game thread only

    uint8_t ReadRawScreenStateNoDebounce() {
        uintptr_t base = GetEFZBase();
        uint8_t v = 255;
        if (base) {
            SafeReadMemory(base + EFZ_BASE_OFFSET_SCREEN_STATE, &v, sizeof(v));
        }
        return v;
    }

    bool ResolveScreenContext(uint8_t screenIndex, uintptr_t& outContext) {
        outContext = 0;
        uintptr_t base = GetEFZBase();
        if (!base) return false;

        uintptr_t slot = base + RVA_GAME_MODE_ARRAY + 4u * static_cast<uintptr_t>(screenIndex);
        return SafeReadMemory(slot, &outContext, sizeof(outContext)) && outContext != 0;
    }

    bool RequestScreenExitFlag(uint8_t screenIndex) {
        uintptr_t context = 0;
        if (!ResolveScreenContext(screenIndex, context)) {
            return false;
        }

        uint8_t exitRequested = 1;
        bool ok = SafeWriteMemory(context + SCREEN_EXIT_FLAG_OFFSET, &exitRequested, sizeof(exitRequested));

        if (ok && screenIndex == SCREEN_BATTLE) {
            uint32_t unpaused = 0;
            SafeWriteMemory(context + BATTLE_ENGINE_PAUSE_OFFSET, &unpaused, sizeof(unpaused));
        }

        return ok;
    }

    void CloseTrainingMenuForFrontendExit() {
        ImGuiImpl::ForceHide();
        menuOpen.store(false);
    }

    void ResetModSessionForFrontendExit(const char* reason) {
        SwitchPlayers::ResetControlMappingForMenusToP1();
        PauseIntegration::SetPracticePausedForFramestep(false);
        ResetPracticeMatchSessionState(reason);
    }

    bool IsPracticeBattleHotkeyContext() {
        if (ReadRawScreenStateNoDebounce() != SCREEN_BATTLE) {
            return false;
        }
        if (GetCurrentGameMode() != GameMode::Practice) {
            return false;
        }
        if (IsNetplaySuspendActive() || IsNetplaySessionActive()) {
            return false;
        }
        return true;
    }

    bool PollMenuKeysIfGameActive(int menuVk, bool& outEscDown, bool& outMenuDown) {
        outEscDown = false;
        outMenuDown = false;
        UpdateWindowActiveState();
        if (!g_efzWindowActive.load(std::memory_order_relaxed)) {
            return false;
        }
        outEscDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
        outMenuDown = (menuVk == VK_ESCAPE) ? outEscDown
                                            : (GetAsyncKeyState(menuVk) & 0x8000) != 0;
        return true;
    }

    char __fastcall HookedBattleUpdate(void* battleContext, void* /*edx*/) {
    auto hookExecution = MinHookUtils::EnterExecution(MinHookUtils::TicketFor<&HookedBattleUpdate>());
    if (!hookExecution.Admitted()) return oBattleUpdate ? oBattleUpdate(battleContext) : SCREEN_BATTLE;
        if (g_onlineModeActive.load(std::memory_order_acquire)
            || IsNetplaySuspendActive() || IsNetplaySessionActive()
            || !IsPracticeContext()) {
            // Netplay or another mode owns this battle: exactly-once original dispatch, nothing else.
            return oBattleUpdate ? oBattleUpdate(battleContext) : SCREEN_BATTLE;
        }
        CharacterHotswap::OnBattleFrontendEntry(reinterpret_cast<uintptr_t>(battleContext));
        const uint32_t batch = s_currentBattleBatch.fetch_add(
            1, std::memory_order_acq_rel) + 1;
        GameThread::Drain();
        if (auto callback = s_beforeBattleUpdate.load(std::memory_order_relaxed)) {
            callback(battleContext);
        }

        const char result = oBattleUpdate ? oBattleUpdate(battleContext) : SCREEN_BATTLE;

        if (auto callback = s_afterBattleUpdate.load(std::memory_order_relaxed)) {
            callback(battleContext);
        }
        s_completedBattleBatch.store(batch, std::memory_order_release);

        const uint8_t accepted = ConsumeBattleFrontendResult(static_cast<uint8_t>(result),
            Practice::BattleCleanupHeld(battleContext),0);
        if (accepted != SCREEN_BATTLE) {
            XInputShim::SetPollingActive(false);
            // Last battle frame: unwind menu/framestep pause while the Practice controller is alive.
            ImGuiImpl::ForceHide();
            Framestep::CancelActiveState("battle exit");
            PauseIntegration::RestoreVisualSitesAtBoundary("battle exit");
            // The frame monitor would also restore these, but only after its phase debounce.
            ForceRestoreFinalMemoryHPBypass("battle exit");
            TimerFreeze::ForceRestore("battle exit");
            // Queued work belongs to this battle; the next one may hold other fighters.
            GameThread::Clear();
        }
        return static_cast<char>(accepted);
    }

    int __fastcall HookedBattleHotkeys(void* battleContext, void* /*edx*/) {
    auto hookExecution = MinHookUtils::EnterExecution(MinHookUtils::TicketFor<&HookedBattleHotkeys>());
    if (!hookExecution.Admitted()) return oBattleHotkeys ? oBattleHotkeys(battleContext) : 0;
        const bool practiceBattle = IsPracticeBattleHotkeyContext();
        const int cfgMenuVk = Config::GetSettings().configMenuKey;
        const int menuVk = cfgMenuVk > 0 ? cfgMenuVk : VK_ESCAPE;
        bool escDown = false;
        bool menuDown = false;
        const bool gameActive = practiceBattle && PollMenuKeysIfGameActive(menuVk, escDown, menuDown);

        if (practiceBattle && Mission::Engine::Demo::IsActive()) {
            const bool wasHeld = s_practiceEscHeld.exchange(gameActive && escDown,
                                                             std::memory_order_acq_rel);
            // The Esc that cancels a demo must not reach EFZ after it ends.
            if (gameActive && escDown) s_menuLatch.escGuard = true;
            if (gameActive && escDown && !wasHeld) {
                LogOut("[MISSION][DEMO] Practice ESC intercepted; canceling demonstration", true);
                Mission::Engine::Demo::Cancel();
            }
            // The native dispatcher owns pause/menu here; none may run during
            // an exclusive demonstration.
            return 0;
        }

        if (practiceBattle &&
            (Mission::Engine::Recorder::IsSessionActive() ||
             Mission::Engine::Recorder::IsMenuInputHandoffActive()) &&
            !ImGuiImpl::IsVisible()) {
            const bool wasHeld = s_practiceEscHeld.exchange(gameActive && menuDown,
                                                             std::memory_order_acq_rel);
            const bool recorderHandoff =
                Mission::Engine::Recorder::IsMenuInputHandoffActive() &&
                !Mission::PauseMenu::IsOpen();
            if (recorderHandoff) {
                // Handoff interval: Menu must be ignored to preserve the
                // neutral-input transaction.
                return 0;
            }
            if (gameActive && menuDown) {
                if (!wasHeld) {
                    // The menu key opens the phase menu; a take is never auto-sealed.
                    OpenMenu();
                }
                return 0;
            }
            if (Mission::PauseMenu::IsOpen() ||
                Mission::Engine::Recorder::OwnsCaptureHotkeys() ||
                Mission::Engine::Recorder::IsMenuInputHandoffActive()) {
                // A visible dedicated menu and the two live-capture phases
                // suppress the native dispatcher entirely.
                return 0;
            }
        }

        if (!practiceBattle) {
            // Outside a Practice battle EFZ's own dispatcher must always run.
            s_practiceEscHeld.store(false, std::memory_order_relaxed);
            s_menuLatch = {};
            return oBattleHotkeys ? oBattleHotkeys(battleContext) : 0;
        }

        // Pass-through except while our UI is up, for the Esc that closed it,
        // and while Esc is the menu key or a Tutorial/Runner owns it.
        const bool uiVisible = ImGuiImpl::IsVisible();
        if (!gameActive && !uiVisible) {
            s_menuLatch = {};
            return 0;
        }
        const bool escOwned = menuVk == VK_ESCAPE ||
            Mission::TutorialSession::IsActive() || Mission::Engine::Runner::IsActive();
        const MenuKeyStep step = StepMenuKey(s_menuLatch, uiVisible, escOwned, escDown, menuDown);
        if (step.openMenu) {
            LogOut("[FRONTEND] Practice menu key intercepted; opening training menu", true);
            OpenMenu();
        }
        return step.passNative ? (oBattleHotkeys ? oBattleHotkeys(battleContext) : 0) : 0;
    }
}

uint8_t ConsumeBattleFrontendResult(uint8_t nativeResult,bool cleanupHeld,uintptr_t heldGameSystem) {
    const auto routed=s_exitRouting.Consume(nativeResult,cleanupHeld);
    if(routed.silenceTitle) {
        // A retained cleanup passes its captured authority.
        const auto gameSystem=heldGameSystem?heldGameSystem:GetGameStatePtr();
        if(gameSystem)PlayBGM(gameSystem,150);
    }
    return routed.screen;
}

GameMode GetCurrentGameMode(uint8_t* rawValueOut) {
    static uint8_t prevRawValue = 99; // sentinel: cannot be a real first value

    GameMode currentMode = GameMode::Unknown;
    uint8_t rawValue = 255;

    uintptr_t efzBase = GetEFZBase();
    uintptr_t gameModeAddr = 0;
    uintptr_t gameStructPtr = 0;

    // [efz.exe + EFZ_BASE_OFFSET_GAME_STATE] -> +GAME_MODE_OFFSET (1 byte)
    if (efzBase) {
        uintptr_t basePtrAddr = efzBase + EFZ_BASE_OFFSET_GAME_STATE;

        if (SafeReadMemory(basePtrAddr, &gameStructPtr, sizeof(uintptr_t))) {
            gameModeAddr = gameStructPtr + GAME_MODE_OFFSET;

            SafeReadMemory(gameModeAddr, &rawValue, sizeof(uint8_t));
        }
    }

    switch (rawValue) {
        case 0: currentMode = GameMode::Arcade; break;
        case 1: currentMode = GameMode::Practice; break;
        case 3: currentMode = GameMode::VsCpu; break;
        case 4: currentMode = GameMode::VsHuman; break;
        case 5: currentMode = GameMode::Replay; break;
        case 6: currentMode = GameMode::AutoReplay; break;
        default: currentMode = GameMode::Unknown; break;
    }

    // Log only on raw-value change, and never in online mode.
    if (rawValue != prevRawValue) {
        // No mode-change logging after entering online mode
        extern std::atomic<bool> g_onlineModeActive;
        if (g_onlineModeActive.load()) {
            prevRawValue = rawValue;
            if (rawValueOut) { *rawValueOut = rawValue; }
            return currentMode;
        }
        std::ostringstream oss;
        oss << std::hex << std::uppercase;
        LogOut("", true);
        LogOut("[GAME_STATE_DBG] --- Game Mode Pointer Trace ---", true);
        oss << "[GAME_STATE_DBG] efz.exe Base Address: 0x" << efzBase;
        LogOut(oss.str(), true); oss.str(""); oss.clear();
        
        oss << "[GAME_STATE_DBG] Reading pointer at [efz.exe + 0x" << EFZ_BASE_OFFSET_GAME_STATE << "] = 0x" << (efzBase + EFZ_BASE_OFFSET_GAME_STATE);
        LogOut(oss.str(), true); oss.str(""); oss.clear();
        
        oss << "[GAME_STATE_DBG]   -> Read value (as 4-byte pointer): 0x" << gameStructPtr;
        LogOut(oss.str(), true); oss.str(""); oss.clear();

        oss << "[GAME_STATE_DBG] Reading byte at [0x" << gameStructPtr << " + 0x" << GAME_MODE_OFFSET << "] = 0x" << gameModeAddr;
        LogOut(oss.str(), true); oss.str(""); oss.clear();

        oss << "[GAME_STATE_DBG]   -> Read value (as 1-byte): " << std::dec << (int)rawValue;
        LogOut(oss.str(), true); oss.str(""); oss.clear();
        LogOut("[GAME_STATE_DBG] ---------------------------------", true);

        std::string modeName = GetGameModeName(currentMode);
        LogOut("[GAME STATE] Game mode changed to: " + modeName + " (Value: " + std::to_string(rawValue) + ")", true);
        prevRawValue = rawValue;
    }

    if (rawValueOut) {
        *rawValueOut = rawValue;
    }

    return currentMode;
}

std::string GetGameModeName(GameMode mode) {
    switch (mode) {
        case GameMode::Arcade:      return "Arcade";
        case GameMode::Practice:    return "Practice";
        case GameMode::VsCpu:       return "VS CPU";
        case GameMode::VsHuman:     return "VS Human";
        case GameMode::Replay:      return "Replay";
        case GameMode::AutoReplay:  return "Auto-Replay";
        default:                    return "Unknown";
    }
}

static uint8_t ReadRawScreenState() {
    uintptr_t base = GetEFZBase();
    uint8_t v = 255;
    if (!base) return v;
    // Direct byte read at efz.exe+0x390148
    SafeReadMemory(base + EFZ_BASE_OFFSET_SCREEN_STATE, &v, sizeof(v));
    return v;
}

bool IsInCharacterSelectScreen() {
    uint8_t st = ReadRawScreenState();
    return st == 1; // 1 = Character Select
}

bool IsInGameplayState() {
    uint8_t st = ReadRawScreenState();
    return st == 3; // 3 = In-game
}

// Single authority for "may a mod-owned modal surface be opened right now".
// Liveness is NOT a phase test: ShouldFeaturesBeActive() is a bare non-null
// read of the P1 object slot, which stays allocated from CS confirm until the
// battle cleanup phase zeroes it.
bool IsTrainingMenuContext() {
    if (IsNetplaySuspendActive() || IsNetplaySessionActive()) {
        return false;
    }
    // Undebounced on purpose - see the header comment.
    if (ReadRawScreenStateNoDebounce() != SCREEN_BATTLE) {
        return false;
    }
    if (GetCurrentGameMode() != GameMode::Practice) {
        return false;
    }
    // Same AreCharactersInitialized gate as every other mutating hotkey.
    if (!AreCharactersInitialized()) {
        return false;
    }
    return true;
}

bool IsPracticeContext() {
    if (g_onlineModeActive.load(std::memory_order_acquire)) {
        return false;
    }
    if (IsNetplaySuspendActive() || IsNetplaySessionActive()) {
        return false;
    }
    return GetCurrentGameMode() == GameMode::Practice;
}

bool IsPracticeWorldLive() {
    if (!IsPracticeContext()) {
        return false;
    }
    const uintptr_t base = GetEFZBase();
    uintptr_t p1 = 0;
    return base != 0
        && SafeReadMemory(base + EFZ_BASE_OFFSET_P1, &p1, sizeof(p1))
        && p1 != 0;
}

void ClearBattleFrontendRouting() {
    s_exitRouting.Arm(GameFrontend::ExitRoute::None);
}

bool PracticeBattleHooksOpen() {
    return XInputShim::IsPollingActive()
        && !g_onlineModeActive.load(std::memory_order_acquire)
        && !IsNetplaySuspendActive()
        && !IsNetplaySessionActive();
}

bool EnsureFrontendControlHooksInstalled() {
    if (s_frontendHooksInstalled.load(std::memory_order_acquire)) {
        return true;
    }

    std::lock_guard<std::mutex> hookLock(s_frontendHookMutex);
    if (s_frontendHooksInstalled.load(std::memory_order_acquire)) return true;

    uintptr_t base = GetEFZBase();
    if (!base) {
        return false;
    }

    LPVOID battleUpdateTarget = reinterpret_cast<LPVOID>(base + RVA_BATTLE_UPDATE);
    LPVOID battleHotkeysTarget = reinterpret_cast<LPVOID>(base + RVA_BATTLE_HOTKEYS);
    auto removeOwned = [](LPVOID target, void** original, const char* label) {
        (void)MinHookUtils::DisableHook(target, "[FRONTEND]", label);
        const bool removed = MinHookUtils::RemoveHook(target, "[FRONTEND]", label);
        if (removed) {
            if (original) *original = nullptr;
            return true;
        }
        LogOut(std::string("[FRONTEND] Failed to remove ") + label +
               "; retaining trampoline for safety", true);
        s_frontendHooksPartial.store(true, std::memory_order_release);
        return false;
    };
    // The signature match is ADVISORY, not a gate: under EfzRevival these
    // prologues are already JMP-patched, so live bytes never match retail.
    // MH_CreateHook is the real gate; refuse only when the target is unreadable.
    uint8_t updateBytes[sizeof(SIG_BATTLE_UPDATE)] = {};
    uint8_t hotkeyBytes[sizeof(SIG_BATTLE_HOTKEYS)] = {};
    const bool readUpdate = SafeReadMemory(reinterpret_cast<uintptr_t>(battleUpdateTarget),
                                           updateBytes, sizeof(updateBytes));
    const bool readHotkey = SafeReadMemory(reinterpret_cast<uintptr_t>(battleHotkeysTarget),
                                           hotkeyBytes, sizeof(hotkeyBytes));
    if (!readUpdate || !readHotkey) {
        const bool alreadyAttempted = s_frontendHooksAttempted.exchange(true);
        if (!alreadyAttempted) {
            LogOut("[FRONTEND] Battle hook targets unreadable; control hooks disabled", true);
        }
        return false;
    }
    const bool sigMatch =
        std::memcmp(updateBytes, SIG_BATTLE_UPDATE, sizeof(updateBytes)) == 0 &&
        std::memcmp(hotkeyBytes, SIG_BATTLE_HOTKEYS, sizeof(hotkeyBytes)) == 0;
    if (!sigMatch) {
        static std::atomic<bool> s_sigMismatchLogged{false};
        if (!s_sigMismatchLogged.exchange(true)) {
            std::ostringstream os;
            os << "[FRONTEND] Battle prologue differs from retail signature "
                  "(expected under Revival pre-hook) - hooking anyway. update=";
            os << std::hex << std::uppercase << std::setfill('0');
            for (int i = 0; i < 5; ++i) os << std::setw(2) << static_cast<int>(updateBytes[i]) << ' ';
            os << "hotkey=";
            for (int i = 0; i < 5; ++i) os << std::setw(2) << static_cast<int>(hotkeyBytes[i]) << ' ';
            LogOut(os.str(), true);
        }
    }

    const bool updateCreated = MinHookUtils::CreateHook(
        battleUpdateTarget,
        reinterpret_cast<LPVOID>(&HookedBattleUpdate),
        reinterpret_cast<void**>(&oBattleUpdate), "[FRONTEND]", "battleUpdate", nullptr, &MinHookUtils::TicketFor<&HookedBattleUpdate>());
    if (!updateCreated) {
        LogOut(MinHookUtils::HasOwnedTarget(battleUpdateTarget)
            ? "[FRONTEND] Battle update hook already belongs to another owner"
            : "[FRONTEND] Failed to create battle update hook", true);
        s_frontendHooksAttempted.store(true);
        return false;
    }

    if (!MinHookUtils::EnableHook(battleUpdateTarget, "[FRONTEND]", "battleUpdate")) {
        (void)removeOwned(battleUpdateTarget,
                          reinterpret_cast<void**>(&oBattleUpdate),
                          "battle update hook");
        LogOut("[FRONTEND] Failed to enable battle update hook", true);
        s_frontendHooksAttempted.store(true);
        return false;
    }

    const bool hotkeyCreated = MinHookUtils::CreateHook(
        battleHotkeysTarget,
        reinterpret_cast<LPVOID>(&HookedBattleHotkeys),
        reinterpret_cast<void**>(&oBattleHotkeys), "[FRONTEND]", "battleHotkeys", nullptr, &MinHookUtils::TicketFor<&HookedBattleHotkeys>());
    if (!hotkeyCreated) {
        (void)removeOwned(battleUpdateTarget,
                          reinterpret_cast<void**>(&oBattleUpdate),
                          "battle update hook");
        LogOut(MinHookUtils::HasOwnedTarget(battleHotkeysTarget)
            ? "[FRONTEND] Battle hotkey hook already belongs to another owner"
            : "[FRONTEND] Failed to create battle hotkey gate", true);
        s_frontendHooksAttempted.store(true);
        return false;
    }

    if (!MinHookUtils::EnableHook(battleHotkeysTarget, "[FRONTEND]", "battleHotkeys")) {
        (void)removeOwned(battleHotkeysTarget,
                          reinterpret_cast<void**>(&oBattleHotkeys),
                          "battle hotkey hook");
        (void)removeOwned(battleUpdateTarget,
                          reinterpret_cast<void**>(&oBattleUpdate),
                          "battle update hook");
        LogOut("[FRONTEND] Failed to enable battle hotkey gate; install rolled back", true);
        s_frontendHooksAttempted.store(true);
        return false;
    }

    s_frontendHooksInstalled.store(true, std::memory_order_release);
    s_frontendHooksPartial.store(false, std::memory_order_release);
    LogOut("[FRONTEND] Control hooks installed transactionally", true);
    return true;
}

bool AreFrontendControlHooksInstalled() {
    return s_frontendHooksInstalled.load(std::memory_order_acquire);
}

void SetBattleUpdateCallbacks(BattleUpdateCallback beforeUpdate, BattleUpdateCallback afterUpdate) {
    s_beforeBattleUpdate.store(beforeUpdate, std::memory_order_relaxed);
    s_afterBattleUpdate.store(afterUpdate, std::memory_order_relaxed);
}

uint32_t GetCurrentBattleUpdateBatch() {
    return s_currentBattleBatch.load(std::memory_order_acquire);
}

uint32_t GetCompletedBattleUpdateBatch() {
    return s_completedBattleBatch.load(std::memory_order_acquire);
}

bool CanRequestFrontendExit(FrontendExitTarget target) {
    if (IsNetplaySuspendActive() || IsNetplaySessionActive()) {
        return false;
    }

    const uint8_t screen = ReadRawScreenStateNoDebounce();
    switch (target) {
    case FrontendExitTarget::CharacterSelect:
        return screen == SCREEN_BATTLE;
    case FrontendExitTarget::Loading:
        return screen == SCREEN_BATTLE && GetCurrentGameMode() == GameMode::Practice;
    case FrontendExitTarget::Title:
        return screen == SCREEN_BATTLE || screen == SCREEN_CHARACTER_SELECT || screen == SCREEN_TITLE;
    default:
        return false;
    }
}

namespace {
bool WriteCapturedBattleExit(FrontendExitTarget target,uintptr_t battleContext,uintptr_t gameSystem) {
    uintptr_t liveBattle=0,liveSystem=0;
    if(!battleContext || !gameSystem || !ResolveScreenContext(SCREEN_BATTLE,liveBattle) ||
       liveBattle!=battleContext || !SafeReadMemory(battleContext+0x1c,&liveSystem,sizeof(liveSystem)) ||
       liveSystem!=gameSystem)return false;
    if(target==FrontendExitTarget::Loading) {
        uint8_t mode=255;
        if(!SafeReadMemory(gameSystem+0x1364,&mode,sizeof(mode)) || mode!=1)return false;
    }
    const auto route=target==FrontendExitTarget::Title?PendingExitOverride::Title:
        target==FrontendExitTarget::Loading?PendingExitOverride::Loading:PendingExitOverride::None;
    __try {return s_exitRouting.RequestCaptured(route,battleContext,liveBattle,ReadRawScreenStateNoDebounce());}
    __except(EXCEPTION_EXECUTE_HANDLER) {s_exitRouting.Arm(PendingExitOverride::None);return false;}
}
}
bool RequestBattleFrontendExit(FrontendExitTarget target,uintptr_t battleContext,uintptr_t gameSystem) {
    if(IsNetplaySuspendActive() || IsNetplaySessionActive())return false;
    if(target!=FrontendExitTarget::CharacterSelect && target!=FrontendExitTarget::Loading && target!=FrontendExitTarget::Title)return false;
    if(target!=FrontendExitTarget::CharacterSelect && !s_frontendHooksInstalled.load(std::memory_order_acquire))return false;
    if(!WriteCapturedBattleExit(target,battleContext,gameSystem))return false;
    const char* resetReason=target==FrontendExitTarget::Title?"MenuExitToTitle":
        target==FrontendExitTarget::Loading?"MissionDirectReload":"MenuExitToCharacterSelect";
    ResetModSessionForFrontendExit(resetReason);
    CloseTrainingMenuForFrontendExit();
    LogOut(target==FrontendExitTarget::Title?"[FRONTEND] Requested exit to Title through battle cleanup":
        target==FrontendExitTarget::Loading?"[FRONTEND] Requested direct Loading through battle cleanup":
        "[FRONTEND] Requested exit to Character Select through battle cleanup",true);
    return true;
}

bool RequestFrontendExit(FrontendExitTarget target) {
    if (!CanRequestFrontendExit(target)) {
        LogOut("[FRONTEND] Exit request ignored in current screen/netplay state", true);
        return false;
    }

    (void)EnsureFrontendControlHooksInstalled();

    const uint8_t screen = ReadRawScreenStateNoDebounce();
    if (target == FrontendExitTarget::Title && screen == SCREEN_TITLE) {
        CloseTrainingMenuForFrontendExit();
        return true;
    }

    if (screen == SCREEN_BATTLE) {
        uintptr_t battleContext=0,gameSystem=0;
        if(!ResolveScreenContext(SCREEN_BATTLE,battleContext) ||
           !SafeReadMemory(battleContext+0x1c,&gameSystem,sizeof(gameSystem)))return false;
        return RequestBattleFrontendExit(target,battleContext,gameSystem);
    }

    if (screen == SCREEN_CHARACTER_SELECT && target == FrontendExitTarget::Title) {
        if (!RequestScreenExitFlag(SCREEN_CHARACTER_SELECT)) {
            LogOut("[FRONTEND] Failed to request Character Select exit", true);
            return false;
        }
        CloseTrainingMenuForFrontendExit();
        LogOut("[FRONTEND] Requested exit to Title through Character Select cleanup", true);
        return true;
    }

    return false;
}

void DebugDumpScreenState() {
    uintptr_t base = GetEFZBase();
    if (!base) {
        LogOut("[SCREEN_STATE] Base missing", true);
        return;
    }
    uintptr_t gameStatePtr = 0;
    if (!SafeReadMemory(base + EFZ_BASE_OFFSET_GAME_STATE, &gameStatePtr, sizeof(uintptr_t)) || !gameStatePtr) {
        LogOut("[SCREEN_STATE] Game state ptr invalid", true);
        return;
    }

    LogOut("[SCREEN_STATE] ---- Game State Raw (first 0x40 bytes) ----", true);
    uint8_t buffer[0x40]{};
    SafeReadMemory(gameStatePtr, buffer, sizeof(buffer));
    std::string line;
    for (int i = 0; i < 0x40; i++) {
        char b[8];
        sprintf_s(b, "%02X ", buffer[i]);
        line += b;
        if ((i & 0x0F) == 0x0F) {
            LogOut("[SCREEN_STATE] " + line, true);
            line.clear();
        }
    }
    if (!line.empty())
        LogOut("[SCREEN_STATE] " + line, true);

    uint8_t rawScr = ReadRawScreenState();
    LogOut("[SCREEN_STATE] Mode=" + GetGameModeName(GetCurrentGameMode()) +
           " ScreenByte=" + std::to_string(rawScr) +
           " CharSelect=" + std::to_string(IsInCharacterSelectScreen()), true);
    LogOut("[SCREEN_STATE] ------------------------------------------", true);
}

GamePhase GetCurrentGamePhase() {
    // Map efz.exe+0x390148 directly to our phases with minimal debounce
    uint8_t st = ReadRawScreenState();
    g_lastRawScreenState.store(st);
    GamePhase derived = GamePhase::Unknown;

    switch (st) {
        case 0: // Title
        case 6: // Settings
        case 8: // Replay selection
        case 5: // Win screen
            derived = GamePhase::Menu;
            break;
        case 1: // Character Select
            derived = GamePhase::CharacterSelect;
            break;
        case 2: // Loading
            derived = GamePhase::Loading;
            break;
        case 3: // In-game
            derived = GamePhase::Match;
            break;
        default:
            derived = GamePhase::Unknown;
            break;
    }

    // Debounce: require 3 consecutive frames before committing
    static std::atomic<GamePhase> lastRaw{GamePhase::Unknown};
    if (lastRaw.exchange(derived) == derived) {
        int s = g_phaseStableFrames.fetch_add(1) + 1;
        if (s >= 3) g_phaseCache.store(derived);
    } else {
        g_phaseStableFrames.store(0);
    }

    return g_phaseCache.load();
}

void LogPhaseIfChanged() {
    static GamePhase prev = GamePhase::Unknown;
    GamePhase now = g_phaseCache.load();
    if (now != prev) {
        LogOut("[GAME_PHASE] " + std::to_string((int)prev) + " -> " + std::to_string((int)now), true);
        prev = now;
    }
}
