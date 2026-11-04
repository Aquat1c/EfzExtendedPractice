#pragma once

namespace AutoJumpState {
enum class Phase { HoldingJump, Neutralizing };
struct LandingInput {
    static constexpr int NeutralTicks = 3;
    Phase phase = Phase::HoldingJump;
    int neutralUntilTick = 0;
    void ObserveTick(int tick) {
        if (neutralUntilTick > tick + NeutralTicks) neutralUntilTick = 0;
        phase = tick < neutralUntilTick ? Phase::Neutralizing : Phase::HoldingJump;
    }
    void Landed(int tick) {
        neutralUntilTick = tick + NeutralTicks;
        phase = Phase::Neutralizing;
    }
};
}
