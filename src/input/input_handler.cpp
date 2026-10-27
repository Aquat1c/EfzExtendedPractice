#include <fstream>
#include <iomanip>
#include <chrono>
#include "../include/input/input_handler.h"
#include "../include/game/frame_advantage.h"
#include "../include/utils/utilities.h"

#include "../include/gui/gui.h"
#include "../include/core/logger.h"
#include "../include/core/memory.h"
#include "../include/core/constants.h"
#include "../include/utils/network.h"
#include <windows.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <dinput.h>
#include <sstream>
#include <string>
#include <commctrl.h>
#include "../include/gui/imgui_impl.h"
#include "../include/gui/overlay.h"
#include "../include/game/mission/mission_pause_menu.h"
#include "../include/game/mission/tutorial_session.h"
#include "../include/utils/config.h"
#include "../include/input/input_motion.h" // For QueueMotionInput
#include "../include/utils/bgm_control.h"
#include "../include/game/mission/mission_engine.h" // Mission record/playback hotkeys
#include "../include/input/input_freeze.h"
#include "../include/input/efz_input_bindings.h"
#include "../include/game/practice_patch.h"
#include "../include/game/game_state.h"
#include "../include/utils/switch_players.h"
#include "../include/game/macro_controller.h"
#include "../include/game/frame_monitor.h" // AreCharactersInitialized, GamePhase
#include "../include/game/auto_action.h" // CancelAutoActionsAndMacros
#include "../include/game/character_settings.h"
#include "../include/game/savestate_hook.h"
#include "../include/input/framestep.h"
#include "../include/game/game_thread_queue.h"
#include "../include/utils/xp_compat.h"
#include <Xinput.h>

// XInput DLL is loaded dynamically via XInputShim
#include "../include/utils/xinput_shim.h"

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "comctl32.lib")

LPDIRECTINPUT8 g_pDI = nullptr;
LPDIRECTINPUTDEVICE8 g_pKeyboard = nullptr;

bool g_directInputReadOnly = true;
bool g_directInputAvailable = false;

static std::atomic<bool> g_hotkeyCooldownActive{false};
static std::chrono::steady_clock::time_point g_menuClosedTime{};
static constexpr double HOTKEY_COOLDOWN_SECONDS = 0.5;

static bool IsHotkeyCooldownActive() {
    if (!g_hotkeyCooldownActive.load(std::memory_order_relaxed)) {
        return false;
    }
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration<double>(now - g_menuClosedTime).count();
    bool inCooldown = elapsed < HOTKEY_COOLDOWN_SECONDS;
    
    if (!inCooldown && g_hotkeyCooldownActive.load(std::memory_order_relaxed)) {
        g_hotkeyCooldownActive.store(false, std::memory_order_relaxed);
        LogOut("[INPUT] Hotkey cooldown expired", true);
    }
    
    return inCooldown;
}

void StartHotkeyCooldown() {
    g_menuClosedTime = std::chrono::steady_clock::now();
    g_hotkeyCooldownActive.store(true, std::memory_order_relaxed);
    LogOut("[INPUT] Menu closed - starting 0.5s hotkey cooldown", true);
}

int g_gamepadCount = 0;
GamepadDevice g_gamepads[MAX_CONTROLLERS];

// Hotkey actions that call into efz/Revival run as game-thread commands.
static void MenuOpenCommand(uintptr_t) { OpenMenu(); }
static void MenuOpenIfHiddenCommand(uintptr_t) { if (!ImGuiImpl::IsVisible()) OpenMenu(); }
static void MenuToggleCommand(uintptr_t) {
    if (!ImGuiImpl::IsVisible()) OpenMenu();
    else ImGuiImpl::ToggleVisibility();
}
// Without the battle hooks there is no game-thread owner; the legacy direct path stays.
static void PostMenuCommand(GameThread::Command cmd) {
    if (!GameThread::Post(cmd) && !AreFrontendControlHooksInstalled()) cmd(0);
}
static void ToggleLocalSideCommand(uintptr_t) {
    if (SwitchPlayers::ToggleLocalSide()) {
        DirectDrawHook::AddMessage("Switch Players: toggled", "SYSTEM", RGB(100,255,100), 1200, 0, 100);
    } else {
        DirectDrawHook::AddMessage("Switch Players: failed", "SYSTEM", RGB(255,100,100), 1200, 0, 100);
    }
}
static void PostToggleLocalSide() {
    if (!GameThread::Post(&ToggleLocalSideCommand)) {
        DirectDrawHook::AddMessage("Switch Players: failed", "SYSTEM", RGB(255,100,100), 1200, 0, 100);
    }
}
static void SavestateSaveCommand(uintptr_t) {
    if (!SavestateHook::TriggerSave()) {
        DirectDrawHook::AddMessage("Savestate save unavailable (Revival not ready)", "SAVESTATE", RGB(255, 180, 120), 1200, 0, 100);
    }
}
static void SavestateLoadCommand(uintptr_t) {
    if (!SavestateHook::TriggerLoad()) {
        DirectDrawHook::AddMessage("Savestate load unavailable (Revival not ready)", "SAVESTATE", RGB(255, 180, 120), 1200, 0, 100);
    }
}
static void FramestepTogglePauseCommand(uintptr_t) { Framestep::TogglePause(); }
static void FramestepStepCommand(uintptr_t) { Framestep::RequestFrameStep(); }

extern void ResetFrameCounter();

#define JOYSTICK_DEADZONE 8000  // Standard deadzone for a 16-bit joystick

bool InitDirectInputReadOnly(HINSTANCE hInstance) {
    HRESULT hr;
    
    CleanupDirectInput();
    
    LogOut("[INPUT] Initializing DirectInput in read-only mode", true);
    
    hr = DirectInput8Create(hInstance, DIRECTINPUT_VERSION, 
                            IID_IDirectInput8, (void**)&g_pDI, NULL);
    if (FAILED(hr)) {
        LogOut("[INPUT] Failed to create DirectInput interface: " + std::to_string(hr), true);
        return false;
    }
    
    hr = g_pDI->CreateDevice(GUID_SysKeyboard, &g_pKeyboard, NULL);
    if (FAILED(hr)) {
        LogOut("[INPUT] Failed to create keyboard device: " + std::to_string(hr), true);
        CleanupDirectInput();
        return false;
    }
    
    hr = g_pKeyboard->SetDataFormat(&c_dfDIKeyboard);
    if (FAILED(hr)) {
        LogOut("[INPUT] Failed to set keyboard data format: " + std::to_string(hr), true);
        CleanupDirectInput();
        return false;
    }
    
    hr = g_pKeyboard->SetCooperativeLevel(
        GetConsoleWindow(), 
        DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
    if (FAILED(hr)) {
        LogOut("[INPUT] Failed to set cooperative level: " + std::to_string(hr), true);
        CleanupDirectInput();
        return false;
    }
    
    g_pKeyboard->Acquire();
    
    g_directInputAvailable = true;
    LogOut("[INPUT] DirectInput initialized in read-only mode", true);
    return true;
}

void CleanupDirectInput() {
    if (g_pKeyboard) {
        g_pKeyboard->Unacquire();
        g_pKeyboard->Release();
        g_pKeyboard = nullptr;
    }
    
    if (g_pDI) {
        g_pDI->Release();
        g_pDI = nullptr;
    }
    
    LogOut("[INPUT] DirectInput cleaned up", true);
}

bool PollDirectInputState(BYTE* keyboardState) {
    if (!XInputShim::IsPollingActive()) return false;
    if (!g_pKeyboard || !g_directInputAvailable)
        return false;
    
    HRESULT hr = g_pKeyboard->GetDeviceState(256, keyboardState);
    
    if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
        hr = g_pKeyboard->Acquire();
        if (SUCCEEDED(hr)) {
            hr = g_pKeyboard->GetDeviceState(256, keyboardState);
        }
    }
    
    return SUCCEEDED(hr);
}

bool IsDIKeyPressed(BYTE* keyboardState, DWORD dikCode) {
    if (!keyboardState)
        return false;
    
    return (keyboardState[dikCode] & 0x80) != 0;
}

bool IsKeyPressed(int vKey, bool checkState) {
    if (!XInputShim::IsPollingActive()) return false;
    SHORT keyState;
    if (checkState) {
        keyState = GetKeyState(vKey);
        if ((keyState & 0x8000) != 0) {
            return true;
        }
        
        keyState = GetAsyncKeyState(vKey);
        return ((keyState & 0x8000) != 0);
    } else {
        // Check for key press since last call (bit 0)
        keyState = GetAsyncKeyState(vKey);
        return ((keyState & 0x0001) != 0);
    }
}

std::atomic<bool> keyMonitorRunning(false);
static std::atomic<bool> keyMonitorAlive{false};
std::mutex keyMonitorMutex;

