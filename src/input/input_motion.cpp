#include "../include/input/input_motion.h"
#include "../include/core/memory.h"
#include "../include/core/logger.h"
#include "../include/core/constants.h"
#include "../include/utils/utilities.h"

#include "../include/game/auto_action_helpers.h"
#include "../include/input/input_buffer.h"  
#include "../include/input/input_debug.h"  
#include "../include/input/input_core.h"    
#include "../include/input/auto_action_motion_transaction.h"
#include "../include/input/shared_constants.h" 
#include "../include/game/auto_action.h"
#include "../include/game/auto_action_charge.h"
#include "../include/game/kaori_recoil_duck.h"
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <atomic>
#include <mutex>
#include "../include/game/practice_patch.h"
// For global shutdown flag
#include "../include/core/globals.h"

#ifdef max
#undef max
#endif

#define MOTION_INPUT_BUTTON (MOTION_BUTTON_A | MOTION_BUTTON_B | MOTION_BUTTON_C | MOTION_BUTTON_D)

std::atomic<bool> g_forceHumanControlActive(false);
extern std::atomic<bool> g_manualInputOverride[3];
extern std::atomic<uint8_t> g_manualInputMask[3];

void SetAIControlFlag(int playerNum, bool human) {
    // Motion producers humanize and restore either side under this mutex; a role write must not land in between.
    std::unique_lock<std::recursive_mutex> p2ControlLock(g_p2ControlMutex);
    if (g_onlineModeActive.load(std::memory_order_acquire)) return;
    if (playerNum == 1 || playerNum == 2) {
        // A control-role change supersedes authored commands on either side (P1 is the dummy after Switch Players); retire the generation before the flag becomes visible.
        KaoriRecoilDuck::Cancel(playerNum, "control role changed");
        CancelAutoActionChargeFollowup(playerNum, "control role changed");
        CancelAutoActionMotionTransaction(playerNum, "control role changed");
        if (g_onlineModeActive.load(std::memory_order_acquire)) return;
        if (IsAutoActionMotionTransactionActive(playerNum)) return;
    }

    uintptr_t base = GetEFZBase();
    if (!base) return;
    
    uintptr_t playerOffset = (playerNum == 1) ? EFZ_BASE_OFFSET_P1 : EFZ_BASE_OFFSET_P2;
    uintptr_t playerPtr = 0;
    if (!SafeReadMemory(base + playerOffset, &playerPtr, sizeof(uintptr_t)) || !playerPtr) {
        return;
    }
    
    uint32_t desired = human ? 0u : 1u;
    uint32_t before = 0xFFFFFFFFu, after = 0xFFFFFFFFu;
    SafeReadMemory(playerPtr + AI_CONTROL_FLAG_OFFSET, &before, sizeof(uint32_t));
    bool okWrite = SafeWriteMemory(playerPtr + AI_CONTROL_FLAG_OFFSET, &desired, sizeof(uint32_t));
    SafeReadMemory(playerPtr + AI_CONTROL_FLAG_OFFSET, &after, sizeof(uint32_t));
    if (before != after || !okWrite) {
        std::ostringstream oss;
        oss << "[AUDIT][AI] SetAIControlFlag P" << playerNum
            << " @0x" << std::hex << (playerPtr + AI_CONTROL_FLAG_OFFSET)
            << std::dec
            << " before=" << before
            << " write=" << desired
            << " after=" << after
            << " okWrite=" << (okWrite?"1":"0");
        LogOut(oss.str(), true);
    }
}

bool IsAIControlFlagHuman(int playerNum) {
    uintptr_t base = GetEFZBase();
    if (!base) return false;
    
    uintptr_t playerOffset = (playerNum == 1) ? EFZ_BASE_OFFSET_P1 : EFZ_BASE_OFFSET_P2;
    uintptr_t playerPtr = 0;
    if (!SafeReadMemory(base + playerOffset, &playerPtr, sizeof(uintptr_t)) || !playerPtr) {
        return false;
    }
    
    uint32_t controlFlag = 1;
    if (!SafeReadMemory(playerPtr + AI_CONTROL_FLAG_OFFSET, &controlFlag, sizeof(uint32_t))) {
        return false;
    }
    
    // 0 = human control, 1 = AI control
    return controlFlag == 0;
}

void RestoreAIControlIfNeeded(int playerNum) {
    if (!g_forceHumanControlActive.load()) {
        SetAIControlFlag(playerNum, false);
    }
}

bool HoldUp(int playerNum) {
    // Up = 0x08
    return WritePlayerInput(playerNum, MOTION_INPUT_UP);
}

bool HoldBackCrouch(int playerNum) {
    bool facingRight = GetPlayerFacingDirection(playerNum);
    
    // If facing right, back is left (0x02). If facing left, back is right (0x01)
    uint8_t backInput = facingRight ? MOTION_INPUT_LEFT : MOTION_INPUT_RIGHT;
    
    // Combine with down input (0x04)
    return WritePlayerInput(playerNum, backInput | MOTION_INPUT_DOWN);
}

bool ReleaseInputs(int playerNum) {
    // Write neutral (0) to release all inputs
    return WritePlayerInput(playerNum, MOTION_INPUT_NEUTRAL);
}
