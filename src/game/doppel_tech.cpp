#include "../include/game/doppel_tech.h"

#include "../include/core/constants.h"
#include "../include/core/memory.h"
#include "../include/core/logger.h"
#include "../include/game/game_state.h"
#include "../include/utils/utilities.h"
#include "../include/utils/network.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

namespace DoppelTech {

namespace {

// File-scope atomics: the v141_xp build uses /Zc:threadSafeInit-, so function-local statics
// are not thread-safe on the monitor thread. Index 0 unused; callers pass playerNum 1/2.
std::atomic<int> g_mode[3];
std::atomic<int> g_stage[3];

// g_stageDone: 0 = still armed (zero-init default). g_ourValue distinguishes
// this module's own latch write from an opponent press.
std::atomic<int> g_stageDone[3];
std::atomic<int> g_prevTechMove[3];
std::atomic<int> g_rolledValue[3];
std::atomic<int> g_ourValue[3];

inline int ClampPlayer(int playerNum) {
    return (playerNum == 2) ? 2 : 1;
}

inline bool IsTechState(int moveID) {
    return moveID == DOPPEL_MOVE_CAPTURE_HOLD ||
           moveID == DOPPEL_MOVE_MAIDEN_CRASH ||
           moveID == DOPPEL_MOVE_BARRAGE ||
           moveID == DOPPEL_MOVE_INNER_SOUL;
}

inline bool StageSelected(int stage, int moveID) {
    switch (stage) {
        case STAGE_1: return moveID == DOPPEL_MOVE_CAPTURE_HOLD;
        case STAGE_2: return moveID == DOPPEL_MOVE_MAIDEN_CRASH;
        case STAGE_3: return moveID == DOPPEL_MOVE_BARRAGE ||
                             moveID == DOPPEL_MOVE_INNER_SOUL;
        case STAGE_ALL:
        default:      return true;
    }
}

// The engine re-arms the latch early in each state (255 at frame index 1 subframe 0, 300/306
// at index 0 subframe 0) and samples the opponent only inside these windows; earlier writes are wiped.
bool InWriteWindow(int moveID, int frameIndex, int subFrame) {
    switch (moveID) {
        case DOPPEL_MOVE_CAPTURE_HOLD:
            // Opponent sampled on this single animation index only.
            return frameIndex == 2;
        case DOPPEL_MOVE_MAIDEN_CRASH:
            // Escape window index 1..9; engine clears at index 1 subframe 0.
            if (frameIndex < 1 || frameIndex > 9) return false;
            return frameIndex > 1 || subFrame > 0;
        case DOPPEL_MOVE_BARRAGE:
        case DOPPEL_MOVE_INNER_SOUL:
            // Escape window index 0..9; cleared at index 0 subframe 0.
            if (frameIndex > 9) return false;
            return frameIndex > 0 || subFrame > 0;
        default:
            return false;
    }
}

// Prefix compare: no other character resource name starts with "exnanase",
// and Rumi's "nanase" does not.
bool SideIsDoppel(uintptr_t base, uintptr_t baseOffset) {
    uintptr_t nameAddr = ResolvePointer(base, baseOffset, CHARACTER_NAME_OFFSET);
    if (!nameAddr) return false;
    char name[16] = {0};
    if (!SafeReadMemory(nameAddr, name, sizeof(name) - 1)) return false;
    name[sizeof(name) - 1] = '\0';
    return _strnicmp(name, "exnanase", 8) == 0;
}

int ResolveAlwaysValue(uintptr_t base, uintptr_t baseOffset) {
    uintptr_t tokenAddr = ResolvePointer(base, baseOffset, MOTION_TOKEN_OFFSET);
    if (!tokenAddr) return 0;
    uint16_t token = 0;
    if (!SafeReadMemory(tokenAddr, &token, sizeof(token))) return 0;

    switch (token) {
        // A-line follow-ups are refused by a C latch.
        case DOPPEL_TOKEN_A_QCF:
        case DOPPEL_TOKEN_A_QCB:
        case DOPPEL_TOKEN_A_SUPER_QCF:
        case DOPPEL_TOKEN_A_SUPER_QCB:
            return DOPPEL_TECH_C;
        // B-line follow-ups are refused by a B latch.
        case DOPPEL_TOKEN_B_QCF:
        case DOPPEL_TOKEN_B_QCB:
        case DOPPEL_TOKEN_B_SUPER_QCF:
        case DOPPEL_TOKEN_B_SUPER_QCB:
            return DOPPEL_TECH_B;
        default:
            return 0;
    }
}

int ResolveValue(int playerNum, int mode, uintptr_t base, uintptr_t baseOffset) {
    switch (mode) {
        case MODE_NEVER:  return DOPPEL_TECH_A;
        case MODE_TECH_B: return DOPPEL_TECH_B;
        case MODE_TECH_C: return DOPPEL_TECH_C;
        case MODE_ALWAYS: return ResolveAlwaysValue(base, baseOffset);
        case MODE_RANDOM: {
            int rolled = g_rolledValue[playerNum].load(std::memory_order_acquire);
            if (rolled != DOPPEL_TECH_B && rolled != DOPPEL_TECH_C) {
                rolled = ((rand() & 1) != 0) ? DOPPEL_TECH_C : DOPPEL_TECH_B;
                g_rolledValue[playerNum].store(rolled, std::memory_order_release);
            }
            return rolled;
        }
        default: return 0;
    }
}

void ClearSide(int playerNum) {
    g_stageDone[playerNum].store(0, std::memory_order_release);
    g_prevTechMove[playerNum].store(0, std::memory_order_release);
    g_rolledValue[playerNum].store(0, std::memory_order_release);
    g_ourValue[playerNum].store(0, std::memory_order_release);
}

const char* ModeName(int mode) {
    switch (mode) {
        case MODE_NEVER:  return "NEVER";
        case MODE_TECH_B: return "TECH B";
        case MODE_TECH_C: return "TECH C";
        case MODE_ALWAYS: return "ALWAYS";
        case MODE_RANDOM: return "RANDOM";
        default:          return "OFF";
    }
}

bool ReadWord(uintptr_t base, uintptr_t baseOffset, uintptr_t offset, int& out) {
    uintptr_t addr = ResolvePointer(base, baseOffset, offset);
    if (!addr) return false;
    uint16_t v = 0;
    if (!SafeReadMemory(addr, &v, sizeof(v))) return false;
    out = (int)v;
    return true;
}

void TickSide(int playerNum, uintptr_t base) {
    const int mode = g_mode[playerNum].load(std::memory_order_acquire);
    if (mode <= MODE_OFF || mode >= MODE_COUNT) {
        ClearSide(playerNum);
        return;
    }

    const uintptr_t baseOffset = (playerNum == 1) ? EFZ_BASE_OFFSET_P1
                                                  : EFZ_BASE_OFFSET_P2;

    if (!SideIsDoppel(base, baseOffset)) {
        ClearSide(playerNum);
        return;
    }

    // Re-read moveID here: the latch offset is shared per-move scratch in
    // other states, so the gate needs a fresh read.
    int moveID = 0;
    if (!ReadWord(base, baseOffset, MOVE_ID_OFFSET, moveID)) return;

    if (!IsTechState(moveID)) {
        ClearSide(playerNum);
        return;
    }

    const int prev = g_prevTechMove[playerNum].load(std::memory_order_acquire);
    if (prev != moveID) {
        g_prevTechMove[playerNum].store(moveID, std::memory_order_release);
        g_stageDone[playerNum].store(0, std::memory_order_release);
        g_rolledValue[playerNum].store(0, std::memory_order_release);
        g_ourValue[playerNum].store(0, std::memory_order_release);
    }

    if (g_stageDone[playerNum].load(std::memory_order_acquire) != 0) return;

    const int stage = g_stage[playerNum].load(std::memory_order_acquire);
    if (!StageSelected(stage, moveID)) return;

    int frameIndex = 0, subFrame = 0;
    if (!ReadWord(base, baseOffset, CURRENT_FRAME_INDEX_OFFSET, frameIndex)) return;
    if (!ReadWord(base, baseOffset, STATE_SUBFRAME_COUNTER_OFFSET, subFrame)) return;
    if (!InWriteWindow(moveID, frameIndex, subFrame)) return;

    uintptr_t latchAddr = ResolvePointer(base, baseOffset, DOPPEL_TECH_LATCH_OFFSET);
    if (!latchAddr) return;

    uint32_t latch = 0;
    if (!SafeReadMemory(latchAddr, &latch, sizeof(latch))) return;

    const int ours = g_ourValue[playerNum].load(std::memory_order_acquire);
    if (latch != (uint32_t)DOPPEL_TECH_NONE && (int)latch != ours) {
        // First press wins: never overwrite an opponent escape.
        g_stageDone[playerNum].store(1, std::memory_order_release);
        return;
    }

    const int value = ResolveValue(playerNum, mode, base, baseOffset);
    if (value == 0) {
        return;
    }

    if ((int)latch == value) {
        if (mode != MODE_ALWAYS) g_stageDone[playerNum].store(1, std::memory_order_release);
        return;
    }

    uint32_t out = (uint32_t)value;
    if (!SafeWriteMemory(latchAddr, &out, sizeof(out))) return;
    g_ourValue[playerNum].store(value, std::memory_order_release);

    // ALWAYS stays armed: her motion recogniser rewrites the committed
    // follow-up every frame, so the escape must track the one she performs.
    if (mode != MODE_ALWAYS) g_stageDone[playerNum].store(1, std::memory_order_release);

    LogOut(std::string("[DOPPEL-TECH] P") + std::to_string(playerNum) +
           " mode=" + ModeName(mode) +
           " moveID=" + std::to_string(moveID) +
           " idx=" + std::to_string(frameIndex) +
           " wrote=" + std::to_string(value),
           detailedLogging.load());
}

}

void SetMode(int playerNum, int mode) {
    const int p = ClampPlayer(playerNum);
    if (mode < MODE_OFF || mode >= MODE_COUNT) mode = MODE_OFF;
    const int prev = g_mode[p].exchange(mode, std::memory_order_acq_rel);
    if (prev != mode) {
        ClearSide(p);
        LogOut(std::string("[DOPPEL-TECH] P") + std::to_string(p) +
               " follow-up tech mode -> " + ModeName(mode), true);
    }
}

void SetStage(int playerNum, int stage) {
    const int p = ClampPlayer(playerNum);
    if (stage < STAGE_ALL || stage >= STAGE_COUNT) stage = STAGE_ALL;
    const int prev = g_stage[p].exchange(stage, std::memory_order_acq_rel);
    if (prev != stage) ClearSide(p);
}

int GetMode(int playerNum)  { return g_mode[ClampPlayer(playerNum)].load(std::memory_order_acquire); }
int GetStage(int playerNum) { return g_stage[ClampPlayer(playerNum)].load(std::memory_order_acquire); }

void ResetState() {
    ClearSide(1);
    ClearSide(2);
}

void Tick() {
    if (!g_featuresEnabled.load()) return;
    if (GetCurrentGameMode() != GameMode::Practice) return;
    if (IsNetplaySuspendActive()) return;

    if (g_mode[1].load(std::memory_order_acquire) == MODE_OFF &&
        g_mode[2].load(std::memory_order_acquire) == MODE_OFF) {
        return;
    }

    uintptr_t base = GetEFZBase();
    if (!base) return;

    TickSide(1, base);
    TickSide(2, base);
}

}
