#pragma once
#include <atomic>
#include <cstdint>

namespace ImmediateInput {
enum class OutputMode { Idle, Held, Timed };
constexpr OutputMode ReadOutputMode(uint8_t desired, int ticks) {
    return ticks > 0 ? OutputMode::Timed : desired != 0 ? OutputMode::Held : OutputMode::Idle;
}

enum class WorkerPhase { Stopped, Running, CancelRequested, Joining };
class WorkerLifecycle {
public:
    bool IsRunning(std::memory_order order = std::memory_order_seq_cst) const {
        return OwnsRunningWorker(phase_.load(order));
    }
    bool ShouldStop() const { return phase_.load(std::memory_order_acquire) != WorkerPhase::Running; }
    void Started() { phase_.store(WorkerPhase::Running, std::memory_order_release); }
    void Exited() { phase_.store(WorkerPhase::Stopped, std::memory_order_release); }
    void RequestStop() {
        auto expected = WorkerPhase::Running;
        (void)phase_.compare_exchange_strong(expected, WorkerPhase::CancelRequested,
                                           std::memory_order_release, std::memory_order_relaxed);
    }
    bool BeginJoin() {
        return OwnsRunningWorker(phase_.exchange(WorkerPhase::Joining, std::memory_order_acq_rel));
    }
private:
    static bool OwnsRunningWorker(WorkerPhase phase) {
        return phase == WorkerPhase::Running || phase == WorkerPhase::CancelRequested;
    }
    std::atomic<WorkerPhase> phase_{WorkerPhase::Stopped};
};
}
