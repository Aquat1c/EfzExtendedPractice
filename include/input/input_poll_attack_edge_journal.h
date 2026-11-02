#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

// SPSC: the input hook is the only producer, mission recording the only consumer.
namespace InputPollAttackEdgeJournalPolicy {

struct Event {
    uint32_t serial{0};
    uint8_t mask{0};
};

struct DrainResult {
    std::size_t count{0};
    uint32_t droppedEvents{0};
    bool overflowed{false};
};

template <std::size_t Capacity>
class Journal {
public:
    static_assert(Capacity > 0, "attack-edge journal capacity must be non-zero");

    // Producer only. A full journal drops the newest edge; the next drain reports a dropped count.
    bool Push(const Event& event) noexcept {
        const uint32_t write = m_write.load(std::memory_order_relaxed);
        const uint32_t read = m_read.load(std::memory_order_acquire);
        if (static_cast<uint32_t>(write - read) >= Capacity) {
            m_dropped.fetch_add(1, std::memory_order_release);
            return false;
        }

        m_events[write % Capacity] = event;
        m_write.store(write + 1, std::memory_order_release);
        return true;
    }

    DrainResult Drain(Event* output, std::size_t outputCapacity) noexcept {
        DrainResult result{};
        if (output != nullptr && outputCapacity != 0) {
            const uint32_t read = m_read.load(std::memory_order_relaxed);
            const uint32_t write = m_write.load(std::memory_order_acquire);
            const uint32_t available = static_cast<uint32_t>(write - read);
            result.count = std::min<std::size_t>(available, outputCapacity);
            for (std::size_t index = 0; index < result.count; ++index) {
                output[index] = m_events[(read + static_cast<uint32_t>(index)) %
                                         Capacity];
            }
            m_read.store(read + static_cast<uint32_t>(result.count),
                         std::memory_order_release);
        }

        result.droppedEvents =
            m_dropped.exchange(0, std::memory_order_acq_rel);
        result.overflowed = result.droppedEvents != 0;
        return result;
    }

    // Consumer/boundary owner only. Discards events before the acquired cursor; an edge racing after it
    // stays visible, so a boundary can't erase it. The input hook owns the monotonic serials.
    void Reset() noexcept {
        const uint32_t write = m_write.load(std::memory_order_acquire);
        m_read.store(write, std::memory_order_release);
        m_dropped.exchange(0, std::memory_order_acq_rel);
    }

private:
    std::array<Event, Capacity> m_events{};
    std::atomic<uint32_t> m_write{0};
    std::atomic<uint32_t> m_read{0};
    std::atomic<uint32_t> m_dropped{0};
};

}
