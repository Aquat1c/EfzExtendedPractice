#pragma once
#include <atomic>
#include <cstdint>

namespace Mission::Engine::State {

enum class DemoPlaybackPhase { Inputs, SettlingContacts, HoldingTerminalFrame, RestoreDue };
class DemoPlayback {
public:
    bool HoldingTerminalFrame() const { return phase_ == DemoPlaybackPhase::HoldingTerminalFrame; }
    int ContactTicksRemaining() const { return phase_ == DemoPlaybackPhase::SettlingContacts ? ticks_ : 0; }
    bool BeginOrAdvanceContactSettle(int timeoutTicks) {
        if (phase_ == DemoPlaybackPhase::Inputs) {
            phase_ = DemoPlaybackPhase::SettlingContacts;
            ticks_ = timeoutTicks;
            return true;
        }
        if (phase_ == DemoPlaybackPhase::SettlingContacts && ticks_ > 0) --ticks_;
        return false;
    }
    void BeginTerminalHold(int ticks) { phase_ = DemoPlaybackPhase::HoldingTerminalFrame; ticks_ = ticks; }
    bool AdvanceTerminalHold() {
        if (phase_ == DemoPlaybackPhase::HoldingTerminalFrame && --ticks_ == 0)
            phase_ = DemoPlaybackPhase::RestoreDue;
        return phase_ == DemoPlaybackPhase::RestoreDue;
    }
    void Reset() { phase_ = DemoPlaybackPhase::Inputs; ticks_ = 0; }
private:
    DemoPlaybackPhase phase_ = DemoPlaybackPhase::Inputs;
    int ticks_ = 0;
};

enum class DemoTutorialReturnPhase { Inactive, AwaitingRestore, RestoredAwaitingAck, FailedAwaitingAck };
class DemoTutorialReturn {
public:
    bool Requested() const { return phase_ != DemoTutorialReturnPhase::Inactive; }
    bool Restored() const { return phase_ == DemoTutorialReturnPhase::RestoredAwaitingAck; }
    bool HasResult() const { return Restored() || phase_ == DemoTutorialReturnPhase::FailedAwaitingAck; }
    void Begin(bool requested) { phase_ = requested ? DemoTutorialReturnPhase::AwaitingRestore : DemoTutorialReturnPhase::Inactive; }
    void Publish(bool restored) {
        if (phase_ == DemoTutorialReturnPhase::AwaitingRestore)
            phase_ = restored ? DemoTutorialReturnPhase::RestoredAwaitingAck : DemoTutorialReturnPhase::FailedAwaitingAck;
    }
    void Reset() { phase_ = DemoTutorialReturnPhase::Inactive; }
private:
    DemoTutorialReturnPhase phase_ = DemoTutorialReturnPhase::Inactive;
};

enum class RecorderComboEndAnchor { None, DirectStep, EntityContact };
class RecorderComboEnd {
public:
    bool Pending() const { return anchor_ != RecorderComboEndAnchor::None; }
    bool IsDirect() const { return anchor_ == RecorderComboEndAnchor::DirectStep; }
    bool IsEntity() const { return anchor_ == RecorderComboEndAnchor::EntityContact; }
    int Step() const { return step_; }
    uint32_t EntitySequence() const { return sequence_; }
    int AfterAction() const { return afterAction_ >= 0 ? afterAction_ : step_; }
    void SetAfterAction(int action) { afterAction_ = action; }
    void SelectStep(int step) { Reset(); anchor_ = RecorderComboEndAnchor::DirectStep; step_ = step; }
    void SelectEntity(uint32_t sequence) { Reset(); anchor_ = RecorderComboEndAnchor::EntityContact; sequence_ = sequence; }
    void Reset() { anchor_ = RecorderComboEndAnchor::None; step_ = -1; sequence_ = 0; afterAction_ = -1; }
private:
    RecorderComboEndAnchor anchor_ = RecorderComboEndAnchor::None;
    int step_ = -1;
    uint32_t sequence_ = 0;
    int afterAction_ = -1;
};

enum class PendingEntryPhase { Idle, WaitingForMatch, CharacterSelectObserved };
class PendingEntry {
public:
    bool Pending(std::memory_order order = std::memory_order_seq_cst) const { return phase_.load(order) != PendingEntryPhase::Idle; }
    bool SawCharacterSelect() const { return phase_.load(std::memory_order_acquire) == PendingEntryPhase::CharacterSelectObserved; }
    void SetPending(bool pending, std::memory_order order = std::memory_order_seq_cst) {
        phase_.store(pending ? PendingEntryPhase::WaitingForMatch : PendingEntryPhase::Idle, order);
    }
    void ObserveCharacterSelect() {
        auto expected = PendingEntryPhase::WaitingForMatch;
        (void)phase_.compare_exchange_strong(expected, PendingEntryPhase::CharacterSelectObserved,
                                             std::memory_order_release, std::memory_order_relaxed);
    }
    void ClearCharacterSelect() {
        auto expected = PendingEntryPhase::CharacterSelectObserved;
        (void)phase_.compare_exchange_strong(expected, PendingEntryPhase::WaitingForMatch,
                                             std::memory_order_release, std::memory_order_relaxed);
    }
private:
    std::atomic<PendingEntryPhase> phase_{PendingEntryPhase::Idle};
};

enum class MatchupSelectionPhase { Empty, Ready, Queued };
class MatchupSelection {
public:
    bool HasCharacters() const { return phase_.load() != MatchupSelectionPhase::Empty; }
    bool Queued() const { return phase_.load() == MatchupSelectionPhase::Queued; }
    void Publish() { phase_.store(MatchupSelectionPhase::Ready); }
    void Queue() {
        auto expected = MatchupSelectionPhase::Ready;
        (void)phase_.compare_exchange_strong(expected, MatchupSelectionPhase::Queued);
    }
    void Reset(std::memory_order order = std::memory_order_seq_cst) { phase_.store(MatchupSelectionPhase::Empty, order); }
private:
    std::atomic<MatchupSelectionPhase> phase_{MatchupSelectionPhase::Empty};
};

enum class RecorderCommandHandoffPhase { Idle, AwaitingCommand, AwaitingNeutralPolls };
class RecorderCommandHandoff {
public:
    bool Pending() const { return phase_.load(std::memory_order_acquire) != RecorderCommandHandoffPhase::Idle; }
    bool WaitingForNeutral() const { return phase_.load(std::memory_order_acquire) == RecorderCommandHandoffPhase::AwaitingNeutralPolls; }
    void Begin() { phase_.store(RecorderCommandHandoffPhase::AwaitingCommand, std::memory_order_release); }
    void CommandCompleted() {
        auto expected = RecorderCommandHandoffPhase::AwaitingCommand;
        (void)phase_.compare_exchange_strong(expected, RecorderCommandHandoffPhase::AwaitingNeutralPolls,
                                             std::memory_order_release, std::memory_order_relaxed);
    }
    void Reset() { phase_.store(RecorderCommandHandoffPhase::Idle, std::memory_order_release); }
private:
    std::atomic<RecorderCommandHandoffPhase> phase_{RecorderCommandHandoffPhase::Idle};
};

enum class RecorderCaptureMenuPhase { Closed, OpenRequested, OpenSuspended, ReleaseRequested, ReleaseSuspended, Draining };
class RecorderCaptureMenu {
public:
    bool IsOpen() const { const auto phase = Load(); return phase == Phase::OpenRequested || phase == Phase::OpenSuspended; }
    bool Suspended() const { return HasSuspension(Load()); }
    bool HasReleaseRequest() const { const auto phase = Load(); return phase == Phase::ReleaseRequested || phase == Phase::ReleaseSuspended; }
    bool InputBlocked() const { return Load() != Phase::Closed; }
    void Open() {
        auto phase = Load();
        while (!phase_.compare_exchange_weak(phase, HasSuspension(phase) ? Phase::OpenSuspended : Phase::OpenRequested,
                                             std::memory_order_acq_rel)) {}
    }
    void Close(bool needsRelease) {
        auto phase = Load();
        for (;;) {
            const auto next = needsRelease
                ? (HasSuspension(phase) ? Phase::ReleaseSuspended : Phase::ReleaseRequested)
                : (phase == Phase::OpenSuspended ? Phase::Draining
                    : phase == Phase::OpenRequested ? Phase::Closed : phase);
            if (phase_.compare_exchange_weak(phase, next, std::memory_order_acq_rel)) return;
        }
    }
    bool ConsumeRelease() {
        auto phase = Load();
        while (phase == Phase::ReleaseRequested || phase == Phase::ReleaseSuspended) {
            const auto next = phase == Phase::ReleaseSuspended ? Phase::Draining : Phase::Closed;
            if (phase_.compare_exchange_weak(phase, next, std::memory_order_acq_rel)) return true;
        }
        return false;
    }
    void AcquireSuspension() {
        auto phase = Load();
        for (;;) {
            const auto next = phase == Phase::OpenRequested ? Phase::OpenSuspended
                : phase == Phase::ReleaseRequested ? Phase::ReleaseSuspended
                : phase == Phase::Closed ? Phase::Draining : phase;
            if (phase_.compare_exchange_weak(phase, next, std::memory_order_acq_rel)) return;
        }
    }
    void EndSuspension() {
        auto phase = Load();
        for (;;) {
            const auto next = phase == Phase::OpenSuspended ? Phase::OpenRequested
                : phase == Phase::ReleaseSuspended ? Phase::ReleaseRequested
                : phase == Phase::Draining ? Phase::Closed : phase;
            if (phase_.compare_exchange_weak(phase, next, std::memory_order_acq_rel)) return;
        }
    }
    void Reset() { phase_.store(Phase::Closed, std::memory_order_release); }
private:
    using Phase = RecorderCaptureMenuPhase;
    static bool HasSuspension(Phase phase) { return phase == Phase::OpenSuspended || phase == Phase::ReleaseSuspended || phase == Phase::Draining; }
    Phase Load() const { return phase_.load(std::memory_order_acquire); }
    std::atomic<Phase> phase_{Phase::Closed};
};

enum class RecorderStopPhase { Idle, AwaitingCaptureTick, AwaitingMacroBoundary };
class RecorderStop {
public:
    bool Pending() const { return phase_ != RecorderStopPhase::Idle; }
    bool Request() {
        if (Pending()) return false;
        phase_ = RecorderStopPhase::AwaitingCaptureTick;
        return true;
    }
    bool ObserveCaptureTick() {
        if (phase_ == RecorderStopPhase::AwaitingCaptureTick) {
            phase_ = RecorderStopPhase::AwaitingMacroBoundary;
            return false;
        }
        return phase_ == RecorderStopPhase::AwaitingMacroBoundary;
    }
    void Reset() { phase_ = RecorderStopPhase::Idle; }
private:
    RecorderStopPhase phase_ = RecorderStopPhase::Idle;
};

enum class RecorderCountInPhase { Counting, AwaitingInputLease };
class RecorderCountIn {
public:
    int Frames() const { return frames_; }
    int Target() const { return target_; }
    int Remaining() const { return target_ > frames_ ? target_ - frames_ : 0; }
    bool WaitingForLease() const { return phase_ == RecorderCountInPhase::AwaitingInputLease; }
    void Begin(int ticks) { Reset(); target_ = ticks; }
    void Restart() { frames_ = 0; phase_ = RecorderCountInPhase::Counting; }
    void Reset() { Restart(); target_ = 0; }
    bool Advance(bool frozen) {
        if (!WaitingForLease()) {
            if (!frozen && frames_ < target_) ++frames_;
            if (frames_ >= target_) phase_ = RecorderCountInPhase::AwaitingInputLease;
        }
        return WaitingForLease();
    }
private:
    RecorderCountInPhase phase_ = RecorderCountInPhase::Counting;
    int frames_ = 0;
    int target_ = 0;
};

enum class RunnerBaselinePhase { Empty, WaitingToSave, WaitingToRestore, Restoring, Ready, Unavailable };
class RunnerBaseline {
public:
    bool SavePending() const { return phase_ == RunnerBaselinePhase::WaitingToSave; }
    bool RestorePending() const { return phase_ == RunnerBaselinePhase::WaitingToRestore; }
    bool Ready() const { return phase_ == RunnerBaselinePhase::Ready; }
    void Begin(bool restore) { phase_ = restore ? RunnerBaselinePhase::WaitingToRestore : RunnerBaselinePhase::WaitingToSave; }
    void BeginRestore() { phase_ = RunnerBaselinePhase::Restoring; }
    void Complete(bool success) { phase_ = success ? RunnerBaselinePhase::Ready : RunnerBaselinePhase::Unavailable; }
    void Reset() { phase_ = RunnerBaselinePhase::Empty; }
private:
    RunnerBaselinePhase phase_ = RunnerBaselinePhase::Empty;
};

}
