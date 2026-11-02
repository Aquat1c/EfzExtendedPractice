#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Mission::SavestateEntityLayout {

constexpr std::size_t kCursorTailOffset = 0x2CA;       // u16 ring tail/oldest
constexpr std::size_t kCursorNextOffset = 0x2CC;       // u16 next allocation
constexpr std::size_t kEntityIdTableOffset = 0x2D0;    // 64 x u32
constexpr std::size_t kAliveFlagsOffset = 0x3D0;       // 64 x u32
constexpr std::size_t kSlotBaseOffset = 0x4D0;
constexpr std::size_t kSlotStride = 0x98;
constexpr std::size_t kSlotAnimPointerOffset = 0x08;   // session-local u32
constexpr std::size_t kSlotPatternOffset = 0x00;       // u16
constexpr std::size_t kSlotFrameOffset = 0x02;         // u16
constexpr std::size_t kSlotFrameTickOffset = 0x04;     // u16
constexpr std::size_t kSlotCount = 64;
constexpr std::size_t kEntityStateOffset = kCursorTailOffset;
constexpr std::size_t kEntityStateEnd = kSlotBaseOffset + kSlotStride * kSlotCount;

constexpr std::size_t SlotOffset(std::size_t slot) {
    return kSlotBaseOffset + kSlotStride * slot;
}

constexpr std::size_t SlotAnimPointerField(std::size_t slot) {
    return SlotOffset(slot) + kSlotAnimPointerOffset;
}

constexpr bool PlayerStateContainsEntityRing(std::size_t size) {
    return size >= kEntityStateEnd;
}

struct Inventory {
    bool valid = false;
    uint16_t tail = 0;
    uint16_t next = 0;
    uint64_t aliveMask = 0;
    int aliveCount = 0;
    uint32_t checksum = 2166136261u; // pointer-excluded FNV-1a
};

inline Inventory Inspect(const uint8_t* bytes, std::size_t size) {
    Inventory out;
    if (!bytes || !PlayerStateContainsEntityRing(size)) return out;

    std::memcpy(&out.tail, bytes + kCursorTailOffset, sizeof(out.tail));
    std::memcpy(&out.next, bytes + kCursorNextOffset, sizeof(out.next));
    for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
        uint32_t alive = 0;
        std::memcpy(&alive, bytes + kAliveFlagsOffset + sizeof(uint32_t) * slot,
                    sizeof(alive));
        if (alive != 0) {
            out.aliveMask |= (uint64_t{1} << slot);
            ++out.aliveCount;
        }
    }

    for (std::size_t offset = kEntityStateOffset; offset < kEntityStateEnd; ++offset) {
        bool pointerByte = false;
        if (offset >= kSlotBaseOffset) {
            const std::size_t inSlot = (offset - kSlotBaseOffset) % kSlotStride;
            pointerByte = inSlot >= kSlotAnimPointerOffset &&
                          inSlot < kSlotAnimPointerOffset + sizeof(uint32_t);
        }
        if (pointerByte) continue;
        out.checksum ^= bytes[offset];
        out.checksum *= 16777619u;
    }
    out.valid = true;
    return out;
}

inline bool Equivalent(const Inventory& lhs, const Inventory& rhs) {
    return lhs.valid && rhs.valid && lhs.tail == rhs.tail && lhs.next == rhs.next &&
           lhs.aliveMask == rhs.aliveMask && lhs.checksum == rhs.checksum;
}

}
