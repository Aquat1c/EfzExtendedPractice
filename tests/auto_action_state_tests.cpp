#include "game/auto_action.h"
#include <cstdlib>
#include <iostream>

void Check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

int main() {
    Check(WakeHoldAfterFailure(WakeHoldPhase::Issued) == WakeHoldPhase::Primed &&
          WakeHoldAfterFailure(WakeHoldPhase::Disabled) == WakeHoldPhase::Disabled,
          "failed normal retries only an eligible wake hold");
    WakeMacroSchedule macro;
    Check(!macro.HasTarget() && !macro.Queued(), "macro schedule starts without a target or playback");
    macro.ArmTarget(30);
    Check(macro.HasTarget() && macro.TargetFrame() == 30, "arming preserves the exact internal tick deadline");
    macro.MarkQueued();
    macro.ClearTarget();
    Check(macro.Queued() && !macro.HasTarget(), "dispatch can retire its deadline while playback stays queued");
    macro.ArmTarget(33);
    Check(macro.Queued() && macro.HasTarget(), "P2 target recomputation must not re-enable queued playback");
    macro.ResetQueue();
    Check(!macro.Queued() && macro.HasTarget(), "queue reset preserves a separately owned target");
    macro.RetryActionable();
    Check(macro.ShouldRetryActionable() && macro.TraceTargetFrame() == -2, "busy actionable fallback retains its retry phase");
    macro.ClearTarget();
    Check(!macro.ShouldRetryActionable() && macro.TraceTargetFrame() == -1, "clearing target retires a fallback retry");
    WakePrearmState prearm;
    prearm.Arm(WakePrearmPhase::Special);
    Check(prearm.Armed() && prearm.IsSpecial() && !prearm.IsMacro(), "special prearm has one producer kind");
    prearm.Arm(WakePrearmPhase::Macro);
    Check(prearm.IsMacro() && !prearm.IsSpecial(), "macro row cannot remain a special producer");
    prearm.Disarm();
    Check(!prearm.Armed() && !prearm.IsSpecial() && !prearm.IsMacro(), "disarming retires only producer metadata");
    WakeBufferedInputState buffered;
    buffered.SetBuffered(true);
    buffered.TrackMotion(42);
    Check(buffered.Phase() == WakeBufferedInputPhase::AwaitingConsumer && buffered.Generation() == 42,
          "buffered native motion keeps its consumer generation");
    buffered.SetBuffered(false);
    Check(buffered.Phase() == WakeBufferedInputPhase::RetainedGeneration && buffered.Generation() == 42,
          "releasing buffer does not erase an outstanding consumer obligation");
    buffered.TrackMotion(0);
    Check(buffered.Phase() == WakeBufferedInputPhase::Empty, "retiring the remaining generation empties the submission");
    buffered.SetBuffered(true);
    Check(buffered.Phase() == WakeBufferedInputPhase::BufferedWithoutGeneration,
          "legacy and macro buffering do not invent a native generation");
    buffered.TrackMotion(43);
    buffered.TrackMotion(0);
    Check(buffered.Buffered(), "retiring a generation preserves separately retained buffering");
    RgPrearmState rg;
    rg.Arm(71);
    Check(rg.Armed() && rg.Generation() == 71, "RG retains the admitted generation while native consumption is pending");
    rg.Clear();
    Check(!rg.Armed() && rg.Generation() == 0, "RG acceptance or failure retires the old generation before retry");
    rg.Arm(0);
    Check(rg.Armed(), "admission preserves the original zero-generation edge case");
    WakeCleanupMonitor cleanup;
    cleanup.Start(2, 96);
    Check(cleanup.Watching() && cleanup.LastMove() == 96, "cleanup starts only after reserving the input owner");
    Check(!cleanup.Tick() && cleanup.TicksRemaining() == 1, "cleanup retains the original tick deadline");
    cleanup.ObserveMove(200);
    Check(cleanup.Tick() && !cleanup.Watching(), "deadline expiry releases the cleanup watcher once");
    Check(!cleanup.Tick(), "idle cleanup cannot expire twice");
    cleanup.Start(20, 96);
    cleanup.Stop();
    Check(!cleanup.Watching() && cleanup.LastMove() == 96, "lost reservation stops watching without discarding the sample");
    cleanup.Reset();
    Check(cleanup.LastMove() == -1, "runtime reset clears the retained cleanup sample");
    WakeMacroHandoff handoff;
    handoff.Preserve(1);
    Check(handoff.Preserving() && !handoff.FinishPlayback(2), "another player's playback cannot consume wake preservation");
    Check(handoff.FinishPlayback(1) && handoff.CompletedPlayer() == 1 && !handoff.Preserving(),
          "matching playback publishes its cleanup obligation");
    handoff.Preserve(2);
    Check(handoff.CompletedPlayer() == 1 && handoff.Preserving(),
          "a fresh playback does not consume an earlier cleanup obligation");
    handoff.ClearCompletion();
    Check(handoff.CompletedPlayer() == 0 && handoff.Preserving(),
          "cleanup admission retires only the completion mailbox");
    handoff.Complete(2);
    handoff.ClearPreservation();
    Check(handoff.CompletedPlayer() == 2 && !handoff.Preserving(),
          "clearing playback preservation retains a pending cleanup retry");
    TriggerDelayState state{};
    Check(!state.IsDelaying(), "no trigger is pending initially");
    state.RetryDispatch();
    Check(state.IsDelaying() && state.phase == TriggerDelayPhase::WaitingDispatch,
          "busy input owner retains a dispatch request");
    state.WaitFor(TriggerDelayPhase::AwaitingNormal, 7);
    Check(state.phase == TriggerDelayPhase::AwaitingNormal && state.pendingGeneration == 7,
          "accepted normal waits for its exact consumer generation");
    state.RetryDispatch();
    Check(state.phase == TriggerDelayPhase::WaitingDispatch && state.pendingGeneration == 0,
          "failed consumption clears the old generation before retry");
    state.WaitFor(TriggerDelayPhase::AwaitingMotion, 8);
    state.WaitFor(TriggerDelayPhase::AwaitingRecipe, 9);
    Check(state.phase == TriggerDelayPhase::AwaitingRecipe && state.pendingGeneration == 9,
          "one trigger cannot wait on multiple consumer types");
    state = TriggerDelayState{};
    Check(!state.IsDelaying() && state.pendingGeneration == 0,
          "retirement clears consumer ownership");
}
