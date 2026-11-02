#include "utils/audio_runtime_state.h"
#include <windows.h>
#include <atomic>
#include <algorithm>
namespace AudioControl {
namespace {
static_assert(std::atomic<uint32_t>::is_always_lock_free, "x86 settings require lock-free uint32 storage");
alignas(4) std::atomic<uint32_t> settings{PackAudioSettings({100,100,true,true,0})};
std::atomic<bool> applyPending{false};
uint32_t disabledLanes = 0;
EfzAudioOwnerV1 acknowledged{};
const EfzAudioOwnerApiV1* boundary = nullptr;
struct LocalBoundary {
    CRITICAL_SECTION cs;
    LocalBoundary() { InitializeCriticalSection(&cs); }
    ~LocalBoundary() { DeleteCriticalSection(&cs); }
} localBoundary;
void Store(AudioSettingsView view) noexcept {
    view.bgmPercent = (std::min)(uint8_t(100), view.bgmPercent);
    view.sePercent = (std::min)(uint8_t(100), view.sePercent);
    const uint32_t unavailable = acknowledged.ownedLaneMask | disabledLanes;
    view.trainingOwnsBgmGain = !(unavailable & EfzAudioLaneBgm);
    view.trainingOwnsSeGain = !(unavailable & EfzAudioLaneSe);
    const auto old = settings.load(std::memory_order_relaxed);
    if ((PackAudioSettings(view) & 0xffff) == (old & 0xffff)) return;
    view.publication = uint16_t((old >> 16) + 1);
    settings.store(PackAudioSettings(view), std::memory_order_release);
    applyPending.store(true, std::memory_order_release);
}
}
void BindAudioBoundary(const EfzAudioOwnerApiV1* provider) noexcept { boundary = provider; }
void EnterAudioBoundary() noexcept { const DWORD saved = GetLastError(); if (boundary) boundary->enter(); else EnterCriticalSection(&localBoundary.cs); SetLastError(saved); }
void LeaveAudioBoundary() noexcept { const DWORD saved = GetLastError(); if (boundary) boundary->leave(); else LeaveCriticalSection(&localBoundary.cs); SetLastError(saved); }
AudioSettingsView ReadAudioSettings() noexcept { return UnpackAudioSettings(settings.load(std::memory_order_acquire)); }
void PublishAudioSettingsView(AudioSettingsView view) noexcept {
    EnterAudioBoundary();
    Store(view);
    LeaveAudioBoundary();
}
void PublishAudioPercents(int bgm, int se) noexcept {
    PublishAudioSettingsView({uint8_t((std::clamp)(bgm,0,100)), uint8_t((std::clamp)(se,0,100)),false,false,0});
}
bool AcknowledgeAudioOwner(const EfzAudioOwnerV1& record) noexcept {
    if (record.size != sizeof(record) || record.version != 1 || !record.providerIncarnation ||
        !record.acknowledgementGeneration || (record.ownedLaneMask & ~EfzAudioAllLanes) ||
        (record.inputsAreRawLaneMask & ~EfzAudioAllLanes) ||
        (record.inputsAreRawLaneMask & record.ownedLaneMask) ||
        (acknowledged.providerIncarnation && acknowledged.providerIncarnation != record.providerIncarnation) ||
        record.acknowledgementGeneration < acknowledged.acknowledgementGeneration) return false;
    if (record.acknowledgementGeneration == acknowledged.acknowledgementGeneration &&
        (record.ownedLaneMask != acknowledged.ownedLaneMask || record.inputsAreRawLaneMask != acknowledged.inputsAreRawLaneMask)) return false;
    acknowledged = record;
    disabledLanes = EfzAudioAllLanes & ~(record.ownedLaneMask | record.inputsAreRawLaneMask);
    Store(UnpackAudioSettings(settings.load(std::memory_order_relaxed)));
    return true;
}
void MarkIncompatibleAudioOwner(uint32_t lanes) noexcept {
    EnterAudioBoundary();
    disabledLanes |= lanes & EfzAudioAllLanes;
    Store(UnpackAudioSettings(settings.load(std::memory_order_relaxed)));
    LeaveAudioBoundary();
}
bool ConsumeAudioApplyRequest() noexcept { return applyPending.exchange(false,std::memory_order_acq_rel); }
}
