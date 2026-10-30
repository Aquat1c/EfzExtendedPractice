#include "../include/utils/audio_runtime_state.h"
#include "../include/utils/audio_file_control.h"
#include "../include/utils/config.h"
#include "../include/utils/i18n.h"
#include "../include/core/logger.h"
#include "../include/utils/utilities.h"
#include "../include/input/efz_input_bindings.h"
#include "../include/core/di_keycodes.h"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <atomic>
#include <map>
#include <mutex>
#include <xinput.h>

namespace Config {
    namespace {
        constexpr float kUiScaleMin = 0.70f;
        constexpr float kUiScaleMax = 1.50f;

        float ClampUiScale(float scale) {
            if (scale < kUiScaleMin) return kUiScaleMin;
            if (scale > kUiScaleMax) return kUiScaleMax;
            return scale;
        }

        int ParseLanguage(const std::string& value) {
            std::string v;
            for (char c : value) {
                if (c != ' ' && c != '\t') v.push_back((char)std::tolower((unsigned char)c));
            }
            return (v == "ja") ? I18n::Japanese : I18n::English;
        }

        const char* LanguageIniValue(int language) {
            return language == I18n::Japanese ? "ja" : "en";
        }
    }

    static Settings settings;

    static std::string configFilePath;

    using IniMap = std::unordered_map<std::string, std::unordered_map<std::string, std::string>>;
    // Read and written from the menu, render and hotkey threads. No file I/O under the lock.
    static IniMap s_ini;
    static std::mutex s_iniMutex;
    static std::atomic<bool> s_efzBoundVks[256];

    void SetIniValue(const std::string& section, const std::string& key, const std::string& value);
#include "../include/utils/audio_config_publication.inl"

