#include "input/immediate_input_state.h"
#include <cstdio>
int main() {
    ImmediateInput::WorkerLifecycle worker;
    using ImmediateInput::OutputMode;
    using ImmediateInput::ReadOutputMode;
    int failed = 0;
    auto check = [&](bool value, const char* message) { if (!value) { std::fprintf(stderr, "%s\n", message); ++failed; } };
    check(ReadOutputMode(0, 0) == OutputMode::Idle && ReadOutputMode(1, 0) == OutputMode::Held,
          "untimed input distinguishes idle from held output");
    check(ReadOutputMode(1, 2) == OutputMode::Timed && ReadOutputMode(0, 2) == OutputMode::Timed,
          "pending tick cleanup retains precedence even after desired input is cleared");
    check(!worker.IsRunning() && worker.ShouldStop(), "unstarted worker admits no input");
    worker.Started();
    check(worker.IsRunning() && !worker.ShouldStop(), "started worker accepts input");
    worker.RequestStop();
    check(worker.IsRunning() && worker.ShouldStop(), "cancel retains worker ownership until exit");
    worker.RequestStop();
    check(worker.BeginJoin() && !worker.IsRunning(), "join closes admission and reports previous worker");
    worker.RequestStop();
    check(!worker.IsRunning() && worker.ShouldStop(), "late cancel cannot reopen a joining worker");
    worker.Exited();
    check(!worker.BeginJoin(), "joining an already exited worker does not report it as running");
    worker.Exited(); worker.Started();
    check(worker.IsRunning() && !worker.ShouldStop(), "new generation starts without old cancellation");
    worker.Exited();
    check(!worker.IsRunning(), "thread creation failure or natural exit retires running status");
    return failed ? 1 : 0;
}
