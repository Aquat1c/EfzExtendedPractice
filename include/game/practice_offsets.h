#pragma once

#include <stdint.h>
#include "efzrevival_addrs.h"

#ifndef EFZ_RVA_TO_VA
#define EFZ_RVA_TO_VA(hmod, rva) (reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(hmod) + static_cast<uintptr_t>(rva)))
#endif

#ifndef EFZREV_RVA_PRACTICE_TICK
#define EFZREV_RVA_PRACTICE_TICK 0x0074F70
#endif

#ifndef EFZREV_RVA_TOGGLE_PAUSE
#define EFZREV_RVA_TOGGLE_PAUSE 0x0075720
#endif

#ifndef EFZREV_RVA_PRACTICE_HOTKEY_EVAL
#define EFZREV_RVA_PRACTICE_HOTKEY_EVAL 0x00773A0
#endif

#ifndef EFZREV_RVA_TOGGLE_HURTBOXES
#define EFZREV_RVA_TOGGLE_HURTBOXES 0x0075140
#endif
#ifndef EFZREV_RVA_TOGGLE_HITBOXES
#define EFZREV_RVA_TOGGLE_HITBOXES 0x0075160
#endif
#ifndef EFZREV_RVA_TOGGLE_DISPLAY
#define EFZREV_RVA_TOGGLE_DISPLAY 0x00756E0
#endif

#ifndef EFZ_RVA_RENDER_BATTLE_SCREEN
#define EFZ_RVA_RENDER_BATTLE_SCREEN 0x003642A0
#endif

#ifndef EFZREV_RVA_PATCH_TOGGLER
#define EFZREV_RVA_PATCH_TOGGLER 0x006B2A0
#endif

#ifndef EFZREV_RVA_PATCH_CTX
#define EFZREV_RVA_PATCH_CTX 0x00A0760
#endif

// WARNING: These offsets are VERSION-SPECIFIC.

#ifndef PRACTICE_OFF_STEP_FLAG
#define PRACTICE_OFF_STEP_FLAG        (EFZ_Practice_StepFlagOffset())
#endif

#ifndef PRACTICE_OFF_STEP_COUNTER
#define PRACTICE_OFF_STEP_COUNTER     (EFZ_Practice_StepCounterOffset())
#endif

#ifndef PRACTICE_OFF_PAUSE_FLAG
#define PRACTICE_OFF_PAUSE_FLAG       (EFZ_Practice_PauseFlagOffset())
#endif

#ifndef PRACTICE_OFF_SPEED_DBL_HI
#define PRACTICE_OFF_SPEED_DBL_HI     0xC0
#endif
#ifndef PRACTICE_OFF_SPEED_DBL_LO
#define PRACTICE_OFF_SPEED_DBL_LO     0xC4
#endif

#ifndef PRACTICE_OFF_PAUSE_KEY
#define PRACTICE_OFF_PAUSE_KEY        0x1D4
#endif
#ifndef PRACTICE_OFF_STEP_KEY
#define PRACTICE_OFF_STEP_KEY         0x1D8
#endif

#ifndef PRACTICE_OFF_GUI_POS
// Dword, 1 = P1, 0 = P2. Practice is 0x2B0 bytes (i: 0x2B8, j: 0x2F0).
#define PRACTICE_OFF_GUI_POS            0x24
#endif

// Misnomer: this is the GUI position field, not a human gate.
#ifndef PRACTICE_OFF_P2_HUMAN_GATE
#define PRACTICE_OFF_P2_HUMAN_GATE      PRACTICE_OFF_GUI_POS
#endif

// Engine game state (efz.exe) offsets
// Relative to the game state pointer stored at [efz.exe + EFZ_BASE_OFFSET_GAME_STATE].

#ifndef GAMESTATE_OFF_ACTIVE_PLAYER
#define GAMESTATE_OFF_ACTIVE_PLAYER     4930 // byte: 0=P1, 1=P2
#endif

// Per-side CPU flags: 1 = CPU, 0 = human. Left is P1's slot, right is P2's.
#ifndef GAMESTATE_OFF_LEFT_SIDE_CPU_FLAG
#define GAMESTATE_OFF_LEFT_SIDE_CPU_FLAG  4931
#endif
#ifndef GAMESTATE_OFF_RIGHT_SIDE_CPU_FLAG  
#define GAMESTATE_OFF_RIGHT_SIDE_CPU_FLAG 4932
#endif

#define GAMESTATE_OFF_P1_CPU_FLAG GAMESTATE_OFF_LEFT_SIDE_CPU_FLAG
#define GAMESTATE_OFF_P2_CPU_FLAG GAMESTATE_OFF_RIGHT_SIDE_CPU_FLAG
