#include "../include/game/attack_reader.h"
#include "../include/game/collision_hook.h"
#include "../include/core/memory.h"
#include "../include/core/logger.h"
#include "../include/core/constants.h"
#include "../include/utils/utilities.h"
#include "../include/input/input_core.h"
#include <sstream>  
#include <string>
#include <atomic>

#define ATTACK_FLAGS_OFFSET     0xAA
#define ATTACK_DAMAGE_OFFSET    0xA8
#define ATTACK_BLOCKSTUN_OFFSET 0xB2
#define ATTACK_HITSTOP_OFFSET   0xC2
#define ATTACK_ACTIVE_REMAINING 0xEA

#define ATTACK_FLAG_HIGH      0x01
#define ATTACK_FLAG_LOW       0x02
#define ATTACK_FLAG_THROW     0x04
#define ATTACK_FLAG_MID       0x08

static std::atomic<int> g_fdToAtkOffsetP1{-1};
static std::atomic<int> g_fdToAtkOffsetP2{-1};
auto ToHexString = [](int value) -> std::string {
            std::ostringstream oss;
            oss << "0x" << std::hex << value;
            return oss.str();
        };
static bool IsPlausibleAttackDataPtr(uintptr_t atkPtr) {
    if (!atkPtr) return false;
    if (atkPtr < 0x00400000 || atkPtr > 0x0FFFFFFF) return false;
    uint8_t flags = 0;
    uint16_t dmg = 0, bs = 0, hs = 0;
    if (!SafeReadMemory(atkPtr + ATTACK_FLAGS_OFFSET, &flags, sizeof(flags))) return false;
    if (!SafeReadMemory(atkPtr + ATTACK_DAMAGE_OFFSET, &dmg, sizeof(dmg))) return false;
    if (!SafeReadMemory(atkPtr + ATTACK_BLOCKSTUN_OFFSET, &bs, sizeof(bs))) return false;
    if (!SafeReadMemory(atkPtr + ATTACK_HITSTOP_OFFSET, &hs, sizeof(hs))) return false;
    if (dmg > 5000 || bs > 1000 || hs > 1000) return false;
    return true;
}

static bool TryFindNestedAttackPtr(uintptr_t frameData, int& outOffset, uintptr_t& outAtk) {
    if (!frameData) return false;
    for (int off = 0; off <= 0x200 - 4; off += 4) {
        uintptr_t candidate = 0;
        if (!SafeReadMemory(frameData + off, &candidate, sizeof(candidate))) continue;
        if (IsPlausibleAttackDataPtr(candidate)) {
            outOffset = off;
            outAtk = candidate;
            return true;
        }
    }
    return false;
}

void AttackReader::LogMoveData(int playerNum, short moveID) {
    if (moveID <= 0) {
        return;
    }
    
    uintptr_t base = GetEFZBase();
    if (!base) return;
    
    uintptr_t playerOffset = (playerNum == 1) ? EFZ_BASE_OFFSET_P1 : EFZ_BASE_OFFSET_P2;
    uintptr_t playerPtr = 0;
    
    if (!SafeReadMemory(base + playerOffset, &playerPtr, sizeof(uintptr_t)) || !playerPtr) {
        return;
    }
    
    uintptr_t attackDataPtr = GetAttackDataPtr(playerPtr, moveID);
    if (!attackDataPtr) {
        LogOut("[ATTACK_READER] P" + std::to_string(playerNum) + " move " + 
               std::to_string(moveID) + " - Failed to get attack data", true);
        return;
    }
    
    uint8_t attackTypeFlags = 0;
    uint16_t activeRemain = 0;
    uint16_t damage = 0;
    uint16_t blockstun = 0;
    uint16_t hitstop = 0;

    if (!SafeReadMemory(attackDataPtr + ATTACK_FLAGS_OFFSET, &attackTypeFlags, sizeof(uint8_t))) {
        return;
    }

    SafeReadMemory(attackDataPtr + ATTACK_ACTIVE_REMAINING, &activeRemain, sizeof(uint16_t));
    SafeReadMemory(attackDataPtr + ATTACK_DAMAGE_OFFSET, &damage, sizeof(uint16_t));
    SafeReadMemory(attackDataPtr + ATTACK_BLOCKSTUN_OFFSET, &blockstun, sizeof(uint16_t));
    SafeReadMemory(attackDataPtr + ATTACK_HITSTOP_OFFSET, &hitstop, sizeof(uint16_t));

    std::string heightStr = "Unknown";
    if (attackTypeFlags & 0x40 || attackTypeFlags & 0x80)
        heightStr = "THROW (Unblockable)";
    else if (attackTypeFlags & 0x01)
        heightStr = "LOW";
    else if (attackTypeFlags & 0x04)
        heightStr = "HIGH";
    else if (attackTypeFlags & 0x02)
        heightStr = "MID";

    std::stringstream ss;
    ss << "[ATTACK_DATA] P" << playerNum << " Move ID: " << moveID 
       << " | Type: 0x" << std::hex << (int)attackTypeFlags << std::dec
       << " (" << heightStr << ")"
    << " | ActiveRemain: " << activeRemain
       << " | Damage: " << damage
       << " | Blockstun: " << blockstun
       << " | Hitstop: " << hitstop;
    
    LogOut(ss.str(), true);
}

