#pragma once
#include <stdint.h>

namespace PauseIntegration {
    using StepAdvanceCallback = void (*)(uint32_t beforeCounter, uint32_t afterCounter);

    // Surfaces share one physical Practice pause: the first visible surface acquires it, the last one releases it.
    enum class MenuSurface : uint8_t {
        ImGui = 1u << 0,
        MissionPause = 1u << 1,
        TutorialPage = 1u << 2,
        RecorderHandoff = 1u << 3,
        RecorderCommandHandoff = 1u << 4,
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
    bool BeginMenuSurfaceRefresh(MenuSurface surface, uint64_t& tokenOut);
    bool EndMenuSurfaceRefresh(MenuSurface surface, uint64_t token);
    void ReassertMenuSurfacePause(MenuSurface surface);
    // Emergency/session teardown only; normal UI releases only its own surface.
    void ForceCloseAllMenuSurfaces();

    void OnMenuVisibilityChanged(bool visible);
    void EnsurePracticePointerCapture();
    void* GetPracticeControllerPtr();
    void NotePracticeControllerCandidate(void* practicePtr, const char* source);
    void* ResolvePracticeControllerPtrNow(const char* reason = nullptr);
    void MaintainFreezeWhileMenuVisible();

    bool IsPracticePaused();
    bool IsGameSpeedFrozen();
    bool IsPausedOrFrozen();
    // Published before the flag flips, so a snapshot on another thread can tell the menu's pause from a real one.
    bool IsPracticePausedByMenu();

    // Practice step counter (+0xB0), incremented each native Revival step while paused; outCounter unchanged on failure.
    bool ReadStepCounter(uint32_t &outCounter);
    bool SetPracticePausedForFramestep(bool paused);
    // Queues one native step by setting the Practice pause (+0xB4) and step-request (+0xAC) flags.
    // 1.02f subframe build only.
    bool RequestPracticeSubframeStep();
    bool ConsumeStepAdvance();

    void SetPracticeStepAdvanceCallback(StepAdvanceCallback callback);

    void ResetCachedPointers(const char* reason);

    void SetRuntimeHooksActive(bool active);

    // Single owner of the 10 efz.exe visual-update sites NOPed by the vanilla pause; last user restores.
    // Refuses to patch outside Practice or unless every site holds its known original bytes.
    enum class VisualFreezeUser : uint8_t {
        Menu = 1u << 0,
        Framestep = 1u << 1,
    };
    bool SetVisualFreeze(VisualFreezeUser user, bool freeze);
    // Practice boundary: drops every user and writes the originals back. Code
    // bytes only, so it is safe after netplay is published.
    void RestoreVisualSitesAtBoundary(const char* reason);
}
