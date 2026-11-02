#pragma once
#include "audio_owner_abi.h"
#include <windows.h>
#include <atomic>

// Registration freezes initial ownership, so a late Initialize cannot activate
// a transform behind an acknowledged raw lane.
class EfzAudioOwnerProvider {
public:
    explicit EfzAudioOwnerProvider(uint64_t incarnation) noexcept {
        InitializeCriticalSection(&boundary_);
        record_.providerIncarnation = incarnation;
        record_.acknowledgementGeneration = 1;
    }
    ~EfzAudioOwnerProvider() { DeleteCriticalSection(&boundary_); }
    void Enter() noexcept {
        const DWORD lastError = GetLastError();
        EnterCriticalSection(&boundary_);
        if (++depth_ == 1) owningThread_.store(GetCurrentThreadId(), std::memory_order_release);
        SetLastError(lastError);
    }
    void Leave() noexcept {
        const DWORD lastError = GetLastError();
        if (--depth_ == 0) owningThread_.store(0, std::memory_order_release);
        LeaveCriticalSection(&boundary_);
        SetLastError(lastError);
    }
    void ActivateAtInitialization(uint32_t lanes) noexcept {
        Enter();
        if (!registered_) {
            record_.ownedLaneMask = lanes & EfzAudioAllLanes;
            record_.inputsAreRawLaneMask = EfzAudioAllLanes & ~record_.ownedLaneMask;
            ++record_.acknowledgementGeneration;
        }
        Leave();
    }
    bool Query(EfzAudioOwnerV1* out) noexcept {
        if (!out || out->size != sizeof(*out) || out->version != 1) return false;
        Enter(); registered_ = true; *out = record_; Leave();
        return true;
    }
    uint32_t OwnedLanesOnBoundary() const noexcept { return record_.ownedLaneMask; }
    bool Retire(const EfzAudioOwnerV1* expected, uint32_t lanes,
                EfzAudioOwnerCommitV1 commit, void* context) noexcept {
        if (owningThread_.load(std::memory_order_acquire) == GetCurrentThreadId() ||
            !expected || !commit || (lanes & ~EfzAudioAllLanes)) return false;
        bool ok = false;
        Enter();
        __try {
            if (expected->size == sizeof(*expected) && expected->version == 1 &&
                expected->providerIncarnation == record_.providerIncarnation &&
                expected->acknowledgementGeneration == record_.acknowledgementGeneration &&
                expected->ownedLaneMask == record_.ownedLaneMask &&
                expected->inputsAreRawLaneMask == record_.inputsAreRawLaneMask) {
                registered_ = true;
                record_.ownedLaneMask &= ~lanes;
                record_.inputsAreRawLaneMask |= lanes;
                ++record_.acknowledgementGeneration;
                commit(&record_, context);
                ok = true;
            }
        } __finally { Leave(); }
        return ok;
    }
private:
    CRITICAL_SECTION boundary_{};
    EfzAudioOwnerV1 record_{};
    bool registered_ = false;
    unsigned depth_ = 0; // accessed only while this thread holds boundary_
    std::atomic<DWORD> owningThread_{0};
};