void MonitorKeys() {
    // CRITICAL: Never run during online mode
    if (!keyMonitorRunning.load() || !XInputShim::IsPollingActive() || !IsTrainingMenuContext()) {
        keyMonitorRunning.store(false);
        keyMonitorAlive.store(false, std::memory_order_release);
        return;
    }
    LogOut("[KEYBINDS] Key monitoring thread started", true);
    
    {
        const Config::Settings& cfg0 = Config::GetSettings();
        LogOut("[KEYBINDS] Hotkey values from config:", true);
        LogOut("[KEYBINDS] Teleport/Load key: " + GetKeyName(cfg0.teleportKey), true);
        LogOut("[KEYBINDS] Record/Save key: " + GetKeyName(cfg0.recordKey), true);
        LogOut("[KEYBINDS] Menu key: " + GetKeyName(cfg0.configMenuKey), true);
    }

    static DWORD s_nextIniRetryTick = GetTickCount() + 60000;

    const double centerX = 320.0;
    const double leftX = 43.6548, rightX = 595.425, teleportY = 0.0;
    const double p1StartX = 240.0, p2StartX = 400.0, startY = 0.0;

    XINPUT_STATE prevPads[4]{};
    DWORD lastConnScanTick = 0;
    unsigned connectedMask = 0; // bit i => pad i connected

    int sleepMs = 16;
    int idleLoops = 0;
    const int idleThreshold = 10;
    unsigned long long nextWindowStateRefreshTick = 0;
    while (keyMonitorRunning.load()) {
        if (!XInputShim::IsPollingActive() || !IsTrainingMenuContext()) {
            keyMonitorRunning.store(false);
            break;
        }
        const unsigned long long nowTick = XPCompat::GetTickCount64Compat();
        if (nowTick >= nextWindowStateRefreshTick) {
            UpdateWindowActiveState();
            nextWindowStateRefreshTick = nowTick + 100;
        }

    if (GetTickCount() >= s_nextIniRetryTick) {
        if (!detectedBindings.attacksDetected || detectedBindings.dButton == 0) {
            if (ReadKeyMappingsFromIni()) {
                LogOut("[KEYBINDS] Loaded EFZ key bindings from key.ini on retry", true);
                s_nextIniRetryTick = GetTickCount() + 60000;
            } else {
                s_nextIniRetryTick = GetTickCount() + 10000;
            }
        } else {
            s_nextIniRetryTick = GetTickCount() + 60000;
        }
    }
    const Config::Settings& cfg = Config::GetSettings();
    int teleportKey = (cfg.teleportKey > 0) ? cfg.teleportKey : '1';
    int recordKey = (cfg.recordKey > 0) ? cfg.recordKey : '2';
    int configMenuKey = (cfg.configMenuKey > 0) ? cfg.configMenuKey : VK_ESCAPE;
    int toggleTitleKey = cfg.toggleTitleKey;
    int resetFrameCounterKey = cfg.resetFrameCounterKey;
    int savestateSaveKey = (cfg.savestateSaveKey > 0) ? cfg.savestateSaveKey : 'U';
    int savestateLoadKey = (cfg.savestateLoadKey > 0) ? cfg.savestateLoadKey : 'J';
    XINPUT_STATE currentPad{};

    bool windowActive = g_efzWindowActive.load();
    bool guiActive = g_guiActive.load();

    // Demo mode owns P1's poll; only Escape and the configured Menu action may cancel it.
    static bool s_demoEscWasDown = false;
    if (windowActive && Mission::Engine::Demo::IsActive()) {
        const bool escDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
        if (escDown && !s_demoEscWasDown) Mission::Engine::Demo::Cancel();
        s_demoEscWasDown = escDown;
        XInputShim::RefreshSnapshotOncePerFrame();
        XInputShim::Snapshot padSnapshot{};
        XInputShim::CopySnapshot(padSnapshot);
        connectedMask = padSnapshot.connectedMask;
        auto padBindingDown = [](const XINPUT_STATE& state, int binding) {
            constexpr int LT_MASK = 0x10000;
            constexpr int RT_MASK = 0x20000;
            if (binding == LT_MASK) return state.Gamepad.bLeftTrigger >= 50;
            if (binding == RT_MASK) return state.Gamepad.bRightTrigger >= 50;
            if (binding < 0) return false;
            return (state.Gamepad.wButtons & static_cast<WORD>(binding & 0xFFFF)) != 0;
        };
        for (int i = 0; i < 4; ++i) {
            if (((connectedMask >> i) & 1u) == 0) continue;
            if (cfg.controllerIndex >= 0 && cfg.controllerIndex <= 3 &&
                i != cfg.controllerIndex) continue;
            XINPUT_STATE cached{};
            if (padSnapshot.CopyState(i, cached)) {
                const bool menuNow = padBindingDown(cached, cfg.gpToggleMenuButton);
                const bool menuWas = padBindingDown(prevPads[i], cfg.gpToggleMenuButton);
                if (menuNow && !menuWas) Mission::Engine::Demo::Cancel();
                prevPads[i] = cached;
            }
        }
        Sleep(8);
        continue;
    }
    if (s_demoEscWasDown) {
        // Do not let a held Escape become a fresh menu-open edge when restoration ends.
        if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) == 0) {
            s_demoEscWasDown = false;
            (void)IsKeyPressed(configMenuKey, false); // flush the completed press
        }
        Sleep(8);
        continue;
    }

    // Tutorial/contextual pause menus own the hotkey router; only the Menu action stays alive.
    static bool s_contextMenuEscWasDown = true;
    const bool recorderMenuHandoff =
        Mission::Engine::Recorder::IsMenuInputHandoffActive() &&
        !Mission::PauseMenu::IsOpen();
    const bool contextualPauseOwnsInput =
        Mission::TutorialSession::IsActive() ||
        recorderMenuHandoff ||
        (Mission::PauseMenu::IsOpen() && !ImGuiImpl::IsVisible());
    if (windowActive && contextualPauseOwnsInput) {
        const bool allowMenuToggle = !recorderMenuHandoff &&
            (Mission::TutorialSession::IsActive() ||
             Mission::PauseMenu::IsOpen());
        const bool escDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
        // The battle-hotkey hook owns Esc on the game thread when installed.
        if (allowMenuToggle && escDown && !s_contextMenuEscWasDown &&
            !AreFrontendControlHooksInstalled()) OpenMenu();
        s_contextMenuEscWasDown = escDown;

        XInputShim::RefreshSnapshotOncePerFrame();
        XInputShim::Snapshot padSnapshot{};
        XInputShim::CopySnapshot(padSnapshot);
        connectedMask = padSnapshot.connectedMask;
        auto padBindingDown = [](const XINPUT_STATE& state, int binding) {
            constexpr int LT_MASK = 0x10000;
            constexpr int RT_MASK = 0x20000;
            if (binding == LT_MASK) return state.Gamepad.bLeftTrigger >= 50;
            if (binding == RT_MASK) return state.Gamepad.bRightTrigger >= 50;
            if (binding < 0) return false;
            return (state.Gamepad.wButtons & static_cast<WORD>(binding & 0xFFFF)) != 0;
        };
        for (int i = 0; i < 4; ++i) {
            if (((connectedMask >> i) & 1u) == 0) continue;
            if (cfg.controllerIndex >= 0 && cfg.controllerIndex <= 3 &&
                i != cfg.controllerIndex) continue;
            XINPUT_STATE cached{};
            if (padSnapshot.CopyState(i, cached)) {
                const bool menuNow = padBindingDown(cached, cfg.gpToggleMenuButton);
                const bool menuWas = padBindingDown(prevPads[i], cfg.gpToggleMenuButton);
                if (allowMenuToggle && menuNow && !menuWas) PostMenuCommand(&MenuOpenCommand);
                prevPads[i] = cached;
            }
        }
        Sleep(8);
        continue;
    }
    s_contextMenuEscWasDown = true;

    if (windowActive && guiActive) {
        // Flush queued menu toggle presses so they don't reopen immediately after exit
        IsKeyPressed(configMenuKey, false);

        XInputShim::RefreshSnapshotOncePerFrame();
        XInputShim::Snapshot padSnapshot{};
        XInputShim::CopySnapshot(padSnapshot);
        connectedMask = padSnapshot.connectedMask;
        for (int i = 0; i < 4; ++i) {
            if (((connectedMask >> i) & 1u) == 0) {
                continue;
            }
            XINPUT_STATE cached{};
            if (padSnapshot.CopyState(i, cached)) {
                prevPads[i] = cached;
            }
        }

        Sleep(64);
        continue;
    }

    if (windowActive && !guiActive) {
            bool keyHandled = false;

            auto pollPad = [](int idx, XINPUT_STATE& out) {
                return XInputShim::CopyCachedState(idx, out);
            };
            auto processActionsForPad = [&](int padIndex, const XINPUT_STATE& cur, XINPUT_STATE& prev) -> bool {
                constexpr int LT_MASK = 0x10000; // pseudo masks defined in config parser
                constexpr int RT_MASK = 0x20000;
                constexpr int TRIGGER_THRESHOLD = 50;
                auto gpWentDown = [&](int mask) -> bool {
                    if (mask < 0) return false; // disabled
                    if (mask == LT_MASK) {
                        bool now = cur.Gamepad.bLeftTrigger >= TRIGGER_THRESHOLD;
                        bool was = prev.Gamepad.bLeftTrigger >= TRIGGER_THRESHOLD;
                        return now && !was;
                    }
                    if (mask == RT_MASK) {
                        bool now = cur.Gamepad.bRightTrigger >= TRIGGER_THRESHOLD;
                        bool was = prev.Gamepad.bRightTrigger >= TRIGGER_THRESHOLD;
                        return now && !was;
                    }
                    WORD wMask = static_cast<WORD>(mask & 0xFFFF);
                    return (cur.Gamepad.wButtons & wMask) && !(prev.Gamepad.wButtons & wMask);
                };

                auto savePositions = [&]() {
                    uintptr_t base = GetEFZBase();
                    if (base) {
                        SavePlayerPositions(base);
                        DirectDrawHook::AddMessage("Position Saved", "SYSTEM", RGB(255, 255, 100), 1500, 0, 100);
                    }
                };
                auto reapplyCharacterLocks = [&](uintptr_t base) {
                    CharacterSettings::TickCharacterEnforcements(base, displayData);
                };
                auto teleportOrLoad = [&]() {
                    uintptr_t base = GetEFZBase();
                    if (!base) return;
                    
                    // Cancel auto-actions and macros before any position change
                    CancelAutoActionsAndMacros();
                    
                    if ((cur.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) && (cur.Gamepad.wButtons & XINPUT_GAMEPAD_A)) {
                        SetPlayerPosition(base, EFZ_BASE_OFFSET_P1, p1StartX, startY);
                        SetPlayerPosition(base, EFZ_BASE_OFFSET_P2, p2StartX, startY);
                        DirectDrawHook::AddMessage("Round Start Position", "SYSTEM", RGB(100, 255, 100), 1500, 0, 100);
                    } else if (cur.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) {
                        SetPlayerPosition(base, EFZ_BASE_OFFSET_P1, centerX, teleportY);
                        SetPlayerPosition(base, EFZ_BASE_OFFSET_P2, centerX, teleportY);
                        DirectDrawHook::AddMessage("Players Centered", "SYSTEM", RGB(100, 255, 100), 1500, 0, 100);
                    } else if (cur.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) {
                        SetPlayerPosition(base, EFZ_BASE_OFFSET_P1, leftX, teleportY);
                        SetPlayerPosition(base, EFZ_BASE_OFFSET_P2, leftX, teleportY);
                        DirectDrawHook::AddMessage("Left Corner", "SYSTEM", RGB(100, 255, 100), 1500, 0, 100);
                    } else if (cur.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) {
                        SetPlayerPosition(base, EFZ_BASE_OFFSET_P1, rightX, teleportY);
                        SetPlayerPosition(base, EFZ_BASE_OFFSET_P2, rightX, teleportY);
                        DirectDrawHook::AddMessage("Right Corner", "SYSTEM", RGB(100, 255, 100), 1500, 0, 100);
                    } else {
                        LoadPlayerPositions(base);
                        DirectDrawHook::AddMessage("Position Loaded", "SYSTEM", RGB(100, 255, 100), 1500, 0, 100);
                    }
                    reapplyCharacterLocks(base);
                };
                auto swapPositions = [&]() {
                    uintptr_t base = GetEFZBase();
                    if (!base) return;
                    double x1=0,y1=0,x2=0,y2=0;
                    uintptr_t xAddr1 = ResolvePointer(base, EFZ_BASE_OFFSET_P1, XPOS_OFFSET);
                    uintptr_t yAddr1 = ResolvePointer(base, EFZ_BASE_OFFSET_P1, YPOS_OFFSET);
                    uintptr_t xAddr2 = ResolvePointer(base, EFZ_BASE_OFFSET_P2, XPOS_OFFSET);
                    uintptr_t yAddr2 = ResolvePointer(base, EFZ_BASE_OFFSET_P2, YPOS_OFFSET);
                    if (xAddr1 && yAddr1 && xAddr2 && yAddr2) {
                        SafeReadMemory(xAddr1, &x1, sizeof(double));
                        SafeReadMemory(yAddr1, &y1, sizeof(double));
                        SafeReadMemory(xAddr2, &x2, sizeof(double));
                        SafeReadMemory(yAddr2, &y2, sizeof(double));
                        SetPlayerPosition(base, EFZ_BASE_OFFSET_P1, x2, y2);
                        SetPlayerPosition(base, EFZ_BASE_OFFSET_P2, x1, y1);
                        reapplyCharacterLocks(base);
                        DirectDrawHook::AddMessage("Positions Swapped", "SYSTEM", RGB(100, 255, 100), 1500, 0, 100);
                    } else {
                        DirectDrawHook::AddMessage("Swap Failed: Can't read positions", "SYSTEM", RGB(255,100,100), 1500, 0, 100);
                    }
                };

                const auto &cgp = cfg;

                if (IsHotkeyCooldownActive()) {
                    prev = cur;
                    return false;
                }

                bool handled = false;
                const auto recPhase = Mission::Engine::Recorder::GetPhase();
                const bool recorderOwnsHotkeys =
                    recPhase == Mission::Engine::Recorder::Phase::CountIn ||
                    recPhase == Mission::Engine::Recorder::Phase::Recording;
                if (recorderOwnsHotkeys) {
                    if (gpWentDown(cgp.gpToggleMenuButton)) {
                        PostMenuCommand(&MenuOpenIfHiddenCommand);
                        handled = true;
                    } else if (gpWentDown(cgp.gpMacroRecordButton) &&
                               !Mission::PauseMenu::IsOpen()) {
                        Mission::Engine::Recorder::Advance();
                        handled = true;
                    }
                    prev = cur;
                    return handled;
                }
                if (!handled && gpWentDown(cgp.gpToggleMenuButton)) {
                    PostMenuCommand(&MenuToggleCommand);
                    handled = true;
                } else if (!handled && gpWentDown(cgp.gpTeleportButton)) {
                    CancelFrameAdvantageCalculation();
                    teleportOrLoad();
                    handled = true;
                } else if (!handled && gpWentDown(cgp.gpSavePositionButton)) {
                    savePositions();
                    handled = true;
                } else if (!handled && gpWentDown(cgp.gpSwapPositionsButton)) {
                    swapPositions();
                    handled = true;
                } else if (!handled && gpWentDown(cgp.gpSwitchPlayersButton)) {
                    // No side swap during macro record/playback: the P2 poll override would drive the human's new side.
                    auto st = MacroController::GetState();
                    if (Mission::Engine::Recorder::IsSessionActive() ||
                        st == MacroController::State::PreRecord || st == MacroController::State::Recording ||
                        st == MacroController::State::Replaying) {
                        DirectDrawHook::AddMessage("Switch Players disabled during Macro PreRecord/Recording/Playback", "SYSTEM", RGB(255,200,120), 1200, 0, 100);
                        handled = true;
                    } else {
                        if (GetCurrentGameMode() == GameMode::Practice) {
                            PostToggleLocalSide();
                        }
                        handled = true;
                    }
                } else if (!handled && gpWentDown(cgp.gpMacroRecordButton)) {
                    if (GetCurrentGamePhase() == GamePhase::Match && AreCharactersInitialized()) {
                        if (Mission::Engine::Recorder::IsSessionActive()) {
                            Mission::Engine::Recorder::Advance();
                        } else {
                            MacroController::ToggleRecord();
                            DirectDrawHook::AddMessage(MacroController::GetStatusLine().c_str(), "MACRO", RGB(200,220,255), 900, 0, 120);
                        }
                    } else {
                        DirectDrawHook::AddMessage("Macro controls available only during Match", "MACRO", RGB(255,180,120), 900, 0, 120);
                    }
                    handled = true;
                } else if (!handled && gpWentDown(cgp.gpMacroPlayButton)) {
                    if (GetCurrentGamePhase() == GamePhase::Match && AreCharactersInitialized()) {
                        if (Mission::Engine::Recorder::IsSessionActive()) {
                            DirectDrawHook::AddMessage("Macro playback is unavailable during mission authoring", "MISSION", RGB(255,200,120), 1200, 0, 120);
                        } else {
                            MacroController::Play();
                            DirectDrawHook::AddMessage(MacroController::GetStatusLine().c_str(), "MACRO", RGB(180,255,180), 900, 0, 120);
                        }
                    } else {
                        DirectDrawHook::AddMessage("Macro controls available only during Match", "MACRO", RGB(255,180,120), 900, 0, 120);
                    }
                    handled = true;
                } else if (!handled && gpWentDown(cgp.gpMacroSlotButton)) {
                    if (GetCurrentGamePhase() == GamePhase::Match && AreCharactersInitialized()) {
                        MacroController::NextSlot();
                        DirectDrawHook::AddMessage((std::string("Macro: Slot ") + std::to_string(MacroController::GetCurrentSlot())).c_str(), "MACRO", RGB(230,230,120), 800, 0, 120);
                    } else {
                        DirectDrawHook::AddMessage("Macro controls available only during Match", "MACRO", RGB(255,180,120), 900, 0, 120);
                    }
                    handled = true;
                }

                prev = cur;
                return handled;
            };

            // 1) Keyboard menu/overlay toggles first (avoids conflicts).
            // The battle-hotkey hook owns this key on the game thread when installed.
            if (!AreFrontendControlHooksInstalled() && IsKeyPressed(configMenuKey, false)) {
                if (!ImGuiImpl::IsVisible()) {
                    OpenMenu();
                }
                Sleep(300); // debounce
                continue;
            }

            // 2) Gamepad processing (selected controller or all)
            auto refreshConnectionsIfNeeded = [&]() {
                DWORD now = GetTickCount();
                if ((now - lastConnScanTick) < 1000) return;
                lastConnScanTick = now;
                XInputShim::RefreshSnapshotOncePerFrame();
                connectedMask = XInputShim::GetConnectedMaskCached();
            };
            refreshConnectionsIfNeeded();
            int cfgIdx = cfg.controllerIndex;
            if (cfgIdx >= 0 && cfgIdx <= 3) {
                XINPUT_STATE cur{};
                if (pollPad(cfgIdx, cur)) {
                    if (processActionsForPad(cfgIdx, cur, prevPads[cfgIdx])) {
                        keyHandled = true;
                    }
                }
            } else {
                unsigned mask = connectedMask;
                if (mask == 0) {
                    XInputShim::RefreshSnapshotOncePerFrame();
                    mask = XInputShim::GetConnectedMaskCached();
                    connectedMask = mask;
                }
                for (int i = 0; i < 4 && !keyHandled; ++i) {
                    if (((mask >> i) & 1u) == 0) continue;
                    XINPUT_STATE cur{}; if (!pollPad(i, cur)) continue;
                    if (processActionsForPad(i, cur, prevPads[i])) {
                        keyHandled = true; break;
                    }
                }
            }

            const auto recorderPhase = Mission::Engine::Recorder::GetPhase();
            const bool recorderOwnsHotkeys =
                recorderPhase == Mission::Engine::Recorder::Phase::CountIn ||
                recorderPhase == Mission::Engine::Recorder::Phase::Recording;
            if (!keyHandled && recorderOwnsHotkeys) {
                if (!Mission::PauseMenu::IsOpen() &&
                    IsKeyPressed(cfg.macroRecordKey > 0 ? cfg.macroRecordKey : 'I', false)) {
                    Mission::Engine::Recorder::Advance();
                    keyHandled = true;
                } else {
                    Sleep(8);
                    continue;
                }
            }

            // 3) Remaining keyboard-only actions
            if (!keyHandled) {
                if (IsInCharacterSelectScreen()) {
                } else if (IsHotkeyCooldownActive()) {
                } else {
                {
                    const bool swapEnabled = cfg.swapCustomEnabled;
                    const int swapKey = cfg.swapCustomKey;
                    bool didSwap = false;
                    auto doSwap = [&]() {
                        uintptr_t base = GetEFZBase();
                        if (!base) return;
                        double x1=0,y1=0,x2=0,y2=0;
                        uintptr_t xAddr1 = ResolvePointer(base, EFZ_BASE_OFFSET_P1, XPOS_OFFSET);
                        uintptr_t yAddr1 = ResolvePointer(base, EFZ_BASE_OFFSET_P1, YPOS_OFFSET);
                        uintptr_t xAddr2 = ResolvePointer(base, EFZ_BASE_OFFSET_P2, XPOS_OFFSET);
                        uintptr_t yAddr2 = ResolvePointer(base, EFZ_BASE_OFFSET_P2, YPOS_OFFSET);
                        if (xAddr1 && yAddr1 && xAddr2 && yAddr2) {
                            SafeReadMemory(xAddr1, &x1, sizeof(double));
                            SafeReadMemory(yAddr1, &y1, sizeof(double));
                            SafeReadMemory(xAddr2, &x2, sizeof(double));
                            SafeReadMemory(yAddr2, &y2, sizeof(double));
                            SetPlayerPosition(base, EFZ_BASE_OFFSET_P1, x2, y2);
                            SetPlayerPosition(base, EFZ_BASE_OFFSET_P2, x1, y1);
                            DirectDrawHook::AddMessage("Positions Swapped", "SYSTEM", RGB(100, 255, 100), 1500, 0, 100);
                        } else {
                            DirectDrawHook::AddMessage("Swap Failed: Can't read positions", "SYSTEM", RGB(255,100,100), 1500, 0, 100);
                        }
                    };
                    if (swapEnabled && swapKey > 0 && IsKeyPressed(swapKey, false)) {
                        if (GetCurrentGameMode() == GameMode::Practice) {
                            doSwap(); didSwap = true; keyHandled = true;
                        } else {
                            DirectDrawHook::AddMessage("Swap available only in Practice", "SYSTEM", RGB(255,180,120), 1200, 0, 110);
                            didSwap = true; keyHandled = true;
                        }
                    }
                    if (didSwap) {
                        Sleep(80);
                    }
                }
                if (IsKeyPressed(cfg.switchPlayersKey > 0 ? cfg.switchPlayersKey : 'L', false)) {
                auto st = MacroController::GetState();
                if (Mission::Engine::Recorder::IsSessionActive() ||
                    st == MacroController::State::PreRecord || st == MacroController::State::Recording ||
                    st == MacroController::State::Replaying) {
                    DirectDrawHook::AddMessage("Switch Players disabled during Macro PreRecord/Recording/Playback", "SYSTEM", RGB(255,200,120), 1200, 0, 100);
                    keyHandled = true;
                } else if (GetCurrentGameMode() == GameMode::Practice && !g_guiActive.load()) {
                    PostToggleLocalSide();
                    keyHandled = true;
                }
            } else if (IsKeyPressed(savestateSaveKey, false)) {
                if (Mission::Engine::Runner::IsActive()) {
                    DirectDrawHook::AddMessage(
                        "Savestate save disabled while a mission owns its checkpoint",
                        "SAVESTATE", RGB(255, 180, 120), 1500, 0, 100);
                } else if (GetCurrentGameMode() == GameMode::Practice
                        && GetCurrentGamePhase() == GamePhase::Match
                        && AreCharactersInitialized()) {
                    if (!GameThread::Post(&SavestateSaveCommand)) {
                        DirectDrawHook::AddMessage("Savestate save unavailable (Revival not ready)", "SAVESTATE", RGB(255, 180, 120), 1200, 0, 100);
                    }
                } else {
                    DirectDrawHook::AddMessage("Savestate save is available only during Practice Match", "SAVESTATE", RGB(255, 180, 120), 1200, 0, 100);
                }
                keyHandled = true;
            } else if (IsKeyPressed(savestateLoadKey, false)) {
                if (Mission::Engine::Runner::IsActive()) {
                    DirectDrawHook::AddMessage(
                        "Savestate load disabled while a mission owns its checkpoint",
                        "SAVESTATE", RGB(255, 180, 120), 1500, 0, 100);
                } else if (GetCurrentGameMode() == GameMode::Practice
                        && GetCurrentGamePhase() == GamePhase::Match
                        && AreCharactersInitialized()) {
                    if (!GameThread::Post(&SavestateLoadCommand)) {
                        DirectDrawHook::AddMessage("Savestate load unavailable (Revival not ready)", "SAVESTATE", RGB(255, 180, 120), 1200, 0, 100);
                    }
                } else {
                    DirectDrawHook::AddMessage("Savestate load is available only during Practice Match", "SAVESTATE", RGB(255, 180, 120), 1200, 0, 100);
                }
                keyHandled = true;
            } else if (IsKeyPressed(teleportKey, true)) {
                // Cancel auto-actions and macros before any position change
                CancelAutoActionsAndMacros();
                
                if (IsKeyPressed(VK_DOWN, true) && IsKeyPressed('A', true)) {
                    uintptr_t base = GetEFZBase();
                    if (base) {
                        SetPlayerPosition(base, EFZ_BASE_OFFSET_P1, p1StartX, startY);
                        SetPlayerPosition(base, EFZ_BASE_OFFSET_P2, p2StartX, startY);
                        CharacterSettings::TickCharacterEnforcements(base, displayData);
                        DirectDrawHook::AddMessage("Round Start Position", "SYSTEM", RGB(100, 255, 100), 1500, 0, 100);
                    }
                    keyHandled = true;
                }
                else if (IsKeyPressed(VK_DOWN, true)) {
                    uintptr_t base = GetEFZBase();
                    if (base) {
                        SetPlayerPosition(base, EFZ_BASE_OFFSET_P1, centerX, teleportY);
                        SetPlayerPosition(base, EFZ_BASE_OFFSET_P2, centerX, teleportY);
                        CharacterSettings::TickCharacterEnforcements(base, displayData);
                        DirectDrawHook::AddMessage("Players Centered", "SYSTEM", RGB(100, 255, 100), 1500, 0, 100);
                    }
                    keyHandled = true;
                }
                else if (IsKeyPressed(VK_LEFT, true)) {
                    uintptr_t base = GetEFZBase();
                    if (base) {
                        SetPlayerPosition(base, EFZ_BASE_OFFSET_P1, leftX, teleportY);
                        SetPlayerPosition(base, EFZ_BASE_OFFSET_P2, leftX, teleportY);
                        CharacterSettings::TickCharacterEnforcements(base, displayData);
                        DirectDrawHook::AddMessage("Left Corner", "SYSTEM", RGB(100, 255, 100), 1500, 0, 100);
                    }
                    keyHandled = true;
                }
                else if (IsKeyPressed(VK_RIGHT, true)) {
                    uintptr_t base = GetEFZBase();
                    if (base) {
                        SetPlayerPosition(base, EFZ_BASE_OFFSET_P1, rightX, teleportY);
                        SetPlayerPosition(base, EFZ_BASE_OFFSET_P2, rightX, teleportY);
                        CharacterSettings::TickCharacterEnforcements(base, displayData);
                        DirectDrawHook::AddMessage("Right Corner", "SYSTEM", RGB(100, 255, 100), 1500, 0, 100);
                    }
                    keyHandled = true;
                }
                else if (IsKeyPressed(detectedBindings.dButton != 0 ? detectedBindings.dButton : 'D', true)) {
                    uintptr_t base = GetEFZBase();
                    if (base) {
                        double tempX1 = 0.0, tempY1 = 0.0;
                        double tempX2 = 0.0, tempY2 = 0.0;
                        
                        uintptr_t xAddr1 = ResolvePointer(base, EFZ_BASE_OFFSET_P1, XPOS_OFFSET);
                        uintptr_t yAddr1 = ResolvePointer(base, EFZ_BASE_OFFSET_P1, YPOS_OFFSET);
                        uintptr_t xAddr2 = ResolvePointer(base, EFZ_BASE_OFFSET_P2, XPOS_OFFSET);
                        uintptr_t yAddr2 = ResolvePointer(base, EFZ_BASE_OFFSET_P2, YPOS_OFFSET);
                        
                        if (xAddr1 && yAddr1 && xAddr2 && yAddr2) {
                            SafeReadMemory(xAddr1, &tempX1, sizeof(double));
                            SafeReadMemory(yAddr1, &tempY1, sizeof(double));
                            SafeReadMemory(xAddr2, &tempX2, sizeof(double));
                            SafeReadMemory(yAddr2, &tempY2, sizeof(double));
                            
                            SetPlayerPosition(base, EFZ_BASE_OFFSET_P1, tempX2, tempY2);
                            SetPlayerPosition(base, EFZ_BASE_OFFSET_P2, tempX1, tempY1);
                            CharacterSettings::TickCharacterEnforcements(base, displayData);
                            DirectDrawHook::AddMessage("Positions Swapped", "SYSTEM", RGB(100, 255, 100), 1500, 0, 100);
                        } else {
                            DirectDrawHook::AddMessage("Swap Failed: Can't read positions", "SYSTEM", RGB(255,100,100), 1500, 0, 100);
                        }
                    }
                    keyHandled = true;
                }
                else {
                    uintptr_t base = GetEFZBase();
                    if (base) {
                        LoadPlayerPositions(base);
                        CharacterSettings::TickCharacterEnforcements(base, displayData);
                        DirectDrawHook::AddMessage("Position Loaded", "SYSTEM", RGB(100, 255, 100), 1500, 0, 100);
                    }
                    keyHandled = true;
                }
            } else if (IsKeyPressed(cfg.recordKey, false)) {
                uintptr_t base = GetEFZBase();
                if (base) {
                    SavePlayerPositions(base);
                    DirectDrawHook::AddMessage("Position Saved", "SYSTEM", RGB(255, 255, 100), 1500, 0, 100);
                }
                keyHandled = true;
            } else if (IsKeyPressed('4', false) || (toggleTitleKey > 0 && IsKeyPressed(toggleTitleKey, true))) {
                bool currentState = g_statsDisplayEnabled.load();
                bool nextState = !currentState;
                g_statsDisplayEnabled.store(nextState);
                if (nextState) g_statsPageIndex.store(0);

                static bool s_comboOverlayWasEnabled = false;
                if (nextState) {
                    s_comboOverlayWasEnabled = cfg.showComboStatisticsOverlay;
                    if (s_comboOverlayWasEnabled) {
                        Config::SetSetting("General", "showComboStatisticsOverlay", "0");
                    }
                    LogOut("[STATS] Stats display enabled", true);
                    DirectDrawHook::AddMessage("Stats Display Enabled", "SYSTEM", RGB(255, 255, 0), 1500, 20, 100);
                } else {
                    if (s_comboOverlayWasEnabled) {
                        Config::SetSetting("General", "showComboStatisticsOverlay", "1");
                    }
                    LogOut("[STATS] Stats display disabled", true);
                    DirectDrawHook::AddMessage("Stats Display Disabled", "SYSTEM", RGB(255, 255, 0), 1500, 20, 100);
                }
                keyHandled = true;
            } else if (g_statsDisplayEnabled.load() && IsKeyPressed('5', false)) {
                int n = g_statsPageCount.load(); if (n < 1) n = 1;
                int p = g_statsPageIndex.load();
                g_statsPageIndex.store((p - 1 + n) % n);
                keyHandled = true;
            } else if (g_statsDisplayEnabled.load() && IsKeyPressed('6', false)) {
                int n = g_statsPageCount.load(); if (n < 1) n = 1;
                int p = g_statsPageIndex.load();
                g_statsPageIndex.store((p + 1) % n);
                keyHandled = true;
            } else if (resetFrameCounterKey > 0 && IsKeyPressed(resetFrameCounterKey, false)) {
                ResetFrameCounter();
                keyHandled = true;
            } else if (cfg.framestepEnabled && IsKeyPressed(cfg.framestepPauseKey > 0 ? cfg.framestepPauseKey : VK_SPACE, false)) {
                if (Framestep::IsEnabled()) {
                    (void)GameThread::Post(&FramestepTogglePauseCommand);
                    keyHandled = true;
                }
            } else if (cfg.framestepEnabled && IsKeyPressed(cfg.framestepStepKey > 0 ? cfg.framestepStepKey : 'P', false)) {
                if (Framestep::IsEnabled()) {
                    (void)GameThread::Post(&FramestepStepCommand);
                    keyHandled = true;
                }
            } else if (IsKeyPressed(VK_F9, false)) {
                autoJumpEnabled = !autoJumpEnabled;
                DirectDrawHook::AddMessage(autoJumpEnabled ? "Auto-Jump: ON" : "Auto-Jump: OFF", "SYSTEM", RGB(255, 165, 0), 1500, 0, 100);
                keyHandled = true;
            } else if (IsKeyPressed(cfg.macroRecordKey > 0 ? cfg.macroRecordKey : 'I', false)) {
                if (GetCurrentGamePhase() == GamePhase::Match && AreCharactersInitialized()) {
                    if (Mission::Engine::Recorder::IsSessionActive()) {
                        Mission::Engine::Recorder::Advance();
                    } else {
                        MacroController::ToggleRecord();
                        DirectDrawHook::AddMessage(MacroController::GetStatusLine().c_str(), "MACRO", RGB(200, 220, 255), 900, 0, 120);
                    }
                } else {
                    DirectDrawHook::AddMessage("Macro controls available only during Match", "MACRO", RGB(255, 180, 120), 900, 0, 120);
                }
                keyHandled = true;
            } else if (IsKeyPressed(cfg.macroPlayKey > 0 ? cfg.macroPlayKey : 'O', false)) {
                if (GetCurrentGamePhase() == GamePhase::Match && AreCharactersInitialized()) {
                    if (Mission::Engine::Recorder::IsSessionActive()) {
                        DirectDrawHook::AddMessage("Macro playback is unavailable during mission authoring", "MISSION", RGB(255,200,120), 1200, 0, 120);
                    } else {
                        MacroController::Play();
                        DirectDrawHook::AddMessage(MacroController::GetStatusLine().c_str(), "MACRO", RGB(180, 255, 180), 900, 0, 120);
                    }
                } else {
                    DirectDrawHook::AddMessage("Macro controls available only during Match", "MACRO", RGB(255, 180, 120), 900, 0, 120);
                }
                keyHandled = true;
            } else if (IsKeyPressed(cfg.macroSlotKey > 0 ? cfg.macroSlotKey : 'K', false)) {
                if (GetCurrentGamePhase() == GamePhase::Match && AreCharactersInitialized()) {
                    MacroController::NextSlot();
                    DirectDrawHook::AddMessage((std::string("Macro: Slot ") + std::to_string(MacroController::GetCurrentSlot())).c_str(), "MACRO", RGB(230, 230, 120), 800, 0, 120);
                } else {
                    DirectDrawHook::AddMessage("Macro controls available only during Match", "MACRO", RGB(255, 180, 120), 900, 0, 120);
                }
                keyHandled = true;
            }
            }
            }

            if (keyHandled) {
                Sleep(100);
          while (IsKeyPressed(teleportKey, true) || IsKeyPressed(recordKey, true) ||
              (toggleTitleKey > 0 && IsKeyPressed(toggleTitleKey, true)) ||
              (resetFrameCounterKey > 0 && IsKeyPressed(resetFrameCounterKey, true)) ||
              IsKeyPressed(VK_F7, true) || IsKeyPressed(VK_F9, true) ||
              IsKeyPressed(savestateSaveKey, true) || IsKeyPressed(savestateLoadKey, true) ||
              IsKeyPressed(cfg.switchPlayersKey > 0 ? cfg.switchPlayersKey : 'L', true) ||
              IsKeyPressed(cfg.macroRecordKey > 0 ? cfg.macroRecordKey : 'I', true) ||
               IsKeyPressed(cfg.macroPlayKey > 0 ? cfg.macroPlayKey : 'O', true) ||
              (cfg.framestepEnabled && IsKeyPressed(cfg.framestepPauseKey > 0 ? cfg.framestepPauseKey : VK_SPACE, true)) ||
              (cfg.framestepEnabled && IsKeyPressed(cfg.framestepStepKey > 0 ? cfg.framestepStepKey : 'P', true))) {
                    Sleep(10);
                }
                sleepMs = 16; idleLoops = 0;
            } else {
                bool anyKbDown =
                    ((GetAsyncKeyState(teleportKey) & 0x8000) != 0) ||
                    ((GetAsyncKeyState(recordKey) & 0x8000) != 0) ||
                    ((GetAsyncKeyState(configMenuKey) & 0x8000) != 0) ||
                    (toggleTitleKey > 0 && ((GetAsyncKeyState(toggleTitleKey) & 0x8000) != 0)) ||
                    (resetFrameCounterKey > 0 && ((GetAsyncKeyState(resetFrameCounterKey) & 0x8000) != 0)) ||
                    ((GetAsyncKeyState(savestateSaveKey) & 0x8000) != 0) ||
                    ((GetAsyncKeyState(savestateLoadKey) & 0x8000) != 0) ||
                    ((GetAsyncKeyState(VK_F7) & 0x8000) != 0) ||
                    ((GetAsyncKeyState(VK_F9) & 0x8000) != 0) ||
                    ((GetAsyncKeyState(cfg.switchPlayersKey > 0 ? cfg.switchPlayersKey : 'L') & 0x8000) != 0) ||
                    ((GetAsyncKeyState(cfg.macroRecordKey > 0 ? cfg.macroRecordKey : 'I') & 0x8000) != 0) ||
                    ((GetAsyncKeyState(cfg.macroPlayKey > 0 ? cfg.macroPlayKey : 'O') & 0x8000) != 0) ||
                    (cfg.framestepEnabled && ((GetAsyncKeyState(cfg.framestepPauseKey > 0 ? cfg.framestepPauseKey : VK_SPACE) & 0x8000) != 0)) ||
                    (cfg.framestepEnabled && ((GetAsyncKeyState(cfg.framestepStepKey > 0 ? cfg.framestepStepKey : 'P') & 0x8000) != 0)) ||
                    ((GetAsyncKeyState(cfg.macroSlotKey > 0 ? cfg.macroSlotKey : 'K') & 0x8000) != 0);
                auto anyControllerActive = [&]() -> bool {
                    unsigned mask = connectedMask;
                    if (mask == 0) return false;
                    XInputShim::RefreshSnapshotOncePerFrame();
                    for (int i = 0; i < 4; ++i) {
                        if (((mask >> i) & 1u) == 0) continue;
                        XINPUT_STATE cur{};
                        if (!XInputShim::CopyCachedState(i, cur)) continue;
                        if (cur.dwPacketNumber != prevPads[i].dwPacketNumber) return true;
                        if (cur.Gamepad.wButtons != 0) return true;
                        if (cur.Gamepad.bLeftTrigger || cur.Gamepad.bRightTrigger) return true;
                    }
                    return false;
                };
                bool anyPadDown = anyControllerActive();

                if (anyKbDown || anyPadDown) {
                    sleepMs = 16; idleLoops = 0;
                } else {
                    if (++idleLoops > idleThreshold) {
                        sleepMs = 24;
                        if (idleLoops > idleThreshold * 3) sleepMs = 32;
                        if (idleLoops > idleThreshold * 6) sleepMs = 48;
                    }
                }
            }
        }

        if (!g_efzWindowActive.load() || g_guiActive.load()) {
            Sleep(64);
        } else {
            Sleep(sleepMs);
        }

    }
    
    LogOut("[KEYBINDS] Key monitoring thread exiting", true);
    keyMonitorRunning.store(false, std::memory_order_release);
    keyMonitorAlive.store(false, std::memory_order_release);
}

