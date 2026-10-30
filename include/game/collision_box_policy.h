#pragma once

#include <cstdint>

namespace CollisionBoxPolicy {

// Same predicates Revival uses to pick a solid or dotted hit box outline.
// frameAttackFlags: frame blob +170. attackState: low byte of player +364.
inline bool CharacterHitBoxActive(uint8_t frameAttackFlags, uint8_t attackState) {
    return (frameAttackFlags & 0x40u) != 0 || attackState != 0;
}

// Revival skips a fighter's hurt boxes for these, and the engine won't hit them.
// frameFlags: frame blob +176. hitImmunity: player +316.
inline bool CharacterHurtBoxesShown(uint8_t frameFlags, int16_t hitImmunity) {
    return (frameFlags & 0x20u) == 0 && hitImmunity == 0;
}

// handlePlayerToPlayerCollision's attacker gate. contactLatch: low byte of player
// +0x168. attackTimer: player +0x16C. Revival's outline ignores the latch.
inline bool CharacterHitCanConnect(int attackBoxes, uint8_t contactLatch, int16_t attackTimer) {
    return attackBoxes > 0 && contactLatch == 0 && attackTimer > 0;
}

// frameFlags: frame blob +176. life: entry +0x84. destroyed: entry +0x80.
inline bool ProjectileHitBoxActive(uint8_t frameFlags, int16_t life, uint32_t destroyed) {
    return (frameFlags & 0x20u) == 0 && life != 0 && destroyed == 0;
}

} // namespace CollisionBoxPolicy