AttackHeight AttackReader::GetAttackHeight(int playerPtr, short moveID) {
    if (moveID <= 0) {
        return ATTACK_HEIGHT_UNKNOWN;
    }
    
    uintptr_t attackDataPtr = GetAttackDataPtr(playerPtr, moveID);
    if (!attackDataPtr) {
        return ATTACK_HEIGHT_UNKNOWN;
    }

    int attackType = 0;
    if (!SafeReadMemory(attackDataPtr + ATTACK_FLAGS_OFFSET, &attackType, sizeof(uint8_t))) {
        return ATTACK_HEIGHT_UNKNOWN;
    }

    LogOut(std::string("[ATTACK_READER] Attack height flags: 0x") + 
           std::to_string(attackType) + " for moveID " + std::to_string(moveID), true);

    if (attackType & 0x40 || attackType & 0x80)
        return ATTACK_HEIGHT_THROW;
    else if (attackType & 0x01)
        return ATTACK_HEIGHT_LOW;
    else if (attackType & 0x04)
        return ATTACK_HEIGHT_HIGH;
    else if (attackType & 0x02)
        return ATTACK_HEIGHT_MID;
    
    return ATTACK_HEIGHT_MID;
}

bool AttackReader::IsMoveActive(int playerPtr, short moveID) {
    if (moveID <= 0)
        return false;
        
    uintptr_t attackDataPtr = GetAttackDataPtr(playerPtr, moveID);
    if (!attackDataPtr)
        return false;
    
    int currentFrame = 0;
    if (!SafeReadMemory(playerPtr + 0x44, &currentFrame, sizeof(int))) {
        return false;
    }

    uint16_t activeRemain = 0;
    if (!SafeReadMemory(attackDataPtr + ATTACK_ACTIVE_REMAINING, &activeRemain, sizeof(uint16_t))) {
        return false;
    }
    
    return activeRemain > 0;
}

