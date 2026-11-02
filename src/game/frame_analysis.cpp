#include "../include/game/frame_analysis.h"
#include "../include/core/constants.h"
#include "../include/utils/utilities.h"

#include "../include/core/memory.h"
#include "../include/core/logger.h"

short initialBlockstunMoveID = -1;

bool IsHitstun(short moveID) {
    return (moveID >= STAND_HITSTUN_START && moveID <= STAND_HITSTUN_END) || 
           (moveID >= CROUCH_HITSTUN_START && moveID <= CROUCH_HITSTUN_END) ||
           moveID == SWEEP_HITSTUN ||
           moveID == 69; // Ikumi 623 hit state
}

bool IsLaunched(short moveID) {
    return moveID >= LAUNCHED_HITSTUN_START && moveID <= LAUNCHED_HITSTUN_END;
}

bool IsAirtech(short moveID) {
    return moveID == FORWARD_AIRTECH || moveID == BACKWARD_AIRTECH;
}

bool IsGroundtech(short moveID) {
    return moveID == GROUNDTECH_PRE ||
           moveID == GROUNDTECH_START ||
           moveID == GROUNDTECH_END || 
           moveID == GROUNDTECH_RECOVERY;
}

bool IsFrozen(short moveID) {
    return moveID >= FROZEN_STATE_START && moveID <= FROZEN_STATE_END;
}

bool IsSpecialStun(short moveID) {
    return moveID == FIRE_STATE || moveID == ELECTRIC_STATE || 
           (moveID >= FROZEN_STATE_START && moveID <= FROZEN_STATE_END);
}

bool IsThrown(short moveID) {
    if (moveID >= THROWN_STATE_START && moveID <= THROWN_STATE_END) return true;
    if (moveID >= THROWN2_STATE_START && moveID <= THROWN2_STATE_END) return true;
    if (moveID >= THROWN3_STATE_START && moveID <= THROWN3_STATE_END) return true;
    if (moveID >= THROWN4_STATE_START && moveID <= THROWN4_STATE_END) return true;
    if (moveID >= THROWN5_STATE_START && moveID <= THROWN5_STATE_END) return true;
    return false;
}

bool IsBlockstunState(short moveID) {
    return moveID == STANDING_BLOCK_LVL1 ||
           moveID == STANDING_BLOCK_LVL2 ||
           moveID == STANDING_BLOCK_LVL3 ||
           moveID == CROUCHING_BLOCK_LVL1 ||
           moveID == CROUCHING_BLOCK_LVL2_A ||
           moveID == CROUCHING_BLOCK_LVL2_B ||
           moveID == AIR_GUARD_ID;
}

int GetAttackLevel(short blockstunMoveID) {
    switch (blockstunMoveID) {
        case STANDING_BLOCK_LVL1:
        case CROUCHING_BLOCK_LVL1:
            return 1;
            
        case STANDING_BLOCK_LVL2:
        case CROUCHING_BLOCK_LVL2_A:
        case CROUCHING_BLOCK_LVL2_B:
            return 2;
            
        case STANDING_BLOCK_LVL3:
            return 3;
            
        case AIR_GUARD_ID:
            return 0;
            
        default:
            return 1;
    }
}

std::string GetBlockStateType(short blockstunMoveID) {
    switch (blockstunMoveID) {
        case STANDING_BLOCK_LVL1:
            return "Standing Block (Lvl 1)";
        case STANDING_BLOCK_LVL2:
            return "Standing Block (Lvl 2)";
        case STANDING_BLOCK_LVL3:
            return "Standing Block (Lvl 3)";
        case CROUCHING_BLOCK_LVL1:
            return "Crouching Block (Lvl 1)";
        case CROUCHING_BLOCK_LVL2_A:
            return "Crouching Block (Lvl 2A)";
        case CROUCHING_BLOCK_LVL2_B:
            return "Crouching Block (Lvl 2B)";
        case AIR_GUARD_ID:
            return "Air Block";
        default:
            return "Unknown Block";
    }
}

int GetExpectedFrameAdvantage(int attackLevel, bool isAirBlock, bool isHit) {
    if (isAirBlock) {
        return isHit ? FRAME_ADV_AIR_BLOCK + 3 : FRAME_ADV_AIR_BLOCK; // Rough estimate
    }
    
    switch (attackLevel) {
        case 1:
            return isHit ? FRAME_ADV_LVL1_HIT : FRAME_ADV_LVL1_BLOCK;
        case 2:
            return isHit ? FRAME_ADV_LVL2_HIT : FRAME_ADV_LVL2_BLOCK;
        case 3:
            return isHit ? FRAME_ADV_LVL3_HIT : FRAME_ADV_LVL3_BLOCK;
        default:
            return isHit ? FRAME_ADV_LVL1_HIT : FRAME_ADV_LVL1_BLOCK;
    }
}

short GetUntechValue(uintptr_t base, int player) {
    short untechValue = 0;
    uintptr_t baseOffset = (player == 1) ? EFZ_BASE_OFFSET_P1 : EFZ_BASE_OFFSET_P2;
    uintptr_t untechAddr = ResolvePointer(base, baseOffset, UNTECH_OFFSET);
    
    if (untechAddr) {
        SafeReadMemory(untechAddr, &untechValue, sizeof(short));
    }
    
    return untechValue;
}

short GetBlockstunValue(uintptr_t base, int player) {
    short v = 0;
    uintptr_t baseOffset = (player == 1) ? EFZ_BASE_OFFSET_P1 : EFZ_BASE_OFFSET_P2;
    uintptr_t addr = ResolvePointer(base, baseOffset, BLOCKSTUN_OFFSET);
    if (addr) {
        SafeReadMemory(addr, &v, sizeof(v));
    }
    return v;
}