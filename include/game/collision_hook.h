#pragma once
#include <cstdint>
#include <cstddef>

#include "mission/contact_event.h"

void InstallCollisionHook();
void SetCollisionHookActive(bool active);
void RemoveCollisionHook();

uintptr_t GetCachedAttackDataForPlayer(int playerNum);

// Offset from the player base to the current attack data pointer, or -1 if not yet found.
int GetAttackDataOffsetForPlayer(int playerNum);
enum class CollisionLayoutStatus : uint8_t {NotAvailableYet,UnsupportedLayout,Ready};
CollisionLayoutStatus GetCollisionLayoutStatusForPlayer(int playerNum);

void ResetCollisionHookSessionCaches(const char* reason);

// Contact events are published from inside EFZ's resolvers (direct 0x767F60, entity 0x7697D0),
// not inferred from a later monitor sample.
bool IsDirectContactHookReady();
bool IsEntityContactHookReady();
uint32_t GetContactEventEpoch();
uint32_t GetContactEventWatermark();
void ResetContactEventJournal(const char* reason);
Mission::Contact::ReadResult ReadCommittedContactEvents(
    uint32_t afterSequence, uint32_t expectedEpoch, uint32_t closedBatch,
    Mission::Contact::Event* out, size_t outCapacity);
