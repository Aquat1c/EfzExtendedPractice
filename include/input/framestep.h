#pragma once

namespace Framestep {
    // Framestep mode: how many subframes to advance per step
    enum class StepMode {
        Subframe = 1,      // Advance 1 subframe
        FullFrame = 3      // Advance 3 subframes (64fps visual frame)
    };

    // Initialize framestep system (call once at startup)
    void Initialize();

    // Update framestep state (call every frame)
    void Update();

    // Check if Practice is currently paused via framestep/Revival
    bool IsPaused();

    // Get the current step counter value
    unsigned int GetStepCounter();

    // Reset step counter to zero
    void ResetStepCounter();

    // Clear any active pause/step state without toggling through the hotkeys.
    void CancelActiveState(const char* reason = nullptr);

    // True when the current pause/step state is owned by our framestep logic,
    // not merely observed from Revival's native pause flag.
    bool OwnsPauseState();

    // Vanilla/Revival-engine backends only: the pause is gamespeed 0 plus the
    // shared visual NOPs, with Practice +0xB4 clear.
    bool HoldsEnginePause();

    // Final restore-side cleanup. The bool must be captured before the restore
    // write, because restoring game memory can reintroduce stale pause metadata.
    void FinishSavestateRestore(bool restoreStartedFromOwnedPause);

    // Handle pause toggle (Space key)
    void TogglePause();

    // Handle frame step request (P key)
    void RequestFrameStep();

    // Check if framestep is enabled for the active backend
    bool IsEnabled();

    // Used by the EfzRevival Practice hotkey hook to keep native pause/step
    // from racing our own framestep implementation in Practice mode.
    bool ShouldSuppressRevivalHotkey(void* practiceController, int key);

    // Update overlay display status
    void UpdateOverlayStatus();

    // Get/set step mode
    StepMode GetStepMode();
    void SetStepMode(StepMode mode);

    // Get subframes per step (1 for subframe, 3 for full frame)
    int GetSubframesPerStep();
}