void RestartKeyMonitoring() {
    // CRITICAL: Never start key monitoring during online mode
    if (!XInputShim::IsPollingActive() || !IsTrainingMenuContext()) return;

    std::lock_guard<std::mutex> guard(keyMonitorMutex);
    if (keyMonitorAlive.load(std::memory_order_acquire)) {
        LogOut("[KEYBINDS] Key monitoring already running", detailedLogging.load());
        return;
    }

    p1Jumping = false;
    p2Jumping = false;

    keyMonitorAlive.store(true, std::memory_order_release);
    keyMonitorRunning.store(true);
    std::thread(MonitorKeys).detach();
}

void DebugInputs() {
    static uint8_t lastP1Input = 0;
    
    if (!IsEFZWindowActive()) return;
    
    uint8_t p1Input = GetPlayerInputs(1);
    
    if (p1Input != lastP1Input) {
        std::string inputStr = "";
        if (p1Input & INPUT_UP) inputStr += "UP ";
        if (p1Input & INPUT_DOWN) inputStr += "DOWN ";
        if (p1Input & INPUT_LEFT) inputStr += "LEFT ";
        if (p1Input & INPUT_RIGHT) inputStr += "RIGHT ";
        if (p1Input & INPUT_A) inputStr += "A ";
        if (p1Input & INPUT_B) inputStr += "B ";
        if (p1Input & INPUT_C) inputStr += "C ";
        if (p1Input & INPUT_D) inputStr += "D ";
        
        LogOut("[DEBUG] P1 Inputs: " + (inputStr.empty() ? "NONE" : inputStr), true);
        lastP1Input = p1Input;
    }
}

