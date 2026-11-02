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
    bool suppressRevivalFramestep;
    bool enableConsole;
    bool enableFpsDiagnostics;
    bool checkForUpdates;
    bool showFrameBar;
    bool frameBarMoveData;
    bool menuAnimations;
    int  bgmVolumePercent;       // 0..100, 100 = current/default BGM level
    int  seVolumePercent;        // 0..100, 100 = current/default SE level
    int  frameBarTimingMode;     // 0 = subframes, 1 = visual frames
    int  frameBarDetailMode;     // 0 = full, 1 = compact, 2 = bars only
    bool collisionDisplayHitboxes;
    bool collisionDisplayHurtboxes;
    bool collisionDisplayCollisionBoxes;
    bool collisionDisplayProjectileInteractions;
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
        int helpKey;                // Unused; always -1
        int toggleImGuiKey;         // Unused; always -1
        int savestateSaveKey;
        int savestateLoadKey;

        int switchPlayersKey;
        int macroRecordKey;
        int macroPlayKey;
        int macroSlotKey;
        int macroRandomKey;

        int framestepPauseKey;
        int framestepStepKey;

        int uiAcceptKey;
        int uiRefreshKey;
        int uiExitKey;
    bool swapCustomEnabled;
    int  swapCustomKey;

        // XInput button bitmasks; -1 = disabled
        int gpTeleportButton;
        int gpSavePositionButton;
    int gpSwitchPlayersButton;
    int gpSwapPositionsButton;
    int gpMacroRecordButton;
    int gpMacroPlayButton;      // Default: RT (virtual trigger bit 0x20000)
    int gpMacroSlotButton;      // Default: LT (virtual trigger bit 0x10000)
    int gpMacroRandomButton;
    int gpSavestateSaveButton;
    int gpSavestateLoadButton;
    int gpFramestepPauseButton;
    int gpFramestepStepButton;
    int gpToggleStatsButton;
    int gpResetCounterButton;
    int gpStatsPagePrevButton;
    int gpStatsPageNextButton;
    int gpAutoJumpButton;
    int gpUiUp;
    int gpUiDown;
    int gpUiLeft;
    int gpUiRight;
    int gpUiConfirm;
    int gpUiBack;
    int gpUiSecondary;
        int gpToggleMenuButton;
        int gpToggleImGuiButton;    // Unused; always -1

    int gpUiTopTabPrev;
    int gpUiTopTabNext;
    int gpUiSubTabPrev;
    int gpUiSubTabNext;

        bool freezeRFAfterContRec;
        bool freezeRFOnlyWhenNeutral;

        bool crRequireBothNeutral;
        int  crBothNeutralDelayMs;       // ms delay after both-neutral before applying CR

        bool autoFixHPOnNeutral;

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
    void SetEfzBoundVks(const std::array<bool, 256>& vks);
    bool IsVkBoundByEfz(int vk);
    int ParseGamepadButton(const std::string& value);
    std::string GetGamepadButtonName(int mask);
    void SwapPadNavigationConflict(const int* field, int previousMask, int mask);
    void ResetPadNavigation();

    std::string GetCurrentWorkingDirectory();
}
