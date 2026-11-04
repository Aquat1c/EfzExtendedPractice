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
