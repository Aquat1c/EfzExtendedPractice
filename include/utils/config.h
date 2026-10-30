#pragma once
#include <windows.h>
#include <string>
#include <unordered_map>
#include <array>
#include "config_policy.h"

namespace Config {
    using ConfigPolicy::ResolveHotkey;
    // Caller serializes the accepted audio control transaction; never republishes.
    void SetAudioSettingsMirror(int bgm, int se);
    struct Settings {
        bool useImGui;
        bool useCustomMenu;
        bool detailedLogging;
        bool enableDebugFileLog;
        bool restrictToPracticeMode;
    bool framestepEnabled;
    bool suppressRevivalFramestep; // Suppress Revival's pause/step hotkeys while our framestep is active
    bool enableConsole;
    bool enableFpsDiagnostics;
    bool checkForUpdates;
    bool showFrameBar;
    bool frameBarMoveData;       // startup/active/recovery line above the frame bar
    bool menuAnimations;         // custom menu slides; off = instant
    int  bgmVolumePercent;       // 0..100, 100 = current/default BGM level
    int  seVolumePercent;        // 0..100, 100 = current/default SE level
    int  frameBarTimingMode;     // 0 = subframes, 1 = visual frames
    int  frameBarDetailMode;     // 0 = full, 1 = compact, 2 = bars only
    bool collisionDisplayHitboxes;
    bool collisionDisplayHurtboxes;
    bool collisionDisplayCollisionBoxes;
    bool collisionDisplayProjectileInteractions;
    // Per-player filters; the layer toggles above remain masters. Default ON.
    bool collisionDisplayP1Hitboxes;
    bool collisionDisplayP2Hitboxes;
    bool collisionDisplayP1Hurtboxes;
    bool collisionDisplayP2Hurtboxes;
    bool collisionDisplayP1CollisionBoxes;
    bool collisionDisplayP2CollisionBoxes;
    int  collisionDisplayFillAlphaPercent; // 0..100 box fill alpha
    bool collisionDisplayProjectileBoxes;
    bool collisionDisplayProjectileOrigins;
    bool collisionDisplayProjectileIntersections;
    bool collisionDisplayNagamoriRanges;
    bool collisionDisplayNagamoriAffected;
    bool enableCharacterSelectLogger;
    bool showPracticeEntryHint;
    float uiScale;               // 0.70..1.50
    int uiFontMode;              // 0=ImGui default, 1=Segoe UI
    int language;                // I18n::Language; INI value en|ja
    int savestateBackendMode;    // 0=Custom, 1=Revival, 2=Custom with Revival fallback
    bool savestateLoadCustomPalettes; // 1=restore saved custom palette usage, 0=force default palettes on load

        float guiNavAnalogThreshold; // 0..1; stick treated as digital dpad for fallback nav
        float guiNavRepeatDelay;     // seconds before key repeat starts
        float guiNavRepeatRate;      // seconds between repeats

        bool  guiScrollRightStickEnable;
        float guiScrollRightStickScale;  // notches per second at full tilt

        bool enableVirtualCursor;
        bool virtualCursorAllowWindowed;
        float virtualCursorBaseSpeed;      // px/sec
        float virtualCursorFastSpeed;      // px/sec (shoulder)
        float virtualCursorDpadSpeed;      // px/sec (dpad nudge)
        float virtualCursorAccelPower;     // 1.0 = linear, >1 slower near center

    // -1 = All controllers, 0..3 = specific XInput user index
    int controllerIndex;

        int teleportKey;
        int recordKey;
        int configMenuKey;          // VK; VK_ESCAPE intercepts Esc, any other key leaves Esc to EFZ
        int toggleTitleKey;
        int resetFrameCounterKey;
        int helpKey;                // Legacy, ignored
        int toggleImGuiKey;         // Legacy, ignored
        int savestateSaveKey;
        int savestateLoadKey;

        int switchPlayersKey;   // Default: 'L'
        int macroRecordKey;     // Default: 'I'
        int macroPlayKey;       // Default: 'O'
        int macroSlotKey;       // Default: 'K'

        // vanilla EFZ / supported Revival
        int framestepPauseKey;  // Default: VK_SPACE
        int framestepStepKey;   // Default: 'P'

        int uiAcceptKey;
        int uiRefreshKey;
        int uiExitKey;
    bool swapCustomEnabled;
    int  swapCustomKey;

        // XInput button bitmasks; -1 = disabled
        int gpTeleportButton;       // Default: XINPUT_GAMEPAD_BACK
        int gpSavePositionButton;   // Default: XINPUT_GAMEPAD_LEFT_THUMB
    int gpSwitchPlayersButton;  // Default: XINPUT_GAMEPAD_RIGHT_SHOULDER (RB)
    // ABXY reserved for UI confirm/back
    int gpSwapPositionsButton;  // Default: XINPUT_GAMEPAD_RIGHT_THUMB (R3)
    int gpMacroRecordButton;    // Default: XINPUT_GAMEPAD_LEFT_SHOULDER (LB)
    int gpMacroPlayButton;      // Default: RT (virtual trigger 0x20000)
    int gpMacroSlotButton;      // Default: LT (virtual trigger 0x10000)
        int gpToggleMenuButton;     // Default: XINPUT_GAMEPAD_START
        int gpToggleImGuiButton;    // Legacy, ignored

    // Top-level tabs cycle (logical order: Main, Auto Action, Settings, Character, Help)
    int gpUiTopTabPrev;         // Default: LB
    int gpUiTopTabNext;         // Default: RB
    int gpUiSubTabPrev;         // Default: LT
    int gpUiSubTabNext;         // Default: RT

        bool freezeRFAfterContRec;       // Start RF freeze after Continuous Recovery enforcement
        bool freezeRFOnlyWhenNeutral;    // Maintain RF freeze only while in neutral states

        bool crRequireBothNeutral;       // Require BOTH players to be neutral before CR applies
        int  crBothNeutralDelayMs;       // ms delay after both-neutral before applying CR

        bool autoFixHPOnNeutral;         // If neutral and HP<=0, set HP to 9999 automatically

        float frameAdvantageDisplayDuration; // seconds FA/RG messages stay visible

        bool showComboStatisticsOverlay;
        bool comboOverlayCompactMode;
        bool comboOverlayShowDetailRow;
        int  comboOverlayDetailRowSource;   // 0=Combo State, 1=Last Hit
        bool comboOverlayShowFinalSummary;
        float comboOverlayDisplayDuration;
        bool comboOverlayHideWhenImGuiVisible;
        bool comboOverlayResumeAfterImGui;
        bool comboOverlayShowRfMultiplier;
        bool comboOverlayShowRawScale;

        // ms; continuous neutral timeout for event-driven modes
        int autoBlockNeutralTimeoutMs;
        // ms of pre-record count-in; 0 = start after the neutral release gate
        int missionRecorderCountInMs;
    };

    bool Initialize();

    bool LoadSettings();

    bool SaveSettings();

    bool CreateDefaultConfig();

    const Settings& GetSettings();

    void SetSetting(const std::string& section, const std::string& key, const std::string& value);

    std::string GetConfigFilePath();

    int ParseKeyValue(const std::string& value);
    std::string GetKeyName(int keyCode);
    bool IsMenuKeyAllowed(int vk);
    // VKs bound in key.ini for either player, as last read by ReadKeyMappingsFromIni.
    void SetEfzBoundVks(const std::array<bool, 256>& vks);
    bool IsVkBoundByEfz(int vk);
    int ParseGamepadButton(const std::string& value);
    std::string GetGamepadButtonName(int mask);

    std::string GetCurrentWorkingDirectory();
}
