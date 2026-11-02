#pragma once
#include <cstdint>

enum AttackHeight {
    ATTACK_HEIGHT_UNKNOWN = 0,
    ATTACK_HEIGHT_HIGH,
    ATTACK_HEIGHT_MID,
    ATTACK_HEIGHT_LOW,
    ATTACK_HEIGHT_THROW
};

class AttackReader {
public:
    static AttackHeight GetAttackHeight(int playerPtr, short moveID);
    static bool IsMoveActive(int playerPtr, short moveID);
    static uintptr_t GetAttackDataPtr(int playerPtr, short moveID);
    static void LogMoveData(int playerNum, short moveID);
    static bool CanPlayerBlock(int playerPtr, int attackerPtr, uintptr_t attackDataPtr);
};