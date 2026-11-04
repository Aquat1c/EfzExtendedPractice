#include "gui/lifecycle_state.h"
#include <iostream>

int main() {
    using namespace GuiLifecycle;
    int failures = 0;
    const auto expect = [&](bool ok, const char* message) {
        if (!ok) { std::cerr << message << '\n'; ++failures; }
    };
    ConfirmationMenu menu;
    menu.CancelConfirmation();
    menu.Confirm();
    expect(!menu.IsOpen(), "confirmation cannot open a closed menu");
    menu.Open();
    menu.Confirm();
    expect(menu.IsOpen() && menu.IsConfirming(), "confirmation is an open menu page");
    menu.CancelConfirmation();
    expect(menu.IsOpen() && !menu.IsConfirming(), "back returns to the menu");
    menu.Close();
    menu.CancelConfirmation();
    expect(!menu.IsOpen(), "late back cannot reopen the menu");

    KeyCapture capture;
    expect(!capture.IsOpen() && !capture.CanCapture(), "capture starts closed");
    capture.ObserveRelease(false);
    expect(!capture.IsOpen(), "release cannot reopen closed capture");
    capture.Open();
    expect(capture.IsOpen() && !capture.CanCapture(), "opening waits for activating key release");
    capture.ObserveRelease(true);
    expect(!capture.CanCapture(), "held opener cannot become a new binding");
    capture.ObserveRelease(false);
    expect(capture.CanCapture(), "release arms capture");
    capture.Suspend();
    expect(capture.IsOpen() && !capture.CanCapture(), "focus loss requires another release");
    capture.Close();
    capture.Suspend();
    capture.ObserveRelease(false);
    expect(!capture.IsOpen(), "cancel stays terminal until explicitly reopened");

    GifLoad load;
    expect(!load.ShouldAttempt(0), "unused GIF does not load");
    load.Request();
    expect(load.ShouldAttempt(0), "first request loads immediately");
    load.Failed(100);
    expect(!load.ShouldAttempt(5099) && load.ShouldAttempt(5100), "failed GIF waits exactly five seconds");
    load.Loaded();
    expect(load.IsReady() && !load.ShouldAttempt(6000), "loaded GIF cannot reload");
    load.Release();
    expect(!load.IsReady() && !load.ShouldAttempt(6000), "unrequested loaded GIF releases to dormant");
    load.Request();
    load.Loaded();
    load.Request();
    load.Release();
    expect(load.ShouldAttempt(6000), "request while loaded survives device release");
    load.Failed(0xfffffff0u);
    expect(!load.ShouldAttempt(0x1377u) && load.ShouldAttempt(0x1378u), "retry timing survives tick wrap");
    load.Release();
    expect(load.ShouldAttempt(0), "device release clears a retry cooldown");
    load.Reset();
    expect(!load.ShouldAttempt(9000) && !load.IsReady(), "shutdown clears pending load");

    FallbackWorker worker;
    expect(!worker.IsRunning() && worker.TryStart(), "stopped fallback starts once");
    expect(!worker.TryStart(), "concurrent launch cannot create a second worker");
    worker.RequestStop();
    expect(worker.IsRunning() && !worker.PublishReady(), "stop during initialization rejects late readiness");
    expect(!worker.TryStart(), "stopping owns resources until cleanup completes");
    worker.Finish(false);
    expect(!worker.IsRunning() && worker.TryStart(), "cleanup permits next start");
    expect(worker.PublishReady() && worker.IsReady(), "startup publishes ready");
    worker.RequestStop();
    expect(!worker.IsReady() && worker.IsRunning(), "stop withdraws readiness before cleanup");
    worker.Finish(true);
    expect(worker.HasFailed() && !worker.IsRunning(), "failed worker is terminal after cleanup");
    expect(worker.TryStart() && !worker.HasFailed(), "explicit retry clears prior failure");
    worker.Finish(false);
    worker.RequestStop();
    expect(!worker.IsRunning(), "stop while inactive is idempotent");
    TextEditor editor;
    editor.RequestFocus();
    expect(!editor.IsOpen(), "closed editor ignores focus requests");
    editor.Open();
    expect(editor.IsOpen() && editor.NeedsFocus(), "editor opens awaiting focus");
    editor.Focused();
    expect(editor.IsOpen() && !editor.NeedsFocus(), "focus consumption enters editing");
    editor.RequestFocus();
    expect(editor.NeedsFocus(), "editing supports explicit refocus");
    editor.Close();
    editor.Focused();
    expect(!editor.IsOpen() && !editor.NeedsFocus(), "late focus cannot revive closed editor");

    ImGuiInitialization initialization;
    expect(initialization.TryBegin() && !initialization.IsReady(), "ImGui has an exclusive initializing state");
    expect(!initialization.TryBegin(), "ImGui rejects overlapping initialization");
    initialization.Finish(false);
    expect(initialization.TryBegin(), "ImGui failed setup permits retry");
    initialization.Finish(true);
    expect(initialization.IsReady() && !initialization.TryBegin(), "ready ImGui cannot initialize twice");
    initialization.Finish(false);
    expect(!initialization.IsReady(), "ImGui shutdown clears readiness");
    RepeatInput repeat;
    expect(repeat.Tick(true, true, 18, 4), "navigation fires on press");
    for (unsigned frame = 2; frame < 18; ++frame)
        expect(!repeat.Tick(true, false, 18, 4), "navigation waits through initial hold delay");
    expect(repeat.Tick(true, false, 18, 4), "navigation repeats on delay boundary");
    for (unsigned frame = 1; frame < 4; ++frame)
        expect(!repeat.Tick(true, false, 18, 4), "repeat interval holds navigation");
    expect(repeat.Tick(true, false, 18, 4), "navigation repeats on interval boundary");
    expect(!repeat.Tick(false, false, 18, 4), "release resets repeat phase");
    expect(repeat.Tick(true, true, 18, 4), "new press fires without old repeat debt");
    MenuRenderRoute route;
    expect(!route.NeedsFallback(), "renderer starts on game route");
    route.NoFrames();
    expect(route.WaitingForFrame() && route.NeedsFallback(), "watchdog selects missing-frame fallback");
    route.BackendUnavailable();
    route.ObserveFrame();
    expect(route.NeedsFallback() && !route.WaitingForFrame(), "late frame cannot clear backend failure");
    route.NoFrames();
    route.ObserveFrame();
    expect(!route.NeedsFallback(), "observed frame releases only watchdog fallback");
    route.BackendUnavailable();
    route.UseGame();
    expect(!route.NeedsFallback(), "successful backend restores game route");
    return failures == 0 ? 0 : 1;
}
