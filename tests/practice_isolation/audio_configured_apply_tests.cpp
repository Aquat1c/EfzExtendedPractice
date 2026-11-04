#include "utils/audio_runtime_state.h"
#include "utils/audio_callback_dispatch.inl"
#include <windows.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <cstdio>

std::recursive_mutex g_p2ControlMutex;
std::atomic<bool> g_onlineModeActive{false};
namespace {
int failures, writes, readyReads;
bool sessionActive, suppressed, ready = true, fault;
std::atomic<bool> applyEntered{false}, releaseApply{true};
AudioControl::AudioSettingsView applied{};
void Check(bool ok, const char* name) { if (!ok) { ++failures; std::printf("FAIL %s\n", name); } }
}
bool IsNetplaySessionActive() { return sessionActive; }
namespace AudioControl {
bool RuntimeAudioControlSuppressed(const char*) { return suppressed; }
bool RuntimeVolumeApplicationReady() { ++readyReads; return ready; }
bool TryEnableVolumeApplication(uintptr_t, const char*) { return ready; }
uintptr_t AudioGameSystem() { return 1; }
bool SehApplyBgmVolumeToGameSystem(uintptr_t, const AudioSettingsView& view) {
    applyEntered = true;
    while (!releaseApply.load()) SwitchToThread();
    if (fault) RaiseException(0xe0000042, 0, 0, nullptr);
    ++writes; applied = view; return true;
}
bool SehApplyCommonSeVolumeToGameSystem(uintptr_t, const AudioSettingsView&) { ++writes; return true; }
bool SehApplyCharacterSeVolumesNow(const AudioSettingsView&) { ++writes; return true; }
#include "utils/audio_configured_apply.inl"
}
namespace {
bool CatchApplyFault() {
    __try { AudioControl::ApplyConfiguredVolumesNow(); return false; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
}
}
int main() {
    using namespace AudioControl;
    PublishAudioPercents(50, 25);
    g_onlineModeActive = true;
    ApplyConfiguredVolumesNow();
    Check(writes == 0 && readyReads == 0, "suspended direct apply touches no native state");
    Check(ConsumeAudioApplyRequest(), "online deferral retains settings request");
    g_onlineModeActive = false;
    sessionActive = true;
    ApplyConfiguredVolumesNow();
    Check(ConsumeAudioApplyRequest(), "ownership change after request consumption requeues the apply");
    Check(writes == 0 && readyReads == 0, "active session blocks apply before suspend publication");
    sessionActive = false;
    ApplyConfiguredVolumesNow();
    Check(writes == 3 && applied.bgmPercent == 50 && applied.sePercent == 25,
          "offline apply retains configured BGM and SE gains");
    suppressed = true;
    ApplyConfiguredVolumesNow();
    Check(writes == 3, "external suppression remains effective");
    suppressed = false;
    ready = false;
    ApplyConfiguredVolumesNow();
    Check(writes == 3, "unready native audio has no writes");
    Check(ConsumeAudioApplyRequest(), "startup apply waits for native sound initialization");
    ready = true;

    g_p2ControlMutex.lock();
    std::atomic<bool> waiting{false};
    std::thread lateApply([&] { waiting = true; ApplyConfiguredVolumesNow(); });
    while (!waiting.load()) SwitchToThread();
    g_onlineModeActive = true;
    g_p2ControlMutex.unlock();
    lateApply.join();
    Check(writes == 3, "apply rechecks online after acquiring handoff barrier");
    g_onlineModeActive = false;

    applyEntered = false;
    releaseApply = false;
    std::thread activeApply([] { ApplyConfiguredVolumesNow(); });
    while (!applyEntered.load()) SwitchToThread();
    std::atomic<bool> transferCheckedLock{false}, acquiredEarly{false}, published{false};
    int writesAtHandoff = 0;
    std::thread handoff([&] {
        const bool acquired = g_p2ControlMutex.try_lock();
        acquiredEarly = acquired;
        transferCheckedLock = true;
        if (!acquired) g_p2ControlMutex.lock();
        writesAtHandoff = writes;
        g_onlineModeActive = true;
        published = true;
        g_p2ControlMutex.unlock();
    });
    while (!transferCheckedLock.load()) SwitchToThread();
    Check(!acquiredEarly.load() && !published.load(), "handoff waits for in-flight native volume writes");
    releaseApply = true;
    activeApply.join(); handoff.join();
    Check(writes == 6 && writesAtHandoff == 6 && published.load(), "handoff publishes only after current apply completes");
    g_onlineModeActive = false;

    fault = true;
    Check(CatchApplyFault(), "native apply exception reaches outer handler");
    fault = false;
    std::thread afterFault([] { ApplyConfiguredVolumesNow(); });
    afterFault.join();
    Check(writes == 9, "native exception releases audio and handoff boundaries");
    std::printf("audio configured apply: %d failures\n", failures);
    return failures ? 1 : 0;
}
