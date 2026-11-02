#pragma once

#include <cstdint>
#include "runtime/practice_contract.h"

enum class GameMode : uint8_t;
enum class GamePhase : uint8_t;

namespace CharacterHotswap {

inline constexpr int kPaletteSlotCount = 6;
inline constexpr int kCharacterSelectCount = 24;

struct PaletteSelection {
	int p1Color = 0;
	int p2Color = 0;
	bool p1UseCustomPalette = false;
	bool p2UseCustomPalette = false;
};

// Character arguments are character-select IDs, not CharacterSettings/CHAR_ID values.
bool QueueReload(int p1SelectId, int p2SelectId, int stageId, int bgmTrack);
bool QueueReload(int p1SelectId, int p2SelectId, int stageId,
                 const PaletteSelection& paletteSelection,
                 int bgmTrack); // -1 keeps native stage music

bool InstallDirectPracticeBootstrap();
void UninstallDirectPracticeBootstrap();
bool QueueDirectPracticeLoad(int p1SelectId, int p2SelectId, int stageId,
                             const PaletteSelection& paletteSelection,
                             int bgmTrack); // -1 keeps native stage music
bool IsDirectPracticeLoadPending();
void CancelDirectPracticeLoad(const char* reason);

bool ConsumeCompletedPracticeLoad(int p1SelectId, int p2SelectId, int stageId,
                                  const PaletteSelection& paletteSelection,
                                  int bgmTrack);

// A receipt is valid only for the session that produced it; teardown must invalidate it
// immediately so an old tuple can never authorize setup in a later Match.
void InvalidateCompletedPracticeLoadReceipt();

uint32_t CaptureLoadingRequest(const EfzTmIdentityV1& identity);
void ConsumeLoadingRequest(const EfzTmIdentityV1& identity,uint32_t ticket,uint32_t nativeResult,uint32_t acceptedResult);

void OnBattleFrontendEntry(uintptr_t battleContext);
void OnSelectorReady(uintptr_t selectorContext);
uint32_t CaptureInitializedRequest(const EfzTmIdentityV1&);
void OnNativeFrontendInstalled();
void OnBattleInitialized(const EfzTmIdentityV1&,uint32_t ticket,uintptr_t battleContext,uintptr_t gameSystem);
void Tick(GamePhase currentPhase, GameMode currentMode);
bool IsBusy();
bool CanQueueReload();
const char* GetActionValueText();
const char* GetDisplayNameForSelectId(int selectId);
const char* GetResourceNameForSelectId(int selectId);
int GetSelectIdForResourceName(const char* resourceName);
bool ReadCurrentPaletteSelection(PaletteSelection& outSelection);
bool HasCustomPaletteFile(int selectId, int paletteIndex);
void SanitizePaletteSelection(int p1SelectId, int p2SelectId, PaletteSelection& selection);
void InvalidateCustomPaletteCache();

}