// Use frameCounter from utilities.cpp instead of redefining
extern std::atomic<int> frameCounter;
std::atomic<int> startFrameCount(0);

bool ReadKeyMappingsFromIni() {
    EFZInputBindings::BindingSet bindings{};
    std::string sourcePath;
    std::string error;
    if (!EFZInputBindings::LoadActiveKeyIni(bindings, sourcePath, error)) {
        LogOut("[INPUT] Failed to read active EFZ bindings - using input fallbacks: " + error, true);
        return false;
    }

    struct KeyMapping {
        EFZInputBindings::Action action;
        const char* name;
        int* bindingPtr;
    } keyMaps[] = {
        { EFZInputBindings::Action::Up, "Up", &detectedBindings.upKey },
        { EFZInputBindings::Action::Down, "Down", &detectedBindings.downKey },
        { EFZInputBindings::Action::Left, "Left", &detectedBindings.leftKey },
        { EFZInputBindings::Action::Right, "Right", &detectedBindings.rightKey },
        { EFZInputBindings::Action::A, "A (Light)", &detectedBindings.aButton },
        { EFZInputBindings::Action::B, "B (Medium)", &detectedBindings.bButton },
        { EFZInputBindings::Action::C, "C (Heavy)", &detectedBindings.cButton },
        { EFZInputBindings::Action::D, "D (Special)", &detectedBindings.dButton },
    };

    bool anyKeyboardDirection = false;
    bool anyKeyboardAttack = false;
    bool anyController = false;
    LogOut("[INPUT] Reading typed P1/P2 bindings from " + sourcePath,
           detailedLogging.load());
    const auto& p1 = bindings.players[0];
    for (std::size_t i = 0; i < sizeof(keyMaps) / sizeof(keyMaps[0]); ++i) {
        const std::size_t action = static_cast<std::size_t>(keyMaps[i].action);
        const EFZInputBindings::Binding& binding = p1.actions[action];
        int vkKey = 0;
        if (binding.valid &&
            binding.device == EFZInputBindings::DeviceKind::Keyboard) {
            vkKey = MapDIKToVK(binding.code);
            if (i < 4) anyKeyboardDirection = anyKeyboardDirection || vkKey != 0;
            else anyKeyboardAttack = anyKeyboardAttack || vkKey != 0;
        } else if (binding.valid) {
            anyController = true;
        }
        *keyMaps[i].bindingPtr = vkKey;

        if (detailedLogging.load()) {
            const char* device = !binding.valid ? "unmapped"
                : binding.device == EFZInputBindings::DeviceKind::Keyboard ? "keyboard"
                : binding.device == EFZInputBindings::DeviceKind::Joystick1 ? "joystick1"
                : "joystick2";
            LogOut("[INPUT] P1 " + std::string(keyMaps[i].name) +
                   " device=" + device + " code=" +
                   std::to_string(binding.code) +
                   (vkKey ? " vk=" + GetKeyName(vkKey) : std::string()), true);
        }
    }

    // Controller actions must never be projected onto virtual-key codes.
    detectedBindings.directionsDetected = anyKeyboardDirection;
    detectedBindings.attacksDetected = anyKeyboardAttack;
    detectedBindings.inputDevice = anyController &&
        (!anyKeyboardDirection || !anyKeyboardAttack)
        ? INPUT_DEVICE_GAMEPAD : INPUT_DEVICE_KEYBOARD;
    detectedBindings.deviceName = anyController
        ? (anyKeyboardDirection || anyKeyboardAttack
            ? "Mixed EFZ controls (from key.ini)"
            : "Controller (from key.ini)")
        : "Keyboard (from key.ini)";

    // User configs are never silently rewritten; only surface collisions in the log.
    std::array<bool, 256> reservedVk{};
    std::array<int, 24> reservedPads{};
    std::size_t reservedPadCount = 0;
    std::array<bool, 256> ignoredDik{};
    EFZInputBindings::CollectReservations(
        bindings, ignoredDik, reservedPads, reservedPadCount);
    for (const EFZInputBindings::PlayerBindings& player : bindings.players) {
        for (const EFZInputBindings::Binding& binding : player.actions) {
            if (!binding.valid ||
                binding.device != EFZInputBindings::DeviceKind::Keyboard) continue;
            const int vk = MapDIKToVK(binding.code);
            if (vk > 0 && vk < static_cast<int>(reservedVk.size())) {
                reservedVk[static_cast<std::size_t>(vk)] = true;
            }
        }
    }
    const Config::Settings& cfg = Config::GetSettings();
    struct NamedValue { const char* name; int value; };
    const NamedValue keyboardFunctions[] = {
        {"Teleport", cfg.teleportKey}, {"Record Position", cfg.recordKey},
        {"Savestate Save", cfg.savestateSaveKey},
        {"Savestate Load", cfg.savestateLoadKey},
        {"Switch Players", cfg.switchPlayersKey},
        {"Macro Record", cfg.macroRecordKey},
        {"Macro Play", cfg.macroPlayKey}, {"Macro Slot", cfg.macroSlotKey},
        {"UI Accept", cfg.uiAcceptKey}, {"UI Refresh", cfg.uiRefreshKey},
        {"UI Exit", cfg.uiExitKey},
        {"Framestep Pause", cfg.framestepPauseKey},
        {"Framestep Step", cfg.framestepStepKey},
    };
    const NamedValue padFunctions[] = {
        {"Pad Teleport", cfg.gpTeleportButton},
        {"Pad Save Position", cfg.gpSavePositionButton},
        {"Pad Switch Players", cfg.gpSwitchPlayersButton},
        {"Pad Swap Positions", cfg.gpSwapPositionsButton},
        {"Pad Macro Record", cfg.gpMacroRecordButton},
        {"Pad Macro Play", cfg.gpMacroPlayButton},
        {"Pad Macro Slot", cfg.gpMacroSlotButton},
        {"Pad Menu", cfg.gpToggleMenuButton},
        {"Pad Top Tab Previous", cfg.gpUiTopTabPrev},
        {"Pad Top Tab Next", cfg.gpUiTopTabNext},
        {"Pad Sub-tab Previous", cfg.gpUiSubTabPrev},
        {"Pad Sub-tab Next", cfg.gpUiSubTabNext},
    };
    std::ostringstream conflictSummary;
    int conflictCount = 0;
    auto appendConflict = [&](const std::string& text) {
        if (conflictCount++ != 0) conflictSummary << "; ";
        conflictSummary << text;
    };
    for (std::size_t i = 0;
         i < sizeof(keyboardFunctions) / sizeof(keyboardFunctions[0]); ++i) {
        const int value = keyboardFunctions[i].value;
        if (value > 0 && value < static_cast<int>(reservedVk.size()) &&
            reservedVk[static_cast<std::size_t>(value)]) {
            appendConflict(std::string(keyboardFunctions[i].name) +
                           " uses EFZ key " + GetKeyName(value));
        }
        if (value < 0) continue;
        for (std::size_t j = i + 1;
             j < sizeof(keyboardFunctions) / sizeof(keyboardFunctions[0]); ++j) {
            if (value == keyboardFunctions[j].value) {
                appendConflict(std::string(keyboardFunctions[i].name) +
                               " and " + keyboardFunctions[j].name +
                               " both use " + GetKeyName(value));
            }
        }
    }
    for (std::size_t i = 0;
         i < sizeof(padFunctions) / sizeof(padFunctions[0]); ++i) {
        const int value = padFunctions[i].value;
        if (value >= 0 && EFZInputBindings::Contains(
                reservedPads.data(), reservedPadCount, value)) {
            appendConflict(std::string(padFunctions[i].name) +
                           " uses EFZ control " +
                           Config::GetGamepadButtonName(value));
        }
        if (value < 0) continue;
        for (std::size_t j = i + 1;
             j < sizeof(padFunctions) / sizeof(padFunctions[0]); ++j) {
            if (value == padFunctions[j].value) {
                appendConflict(std::string(padFunctions[i].name) +
                               " and " + padFunctions[j].name +
                               " both use " +
                               Config::GetGamepadButtonName(value));
            }
        }
    }
    static std::string lastConflictSummary;
    const std::string summary = conflictSummary.str();
    if (summary != lastConflictSummary) {
        lastConflictSummary = summary;
        if (summary.empty()) {
            LogOut("[INPUT][BIND_AUDIT] EFZ P1/P2 and mod function bindings are conflict-free", true);
        } else {
            LogOut("[INPUT][BIND_AUDIT] " + std::to_string(conflictCount) +
                   " conflict(s) in existing config (not auto-rewritten): " + summary, true);
        }
    }
    return true;
}

