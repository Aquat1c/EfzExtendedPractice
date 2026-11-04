#pragma once

#include <atomic>
#include <cstdint>

// Owner state for one overlay backend and one pending device reset; hold Access for every backend
// operation. Try-acquire refuses reentrancy rather than waiting inside D3D9 Reset's message dispatch.
class EfzOverlayResetGate {
public:
    enum class Result { Ok, InvalidArgument, WrongThread, Busy };

    class Access {
    public:
        explicit Access(EfzOverlayResetGate& gate)
            : gate_(gate), acquired_(!gate_.lock_.test_and_set(std::memory_order_acquire)) {}
        ~Access() { Release(); }
        Access(const Access&) = delete;
        Access& operator=(const Access&) = delete;
        bool Acquired() const { return acquired_; }
        void Release() {
            if (acquired_) {
                acquired_ = false;
                gate_.lock_.clear(std::memory_order_release);
            }
        }
    private:
        EfzOverlayResetGate& gate_;
        bool acquired_;
    };

    bool CanRender(void* device, uint32_t thread) const {
        return device && thread && !(IsResetPending() && resetDevice_ == device)
            && (!boundDevice_ || ownerThread_ == thread);
    }

    bool BindBackend(void* device, uint32_t thread) {
        if (!CanRender(device, thread)) return false;
        boundDevice_ = device;
        ownerThread_ = thread;
        if (phase_ == ResetPhase::BackendInvalidated) phase_ = ResetPhase::Pending;
        return true;
    }

    void UnbindBackend() {
        boundDevice_ = nullptr;
        ownerThread_ = 0;
        if (phase_ == ResetPhase::BackendInvalidated) phase_ = ResetPhase::Pending;
        // A menu/online suspension must not clear the game-device reset gate.
    }

    void* BoundDevice() const { return boundDevice_; }
    uint32_t OwnerThread() const { return ownerThread_; }
    bool PendingFor(void* device) const { return IsResetPending() && resetDevice_ == device; }
    bool BackendInvalidatedFor(void* device) const {
        return PendingFor(device) && boundDevice_ == device && phase_ == ResetPhase::BackendInvalidated;
    }

    Result BeforeReset(void* device, uint32_t epoch, uint32_t thread,
                       bool& invalidate) {
        invalidate = false;
        if (!device || !epoch || !thread) return Result::InvalidArgument;
        if (boundDevice_ == device && ownerThread_ != thread) return Result::WrongThread;
        if (IsResetPending()) {
            if (resetDevice_ != device) return Result::Busy;
            if (resetThread_ != thread) return Result::WrongThread;
            // Accept epoch forward progress (incl. wrap); never let a stale
            // BeforeReset authorize a stale completion.
            if (epoch != epoch_ && uint32_t(epoch - epoch_) > 0x7fffffffu)
                return Result::InvalidArgument;
        }
        if (phase_ == ResetPhase::Idle) phase_ = ResetPhase::Pending;
        resetDevice_ = device;
        resetThread_ = thread;
        epoch_ = epoch;
        invalidate = boundDevice_ == device && phase_ != ResetPhase::BackendInvalidated;
        return Result::Ok;
    }

    void DidInvalidateBackend() {
        if (IsResetPending() && boundDevice_ == resetDevice_) phase_ = ResetPhase::BackendInvalidated;
    }

    Result AfterReset(void* device, uint32_t epoch, uint32_t thread,
                      uint32_t ready) {
        if (!device || !epoch || !thread || ready > 1 || !IsResetPending()
            || resetDevice_ != device || epoch_ != epoch)
            return Result::InvalidArgument;
        if (resetThread_ != thread
            || (boundDevice_ == device && ownerThread_ != thread))
            return Result::WrongThread;
        if (ready && boundDevice_ == device && phase_ != ResetPhase::BackendInvalidated)
            return Result::Busy;
        if (ready) {
            phase_ = ResetPhase::Idle;
            resetDevice_ = nullptr;
            resetThread_ = 0;
            epoch_ = 0;
        }
        return Result::Ok;
    }

private:
    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    void* boundDevice_ = nullptr;
    uint32_t ownerThread_ = 0;
    void* resetDevice_ = nullptr;
    uint32_t resetThread_ = 0;
    uint32_t epoch_ = 0;
    enum class ResetPhase : uint8_t { Idle, Pending, BackendInvalidated };
    ResetPhase phase_ = ResetPhase::Idle;
    bool IsResetPending() const { return phase_ != ResetPhase::Idle; }
};
