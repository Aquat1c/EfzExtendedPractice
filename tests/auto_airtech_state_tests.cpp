#include "game/auto_airtech_state.h"
#include <cstdlib>
#include <iostream>

void Check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

int main() {
    using namespace AutoAirtechState;
    Attempt attempt;
    Check(attempt.phase == AttemptPhase::Idle, "attempt starts idle");
    attempt.Start(3, 6);
    Check(attempt.phase == AttemptPhase::Neutralizing, "attempt starts with neutral input");
    attempt.NeutralPublished(100);
    attempt.NeutralPublished(102);
    Check(attempt.phase == AttemptPhase::Neutralizing && attempt.neutralRemaining == 1,
          "neutral counts engine ticks, not monitor calls");
    attempt.NeutralPublished(103);
    Check(attempt.phase == AttemptPhase::AwaitingInjection && attempt.anchorTick == -1,
          "injection starts on a later monitor pass after neutral completes");
    attempt.InjectionPublished();
    attempt.AdvanceInjection(104);
    attempt.AdvanceInjection(104);
    Check(attempt.injectRemaining == 6, "repeated monitor pass consumes no engine ticks");
    attempt.AdvanceInjection(110);
    Check(attempt.phase == AttemptPhase::AwaitingRelease, "full hold ends before release pass");
    attempt.Finish();
    Check(attempt.phase == AttemptPhase::Idle, "release retires the attempt");
    attempt.Start(3, 6);
    attempt.NeutralPublished(200);
    attempt.Start(3, 6);
    attempt.NeutralPublished(201);
    Check(attempt.neutralRemaining == 2,
          "rearming preserves the current engine tick anchor");
    attempt.Reset();
    Check(attempt.phase == AttemptPhase::Idle && attempt.anchorTick == -1 &&
          attempt.neutralRemaining == 0 && attempt.injectRemaining == 0,
          "lifecycle reset drops all old timing anchors");
    return 0;
}