bool ReadDirectInputKeyboardState(BYTE* keyboardState) {
    if (!XInputShim::IsPollingActive()) return false;
    if (!g_directInputAvailable || !g_pKeyboard || !keyboardState)
        return false;
    
    HRESULT hr = g_pKeyboard->GetDeviceState(256, keyboardState);
    
    if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
        hr = g_pKeyboard->Acquire();
        if (SUCCEEDED(hr)) {
            hr = g_pKeyboard->GetDeviceState(256, keyboardState);
        }
    }
    
    return SUCCEEDED(hr);
}

int MapEFZKeyToVK(unsigned short efzKey) {
    // Extended EFZ joystick encodings; not plain DIK codes.
    switch (efzKey) {
        case 0x114: return VK_RIGHT; // Joystick Right (extended)
        case 0x111: return VK_UP;    // Joystick Up
        case 0x112: return VK_DOWN;  // Joystick Down
        case 0x113: return VK_LEFT;  // Joystick Left
    }

    // Prefer centralized DI scancode -> VK mapping from di_keycodes
    int vk = MapDIKToVK(static_cast<int>(efzKey));
    if (vk != 0) return vk;

    UINT apiVk = MapVirtualKey(efzKey, MAPVK_VSC_TO_VK);
    return apiVk ? static_cast<int>(apiVk) : 0;
}



