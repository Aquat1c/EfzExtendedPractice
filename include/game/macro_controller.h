#pragma once

#include <cstdint>
#include <string>
struct EfzTmEntryV1;

namespace MacroController {
    namespace PlaybackFinishPolicy {
        enum class Phase { Streaming, AwaitingFinalPoll, AwaitingClearTick, Neutralizing, Guarding };
        constexpr Phase AfterFinalPoll(uint32_t initial, uint32_t current, int waited) {
            return current != initial || waited >= 12
                ? Phase::AwaitingClearTick : Phase::AwaitingFinalPoll;
        }
        constexpr Phase AfterClearTick(int frameDiv) {
            return frameDiv == 0 ? Phase::Neutralizing : Phase::AwaitingClearTick;
        }
        constexpr Phase AfterNeutral(int remaining) {
            return remaining > 0 ? Phase::Neutralizing : Phase::Guarding;
        }
        constexpr bool GuardFinished(bool activated, int remaining) {
            return activated || remaining <= 0;
        }
    }


enum class State : uint8_t {
    Idle = 0,
    PreRecord,
    Recording,
    Replaying
};

enum class PlaybackStartMode : uint8_t {
    Ordinary = 0,
    PrimeBeforeNextPoll,
};

// Trigger MACRO SLOT value after the real slots: roll a recorded slot.
constexpr int kRandomSlotChoice = 9;

namespace PlaybackStartPolicy {
    // The roll-th recorded slot, 1-based; 0 when nothing is recorded.
    inline int PickFilledSlot(const bool* filled, int count, unsigned roll) {
        int filledCount = 0;
        for (int i = 0; i < count; ++i) if (filled[i]) ++filledCount;
        if (filledCount == 0) return 0;
        int remaining = static_cast<int>(roll % static_cast<unsigned>(filledCount));
        for (int i = 0; i < count; ++i) {
            if (filled[i] && remaining-- == 0) return i + 1;
        }
        return 0;
    }

    struct Decision {
        bool valid;
        bool prime;
        int tickToPrepare;
        int cursorAfterAdmission;
    };

    constexpr Decision Decide(PlaybackStartMode mode, int startTick,
                              int streamTicks) {
        const bool valid = startTick >= 0 && startTick < streamTicks;
        const bool prime = valid &&
            mode == PlaybackStartMode::PrimeBeforeNextPoll;
        return {valid, prime, startTick, startTick + (prime ? 1 : 0)};
    }

    // Admission is not a gameplay subframe: only the first third of tick zero's writes go before the
    // first poll. Never collapse them all, even while the mission transition freezes the world.
    constexpr int WritesForSubframe(int writesLeft, int subframesLeft) {
        return writesLeft > 0 && subframesLeft > 0
            ? (writesLeft + subframesLeft - 1) / subframesLeft
            : 0;
    }

    // One distinct raw state per 192 Hz subframe; larger groups would lose motion samples, so reject them.
    constexpr bool SupportedRawWriteCount(int writes) {
        return writes >= 0 && writes <= 3;
    }

    constexpr bool CanPublishNextPrimedSlice(bool awaitingFirstPoll,
                                              bool pollAdvanced,
                                              bool bufferAdvanced,
                                              bool frameStepAdvanced) {
        return !awaitingFirstPoll &&
               (pollAdvanced || bufferAdvanced || frameStepAdvanced);
    }

    constexpr bool PrimedTickPending(int subframesRemaining,
                                     int queuedWrites,
                                     int writesLeft) {
        return subframesRemaining > 0 || queuedWrites > 0 || writesLeft > 0;
    }

    constexpr bool PrimedSliceCanLatchEnd(
        int subframesRemainingBeforePublish) {
        return subframesRemainingBeforePublish <= 1;
    }

    // The serialized mask owns the whole 64 Hz tick. Monitor passes don't reliably interleave with EFZ's
    // three polls, so a raw sample must never replace it, or {A, B, neutral} depends on scheduling.
    constexpr uint8_t PublishMonitorSlice(uint8_t logicalMask,
                                          uint8_t rawMask,
                                          uint8_t directionMask,
                                          uint8_t buttonMask) {
        (void)rawMask;
        (void)directionMask;
        (void)buttonMask;
        return logicalMask;
    }

    constexpr bool StreamEndCanLatch(int cursor, int streamTicks,
                                     int queuedWrites, int writesLeft) {
        return cursor >= streamTicks && queuedWrites == 0 && writesLeft == 0;
    }
}

struct SlotStats {
    int spanCount;
    int totalTicks;
    int bufEntries;
    int bufTicks;
    int bufIndexTicks;
    uint16_t bufStartIdx;
    uint16_t bufEndIdx;
    bool hasData;
};

// Call once per internal frame from the Frame Monitor (Match phase only)
void Tick();
bool DidAdvanceRecordingTick();

// While suspended neither the timeline nor the buffer cursor advances; release rebases the cursor
// to the frozen buffer head so menu input never becomes clip input.
void SetRecordingCaptureSuspended(bool suspended);
bool IsRecordingCaptureSuspended();

void ToggleRecord();   // Idle -> PreRecord -> Recording -> Idle(stop)
void Play();
void PlayFromTick(int startTick);
bool PlayForPlayer(int playerNum, int startTick = 0,
                   bool exclusiveInput = false);
void Stop();
void UnswapThenStop();
void ResetTimeline();
bool BindPracticeWorld(const EfzTmEntryV1& world);
void CancelPracticeWork();
uint32_t RetirePracticeWorld(const EfzTmEntryV1& heldWorld);

bool BeginPlayerRecording(int playerNum, bool switchLocalControl = false);
bool StartPlayerRecording();
bool FinishPlayerRecording();
bool FinishPlayerRecordingAndSerialize(std::string& serializedOut,
                                       bool includeBuffers);
void RequestRecordingStopAtBoundary();
std::string SerializeLastPlayerRecording(bool includeBuffers);

// Never touches the eight user slots. With exclusiveInput, callers must gate their frontend so
// only an explicit cancel command is accepted.
bool PlaySerializedForPlayer(const std::string& text, int playerNum,
                             bool exclusiveInput, std::string& errorOut,
                             PlaybackStartMode startMode =
                                 PlaybackStartMode::Ordinary);
bool ValidateSerialized(const std::string& text, std::string& errorOut);
bool IsExclusivePlayback();
int  GetPlaybackPlayer();
void ReleaseExclusivePlaybackHold();

int  GetCurrentSlot();           // 1-based slot index
void SetCurrentSlot(int slot);
void NextSlot();
void PrevSlot();
int  GetSlotCount();
bool IsSlotEmpty(int slot);
int  PickRandomFilledSlot();     // 0 when no slot is recorded
void SetRandomPlay(bool enabled);
bool IsRandomPlay();
inline bool IsCurrentSlotEmpty() { return IsSlotEmpty(GetCurrentSlot()); }

State GetState();
std::string GetStatusLine();

SlotStats GetSlotStats(int slot);

// Last non-neutral tick + 1; excludes trailing neutral inputs.
int GetEffectiveTicks(int slot);

// 0-based tick of the first attack button press (A/B/C/D), or -1 if none.
int GetFirstButtonTick(int slot);

uint8_t GetFirstAttackInput(int slot);

// "EFZMACRO 1", then space-separated 64 Hz tick tokens: numpad + buttons (6A, N), optional xN repeat,
// optional {k: v1 v2 ...} group of k raw buffer writes. Without a group: one write of the tick mask.
std::string SerializeSlot(int slot, bool includeBuffers);

bool DeserializeSlot(int slot, const std::string& text, std::string& errorOut);

}
