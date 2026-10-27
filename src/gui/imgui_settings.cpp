#include "../include/gui/imgui_settings.h"
#include "../include/gui/imgui_gui.h"
#include "../include/utils/controller_names.h"
#include "../include/utils/config.h"
#include "../include/core/logger.h"
#include "../include/utils/switch_players.h"
#include "../include/game/combo_overlay.h"
#include "../include/game/game_state.h"
#include "../include/utils/debug_log.h"
#include "../include/input/framestep.h"
#include "../include/utils/audio_control.h"
#include "../include/utils/extended_config_bridge.h"
#include "../include/utils/network.h"
#include <windows.h>
#include <Xinput.h>
#include "../include/utils/xinput_shim.h"

namespace ImGuiSettings {
    // Pseudo-bits for triggers when mapping to a button mask
    static constexpr uint32_t GP_LT_BIT = 0x10000; // Left Trigger
    static constexpr uint32_t GP_RT_BIT = 0x20000; // Right Trigger
    static constexpr int GP_TRIGGER_THRESH = 30;

    struct KeyboardCaptureState {
        bool active = false;
        bool armed = false;
        std::string which;
        bool prevPressed[256] = {};
    };

    static bool IsKeyboardBindableVk(int vk) {
        if (vk <= 0) return false;
        if (vk >= 0x01 && vk <= 0x06) return false;
        if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT) return false;
        if (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL) return false;
        if (vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU) return false;
        if (vk == VK_LWIN || vk == VK_RWIN) return false;
        if (vk == VK_CLEAR) return false;
        if (vk == VK_ESCAPE) return false;
        if (vk == Config::GetSettings().configMenuKey) return false;
        return true;
    }

    static void BeginKeyboardCapture(KeyboardCaptureState& state, const char* cfgKey) {
        state.active = true;
        state.armed = false;
        state.which = cfgKey ? cfgKey : "";
        for (int vk = 0; vk < 256; ++vk) {
            state.prevPressed[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
        }
    }

    static void CancelKeyboardCapture(KeyboardCaptureState& state) {
        state.active = false;
        state.armed = false;
        state.which.clear();
    }

    static bool PollKeyboardCapture(KeyboardCaptureState& state,
                                    int& keyCode,
                                    const char* cfgKey,
                                    bool disallowFooterKeys = false) {
        if (!state.active || state.which != (cfgKey ? cfgKey : "")) {
            return false;
        }

        const bool escNow = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
        const bool escWas = state.prevPressed[VK_ESCAPE];
        state.prevPressed[VK_ESCAPE] = escNow;
        if (escNow && !escWas) {
            CancelKeyboardCapture(state);
            return false;
        }

        bool anyHeld = false;
        int captured = 0;
        for (int vk = 0; vk < 256; ++vk) {
            if (!IsKeyboardBindableVk(vk)) continue;

            const bool now = (GetAsyncKeyState(vk) & 0x8000) != 0;
            const bool was = state.prevPressed[vk];
            state.prevPressed[vk] = now;

            if (now) anyHeld = true;
            if (state.armed && now && !was && captured == 0) {
                captured = vk;
            }
        }

        if (!state.armed) {
            if (!anyHeld) state.armed = true;
            return false;
        }

        if (captured == 0) {
            return false;
        }

        if (disallowFooterKeys && (captured == VK_RETURN || captured == VK_SPACE)) {
            LogOut("[CONFIG/UI] Disallowed footer key (Enter/Space) ignored", false);
            CancelKeyboardCapture(state);
            return false;
        }

        keyCode = captured;
        char hexBuf[16];
        snprintf(hexBuf, sizeof(hexBuf), "0x%X", keyCode);
        Config::SetSetting("Hotkeys", cfgKey, hexBuf);
        CancelKeyboardCapture(state);
        return true;
    }

    static void CheckboxApply(const char* label, bool& value, const char* section, const char* key) {
        if (ImGui::Checkbox(label, &value)) {
            Config::SetSetting(section, key, value ? "1" : "0");
            if (std::string(key) == "DetailedLogging") {
                detailedLogging.store(value);
                LogOut(std::string("[CONFIG/UI] detailedLogging set to ") + (value ? "true" : "false"), false);
            }
            else if (std::string(key) == "enableDebugFileLog") {
                if (!DebugLog::SetEnabled(value)) {
                    value = false;
                    Config::SetSetting(section, key, "0");
                    LogOut("[CONFIG/UI][SETUP-FAILURE] Could not enable efz_training_debug.log", false);
                } else {
                    LogOut(std::string("[CONFIG/UI] enableDebugFileLog set to ") +
                           (value ? "true" : "false"), false);
                }
            }
        }
    }

    static bool InputKeyHex(const char* label, int& keyCode, const char* setKeyName) {
        char buf[16] = {0};
        if (keyCode < 0) {
            snprintf(buf, sizeof(buf), "-1");
        } else {
            snprintf(buf, sizeof(buf), "0x%X", keyCode);
        }
        ImGui::SetNextItemWidth(90);
        if (ImGui::InputText(label, buf, sizeof(buf))) {
            int parsed = Config::ParseKeyValue(buf);
            keyCode = parsed;
            Config::SetSetting("Hotkeys", setKeyName, buf);
            return true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", keyCode < 0 ? "Disabled" : GetKeyName(keyCode).c_str());
        ImGui::SameLine();
        static KeyboardCaptureState captureState;
        std::string btnId = std::string("Bind##") + setKeyName;
        if (!captureState.active) {
            if (ImGui::Button(btnId.c_str())) {
                BeginKeyboardCapture(captureState, setKeyName);
                LogOut(std::string("[CONFIG/UI] Capturing key for ") + setKeyName + "... press any key", false);
            }
        } else if (captureState.which == setKeyName) {
            ImGui::TextColored(ImVec4(1,1,0,1), captureState.armed ? "Press any key..." : "Release all keys...");
            if (PollKeyboardCapture(captureState, keyCode, setKeyName)) {
                LogOut(std::string("[CONFIG/UI] ") + setKeyName + " bound to " + GetKeyName(keyCode), false);
            }
        }
        return false;
    }

    static uint32_t PollAggregatedGamepadMask(uint32_t* padsConnectedMask = nullptr) {
        XInputShim::RefreshSnapshotOncePerFrame();
        XInputShim::Snapshot snapshot{};
        XInputShim::CopySnapshot(snapshot);
        uint32_t agg = 0;
        const uint32_t connected = snapshot.connectedMask;
        for (int i = 0; i < 4; ++i) {
            if (!snapshot.IsConnected(i)) continue;
            const XINPUT_STATE& state = snapshot.states[i];
            agg |= state.Gamepad.wButtons;
            if (state.Gamepad.bLeftTrigger > GP_TRIGGER_THRESH) agg |= GP_LT_BIT;
            if (state.Gamepad.bRightTrigger > GP_TRIGGER_THRESH) agg |= GP_RT_BIT;
        }
        if (padsConnectedMask) *padsConnectedMask = connected;
        return agg;
    }

    static void FooterHotkeyRebind(const char* actionLabel, int& keyCode, const char* cfgKey) {
        ImGui::TextUnformatted(actionLabel);
        ImGui::SameLine();
        ImGui::TextDisabled("[%s]", GetKeyName(keyCode).c_str());
        ImGui::SameLine();
        std::string btnId = std::string("Rebind##") + cfgKey;
        static KeyboardCaptureState captureState;
        if (!captureState.active) {
            if (ImGui::Button(btnId.c_str())) { BeginKeyboardCapture(captureState, cfgKey); }
        } else if (captureState.which == cfgKey) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1,1,0,1), captureState.armed ? "Press a key (no Enter/Space)..." : "Release all keys...");
            if (PollKeyboardCapture(captureState, keyCode, cfgKey, true)) {
                LogOut(std::string("[CONFIG/UI] ") + cfgKey + " footer key -> " + GetKeyName(keyCode), false);
            }
        }
    }

    void RenderSettingsTab() {
        const Config::Settings& cfg = Config::GetSettings();

        bool logVerbose = cfg.detailedLogging;
        bool debugFileLog = cfg.enableDebugFileLog;
        bool fpsDiag = cfg.enableFpsDiagnostics;
        
        bool showPracticeHint = cfg.showPracticeEntryHint;
        bool enableConsole = cfg.enableConsole;
        float uiScale = cfg.uiScale;
        int uiFontMode = cfg.uiFontMode; // 0=Default, 1=Segoe UI

    if (ImGui::BeginTabBar("##SettingsTabs")) {
            if (ImGui::BeginTabItem("General")) {
                ImGui::SeparatorText("User Interface");
                
                

                ImGui::Text("UI Scale:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(140);
                if (ImGui::SliderFloat("##UiScale", &uiScale, 0.70f, 1.50f, "%.2f")) {
                    Config::SetSetting("General", "uiScale", std::to_string(uiScale));
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset##UiScale")) {
                    uiScale = 0.90f;
                    Config::SetSetting("General", "uiScale", "0.90");
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(applies immediately)");

                ImGui::Text("UI Font:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(180);
                const char* fontItems[] = { "Default (ImGui)", "Segoe UI (Windows)" };
                if (ImGui::Combo("##UiFont", &uiFontMode, fontItems, IM_ARRAYSIZE(fontItems))) {
                    Config::SetSetting("General", "uiFont", std::to_string(uiFontMode));
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(applies immediately)");

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::SeparatorText("Display Settings");

                float faDuration = cfg.frameAdvantageDisplayDuration;
                ImGui::Text("FA Display Duration:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(200);
                if (ImGui::SliderFloat("##FADuration", &faDuration, 0.5f, 30.0f, "%.1f sec")) {
                    Config::SetSetting("General", "frameAdvantageDisplayDuration", std::to_string(faDuration));
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset##FADuration")) {
                    faDuration = 1.9f;
                    Config::SetSetting("General", "frameAdvantageDisplayDuration", "1.9");
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("How long frame advantage and gap messages stay visible (default: 1.9 seconds)");
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::SeparatorText("Combo Statistics");

                ImGui::TextWrapped("Combo summary persists until you leave the match. Enable or disable the overlay from Main -> Options.");

                bool hideComboWhenImGuiVisible = cfg.comboOverlayHideWhenImGuiVisible;
                if (ImGui::Checkbox("Hide Overlay While ImGui Is Open", &hideComboWhenImGuiVisible)) {
                    Config::SetSetting("General", "comboOverlayHideWhenImGuiVisible", hideComboWhenImGuiVisible ? "1" : "0");
                    ComboOverlay::ClearDisplay();
                }

                bool resumeComboAfterImGui = cfg.comboOverlayResumeAfterImGui;
                if (ImGui::Checkbox("Resume Summary After Closing ImGui", &resumeComboAfterImGui)) {
                    Config::SetSetting("General", "comboOverlayResumeAfterImGui", resumeComboAfterImGui ? "1" : "0");
                    ComboOverlay::ClearDisplay();
                }

                bool showRfMultiplier = cfg.comboOverlayShowRfMultiplier;
                if (ImGui::Checkbox("Show RF Multiplier", &showRfMultiplier)) {
                    Config::SetSetting("General", "comboOverlayShowRfMultiplier", showRfMultiplier ? "1" : "0");
                    ComboOverlay::ClearDisplay();
                }

                bool showRawScale = cfg.comboOverlayShowRawScale;
                if (ImGui::Checkbox("Show Raw Scale Value", &showRawScale)) {
                    Config::SetSetting("General", "comboOverlayShowRawScale", showRawScale ? "1" : "0");
                    ComboOverlay::ClearDisplay();
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::SeparatorText("Practice Options");

                
                if (ImGui::Checkbox("Show Practice overlay hint once per session", &showPracticeHint)) {
                    Config::SetSetting("General", "showPracticeEntryHint", showPracticeHint ? "1" : "0");
                }
                ImGui::TextDisabled("Appears when the first Practice match starts");

                float missionCountInSeconds =
                    static_cast<float>(cfg.missionRecorderCountInMs) / 1000.0f;
                ImGui::Text("Mission recording count-in:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(200);
                if (ImGui::SliderFloat("##MissionRecordCountIn",
                                       &missionCountInSeconds,
                                       0.0f, 3.0f, "%.1f sec")) {
                    const int rawMilliseconds = static_cast<int>(
                        missionCountInSeconds * 1000.0f + 0.5f);
                    const int milliseconds = ((rawMilliseconds + 50) / 100) * 100;
                    Config::SetSetting("General", "missionRecorderCountInMs",
                                       std::to_string(milliseconds));
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset##MissionRecordCountIn")) {
                    Config::SetSetting("General", "missionRecorderCountInMs", "500");
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Delay before a mission recording starts. The exact baseline is saved immediately after it. Set 0 to disable the countdown.");
                }

                int abTimeoutMs = cfg.autoBlockNeutralTimeoutMs;
                int abTimeoutSec = (abTimeoutMs + 500) / 1000; // round to nearest second for UI
                ImGui::Text("Auto-Block neutral timeout:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(200);
                if (ImGui::SliderInt("##ABTimeout", &abTimeoutSec, 0, 60, "%d sec")) {
                    if (abTimeoutSec < 0) abTimeoutSec = 0; 
                    if (abTimeoutSec > 600) abTimeoutSec = 600; // hard cap
                    int ms = abTimeoutSec * 1000;
                    Config::SetSetting("General", "autoBlockNeutralTimeoutMs", std::to_string(ms));
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset##ABTimeout")) {
                    Config::SetSetting("General", "autoBlockNeutralTimeoutMs", "10000");
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("When using First Hit/After First Hit modes, require this much continuous neutral before re-arming/disabling. 0 = toggle on the first neutral frame.");
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::SeparatorText("Audio");

                int bgmVolumePercent = AudioControl::GetConfiguredBgmVolumePercent();
                ImGui::Text("BGM Volume:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(200);
                if (ImGui::SliderInt("##BgmVolumePercent", &bgmVolumePercent, 0, 100, "%d%%")) {
                    ExtendedConfigBridge::PublishAudioLaneSetting(true, bgmVolumePercent);
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset##BgmVolumePercent")) {
                    ExtendedConfigBridge::PublishAudioLaneSetting(true, 100);
                }

                int seVolumePercent = AudioControl::GetConfiguredSeVolumePercent();
                ImGui::Text("SE Volume:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(200);
                if (ImGui::SliderInt("##SeVolumePercent", &seVolumePercent, 0, 100, "%d%%")) {
                    ExtendedConfigBridge::PublishAudioLaneSetting(false, seVolumePercent);
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset##SeVolumePercent")) {
                    ExtendedConfigBridge::PublishAudioLaneSetting(false, 100);
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::SeparatorText("Recovery & Health");

                bool crBoth = cfg.crRequireBothNeutral;
                if (ImGui::Checkbox("CR: require both players neutral", &crBoth)) {
                    Config::SetSetting("General", "crRequireBothNeutral", crBoth ? "1" : "0");
                }
                int crDelay = cfg.crBothNeutralDelayMs;
                ImGui::SetNextItemWidth(180);
                if (ImGui::InputInt("CR: neutral delay (ms)", &crDelay)) {
                    if (crDelay < 0) crDelay = 0; if (crDelay > 5000) crDelay = 5000;
                    Config::SetSetting("General", "crBothNeutralDelayMs", std::to_string(crDelay));
                }

                bool autoFixHp = cfg.autoFixHPOnNeutral;
                if (ImGui::Checkbox("Auto-fix HP<=0 in neutral (set to 9999)", &autoFixHp)) {
                    Config::SetSetting("General", "autoFixHPOnNeutral", autoFixHp ? "1" : "0");
                }

                ImGui::Dummy(ImVec2(1,2));
                bool freezeAfterCR = cfg.freezeRFAfterContRec;
                if (ImGui::Checkbox("Freeze RF after Continuous Recovery", &freezeAfterCR)) {
                    Config::SetSetting("General", "freezeRFAfterContRec", freezeAfterCR ? "1" : "0");
                }
                bool freezeOnlyNeutral = cfg.freezeRFOnlyWhenNeutral;
                if (ImGui::Checkbox("Freeze RF only when neutral", &freezeOnlyNeutral)) {
                    Config::SetSetting("General", "freezeRFOnlyWhenNeutral", freezeOnlyNeutral ? "1" : "0");
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::SeparatorText("Advanced");

                CheckboxApply("Detailed logging", logVerbose, "General", "DetailedLogging");

                if (ImGui::Checkbox("Show debug console (restart not required)", &enableConsole)) {
                    Config::SetSetting("General", "enableConsole", enableConsole ? "1" : "0");
                    if (enableConsole) {
                        if (!GetConsoleWindow()) { CreateDebugConsole(); } else { SetConsoleVisibility(true); }
                        SetConsoleReady(true);
                        FlushPendingConsoleLogs();
                    } else {
                        SetConsoleVisibility(false);
                    }
                }

                ImGui::Spacing();
                ImGui::Separator();
                if (ImGui::Button("Save to disk")) {
                    Config::SaveSettings();
                    LogOut("[CONFIG/UI] Settings saved to ini", false);
                }

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Keyboard Hotkeys")) {
                int teleport = cfg.teleportKey;
                int record = cfg.recordKey;
                int toggleTitle = cfg.toggleTitleKey;
                int resetFrame = cfg.resetFrameCounterKey;

                int menuKey = cfg.configMenuKey;
                InputKeyHex("Open Menu", menuKey, "MenuKey");
                ImGui::SameLine();
                if (ImGui::SmallButton("Use Esc##MenuKey")) Config::SetSetting("Hotkeys", "MenuKey", "0x1B");
                InputKeyHex("Teleport/Load", teleport, "TeleportKey");
                InputKeyHex("Record/Save", record, "RecordKey");
                InputKeyHex("Toggle Title", toggleTitle, "ToggleTitleKey");
                InputKeyHex("Reset Frame Counter", resetFrame, "ResetFrameCounterKey");

                ImGui::Separator();
                ImGui::SeparatorText("Practice / Macros");
                int switchPlayers = cfg.switchPlayersKey;
                int macroRecord = cfg.macroRecordKey;
                int macroPlay   = cfg.macroPlayKey;
                int macroSlot   = cfg.macroSlotKey;
                InputKeyHex("Switch Players (Practice)", switchPlayers, "SwitchPlayersKey");
                InputKeyHex("Macro: Record", macroRecord, "MacroRecordKey");
                InputKeyHex("Macro: Play", macroPlay, "MacroPlayKey");
                InputKeyHex("Macro: Next Slot", macroSlot, "MacroSlotKey");

                ImGui::Separator();
                ImGui::SeparatorText("Framestep");
                bool fsEnabled = cfg.framestepEnabled;
                if (ImGui::Checkbox("Enable Framestep", &fsEnabled)) {
                    Config::SetSetting("General", "framestepEnabled", fsEnabled ? "1" : "0");
                }
                bool suppressRevival = cfg.suppressRevivalFramestep;
                if (ImGui::Checkbox("Suppress Revival Framestep", &suppressRevival)) {
                    Config::SetSetting("General", "suppressRevivalFramestep", suppressRevival ? "1" : "0");
                }
                int fsPause = cfg.framestepPauseKey;
                int fsStep  = cfg.framestepStepKey;
                InputKeyHex("Framestep: Toggle Pause", fsPause, "FramestepPauseKey");
                InputKeyHex("Framestep: Step Frame", fsStep, "FramestepStepKey");

                ImGui::Separator();
                ImGui::SeparatorText("Swap Positions");
                bool swapEnabled = cfg.swapCustomEnabled;
                if (ImGui::Checkbox("Enable custom swap key", &swapEnabled)) {
                    Config::SetSetting("Hotkeys", "SwapCustomEnabled", swapEnabled ? "1" : "0");
                }
                int swapKey = cfg.swapCustomKey;
                InputKeyHex("Custom swap key", swapKey, "SwapCustomKey");

                ImGui::Separator();
                if (ImGui::Button("Save to disk")) {
                    Config::SaveSettings();
                    LogOut("[CONFIG/UI] Settings saved to ini", false);
                }
                ImGui::SameLine();
                if (ImGui::Button("Reload from disk")) {
                    if (Config::LoadSettings()) {
                        detailedLogging.store(Config::GetSettings().detailedLogging);
                        if (!DebugLog::SetEnabled(Config::GetSettings().enableDebugFileLog)) {
                            Config::SetSetting("General", "enableDebugFileLog", "0");
                            LogOut("[CONFIG/UI][SETUP-FAILURE] Reloaded settings, but could not enable efz_training_debug.log", false);
                        } else {
                            LogOut("[CONFIG/UI] Settings reloaded from ini", false);
                        }
                    } else {
                        LogOut("[CONFIG/UI][SETUP-FAILURE] Settings reload failed", false);
                    }
                }

                ImGui::Dummy(ImVec2(1,6));
                ImGui::SeparatorText("Footer Hotkeys");
                int accept = cfg.uiAcceptKey;
                int refresh = cfg.uiRefreshKey;
                int exitK = cfg.uiExitKey;
                FooterHotkeyRebind("Apply", accept, "UIAcceptKey");
                FooterHotkeyRebind("Refresh", refresh, "UIRefreshKey");
                FooterHotkeyRebind("Exit", exitK, "UIExitKey");
                ImGui::TextDisabled("Disallowed: Enter / Escape / Space");

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Controller")) {
                ImGui::SeparatorText("Bindings");
                ImGui::TextWrapped("%s", "Rebind: activates capture, waits for release, then captures the next button/trigger pressed. Disable sets -1.");
                ImGui::Separator();

                struct Row { const char* label; const char* key; int val; };
                Row rows[] = {
                    {"Teleport / Load", "gpTeleportButton", cfg.gpTeleportButton},
                    {"Save Position",   "gpSavePositionButton", cfg.gpSavePositionButton},
                    {"Switch Players",  "gpSwitchPlayersButton", cfg.gpSwitchPlayersButton},
                    {"Swap Positions",  "gpSwapPositionsButton", cfg.gpSwapPositionsButton},
                    {"Macro Record",    "gpMacroRecordButton", cfg.gpMacroRecordButton},
                    {"Macro Play",      "gpMacroPlayButton", cfg.gpMacroPlayButton},
                    {"Macro Slot Next", "gpMacroSlotButton", cfg.gpMacroSlotButton},
                    {"Toggle Menu",     "gpToggleMenuButton", cfg.gpToggleMenuButton}
                };

                static bool capturing = false;
                static std::string which;
                static bool armed = false;
                static uint32_t prevAggMask = 0;
                static double startTime = 0.0;

                const size_t rowCount = sizeof(rows) / sizeof(rows[0]);
                for (size_t i = 0; i < rowCount; ++i) {
                    const Row& r = rows[i];
                    ImGui::PushID(r.key);
                    ImGui::TextUnformatted(r.label);
                    ImGui::SameLine();
                    ImGui::TextDisabled("[%s]", Config::GetGamepadButtonName(r.val).c_str());
                    ImGui::SameLine();
                    std::string btn = std::string("Rebind##") + r.key;
                    if (!capturing) {
                        if (ImGui::SmallButton(btn.c_str())) {
                            capturing = true; which = r.key; armed = false; prevAggMask = 0; startTime = ImGui::GetTime();
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Disable")) {
                            Config::SetSetting("Hotkeys", r.key, "-1");
                        }
                    } else if (capturing && which == r.key) {
                        ImGui::TextColored(ImVec4(1,1,0,1), armed ? "Waiting for next input..." : "Release all buttons/triggers...");
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Cancel")) { capturing = false; which.clear(); armed=false; }

                        uint32_t padsMask = 0;
                        uint32_t agg = PollAggregatedGamepadMask(&padsMask);

                        const double elapsed = ImGui::GetTime() - startTime;
                        if (!armed) {
                            if (elapsed > 0.12 && agg == 0) { // 120ms debounce and full release
                                armed = true;
                                prevAggMask = 0;
                            }
                        } else {
                            uint32_t edge = agg & ~prevAggMask;
                            if (edge != 0) {
                                std::string name = Config::GetGamepadButtonName((int)edge);
                                Config::SetSetting("Hotkeys", which, name);
                                capturing = false; which.clear(); armed = false; prevAggMask = 0; startTime = 0.0;
                            } else {
                                prevAggMask = agg;
                            }
                        }
                    }
                    ImGui::PopID();
                }

                ImGui::Separator();
                if (ImGui::Button("Save Controller Binds")) { Config::SaveSettings(); LogOut("[CONFIG/UI] Controller binds saved", false); }
                ImGui::SameLine(); if (ImGui::Button("Reload")) { Config::LoadSettings(); LogOut("[CONFIG/UI] Controller binds reloaded", false); }

                ImGui::Dummy(ImVec2(1,6));
                ImGui::SeparatorText("Device Selection");
                {
                    int idx = Config::GetSettings().controllerIndex;
                    int current = idx; // -1 = All
                    std::string labelAll = "All (Any)";
                    // Names are pre-published by the watcher; the render thread must not enumerate XInput/RawInput.
                    char nm0[96] = {}, nm1[96] = {}, nm2[96] = {}, nm3[96] = {};
                    XInputShim::GetPublishedControllerName(0, nm0, sizeof(nm0));
                    XInputShim::GetPublishedControllerName(1, nm1, sizeof(nm1));
                    XInputShim::GetPublishedControllerName(2, nm2, sizeof(nm2));
                    XInputShim::GetPublishedControllerName(3, nm3, sizeof(nm3));
                    const char* items[] = { labelAll.c_str(), nm0, nm1, nm2, nm3 };
                    int comboIndex = (current < 0) ? 0 : (current + 1);
                    ImGui::TextUnformatted("Controller for mod inputs"); ImGui::SameLine();
                    if (ImGui::BeginCombo("##controllerIndex", items[comboIndex])) {
                        for (int i = 0; i < 5; ++i) {
                            bool sel = (i == comboIndex);
                            if (ImGui::Selectable(items[i], sel)) {
                                int newVal = (i == 0) ? -1 : (i - 1);
                                Config::SetSetting("General", "controllerIndex", std::to_string(newVal));
                            }
                            if (sel) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SameLine(); if (ImGui::SmallButton("Save Controller")) { Config::SaveSettings(); }
                }

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Debug")) {
                ImGui::SeparatorText("Debug Settings");
                CheckboxApply("Debug file log (efz_training_debug.log)", debugFileLog, "General", "enableDebugFileLog");
                CheckboxApply("Enable FPS/timing diagnostics", fpsDiag, "General", "enableFpsDiagnostics");
                
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                
                ImGuiGui::RenderDebugInputTab();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::TextDisabled("Config file: %s", Config::GetConfigFilePath().c_str());
    }
}
