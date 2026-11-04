#include "game/sayuri_counter_state.h"
#include <cstdio>
int main() {
    using namespace SayuriCounter;
    int failed = 0;
    auto check = [&](bool value, const char* message) { if (!value) { std::fprintf(stderr, "%s\n", message); ++failed; } };
    CutterSequence sequence;
    check(sequence.phase == CutterPhase::AwaitingWindow, "no window before native readiness");
    sequence.OpenWindow(-1);
    check(sequence.phase == CutterPhase::AwaitingWindow, "invalid engine tick cannot establish a delay anchor");
    sequence.OpenWindow(30);
    sequence.OpenWindow(40);
    check(sequence.openTick == 30 && sequence.phase == CutterPhase::Waiting, "first open window owns delay anchor");
    sequence.Submitted(7);
    check(sequence.phase == CutterPhase::AwaitingPulse && sequence.generation == 7 && sequence.attempts == 1,
          "submitted pulse retains exact generation until consumed");
    sequence.PulseFinished();
    check(sequence.phase == CutterPhase::Waiting && sequence.generation == 0 && sequence.openTick == 30,
          "retry preserves initial window delay");
    sequence.Submitted(8); sequence.PulseFinished();
    sequence.Submitted(9); sequence.PulseFinished();
    check(sequence.phase == CutterPhase::Exhausted && sequence.attempts == 3, "third attempt exhausts the native window");
    sequence.Reset(); sequence.OpenWindow(60); sequence.Invalid();
    check(sequence.phase == CutterPhase::Exhausted, "invalid intent ends retries immediately");
    sequence.Reset();
    check(sequence.phase == CutterPhase::AwaitingWindow && sequence.openTick == -1 && sequence.generation == 0 && sequence.attempts == 0,
          "blockstun boundary resets window and receipts");
    return failed ? 1 : 0;
}
