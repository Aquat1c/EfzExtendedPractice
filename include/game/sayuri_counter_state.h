#pragma once
#include <cstdint>

namespace SayuriCounter {
enum class CutterPhase { AwaitingWindow, Waiting, AwaitingPulse, Exhausted };
struct CutterSequence {
    static constexpr int MaxAttempts = 3;
    CutterPhase phase = CutterPhase::AwaitingWindow;
    int attempts = 0;
    uint64_t generation = 0;
    int openTick = -1;

    void Reset() { *this = CutterSequence{}; }
    void OpenWindow(int tick) {
        if (phase == CutterPhase::AwaitingWindow) {
            openTick = tick;
            phase = tick >= 0 ? CutterPhase::Waiting : CutterPhase::AwaitingWindow;
        }
    }
    void Submitted(uint64_t value) {
        generation = value;
        ++attempts;
        phase = CutterPhase::AwaitingPulse;
    }
    void PulseFinished() {
        generation = 0;
        phase = attempts < MaxAttempts ? CutterPhase::Waiting : CutterPhase::Exhausted;
    }
    void Invalid() { attempts = MaxAttempts; phase = CutterPhase::Exhausted; }
};
}
