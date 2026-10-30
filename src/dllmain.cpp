#include <fstream>
#include <iomanip>
#include <chrono>
#include <vector>
#include <windows.h>
#include <thread>
#include "runtime/practice_runtime.h"
#include "runtime/practice_worker.h"
#include "game/character_hotswap.h"
#include "utils/minhook_utils.h"
#include "../include/utils/xp_compat.h"
#include "../include/core/memory.h"
#include "../include/utils/utilities.h"
#include "../include/gui/framebar.h"
#include "../include/input/input_buffer.h"
#include "../include/core/logger.h"
#include "../include/game/frame_monitor.h"
#include "../include/utils/network.h"
#include "../include/core/di_keycodes.h"
#include "../include/input/input_handler.h"
#include "../include/gui/overlay.h"
#include "../include/gui/imgui_impl.h"
#include "../include/gui/imgui_gui.h"
#include "../include/utils/config.h"
#include "../include/game/practice_patch.h"
#include "../include/game/final_memory_patch.h"
#include "../include/game/timer_freeze_patch.h"
#include "../include/game/auto_action.h"  
#include "../include/input/input_hook.h" 
#include "../3rdparty/minhook/include/MinHook.h" 
#include "../include/utils/bgm_control.h"
#include "../include/utils/audio_control.h"
#include "../include/utils/extended_config_bridge.h"
#include "../include/game/game_state.h"
#include "../include/core/globals.h"  
#include "../include/game/collision_hook.h"
#include "../include/game/hud_disable.h"
#include "../include/game/collision_display.h"
#include "../include/game/practice_hotkey_gate.h"
#include "../include/game/practice_offsets.h"
#include "../include/utils/crash_handler.h"
#include "../include/utils/debug_log.h"
#include "../include/game/efzrevival_addrs.h"
#include "../include/input/framestep.h"
#include "../include/game/savestate_hook.h"
#include "../include/game/practice_menu/practice_menu.h"
// forward declaration for overlay gate
namespace PracticeOverlayGate { void EnsureInstalled(); void SetMenuVisible(bool); }
#pragma comment(lib, "winmm.lib")

// Forward declarations for functions in other files
void MonitorKeys();
void FrameDataMonitor();
void UpdateConsoleTitle();
void WriteStartupLog(const std::string& message);
extern std::atomic<bool> inStartupPhase;

void InitializeConfig();
void DelayedInitialization(HMODULE hModule);

extern std::atomic<bool> g_isShuttingDown;  // Reference the one defined in globals.cpp
std::atomic<bool> g_initialized(false);
std::atomic<bool> g_featuresEnabled(false);

static void WriteEarlyLoaderTrace(const char* message) {
#if defined(EFZ_XP_COMPAT)
    char path[MAX_PATH] = {0};
    DWORD len = GetModuleFileNameA(NULL, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return;
    }

    char* slash = path + len;
    while (slash > path && *slash != '\\' && *slash != '/') {
        --slash;
    }
    if (*slash == '\\' || *slash == '/') {
        *(slash + 1) = '\0';
    }

    const char* traceName = "efz_loader_trace.log";
    char fullPath[MAX_PATH] = {0};
    lstrcpynA(fullPath, path, MAX_PATH);
    lstrcpynA(fullPath + lstrlenA(fullPath), traceName, MAX_PATH - lstrlenA(fullPath));

    HANDLE hFile = CreateFileA(fullPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return;
    }

    SYSTEMTIME st{};
    GetLocalTime(&st);
    char line[512] = {0};
    wsprintfA(line, "[%02u:%02u:%02u.%03u] %s\r\n",
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, message);
    DWORD bytesWritten = 0;
    WriteFile(hFile, line, static_cast<DWORD>(lstrlenA(line)), &bytesWritten, nullptr);
    CloseHandle(hFile);
#else
    (void)message;
#endif
}

static DWORD WINAPI DelayedInitializationThreadProc(LPVOID param) {
    WriteEarlyLoaderTrace("DelayedInitializationThreadProc entered");
    DelayedInitialization(static_cast<HMODULE>(param));
    return 0;
}

