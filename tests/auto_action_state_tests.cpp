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
