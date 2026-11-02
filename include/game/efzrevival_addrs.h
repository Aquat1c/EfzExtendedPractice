#pragma once
#include <stdint.h>

// Per-version EfzRevival RVAs and offsets. Unsupported versions return 0 so callers can skip.

// False on 1.02j. Only gates the direct +0x24 access at match start and on character select.
bool EFZ_SupportsNativePracticeSideSwitch();

// Patch toggler normal-speed parameter: 3 on 1.02f subframe, g, h, i, j; 1 on 1.02e, classic f.
int EFZ_PatchToggleUnfreezeParam();

uintptr_t EFZ_RVA_PatchToggler();
uintptr_t EFZ_RVA_PatchCtx();
uintptr_t EFZ_RVA_TogglePause();
uintptr_t EFZ_RVA_PracticeTick();
uintptr_t EFZ_RVA_RenderBattleScreen();
uintptr_t EFZ_RVA_RenderContextGlobal();
// Always returns 0: the old RVAs are Revival session-pointer globals, not Practice controllers.
uintptr_t EFZ_RVA_PracticeControllerPtr();
uintptr_t EFZ_RVA_PracticeDispatcher();

uintptr_t EFZ_Practice_PauseFlagOffset();    // e-i: 0xB4, j: 0xDC
uintptr_t EFZ_Practice_StepFlagOffset();     // e-i: 0xAC, j: 0xD4
uintptr_t EFZ_Practice_StepCounterOffset();  // e-i: 0xB0, j: 0xD8
uintptr_t EFZ_Practice_PauseHotkeyOffset();  // e/f/g/h: 0x1D4, i: 0x1D8, j: 0x200
uintptr_t EFZ_Practice_StepHotkeyOffset();   // e/f/g/h: 0x1D8, i: 0x1DC, j: 0x204
uintptr_t EFZ_Practice_SaveHotkeyOffset();   // e/f/g/h: 0x1DC, i: 0x1E0, j: 0x208
uintptr_t EFZ_Practice_LoadHotkeyOffset();   // e/f/g/h: 0x1E0, i: 0x1E4, j: 0x20C


uintptr_t EFZ_RVA_ToggleHurtboxDisplay();
uintptr_t EFZ_RVA_ToggleHitboxDisplay();
uintptr_t EFZ_RVA_ToggleFrameDisplay();

void EFZ_Debug_LogScannerComparison();

uintptr_t EFZ_RVA_LoadState();
uintptr_t EFZ_RVA_SaveState();
uintptr_t EFZ_RVA_PracticeHotkeyHandler();

// Replay/Recording Mode savestates (only while recording is active, NOT normal
// Practice mode).
uintptr_t EFZ_RVA_ReplayLoadState();
uintptr_t EFZ_RVA_ReplaySaveState();
