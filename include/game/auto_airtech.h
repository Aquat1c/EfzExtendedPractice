#pragma once
#include <windows.h>
#include <atomic>

extern std::atomic<bool> g_airtechP1Active;
extern std::atomic<bool> g_airtechP2Active;
extern std::atomic<bool> g_airtechP1Airtechable;
extern std::atomic<bool> g_airtechP2Airtechable;
// true = facing right, false = facing left
extern std::atomic<bool> g_airtechP1FacingRight;
extern std::atomic<bool> g_airtechP2FacingRight;

bool IsPlayerAirtechable(short moveID, int playerNum);
bool IsAirtechAnimation(short moveID);

void MonitorAutoAirtech(short moveID1, short moveID2);

// No-ops: airtech uses input injection, so no code bytes are patched.
void ApplyAirtechPatches();
void RemoveAirtechPatches();