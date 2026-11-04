#include "game/macro_controller.h"

#include <cstdlib>
#include <iostream>

namespace {

void Check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

}

int main() {
    {
        using MacroController::PlaybackStartPolicy::PickFilledSlot;
        const bool none[3] = {false, false, false};
        Check(PickFilledSlot(none, 3, 7) == 0, "nothing recorded");
        const bool some[4] = {false, true, false, true};
        Check(PickFilledSlot(some, 4, 0) == 2, "first recorded slot");
        Check(PickFilledSlot(some, 4, 1) == 4, "second recorded slot");
        Check(PickFilledSlot(some, 4, 2) == 2, "roll wraps over recorded slots only");
    }
    using MacroController::PlaybackStartMode;
    using MacroController::PlaybackStartPolicy::CanPublishNextPrimedSlice;
    using MacroController::PlaybackStartPolicy::PrimePhase;
    using MacroController::PlaybackStartPolicy::AfterPrimedSlice;
    using MacroController::PlaybackStartPolicy::Decide;
    using MacroController::PlaybackStartPolicy::PublishMonitorSlice;
    using MacroController::PlaybackStartPolicy::PrimedSliceCanLatchEnd;
    using MacroController::PlaybackStartPolicy::PrimedTickPending;
    using MacroController::PlaybackStartPolicy::SupportedRawWriteCount;
    using MacroController::PlaybackStartPolicy::StreamEndCanLatch;
    using MacroController::PlaybackStartPolicy::WritesForSubframe;

    constexpr auto ordinary = Decide(
        PlaybackStartMode::Ordinary, 0, 12);
    Check(ordinary.valid && !ordinary.prime &&
              ordinary.tickToPrepare == 0 &&
              ordinary.cursorAfterAdmission == 0,
          "ordinary playback preserves the historical deferred first tick");

    constexpr auto aligned = Decide(
        PlaybackStartMode::PrimeBeforeNextPoll, 0, 12);
    Check(aligned.valid && aligned.prime &&
              aligned.tickToPrepare == 0 &&
              aligned.cursorAfterAdmission == 1,
          "aligned playback publishes tick zero and advances exactly once");

    constexpr auto alignedOffset = Decide(
        PlaybackStartMode::PrimeBeforeNextPoll, 5, 12);
    Check(alignedOffset.valid && alignedOffset.prime &&
              alignedOffset.tickToPrepare == 5 &&
              alignedOffset.cursorAfterAdmission == 6,
          "aligned admission advances once from an explicit start tick");

    Check(!Decide(PlaybackStartMode::Ordinary, -1, 12).valid &&
              !Decide(PlaybackStartMode::PrimeBeforeNextPoll, 12, 12).valid &&
              !Decide(PlaybackStartMode::PrimeBeforeNextPoll, 0, 0).valid,
          "invalid stream ranges cannot be primed or deferred");

    Check(WritesForSubframe(3, 3) == 1 &&
              WritesForSubframe(2, 2) == 1 &&
              WritesForSubframe(1, 1) == 1,
          "three tick-zero buffer writes remain distributed across subframes");
    Check(WritesForSubframe(3, 2) == 2 &&
              WritesForSubframe(1, 1) == 1 &&
              WritesForSubframe(0, 3) == 0,
          "supported uneven raw writes drain without an admission burst");
    Check(SupportedRawWriteCount(0) && SupportedRawWriteCount(1) &&
              SupportedRawWriteCount(2) && SupportedRawWriteCount(3) &&
              !SupportedRawWriteCount(4) &&
              !SupportedRawWriteCount(65535),
          "raw groups larger than the three native subframes fail closed");
    Check(!CanPublishNextPrimedSlice(PrimePhase::AwaitingFirstPoll, true, true, true) &&
              !CanPublishNextPrimedSlice(PrimePhase::Draining, false, false, false) &&
              CanPublishNextPrimedSlice(PrimePhase::Draining, true, false, false) &&
              CanPublishNextPrimedSlice(PrimePhase::Draining, false, true, false) &&
              CanPublishNextPrimedSlice(PrimePhase::Draining, false, false, true),
          "each remaining primed slice waits for an acknowledged poll or frame step");
    Check(PrimedTickPending(2, 0, 0) &&
              PrimedTickPending(1, 0, 0) &&
              PrimedTickPending(0, 1, 1) &&
              !PrimedTickPending(0, 0, 0) &&
              !PrimedSliceCanLatchEnd(2) &&
              PrimedSliceCanLatchEnd(1),
          "tick zero retains all cadence slots before tick one or stream end");
    Check(!CanPublishNextPrimedSlice(PrimePhase::Inactive, true, true, true) &&
          AfterPrimedSlice(1, 0, 0) == PrimePhase::Draining &&
          AfterPrimedSlice(0, 1, 0) == PrimePhase::Draining &&
          AfterPrimedSlice(0, 0, 0) == PrimePhase::Inactive,
          "prime drain finishes only after cadence and queued writes are empty");
    constexpr uint8_t directions = 0x0F;
    constexpr uint8_t buttons = 0xF0;
    Check(PublishMonitorSlice(0x10, 0x10, directions, buttons) == 0x10 &&
              PublishMonitorSlice(0x10, 0x00, directions, buttons) == 0x10 &&
              PublishMonitorSlice(0x10, 0x00, directions, buttons) == 0x10,
          "raw A-neutral-neutral cannot erase the logical one-tick attack before EFZ polls");
    Check(PublishMonitorSlice(0x41, 0x41, directions, buttons) == 0x41 &&
              PublishMonitorSlice(0x41, 0x01, directions, buttons) == 0x41 &&
              PublishMonitorSlice(0x41, 0x01, directions, buttons) == 0x41,
          "trailing direction-only samples preserve a logical 6C input in prime and ordinary playback");
    Check(PublishMonitorSlice(0x05, 0x25, directions, buttons) == 0x05 &&
              PublishMonitorSlice(0x05, 0x00, directions, buttons) == 0x05,
          "a raw button cannot schedule-dependently refine the logical tick");
    Check(PublishMonitorSlice(0x30, 0x10, directions, buttons) == 0x30 &&
              PublishMonitorSlice(0x30, 0x20, directions, buttons) == 0x30 &&
              PublishMonitorSlice(0x30, 0x00, directions, buttons) == 0x30,
          "an A-B-neutral raw group keeps its authoritative logical button union");
    Check(!StreamEndCanLatch(1, 1, 2, 2) &&
              !StreamEndCanLatch(1, 1, 0, 1) &&
              !StreamEndCanLatch(0, 1, 0, 0) &&
              StreamEndCanLatch(1, 1, 0, 0),
          "one-tick playback cannot finish before every primed raw slice drains");

    using FinishPhase = MacroController::PlaybackFinishPolicy::Phase;
    namespace Finish = MacroController::PlaybackFinishPolicy;
    Check(Finish::AfterFinalPoll(7, 7, 11) == FinishPhase::AwaitingFinalPoll &&
          Finish::AfterFinalPoll(7, 7, 12) == FinishPhase::AwaitingClearTick &&
          Finish::AfterFinalPoll(0xffffffffu, 0, 0) == FinishPhase::AwaitingClearTick,
          "final byte survives a poll or the exact twelve-tick watchdog");
    Check(Finish::AfterClearTick(1) == FinishPhase::AwaitingClearTick &&
          Finish::AfterClearTick(0) == FinishPhase::Neutralizing &&
          Finish::AfterNeutral(1) == FinishPhase::Neutralizing &&
          Finish::AfterNeutral(0) == FinishPhase::Guarding,
          "tail waits for a full tick and drains every neutral tick");
    Check(!Finish::GuardFinished(false, 1) && Finish::GuardFinished(false, 0) &&
          Finish::GuardFinished(true, 6),
          "guard releases on activation or timeout");

    std::cout << "macro playback start policy tests passed\n";
    return 0;
}