uintptr_t AttackReader::GetAttackDataPtr(int playerPtr, short) {
    uintptr_t p1 = GetPlayerPointer(1);
    uintptr_t p2 = GetPlayerPointer(2);
    int playerNum = (playerPtr == p1) ? 1 : ((playerPtr == p2) ? 2 : 0);

    if (playerNum != 0) {
        std::atomic<int>& offCache = (playerNum == 1) ? g_fdToAtkOffsetP1 : g_fdToAtkOffsetP2;

        uintptr_t frameData = GetCachedAttackDataForPlayer(playerNum);
    if (frameData) {
            int off = offCache.load();
            if (off >= 0) {
                uintptr_t atk = 0;
                if (SafeReadMemory(frameData + off, &atk, sizeof(atk)) && IsPlausibleAttackDataPtr(atk)) return atk;
            } else {
                uintptr_t atk = 0; int foundOff = -1;
                if (TryFindNestedAttackPtr(frameData, foundOff, atk)) {
                    offCache.store(foundOff);
                    LogOut("[ATTACK_READER] Discovered fd->atk offset P" + std::to_string(playerNum) + ": +0x" + ToHexString((uint32_t)foundOff), true);
                    return atk;
                }
        if (SafeReadMemory(frameData + 0x38, &atk, sizeof(atk)) && IsPlausibleAttackDataPtr(atk)) return atk;
            }
        }

        int playerFdOff = GetAttackDataOffsetForPlayer(playerNum);
        if (playerFdOff >= 0) {
            uintptr_t fd = 0;
        if (SafeReadMemory(playerPtr + playerFdOff, &fd, sizeof(fd)) && fd) {
                int off = offCache.load();
                if (off >= 0) {
                    uintptr_t atk = 0;
                    if (SafeReadMemory(fd + off, &atk, sizeof(atk)) && IsPlausibleAttackDataPtr(atk)) return atk;
                } else {
                    uintptr_t atk = 0; int foundOff = -1;
                    if (TryFindNestedAttackPtr(fd, foundOff, atk)) {
                        offCache.store(foundOff);
                        LogOut("[ATTACK_READER] Discovered fd->atk offset P" + std::to_string(playerNum) + ": +0x" + ToHexString((uint32_t)foundOff), true);
                        return atk;
                    }
            if (SafeReadMemory(fd + 0x38, &atk, sizeof(atk)) && IsPlausibleAttackDataPtr(atk)) return atk;
                }
            }
        }
    }

    // +0x168 is a raw result latch, not an attack-data pointer; the nested probe is legacy compatibility.
    uintptr_t currentAttackData = 0;
    if (SafeReadMemory(playerPtr + 0x168, &currentAttackData, sizeof(uintptr_t)) && currentAttackData) {
        uintptr_t atk = 0; int foundOff = -1;
        if (TryFindNestedAttackPtr(currentAttackData, foundOff, atk)) return atk;
    }
    if (SafeReadMemory(playerPtr + 0x234, &currentAttackData, sizeof(uintptr_t)) && currentAttackData) {
        uintptr_t atk = 0; int foundOff = -1;
        if (TryFindNestedAttackPtr(currentAttackData, foundOff, atk)) return atk;
    }
    if (SafeReadMemory(playerPtr + 0x364, &currentAttackData, sizeof(uintptr_t)) && currentAttackData) {
        uintptr_t atk = 0; int foundOff = -1;
        if (TryFindNestedAttackPtr(currentAttackData, foundOff, atk)) return atk;
    }
    LogOut("[ATTACK_READER] Could not find attack data pointer", true);
    return 0;
}

bool AttackReader::CanPlayerBlock(int playerPtr, int attackerPtr, uintptr_t attackDataPtr) {
    int guardState = 0;
    short guardGauge = 0;
    short specialState = 0;
    uint8_t attackFlags = 0;
    
    SafeReadMemory(playerPtr + 368, &guardState, sizeof(int));
    SafeReadMemory(playerPtr + 334, &guardGauge, sizeof(short));
    SafeReadMemory(playerPtr + 314, &specialState, sizeof(short));
    SafeReadMemory(attackDataPtr + ATTACK_FLAGS_OFFSET, &attackFlags, sizeof(uint8_t));
    
    if (!guardState ||
        guardGauge < 30 ||
        specialState ||
        (attackFlags & 0x80) ||         // Unblockable attack
        (attackFlags & 0x40)) {
        return false;
    }
    
    uint8_t stance = 0;
    SafeReadMemory(playerPtr + 393, &stance, sizeof(uint8_t));
    bool isCrouching = (stance == 1);
    
    double yPos = 0.0;
    SafeReadMemory(playerPtr + 40, &yPos, sizeof(double));
    bool isAerial = (yPos < 0.0);

    if (attackFlags & 0x01) {          // Low attack
        return isCrouching;
    }
    else if (attackFlags & 0x04) {     // High attack
        return !isCrouching || isAerial;
    }
    
    return true;
}
