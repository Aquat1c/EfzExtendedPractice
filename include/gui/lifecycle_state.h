#pragma once

#include <atomic>
#include <cstdint>

namespace GuiLifecycle {

enum class ConfirmationPage { Closed, Menu, Confirmation };
class ConfirmationMenu {
public:
    bool IsOpen() const { return page_ != ConfirmationPage::Closed; }
    bool IsConfirming() const { return page_ == ConfirmationPage::Confirmation; }
    void Open() { page_ = ConfirmationPage::Menu; }
    void Close() { page_ = ConfirmationPage::Closed; }
    void Confirm() {
        if (page_ == ConfirmationPage::Menu) page_ = ConfirmationPage::Confirmation;
    }
    void CancelConfirmation() {
        if (page_ == ConfirmationPage::Confirmation) page_ = ConfirmationPage::Menu;
    }
private:
    ConfirmationPage page_ = ConfirmationPage::Closed;
};
enum class CapturePhase { Closed, AwaitRelease, Capturing };

class KeyCapture {
public:
    bool IsOpen() const { return phase_ != CapturePhase::Closed; }
    bool CanCapture() const { return phase_ == CapturePhase::Capturing; }
    void Open() { phase_ = CapturePhase::AwaitRelease; }
    void Close() { phase_ = CapturePhase::Closed; }
    void Suspend() { if (IsOpen()) phase_ = CapturePhase::AwaitRelease; }
    void ObserveRelease(bool anyHeld) {
        if (phase_ == CapturePhase::AwaitRelease && !anyHeld)
            phase_ = CapturePhase::Capturing;
    }
private:
    CapturePhase phase_ = CapturePhase::Closed;
};

enum class GifLoadPhase { Unloaded, RetryWait, Ready };

class GifLoad {
public:
    bool IsReady() const { return phase_ == GifLoadPhase::Ready; }
    bool ShouldAttempt(uint32_t now) const {
        return requested_ && !IsReady()
            && (phase_ != GifLoadPhase::RetryWait || failedAt_ == 0
                || static_cast<uint32_t>(now - failedAt_) >= 5000);
    }
    void Request() { requested_ = true; }
    void Failed(uint32_t now) { phase_ = GifLoadPhase::RetryWait; failedAt_ = now; }
    void Loaded() { phase_ = GifLoadPhase::Ready; requested_ = false; }
    void Release() { phase_ = GifLoadPhase::Unloaded; }
    void Reset() { *this = {}; }
private:
    GifLoadPhase phase_ = GifLoadPhase::Unloaded;
    bool requested_ = false;
    uint32_t failedAt_ = 0;
};

enum class FallbackPhase { Stopped, Starting, Ready, Stopping, Failed };

class FallbackWorker {
public:
    bool IsRunning() const {
        const auto phase = phase_.load(std::memory_order_acquire);
        return phase == FallbackPhase::Starting || phase == FallbackPhase::Ready
            || phase == FallbackPhase::Stopping;
    }
    bool IsReady() const { return phase_.load(std::memory_order_acquire) == FallbackPhase::Ready; }
    bool HasFailed() const { return phase_.load(std::memory_order_acquire) == FallbackPhase::Failed; }
    bool StopRequested() const { return phase_.load(std::memory_order_acquire) == FallbackPhase::Stopping; }
    bool TryStart() {
        auto phase = phase_.load(std::memory_order_acquire);
        while (phase == FallbackPhase::Stopped || phase == FallbackPhase::Failed) {
            if (phase_.compare_exchange_weak(phase, FallbackPhase::Starting,
                    std::memory_order_acq_rel)) return true;
        }
        return false;
    }
    bool PublishReady() {
        auto phase = FallbackPhase::Starting;
        return phase_.compare_exchange_strong(phase, FallbackPhase::Ready,
            std::memory_order_acq_rel);
    }
    void RequestStop() {
        auto phase = phase_.load(std::memory_order_acquire);
        while (phase == FallbackPhase::Starting || phase == FallbackPhase::Ready) {
            if (phase_.compare_exchange_weak(phase, FallbackPhase::Stopping,
                    std::memory_order_acq_rel)) return;
        }
    }
    // Publish only after the worker releases its window and backend resources.
    void Finish(bool failed) {
        phase_.store(failed ? FallbackPhase::Failed : FallbackPhase::Stopped,
            std::memory_order_release);
    }
private:
    std::atomic<FallbackPhase> phase_{FallbackPhase::Stopped};
};

enum class RepeatPhase { Released, InitialDelay, Repeating };
class RepeatInput {
public:
    unsigned HeldFrames() const { return heldFrames_; }
    unsigned FramesSinceFire() const { return framesSinceFire_; }
    bool Tick(bool pressed, bool risingEdge, unsigned delay, unsigned interval) {
        if (!pressed) { *this = {}; return false; }
        if (risingEdge) {
            phase_ = RepeatPhase::InitialDelay;
            heldFrames_ = 1;
            framesSinceFire_ = 0;
            return true;
        }
        if (phase_ == RepeatPhase::Released) phase_ = RepeatPhase::InitialDelay;
        ++heldFrames_;
        if (phase_ == RepeatPhase::InitialDelay && heldFrames_ >= delay)
            phase_ = RepeatPhase::Repeating;
        ++framesSinceFire_;
        if (phase_ == RepeatPhase::Repeating && framesSinceFire_ >= interval) {
            framesSinceFire_ = 0;
            return true;
        }
        return false;
    }
private:
    RepeatPhase phase_ = RepeatPhase::Released;
    unsigned heldFrames_ = 0;
    unsigned framesSinceFire_ = 0;
};

enum class MenuRenderPhase { Game, BackendUnavailable, AwaitingGameFrame };
class MenuRenderRoute {
public:
    bool NeedsFallback() const { return phase_.load(std::memory_order_acquire) != MenuRenderPhase::Game; }
    bool WaitingForFrame() const { return phase_.load(std::memory_order_acquire) == MenuRenderPhase::AwaitingGameFrame; }
    void UseGame() { phase_.store(MenuRenderPhase::Game, std::memory_order_release); }
    void BackendUnavailable() { phase_.store(MenuRenderPhase::BackendUnavailable, std::memory_order_release); }
    void NoFrames() { phase_.store(MenuRenderPhase::AwaitingGameFrame, std::memory_order_release); }
    void ObserveFrame() {
        auto phase = MenuRenderPhase::AwaitingGameFrame;
        (void)phase_.compare_exchange_strong(phase, MenuRenderPhase::Game, std::memory_order_acq_rel);
    }
private:
    std::atomic<MenuRenderPhase> phase_{MenuRenderPhase::Game};
};

enum class MenuFadePhase { Opening, Closing };
enum class TextEditorPhase { Closed, FocusRequested, Editing };
class TextEditor {
public:
    bool IsOpen() const { return phase_ != TextEditorPhase::Closed; }
    bool NeedsFocus() const { return phase_ == TextEditorPhase::FocusRequested; }
    void Open() { phase_ = TextEditorPhase::FocusRequested; }
    void Close() { phase_ = TextEditorPhase::Closed; }
    void RequestFocus() { if (IsOpen()) phase_ = TextEditorPhase::FocusRequested; }
    void Focused() { if (NeedsFocus()) phase_ = TextEditorPhase::Editing; }
private:
    TextEditorPhase phase_ = TextEditorPhase::Closed;
};

enum class ImGuiPhase { Uninitialized, Initializing, Ready };
class ImGuiInitialization {
public:
    bool IsReady() const { return phase_.load(std::memory_order_acquire) == ImGuiPhase::Ready; }
    bool TryBegin() {
        auto phase = ImGuiPhase::Uninitialized;
        return phase_.compare_exchange_strong(phase, ImGuiPhase::Initializing,
            std::memory_order_acq_rel);
    }
    void Finish(bool ready) {
        phase_.store(ready ? ImGuiPhase::Ready : ImGuiPhase::Uninitialized,
            std::memory_order_release);
    }
private:
    std::atomic<ImGuiPhase> phase_{ImGuiPhase::Uninitialized};
};
}
