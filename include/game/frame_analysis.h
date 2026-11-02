#pragma once
#include <windows.h>
#include <string>

extern short initialBlockstunMoveID;

bool IsHitstun(short moveID);
bool IsLaunched(short moveID);
bool IsAirtech(short moveID);
bool IsGroundtech(short moveID);
bool IsFrozen(short moveID);
bool IsSpecialStun(short moveID);
bool IsThrown(short moveID);
bool IsBlockstunState(short moveID);   
int GetAttackLevel(short blockstunMoveID);
std::string GetBlockStateType(short blockstunMoveID);
int GetExpectedFrameAdvantage(int attackLevel, bool isAirBlock, bool isHit = false);
short GetUntechValue(uintptr_t base, int player);

short GetBlockstunValue(uintptr_t base, int player);