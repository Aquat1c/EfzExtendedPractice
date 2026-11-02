#include "../../include/input/scoped_input_reservation.h"

#include <atomic>

namespace {

struct Slot {
    std::atomic<uint8_t> owner{0};
    std::atomic<uint64_t> token{0};
};

Slot g_slots[3];
std::atomic<uint64_t> g_nextToken{1};

bool ValidPlayer(int playerNum) {
    return playerNum == 1 || playerNum == 2;
}

}

bool TryReserveScopedInput(int playerNum, ScopedInputOwner owner,
                           uint64_t& tokenOut) {
    tokenOut = 0;
    if (!ValidPlayer(playerNum) || owner == ScopedInputOwner::None) return false;

    uint8_t expected = 0;
    const uint8_t desired = static_cast<uint8_t>(owner);
    if (!g_slots[playerNum].owner.compare_exchange_strong(
            expected, desired, std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        return false;
    }

    uint64_t token = g_nextToken.fetch_add(1, std::memory_order_acq_rel);
    if (token == 0) token = g_nextToken.fetch_add(1, std::memory_order_acq_rel);
    g_slots[playerNum].token.store(token, std::memory_order_release);
    tokenOut = token;
    return true;
}

bool ScopedInputReservationMatches(int playerNum, ScopedInputOwner owner,
                                   uint64_t token) {
    return ValidPlayer(playerNum) && token != 0 &&
           g_slots[playerNum].owner.load(std::memory_order_acquire) ==
               static_cast<uint8_t>(owner) &&
           g_slots[playerNum].token.load(std::memory_order_acquire) == token;
}

bool IsScopedInputReserved(int playerNum) {
    return ValidPlayer(playerNum) &&
           g_slots[playerNum].owner.load(std::memory_order_acquire) != 0;
}

bool IsScopedInputReservedByOther(int playerNum, uint64_t allowedToken) {
    if (!ValidPlayer(playerNum)) return false;
    if (g_slots[playerNum].owner.load(std::memory_order_acquire) == 0) return false;
    return allowedToken == 0 ||
           g_slots[playerNum].token.load(std::memory_order_acquire) != allowedToken;
}

void ReleaseScopedInputReservation(int playerNum, ScopedInputOwner owner,
                                   uint64_t token) {
    if (!ScopedInputReservationMatches(playerNum, owner, token)) return;
    g_slots[playerNum].token.store(0, std::memory_order_release);
    uint8_t expected = static_cast<uint8_t>(owner);
    (void)g_slots[playerNum].owner.compare_exchange_strong(
        expected, 0, std::memory_order_acq_rel, std::memory_order_acquire);
}

void ReleaseAllScopedInputReservations(ScopedInputOwner owner) {
    for (int playerNum = 1; playerNum <= 2; ++playerNum) {
        const uint8_t desired = static_cast<uint8_t>(owner);
        if (g_slots[playerNum].owner.load(std::memory_order_acquire) != desired) {
            continue;
        }
        const uint64_t token =
            g_slots[playerNum].token.load(std::memory_order_acquire);
        ReleaseScopedInputReservation(playerNum, owner, token);
    }
}

