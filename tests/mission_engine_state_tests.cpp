#include "game/mission/mission_engine_state.h"
#include "game/mission/mission_sequence_policy.h"
#include <cstdlib>
#include <iostream>

void Check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

int main() {
    using namespace Mission::Engine::State;
    DemoPlayback playback;
    Check(playback.BeginOrAdvanceContactSettle(3) && playback.ContactTicksRemaining() == 3,
          "first final-contact wait starts the full timeout without consuming a tick");
    Check(!playback.BeginOrAdvanceContactSettle(3) && playback.ContactTicksRemaining() == 2,
          "later final-contact wait consumes one tick");
    playback.BeginOrAdvanceContactSettle(3);
    Check(playback.ContactTicksRemaining() == 1, "last settle tick remains available to the failure policy");
    playback.BeginTerminalHold(2);
    Check(playback.HoldingTerminalFrame() && playback.ContactTicksRemaining() == 0,
          "terminal hold retires contact settling");
    Check(!playback.AdvanceTerminalHold() && playback.AdvanceTerminalHold(),
          "terminal frame remains visible for the exact hold duration");
    playback.Reset();
    Check(!playback.HoldingTerminalFrame() && playback.ContactTicksRemaining() == 0,
          "cancel or baseline restore retires playback subphases");

    using namespace Mission::SequencePolicy;
    playback.Reset();
    for (int tick = 0; tick < kDemoRecipeSettleTimeoutTicks; ++tick) {
        Check(DecideDemoRecipeEnd(true, false, playback.ContactTicksRemaining()) == DemoRecipeEndAction::SettleFinalContacts,
              "final contacts retain every timeout tick before failure");
        playback.BeginOrAdvanceContactSettle(kDemoRecipeSettleTimeoutTicks);
    }
    Check(DecideDemoRecipeEnd(true, false, playback.ContactTicksRemaining()) == DemoRecipeEndAction::FailThenHold,
          "final-contact timeout still fails on the original boundary");
    playback.BeginTerminalHold(kDemoTerminalRecipeHoldTicks);
    for (int tick = 1; tick < kDemoTerminalRecipeHoldTicks; ++tick)
        Check(!playback.AdvanceTerminalHold(), "terminal frame remains held before its final tick");
    Check(playback.AdvanceTerminalHold(), "terminal frame restore occurs on its final tick");

    DemoTutorialReturn tutorialReturn;
    tutorialReturn.Begin(true);
    Check(tutorialReturn.Requested() && !tutorialReturn.HasResult(), "tutorial handoff waits for restore outcome");
    tutorialReturn.Publish(true);
    Check(tutorialReturn.Requested() && tutorialReturn.Restored(), "successful restore retains acknowledgement ownership");
    tutorialReturn.Reset();
    tutorialReturn.Publish(false);
    Check(!tutorialReturn.Requested() && !tutorialReturn.HasResult(), "late restore cannot reopen acknowledged handoff");
    tutorialReturn.Begin(true);
    tutorialReturn.Publish(false);
    Check(tutorialReturn.HasResult() && !tutorialReturn.Restored(), "failed restore remains available for acknowledgement");
    tutorialReturn.Begin(false);
    Check(!tutorialReturn.Requested(), "ordinary preview has no tutorial handoff");

    RecorderComboEnd comboEnd;
    comboEnd.SelectStep(4);
    Check(comboEnd.Pending() && comboEnd.IsDirect() && comboEnd.AfterAction() == 4,
          "direct combo boundary defaults to its recorded action");
    comboEnd.SetAfterAction(6);
    Check(comboEnd.AfterAction() == 6, "later setup action owns the boundary position");
    comboEnd.SelectEntity(17);
    comboEnd.SetAfterAction(8);
    Check(comboEnd.IsEntity() && !comboEnd.IsDirect() && comboEnd.EntitySequence() == 17 && comboEnd.AfterAction() == 8,
          "entity contact replaces direct boundary ownership");
    comboEnd.Reset();
    Check(!comboEnd.Pending() && !comboEnd.IsDirect() && !comboEnd.IsEntity(), "commit or capture reset clears the anchor");

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
