#pragma once

#include "practice_contract.h"
#include <atomic>
#include <cassert>
#include <limits>
#include <mutex>
#include <utility>

namespace Practice {

class WorkDomain;

// Protects one immutable mod context; not a callback/trampoline execution guard.
class WorkLease {
public:
    WorkLease() noexcept = default;
    ~WorkLease() { Release(); }
    WorkLease(const WorkLease&) = delete;
    WorkLease& operator=(const WorkLease&) = delete;
    WorkLease(WorkLease&& other) noexcept { Swap(other); }
    WorkLease& operator=(WorkLease&& other) noexcept {
        if (this != &other) { Release(); Swap(other); }
        return *this;
    }
    explicit operator bool() const noexcept { return domain_ != nullptr; }
    uint32_t Generation() const noexcept { return generation_; }
    const EfzTmEntryV1& Context() const noexcept {
        assert(context_);
        return *context_;
    }
    bool IsCurrent() const noexcept;

private:
    friend class WorkDomain;
    WorkLease(WorkDomain* domain, const EfzTmEntryV1* context,
              uint32_t generation) noexcept
        : domain_(domain), context_(context), generation_(generation) {}
    void Release() noexcept;
    void Swap(WorkLease& other) noexcept {
        std::swap(domain_, other.domain_);
        std::swap(context_, other.context_);
        std::swap(generation_, other.generation_);
    }
    WorkDomain* domain_ = nullptr;
    const EfzTmEntryV1* context_ = nullptr;
    uint32_t generation_ = 0;
};

// TryEnter takes no allocation or mutex, so it is safe on the active callback path.
// The domain must outlive all callers, including rejected acquisition attempts.
class WorkDomain {
public:
    WorkDomain() = default;
    WorkDomain(const WorkDomain&) = delete;
    WorkDomain& operator=(const WorkDomain&) = delete;
    ~WorkDomain() {
        assert(admission_.load() == 0);
        assert(readers_.load() == 0);
    }

    bool Publish(const EfzTmEntryV1& context) {
        if (context.size != sizeof(context) ||
            context.abiVersion != EFZ_TM_LIFECYCLE_ABI ||
            !context.id.providerIncarnation || !context.id.practiceSession)
            return false;
        std::lock_guard<std::mutex> lock(transition_);
        if (hasContext_ || admission_.load() || readers_.load() ||
            nextGeneration_ == (std::numeric_limits<uint32_t>::max)())
            return false;
        context_ = context;
        hasContext_ = true;
        admission_.store(++nextGeneration_);
        return true;
    }

    WorkLease TryEnter() noexcept {
        // Both atomics must stay sequentially consistent, or retirement can miss a reader
        // of the previous open generation.
        const uint32_t generation = admission_.load();
        if (!generation) return {};
        uint32_t previous = readers_.load();
        do {
            // Saturate instead of publishing a wrapped zero to retirement.
            if (previous == (std::numeric_limits<uint32_t>::max)()) return {};
        } while (!readers_.compare_exchange_weak(previous, previous + 1));
        if (admission_.load() != generation) {
            readers_.fetch_sub(1);
            return {};
        }
        return WorkLease(this, &context_, generation);
    }

    uint32_t CloseAdmission() noexcept {
        std::lock_guard<std::mutex> lock(transition_);
        return admission_.exchange(0);
    }

    bool Reclaim() noexcept {
        std::lock_guard<std::mutex> lock(transition_);
        if (admission_.load() || readers_.load()) return false;
        context_ = {};
        hasContext_ = false;
        return true;
    }

    uint32_t OutstandingWork() const noexcept { return readers_.load(); }
    uint32_t AdmissionGeneration() const noexcept { return admission_.load(); }

private:
    friend class WorkLease;
    std::mutex transition_;
    alignas(4) std::atomic<uint32_t> admission_{0};
    alignas(4) std::atomic<uint32_t> readers_{0};
    uint32_t nextGeneration_ = 0;
    bool hasContext_ = false;
    EfzTmEntryV1 context_{};
};

inline bool WorkLease::IsCurrent() const noexcept {
    return domain_ && domain_->AdmissionGeneration() == generation_;
}
inline void WorkLease::Release() noexcept {
    if (domain_) domain_->readers_.fetch_sub(1);
    domain_ = nullptr;
    context_ = nullptr;
    generation_ = 0;
}

}
