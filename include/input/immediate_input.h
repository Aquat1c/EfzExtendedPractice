#pragma once

#include <atomic>
#include <cstdint>
struct EfzTmEntryV1;

// Writes only the immediate registers at 64 Hz, never the buffer. Inserts a neutral frame when
// changing or re-asserting a non-zero mask.

namespace ImmediateInput {

void Start();
void Stop();
void RetireWorker();
bool BindPracticeWorld(const EfzTmEntryV1& world);
bool RetireWorker(const EfzTmEntryV1& heldWorld);
void CancelWork();
bool IsRunning();

bool Set(int playerNum, uint8_t mask);

// ticks are visual frames; ticks<=0 is ignored. Returns false, caching nothing, when another owner
// holds the raw lane; callers may retry the same action later.
bool PressFor(int playerNum, uint8_t mask, int ticks);

void Clear(int playerNum);

uint8_t GetCurrentDesired(int playerNum);
int GetRemainingTicks(int playerNum);

bool RetireCompletedBookkeeping(int playerNum);

// Tutorial-only lease: regular Set/PressFor/Clear calls are ignored for this player while held.
bool AcquireTutorialLease(int playerNum, uint64_t& tokenOut);
bool PressForTutorial(int playerNum, uint64_t token, uint8_t mask, int ticks);
void ReleaseTutorialLease(int playerNum, uint64_t token);
bool TutorialLeaseActive(int playerNum);

}