void DelayedInitialization(HMODULE hModule) {
    try {
        WriteStartupLog("Delayed initialization thread entered");
        WriteStartupLog("Starting early initialization");

        // Initialize logging system (starts title updater thread)
        WriteStartupLog("Initializing logging system...");
        InitializeLogging();
        WriteStartupLog("Logging system initialized");
        WriteStartupLog(XPCompat::GetRuntimeSummary());
        LogOut(XPCompat::GetRuntimeSummary(), true);

        // Initialize configuration system first so we can gate file logging
        InitializeConfig();

        // Gate startup log file behind detailedLogging flag (messages before this point are still logged)
        if (!Config::GetSettings().detailedLogging) {
            SetStartupLogEnabled(false);
        }

    // Gate file debug logging behind dedicated config flag (separate from console verbosity)
    DebugLog::g_EnableDebugLog = Config::GetSettings().enableDebugFileLog;
        if (DebugLog::g_EnableDebugLog) {
            WriteStartupLog("Initializing debug log file (enabled by config)...");
        } else {
            WriteStartupLog("Debug log file disabled by config");
        }
        DebugLog::Initialize();
        WriteStartupLog("Debug log initialization step complete");

        if (Config::GetSettings().enableConsole) {
            WriteStartupLog("Creating debug console as per settings...");
            CreateDebugConsole();
            if (HWND consoleWnd = GetConsoleWindow()) {
                ShowWindow(consoleWnd, SW_SHOW);
            }
        } else {
            // Ensure any inherited console is hidden; logs will be buffered
            if (HWND consoleWnd = GetConsoleWindow()) {
                ShowWindow(consoleWnd, SW_HIDE);
            }
            SetConsoleReady(false);
        }

        // Initialize MinHook once for the entire application.
        if (MH_Initialize() != MH_OK) {
            LogOut("[SYSTEM] MinHook initialization failed. Hooks will not be installed.", true);
            inStartupPhase = false; // ensure we don't stay stuck in startup state
            return; // Early exit if MinHook fails
        }
        LogOut("[SYSTEM] MinHook initialized successfully.", true);
        Practice::BindRuntimeResources(MinHookUtils::OwnedHooks(),Practice::NativePatchLedger());
        Practice::BindLoadingRequestConsumer(CharacterHotswap::CaptureLoadingRequest,CharacterHotswap::ConsumeLoadingRequest);
        Practice::BindMeasurementResetConsumer(Practice::ResetPracticeMeasurements);
        Practice::BindInputRetirementConsumer(Practice::CapturePracticeInputBaseline,Practice::RetirePracticeInput,Practice::CancelPracticeInputWork);
        Practice::RegisterExistingPracticeProvider();

        // Default builds leave EFZ's title screen and vanilla Practice entry untouched.
        // Opting in also enables the title update detour, Practice case patch, and
        // direct-Loading bootstrap used exclusively by that flow.
#if defined(EFZ_ENABLE_EXPERIMENTAL_TRIAL_TUTORIAL)
        {
            constexpr DWORD kTitleHookWindowMs = 2000;
            constexpr DWORD kTitleHookRetryMs = 50;
            const DWORD titleWaitStart = GetTickCount();
            bool titleHooked = false;
            while ((GetTickCount() - titleWaitStart) < kTitleHookWindowMs) {
                try {
                    if (PracticeMenu::Install()) { titleHooked = true; break; }
                } catch (...) {
                    LogOut("[SYSTEM] Exception while installing practice menu hook (early).", true);
                    break;
                }
                Sleep(kTitleHookRetryMs);
            }
            if (titleHooked) {
                LogOut("[PRACTICE_MENU] Title hook installed early (pre-stabilize)", true);
            } else {
                LogOut("[PRACTICE_MENU] Title hook not installed within early window (unrecognized title build?)", true);
            }
        }
#else
        LogOut("[PRACTICE_MENU] Title patch and trial/tutorial modes are disabled for this build", true);
#endif

        bool audioHooksReady = false;
        AudioControl::HookInstallResult audioHookResult = AudioControl::HookInstallResult::Failed;
        // Install only the four EFZ audio hooks Revival does not own.
        // playSoundBuffer/setSoundVolume must remain pristine until the delayed
        // host/version resolution below, or Revival can copy MinHook's relative
        // JMP into its own trampoline without relocating it.
        try {
            const uintptr_t efzBase = GetEFZBase();
            audioHookResult = AudioControl::InstallHooks(
                efzBase,
                AudioControl::HookInstallPhase::CommonOnly);
            LogOut(std::string("[AUDIO] Early common-hook phase result=")
                   + AudioControl::HookInstallResultName(audioHookResult)
                   + "; contested audio ownership remains deferred", true);
        } catch (...) {
            LogOut("[AUDIO] Exception while installing early non-conflicting audio hooks.", true);
        }

        // Do not touch Revival's two contested EFZ entrypoints anywhere in this window.
        {
            constexpr DWORD kStartupStabilizeDelayMs = 1500;
            Sleep(kStartupStabilizeDelayMs);
        }
        WriteStartupLog("Starting delayed initialization");
        
        // Initialize framestep system (vanilla / supported Revival)
        Framestep::Initialize();
        CollisionDisplay::Initialize();

        // Suppress EFZ DirectInput battle hotkeys while our menu is open.
        EnsureFrontendControlHooksInstalled();

        // Attempt to install Practice hotkey gate (will succeed only after EfzRevival.dll present)
        try {
            if (PracticeHotkeyGate::Install()) {
                LogOut("[HOTKEY] Practice hotkey gate active (menu suppression)", true);
            } else {
                LogOut("[HOTKEY] Practice hotkey gate not installed yet (EfzRevival may not be loaded)", true);
            }
            // Also install overlay toggle hooks (will silently do nothing if module not loaded yet)
            PracticeOverlayGate::EnsureInstalled();
        } catch (...) {
            LogOut("[HOTKEY] Exception while installing practice hotkey gate", true);
        }

        // Revival savestate tracking; the custom savestate backend is not reached from here.
        try {
            const bool revivalReady = SavestateHook::Install();
            LogOut(std::string("[SAVESTATE] Revival hooks=") + (revivalReady ? "ready" : "not-ready"), true);
        } catch (...) {
            LogOut("[SAVESTATE] Exception while initializing savestate systems", true);
        }

        RefreshNetplayRuntimeState();
        {
            NetplayRuntimeState state = GetNetplayRuntimeState();
            std::ostringstream oss;
            oss << "[NETPLAY] Startup snapshot"
                << " source=" << NetplayStateSourceName(state.source)
                << " suspend=" << (state.suspendTraining ? "1" : "0")
                << " session=" << (state.sessionActive ? "1" : "0")
                << " menu=" << (state.inNetplayMenu ? "1" : "0")
                << " flow=" << (state.inNetplayFlow ? "1" : "0");
            if (state.exportAvailable) {
                oss << " mode=" << state.exportState.sessionMode
                    << " phase=" << state.exportState.sessionPhase
                    << " activity=" << static_cast<int>(state.exportState.activityPhase);
            } else {
                oss << " legacy=" << OnlineStateName(state.legacyOnlineState);
            }
            oss << " reason=" << GetLastOnlineDetectionReason();
            LogOut(oss.str(), true);
        }
        if (IsNetplaySuspendActive()) {
            LogOut("[NETPLAY] Startup entering suspended mode", true);
            EnterNetplaySuspend();
        } else {
            LogOut("[NETPLAY] Startup entering active local mode", true);
        }

        try {
            InstallInputHook();
        } catch (...) {
            LogOut("[SYSTEM] Exception while installing input hook.", true);
        }
        try {
            InstallCollisionHook();
        } catch (...) {
            LogOut("[SYSTEM] Exception while installing collision hook.", true);
        }
        try {
            HudDisable::Install();
        } catch (...) {
            LogOut("[SYSTEM] Exception while installing HUD-disable hook.", true);
        }
        // When experimental trial/tutorial support is enabled, its title hook is
        // installed in the early monitored window immediately after MH_Initialize.
        try {
            if (!audioHooksReady) {
                const uintptr_t efzBase = GetEFZBase();
                audioHookResult = AudioControl::InstallHooks(
                    efzBase,
                    AudioControl::HookInstallPhase::HostResolved);
                for (int attempt = 0;
                     audioHookResult == AudioControl::HookInstallResult::Deferred && attempt < 20;
                     ++attempt) {
                    Sleep(50);
                    audioHookResult = AudioControl::InstallHooks(
                        efzBase,
                        AudioControl::HookInstallPhase::HostResolved);
                }
                audioHooksReady = audioHookResult == AudioControl::HookInstallResult::Ready;
                if (audioHooksReady) {
                    LogOut("[AUDIO] Runtime audio ownership resolved and hooks are ready", true);
                } else {
                    LogOut(std::string("[AUDIO] Runtime audio ownership result=")
                           + AudioControl::HookInstallResultName(audioHookResult), true);
                }
            }

            // The four common hooks and the absolute DirectSound volume sync do
            // not depend on ownership of playSoundBuffer/setSoundVolume.  Keep
            // them active while a late Revival host is deliberately deferred.
            if (audioHookResult != AudioControl::HookInstallResult::Suppressed) {
                bool audioReady = AudioControl::EnableVolumeApplicationIfSoundReady(0, "delayed initialization");
                for (int attempt = 0; !audioReady && attempt < 20; ++attempt) {
                    Sleep(50);
                    audioReady = AudioControl::EnableVolumeApplicationIfSoundReady(0, "delayed initialization poll");
                }

                if (audioReady) {
                    AudioControl::ApplyConfiguredVolumesNow();
                } else {
                    LogOut("[AUDIO] Runtime volume sync remains deferred; EFZ sound buffers are not ready yet"
                           " (contested hook ownership may still be pending).", true);
                }
            }
        } catch (...) {
            LogOut("[AUDIO] Exception while applying runtime audio settings.", true);
        }
        try {
            StartBGMSuppressionPoller();
        } catch (...) {
            LogOut("[SYSTEM] Exception while starting BGM suppression poller.", true);
        }

        // Safety baseline: if a previous injected session left FM bypass patched,
        // restore the original HP checks before this runtime decides whether to
        // reapply it for local practice.
        ForceRestoreFinalMemoryHPBypass("startup baseline");
        TimerFreeze::ForceRestore("startup baseline");

        // Final Memory HP bypass is manual via the Debug tab.

        LogOut("[SYSTEM] EfzExtendedPractice - Delayed initialization starting", true);
        LogOut("[SYSTEM] Console initialized with code page: " + std::to_string(GetConsoleOutputCP()), true);
        LogOut("[SYSTEM] Current locale: C", true);

    LogOut("[SYSTEM] Starting background threads...", true);
    // UpdateConsoleTitle thread is already started by InitializeLogging(); don't start a duplicate here.
    if(Practice::LegacyMonitorStartupRequired()) (void)Practice::StartMonitorThread();
    std::thread(LifecycleWatcherThread).detach();
        LogOut("[SYSTEM] Essential background threads started.", true);

        // Keyboard hotkeys stay on WinAPI; controller input prefers XInput with
        // a DirectInput fallback handled inside XInputShim.
        WriteStartupLog("Using WinAPI keyboard input path; controller input prefers XInput with DirectInput fallback");
        g_directInputAvailable = false;

        WriteStartupLog("Reading key.ini file...");
        ReadKeyMappingsFromIni();
        WriteStartupLog("Key mappings read");

        LogOut("EfzExtendedPractice initialized successfully", true);
        WriteStartupLog("Delayed initialization complete");

    std::thread([]{
            Sleep(2000); // Give the game a moment to be fully ready
            try {
                if (DirectDrawHook::InitializeD3D9()) {
                    LogOut("[SYSTEM] D3D9 Overlay system initialized.", true);
                } else if (DirectDrawHook::WasLastD3D9InitDeferredForNetplay()) {
                    LogOut("[SYSTEM] D3D9 overlay initialization deferred because netplay suspend is active; resume path will retry.", true);
                } else {
                    LogOut("[SYSTEM] Failed to initialize D3D9 Overlay system.", true);
                    static bool s_warnedNoD3D9 = false;
                    if (!s_warnedNoD3D9) {
                        s_warnedNoD3D9 = true;
                        // Show a one-time guidance message to help users (esp. on Linux/Wine)
                        const char* msg =
                            "EfzExtendedPractice: D3D9 overlay not detected.\n\n"
                            "This disables on-screen overlays (frame advantage, triggers, etc.).\n\n"
                            "If you're running under Linux/Wine: open winecfg and add an override for ddraw.dll\n"
                            "(set it to native, then builtin), or set WINEDLLOVERRIDES=ddraw=n,b before launching the game.\n\n"
                            "If you're on Windows: ensure d3d9.dll is available and not blocked by overlays from other apps.";
                        MessageBoxA(FindEFZWindow(), msg, "EfzExtendedPractice", MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
                    }
                }
            } catch (...) {
                LogOut("[SYSTEM] Exception during D3D9 overlay initialization.", true);
            }
        }).detach();

        g_initialized = true;
        inStartupPhase = false;

    // RF freeze is maintained inline by FrameDataMonitor.
    } catch (...) {
        LogOut("[SYSTEM] Exception during DelayedInitialization (top-level catch).", true);
        inStartupPhase = false;
    }
}

void InitializeConfig() {
    LogOut("[SYSTEM] Initializing configuration system...", true);
    if (Config::Initialize()) {
        LogOut("[SYSTEM] Configuration loaded successfully", true);
        ExtendedConfigBridge::InitializeAudioControl();
        ExtendedConfigBridge::ImportAudioSettingsIfAvailable(true);

        detailedLogging = Config::GetSettings().detailedLogging;
    // Keep file debug logging in sync with config flag
    DebugLog::g_EnableDebugLog = Config::GetSettings().enableDebugFileLog;
    // FrameBar overlay enable mirror
    FrameBar::g_enabled.store(Config::GetSettings().showFrameBar);
    // Console visibility will be handled post-init in DelayedInitialization
    }
    else {
        LogOut("[SYSTEM] Failed to initialize configuration, using defaults", true);
        ExtendedConfigBridge::InitializeAudioControl();
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
    // Module handle for any future runtime services that need it.
    g_hSelfModule = hModule;
        CrashHandler::Install(hModule);
        WriteEarlyLoaderTrace("DLL_PROCESS_ATTACH reached");
        // The XP build links the static CRT (/MT). Microsoft explicitly
        // disallows DisableThreadLibraryCalls for that configuration; leave
        // normal thread notifications enabled instead of relying on a failed
        // suppression call.
        if (HANDLE initThread = CreateThread(nullptr, 0, DelayedInitializationThreadProc, hModule, 0, nullptr)) {
            WriteEarlyLoaderTrace("Delayed initialization thread created");
            CloseHandle(initThread);
        } else {
            WriteEarlyLoaderTrace("Failed to create delayed initialization thread");
            OutputDebugStringA("[EFZ_TM][XP] Failed to create delayed initialization thread.\n");
        }
        break;
    case DLL_PROCESS_DETACH:
        ExtendedConfigBridge::SignalAudioControlStop();
        // Signal-only during process termination. Windows is already reclaiming
        // process resources and DllMain holds the loader lock; waiting for
        // workers, flushing streams, or asking MinHook/DirectX to unload here
        // can deadlock the exit path.
        g_isShuttingDown = true;
        g_featuresEnabled = false;
        // At termination other threads are gone and may have died holding a mutex.
        if (lpReserved != nullptr) {
            break;
        }
        Practice::RequestMonitorStop();

        // Explicit FreeLibrary remains a legacy best-effort path. A future
        // injector-facing shutdown API must quiesce and join every worker before
        // calling FreeLibrary; do not use this branch as that lifecycle API.
        DebugLog::Shutdown();

        // CRITICAL: Stop buffer freezing FIRST
        StopBufferFreezingIgnoringTutorialLease();
        ForceRestoreFinalMemoryHPBypass("DLL_PROCESS_DETACH");
        TimerFreeze::ForceRestore("DLL_PROCESS_DETACH");

        // Then restore P2 control
        if (g_p2ControlOverridden) {
            RestoreP2ControlState();
        }

        try {
            PracticeMenu::Uninstall();
            RemoveInputHook();
            RemoveCollisionHook();
            HudDisable::Remove();
            CollisionDisplay::Shutdown();
            StopBGMSuppressionPoller();
            SavestateHook::Uninstall();
            if (g_guiActive.load()) {
                g_guiActive = false;
            }
        } catch (...) {
            // Suppress exceptions during shutdown
        }

        // Give threads a moment to clean up
        Sleep(100);

        // Revival callback hooks hold an explicit module reference. Remove
        // those hooks first, but never call FreeLibrary while DllMain owns the
        // loader lock; retain the reference safely until process exit.
        AudioControl::ShutdownHooks(false);

        MH_Uninitialize();
        break;
    }
    return TRUE;
}
