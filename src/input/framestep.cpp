#include "../../include/input/framestep.h"
#include "../../include/core/logger.h"
#include "../../include/game/efzrevival_addrs.h"
#include "../../include/game/game_state.h"
#include "../../include/game/game_thread_queue.h"
#include "../../include/game/practice_hotkey_gate.h"
#include "../../include/utils/pause_integration.h"
#include "../../include/core/memory.h"
#include "../../include/utils/network.h"
#include "../../include/utils/utilities.h"
#include "../../include/utils/config.h"
#include "../../include/utils/i18n.h"
#include "../../include/utils/debug_log.h"
#include "../../include/gui/overlay.h"
#include "../../include/gui/imgui_impl.h"
#include "../../include/runtime/native_game_profile.h"
#include <windows.h>
#include <atomic>
#include <sstream>

// External frame counter (incremented at 192fps in frame_monitor.cpp)
extern std::atomic<int> frameCounter;
// External window active state (from utilities.cpp)
extern std::atomic<bool> g_efzWindowActive;

namespace {
    std::atomic<bool> s_enabled{false};
    std::atomic<bool> s_paused{false};
    std::atomic<bool> s_stepRequested{false};
    std::atomic<uint32_t> s_stepCounter{0};
    std::atomic<bool> s_inFrameStep{false};
    std::atomic<bool> s_visualPatchesApplied{false};
    std::atomic<bool> s_wePaused{false};
    std::atomic<uintptr_t> s_lastBattleContext{0};
    
    std::atomic<int> s_targetFrame{0};
    std::atomic<int> s_stepStartFrame{0};
    std::atomic<Framestep::StepMode> s_stepMode{Framestep::StepMode::FullFrame};
    std::atomic<int> s_pendingVanillaSubsteps{0};
    std::atomic<bool> s_vanillaHookStepActive{false};
    std::atomic<bool> s_vanillaStepInBattleUpdate{false};
    std::atomic<uint8_t> s_vanillaStepSpeed{0};
    std::atomic<int> s_pendingRevivalNativeSteps{0};
    std::atomic<uint32_t> s_lastRevivalStepCounter{0};
    std::atomic<bool> s_haveRevivalStepCounter{false};
    std::atomic<bool> s_revivalHotkeyGateReady{false};
    std::atomic<DWORD> s_revivalStepQueuedTick{0};
    std::atomic<DWORD> s_nextHotkeyGateAttemptTick{0};
    std::atomic<bool> s_loggedHotkeyGateUnavailable{false};
    std::atomic<bool> s_menuWasVisible{false};
    std::atomic<bool> s_restorePausedAfterMenu{false};
    std::atomic<bool> s_restoreWePausedAfterMenu{false};

    void TraceFramestep(const std::string& message) {
        DebugLog::Write(message);
    }

    constexpr uintptr_t kBattleGamespeedOffset = 0x578; // one byte
    constexpr uintptr_t kBattleEnginePauseOffset = 0x588; // four bytes

    enum class Backend {
        Disabled,
        Vanilla,
        RevivalNativeFrame,
        Revival102fSubframe
    };

    void SnapshotPauseStateForMenu(Backend backend);
    void RestorePauseStateAfterMenu(Backend backend);

