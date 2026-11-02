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

// True where a mod-owned modal surface may open. Reads the raw screen byte: GetCurrentGamePhase()
// lags up to three calls and polling it from the hotkey thread perturbs the frame monitor's hysteresis.
bool IsTrainingMenuContext();

bool IsPracticeContext();
// IsPracticeContext() plus a live P1 fighter; world-scoped restores may write only under this.
// Once the fighters are gone, the next owner of those objects re-initialises them itself.
bool IsPracticeWorldLive();
void ClearBattleFrontendRouting();

enum class FrontendExitTarget : uint8_t {
    CharacterSelect = 1,
    Loading = 2,
    Title = 0,
};

bool EnsureFrontendControlHooksInstalled();
bool AreFrontendControlHooksInstalled();

// Closed means forward to the original and do nothing else. Atomics only: safe for every call.
bool PracticeBattleHooksOpen();

using BattleUpdateCallback = void (*)(void* battleContext);
void SetBattleUpdateCallbacks(BattleUpdateCallback beforeUpdate, BattleUpdateCallback afterUpdate);

// Battle update batch IDs. Read through the completed ID so a monitor tick never sees half an update.
uint32_t GetCurrentBattleUpdateBatch();
uint32_t GetCompletedBattleUpdateBatch();

bool CanRequestFrontendExit(FrontendExitTarget target);
bool RequestFrontendExit(FrontendExitTarget target);
bool RequestBattleFrontendExit(FrontendExitTarget target,uintptr_t battleContext,uintptr_t gameSystem);

enum class GamePhase : uint8_t {
    Unknown = 0,
    Menu,          // Title / settings / replay select / win screen
    CharacterSelect,
    Loading,
    Match,
};

GamePhase GetCurrentGamePhase();

inline bool IsMatchPhase() { return GetCurrentGamePhase() == GamePhase::Match; }
void LogPhaseIfChanged();

uint8_t ConsumeBattleFrontendResult(uint8_t nativeResult,bool cleanupHeld,uintptr_t heldGameSystem);
