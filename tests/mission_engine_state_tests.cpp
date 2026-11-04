#include "game/mission/mission_engine_state.h"
#include <cstdlib>
#include <iostream>

void Check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

int main() {
    using namespace Mission::Engine::State;
    PendingEntry entry;
    entry.ObserveCharacterSelect();
    Check(!entry.Pending(), "late selector observation cannot reopen canceled entry");
    entry.SetPending(true);
    entry.ObserveCharacterSelect();
    Check(entry.Pending() && entry.SawCharacterSelect(), "pending entry remembers selector exit obligation");
    entry.ClearCharacterSelect();
    Check(entry.Pending() && !entry.SawCharacterSelect(), "generic browser entry survives canceled selector receipt");
    entry.SetPending(false);
    Check(!entry.Pending() && !entry.SawCharacterSelect(), "entry cancellation retires selector history");

    MatchupSelection selection;
    selection.Publish();
    Check(selection.HasCharacters() && !selection.Queued(), "published characters await selector drive");
    selection.Queue();
    Check(selection.Queued() && selection.HasCharacters(), "queued drive keeps matchup available");
    selection.Reset();
    Check(!selection.HasCharacters() && !selection.Queued(), "manual fallback releases automatic drive");

    selection.Queue();
    Check(!selection.HasCharacters() && !selection.Queued(), "late selector queue cannot reopen canceled metadata");

    RecorderCommandHandoff command;
    command.Begin();
    Check(command.Pending() && !command.WaitingForNeutral(), "command handoff waits for command completion");
    command.CommandCompleted();
    Check(command.WaitingForNeutral(), "completed command keeps input gate while neutral polls settle");
    command.Reset();
    command.CommandCompleted();
    Check(!command.Pending(), "late completion cannot reopen a cleared command handoff");

    RecorderCaptureMenu menu;
    menu.Open();
    menu.Close(true);
    Check(!menu.IsOpen() && menu.HasReleaseRequest() && menu.InputBlocked(),
          "close before the capture tick retains the release obligation");
    Check(menu.ConsumeRelease() && !menu.ConsumeRelease(), "release request is consumed once");
    menu.AcquireSuspension();
    Check(menu.Suspended() && menu.InputBlocked(), "raced close acquires neutral input quarantine");
    menu.Open();
    menu.EndSuspension();
    Check(menu.IsOpen() && menu.InputBlocked() && !menu.Suspended(),
          "retiring suspension cannot erase a newly reopened menu");
    menu.AcquireSuspension();
    menu.Close(true);
    Check(menu.ConsumeRelease() && menu.Suspended(), "release request preserves acquired suspension");
    menu.EndSuspension();
    Check(!menu.InputBlocked(), "last physical owner retirement releases capture gate");
    menu.Open();
    menu.Reset();
    Check(!menu.InputBlocked(), "reset clears all pending capture menu work");

    RecorderStop stop;
    Check(!stop.Pending() && !stop.ObserveCaptureTick(), "idle stop does not consume a capture tick");
    Check(stop.Request() && !stop.Request(), "duplicate stop cannot restart its boundary");
    Check(!stop.ObserveCaptureTick(), "first post-request capture must finish before sealing");
    Check(stop.ObserveCaptureTick(), "later capture can wait for a macro boundary");
    stop.Reset();
    Check(stop.Request() && !stop.ObserveCaptureTick(), "reset requires a fresh post-request tick");

    RecorderCountIn count;
    count.Begin(2);
    Check(!count.Advance(true) && count.Frames() == 0, "frozen countdown consumes no time");
    Check(!count.Advance(false) && count.Remaining() == 1, "countdown uses logical ticks");
    Check(count.Advance(false) && count.WaitingForLease(), "elapsed countdown enters input lease wait");
    count.Restart();
    Check(!count.WaitingForLease() && count.Frames() == 0 && count.Target() == 2,
          "a blocker restarts time while retaining configured duration");
    count.Begin(0);
    Check(!count.WaitingForLease() && count.Advance(false), "zero countdown still crosses a tick boundary");
    count.Reset();
    Check(count.Target() == 0 && !count.WaitingForLease(), "cancel clears old count-in progress");

    RunnerBaseline baseline;
    baseline.Begin(true);
    Check(baseline.RestorePending() && !baseline.SavePending() && !baseline.Ready(),
          "embedded state waits for restore, not a new save");
    baseline.BeginRestore();
    Check(!baseline.RestorePending() && !baseline.Ready(), "restore is consumed before synchronous callbacks");
    baseline.Complete(false);
    Check(!baseline.Ready(), "failed restore cannot publish a baseline");
    baseline.Begin(false);
    Check(baseline.SavePending() && !baseline.RestorePending(), "capture path waits for save");
    baseline.Complete(true);
    Check(baseline.Ready() && !baseline.SavePending(), "successful save publishes one ready baseline");
    baseline.Reset();
    Check(!baseline.Ready() && !baseline.RestorePending() && !baseline.SavePending(),
          "unload retires all baseline obligations");
}
