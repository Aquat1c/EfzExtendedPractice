#pragma once

namespace Framestep {
    enum class StepMode {
        Subframe = 1,
        FullFrame = 3      // 3 subframes = one 64 Hz visual frame
    };

    void Initialize();

    void Update();

    bool IsPaused();

    unsigned int GetStepCounter();

    void ResetStepCounter();

    void CancelActiveState(const char* reason = nullptr);

    // True only when our framestep logic owns the pause, not when observing Revival's native flag.
    bool OwnsPauseState();

    // Vanilla/Revival-engine backends only: the pause is gamespeed 0 plus the
    // shared visual NOPs, with Practice +0xB4 clear.
    bool HoldsEnginePause();

    // Final restore-side cleanup. The bool must be captured before the restore
    // write, because restoring game memory can reintroduce stale pause metadata.
    void FinishSavestateRestore(bool restoreStartedFromOwnedPause);

    void TogglePause();

    void RequestFrameStep();

    bool IsEnabled();

    bool ShouldSuppressRevivalHotkey(void* practiceController, int key);

    void UpdateOverlayStatus();

    StepMode GetStepMode();
    void SetStepMode(StepMode mode);

    int GetSubframesPerStep();
}
