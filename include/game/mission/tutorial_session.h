#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct ImDrawList;

namespace Mission { struct Mission; }
namespace Mission::Engine { struct Snapshot; }

namespace Mission::TutorialSession {

enum class Phase : int {
    Idle = 0,
    Preparing,    // runner setup/baseline is not ready; no visible controls
    Intro,        // reading pages (frozen; no attempt can start)
    ConfirmExit,  // page-1 cancel: RETURN TO LESSONS? (frozen)
    NeutralGate,  // release-to-neutral before arming a task
    TaskActive,   // combat task armed; gameplay input live
    Choice,       // choice task (frozen, controller-navigable)
    Feedback,     // success/failure line before checkpoint restore
    Review,       // re-reading pages mid-lesson (frozen; returns to task)
    DemoSuspended,// demonstration owns input/presentation; returns via neutral
    Complete,     // LESSON COMPLETE menu (frozen)
    Error,        // checkpoint/setup failure; frozen until restart/return
};

// DemoSuspended blocks menu commands even after Demo::IsActive() goes false: the restore result
// must be consumed and freeze/neutral acquired first.
constexpr bool SessionMenuAllowed(Phase phase) {
    return phase != Phase::Preparing && phase != Phase::DemoSuspended;
}

constexpr int PersistedAttemptDelta(int attemptsThisRun) {
    return attemptsThisRun > 0 ? attemptsThisRun : 0;
}

// Directions through here would let the Up/Back that closed a surface become a jump on resume:
// every gameplay bit stays suppressed until a physically neutral poll.
constexpr uint8_t NeutralGatePublishedMask(uint8_t) {
    return 0;
}

constexpr bool NeutralGatePollIsReleased(bool sampleValid,
                                         uint8_t physicalPoll) {
    return sampleValid && physicalPoll == 0;
}

constexpr int NeutralGateRequiredFreshPolls() { return 2; }

constexpr uint32_t NeutralGateObservedSerialBaseline(bool sampleValid,
                                                     uint32_t observedSerial) {
    return sampleValid ? observedSerial : 0;
}

constexpr int NeutralGateNeutralPollCount(bool sampleValid, bool freshSample,
                                          uint8_t physicalPoll,
                                          int previousCount) {
    if (!sampleValid || !freshSample) return previousCount;
    return physicalPoll == 0 ? previousCount + 1 : 0;
}

struct DemoResumeTarget {
    Phase phase = Phase::TaskActive;
    int task = -1;
};

constexpr DemoResumeTarget DecideDemoResumeTarget(
    Phase sourcePhase, bool feedbackSuccess, bool completionMet,
    int currentTask, int firstPendingTask) {
    if (sourcePhase == Phase::Intro || sourcePhase == Phase::Review ||
        sourcePhase == Phase::ConfirmExit) {
        return {sourcePhase, -1};
    }
    if (sourcePhase == Phase::Feedback && !feedbackSuccess) {
        return {Phase::TaskActive, currentTask};
    }
    if (sourcePhase == Phase::Complete || completionMet || firstPendingTask < 0) {
        return {Phase::Complete, -1};
    }
    return {Phase::TaskActive, firstPendingTask};
}

constexpr bool ShouldCarryCommitEdge(bool frozenSample,
                                     bool nextRequiresCommit,
                                     bool nextInputMatches) {
    return frozenSample && nextRequiresCommit && nextInputMatches;
}

constexpr bool ShouldRejectUnprovenCommit(bool destinationEdge,
                                          bool requiresCommit,
                                          bool committedInput) {
    return destinationEdge && requiresCommit && !committedInput;
}

// Keep the freeze until the closing UI input is released: a held EFZ A/B could become an attack
// on the first live poll.
constexpr bool ShouldResumeReviewAfterRelease(bool resumePending,
                                              bool inputActive,
                                              bool anyUiInputHeld) {
    return resumePending && inputActive && !anyUiInputHeld;
}

constexpr bool FlickerICProofSatisfied(bool destinationEdge,
                                       bool sourceMatches,
                                       bool committedInput,
                                       bool sourceDistanceSafe,
                                       bool projectileAlive,
                                       bool contactSeen,
                                       bool defenderUntouched) {
    return destinationEdge && sourceMatches && committedInput &&
           sourceDistanceSafe && projectileAlive && !contactSeen &&
           defenderUntouched;
}

inline bool IsCarryContinuity(const std::string& continuity) {
    return continuity == "continue";
}

inline int ContinuationRetryHead(const std::vector<std::string>& continuities,
                                 int currentTask) {
    if (currentTask < 0 || currentTask >= static_cast<int>(continuities.size())) {
        return currentTask;
    }
    while (currentTask > 0 && IsCarryContinuity(continuities[currentTask])) {
        --currentTask;
    }
    return currentTask;
}

constexpr bool IsNewForbiddenAbsenceAttack(bool moveInstanceEdge,
                                           int moveId) {
    return moveInstanceEdge && moveId >= 200;
}

constexpr bool IsDummyAttackStart(short previousMove, short currentMove) {
    return previousMove < 200 && currentMove >= 200;
}

constexpr bool IsDummyAttackEnd(bool attackSeen, short previousMove,
                                short currentMove) {
    return attackSeen && previousMove >= 200 && currentMove < 200;
}

bool Begin(const ::Mission::Mission& lesson);
void End(const char* reason);
void NotifyStartupFailure(const std::string& message);
void NotifyStateLoaded();
bool IsActive();
Phase GetPhase();
bool HasStartupInputOwnership();

// Monitor-thread tick (called from Engine::Tick in place of TickRun).
void Tick(const ::Mission::Engine::Snapshot& s);

bool CommandRetryTask();
bool CommandRestartLesson();
bool CommandReviewLesson();
bool CommandPlayDemo(std::string& message);
bool CommandNextLesson(std::string& message);
bool LessonCleared();
std::string NextLessonName();
std::string ProgressLabel();
bool CanRetryCurrentTask();

void BeginLessonRegistryRefresh();
void RegisterLessonPath(const std::string& packId, const std::string& lessonId,
                        const std::string& path,
                        const std::string& displayName,
                        const std::string& nextLessonId,
                        bool available = true);
void FinishLessonRegistryRefresh();

// render thread
bool WantsDraw();
void Draw(void* device, ImDrawList* dl);
// Called only after the EndScene hook is detached; releases the Win32-TLS
// snapshot cache without adding a PE TLS directory on XP.
void ReleaseRenderThreadState();

}