    bool SehSnapshotPauseStateForMenu(Backend backend) {
        __try {
            SnapshotPauseStateForMenu(backend);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool SehRestorePauseStateAfterMenu(Backend backend) {
        __try {
            RestorePauseStateAfterMenu(backend);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    Backend GetBackend() {
        HMODULE hRev = GetModuleHandleA("EfzRevival.dll");
        if (!hRev) {
            return Backend::Vanilla;
        }

        if (IsEfzRevival102fSubframeBuild()) {
            return Backend::Revival102fSubframe;
        }
        if (IsEfzRevivalVersionSupported()) {
            return Backend::RevivalNativeFrame;
        }
        return Backend::Disabled;
    }

    bool IsRevivalBackend(Backend backend) {
        return backend == Backend::RevivalNativeFrame
            || backend == Backend::Revival102fSubframe;
    }

    bool UsesRevivalNativeStep(Backend backend) {
        return backend == Backend::Revival102fSubframe;
    }

    bool UsesEngineStep(Backend backend) {
        return backend == Backend::Vanilla
            || backend == Backend::RevivalNativeFrame;
    }

    bool IsConfigEnabled() {
        return Config::GetSettings().framestepEnabled;
    }

    bool ShouldSuppressNativeRevivalFramestep() {
        return Config::GetSettings().suppressRevivalFramestep;
    }

    bool IsRuntimeEnabled() {
        return s_enabled.load() && IsConfigEnabled() && GetBackend() != Backend::Disabled;
    }

    const char* BackendName(Backend backend) {
        switch (backend) {
        case Backend::Vanilla: return "vanilla";
        case Backend::RevivalNativeFrame: return "Revival engine";
        case Backend::Revival102fSubframe: return "Revival 1.02f subframe";
        default: return "disabled";
        }
    }

    const char* DetectedRevivalName() {
        EfzRevivalVersion version = GetEfzRevivalVersion();
        if (version == EfzRevivalVersion::Revival102f) {
            return EfzRevivalDllFlavorName(GetEfzRevivalDllFlavor());
        }
        return EfzRevivalVersionName(version);
    }

    bool IsValidFramestepMode() {
        return GetCurrentGameMode() == GameMode::Practice;
    }

    void RemoveStatusMessage() {
        if (g_FramestepStatusId != -1) {
            DirectDrawHook::RemovePermanentMessage(g_FramestepStatusId);
            g_FramestepStatusId = -1;
        }
    }

    void ResetRevivalStepTracking() {
        s_pendingRevivalNativeSteps.store(0);
        s_inFrameStep.store(false);
        s_haveRevivalStepCounter.store(false);
        s_revivalStepQueuedTick.store(0);
    }

    void ResetVanillaStepTracking() {
        s_pendingVanillaSubsteps.store(0);
        s_vanillaHookStepActive.store(false);
        s_vanillaStepInBattleUpdate.store(false);
        s_vanillaStepSpeed.store(0);
        s_inFrameStep.store(false);
    }

    bool EnsureRevivalHotkeyGate() {
        if (s_revivalHotkeyGateReady.load()) {
            return true;
        }
        if (!IsRevivalBackend(GetBackend())) {
            return false;
        }

        const DWORD now = GetTickCount();
        const DWORD nextAttempt = s_nextHotkeyGateAttemptTick.load();
        if (nextAttempt != 0 && static_cast<LONG>(now - nextAttempt) < 0) {
            return false;
        }
        s_nextHotkeyGateAttemptTick.store(now + 1000);

        if (PracticeHotkeyGate::Install()) {
            s_revivalHotkeyGateReady.store(true);
            s_nextHotkeyGateAttemptTick.store(0);
            s_loggedHotkeyGateUnavailable.store(false);
            LogOut("[FRAMESTEP][REVIVAL] Practice hotkey gate ready", true);
            return true;
        }
        return false;
    }

    void LogHotkeyGateUnavailableOnce(const char* keyName) {
        if (s_loggedHotkeyGateUnavailable.exchange(true)) {
            return;
        }

        std::ostringstream oss;
        oss << "[FRAMESTEP][REVIVAL] Native " << keyName
            << " key overlaps our framestep key, but the hotkey gate is unavailable; using our framestep anyway";
        LogOut(oss.str(), true);
    }

    bool ApplyVisualPatches(bool freeze) {
        return PauseIntegration::SetVisualFreeze(PauseIntegration::VisualFreezeUser::Framestep, freeze);
    }

    bool ReadBattleGamespeed(uintptr_t battleContext, uint8_t& outValue) {
        if (!battleContext) return false;
        return SafeReadMemory(battleContext + kBattleGamespeedOffset, &outValue, sizeof(outValue));
    }

    bool WriteBattleGamespeed(uintptr_t battleContext, uint8_t value) {
        if (!battleContext) return false;
        return SafeWriteMemory(battleContext + kBattleGamespeedOffset, &value, sizeof(value));
    }

    uintptr_t GetGamespeedAddress() {
        Practice::PatchModule image;
        if (!Practice::GetQualifiedMemorialImage(image)) return 0;
        uint8_t screen = 0;
        uintptr_t battle = 0;
        if (!SafeReadMemory(image.base + 0x390148, &screen, sizeof(screen)) || screen != 3 ||
            !SafeReadMemory(image.base + 0x39011c, &battle, sizeof(battle)) || !battle) return 0;
        const uintptr_t captured = s_lastBattleContext.load();
        if (captured && captured != battle) return 0;
        uint8_t speed = 0xff;
        return ReadBattleGamespeed(battle, speed) && speed <= 3
            ? battle + kBattleGamespeedOffset : 0;
    }

    // Set gamespeed (0 = paused, 3 = normal)
    bool SetGamespeed(uint8_t value) {
        uintptr_t addr = GetGamespeedAddress();
        if (!addr) return false;

        bool ok = SafeWriteMemory(addr, &value, sizeof(value));
        std::ostringstream oss;
        oss << "[FRAMESTEP] SetGamespeed(" << (int)value << ") at 0x" << std::hex << addr 
            << (ok ? " OK" : " FAIL");
        LogOut(oss.str(), true);
        return ok;
    }

    bool HasActiveFramestepState() {
        return s_stepRequested.load()
            || s_paused.load()
            || s_inFrameStep.load()
            || s_visualPatchesApplied.load()
            || s_wePaused.load()
            || s_pendingRevivalNativeSteps.load() > 0
            || g_FramestepStatusId != -1;
    }

    bool OwnsPauseStateForRestore(Backend backend) {
        if (UsesRevivalNativeStep(backend)) {
            return s_wePaused.load()
                || s_inFrameStep.load()
                || s_stepRequested.load()
                || s_pendingRevivalNativeSteps.load() > 0;
        }

        return s_paused.load()
            || s_inFrameStep.load()
            || s_stepRequested.load()
            || s_visualPatchesApplied.load()
            || s_vanillaHookStepActive.load()
            || s_pendingVanillaSubsteps.load() > 0;
    }

    void ClearActiveFramestepState(Backend backend, const char* reason, bool forceNativeUnpause = false) {
        const bool hadState = HasActiveFramestepState();

        s_stepRequested.store(false);
        s_menuWasVisible.store(false);
        s_restorePausedAfterMenu.store(false);
        s_restoreWePausedAfterMenu.store(false);

        if (UsesRevivalNativeStep(backend)) {
            if ((forceNativeUnpause || s_wePaused.load()) && PauseIntegration::IsPracticePaused()) {
                PauseIntegration::SetPracticePausedForFramestep(false);
            }
            s_paused.store(false);
            s_wePaused.store(false);
            ResetRevivalStepTracking();
        } else {
            if (s_paused.load() || s_inFrameStep.load() || s_visualPatchesApplied.load()) {
                if (s_visualPatchesApplied.load()) {
                    ApplyVisualPatches(false);
                    s_visualPatchesApplied.store(false);
                }
                SetGamespeed(3);
            }
            s_paused.store(false);
            ResetVanillaStepTracking();
        }

        RemoveStatusMessage();
        if (hadState && detailedLogging.load() && reason) {
            std::ostringstream oss;
            oss << "[FRAMESTEP] Cleared active state: " << reason;
            LogOut(oss.str(), true);
        }
    }

    void AdvanceOneFrame() {
        int subframes = static_cast<int>(s_stepMode.load());
        int currentFrame = frameCounter.load();
        s_stepStartFrame.store(currentFrame);
        s_targetFrame.store(currentFrame + subframes);

        if (s_visualPatchesApplied.load()) {
            ApplyVisualPatches(false);
            s_visualPatchesApplied.store(false);
        }
        uint8_t speed = static_cast<uint8_t>(subframes);
        if (speed > 3) speed = 3;
        if (speed == 0) speed = 1;
        SetGamespeed(speed);

        std::stringstream oss;
        oss << "[FRAMESTEP] Advancing " << subframes << " subframe(s) from frame " 
            << currentFrame << " to " << s_targetFrame.load();
        LogOut(oss.str(), true);
    }

    bool IsFrameAdvanceComplete() {
        if (!s_inFrameStep.load()) return true;
        
        int current = frameCounter.load();
        int target = s_targetFrame.load();
        
        if (current >= target) {
            SetGamespeed(0);
            if (ApplyVisualPatches(true)) {
                s_visualPatchesApplied.store(true);
            }

            std::stringstream oss;
            oss << "[FRAMESTEP] Frame advance complete at frame " << current 
                << " (advanced " << (current - s_stepStartFrame.load()) << " subframes)";
            LogOut(oss.str(), true);

            s_inFrameStep.store(false);
            return true;
        }
        
        return false;
    }

    bool BeginEngineHookStep() {
        const int subframes = Framestep::GetSubframesPerStep();
        if (subframes <= 0) {
            return false;
        }

        if (s_inFrameStep.exchange(true)) {
            return false;
        }

        s_stepRequested.store(false);
        s_stepStartFrame.store(frameCounter.load());
        s_pendingVanillaSubsteps.store(subframes);
        s_vanillaHookStepActive.store(true);
        s_vanillaStepInBattleUpdate.store(false);
        s_vanillaStepSpeed.store(0);

        std::ostringstream oss;
        oss << "[FRAMESTEP][ENGINE] Queued " << subframes
            << " subframe(s) for next battle update";
        LogOut(oss.str(), true);
        return true;
    }

    void OnBattleUpdateBefore(void* battleContext) {
        const uintptr_t battle = reinterpret_cast<uintptr_t>(battleContext);
        s_lastBattleContext.store(battle);

        if (!s_vanillaHookStepActive.load()
            || !s_inFrameStep.load()
            || !s_paused.load()
            || !UsesEngineStep(GetBackend())
            || GetCurrentGameMode() != GameMode::Practice
            || IsNetplaySuspendActive()) {
            return;
        }

        int pending = s_pendingVanillaSubsteps.load();
        if (pending <= 0) {
            return;
        }

        uint8_t speed = static_cast<uint8_t>(pending);
        if (speed > 3) speed = 3;
        if (speed == 0) speed = 1;

        if (s_visualPatchesApplied.load()) {
            ApplyVisualPatches(false);
            s_visualPatchesApplied.store(false);
        }

        uint32_t unpaused = 0;
        SafeWriteMemory(battle + kBattleEnginePauseOffset, &unpaused, sizeof(unpaused));

        if (WriteBattleGamespeed(battle, speed)) {
            s_vanillaStepSpeed.store(speed);
            s_vanillaStepInBattleUpdate.store(true);
        }
    }

    void OnBattleUpdateAfter(void* battleContext) {
        const uintptr_t battle = reinterpret_cast<uintptr_t>(battleContext);
        s_lastBattleContext.store(battle);

        if (!s_vanillaStepInBattleUpdate.exchange(false)) {
            return;
        }

        const uint8_t speed = s_vanillaStepSpeed.exchange(0);
        WriteBattleGamespeed(battle, 0);

        if (s_paused.load()) {
            if (ApplyVisualPatches(true)) {
                s_visualPatchesApplied.store(true);
            }
        }

        s_pendingVanillaSubsteps.store(0);
        s_vanillaHookStepActive.store(false);
        s_inFrameStep.store(false);
        s_stepCounter.fetch_add(1);

        std::ostringstream oss;
        oss << "[FRAMESTEP][ENGINE] Battle update consumed "
            << static_cast<int>(speed) << " subframe(s) (step "
            << s_stepCounter.load() << ")";
        LogOut(oss.str(), true);
    }

    bool ReadRevivalPracticeHotkey(uintptr_t offset, int& outKey) {
        outKey = 0;
        if (!offset || !IsRevivalBackend(GetBackend())) {
            return false;
        }

        PauseIntegration::EnsurePracticePointerCapture();
        void* practice = PauseIntegration::ResolvePracticeControllerPtrNow(false, true, "framestep hotkey");
        if (!practice) {
            practice = PauseIntegration::GetPracticeControllerPtr();
        }
        if (!practice) {
            return false;
        }

        uint32_t key = 0;
        if (!SafeReadMemory(reinterpret_cast<uintptr_t>(practice) + offset, &key, sizeof(key))) {
            return false;
        }
        outKey = static_cast<int>(key);
        return key != 0;
    }

    bool ConfiguredKeyMatchesRevivalNative(bool stepKey) {
        const Config::Settings cfg = Config::GetSettings();
        const int configured = stepKey
            ? (cfg.framestepStepKey > 0 ? cfg.framestepStepKey : 'P')
            : (cfg.framestepPauseKey > 0 ? cfg.framestepPauseKey : VK_SPACE);

        int native = 0;
        const uintptr_t offset = stepKey
            ? EFZ_Practice_StepHotkeyOffset()
            : EFZ_Practice_PauseHotkeyOffset();
        if (ReadRevivalPracticeHotkey(offset, native)) {
            return configured == native;
        }

        // Before the Practice pointer is captured, avoid a double-toggle on the stock Revival bindings.
        const int defaultKey = stepKey ? 'P' : VK_SPACE;
        return configured == defaultKey;
    }

    void ContinueRevivalStepOnGameThread(uintptr_t) {
        if (!s_inFrameStep.load()) return;  // canceled while queued
        if (!PauseIntegration::RequestPracticeSubframeStep()) {
            s_pendingRevivalNativeSteps.store(0);
            s_inFrameStep.store(false);
            s_revivalStepQueuedTick.store(0);
        }
    }

    void HandleRevivalStepDelta(uint32_t delta) {
        if (delta == 0) {
            return;
        }

        int observed = static_cast<int>(delta);
        if (observed < 0 || observed > 16) {
            observed = 1;
        }

        if (s_inFrameStep.load()) {
            int remaining = s_pendingRevivalNativeSteps.load();
            if (observed > 1) {
                remaining -= (observed - 1);
                if (remaining < 0) {
                    remaining = 0;
                }
            }

            if (remaining > 0) {
                s_pendingRevivalNativeSteps.store(remaining - 1);
                s_revivalStepQueuedTick.store(GetTickCount());
                if (!GameThread::Post(&ContinueRevivalStepOnGameThread)) {
                    s_pendingRevivalNativeSteps.store(0);
                    s_inFrameStep.store(false);
                    s_revivalStepQueuedTick.store(0);
                }
            } else {
                s_pendingRevivalNativeSteps.store(0);
                s_inFrameStep.store(false);
                s_revivalStepQueuedTick.store(0);
            }
            return;
        }

        // Native Revival steps can still happen if suppression is disabled.
        s_stepCounter.fetch_add(static_cast<uint32_t>(observed));
    }

    void OnPracticeStepAdvanced(uint32_t beforeCounter, uint32_t afterCounter) {
        if (!IsRuntimeEnabled() || !UsesRevivalNativeStep(GetBackend())) {
            return;
        }

        if (afterCounter < beforeCounter) {
            ResetRevivalStepTracking();
            s_haveRevivalStepCounter.store(true);
            s_lastRevivalStepCounter.store(afterCounter);
            return;
        }

        const uint32_t delta = afterCounter - beforeCounter;
        s_haveRevivalStepCounter.store(true);
        s_lastRevivalStepCounter.store(afterCounter);
        HandleRevivalStepDelta(delta);
    }

    bool RefreshRevivalStepCounterNow() {
        uint32_t currentCounter = 0;
        if (!PauseIntegration::ReadStepCounter(currentCounter)) {
            return false;
        }

        if (!s_haveRevivalStepCounter.exchange(true)) {
            s_lastRevivalStepCounter.store(currentCounter);
            return false;
        }

        const uint32_t lastCounter = s_lastRevivalStepCounter.load();
        if (currentCounter == lastCounter) {
            return false;
        }

        s_lastRevivalStepCounter.store(currentCounter);
        if (currentCounter < lastCounter) {
            s_pendingRevivalNativeSteps.store(0);
            s_inFrameStep.store(false);
            s_revivalStepQueuedTick.store(0);
            return true;
        }

        HandleRevivalStepDelta(currentCounter - lastCounter);
        return true;
    }

    bool ClearStaleRevivalInFlightIfNeeded() {
        RefreshRevivalStepCounterNow();
        if (!s_inFrameStep.load()) {
            return true;
        }

        const DWORD queuedTick = s_revivalStepQueuedTick.load();
        const DWORD now = GetTickCount();
        if (queuedTick == 0 || static_cast<LONG>(now - queuedTick) < 250) {
            return false;
        }

        uint32_t currentCounter = 0;
        if (PauseIntegration::ReadStepCounter(currentCounter)) {
            s_haveRevivalStepCounter.store(true);
            s_lastRevivalStepCounter.store(currentCounter);
        }

        s_pendingRevivalNativeSteps.store(0);
        s_inFrameStep.store(false);
        s_revivalStepQueuedTick.store(0);
        LogOut("[FRAMESTEP][REVIVAL] Cleared stale pending native step", true);
        return true;
    }

    bool QueueRevivalNativeSteps(int steps) {
        if (steps <= 0) {
            return false;
        }

        // Native Revival has a single step-request bit.
        if (s_inFrameStep.load() && !ClearStaleRevivalInFlightIfNeeded()) {
            return false;
        }
        if (s_inFrameStep.exchange(true)) {
            return false;
        }

        uint32_t currentCounter = 0;
        if (PauseIntegration::ReadStepCounter(currentCounter)) {
            s_lastRevivalStepCounter.store(currentCounter);
            s_haveRevivalStepCounter.store(true);
        }

        s_stepCounter.fetch_add(1);
        s_pendingRevivalNativeSteps.store(steps - 1);
        if (PauseIntegration::RequestPracticeSubframeStep()) {
            s_inFrameStep.store(true);
            s_revivalStepQueuedTick.store(GetTickCount());
            return true;
        }

        s_pendingRevivalNativeSteps.store(0);
        if (s_stepCounter.load() > 0) {
            s_stepCounter.fetch_sub(1);
        }
        s_inFrameStep.store(false);
        s_revivalStepQueuedTick.store(0);
        return false;
    }

    int GetRevivalNativeStepsPerRequest(Backend backend) {
        if (backend == Backend::Revival102fSubframe) {
            return Framestep::GetSubframesPerStep();
        }
        return 0;
    }

    const char* RevivalNativeStepUnit(Backend backend) {
        return UsesRevivalNativeStep(backend) ? "subframe(s)" : "visual frame(s)";
    }

    void SnapshotPauseStateForMenu(Backend backend) {
        const bool paused = UsesRevivalNativeStep(backend)
            ? PauseIntegration::IsPracticePaused()
            : s_paused.load();

        s_restorePausedAfterMenu.store(paused);
        s_restoreWePausedAfterMenu.store(UsesRevivalNativeStep(backend) && s_wePaused.load());
        if (UsesRevivalNativeStep(backend)) {
            s_paused.store(paused);
        }

        std::ostringstream oss;
        oss << "[FRAMESTEP][TRACE] SnapshotPauseStateForMenu backend=" << BackendName(backend)
            << " paused=" << (paused ? 1 : 0)
            << " wePaused=" << (s_wePaused.load() ? 1 : 0);
        TraceFramestep(oss.str());
    }

    void RestorePauseStateAfterMenu(Backend backend) {
        const bool shouldRestore = s_restorePausedAfterMenu.exchange(false);
        const bool restoreOwnedPause = s_restoreWePausedAfterMenu.exchange(false);
        std::ostringstream begin;
        begin << "[FRAMESTEP][TRACE] RestorePauseStateAfterMenu backend=" << BackendName(backend)
              << " shouldRestore=" << (shouldRestore ? 1 : 0)
              << " restoreOwnedPause=" << (restoreOwnedPause ? 1 : 0);
        TraceFramestep(begin.str());
        if (!shouldRestore) {
            return;
        }

        if (UsesRevivalNativeStep(backend)) {
            const bool pausedNow = PauseIntegration::IsPracticePaused();
            std::ostringstream revival;
            revival << "[FRAMESTEP][TRACE] RestorePauseStateAfterMenu revival pausedNow=" << (pausedNow ? 1 : 0);
            TraceFramestep(revival.str());
            if (!pausedNow) {
                if (!PauseIntegration::SetPracticePausedForFramestep(true)) {
                    TraceFramestep("[FRAMESTEP][TRACE] RestorePauseStateAfterMenu revival failed to reassert practice pause");
                    return;
                }
            }

            s_paused.store(true);
            s_wePaused.store(restoreOwnedPause);
            ResetRevivalStepTracking();
            LogOut("[FRAMESTEP][REVIVAL] Restored paused state after menu close", true);
            return;
        }

        s_paused.store(true);
        const bool gamespeedOk = SetGamespeed(0);
        bool visualOk = s_visualPatchesApplied.load();
        if (!s_visualPatchesApplied.load() && ApplyVisualPatches(true)) {
            s_visualPatchesApplied.store(true);
            visualOk = true;
        }
        s_stepRequested.store(false);
        std::ostringstream vanilla;
        vanilla << "[FRAMESTEP][TRACE] RestorePauseStateAfterMenu vanilla gamespeedOk=" << (gamespeedOk ? 1 : 0)
                << " visualOk=" << (visualOk ? 1 : 0);
        TraceFramestep(vanilla.str());
        LogOut("[FRAMESTEP] Restored paused state after menu close", true);
    }

    void RestorePauseStateAfterMenuOnGameThread(uintptr_t backend) {
        if (ImGuiImpl::IsVisible()) return;  // reopened while queued; the next close re-posts
        if (!SehRestorePauseStateAfterMenu(static_cast<Backend>(backend))) {
            LogOut("[FRAMESTEP][SEH] Exception while restoring pause state after menu close", true);
            TraceFramestep("[FRAMESTEP][TRACE] Menu restore crashed under SEH");
            s_restorePausedAfterMenu.store(false);
            s_restoreWePausedAfterMenu.store(false);
        }
    }

    void SyncRevivalFramestep() {
        PauseIntegration::EnsurePracticePointerCapture();
        const bool paused = PauseIntegration::IsPracticePaused();
        s_paused.store(paused);

        if (!paused) {
            ResetRevivalStepTracking();
            s_wePaused.store(false);
            return;
        }

        uint32_t currentCounter = 0;
        if (!PauseIntegration::ReadStepCounter(currentCounter)) {
            return;
        }

        if (!s_haveRevivalStepCounter.exchange(true)) {
            s_lastRevivalStepCounter.store(currentCounter);
            return;
        }

        const uint32_t lastCounter = s_lastRevivalStepCounter.load();
        if (currentCounter == lastCounter) {
            return;
        }
        if (currentCounter < lastCounter) {
            s_lastRevivalStepCounter.store(currentCounter);
            ResetRevivalStepTracking();
            s_haveRevivalStepCounter.store(true);
            return;
        }
        s_lastRevivalStepCounter.store(currentCounter);

        uint32_t delta = currentCounter - lastCounter;
        if (delta == 0) {
            delta = 1;
        }

        HandleRevivalStepDelta(delta);
    }
}

namespace Framestep {
    void Initialize() {
        s_enabled.store(true);
        SetBattleUpdateCallbacks(&OnBattleUpdateBefore, &OnBattleUpdateAfter);
        EnsureFrontendControlHooksInstalled();
        PauseIntegration::SetPracticeStepAdvanceCallback(&OnPracticeStepAdvanced);
        Backend backend = GetBackend();
        std::ostringstream oss;
        oss << "[FRAMESTEP] Initialized; backend=" << BackendName(backend);
        if (IsRevivalBackend(backend)) {
            oss << " version=" << DetectedRevivalName();
        }
        if (backend == Backend::Disabled && GetModuleHandleA("EfzRevival.dll")) {
            oss << " (unsupported Revival framestep)";
        }
        LogOut(oss.str(), true);
    }

    void Update() {
        if (!s_enabled.load()) {
            RemoveStatusMessage();
            return;
        }

        Backend backend = GetBackend();

        if (!IsConfigEnabled()) {
            ClearActiveFramestepState(backend, "disabled by config");
            return;
        }

        if (backend == Backend::Disabled) {
            ClearActiveFramestepState(backend, "backend unavailable");
            return;
        }

        if (!g_efzWindowActive.load()) {
            return;
        }

        if (!IsValidFramestepMode()) {
            if (UsesRevivalNativeStep(backend)) {
                // Revival's pause was released on the game thread at battle exit.
                s_wePaused.store(false);
                s_paused.store(false);
                ResetRevivalStepTracking();
            } else if (s_paused.load()) {
                s_paused.store(false);
                if (s_visualPatchesApplied.load()) {
                    ApplyVisualPatches(false);
                    s_visualPatchesApplied.store(false);
                }
                SetGamespeed(3);
            }
            return;
        }

        if (IsNetplaySuspendActive()) {
            return;
        }

        if (IsRevivalBackend(backend) && ShouldSuppressNativeRevivalFramestep()) {
            EnsureRevivalHotkeyGate();
        }

        const bool menuVisible = ImGuiImpl::IsVisible();
        if (menuVisible) {
            if (!s_menuWasVisible.exchange(true)) {
                std::ostringstream oss;
                oss << "[FRAMESTEP][TRACE] Menu became visible backend=" << BackendName(backend)
                    << " paused=" << (s_paused.load() ? 1 : 0)
                    << " wePaused=" << (s_wePaused.load() ? 1 : 0);
                TraceFramestep(oss.str());
                if (!SehSnapshotPauseStateForMenu(backend)) {
                    LogOut("[FRAMESTEP][SEH] Exception while snapshotting pause state for menu", true);
                    TraceFramestep("[FRAMESTEP][TRACE] Menu snapshot crashed under SEH");
                }
            }
            return;
        }
        if (s_menuWasVisible.exchange(false)) {
            std::ostringstream oss;
            oss << "[FRAMESTEP][TRACE] Menu became hidden backend=" << BackendName(backend)
                << " restorePaused=" << (s_restorePausedAfterMenu.load() ? 1 : 0)
                << " restoreWePaused=" << (s_restoreWePausedAfterMenu.load() ? 1 : 0);
            TraceFramestep(oss.str());
            if (!GameThread::Post(&RestorePauseStateAfterMenuOnGameThread, static_cast<uintptr_t>(backend))) {
                TraceFramestep("[FRAMESTEP][TRACE] Menu restore refused: game-thread queue unavailable");
                s_restorePausedAfterMenu.store(false);
                s_restoreWePausedAfterMenu.store(false);
            }
        }

        if (UsesRevivalNativeStep(backend)) {
            if (ShouldSuppressNativeRevivalFramestep()) {
                EnsureRevivalHotkeyGate();
            }
            SyncRevivalFramestep();
            return;
        }

        if (s_inFrameStep.load()) {
            if (!s_vanillaHookStepActive.load()) {
                IsFrameAdvanceComplete();
            }
            return;
        }

        if (s_stepRequested.load() && s_paused.load()) {
            if (EnsureFrontendControlHooksInstalled() && BeginEngineHookStep()) {
                return;
            }

            s_inFrameStep.store(true);
            s_stepCounter++;

            std::ostringstream oss;
            oss << "[FRAMESTEP] Frame step requested (fallback step " << s_stepCounter.load() << ")";
            LogOut(oss.str(), true);

            AdvanceOneFrame();
            s_stepRequested.store(false);
        }
    }

    bool IsPaused() {
        if (!IsRuntimeEnabled()) return false;
        if (UsesRevivalNativeStep(GetBackend())) {
            if (ShouldSuppressNativeRevivalFramestep()) {
                EnsureRevivalHotkeyGate();
            }
            return PauseIntegration::IsPracticePaused();
        }
        return s_paused.load();
    }

    unsigned int GetStepCounter() {
        return s_stepCounter.load();
    }

    void ResetStepCounter() {
        s_stepCounter.store(0);
        if (UsesRevivalNativeStep(GetBackend())) {
            ResetRevivalStepTracking();
        } else {
            ResetVanillaStepTracking();
        }
        LogOut("[FRAMESTEP] Step counter reset", true);
    }

    void CancelActiveState(const char* reason) {
        ClearActiveFramestepState(GetBackend(), reason ? reason : "external reset");
    }

    bool OwnsPauseState() {
        Backend backend = GetBackend();
        return OwnsPauseStateForRestore(backend);
    }

    void FinishSavestateRestore(bool restoreStartedFromOwnedPause) {
        ClearActiveFramestepState(GetBackend(),
                                  "savestate restore complete",
                                  restoreStartedFromOwnedPause);
    }

    void TogglePause() {
        if (!IsRuntimeEnabled()) return;

        GameMode mode = GetCurrentGameMode();
        if (mode != GameMode::Practice) return;

        Backend backend = GetBackend();
        if (UsesRevivalNativeStep(backend)) {
            if (ShouldSuppressNativeRevivalFramestep()
                && ConfiguredKeyMatchesRevivalNative(false)
                && !EnsureRevivalHotkeyGate()) {
                LogHotkeyGateUnavailableOnce("pause");
            }

            const bool newState = !PauseIntegration::IsPracticePaused();
            if (PauseIntegration::SetPracticePausedForFramestep(newState)) {
                s_paused.store(newState);
                s_wePaused.store(newState);
                ResetRevivalStepTracking();
                if (newState) {
                    ResetStepCounter();
                    LogOut("[FRAMESTEP][REVIVAL] Paused", true);
                } else {
                    LogOut("[FRAMESTEP][REVIVAL] Unpaused", true);
                }
            }
            return;
        }

        if (IsRevivalBackend(backend)
            && ShouldSuppressNativeRevivalFramestep()
            && ConfiguredKeyMatchesRevivalNative(false)
            && !EnsureRevivalHotkeyGate()) {
            LogHotkeyGateUnavailableOnce("pause");
        }

        bool newState = !s_paused.load();
        s_paused.store(newState);

        if (newState) {
            SetGamespeed(0);
            if (ApplyVisualPatches(true)) {
                s_visualPatchesApplied.store(true);
            }
            ResetStepCounter();
            LogOut(backend == Backend::RevivalNativeFrame ? "[FRAMESTEP][REVIVAL] Engine-paused" : "[FRAMESTEP] Paused", true);
        } else {
            ResetVanillaStepTracking();
            if (s_visualPatchesApplied.load()) {
                ApplyVisualPatches(false);
                s_visualPatchesApplied.store(false);
            }
            SetGamespeed(3);
            LogOut(backend == Backend::RevivalNativeFrame ? "[FRAMESTEP][REVIVAL] Engine-unpaused" : "[FRAMESTEP] Unpaused", true);
        }

        s_stepRequested.store(false);
    }

    void RequestFrameStep() {
        if (!IsRuntimeEnabled()) return;

        GameMode mode = GetCurrentGameMode();
        if (mode != GameMode::Practice) return;

        Backend backend = GetBackend();
        if (UsesRevivalNativeStep(backend)) {
            if (ShouldSuppressNativeRevivalFramestep()
                && ConfiguredKeyMatchesRevivalNative(true)
                && !EnsureRevivalHotkeyGate()) {
                LogHotkeyGateUnavailableOnce("step");
            }

            if (!PauseIntegration::IsPracticePaused()) {
                if (!PauseIntegration::SetPracticePausedForFramestep(true)) {
                    return;
                }
                s_paused.store(true);
                s_wePaused.store(true);
            }

            const int nativeSteps = GetRevivalNativeStepsPerRequest(backend);
            if (QueueRevivalNativeSteps(nativeSteps)) {
                std::ostringstream oss;
                oss << "[FRAMESTEP][REVIVAL] Queued " << nativeSteps << " "
                    << RevivalNativeStepUnit(backend);
                LogOut(oss.str(), true);
            }
            return;
        }

        if (IsRevivalBackend(backend)
            && ShouldSuppressNativeRevivalFramestep()
            && ConfiguredKeyMatchesRevivalNative(true)
            && !EnsureRevivalHotkeyGate()) {
            LogHotkeyGateUnavailableOnce("step");
        }

        if (!s_paused.load()) {
            TogglePause();
        }

        s_stepRequested.store(true);
        LogOut("[FRAMESTEP] Frame step requested", true);
    }

    bool IsEnabled() {
        return IsRuntimeEnabled();
    }

    bool ShouldSuppressRevivalHotkey(void* practiceController, int key) {
        if (!practiceController || key == 0) return false;
        if (!IsRuntimeEnabled() || !IsRevivalBackend(GetBackend())) return false;
        if (!ShouldSuppressNativeRevivalFramestep()) return false;
        if (GetCurrentGameMode() != GameMode::Practice || IsNetplaySuspendActive()) return false;

        uint32_t pauseKey = 0;
        uint32_t stepKey = 0;
        const uintptr_t base = reinterpret_cast<uintptr_t>(practiceController);
        const uintptr_t pauseOff = EFZ_Practice_PauseHotkeyOffset();
        const uintptr_t stepOff = EFZ_Practice_StepHotkeyOffset();
        if (pauseOff) {
            (void)SafeReadMemory(base + pauseOff, &pauseKey, sizeof(pauseKey));
        }
        if (stepOff) {
            (void)SafeReadMemory(base + stepOff, &stepKey, sizeof(stepKey));
        }

        const bool suppress = (pauseKey != 0 && key == static_cast<int>(pauseKey))
            || (stepKey != 0 && key == static_cast<int>(stepKey));
        if (suppress && detailedLogging.load()) {
            LogOut("[FRAMESTEP][REVIVAL] Suppressed native Revival pause/step hotkey", true);
        }
        return suppress;
    }

    void UpdateOverlayStatus() {
        if (!IsRuntimeEnabled()) {
            RemoveStatusMessage();
            return;
        }

        GameMode mode = GetCurrentGameMode();
        if (mode != GameMode::Practice) {
            RemoveStatusMessage();
            return;
        }

        const bool paused = IsPaused();
        s_paused.store(paused);

        if (!paused) {
            RemoveStatusMessage();
            return;
        }

        const Config::Settings cfg = Config::GetSettings();
        const int pauseKey = (cfg.framestepPauseKey > 0) ? cfg.framestepPauseKey : VK_SPACE;
        const int stepKey = (cfg.framestepStepKey > 0) ? cfg.framestepStepKey : 'P';
        const std::string pauseName = GetKeyName(pauseKey);
        const std::string stepName = GetKeyName(stepKey);
        const uint32_t steps = s_stepCounter.load();
        std::string text;
        COLORREF color = RGB(255, 100, 100);

        if (steps == 0) {
            text = I18n::TrFormat("Paused [%s=Resume %s=Step]", pauseName.c_str(), stepName.c_str());
        } else if (s_stepMode.load() == StepMode::Subframe) {
            const float visualFrames = steps / 3.0f;
            text = I18n::TrFormat("Paused [%s=Resume %s=Step] (%d = %.2ff)",
                                  pauseName.c_str(), stepName.c_str(), (int)steps, visualFrames);
        } else {
            text = I18n::TrFormat("Paused [%s=Resume %s=Step] (%d)", pauseName.c_str(), stepName.c_str(), (int)steps);
        }

        // Position below Revival's built-in top-right practice text block.
        const int x = 540;
        const int y = 145;

        if (g_FramestepStatusId == -1) {
            g_FramestepStatusId = DirectDrawHook::AddPermanentMessage(text, color, x, y);
        } else {
            DirectDrawHook::RemovePermanentMessage(g_FramestepStatusId);
            g_FramestepStatusId = DirectDrawHook::AddPermanentMessage(text, color, x, y);
        }
    }

    StepMode GetStepMode() {
        return s_stepMode.load();
    }

    void SetStepMode(StepMode mode) {
        s_stepMode.store(mode);
        std::string modeStr = (mode == StepMode::Subframe) ? "Subframe (1 step = 1 subframe)" : "Full Frame (1 step = 1 visual frame)";
        LogOut("[FRAMESTEP] Step mode changed to: " + modeStr, true);
    }

    int GetSubframesPerStep() {
        return s_stepMode.load() == StepMode::Subframe ? 1 : 3;
    }
}