void DetectKeyBindings() {
    if (ReadKeyMappingsFromIni()) {
        return;
    }

    if (g_directInputAvailable) {
        DetectKeyBindingsWithDI();
    }
    else {
        LogOut("[INPUT] Using WinAPI for key detection", true);
    }
}

bool UpdateGamepadState(int gamepadIndex) {
    if (!XInputShim::IsPollingActive()) return false;
    if (gamepadIndex < 0 || gamepadIndex >= g_gamepadCount || !g_gamepads[gamepadIndex].device) {
        return false;
    }
    
    memcpy(&g_gamepads[gamepadIndex].prevState, &g_gamepads[gamepadIndex].state, sizeof(DIJOYSTATE2));
    
    HRESULT hr = g_gamepads[gamepadIndex].device->Poll();
    
    if (FAILED(hr)) {
        hr = g_gamepads[gamepadIndex].device->Acquire();
        if (FAILED(hr)) {
            g_gamepads[gamepadIndex].connected = false;
            return false;
        }
        
        hr = g_gamepads[gamepadIndex].device->Poll();
        if (FAILED(hr)) {
            return false;
        }
    }
    
    hr = g_gamepads[gamepadIndex].device->GetDeviceState(sizeof(DIJOYSTATE2), &g_gamepads[gamepadIndex].state);
    if (FAILED(hr)) {
        return false;
    }
    
    return true;
}

