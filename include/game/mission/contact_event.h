#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

// Player +0x168 is deliberately not a result enum: it is a latch that FIC scripts and
// entity interactions also write.
namespace Mission::Contact {

enum class Source : uint8_t {
    None = 0,
    DirectPlayer,
    Entity,
};

enum class Result : uint8_t {
    None = 0,
    Hit,
    Block,
    RecoilGuard,
    Throw,
    SpecialHit,
    GuardPoint,
    Unknown,
};

struct DirectEvidence {
    bool resolved = false;
    bool comboIncreased = false;
    // EFZ can start a new combo inside one collision update, exposing N -> 1 and never N -> 0 -> 1.
    // Set only with positive-combo, damage, and hit-branch corroboration.
    bool comboRestarted = false;
    bool defenderBlocked = false;
    bool defenderRecoilGuard = false;
    bool exactThrowBranch = false;
    bool exactGuardPointBranch = false;
    int  rawAttackerState = 0;      // 7 = special hit; other values are diagnostic
};

// Entity -> player resolver at 0x7697D0. rawEntityState is the entity's +0x80 latch: character
// scripts also write it, so it only counts paired with a resolver-local side effect.
struct EntityEvidence {
    bool resolved = false;
    bool comboIncreased = false;
    bool comboRestarted = false;
    bool defenderBlocked = false;
    bool defenderRecoilGuard = false;
    int  rawEntityState = 0;
};

constexpr bool CorroboratesComboRestart(int comboBefore, int comboAfter,
                                        int hpBefore, int hpAfter,
                                        bool defenderInHitReaction,
                                        int rawHitState) {
    return comboBefore > 0 && comboAfter > 0 && comboAfter < comboBefore &&
           hpAfter < hpBefore && defenderInHitReaction &&
           (rawHitState == 3 || rawHitState == 7);
}

// Ambiguous armor/counter/Guard Point paths stay Unknown, which never clears a strict contract.
constexpr Result ClassifyDirect(const DirectEvidence& e) {
    if (!e.resolved) return Result::None;
    if (e.defenderRecoilGuard) return Result::RecoilGuard;
    if (e.exactThrowBranch) return Result::Throw;
    if (e.exactGuardPointBranch) return Result::GuardPoint;
    if (e.defenderBlocked) return Result::Block;
    if (e.comboIncreased || e.comboRestarted) {
        return e.rawAttackerState == 7 ? Result::SpecialHit : Result::Hit;
    }
    return Result::Unknown;
}

constexpr Result ClassifyEntity(const EntityEvidence& e) {
    if (!e.resolved) return Result::None;
    if (e.rawEntityState == 2 && e.defenderRecoilGuard) {
        return Result::RecoilGuard;
    }
    if (e.rawEntityState == 2 && e.defenderBlocked) {
        return Result::Block;
    }
    if ((e.rawEntityState == 3 || e.rawEntityState == 7) &&
        (e.comboIncreased || e.comboRestarted)) {
        return e.rawEntityState == 7 ? Result::SpecialHit : Result::Hit;
    }
    return Result::Unknown;
}

inline const char* ResultName(Result result) {
    switch (result) {
        case Result::Hit:         return "hit";
        case Result::Block:       return "block";
        case Result::RecoilGuard: return "recoil guard";
        case Result::Throw:       return "throw";
        case Result::SpecialHit:  return "special hit";
        case Result::GuardPoint:  return "guard point";
        case Result::Unknown:     return "unclassified contact";
        default:                  return "none";
    }
}

inline bool ValidResultRequirement(const std::string& result) {
    return result == "hit" || result == "block" ||
           result == "recoil_guard" || result == "throw" ||
           result == "special" || result == "guard_point" ||
           result == "whiff";
}

inline bool ResultMatches(const std::string& requirement, Result observed) {
    if (requirement == "hit") {
        return observed == Result::Hit || observed == Result::SpecialHit;
    }
    if (requirement == "block") return observed == Result::Block;
    if (requirement == "recoil_guard") return observed == Result::RecoilGuard;
    if (requirement == "throw") return observed == Result::Throw;
    if (requirement == "special") return observed == Result::SpecialHit;
    if (requirement == "guard_point") return observed == Result::GuardPoint;
    return false;
}

struct Event {
    uint32_t sequence = 0;
    uint32_t worldEpoch = 0;
    uint32_t batchId = 0;
    uint8_t attacker = 0;
    uint8_t defender = 0;
    Source source = Source::None;
    Result result = Result::None;
    short attackerMove = 0;
    short attackerFrame = 0;
    // Sampled around the exact resolver call. OTG and other state-sensitive contact must use the
    // before value; a later mission snapshot already shows hitstun.
    short defenderMoveBefore = 0;
    short defenderMove = 0;
    short timerBefore = 0;
    short timerAfter = 0;
    int rawStateBefore = 0;
    int rawStateAfter = 0;
    int comboBefore = 0;
    int comboAfter = 0;
    int defenderHpBefore = 0;
    int defenderHpAfter = 0;
    int entitySlot = -1;
    int entityPattern = -1;
};

struct ReadResult {
    size_t count = 0;
    uint32_t consumedThrough = 0;
    bool overflow = false;
};

// Single writer, many readers. Every payload member is atomic so a versioned slot read stays
// race-free when a game update wraps the ring during a monitor-thread batch copy.
template <size_t Capacity>
class Journal {
    static_assert(Capacity >= 4, "contact journal is too small");

