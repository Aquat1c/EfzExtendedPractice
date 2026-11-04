#pragma once

namespace AutoAirtechState {

enum class DelayPhase { Idle, Waiting };
enum class AttemptPhase { Idle, Neutralizing, AwaitingInjection, Injecting, AwaitingRelease };

struct Attempt {
    AttemptPhase phase = AttemptPhase::Idle;
    int neutralRemaining = 0;
    int injectRemaining = 0;
    int anchorTick = -1;

    void Start(int neutralTicks, int injectTicks) {
        neutralRemaining = neutralTicks;
        injectRemaining = injectTicks;
        phase = neutralTicks > 0 ? AttemptPhase::Neutralizing : AttemptPhase::AwaitingInjection;
    }
    void NeutralPublished(int tick) {
        ConsumeTicks(neutralRemaining, tick);
        phase = neutralRemaining > 0 ? AttemptPhase::Neutralizing : AttemptPhase::AwaitingInjection;
    }
    void InjectionPublished() { phase = AttemptPhase::Injecting; }
    void AdvanceInjection(int tick) {
        ConsumeTicks(injectRemaining, tick);
        if (injectRemaining == 0) phase = AttemptPhase::AwaitingRelease;
    }
    void Finish() { phase = AttemptPhase::Idle; }
    void Reset() { *this = Attempt{}; }

private:
    void ConsumeTicks(int& remaining, int tick) {
        if (anchorTick >= 0) {
            remaining -= tick - anchorTick;
            if (remaining < 0) remaining = 0;
        }
        anchorTick = remaining > 0 ? tick : -1;
    }
};

}