// GetDIKeyName is implemented centrally in di_keycodes.cpp

void DetectKeyBindingsWithDI() {
    if (!XInputShim::IsPollingActive()) return;
    static BYTE prevKeyboardState[256] = {0};
    BYTE keyboardState[256] = {0};
    
    bool keyboardActive = ReadDirectInputKeyboardState(keyboardState);
    
    bool anyGamepadActive = false;
    for (int i = 0; i < g_gamepadCount; i++) {
        if (UpdateGamepadState(i)) {
            anyGamepadActive = true;
        }
    }
    
    uint8_t currentInputs = GetPlayerInputs(1);
    static uint8_t prevInputs = 0;
    
    if (!detectedBindings.directionsDetected) {
        if (keyboardActive) {
            for (int key = 0; key < 256; key++) {
                if ((keyboardState[key] & 0x80) && !(prevKeyboardState[key] & 0x80)) {
                    if ((currentInputs & INPUT_UP) && !(prevInputs & INPUT_UP)) {
                        detectedBindings.upKey = key;
                        detectedBindings.inputDevice = INPUT_DEVICE_KEYBOARD;
                        detectedBindings.deviceName = "Keyboard";
                        LogOut("[KEYBIND] Detected Up key (Keyboard): " + GetDIKeyName(key), detailedLogging);
                    }
                    if ((currentInputs & INPUT_DOWN) && !(prevInputs & INPUT_DOWN)) {
                        detectedBindings.downKey = key;
                        detectedBindings.inputDevice = INPUT_DEVICE_KEYBOARD;
                        detectedBindings.deviceName = "Keyboard";
                        LogOut("[KEYBIND] Detected Down key (Keyboard): " + GetDIKeyName(key), detailedLogging);
                    }
                    if ((currentInputs & INPUT_LEFT) && !(prevInputs & INPUT_LEFT)) {
                        detectedBindings.leftKey = key;
                        detectedBindings.inputDevice = INPUT_DEVICE_KEYBOARD;
                        detectedBindings.deviceName = "Keyboard";
                        LogOut("[KEYBIND] Detected Left key (Keyboard): " + GetDIKeyName(key), detailedLogging);
                    }
                    if ((currentInputs & INPUT_RIGHT) && !(prevInputs & INPUT_RIGHT)) {
                        detectedBindings.rightKey = key;
                        detectedBindings.inputDevice = INPUT_DEVICE_KEYBOARD;
                        detectedBindings.deviceName = "Keyboard";
                        LogOut("[KEYBIND] Detected Right key (Keyboard): " + GetDIKeyName(key), detailedLogging);
                    }
                }
            }
        }

        bool leftPressed = ((currentInputs & INPUT_LEFT) != 0);
        if ((GetAsyncKeyState(VK_LEFT) & 0x8000) != 0) {
            leftPressed = true;
        }
        
        bool rightPressed = ((currentInputs & INPUT_RIGHT) != 0);
        if ((GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0) {
            rightPressed = true;
        }
        
        bool upPressed = ((currentInputs & INPUT_UP) != 0);
        if ((GetAsyncKeyState(VK_UP) & 0x8000) != 0) {
            upPressed = true;
        }
        
        bool downPressed = ((currentInputs & INPUT_DOWN) != 0);
        if ((GetAsyncKeyState(VK_DOWN) & 0x8000) != 0) {
            downPressed = true;
        }
        
        bool aPressed = ((currentInputs & INPUT_A) != 0);
        if ((GetAsyncKeyState('Z') & 0x8000) != 0) {
            aPressed = true;
        }
        
        if (detectedBindings.directionsDetected) {
            if ((GetAsyncKeyState(detectedBindings.leftKey) & 0x8000) != 0) {
                leftPressed = true;
            }
            if ((GetAsyncKeyState(detectedBindings.rightKey) & 0x8000) != 0) {
                rightPressed = true;
            }
            if ((GetAsyncKeyState(detectedBindings.upKey) & 0x8000) != 0) {
                upPressed = true;
            }
            if ((GetAsyncKeyState(detectedBindings.downKey) & 0x8000) != 0) {
                downPressed = true;
            }
            
            if (detectedBindings.attacksDetected) {
                if ((GetAsyncKeyState(detectedBindings.aButton) & 0x8000) != 0) {
                    aPressed = true;
                }
            }
        }
        
        if (anyGamepadActive) {
            for (int i = 0; i < g_gamepadCount; i++) {
                if (!g_gamepads[i].connected) continue;
                
                bool gpLeftPressed = (g_gamepads[i].state.lX < -JOYSTICK_DEADZONE) && 
                                    !(g_gamepads[i].prevState.lX < -JOYSTICK_DEADZONE);
                bool gpRightPressed = (g_gamepads[i].state.lX > JOYSTICK_DEADZONE) && 
                                     !(g_gamepads[i].prevState.lX > JOYSTICK_DEADZONE);
                bool gpUpPressed = (g_gamepads[i].state.lY < -JOYSTICK_DEADZONE) && 
                                  !(g_gamepads[i].prevState.lY < -JOYSTICK_DEADZONE);
                bool gpDownPressed = (g_gamepads[i].state.lY > JOYSTICK_DEADZONE) && 
                                    !(g_gamepads[i].prevState.lY > JOYSTICK_DEADZONE);
                
                if (g_gamepads[i].state.rgdwPOV[0] != 0xFFFFFFFF) {
                    DWORD pov = g_gamepads[i].state.rgdwPOV[0];
                    DWORD prevPov = g_gamepads[i].prevState.rgdwPOV[0];
                    
                    if ((pov >= 31500 || pov <= 00) && (prevPov < 31500 && prevPov > 00)) gpUpPressed = true;
                    if ((pov >= 00 && pov <= 13500) && (prevPov < 00 || prevPov > 13500)) gpRightPressed = true;
                    if ((pov >= 13500 && pov <= 200) && (prevPov < 13500 || prevPov > 200)) gpDownPressed = true;
                    if ((pov >= 200 && pov <= 31500) && (prevPov < 200 || prevPov > 31500)) gpLeftPressed = true;
                }
                
                if (gpLeftPressed && (currentInputs & INPUT_LEFT)) {
                    detectedBindings.leftKey = VK_GAMEPAD_DPAD_LEFT;
                    detectedBindings.inputDevice = INPUT_DEVICE_GAMEPAD;
                    detectedBindings.gamepadIndex = i;
                    detectedBindings.deviceName = "Gamepad " + std::to_string(i + 1);
                    leftPressed = true;
                    LogOut("[KEYBIND] Detected Left input (Gamepad " + std::to_string(i + 1) + ")", detailedLogging);
                }
                if (gpRightPressed && (currentInputs & INPUT_RIGHT)) {
                    detectedBindings.rightKey = VK_GAMEPAD_DPAD_RIGHT;
                    detectedBindings.inputDevice = INPUT_DEVICE_GAMEPAD;
                    detectedBindings.gamepadIndex = i;
                    detectedBindings.deviceName = "Gamepad " + std::to_string(i + 1);
                    rightPressed = true;
                    LogOut("[KEYBIND] Detected Right input (Gamepad " + std::to_string(i + 1) + ")", detailedLogging);
                }
                if (gpUpPressed && (currentInputs & INPUT_UP)) {
                    detectedBindings.upKey = VK_GAMEPAD_DPAD_UP;
                    detectedBindings.inputDevice = INPUT_DEVICE_GAMEPAD;
                    detectedBindings.gamepadIndex = i;
                    detectedBindings.deviceName = "Gamepad " + std::to_string(i + 1);
                    upPressed = true;
                    LogOut("[KEYBIND] Detected Up input (Gamepad " + std::to_string(i + 1) + ")", detailedLogging);
                }
                if (gpDownPressed && (currentInputs & INPUT_DOWN)) {
                    detectedBindings.downKey = VK_GAMEPAD_DPAD_DOWN;
                    detectedBindings.inputDevice = INPUT_DEVICE_GAMEPAD;
                    detectedBindings.gamepadIndex = i;
                    detectedBindings.deviceName = "Gamepad " + std::to_string(i + 1);
                    downPressed = true;
                    LogOut("[KEYBIND] Detected Down input (Gamepad " + std::to_string(i + 1) + ")", detailedLogging);
                }
            }
        }

        if (leftPressed) {
            if (aPressed) {
                detectedBindings.attacksDetected = true;
                LogOut("[KEYBIND] Detected A button from Left+A combo", detailedLogging);
            }
            
            if (!detectedBindings.directionsDetected) {
                detectedBindings.directionsDetected = true;
                LogOut("[KEYBIND] Direction keys detected", true);
            }
        }
        
        if (rightPressed || upPressed || downPressed) {
            if (!detectedBindings.directionsDetected) {
                detectedBindings.directionsDetected = true;
                LogOut("[KEYBIND] Direction keys detected", true);
            }
        }
    }
    
    if (detectedBindings.directionsDetected && !detectedBindings.attacksDetected) {
        if ((currentInputs & INPUT_A) && !(prevInputs & INPUT_A)) {
            if (keyboardActive) {
                for (int key = 0; key < 256; key++) {
                    if ((keyboardState[key] & 0x80) && !(prevKeyboardState[key] & 0x80)) {
                        detectedBindings.aButton = key;
                        LogOut("[KEYBIND] Detected A button (Keyboard): " + GetDIKeyName(key), detailedLogging);
                        detectedBindings.attacksDetected = true;
                        break;
                    }
                }
            }
            
            if (anyGamepadActive && !detectedBindings.attacksDetected) {
                for (int i = 0; i < g_gamepadCount; i++) {
                    if (!g_gamepads[i].connected) continue;
                    
                    for (int btn = 0; btn < 32; btn++) {
                        if ((g_gamepads[i].state.rgbButtons[btn] & 0x80) && 
                            !(g_gamepads[i].prevState.rgbButtons[btn] & 0x80)) {
                            detectedBindings.aButton = VK_GAMEPAD_A + btn;
                            detectedBindings.inputDevice = INPUT_DEVICE_GAMEPAD;
                            detectedBindings.gamepadIndex = i;
                            detectedBindings.deviceName = "Gamepad " + std::to_string(i + 1);
                            detectedBindings.attacksDetected = true;
                            LogOut("[KEYBIND] Detected A button (Gamepad " + std::to_string(i + 1) + 
                                   " button " + std::to_string(btn) + ")", detailedLogging);
                            break;
                        }
                    }
                    
                    if (detectedBindings.attacksDetected) break;
                }
            }
        }
    
    }
    
    if (keyboardActive) {
        memcpy(prevKeyboardState, keyboardState, sizeof(prevKeyboardState));
    }
    prevInputs = currentInputs;
}

