#pragma once
#include <stdint.h>

// Mirrors EfzRevival Practice pause for our menu surfaces: freeze via Revival's
// patch toggler on open; unfreeze on close only if we froze it.

namespace PauseIntegration {
    using StepAdvanceCallback = void (*)(uint32_t beforeCounter, uint32_t afterCounter);

    // Surfaces share one physical Practice pause: the first visible surface acquires it, the last one releases it.
    enum class MenuSurface : uint8_t {
        ImGui = 1u << 0,
        MissionPause = 1u << 1,
        TutorialPage = 1u << 2,
        RecorderHandoff = 1u << 3,
        RecorderCommandHandoff = 1u << 4,
        // Freezes both fighters and world entities while a mission baseline is restored and tick zero is published to the native input lane.
        DemoTransition = 1u << 5,
    };

    constexpr uint8_t MenuSurfaceBit(MenuSurface surface) {
        return static_cast<uint8_t>(surface);
    }

    constexpr uint8_t UpdatedMenuSurfaceMask(uint8_t current,
                                             MenuSurface surface,
                                             bool visible) {
        const uint8_t bit = MenuSurfaceBit(surface);
        return visible ? static_cast<uint8_t>(current | bit)
                       : static_cast<uint8_t>(current & ~bit);
    }

    void OnMenuSurfaceVisibilityChanged(MenuSurface surface, bool visible);
    // Briefly releases only the physical Practice pause for bounded render-state refreshes; exclusive, tokenized, cancelled by any surface ownership change.
    bool BeginMenuSurfaceRefresh(MenuSurface surface, uint64_t& tokenOut);
    bool EndMenuSurfaceRefresh(MenuSurface surface, uint64_t token);
    // Re-acquire the aggregate physical pause when an owned surface remains visible but a game-state transition recreated/cleared Practice pause.
    void ReassertMenuSurfacePause(MenuSurface surface);
    // Emergency/session teardown only; normal UI releases only its own surface.
    void ForceCloseAllMenuSurfaces();

    // Legacy ImGui entry point, equivalent to the ImGui surface above.
    void OnMenuVisibilityChanged(bool visible);
    // No-op if already installed.
    void EnsurePracticePointerCapture();
    // nullptr if not yet captured.
    void* GetPracticeControllerPtr();
    // Record a Practice controller pointer observed by a trusted Revival hook.
    void NotePracticeControllerCandidate(void* practicePtr, const char* source);
    // Force an immediate Practice controller resolution attempt.
    // `allowCharacterSelect` is only for menu/reset paths that legitimately run in CS.
    // `allowLooseValidation` allows a match-only fallback that accepts a live Practice object
    // even if the stricter pause/menu invariants are not initialized yet.
    void* ResolvePracticeControllerPtrNow(bool allowCharacterSelect = false,
                                          bool allowLooseValidation = false,
                                          const char* reason = nullptr);
    // Guards against external unfreeze while the menu is visible.
    void MaintainFreezeWhileMenuVisible();

    // IsPracticePaused: Practice pause flag is set.
    // IsGameSpeedFrozen: gamespeed byte resolves and equals 0.
    // IsPausedOrFrozen: OR of the above.
    bool IsPracticePaused();
    bool IsGameSpeedFrozen();
    bool IsPausedOrFrozen();
    // Published before the flag flips, so a snapshot on another thread can tell the menu's pause from a real one.
    bool IsPracticePausedByMenu();

    // Practice step counter (+0xB0), incremented each native Revival step while paused; outCounter unchanged on failure.
    bool ReadStepCounter(uint32_t &outCounter);
    // Mirrors the official Revival pause toggle state (and step-counter reset) without calling the hotkey path.
    bool SetPracticePausedForFramestep(bool paused);
    // Queues one EfzRevival native step by setting Practice pause (+0xB4) and step-request (+0xAC) flags; 1.02f subframe build only.
    bool RequestPracticeSubframeStep();
    // True when paused and the step counter advanced since the last call; internally debounced, safe to call every tick.
    bool ConsumeStepAdvance();

    // Called from the hooked Revival Practice tick after a step-request flag
    // is consumed and the step counter incremented.
    void SetPracticeStepAdvanceCallback(StepAdvanceCallback callback);

    // Clears session-scoped Practice/battle/gamespeed pointers; hooks remain installed.
    void ResetCachedPointers(const char* reason);

    void SetRuntimeHooksActive(bool active);

    // Single owner of the 10 efz.exe visual-update sites NOPed by the vanilla
    // pause. Bytes are NOPed while any user holds them and restored to the
    // known originals when the last user releases. Refuses to patch outside
    // Practice or unless every site holds its known original bytes.
    enum class VisualFreezeUser : uint8_t {
        Menu = 1u << 0,
        Framestep = 1u << 1,
    };
    bool SetVisualFreeze(VisualFreezeUser user, bool freeze);
    // Practice boundary: drops every user and writes the originals back. Code
    // bytes only, so it is safe after netplay is published.
    void RestoreVisualSitesAtBoundary(const char* reason);
}