    bool GetValueBool(const std::string& section, const std::string& key, bool defaultValue);
    int GetValueInt(const std::string& section, const std::string& key, int defaultValue);
    bool LoadIniFromFile();
    static std::string ToLower(std::string s) { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return (char)std::tolower(c); }); return s; }

    void SetIniValue(const std::string& section, const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(s_iniMutex);
        s_ini[ToLower(section)][ToLower(key)] = value;
    }
    
    bool GetValueBool(const std::string& section, const std::string& key, bool defaultValue) {
        std::lock_guard<std::mutex> lock(s_iniMutex);
        auto sectionIt = s_ini.find(ToLower(section));
        if (sectionIt != s_ini.end()) {
            auto keyIt = sectionIt->second.find(ToLower(key));
            if (keyIt != sectionIt->second.end()) {
                return keyIt->second == "1" || keyIt->second == "true" || keyIt->second == "yes";
            }
        }
        return defaultValue;
    }
    
    int GetValueInt(const std::string& section, const std::string& key, int defaultValue) {
        std::lock_guard<std::mutex> lock(s_iniMutex);
        auto sectionIt = s_ini.find(ToLower(section));
        if (sectionIt != s_ini.end()) {
            auto keyIt = sectionIt->second.find(ToLower(key));
            if (keyIt != sectionIt->second.end()) {
                return ParseKeyValue(keyIt->second);
            }
        }
        return defaultValue;
    }
    
    bool Initialize() {
        char modulePath[MAX_PATH] = {0};

        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        char timeBuffer[26];
        ctime_s(timeBuffer, sizeof(timeBuffer), &time_t_now);
        std::string timestamp(timeBuffer);
        timestamp.pop_back();

        LogOut("[CONFIG] Initializing config system at " + timestamp, true);
        LogOut("[CONFIG] Current working directory: " + GetCurrentWorkingDirectory(), true);

        HMODULE hModule = GetModuleHandleA("efz_training_mode.dll");
        if (hModule) {
            DWORD result = GetModuleFileNameA(hModule, modulePath, MAX_PATH);
            if (result > 0) {
                LogOut("[CONFIG] Found module path via GetModuleHandleA: " + std::string(modulePath), true);
            } else {
                LogOut("[CONFIG] GetModuleFileNameA failed with error: " + std::to_string(GetLastError()), true);
            }
        } else {
            LogOut("[CONFIG] GetModuleHandleA failed to find efz_training_mode.dll, error: " + std::to_string(GetLastError()), true);

            if (GetModuleFileNameA(NULL, modulePath, MAX_PATH) > 0) {
                LogOut("[CONFIG] Using executable path instead: " + std::string(modulePath), true);
            } else {
                LogOut("[CONFIG] GetModuleFileNameA(NULL) failed with error: " + std::to_string(GetLastError()), true);
            }
        }
        
        try {
            std::filesystem::path basePath(modulePath);
            std::filesystem::path dirPath = basePath.parent_path();
            LogOut("[CONFIG] Base directory path: " + dirPath.string(), true);

            std::filesystem::path configPath = dirPath / "efz_training_config.ini";
            LogOut("[CONFIG] Attempting to use config path: " + configPath.string(), true);

            configFilePath = configPath.string();

            {
                LogOut("[CONFIG] Testing write permissions...", true);
                std::ofstream testFile(configPath, std::ios::app);
                if (testFile) {
                    LogOut("[CONFIG] File location is writable", true);
                    testFile.close();
                } else {
                    LogOut("[CONFIG] Cannot write to file location, error: " + std::to_string(errno), true);

                    char errMsg[256];
                    strerror_s(errMsg, sizeof(errMsg), errno);
                    LogOut("[CONFIG] Error message: " + std::string(errMsg), true);

                    char currentDir[MAX_PATH];
                    GetCurrentDirectoryA(MAX_PATH, currentDir);
                    LogOut("[CONFIG] Falling back to current directory: " + std::string(currentDir), true);

                    configPath = std::filesystem::path(currentDir) / "efz_training_config.ini";
                    configFilePath = configPath.string();
                    LogOut("[CONFIG] Fallback config path: " + configFilePath, true);

                    std::ofstream testFile2(configPath, std::ios::app);
                    if (testFile2) {
                        LogOut("[CONFIG] Fallback location is writable", true);
                        testFile2.close();
                    } else {
                        LogOut("[CONFIG] Cannot write to fallback location either, error: " + std::to_string(errno), true);
                        strerror_s(errMsg, sizeof(errMsg), errno);
                        LogOut("[CONFIG] Error message: " + std::string(errMsg), true);
                    }
                }
            }
            
            LogOut("[CONFIG] Checking if config file exists at: " + configFilePath, true);
            bool fileExists = std::filesystem::exists(configPath);
            LogOut("[CONFIG] File exists: " + std::string(fileExists ? "YES" : "NO"), true);
            
            if (!fileExists) {
                LogOut("[CONFIG] Config file does not exist, creating default...", true);
                if (!CreateDefaultConfig()) {
                    LogOut("[CONFIG] Failed to create default config file", true);
                    LoadSettings();
                    return false;
                }
            }
            else {
                if (std::filesystem::file_size(configPath) == 0) {
                    LogOut("[CONFIG] Config file exists but is empty, creating default content", true);
                    if (!CreateDefaultConfig()) {
                        LogOut("[CONFIG] Failed to create default config file", true);
                        LoadSettings();
                        return false;
                    }
                } else {
                    LogOut("[CONFIG] Config file exists with size: " + std::to_string(std::filesystem::file_size(configPath)) + " bytes", true);
                }
            }
            
            if (!LoadIniFromFile()) {
                LogOut("[CONFIG] Failed to parse ini file, continuing with defaults", true);
            }
            bool loadResult = LoadSettings();
            LogOut("[CONFIG] LoadSettings result: " + std::to_string(loadResult), true);
            return loadResult;
        }
        catch (const std::exception& e) {
            LogOut("[CONFIG] Exception in Initialize: " + std::string(e.what()), true);
            LoadSettings();
            return false;
        }
    }
    
    std::string GetCurrentWorkingDirectory() {
        char currentDir[MAX_PATH];
        if (GetCurrentDirectoryA(MAX_PATH, currentDir)) {
            return std::string(currentDir);
        }
        return "Unknown";
    }
    
    bool CreateDefaultConfig() {
        LogOut("[CONFIG] Creating default config file at: " + configFilePath, true);
        
        try {
            EFZInputBindings::BindingSet nativeBindings{};
            std::string nativeBindingPath;
            std::string nativeBindingError;
            const bool hasNativeBindings = EFZInputBindings::LoadActiveKeyIni(
                nativeBindings, nativeBindingPath, nativeBindingError);

            std::array<bool, 256> reservedDik{};
            std::array<int, 24> reservedPads{};
            std::size_t reservedPadCount = 0;
            EFZInputBindings::CollectReservations(
                nativeBindings, reservedDik, reservedPads, reservedPadCount);
            std::array<bool, 256> reservedVk{};
            for (std::size_t dik = 0; dik < reservedDik.size(); ++dik) {
                if (!reservedDik[dik]) continue;
                const int vk = MapDIKToVK(static_cast<int>(dik));
                if (vk > 0 && vk < static_cast<int>(reservedVk.size())) {
                    reservedVk[static_cast<std::size_t>(vk)] = true;
                }
            }
            constexpr int fixedUiKeys[] = {
                VK_ESCAPE, VK_RETURN, VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT,
                VK_PRIOR, VK_NEXT, VK_OEM_4, VK_OEM_6,
            };
            for (int vk : fixedUiKeys) reservedVk[static_cast<std::size_t>(vk)] = true;

            const EFZInputBindings::FirstRunKeyboardPlan keyboardPlan =
                EFZInputBindings::BuildFirstRunKeyboardPlan(reservedVk);
            const EFZInputBindings::FirstRunGamepadPlan gamepadPlan =
                EFZInputBindings::BuildFirstRunGamepadPlan(
                    reservedPads.data(), reservedPadCount);

            if (hasNativeBindings) {
                LogOut("[CONFIG][FIRST_RUN] Derived conflict-free defaults from both players in " +
                       nativeBindingPath, true);
            } else {
                LogOut("[CONFIG][FIRST_RUN] Native bindings unavailable; using conflict-free stock defaults: " +
                       nativeBindingError, true);
            }

            auto keyValue = [](int value) {
                if (value < 0) return std::string("-1");
                std::ostringstream oss;
                oss << "0x" << std::hex << std::uppercase << value;
                return oss.str();
            };

            std::ofstream file(configFilePath);
            if (!file.is_open()) {
                LogOut("[CONFIG] Error: Could not open config file for writing.", true);
                return false;
            }
            
            file << "[General]\n";
            file << "; Compatibility flag for ImGui-hosted helpers. The configuration menu now always uses ImGui/fallback hosts.\n";
            file << "useImGui = 1\n";
            file << "; Render the EFZ-native custom menu instead of the legacy ImGui list (1 = yes, 0 = no)\n";
            file << "useCustomMenu = 1\n";
            file << "; Enable detailed debug messages in the console (1 = yes, 0 = no)\n";
            file << "detailedLogging = 0\n";
            file << "; Enable writing efz_training_debug.log (1 = yes, 0 = no)\n";
            file << "enableDebugFileLog = 0\n";
            file << "; Show the debug console window (1 = yes, 0 = no)\n";
            file << "enableConsole = 0\n";
            file << "; Restrict functionality to Practice Mode only (1 = yes, 0 = no)\n";
            file << "restrictToPracticeMode = 1\n\n";
            file << "; Check GitHub once per launch for a newer release (1 = yes, 0 = no)\n";
            file << "checkForUpdates = 1\n\n";
            file << "; Enable this mod's framestep hotkeys/runtime (1 = yes, 0 = no)\n";
            file << "framestepEnabled = 1\n";
            file << "; Suppress Revival's native pause/step hotkeys while this mod owns framestep (1 = yes, 0 = no)\n";
            file << "suppressRevivalFramestep = 1\n\n";
            file << "; Enable FPS/timing diagnostics in logs (1 = yes, 0 = no)\n";
            file << "enableFpsDiagnostics = 0\n\n";
            file << "; Show per-player FrameBar overlay (1 = yes, 0 = no)\n";
            file << "showFrameBar = 0\n\n";
            file << "frameBarMoveData = 1\n\n";
            file << "menuAnimations = 1\n\n";
            file << "; Audio volume levels as percent of the current default mix (0-100)\n";
            file << "bgmVolumePercent = 100\n";
            file << "seVolumePercent = 100\n\n";
            file << "; FrameBar cell cadence: 0 = subframes, 1 = visual frames\n";
            file << "frameBarTimingMode = 0\n";
            file << "; FrameBar detail: 0 = full, 1 = compact, 2 = bars only\n";
            file << "frameBarDetailMode = 0\n\n";
            file << "; Collision / hitbox overlay display (Practice only)\n";
            file << "collisionDisplayHitboxes = 0\n";
            file << "collisionDisplayHurtboxes = 0\n";
            file << "collisionDisplayCollisionBoxes = 0\n";
            file << "collisionDisplayProjectileInteractions = 0\n";
            file << "; Per-player ordinary box filters (master layers above still apply)\n";
            file << "collisionDisplayP1Hitboxes = 1\n";
            file << "collisionDisplayP2Hitboxes = 1\n";
            file << "collisionDisplayP1Hurtboxes = 1\n";
            file << "collisionDisplayP2Hurtboxes = 1\n";
            file << "collisionDisplayP1CollisionBoxes = 1\n";
            file << "collisionDisplayP2CollisionBoxes = 1\n";
            file << "; Box fill alpha percent. Outlines stay readable.\n";
            file << "collisionDisplayFillAlphaPercent = 25\n";
            file << "; Projectile interaction sub-layers\n";
            file << "collisionDisplayProjectileBoxes = 1\n";
            file << "collisionDisplayProjectileOrigins = 1\n";
            file << "collisionDisplayProjectileIntersections = 1\n";
            file << "collisionDisplayNagamoriRanges = 1\n";
            file << "collisionDisplayNagamoriAffected = 1\n\n";
            file << "; Log active player / CPU flags during Character Select (1 = yes, 0 = no)\n";
            file << "enableCharacterSelectLogger = 1\n\n";

            file << "; Show a one-time Practice hint about opening the overlay (1 = yes, 0 = no)\n";
            file << "showPracticeEntryHint = 1\n\n";

            file << "; UI scale for ImGui window (0.70 - 1.50)\n";
            file << "uiScale = 0.90\n\n";
            file << "; UI font: 0 = ImGui default font, 1 = Segoe UI (Windows)\n";
            file << "uiFont = 0\n\n";
            file << "; Custom menu and overlay language: en or ja\n";
            file << "language = en\n\n";

            file << "; ImGui navigation tuning\n";
            file << "; Analog threshold for keyboard-fallback nav (0..1). Default 0.45\n";
            file << "guiNavAnalogThreshold = 0.45\n";
            file << "; Key repeat timings (seconds). Defaults 0.50 delay, 0.15 rate\n";
            file << "guiNavRepeatDelay = 0.50\n";
            file << "guiNavRepeatRate = 0.15\n\n";

            file << "; Right-stick to mouse wheel (GUI)\n";
            file << "guiScrollRightStickEnable = 1\n";
            file << "; Notches per second at full tilt (both axes). Default 10.0\n";
            file << "guiScrollRightStickScale = 10.0\n\n";

            file << "; RF freeze behavior (after Continuous Recovery)\n";
            file << "; Start freezing RF after enforcement (1=yes, 0=no)\n";
            file << "freezeRFAfterContRec = 1\n";
            file << "; Maintain RF freeze only when neutral (allowed MoveIDs) (1=yes, 0=no)\n";
            file << "freezeRFOnlyWhenNeutral = 1\n\n";

            file << "; Frame Advantage display duration (seconds)\n";
            file << "; How long to show frame advantage and gap messages on screen\n";
            file << "frameAdvantageDisplayDuration = 1.9\n\n";

            file << "; Combo statistics overlay\n";
            file << "showComboStatisticsOverlay = 1\n";
            file << "comboOverlayCompactMode = 1\n";
            file << "comboOverlayShowDetailRow = 0\n";
            file << "comboOverlayDetailRowSource = 0\n";
            file << "comboOverlayShowFinalSummary = 1\n";
            file << "comboOverlayDisplayDuration = 1.9\n";
            file << "comboOverlayHideWhenImGuiVisible = 1\n";
            file << "comboOverlayResumeAfterImGui = 1\n";
            file << "comboOverlayShowRfMultiplier = 0\n";
            file << "comboOverlayShowRawScale = 0\n\n";

            file << "; Practice: Dummy Auto-Block neutral timeout (ms) for First Hit/After First Hit modes.\n";
            file << "; When waiting to re-arm/disable, require this many milliseconds of continuous neutral before toggling.\n";
            file << "; Default: 10000 (10 seconds). Set 0 to toggle immediately on any neutral edge.\n";
            file << "autoBlockNeutralTimeoutMs = 10000\n\n";
            file << "; Mission recording count-in in milliseconds (0 = off, default 500).\n";
            file << "missionRecorderCountInMs = 500\n\n";

            file << "; Virtual Cursor (software controller-driven cursor) settings\n";
            file << "; Master enable (1=on,0=off)\n";
            file << "enableVirtualCursor = 1\n";
            file << "; Allow virtual cursor while windowed (1=yes,0=only fullscreen)\n";
            file << "virtualCursorAllowWindowed = 0\n";
            file << "; Movement speeds in pixels/second (base, fast/shoulder, dpad nudge)\n";
            file << "virtualCursorBaseSpeed = 900\n";
            file << "virtualCursorFastSpeed = 1800\n";
            file << "virtualCursorDpadSpeed = 700\n";
            file << "; Analog stick acceleration exponent (1.0=linear, 1.5=gentler near center)\n";
            file << "virtualCursorAccelPower = 1.35\n\n";

            file << "; Controller selection: -1 = All, 0..3 = XInput user index\n";
            file << "controllerIndex = -1\n\n";

            file << "; Savestate backend: 0=Custom, 1=Revival, 2=Custom with Revival fallback\n";
            file << "savestateBackendMode = 2\n\n";
            file << "; Restore saved custom .pal usage when loading savestates (1=yes, 0=use default palettes)\n";
            file << "savestateLoadCustomPalettes = 1\n\n";

            file << "[Hotkeys]\n";
            file << "; First-run defaults were derived from EFZ's active P1/P2 bindings. Existing configs are never rewritten automatically.\n";
            file << "FirstRunBindingSource=" << (hasNativeBindings ? "key.ini" : "stock") << "\n";
            file << "; Use virtual-key codes (hexadecimal, e.g., 0x70 for F1)\n";
            file << "; See key code definitions at: https://docs.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes\n";
            file << "; Common keys: \n";
            file << "; - 0x31-0x39: Number keys 1-9\n";
            file << "; - 0x41-0x5A: Letter keys A-Z\n";
            file << "; - 0x70-0x7B: Function keys F1-F12\n\n";
            
            file << "; Teleport players to recorded positions\n";
            file << "TeleportKey=" << keyValue(keyboardPlan.teleport) << "\n\n";
            
            file << "; Record current player positions\n";
            file << "RecordKey=" << keyValue(keyboardPlan.recordPosition) << "\n\n";
            
            file << "; Toggle title display mode (disabled by default)\n";
            file << "ToggleTitleKey=-1\n\n";
            
            file << "; Reset frame counter (disabled by default)\n";
            file << "ResetFrameCounterKey=-1\n\n";

            file << "; Key that opens the training menu (0x1B = Esc). With any other key, Esc does its normal EFZ job again.\n";
            file << "MenuKey=0x1B\n";
            file << "\n; Practice snapshot hotkeys (save/load through EfzRevival)\n";
            file << "SavestateSaveKey=" << keyValue(keyboardPlan.savestateSave) << "\n";
            file << "SavestateLoadKey=" << keyValue(keyboardPlan.savestateLoad) << "\n";

            file << "\n; Practice: Switch Players toggle (Practice only)\n";
            file << "SwitchPlayersKey=" << keyValue(keyboardPlan.switchPlayers) << "\n";
            file << "; Macro: Record (two-press)\n";
            file << "MacroRecordKey=" << keyValue(keyboardPlan.macroRecord) << "\n";
            file << "; Macro: Play (replay)\n";
            file << "MacroPlayKey=" << keyValue(keyboardPlan.macroPlay) << "\n";
            file << "; Macro: Cycle Slot (next)\n";
            file << "MacroSlotKey=" << keyValue(keyboardPlan.macroSlot) << "\n";
            file << "\n; UI footer actions (Apply / Refresh / Exit)\n";
            file << "; Avoid using in-game bound keys (Enter/Escape/Space). Defaults: E, R, Q.\n";
            file << "UIAcceptKey=" << keyValue(keyboardPlan.uiAccept) << "\n";
            file << "UIRefreshKey=" << keyValue(keyboardPlan.uiRefresh) << "\n";
            file << "UIExitKey=" << keyValue(keyboardPlan.uiExit) << "\n";

            file << "\n; Framestep (vanilla EFZ / supported Revival)\n";
            file << "; Pause toggle\n";
            file << "FramestepPauseKey=" << keyValue(keyboardPlan.framestepPause) << "\n";
            file << "; Step forward one frame (when paused)\n";
            file << "FramestepStepKey=" << keyValue(keyboardPlan.framestepStep) << "\n";

            file << "\n; Swap Positions custom binding\n";
            file << "; Enable a dedicated key for swapping positions (1=yes,0=no)\n";
            file << "SwapCustomEnabled=0\n";
            file << "; Dedicated key (hex VK) for swapping positions when enabled (-1 disables)\n";
            file << "SwapCustomKey=-1\n";

            file << "\n; Controller bindings (XInput) \n";
            file << "; Use symbolic names (e.g. A, B, X, Y, LB, RB, BACK, START, L3, R3, DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT) or hex mask (e.g. 0x2000). -1 disables.\n";
            file << "; Triggers (LT, RT) are treated as virtual buttons (threshold based).\n";
            file << "gpTeleportButton=" << GetGamepadButtonName(gamepadPlan.teleport) << "\n";
            file << "gpSavePositionButton=" << GetGamepadButtonName(gamepadPlan.savePosition) << "\n";
            file << "gpSwitchPlayersButton=" << GetGamepadButtonName(gamepadPlan.switchPlayers) << "\n";
            // ABXY reserved for UI (A=confirm, B=back).
            file << "gpSwapPositionsButton=" << GetGamepadButtonName(gamepadPlan.swapPositions) << "\n";
            file << "gpMacroRecordButton=" << GetGamepadButtonName(gamepadPlan.macroRecord) << "\n";
            file << "gpMacroPlayButton=" << GetGamepadButtonName(gamepadPlan.macroPlay) << "\n";
            file << "gpMacroSlotButton=" << GetGamepadButtonName(gamepadPlan.macroSlot) << "\n";
            file << "gpToggleMenuButton=" << GetGamepadButtonName(gamepadPlan.toggleMenu) << "\n";

            file << "; UI navigation: cycle tabs/sub-tabs (rebindable)\n";
            file << "; D-pad focus navigation reaches tabs and sub-tabs without consuming extra buttons.\n";
            file << "gpUiTopTabPrev=" << GetGamepadButtonName(gamepadPlan.topTabPrevious) << "\n";
            file << "gpUiTopTabNext=" << GetGamepadButtonName(gamepadPlan.topTabNext) << "\n";
            file << "gpUiSubTabPrev=" << GetGamepadButtonName(gamepadPlan.subTabPrevious) << "\n";
            file << "gpUiSubTabNext=" << GetGamepadButtonName(gamepadPlan.subTabNext) << "\n";
            
            file.close();
            
            if (file.fail()) {
                char errMsg[256];
                strerror_s(errMsg, sizeof(errMsg), errno);
                LogOut("[CONFIG] Error occurred while closing file: " + std::string(errMsg), true);
                return false;
            }
            
            if (std::filesystem::exists(configFilePath)) {
                LogOut("[CONFIG] Default config file created successfully, size: " + 
                       std::to_string(std::filesystem::file_size(configFilePath)) + " bytes", true);
                std::ifstream verifyFile(configFilePath);
                if (verifyFile) {
                    std::string line;
                    int lineCount = 0;
                    LogOut("[CONFIG] File content sample:", true);
                    while (std::getline(verifyFile, line) && lineCount < 5) {
                        LogOut("[CONFIG] " + line, true);
                        lineCount++;
                    }
                    verifyFile.close();
                }
                LoadIniFromFile();
                return true;
            } else {
                LogOut("[CONFIG] File creation appeared to succeed, but file does not exist!", true);
                return false;
            }
        }
        catch (const std::exception& e) {
            LogOut("[CONFIG] Exception creating default config: " + std::string(e.what()), true);
            return false;
        }
    }
    
    bool LoadSettings() {
        LogOut("[CONFIG] Loading settings from: " + configFilePath, true);
        LoadIniFromFile();
        
        try {
            const IniMap iniData = [] {
                std::lock_guard<std::mutex> lock(s_iniMutex);
                return s_ini;
            }();
            settings.useImGui = GetValueBool("General", "useImGui", true);
            settings.useCustomMenu = GetValueBool("General", "useCustomMenu", true);
            settings.detailedLogging = GetValueBool("General", "detailedLogging", false);
            settings.enableDebugFileLog = GetValueBool("General", "enableDebugFileLog", false);
            settings.enableConsole = GetValueBool("General", "enableConsole", false);
            settings.restrictToPracticeMode = GetValueBool("General", "restrictToPracticeMode", true);
            settings.framestepEnabled = GetValueBool("General", "framestepEnabled", true);
            settings.suppressRevivalFramestep = GetValueBool("General", "suppressRevivalFramestep", true);
            settings.enableFpsDiagnostics = GetValueBool("General", "enableFpsDiagnostics", false);
            settings.showFrameBar = GetValueBool("General", "showFrameBar", false);
            settings.frameBarMoveData = GetValueBool("General", "frameBarMoveData", true);
            settings.menuAnimations = GetValueBool("General", "menuAnimations", true);
            settings.checkForUpdates = GetValueBool("General", "checkForUpdates", true);
            settings.bgmVolumePercent = GetValueInt("General", "bgmVolumePercent", 100);
            if (settings.bgmVolumePercent < 0) settings.bgmVolumePercent = 0;
            if (settings.bgmVolumePercent > 100) settings.bgmVolumePercent = 100;
            settings.seVolumePercent = GetValueInt("General", "seVolumePercent", 100);
            if (settings.seVolumePercent < 0) settings.seVolumePercent = 0;
            if (settings.seVolumePercent > 100) settings.seVolumePercent = 100;
            settings.frameBarTimingMode = GetValueInt("General", "frameBarTimingMode", 0);
            if (settings.frameBarTimingMode < 0 || settings.frameBarTimingMode > 1) {
                settings.frameBarTimingMode = 0;
            }
            settings.frameBarDetailMode = GetValueInt("General", "frameBarDetailMode", 0);
            if (settings.frameBarDetailMode < 0 || settings.frameBarDetailMode > 2) {
                settings.frameBarDetailMode = 0;
            }
            settings.collisionDisplayHitboxes = GetValueBool("General", "collisionDisplayHitboxes", false);
            settings.collisionDisplayHurtboxes = GetValueBool("General", "collisionDisplayHurtboxes", false);
            settings.collisionDisplayCollisionBoxes = GetValueBool("General", "collisionDisplayCollisionBoxes", false);
            settings.collisionDisplayProjectileInteractions = GetValueBool("General", "collisionDisplayProjectileInteractions", false);
            settings.collisionDisplayP1Hitboxes = GetValueBool("General", "collisionDisplayP1Hitboxes", true);
            settings.collisionDisplayP2Hitboxes = GetValueBool("General", "collisionDisplayP2Hitboxes", true);
            settings.collisionDisplayP1Hurtboxes = GetValueBool("General", "collisionDisplayP1Hurtboxes", true);
            settings.collisionDisplayP2Hurtboxes = GetValueBool("General", "collisionDisplayP2Hurtboxes", true);
            settings.collisionDisplayP1CollisionBoxes = GetValueBool("General", "collisionDisplayP1CollisionBoxes", true);
            settings.collisionDisplayP2CollisionBoxes = GetValueBool("General", "collisionDisplayP2CollisionBoxes", true);
            settings.collisionDisplayFillAlphaPercent = GetValueInt("General", "collisionDisplayFillAlphaPercent", 25);
            if (settings.collisionDisplayFillAlphaPercent < 0) settings.collisionDisplayFillAlphaPercent = 0;
            if (settings.collisionDisplayFillAlphaPercent > 100) settings.collisionDisplayFillAlphaPercent = 100;
            settings.collisionDisplayProjectileBoxes = GetValueBool("General", "collisionDisplayProjectileBoxes", true);
            settings.collisionDisplayProjectileOrigins = GetValueBool("General", "collisionDisplayProjectileOrigins", true);
            settings.collisionDisplayProjectileIntersections = GetValueBool("General", "collisionDisplayProjectileIntersections", true);
            settings.collisionDisplayNagamoriRanges = GetValueBool("General", "collisionDisplayNagamoriRanges", true);
            settings.collisionDisplayNagamoriAffected = GetValueBool("General", "collisionDisplayNagamoriAffected", true);
            settings.enableCharacterSelectLogger = GetValueBool("General", "enableCharacterSelectLogger", true);
            settings.showPracticeEntryHint = GetValueBool("General", "showPracticeEntryHint", true);
            {
                int raw = 0;
                auto sectionIt = iniData.find("general");
                float scale = 0.90f;
                if (sectionIt != iniData.end()) {
                    auto keyIt = sectionIt->second.find("uiscale");
                    if (keyIt != sectionIt->second.end()) {
                        try {
                            scale = std::stof(keyIt->second);
                        } catch (...) { scale = 0.90f; }
                    }
                }
                settings.uiScale = ClampUiScale(scale);
            }

            settings.uiFontMode = 0;
            {
                auto sectionIt = iniData.find("general");
                if (sectionIt != iniData.end()) {
                    auto keyIt = sectionIt->second.find("uifont");
                    if (keyIt != sectionIt->second.end()) {
                        try { settings.uiFontMode = std::stoi(keyIt->second); } catch (...) { settings.uiFontMode = 0; }
                        if (settings.uiFontMode < 0 || settings.uiFontMode > 1) settings.uiFontMode = 0;
                    }
                }
            }

            settings.language = I18n::English;
            {
                auto sectionIt = iniData.find("general");
                if (sectionIt != iniData.end()) {
                    auto keyIt = sectionIt->second.find("language");
                    if (keyIt != sectionIt->second.end()) settings.language = ParseLanguage(keyIt->second);
                }
            }
            I18n::SetLanguage(static_cast<I18n::Language>(settings.language));

            settings.enableVirtualCursor = GetValueBool("General", "enableVirtualCursor", true);
            settings.virtualCursorAllowWindowed = GetValueBool("General", "virtualCursorAllowWindowed", false);
            settings.virtualCursorBaseSpeed = (float)GetValueInt("General", "virtualCursorBaseSpeed", 900);
            settings.virtualCursorFastSpeed = (float)GetValueInt("General", "virtualCursorFastSpeed", 1800);
            settings.virtualCursorDpadSpeed = (float)GetValueInt("General", "virtualCursorDpadSpeed", 700);
            {
                auto sectionIt = iniData.find("general");
                float p = 1.35f;
                if (sectionIt != iniData.end()) {
                    auto keyIt = sectionIt->second.find("virtualcursoraccelpower");
                    if (keyIt != sectionIt->second.end()) {
                        try { p = std::stof(keyIt->second); } catch (...) { p = 1.35f; }
                    }
                }
                if (p < 0.5f) p = 0.5f; if (p > 3.0f) p = 3.0f;
                settings.virtualCursorAccelPower = p;
            }

            {
                auto sectionIt = iniData.find("general");
                float thr = 0.45f;
                float repDelay = 0.50f;
                float repRate = 0.15f;
                bool scrollEnable = true;
                float scrollScale = 10.0f;
                if (sectionIt != iniData.end()) {
                    auto getf = [&](const char* k, float defv){
                        auto it = sectionIt->second.find(k);
                        if (it != sectionIt->second.end()) { try { return std::stof(it->second); } catch (...) {} }
                        return defv;
                    };
                    auto geti = [&](const char* k, bool defv){
                        auto it = sectionIt->second.find(k);
                        if (it != sectionIt->second.end()) { return it->second == "1" || it->second == "true"; }
                        return defv;
                    };
                    thr = getf("guinavanalogthreshold", 0.45f);
                    repDelay = getf("guinavrepeatdelay", 0.50f);
                    repRate = getf("guinavrepeatrate", 0.15f);
                    scrollEnable = geti("guiscrollrightstickenable", true);
                    scrollScale = getf("guiscrollrightstickscale", 10.0f);
                }
                if (thr < 0.05f) thr = 0.05f; if (thr > 0.95f) thr = 0.95f;
                if (repDelay < 0.05f) repDelay = 0.05f; if (repDelay > 1.00f) repDelay = 1.00f;
                if (repRate < 0.01f) repRate = 0.01f; if (repRate > 0.50f) repRate = 0.50f;
                if (scrollScale < 1.0f) scrollScale = 1.0f; if (scrollScale > 50.0f) scrollScale = 50.0f;
                settings.guiNavAnalogThreshold = thr;
                settings.guiNavRepeatDelay = repDelay;
                settings.guiNavRepeatRate = repRate;
                settings.guiScrollRightStickEnable = scrollEnable;
                settings.guiScrollRightStickScale = scrollScale;
            }

            settings.controllerIndex = -1;
            {
                auto sectionIt = iniData.find("general");
                if (sectionIt != iniData.end()) {
                    auto keyIt = sectionIt->second.find("controllerindex");
                    if (keyIt != sectionIt->second.end()) {
                        try { settings.controllerIndex = std::stoi(keyIt->second); } catch (...) { settings.controllerIndex = -1; }
                    }
                }
                if (settings.controllerIndex < -1 || settings.controllerIndex > 3) settings.controllerIndex = -1;
            }

            settings.freezeRFAfterContRec = GetValueBool("General", "freezeRFAfterContRec", true);
            settings.freezeRFOnlyWhenNeutral = GetValueBool("General", "freezeRFOnlyWhenNeutral", true);
            settings.crRequireBothNeutral = GetValueBool("General", "crRequireBothNeutral", true);
            settings.crBothNeutralDelayMs = GetValueInt("General", "crBothNeutralDelayMs", 500);
            if (settings.crBothNeutralDelayMs < 0) settings.crBothNeutralDelayMs = 0;
            if (settings.crBothNeutralDelayMs > 5000) settings.crBothNeutralDelayMs = 5000;
            settings.autoFixHPOnNeutral = GetValueBool("General", "autoFixHPOnNeutral", false);
            {
                auto sectionIt = iniData.find("general");
                float duration = 1.9f;
                if (sectionIt != iniData.end()) {
                    auto keyIt = sectionIt->second.find("frameadvantagedisplayduration");
                    if (keyIt != sectionIt->second.end()) {
                        try { duration = std::stof(keyIt->second); } catch (...) { duration = 1.9f; }
                    }
                }
                if (duration < 0.5f) duration = 0.5f;
                if (duration > 30.0f) duration = 30.0f;
                settings.frameAdvantageDisplayDuration = duration;
            }
            settings.autoBlockNeutralTimeoutMs = GetValueInt("General", "autoBlockNeutralTimeoutMs", 10000);
            settings.missionRecorderCountInMs =
                GetValueInt("General", "missionRecorderCountInMs", 500);
            if (settings.missionRecorderCountInMs < 0) {
                settings.missionRecorderCountInMs = 0;
            }
            if (settings.missionRecorderCountInMs > 3000) {
                settings.missionRecorderCountInMs = 3000;
            }
            settings.showComboStatisticsOverlay = GetValueBool("General", "showComboStatisticsOverlay", true);
            settings.comboOverlayCompactMode = GetValueBool("General", "comboOverlayCompactMode", true);
            settings.comboOverlayShowDetailRow = GetValueBool("General", "comboOverlayShowDetailRow", false);
            settings.comboOverlayDetailRowSource = GetValueInt("General", "comboOverlayDetailRowSource", 0);
            if (settings.comboOverlayDetailRowSource < 0 || settings.comboOverlayDetailRowSource > 1) {
                settings.comboOverlayDetailRowSource = 0;
            }
            settings.comboOverlayShowFinalSummary = GetValueBool("General", "comboOverlayShowFinalSummary", true);
            {
                auto sectionIt = iniData.find("general");
                float duration = 1.9f;
                if (sectionIt != iniData.end()) {
                    auto keyIt = sectionIt->second.find("combooverlaydisplayduration");
                    if (keyIt != sectionIt->second.end()) {
                        try { duration = std::stof(keyIt->second); } catch (...) { duration = 1.9f; }
                    }
                }
                if (duration < 0.5f) duration = 0.5f;
                if (duration > 30.0f) duration = 30.0f;
                settings.comboOverlayDisplayDuration = duration;
            }
            settings.comboOverlayHideWhenImGuiVisible = GetValueBool("General", "comboOverlayHideWhenImGuiVisible", true);
            settings.comboOverlayResumeAfterImGui = GetValueBool("General", "comboOverlayResumeAfterImGui", true);
            settings.comboOverlayShowRfMultiplier = GetValueBool("General", "comboOverlayShowRfMultiplier", false);
            settings.comboOverlayShowRawScale = GetValueBool("General", "comboOverlayShowRawScale", false);

            settings.teleportKey = GetValueInt("Hotkeys", "TeleportKey", 0x31);          // Default: '1'
            settings.recordKey = GetValueInt("Hotkeys", "RecordKey", 0x32);            // Default: '2'
            settings.configMenuKey = GetValueInt("Hotkeys", "MenuKey", VK_ESCAPE);    // Legacy ConfigMenuKey ignored
            if (!IsMenuKeyAllowed(settings.configMenuKey)) settings.configMenuKey = VK_ESCAPE;
            settings.toggleTitleKey = GetValueInt("Hotkeys", "ToggleTitleKey", -1);
            if (settings.toggleTitleKey == 0x34) settings.toggleTitleKey = -1;
            settings.resetFrameCounterKey = GetValueInt("Hotkeys", "ResetFrameCounterKey", -1);
            if (settings.resetFrameCounterKey == 0x35) settings.resetFrameCounterKey = -1;
            settings.helpKey = -1;
            settings.toggleImGuiKey = -1;
            settings.savestateSaveKey = GetValueInt("Hotkeys", "SavestateSaveKey", 0x55); // 'U'
            settings.savestateLoadKey = GetValueInt("Hotkeys", "SavestateLoadKey", 0x4A); // 'J'
            settings.switchPlayersKey = GetValueInt("Hotkeys", "SwitchPlayersKey", 0x4C); // 'L'
            settings.macroRecordKey   = GetValueInt("Hotkeys", "MacroRecordKey",   0x49); // 'I'
            settings.macroPlayKey     = GetValueInt("Hotkeys", "MacroPlayKey",     0x4F); // 'O'
            settings.macroSlotKey     = GetValueInt("Hotkeys", "MacroSlotKey",     0x4B); // 'K'
            settings.uiAcceptKey      = GetValueInt("Hotkeys", "UIAcceptKey",     0x45); // 'E'
            settings.uiRefreshKey     = GetValueInt("Hotkeys", "UIRefreshKey",    0x52); // 'R'
            settings.uiExitKey        = GetValueInt("Hotkeys", "UIExitKey",       0x51); // 'Q'
            settings.framestepPauseKey = GetValueInt("Hotkeys", "FramestepPauseKey", 0x20); // VK_SPACE
            settings.framestepStepKey  = GetValueInt("Hotkeys", "FramestepStepKey",  0x50); // 'P'
            settings.swapCustomEnabled = GetValueBool("Hotkeys", "SwapCustomEnabled", false);
            settings.swapCustomKey     = GetValueInt("Hotkeys", "SwapCustomKey", -1);
            auto getPad = [&](const char* name, const char* defStr){
                auto sectionIt = iniData.find("hotkeys");
                if (sectionIt != iniData.end()) {
                    auto keyIt = sectionIt->second.find(std::string(name));
                    if (keyIt != sectionIt->second.end()) return ParseGamepadButton(keyIt->second);
                }
                return ParseGamepadButton(defStr);
            };
            settings.gpTeleportButton       = getPad("gpteleportbutton", "BACK");
            settings.gpSavePositionButton   = getPad("gpsavepositionbutton", "L3");
            settings.gpSwitchPlayersButton  = getPad("gpswitchplayersbutton", "RB");
            settings.gpSwapPositionsButton  = getPad("gpswappositionsbutton", "R3");
            settings.gpMacroRecordButton    = getPad("gpmacrorecordbutton", "LB");
            settings.gpMacroPlayButton      = getPad("gpmacroplaybutton", "RT");
            settings.gpMacroSlotButton      = getPad("gpmacroslotbutton", "LT");
            settings.gpToggleMenuButton     = getPad("gptogglemenubutton", "START");
            settings.gpToggleImGuiButton    = -1;
            settings.gpUiTopTabPrev         = getPad("gpuitoptabprev", "LB");
            settings.gpUiTopTabNext         = getPad("gpuitoptabnext", "RB");
            settings.gpUiSubTabPrev         = getPad("gpuisubtabprev", "LT");
            settings.gpUiSubTabNext         = getPad("gpuisubtabnext", "RT");
            settings.savestateBackendMode   = GetValueInt("General", "savestateBackendMode", 2);
            if (settings.savestateBackendMode < 0 || settings.savestateBackendMode > 2) settings.savestateBackendMode = 2;
            settings.savestateLoadCustomPalettes = GetValueBool("General", "savestateLoadCustomPalettes", true);
            PublishLoadedAudioSettings();
            LogOut("[CONFIG] Settings loaded successfully", true);
            LogOut("[CONFIG] UseImGui: " + std::to_string(settings.useImGui), true);
            LogOut("[CONFIG] DetailedLogging: " + std::to_string(settings.detailedLogging), true);
            LogOut("[CONFIG] EnableDebugFileLog: " + std::to_string(settings.enableDebugFileLog), true);
            LogOut("[CONFIG] EnableConsole: " + std::to_string(settings.enableConsole), true);
            LogOut("[CONFIG] FramestepEnabled: " + std::to_string(settings.framestepEnabled), true);
            LogOut("[CONFIG] SuppressRevivalFramestep: " + std::to_string(settings.suppressRevivalFramestep), true);
            LogOut("[CONFIG] TeleportKey: " + std::to_string(settings.teleportKey) + " (" + GetKeyName(settings.teleportKey) + ")", true);
            LogOut("[CONFIG] RecordKey: " + std::to_string(settings.recordKey) + " (" + GetKeyName(settings.recordKey) + ")", true);
            LogOut("[CONFIG] MenuKey: " + GetKeyName(settings.configMenuKey), true);
            LogOut("[CONFIG] ToggleTitleKey: " + std::to_string(settings.toggleTitleKey) + " (" + (settings.toggleTitleKey < 0 ? std::string("Disabled") : GetKeyName(settings.toggleTitleKey)) + ")", true);
            LogOut("[CONFIG] ResetFrameCounterKey: " + std::to_string(settings.resetFrameCounterKey) + " (" + (settings.resetFrameCounterKey < 0 ? std::string("Disabled") : GetKeyName(settings.resetFrameCounterKey)) + ")", true);
            LogOut("[CONFIG] SavestateSaveKey: " + std::to_string(settings.savestateSaveKey) + " (" + GetKeyName(settings.savestateSaveKey) + ")", true);
            LogOut("[CONFIG] SavestateLoadKey: " + std::to_string(settings.savestateLoadKey) + " (" + GetKeyName(settings.savestateLoadKey) + ")", true);
            LogOut("[CONFIG] SwitchPlayersKey: " + std::to_string(settings.switchPlayersKey) + " (" + GetKeyName(settings.switchPlayersKey) + ")", true);
            LogOut("[CONFIG] MacroRecordKey: " + std::to_string(settings.macroRecordKey) + " (" + GetKeyName(settings.macroRecordKey) + ")", true);
            LogOut("[CONFIG] MacroPlayKey: " + std::to_string(settings.macroPlayKey) + " (" + GetKeyName(settings.macroPlayKey) + ")", true);
            LogOut("[CONFIG] MacroSlotKey: " + std::to_string(settings.macroSlotKey) + " (" + GetKeyName(settings.macroSlotKey) + ")", true);
            LogOut("[CONFIG] UIAcceptKey: " + std::to_string(settings.uiAcceptKey) + " (" + GetKeyName(settings.uiAcceptKey) + ")", true);
            LogOut("[CONFIG] UIRefreshKey: " + std::to_string(settings.uiRefreshKey) + " (" + GetKeyName(settings.uiRefreshKey) + ")", true);
            LogOut("[CONFIG] UIExitKey: " + std::to_string(settings.uiExitKey) + " (" + GetKeyName(settings.uiExitKey) + ")", true);
            LogOut("[CONFIG] FramestepPauseKey: " + std::to_string(settings.framestepPauseKey) + " (" + GetKeyName(settings.framestepPauseKey) + ")", true);
            LogOut("[CONFIG] FramestepStepKey: " + std::to_string(settings.framestepStepKey) + " (" + GetKeyName(settings.framestepStepKey) + ")", true);
            LogOut("[CONFIG] SwapCustomEnabled: " + std::to_string(settings.swapCustomEnabled), true);
            LogOut("[CONFIG] SwapCustomKey: " + GetKeyName(settings.swapCustomKey), true);
            LogOut("[CONFIG] gpTeleportButton: " + GetGamepadButtonName(settings.gpTeleportButton), true);
            LogOut("[CONFIG] gpSavePositionButton: " + GetGamepadButtonName(settings.gpSavePositionButton), true);
            LogOut("[CONFIG] gpSwitchPlayersButton: " + GetGamepadButtonName(settings.gpSwitchPlayersButton), true);
            LogOut("[CONFIG] gpSwapPositionsButton: " + GetGamepadButtonName(settings.gpSwapPositionsButton), true);
            LogOut("[CONFIG] gpMacroRecordButton: " + GetGamepadButtonName(settings.gpMacroRecordButton), true);
            LogOut("[CONFIG] gpMacroPlayButton: " + GetGamepadButtonName(settings.gpMacroPlayButton), true);
            LogOut("[CONFIG] gpMacroSlotButton: " + GetGamepadButtonName(settings.gpMacroSlotButton), true);
            LogOut("[CONFIG] gpToggleMenuButton: " + GetGamepadButtonName(settings.gpToggleMenuButton), true);
            LogOut("[CONFIG] gpUiTopTabPrev: " + GetGamepadButtonName(settings.gpUiTopTabPrev), true);
            LogOut("[CONFIG] gpUiTopTabNext: " + GetGamepadButtonName(settings.gpUiTopTabNext), true);
            LogOut("[CONFIG] gpUiSubTabPrev: " + GetGamepadButtonName(settings.gpUiSubTabPrev), true);
            LogOut("[CONFIG] gpUiSubTabNext: " + GetGamepadButtonName(settings.gpUiSubTabNext), true);
            LogOut("[CONFIG] enableFpsDiagnostics: " + std::to_string(settings.enableFpsDiagnostics), true);
            LogOut("[CONFIG] uiScale: " + std::to_string(settings.uiScale), true);
            LogOut("[CONFIG] uiFontMode: " + std::to_string(settings.uiFontMode), true);
            LogOut(std::string("[CONFIG] language: ") + LanguageIniValue(settings.language), true);
            LogOut("[CONFIG] savestateBackendMode: " + std::to_string(settings.savestateBackendMode), true);
            LogOut("[CONFIG] savestateLoadCustomPalettes: " + std::to_string(settings.savestateLoadCustomPalettes), true);
            
            return true;
        }
        catch (const std::exception& e) {
            LogOut("[CONFIG] Exception loading settings: " + std::string(e.what()), true);
            return false;
        }
    }
    
    bool SaveSettings() {
        try {
            const std::string tempPath = configFilePath + ".tmp";
            std::ofstream file(tempPath, std::ios::trunc);
            if (!file.is_open()) {
                LogOut("[CONFIG] SaveSettings: failed to open file for writing", true);
                return false;
            }

            auto toHexString = [](int value) -> std::string {
                if (value < 0) return std::string("-1");
                std::ostringstream oss;
                oss << "0x" << std::hex << std::uppercase << value;
                return oss.str();
            };

            file << "[General]\n";
            file << "; Compatibility flag for ImGui-hosted helpers. The configuration menu now always uses ImGui/fallback hosts.\n";
            file << "useImGui = " << (settings.useImGui ? "1" : "0") << "\n";
            file << "; Render the EFZ-native custom menu instead of the legacy ImGui list (1 = yes, 0 = no)\n";
            file << "useCustomMenu = " << (settings.useCustomMenu ? "1" : "0") << "\n";
            file << "; Enable detailed debug messages in the console (1 = yes, 0 = no)\n";
            file << "detailedLogging = " << (settings.detailedLogging ? "1" : "0") << "\n";
            file << "; Enable writing efz_training_debug.log (1 = yes, 0 = no)\n";
            file << "enableDebugFileLog = " << (settings.enableDebugFileLog ? "1" : "0") << "\n";
            file << "; Show the debug console window (1 = yes, 0 = no)\n";
            file << "enableConsole = " << (settings.enableConsole ? "1" : "0") << "\n";
            file << "; Restrict functionality to Practice Mode only (1 = yes, 0 = no)\n";
            file << "restrictToPracticeMode = " << (settings.restrictToPracticeMode ? "1" : "0") << "\n\n";
            file << "; Check GitHub once per launch for a newer release (1 = yes, 0 = no)\n";
            file << "checkForUpdates = " << (settings.checkForUpdates ? "1" : "0") << "\n\n";
            file << "; Enable this mod's framestep hotkeys/runtime (1 = yes, 0 = no)\n";
            file << "framestepEnabled = " << (settings.framestepEnabled ? "1" : "0") << "\n";
            file << "; Suppress Revival's native pause/step hotkeys while this mod owns framestep (1 = yes, 0 = no)\n";
            file << "suppressRevivalFramestep = " << (settings.suppressRevivalFramestep ? "1" : "0") << "\n\n";
            file << "; Enable FPS/timing diagnostics in logs (1 = yes, 0 = no)\n";
            file << "enableFpsDiagnostics = " << (settings.enableFpsDiagnostics ? "1" : "0") << "\n\n";
            file << "; Show per-player FrameBar overlay (1 = yes, 0 = no)\n";
            file << "showFrameBar = " << (settings.showFrameBar ? "1" : "0") << "\n\n";
            file << "frameBarMoveData = " << (settings.frameBarMoveData ? "1" : "0") << "\n\n";
            file << "menuAnimations = " << (settings.menuAnimations ? "1" : "0") << "\n\n";
            file << "; Audio volume levels as percent of the current default mix (0-100)\n";
            file << "bgmVolumePercent = " << settings.bgmVolumePercent << "\n";
            file << "seVolumePercent = " << settings.seVolumePercent << "\n\n";
            file << "; FrameBar cell cadence: 0 = subframes, 1 = visual frames\n";
            file << "frameBarTimingMode = " << settings.frameBarTimingMode << "\n";
            file << "; FrameBar detail: 0 = full, 1 = compact, 2 = bars only\n";
            file << "frameBarDetailMode = " << settings.frameBarDetailMode << "\n\n";
            file << "; Collision / hitbox overlay display (Practice only)\n";
            file << "collisionDisplayHitboxes = " << (settings.collisionDisplayHitboxes ? "1" : "0") << "\n";
            file << "collisionDisplayHurtboxes = " << (settings.collisionDisplayHurtboxes ? "1" : "0") << "\n";
            file << "collisionDisplayCollisionBoxes = " << (settings.collisionDisplayCollisionBoxes ? "1" : "0") << "\n";
            file << "collisionDisplayProjectileInteractions = " << (settings.collisionDisplayProjectileInteractions ? "1" : "0") << "\n";
            file << "; Per-player ordinary box filters (master layers above still apply)\n";
            file << "collisionDisplayP1Hitboxes = " << (settings.collisionDisplayP1Hitboxes ? "1" : "0") << "\n";
            file << "collisionDisplayP2Hitboxes = " << (settings.collisionDisplayP2Hitboxes ? "1" : "0") << "\n";
            file << "collisionDisplayP1Hurtboxes = " << (settings.collisionDisplayP1Hurtboxes ? "1" : "0") << "\n";
            file << "collisionDisplayP2Hurtboxes = " << (settings.collisionDisplayP2Hurtboxes ? "1" : "0") << "\n";
            file << "collisionDisplayP1CollisionBoxes = " << (settings.collisionDisplayP1CollisionBoxes ? "1" : "0") << "\n";
            file << "collisionDisplayP2CollisionBoxes = " << (settings.collisionDisplayP2CollisionBoxes ? "1" : "0") << "\n";
            file << "; Box fill alpha percent. Outlines stay readable.\n";
            file << "collisionDisplayFillAlphaPercent = " << settings.collisionDisplayFillAlphaPercent << "\n";
            file << "; Projectile interaction sub-layers\n";
            file << "collisionDisplayProjectileBoxes = " << (settings.collisionDisplayProjectileBoxes ? "1" : "0") << "\n";
            file << "collisionDisplayProjectileOrigins = " << (settings.collisionDisplayProjectileOrigins ? "1" : "0") << "\n";
            file << "collisionDisplayProjectileIntersections = " << (settings.collisionDisplayProjectileIntersections ? "1" : "0") << "\n";
            file << "collisionDisplayNagamoriRanges = " << (settings.collisionDisplayNagamoriRanges ? "1" : "0") << "\n";
            file << "collisionDisplayNagamoriAffected = " << (settings.collisionDisplayNagamoriAffected ? "1" : "0") << "\n\n";
            file << "; Log active player / CPU flags during Character Select (1 = yes, 0 = no)\n";
            file << "enableCharacterSelectLogger = " << (settings.enableCharacterSelectLogger ? "1" : "0") << "\n\n";
            file << "; Show a one-time Practice hint about opening the overlay (1 = yes, 0 = no)\n";
            file << "showPracticeEntryHint = " << (settings.showPracticeEntryHint ? "1" : "0") << "\n\n";
            file << "; UI scale for ImGui window (0.70 - 1.50)\n";
            file << "uiScale = " << settings.uiScale << "\n\n";
            file << "; UI font: 0 = ImGui default font, 1 = Segoe UI (Windows)\n";
            file << "uiFont = " << settings.uiFontMode << "\n\n";
            file << "; Custom menu and overlay language: en or ja\n";
            file << "language = " << LanguageIniValue(settings.language) << "\n\n";
            file << "; ImGui navigation tuning\n";
            file << "guiNavAnalogThreshold = " << settings.guiNavAnalogThreshold << "\n";
            file << "guiNavRepeatDelay = " << settings.guiNavRepeatDelay << "\n";
            file << "guiNavRepeatRate = " << settings.guiNavRepeatRate << "\n\n";
            file << "; Right-stick to mouse wheel (GUI)\n";
            file << "guiScrollRightStickEnable = " << (settings.guiScrollRightStickEnable?"1":"0") << "\n";
            file << "guiScrollRightStickScale = " << settings.guiScrollRightStickScale << "\n\n";
            file << "; RF freeze behavior (after Continuous Recovery)\n";
            file << "freezeRFAfterContRec = " << (settings.freezeRFAfterContRec?"1":"0") << "\n";
            file << "freezeRFOnlyWhenNeutral = " << (settings.freezeRFOnlyWhenNeutral?"1":"0") << "\n\n";
            file << "; Continuous Recovery gating\n";
            file << "crRequireBothNeutral = " << (settings.crRequireBothNeutral?"1":"0") << "\n";
            file << "; Delay after both players are neutral before applying CR (ms)\n";
            file << "crBothNeutralDelayMs = " << settings.crBothNeutralDelayMs << "\n\n";
            file << "; Auto-fix HP: when neutral and HP<=0, set to 9999 (1=yes,0=no)\n";
            file << "autoFixHPOnNeutral = " << (settings.autoFixHPOnNeutral?"1":"0") << "\n\n";
            file << "; Frame Advantage display duration (seconds)\n";
            file << "frameAdvantageDisplayDuration = " << settings.frameAdvantageDisplayDuration << "\n\n";
            // Combo statistics overlay
            file << "; Combo statistics overlay\n";
            file << "showComboStatisticsOverlay = " << (settings.showComboStatisticsOverlay ? "1" : "0") << "\n";
            file << "comboOverlayCompactMode = " << (settings.comboOverlayCompactMode ? "1" : "0") << "\n";
            file << "comboOverlayShowDetailRow = " << (settings.comboOverlayShowDetailRow ? "1" : "0") << "\n";
            file << "comboOverlayDetailRowSource = " << settings.comboOverlayDetailRowSource << "\n";
            file << "comboOverlayShowFinalSummary = " << (settings.comboOverlayShowFinalSummary ? "1" : "0") << "\n";
            file << "comboOverlayDisplayDuration = " << settings.comboOverlayDisplayDuration << "\n";
            file << "comboOverlayHideWhenImGuiVisible = " << (settings.comboOverlayHideWhenImGuiVisible ? "1" : "0") << "\n";
            file << "comboOverlayResumeAfterImGui = " << (settings.comboOverlayResumeAfterImGui ? "1" : "0") << "\n";
            file << "comboOverlayShowRfMultiplier = " << (settings.comboOverlayShowRfMultiplier ? "1" : "0") << "\n";
            file << "comboOverlayShowRawScale = " << (settings.comboOverlayShowRawScale ? "1" : "0") << "\n\n";
            // Practice options
            file << "; Practice: Dummy Auto-Block neutral timeout (ms) for First Hit/After First Hit modes.\n";
            file << "autoBlockNeutralTimeoutMs = " << settings.autoBlockNeutralTimeoutMs << "\n\n";
            file << "; Mission recording count-in in milliseconds (0 = off).\n";
            file << "missionRecorderCountInMs = " << settings.missionRecorderCountInMs << "\n\n";
            file << "; Virtual Cursor settings\n";
            file << "enableVirtualCursor = " << (settings.enableVirtualCursor?"1":"0") << "\n";
            file << "virtualCursorAllowWindowed = " << (settings.virtualCursorAllowWindowed?"1":"0") << "\n";
            file << "virtualCursorBaseSpeed = " << (int)settings.virtualCursorBaseSpeed << "\n";
            file << "virtualCursorFastSpeed = " << (int)settings.virtualCursorFastSpeed << "\n";
            file << "virtualCursorDpadSpeed = " << (int)settings.virtualCursorDpadSpeed << "\n";
            file << "virtualCursorAccelPower = " << settings.virtualCursorAccelPower << "\n\n";

            file << "; Controller selection: -1 = All, 0..3 = XInput user index\n";
            file << "controllerIndex = " << settings.controllerIndex << "\n\n";

            file << "; Savestate backend: 0=Custom, 1=Revival, 2=Custom with Revival fallback\n";
            file << "savestateBackendMode = " << settings.savestateBackendMode << "\n\n";
            file << "; Restore saved custom .pal usage when loading savestates (1=yes, 0=use default palettes)\n";
            file << "savestateLoadCustomPalettes = " << (settings.savestateLoadCustomPalettes ? "1" : "0") << "\n\n";

            file << "; Show the debug console window (1 = yes, 0 = no)\n";

            file << "[Hotkeys]\n";
            file << "; Use virtual-key codes (hexadecimal, e.g., 0x70 for F1)\n";
            file << "TeleportKey=" << toHexString(settings.teleportKey) << "\n";
            file << "RecordKey=" << toHexString(settings.recordKey) << "\n";
            file << "MenuKey=" << toHexString(settings.configMenuKey) << "\n";
            file << "ToggleTitleKey=" << toHexString(settings.toggleTitleKey) << "\n";
            file << "ResetFrameCounterKey=" << toHexString(settings.resetFrameCounterKey) << "\n";
            file << "SavestateSaveKey=" << toHexString(settings.savestateSaveKey) << "\n";
            file << "SavestateLoadKey=" << toHexString(settings.savestateLoadKey) << "\n";
            file << "SwitchPlayersKey=" << toHexString(settings.switchPlayersKey) << "\n";
            file << "MacroRecordKey=" << toHexString(settings.macroRecordKey) << "\n";
            file << "MacroPlayKey=" << toHexString(settings.macroPlayKey) << "\n";
            file << "MacroSlotKey=" << toHexString(settings.macroSlotKey) << "\n";
            file << "UIAcceptKey=" << toHexString(settings.uiAcceptKey) << "\n";
            file << "UIRefreshKey=" << toHexString(settings.uiRefreshKey) << "\n";
            file << "UIExitKey=" << toHexString(settings.uiExitKey) << "\n";
            file << "FramestepPauseKey=" << toHexString(settings.framestepPauseKey) << "\n";
            file << "FramestepStepKey=" << toHexString(settings.framestepStepKey) << "\n";

            file << "\n; Swap Positions custom binding\n";
            file << "SwapCustomEnabled=" << (settings.swapCustomEnabled ? "1" : "0") << "\n";
            file << "SwapCustomKey=" << toHexString(settings.swapCustomKey) << "\n";

            file << "\n; Controller bindings (symbolic names or hex). -1 disables.\n";
            file << "; NOTE: ABXY reserved: A=UI confirm, B=UI back. Defaults map actions to shoulders/triggers/sticks.\n";
            auto writePad = [&](const char* key, int mask){
                file << key << "=" << GetGamepadButtonName(mask) << "\n";
            };
            writePad("gpTeleportButton", settings.gpTeleportButton);
            writePad("gpSavePositionButton", settings.gpSavePositionButton);
            writePad("gpSwitchPlayersButton", settings.gpSwitchPlayersButton);
            writePad("gpSwapPositionsButton", settings.gpSwapPositionsButton);
            writePad("gpMacroRecordButton", settings.gpMacroRecordButton);
            writePad("gpMacroPlayButton", settings.gpMacroPlayButton);
            writePad("gpMacroSlotButton", settings.gpMacroSlotButton);
            writePad("gpToggleMenuButton", settings.gpToggleMenuButton);
            writePad("gpUiTopTabPrev", settings.gpUiTopTabPrev);
            writePad("gpUiTopTabNext", settings.gpUiTopTabNext);
            writePad("gpUiSubTabPrev", settings.gpUiSubTabPrev);
            writePad("gpUiSubTabNext", settings.gpUiSubTabNext);

            file.close();
            if (file.fail()) {
                LogOut("[CONFIG] SaveSettings: error when closing the file", true);
                DeleteFileA(tempPath.c_str());
                return false;
            }
            if (!MoveFileExA(tempPath.c_str(), configFilePath.c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                LogOut("[CONFIG] SaveSettings: failed to replace config file, error: " +
                       std::to_string(GetLastError()), true);
                DeleteFileA(tempPath.c_str());
                return false;
            }

            LoadIniFromFile();
            LogOut("[CONFIG] Settings saved to disk", true);
            return true;
        } catch (const std::exception& e) {
            LogOut(std::string("[CONFIG] SaveSettings exception: ") + e.what(), true);
            return false;
        }
    }
    
    const Settings& GetSettings() {
        return settings;
    }
    
    void SetSetting(const std::string& section, const std::string& key, const std::string& value) {
        if (TrySetAudioSetting(section, key, value)) return;
        std::string sec = ToLower(section);
        std::string k = ToLower(key);
        SetIniValue(sec, k, value);

        if (sec == "general") {
            if (k == "useimgui" || k == "useimgui ") settings.useImGui = (value == "1");
            if (k == "usecustommenu") settings.useCustomMenu = (value == "1");
            if (k == "detailedlogging") settings.detailedLogging = (value == "1");
            if (k == "enabledebugfilelog") settings.enableDebugFileLog = (value == "1");
            if (k == "enableconsole") settings.enableConsole = (value == "1");
            if (k == "showframebar") settings.showFrameBar = (value == "1");
            if (k == "framebarmovedata") settings.frameBarMoveData = (value == "1");
            if (k == "menuanimations") settings.menuAnimations = (value == "1");
            if (k == "framebartimingmode") {
                try { settings.frameBarTimingMode = std::stoi(value); } catch (...) { settings.frameBarTimingMode = 0; }
                if (settings.frameBarTimingMode < 0 || settings.frameBarTimingMode > 1) settings.frameBarTimingMode = 0;
            }
            if (k == "framebardetailmode") {
                try { settings.frameBarDetailMode = std::stoi(value); } catch (...) { settings.frameBarDetailMode = 0; }
                if (settings.frameBarDetailMode < 0 || settings.frameBarDetailMode > 2) settings.frameBarDetailMode = 0;
            }
            if (k == "collisiondisplayhitboxes") settings.collisionDisplayHitboxes = (value == "1" || value == "true");
            if (k == "collisiondisplayhurtboxes") settings.collisionDisplayHurtboxes = (value == "1" || value == "true");
            if (k == "collisiondisplaycollisionboxes") settings.collisionDisplayCollisionBoxes = (value == "1" || value == "true");
            if (k == "collisiondisplayprojectileinteractions") settings.collisionDisplayProjectileInteractions = (value == "1" || value == "true");
            if (k == "collisiondisplayp1hitboxes") settings.collisionDisplayP1Hitboxes = (value == "1" || value == "true");
            if (k == "collisiondisplayp2hitboxes") settings.collisionDisplayP2Hitboxes = (value == "1" || value == "true");
            if (k == "collisiondisplayp1hurtboxes") settings.collisionDisplayP1Hurtboxes = (value == "1" || value == "true");
            if (k == "collisiondisplayp2hurtboxes") settings.collisionDisplayP2Hurtboxes = (value == "1" || value == "true");
            if (k == "collisiondisplayp1collisionboxes") settings.collisionDisplayP1CollisionBoxes = (value == "1" || value == "true");
            if (k == "collisiondisplayp2collisionboxes") settings.collisionDisplayP2CollisionBoxes = (value == "1" || value == "true");
            if (k == "collisiondisplayfillalphapercent") {
                try { settings.collisionDisplayFillAlphaPercent = std::stoi(value); } catch (...) { settings.collisionDisplayFillAlphaPercent = 25; }
                if (settings.collisionDisplayFillAlphaPercent < 0) settings.collisionDisplayFillAlphaPercent = 0;
                if (settings.collisionDisplayFillAlphaPercent > 100) settings.collisionDisplayFillAlphaPercent = 100;
            }
            if (k == "collisiondisplayprojectileboxes") settings.collisionDisplayProjectileBoxes = (value == "1" || value == "true");
            if (k == "collisiondisplayprojectileorigins") settings.collisionDisplayProjectileOrigins = (value == "1" || value == "true");
            if (k == "collisiondisplayprojectileintersections") settings.collisionDisplayProjectileIntersections = (value == "1" || value == "true");
            if (k == "collisiondisplaynagamoriranges") settings.collisionDisplayNagamoriRanges = (value == "1" || value == "true");
            if (k == "collisiondisplaynagamoriaffected") settings.collisionDisplayNagamoriAffected = (value == "1" || value == "true");
            if (k == "restricttopracticemode") settings.restrictToPracticeMode = (value == "1");
            if (k == "checkforupdates") settings.checkForUpdates = (value == "1" || value == "true");
            if (k == "framestepenabled") settings.framestepEnabled = (value == "1" || value == "true");
            if (k == "suppressrevivalframestep") settings.suppressRevivalFramestep = (value == "1" || value == "true");
            if (k == "showpracticeentryhint") settings.showPracticeEntryHint = (value == "1");
            if (k == "savestatebackendmode") {
                try { settings.savestateBackendMode = std::stoi(value); } catch (...) { settings.savestateBackendMode = 2; }
                if (settings.savestateBackendMode < 0 || settings.savestateBackendMode > 2) settings.savestateBackendMode = 2;
            }
            if (k == "savestateloadcustompalettes") settings.savestateLoadCustomPalettes = (value == "1" || value == "true");
            if (k == "uiscale") {
                try { settings.uiScale = std::stof(value); } catch (...) {}
                settings.uiScale = ClampUiScale(settings.uiScale);
            }
            if (k == "uifont") {
                try { settings.uiFontMode = std::stoi(value); } catch (...) {}
                if (settings.uiFontMode < 0 || settings.uiFontMode > 1) settings.uiFontMode = 0;
            }
            if (k == "language") {
                settings.language = ParseLanguage(value);
                I18n::SetLanguage(static_cast<I18n::Language>(settings.language));
            }
            if (k == "guinavanalogthreshold") { try { settings.guiNavAnalogThreshold = std::stof(value); } catch(...){} }
            if (k == "guinavrepeatdelay") { try { settings.guiNavRepeatDelay = std::stof(value); } catch(...){} }
            if (k == "guinavrepeatrate") { try { settings.guiNavRepeatRate = std::stof(value); } catch(...){} }
            if (k == "guiscrollrightstickenable") settings.guiScrollRightStickEnable = (value == "1");
            if (k == "guiscrollrightstickscale") { try { settings.guiScrollRightStickScale = std::stof(value); } catch(...){} }
            if (k == "enablevirtualcursor") settings.enableVirtualCursor = (value == "1");
            if (k == "virtualcursorallowwindowed") settings.virtualCursorAllowWindowed = (value == "1");
            if (k == "virtualcursorbasespeed") { try { settings.virtualCursorBaseSpeed = std::stof(value); } catch(...){} }
            if (k == "virtualcursorfastspeed") { try { settings.virtualCursorFastSpeed = std::stof(value); } catch(...){} }
            if (k == "virtualcursordpadspeed") { try { settings.virtualCursorDpadSpeed = std::stof(value); } catch(...){} }
            if (k == "virtualcursoraccelpower") { try { settings.virtualCursorAccelPower = std::stof(value); } catch(...){} }
            if (k == "controllerindex") { try { settings.controllerIndex = std::stoi(value); } catch(...) { settings.controllerIndex = -1; } if (settings.controllerIndex < -1 || settings.controllerIndex > 3) settings.controllerIndex = -1; }
            if (k == "freezerfaftercontrec") settings.freezeRFAfterContRec = (value == "1");
            if (k == "freezerfonlywhenneutral") settings.freezeRFOnlyWhenNeutral = (value == "1");
            if (k == "crrequirebothneutral") settings.crRequireBothNeutral = (value == "1");
            if (k == "crbothneutraldelayms") { try { settings.crBothNeutralDelayMs = std::stoi(value); } catch(...){} if (settings.crBothNeutralDelayMs < 0) settings.crBothNeutralDelayMs = 0; }
            if (k == "autofixhponneutral") settings.autoFixHPOnNeutral = (value == "1");
            if (k == "frameadvantagedisplayduration") { try { settings.frameAdvantageDisplayDuration = std::stof(value); } catch(...){} }
            if (k == "showcombostatisticsoverlay") settings.showComboStatisticsOverlay = (value == "1");
            if (k == "combooverlaycompactmode") settings.comboOverlayCompactMode = (value == "1");
            if (k == "combooverlayshowdetailrow") settings.comboOverlayShowDetailRow = (value == "1");
            if (k == "combooverlaydetailrowsource") {
                try { settings.comboOverlayDetailRowSource = std::stoi(value); } catch(...) { settings.comboOverlayDetailRowSource = 0; }
                if (settings.comboOverlayDetailRowSource < 0 || settings.comboOverlayDetailRowSource > 1) settings.comboOverlayDetailRowSource = 0;
            }
            if (k == "combooverlayshowfinalsummary") settings.comboOverlayShowFinalSummary = (value == "1");
            if (k == "combooverlaydisplayduration") {
                try { settings.comboOverlayDisplayDuration = std::stof(value); } catch(...) { settings.comboOverlayDisplayDuration = 1.9f; }
                if (settings.comboOverlayDisplayDuration < 0.5f) settings.comboOverlayDisplayDuration = 0.5f;
                if (settings.comboOverlayDisplayDuration > 30.0f) settings.comboOverlayDisplayDuration = 30.0f;
            }
            if (k == "combooverlayhidewhenimguivisible") settings.comboOverlayHideWhenImGuiVisible = (value == "1");
            if (k == "combooverlayresumeafterimgui") settings.comboOverlayResumeAfterImGui = (value == "1");
            if (k == "combooverlayshowrfmultiplier") settings.comboOverlayShowRfMultiplier = (value == "1");
            if (k == "combooverlayshowrawscale") settings.comboOverlayShowRawScale = (value == "1");
            if (k == "autoblockneutraltimeoutms") { try { settings.autoBlockNeutralTimeoutMs = std::stoi(value); } catch(...) { settings.autoBlockNeutralTimeoutMs = 10000; } }
            if (k == "missionrecordercountinms") {
                try { settings.missionRecorderCountInMs = std::stoi(value); }
                catch (...) { settings.missionRecorderCountInMs = 500; }
                if (settings.missionRecorderCountInMs < 0) settings.missionRecorderCountInMs = 0;
                if (settings.missionRecorderCountInMs > 3000) settings.missionRecorderCountInMs = 3000;
            }
        }
        else if (sec == "hotkeys") {
            int intValue = ParseKeyValue(value);
            if (k == "teleportkey") settings.teleportKey = intValue;
            if (k == "recordkey") settings.recordKey = intValue;
            if (k == "menukey") settings.configMenuKey = IsMenuKeyAllowed(intValue) ? intValue : VK_ESCAPE;
            if (k == "toggletitlekey") settings.toggleTitleKey = (intValue == 0x34) ? -1 : intValue;
            if (k == "resetframecounterkey") settings.resetFrameCounterKey = (intValue == 0x35) ? -1 : intValue;
            if (k == "savestatesavekey") settings.savestateSaveKey = intValue;
            if (k == "savestateloadkey") settings.savestateLoadKey = intValue;
            if (k == "switchplayerskey") settings.switchPlayersKey = intValue;
            if (k == "macrorecordkey") settings.macroRecordKey = intValue;
            if (k == "macroplaykey") settings.macroPlayKey = intValue;
            if (k == "macroslotkey") settings.macroSlotKey = intValue;
            if (k == "uiacceptkey") settings.uiAcceptKey = intValue;
            if (k == "uirefreshkey") settings.uiRefreshKey = intValue;
            if (k == "uiexitkey") settings.uiExitKey = intValue;
            if (k == "framesteppausekey") settings.framestepPauseKey = intValue;
            if (k == "framestepstepkey") settings.framestepStepKey = intValue;
            if (k == "swapcustomenabled") settings.swapCustomEnabled = (value == "1" || value == "true");
            if (k == "swapcustomkey") settings.swapCustomKey = intValue;
            if (k == "gpteleportbutton") settings.gpTeleportButton = ParseGamepadButton(value);
            if (k == "gpsavepositionbutton") settings.gpSavePositionButton = ParseGamepadButton(value);
            if (k == "gpswitchplayersbutton") settings.gpSwitchPlayersButton = ParseGamepadButton(value);
            if (k == "gpswappositionsbutton") settings.gpSwapPositionsButton = ParseGamepadButton(value);
            if (k == "gpmacrorecordbutton") settings.gpMacroRecordButton = ParseGamepadButton(value);
            if (k == "gpmacroplaybutton") settings.gpMacroPlayButton = ParseGamepadButton(value);
            if (k == "gpmacroslotbutton") settings.gpMacroSlotButton = ParseGamepadButton(value);
            if (k == "gptogglemenubutton") settings.gpToggleMenuButton = ParseGamepadButton(value);
            if (k == "gpuitoptabprev") settings.gpUiTopTabPrev = ParseGamepadButton(value);
            if (k == "gpuitoptabnext") settings.gpUiTopTabNext = ParseGamepadButton(value);
            if (k == "gpuisubtabprev") settings.gpUiSubTabPrev = ParseGamepadButton(value);
            if (k == "gpuisubtabnext") settings.gpUiSubTabNext = ParseGamepadButton(value);
        }
    }
    
    std::string GetConfigFilePath() {
        return configFilePath;
    }

    int ParseKeyValue(const std::string& value) {
        try {
            if (value.size() >= 2 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) {
                // Older saves wrote disabled (-1) as 0xFFFFFFFF.
                return static_cast<int>(std::stoul(value.substr(2), nullptr, 16));
            }
            return std::stoi(value);
        }
        catch (...) {
            LogOut("[CONFIG] Failed to parse key value: " + value, true);
            return 0;
        }
    }
    
    std::string GetKeyName(int keyCode) {
        // Reuse existing GetKeyName from utilities.cpp
        return ::GetKeyName(keyCode);
    }

    bool IsMenuKeyAllowed(int vk) {
        if (!ConfigPolicy::IsPhysicalKeyVk(vk)) return false;
        if (vk == VK_ESCAPE) return true;
        if (vk >= 0x01 && vk <= 0x06) return false;
        switch (vk) {
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
        case VK_MENU: case VK_LMENU: case VK_RMENU: case VK_LWIN: case VK_RWIN: case VK_CLEAR:
        case VK_RETURN: case VK_SEPARATOR: case VK_SPACE: case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT:
        case VK_PRIOR: case VK_NEXT: case VK_OEM_4: case VK_OEM_6:   // fixed custom-menu nav
        case VK_F4: case VK_F5: case VK_F6: case VK_F7: case VK_F8:  // native battle hotkeys (0x765660)
            return false;
        }
        return true;
    }

    void SetEfzBoundVks(const std::array<bool, 256>& vks) {
        for (std::size_t i = 0; i < vks.size(); ++i) s_efzBoundVks[i].store(vks[i], std::memory_order_relaxed);
    }

    bool IsVkBoundByEfz(int vk) {
        return vk > 0 && vk < 256 && s_efzBoundVks[vk].load(std::memory_order_relaxed);
    }

    int ParseGamepadButton(const std::string& value) {
        std::string v = value;
        auto trim = [](std::string &s){
            size_t a = s.find_first_not_of(" \t\r\n");
            size_t b = s.find_last_not_of(" \t\r\n");
            if (a == std::string::npos) { s.clear(); return; }
            s = s.substr(a, b - a + 1);
        };
        trim(v);
        if (v.empty()) return -1;
        if (v.size() > 2 && (v[0]=='0') && (v[1]=='x' || v[1]=='X')) {
            try { return std::stoi(v.substr(2), nullptr, 16); } catch(...) { return -1; }
        }
        bool numeric = std::all_of(v.begin(), v.end(), [](unsigned char c){ return std::isdigit(c) || c=='-'; });
        if (numeric) {
            try { return std::stoi(v); } catch(...) { return -1; }
        }
        std::string u; u.reserve(v.size());
        for (char c: v) u.push_back((char)std::toupper((unsigned char)c));
        static const std::unordered_map<std::string,int> map = {
            {"A", XINPUT_GAMEPAD_A}, {"B", XINPUT_GAMEPAD_B}, {"X", XINPUT_GAMEPAD_X}, {"Y", XINPUT_GAMEPAD_Y},
            {"LB", XINPUT_GAMEPAD_LEFT_SHOULDER}, {"RB", XINPUT_GAMEPAD_RIGHT_SHOULDER},
            {"BACK", XINPUT_GAMEPAD_BACK}, {"START", XINPUT_GAMEPAD_START},
            {"L3", XINPUT_GAMEPAD_LEFT_THUMB}, {"R3", XINPUT_GAMEPAD_RIGHT_THUMB},
            {"DPAD_UP", XINPUT_GAMEPAD_DPAD_UP}, {"DPAD_DOWN", XINPUT_GAMEPAD_DPAD_DOWN},
            {"DPAD_LEFT", XINPUT_GAMEPAD_DPAD_LEFT}, {"DPAD_RIGHT", XINPUT_GAMEPAD_DPAD_RIGHT},
            // Virtual trigger pseudo-bits (outside wButtons range to avoid collision)
            {"LT", 0x10000}, {"RT", 0x20000}
        };
        auto it = map.find(u);
        if (it != map.end()) return it->second;
        return -1;
    }

    std::string GetGamepadButtonName(int mask) {
        if (mask < 0) return "-1"; // disabled
        if (mask == 0x10000) return "LT";
        if (mask == 0x20000) return "RT";
        switch(mask) {
            case XINPUT_GAMEPAD_A: return "A";
            case XINPUT_GAMEPAD_B: return "B";
            case XINPUT_GAMEPAD_X: return "X";
            case XINPUT_GAMEPAD_Y: return "Y";
            case XINPUT_GAMEPAD_LEFT_SHOULDER: return "LB";
            case XINPUT_GAMEPAD_RIGHT_SHOULDER: return "RB";
            case XINPUT_GAMEPAD_BACK: return "BACK";
            case XINPUT_GAMEPAD_START: return "START";
            case XINPUT_GAMEPAD_LEFT_THUMB: return "L3";
            case XINPUT_GAMEPAD_RIGHT_THUMB: return "R3";
            case XINPUT_GAMEPAD_DPAD_UP: return "DPAD_UP";
            case XINPUT_GAMEPAD_DPAD_DOWN: return "DPAD_DOWN";
            case XINPUT_GAMEPAD_DPAD_LEFT: return "DPAD_LEFT";
            case XINPUT_GAMEPAD_DPAD_RIGHT: return "DPAD_RIGHT";
            default: {
                std::ostringstream oss; oss << "0x" << std::hex << std::uppercase << mask; return oss.str();
            }
        }
    }

    bool LoadIniFromFile() {
        IniMap parsed;
        std::ifstream file(configFilePath);
        if (!file.is_open()) {
            LogOut("[CONFIG] LoadIniFromFile: failed to open ini file", true);
            std::lock_guard<std::mutex> lock(s_iniMutex);
            s_ini.clear();
            return false;
        }
        std::string line;
        std::string currentSection;
        bool firstLine = true;
        while (std::getline(file, line)) {
            if (firstLine) {
                ConfigPolicy::StripUtf8Bom(line);
                firstLine = false;
            }
            auto trim = [](std::string &s){
                size_t start = s.find_first_not_of(" \t\r\n");
                size_t end = s.find_last_not_of(" \t\r\n");
                if (start == std::string::npos) { s.clear(); return; }
                s = s.substr(start, end - start + 1);
            };
            trim(line);
            if (line.empty()) continue;
            if (line[0] == ';' || line[0] == '#') continue;
            std::string sectionName;
            if (ConfigPolicy::ParseSectionHeader(line, sectionName)) {
                currentSection = ToLower(sectionName);
                continue;
            }
            size_t hashPos = line.find('#');
            size_t semiPos = line.find(';');
            auto minPos = [](size_t a, size_t b) -> size_t {
                if (a == std::string::npos) return b;
                if (b == std::string::npos) return a;
                return (a < b) ? a : b;
            };
            size_t commentPos = minPos(hashPos, semiPos);
            std::string kv = (commentPos == std::string::npos) ? line : line.substr(0, commentPos);
            trim(kv);
            if (kv.empty()) continue;
            size_t eq = kv.find('=');
            if (eq == std::string::npos) continue;
            std::string key = kv.substr(0, eq);
            std::string value = kv.substr(eq + 1);
            trim(key); trim(value);
            if (!currentSection.empty() && !key.empty()) {
                parsed[currentSection][ToLower(key)] = value;
            }
        }
        std::lock_guard<std::mutex> lock(s_iniMutex);
        s_ini.swap(parsed);
        return true;
    }
}