    struct Slot {
        std::atomic<uint32_t> version{0};
        std::atomic<uint32_t> sequence{0};
        std::atomic<uint32_t> worldEpoch{0};
        std::atomic<uint32_t> batchId{0};
        std::atomic<int> attacker{0};
        std::atomic<int> defender{0};
        std::atomic<int> source{0};
        std::atomic<int> result{0};
        std::atomic<int> attackerMove{0};
        std::atomic<int> attackerFrame{0};
        std::atomic<int> defenderMoveBefore{0};
        std::atomic<int> defenderMove{0};
        std::atomic<int> timerBefore{0};
        std::atomic<int> timerAfter{0};
        std::atomic<int> rawStateBefore{0};
        std::atomic<int> rawStateAfter{0};
        std::atomic<int> comboBefore{0};
        std::atomic<int> comboAfter{0};
        std::atomic<int> defenderHpBefore{0};
        std::atomic<int> defenderHpAfter{0};
        std::atomic<int> entitySlot{-1};
        std::atomic<int> entityPattern{-1};
    };

public:
    uint32_t Publish(Event event) {
        // Do not advance the public watermark until the slot is complete: a reader seeing an odd slot
        // would report overflow, advance its cursor, and permanently lose the event.
        const uint32_t seq = nextSequence_.load(std::memory_order_relaxed) + 1;
        event.sequence = seq;
        event.worldEpoch = epoch_.load(std::memory_order_acquire);
        Slot& slot = slots_[seq % Capacity];
        slot.version.fetch_add(1, std::memory_order_acq_rel); // odd: writer owns slot
        slot.sequence.store(event.sequence, std::memory_order_relaxed);
        slot.worldEpoch.store(event.worldEpoch, std::memory_order_relaxed);
        slot.batchId.store(event.batchId, std::memory_order_relaxed);
        slot.attacker.store(event.attacker, std::memory_order_relaxed);
        slot.defender.store(event.defender, std::memory_order_relaxed);
        slot.source.store(static_cast<int>(event.source), std::memory_order_relaxed);
        slot.result.store(static_cast<int>(event.result), std::memory_order_relaxed);
        slot.attackerMove.store(event.attackerMove, std::memory_order_relaxed);
        slot.attackerFrame.store(event.attackerFrame, std::memory_order_relaxed);
        slot.defenderMoveBefore.store(event.defenderMoveBefore, std::memory_order_relaxed);
        slot.defenderMove.store(event.defenderMove, std::memory_order_relaxed);
        slot.timerBefore.store(event.timerBefore, std::memory_order_relaxed);
        slot.timerAfter.store(event.timerAfter, std::memory_order_relaxed);
        slot.rawStateBefore.store(event.rawStateBefore, std::memory_order_relaxed);
        slot.rawStateAfter.store(event.rawStateAfter, std::memory_order_relaxed);
        slot.comboBefore.store(event.comboBefore, std::memory_order_relaxed);
        slot.comboAfter.store(event.comboAfter, std::memory_order_relaxed);
        slot.defenderHpBefore.store(event.defenderHpBefore, std::memory_order_relaxed);
        slot.defenderHpAfter.store(event.defenderHpAfter, std::memory_order_relaxed);
        slot.entitySlot.store(event.entitySlot, std::memory_order_relaxed);
        slot.entityPattern.store(event.entityPattern, std::memory_order_relaxed);
        slot.version.fetch_add(1, std::memory_order_release); // even: publish
        nextSequence_.store(seq, std::memory_order_release);
        return seq;
    }

