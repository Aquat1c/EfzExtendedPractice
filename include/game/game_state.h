#pragma once
#include <cstdint>
#include <string>

enum class GameMode : uint8_t {
    Arcade = 0,
    Practice = 1,
    VsCpu = 3,
    VsHuman = 4,
    Replay = 5,
    AutoReplay = 6,
    Unknown = 255
};

GameMode GetCurrentGameMode(uint8_t* rawValueOut = nullptr);

std::string GetGameModeName(GameMode mode);
void DebugDumpScreenState();
bool IsInGameplayState();
bool IsInCharacterSelectScreen();

// True only where it is safe to open a mod-owned modal surface: the raw screen
// byte is the battle screen, the mode is Practice, both fighters exist, and no
// netplay session is live. Built on the raw screen byte rather than
// GetCurrentGamePhase(): the phase query lags by up to three calls and is not
// side-effect free, so polling it from the hotkey thread would perturb the
// frame monitor's own hysteresis.
bool IsTrainingMenuContext();

// Online mode is not latched, the netplay export publishes neither a session
// nor a suspend, and the mode byte says Practice.
bool IsPracticeContext();
// IsPracticeContext() plus a live P1 fighter object; world-scoped restores
// (CPU/AI flags, fighter fields, the Practice controller) may write only under
// this. Once the fighters are gone, the next owner of those objects
// re-initialises them itself.
bool IsPracticeWorldLive();
// Drops any armed one-shot battle exit route; netplay owns the battle now.
void ClearBattleFrontendRouting();

enum class FrontendExitTarget : uint8_t {
    CharacterSelect = 1,
    Loading = 2,
    Title = 0,
};

// Front-end safety hooks: suppresses EFZ's DirectInput ESC/F-key battle
// hotkeys while our menu is open, redirects Practice ESC to the training menu
// instead of EFZ's character-select exit, and lets requested battle cleanup
// return to Title or Loading instead of Character Select.
bool EnsureFrontendControlHooksInstalled();
bool AreFrontendControlHooksInstalled();

using BattleUpdateCallback = void (*)(void* battleContext);
void SetBattleUpdateCallbacks(BattleUpdateCallback beforeUpdate, BattleUpdateCallback afterUpdate);

// Monotonic IDs around the exact game-thread Battle update; consumers only
// read through the completed ID so a monitor tick cannot observe half of one
// game update.
uint32_t GetCurrentBattleUpdateBatch();
uint32_t GetCompletedBattleUpdateBatch();

bool CanRequestFrontendExit(FrontendExitTarget target);
bool RequestFrontendExit(FrontendExitTarget target);
// Native battle-entry consumer: validates and writes only the queued allocation.
bool RequestBattleFrontendExit(FrontendExitTarget target,uintptr_t battleContext,uintptr_t gameSystem);

enum class GamePhase : uint8_t {
    Unknown = 0,
    Menu,          // Title / config / replay select
    CharacterSelect,
    Loading,
    Match,         // Active fight (intros / round / result still with spawned chars)
};

GamePhase GetCurrentGamePhase();

inline bool IsMatchPhase() { return GetCurrentGamePhase() == GamePhase::Match; }
void LogPhaseIfChanged();

// Consumes the existing one-shot Title/Loading route after normal or retained cleanup.
uint8_t ConsumeBattleFrontendResult(uint8_t nativeResult,bool cleanupHeld,uintptr_t heldGameSystem);
