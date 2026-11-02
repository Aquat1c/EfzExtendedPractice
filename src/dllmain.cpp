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
namespace PracticeOverlayGate { void EnsureInstalled(); void SetMenuVisible(bool); }
#pragma comment(lib, "winmm.lib")

void MonitorKeys();
void FrameDataMonitor();
void UpdateConsoleTitle();
void WriteStartupLog(const std::string& message);
extern std::atomic<bool> inStartupPhase;

void InitializeConfig();
void DelayedInitialization(HMODULE hModule);

extern std::atomic<bool> g_isShuttingDown;
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

        WriteStartupLog("Initializing logging system...");
        InitializeLogging();
        WriteStartupLog("Logging system initialized");
        WriteStartupLog(XPCompat::GetRuntimeSummary());
        LogOut(XPCompat::GetRuntimeSummary(), true);

        InitializeConfig();

        if (!Config::GetSettings().detailedLogging) {
            SetStartupLogEnabled(false);
        }

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
            if (HWND consoleWnd = GetConsoleWindow()) {
                ShowWindow(consoleWnd, SW_HIDE);
            }
            SetConsoleReady(false);
        }

        if (MH_Initialize() != MH_OK) {
            LogOut("[SYSTEM] MinHook initialization failed. Hooks will not be installed.", true);
            inStartupPhase = false;
            return;
        }
        LogOut("[SYSTEM] MinHook initialized successfully.", true);
        Practice::BindRuntimeResources(MinHookUtils::OwnedHooks(),Practice::NativePatchLedger());
        Practice::BindLoadingRequestConsumer(CharacterHotswap::CaptureLoadingRequest,CharacterHotswap::ConsumeLoadingRequest);
        Practice::BindMeasurementResetConsumer(Practice::ResetPracticeMeasurements);
        Practice::BindInputRetirementConsumer(Practice::CapturePracticeInputBaseline,Practice::RetirePracticeInput,Practice::CancelPracticeInputWork);
        Practice::RegisterExistingPracticeProvider();

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
        // Only the four audio hooks Revival does not own. playSoundBuffer/setSoundVolume stay untouched until
        // host resolution below, or Revival can copy MinHook's relative JMP without relocating it.
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
        
        Framestep::Initialize();
        CollisionDisplay::Initialize();

        EnsureFrontendControlHooksInstalled();

        try {
            if (PracticeHotkeyGate::Install()) {
                LogOut("[HOTKEY] Practice hotkey gate active (menu suppression)", true);
            } else {
                LogOut("[HOTKEY] Practice hotkey gate not installed yet (EfzRevival may not be loaded)", true);
            }
            PracticeOverlayGate::EnsureInstalled();
        } catch (...) {
            LogOut("[HOTKEY] Exception while installing practice hotkey gate", true);
        }

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

        // A previous injected session may have left the FM bypass patched; restore before reapplying locally.
        ForceRestoreFinalMemoryHPBypass("startup baseline");
        TimerFreeze::ForceRestore("startup baseline");

        LogOut("[SYSTEM] EfzExtendedPractice - Delayed initialization starting", true);
        LogOut("[SYSTEM] Console initialized with code page: " + std::to_string(GetConsoleOutputCP()), true);
        LogOut("[SYSTEM] Current locale: C", true);

    LogOut("[SYSTEM] Starting background threads...", true);
    if(Practice::LegacyMonitorStartupRequired()) (void)Practice::StartMonitorThread();
    std::thread(LifecycleWatcherThread).detach();
        LogOut("[SYSTEM] Essential background threads started.", true);

        WriteStartupLog("Using WinAPI keyboard input path; controller input prefers XInput with DirectInput fallback");
        g_directInputAvailable = false;

        WriteStartupLog("Reading key.ini file...");
        ReadKeyMappingsFromIni();
        WriteStartupLog("Key mappings read");

        LogOut("EfzExtendedPractice initialized successfully", true);
        WriteStartupLog("Delayed initialization complete");

    std::thread([]{
            Sleep(2000);
            try {
                if (DirectDrawHook::InitializeD3D9()) {
                    LogOut("[SYSTEM] D3D9 Overlay system initialized.", true);
                } else if (DirectDrawHook::WasLastD3D9InitDeferredForNetplay()) {
                    LogOut("[SYSTEM] D3D9 overlay initialization deferred because netplay suspend is active; resume path will retry.", true);
                } else {
                    LogOut("[SYSTEM] Failed to initialize D3D9 Overlay system.", true);
                }
            } catch (...) {
                LogOut("[SYSTEM] Exception during D3D9 overlay initialization.", true);
            }
        }).detach();

        g_initialized = true;
        inStartupPhase = false;
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
    DebugLog::g_EnableDebugLog = Config::GetSettings().enableDebugFileLog;
    FrameBar::g_enabled.store(Config::GetSettings().showFrameBar);
    }
    else {
        LogOut("[SYSTEM] Failed to initialize configuration, using defaults", true);
        ExtendedConfigBridge::InitializeAudioControl();
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
    g_hSelfModule = hModule;
        CrashHandler::Install(hModule);
        WriteEarlyLoaderTrace("DLL_PROCESS_ATTACH reached");
        // Do not call DisableThreadLibraryCalls: the XP build links the static CRT (/MT).
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
        // Signal only: at process exit DllMain holds the loader lock. Waiting for workers, flushing
        // streams, or unloading MinHook/DirectX here can deadlock.
        g_isShuttingDown = true;
        g_featuresEnabled = false;
        // At termination other threads are gone and may have died holding a mutex.
        if (lpReserved != nullptr) {
            break;
        }
        Practice::RequestMonitorStop();

        // Explicit FreeLibrary is best-effort. A real shutdown API must quiesce and join every worker
        // before calling FreeLibrary; this branch is not that API.
        DebugLog::Shutdown();

        // Stop buffer freezing before restoring P2 control.
        StopBufferFreezingIgnoringTutorialLease();
        ForceRestoreFinalMemoryHPBypass("DLL_PROCESS_DETACH");
        TimerFreeze::ForceRestore("DLL_PROCESS_DETACH");

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
        }

        Sleep(100);

        // Revival callback hooks hold a module reference. Remove the hooks, but never FreeLibrary under the
        // loader lock; keep the reference until process exit.
        AudioControl::ShutdownHooks(false);

        MH_Uninitialize();
        break;
    }
    return TRUE;
}