    uint32_t ResetEpoch() {
        return epoch_.fetch_add(1, std::memory_order_acq_rel) + 1;
    }

    uint32_t Epoch() const { return epoch_.load(std::memory_order_acquire); }
    uint32_t Watermark() const { return nextSequence_.load(std::memory_order_acquire); }

    ReadResult ReadAfter(uint32_t afterSequence, uint32_t expectedEpoch,
                         uint32_t closedBatch, Event* out, size_t outCapacity) const {
        ReadResult result;
        result.consumedThrough = afterSequence;
        const uint32_t current = Watermark();
        uint32_t first = afterSequence + 1;
        if (current > afterSequence && current - afterSequence > Capacity) {
            first = current - static_cast<uint32_t>(Capacity) + 1;
            result.overflow = true;
        }

        for (uint32_t seq = first; seq != 0 && seq <= current; ++seq) {
            Event event;
            if (!LoadSlot(seq, event)) {
                result.overflow = true;
                result.consumedThrough = seq;
                continue;
            }
            if (event.batchId > closedBatch) break;
            result.consumedThrough = seq;
            if (event.worldEpoch != expectedEpoch) continue;
            if (result.count < outCapacity && out) {
                out[result.count++] = event;
            } else {
                result.overflow = true;
            }
        }
        return result;
    }

private:
    bool LoadSlot(uint32_t expectedSequence, Event& out) const {
        const Slot& slot = slots_[expectedSequence % Capacity];
        for (int attempt = 0; attempt < 6; ++attempt) {
            const uint32_t before = slot.version.load(std::memory_order_acquire);
            if (before & 1u) continue;
            Event event;
            event.sequence = slot.sequence.load(std::memory_order_relaxed);
            event.worldEpoch = slot.worldEpoch.load(std::memory_order_relaxed);
            event.batchId = slot.batchId.load(std::memory_order_relaxed);
            event.attacker = static_cast<uint8_t>(slot.attacker.load(std::memory_order_relaxed));
            event.defender = static_cast<uint8_t>(slot.defender.load(std::memory_order_relaxed));
            event.source = static_cast<Source>(slot.source.load(std::memory_order_relaxed));
            event.result = static_cast<Result>(slot.result.load(std::memory_order_relaxed));
            event.attackerMove = static_cast<short>(slot.attackerMove.load(std::memory_order_relaxed));
            event.attackerFrame = static_cast<short>(slot.attackerFrame.load(std::memory_order_relaxed));
            event.defenderMoveBefore = static_cast<short>(
                slot.defenderMoveBefore.load(std::memory_order_relaxed));
            event.defenderMove = static_cast<short>(slot.defenderMove.load(std::memory_order_relaxed));
            event.timerBefore = static_cast<short>(slot.timerBefore.load(std::memory_order_relaxed));
            event.timerAfter = static_cast<short>(slot.timerAfter.load(std::memory_order_relaxed));
            event.rawStateBefore = slot.rawStateBefore.load(std::memory_order_relaxed);
            event.rawStateAfter = slot.rawStateAfter.load(std::memory_order_relaxed);
            event.comboBefore = slot.comboBefore.load(std::memory_order_relaxed);
            event.comboAfter = slot.comboAfter.load(std::memory_order_relaxed);
            event.defenderHpBefore = slot.defenderHpBefore.load(std::memory_order_relaxed);
            event.defenderHpAfter = slot.defenderHpAfter.load(std::memory_order_relaxed);
            event.entitySlot = slot.entitySlot.load(std::memory_order_relaxed);
            event.entityPattern = slot.entityPattern.load(std::memory_order_relaxed);
            const uint32_t after = slot.version.load(std::memory_order_acquire);
            if (before == after && !(after & 1u) && event.sequence == expectedSequence) {
                out = event;
                return true;
            }
        }
        return false;
    }

    std::array<Slot, Capacity> slots_{};
    std::atomic<uint32_t> nextSequence_{0};
    std::atomic<uint32_t> epoch_{1};
};

}
