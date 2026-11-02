#pragma once

#include <cstdint>
#include <array>
#include <string>
#include <vector>

#include "contact_event.h"

namespace Mission {
struct Mission;
namespace Authoring { struct MissionMetadata; }
}

namespace Mission::Engine {

struct Snapshot {
    // Must match the producer journal capacity; smaller drains discard dense barrages.
    static constexpr size_t kMaxContactEvents = 128;
    static constexpr size_t kMaxPolledAttackEdges = 128;
    struct PolledAttackEdge {
        uint32_t serial = 0;
        uint8_t mask = 0;
    };
    bool  valid = false;
    short p1Move = 0;      // P1 current move-ID (playerBase + 0x8)
    short p2Move = 0;
    short p2FrameIdx = 0;
    int   p1Combo = 0;     // attacker combo-hit counter (+0x174)
    int   p1ComboDamage = 0; // Combo damage (+0x100)
    uint8_t p1Inputs = 0;
    uint8_t p1PolledAttackEdges = 0;
    uint32_t p1PolledAttackEdgeSerial = 0;
    std::array<PolledAttackEdge, kMaxPolledAttackEdges>
        p1PolledAttackEdgeEvents{};
    uint8_t p1PolledAttackEdgeCount = 0;
    uint32_t p1PolledAttackEdgesDropped = 0;
    bool p1PolledAttackEdgeOverflow = false;
    uint32_t p1InputPollSerial = 0;
    bool  p1Attacking = false;
    bool  p2InStun = false;
    short p1Token = 99;        // engine command index (+0x262, MOTION_TOKEN_OFFSET; 99 = none), facing-independent
    short p1FrameIdx = 0;      // frame index within current move (+0xA); reset marks same-move re-entry
    // superflash (+0x14C) or contact hitstop (+0x14A); mission timers must not tick during it
    bool freezeActive = false;
    // raw +0x168 producer latch; values overlap producers and are not a typed-contact result
    int p1HitState = 0;
    std::array<::Mission::Contact::Event, kMaxContactEvents> contactEvents{};
    uint8_t contactEventCount = 0;
    uint32_t contactEventEpoch = 0;
    uint32_t completedBattleUpdateBatch = 0;
    bool contactEventOverflow = false;
    bool directContactHookReady = false;
    bool entityContactHookReady = false;
};

void Tick();

void SetPendingMissionMode(bool on);

bool SetPendingMissionLoad(const std::string& missionPath, std::string& errorOut);

bool ValidateMissionRuntimeReadiness(const ::Mission::Mission& mission,
                                     const std::string& sourcePath,
                                     std::string& outError);

bool CancelPendingMissionLoad(const char* reason);

namespace PendingLaunchPolicy {
    enum class CancelEffect : uint8_t {
        PreserveGenericBrowser,
        ClearSpecificMission,
    };

    constexpr CancelEffect DecideCancel(bool hasSpecificMission) {
        return hasSpecificMission
            ? CancelEffect::ClearSpecificMission
            : CancelEffect::PreserveGenericBrowser;
    }

    enum class ConsumeEffect : uint8_t {
        OpenGenericBrowser,
        LoadPreparedMission,
        RejectIncompleteTransaction,
    };

    // Path and value publish under one mutex; partial states must fail visibly.
    constexpr ConsumeEffect DecideConsume(bool hasSelectedMissionPath,
                                           bool hasPreparedMission) {
        if (hasSelectedMissionPath && hasPreparedMission) {
            return ConsumeEffect::LoadPreparedMission;
        }
        if (!hasSelectedMissionPath && !hasPreparedMission) {
            return ConsumeEffect::OpenGenericBrowser;
        }
        return ConsumeEffect::RejectIncompleteTransaction;
    }

    constexpr bool ReachedSelectorExit(bool missionModePending,
                                       bool sawCharacterSelect,
                                       bool nowAtMenu) {
        return missionModePending && sawCharacterSelect && nowAtMenu;
    }

    // One zero-poll owner must span title transaction through runner
    // restore/save with no release frame between.
    constexpr bool StartupInputOwned(bool concreteTitlePending,
                                     bool runnerRestorePending,
                                     bool runnerSavePending) {
        return concreteTitlePending || runnerRestorePending || runnerSavePending;
    }

    constexpr bool CanReleaseStartupInput(bool concreteTitlePending,
                                          bool runnerRestorePending,
                                          bool runnerSavePending,
                                          bool tutorialActive,
                                          bool tutorialOwnerEstablished) {
        return !StartupInputOwned(concreteTitlePending,
                                  runnerRestorePending,
                                  runnerSavePending) &&
               (!tutorialActive || tutorialOwnerEstablished);
    }
}

namespace StartupFailurePolicy {
    enum class Effect : uint8_t {
        AbortRunner,
        RetainTutorialError,
    };

    constexpr Effect Decide(bool isTutorial) {
        return isTutorial ? Effect::RetainTutorialError : Effect::AbortRunner;
    }
}

namespace StartupRestorePolicy {
    constexpr bool CanAttempt(bool worldSettled,
                              bool practiceControllerReady) {
        return worldSettled && practiceControllerReady;
    }
}

void SetPendingRecordMode(bool on);

// Called by the savestate hook after any Revival state restore; the runner's
// own auto-retry loads are filtered out internally.
void NotifyStateLoaded();

void NotifyPracticeSessionReset(const char* reason);

Snapshot GetSnapshot();

void SetInspectorEnabled(bool on);
bool IsInspectorEnabled();

namespace Recorder {
    enum class Phase : uint8_t { Idle, PreRecord, CountIn, Recording, Review };

    constexpr bool UsesDedicatedPauseMenu(Phase phase) {
        return phase != Phase::Idle;
    }

    constexpr bool IsCapturePhase(Phase phase) {
        return phase == Phase::CountIn || phase == Phase::Recording;
    }

    constexpr bool NeedsCaptureMenuHandoff(bool suspensionOwned, Phase phase) {
        return suspensionOwned && IsCapturePhase(phase);
    }

    enum class AdvanceEffect : uint8_t {
        None,
        BeginCountIn,
        CancelCountIn,
        StopToReview,
        KeepReview,
    };

    constexpr AdvanceEffect DecideAdvance(Phase phase) {
        switch (phase) {
            case Phase::PreRecord: return AdvanceEffect::BeginCountIn;
            case Phase::CountIn:   return AdvanceEffect::CancelCountIn;
            case Phase::Recording: return AdvanceEffect::StopToReview;
            case Phase::Review:    return AdvanceEffect::KeepReview;
            case Phase::Idle:
            default:               return AdvanceEffect::None;
        }
    }

    // Commands are consumed by Tick() on the frame-monitor thread.
    void Arm();
    void Advance();
    void Retake();
    void Cancel();
    void SetActive(bool on);
    bool IsActive();
    bool IsSessionActive();    // PreRecord, CountIn, Recording, or Review
    bool OwnsCaptureHotkeys();
    void SetCaptureMenuOpen(bool open);
    bool IsCaptureMenuSuspended();
    bool IsMenuInputHandoffActive();
    void BeginMenuCommandHandoff();
    Phase GetPhase();
    int  GetStepCount();
    int  GetComboEndCount();
    int  GetCountInValue();    // remaining milliseconds while counting down
    bool HasPreviewableTake();

    std::string GetMacroRecordBindingLabel();

    bool BuildDraft(::Mission::Mission& out);

    bool SaveRecorded(std::string& outMsg);
    bool SaveRecordedDraft(const ::Mission::Authoring::MissionMetadata& metadata,
                           std::string& outMsg);

    bool CanPublishRecorded(std::string& outReason);

    bool PublishRecorded(const std::string& packJsonPath,
                         const std::string& categoryId,
                         const ::Mission::Authoring::MissionMetadata& metadata,
                         std::string& outMsg);
}

namespace Demo {
    enum class Phase : uint8_t { Idle, Preparing, Playing, Restoring };

    enum class RestoreResult : uint8_t { None, Restored, RestoreFailed };

    constexpr bool RequiresTutorialRestoreAcknowledgement(
        bool requested, bool fromRecorder, bool tutorialSessionActive) {
        return requested && !fromRecorder && tutorialSessionActive;
    }

    bool PlayLoaded(std::string& outMsg, bool tutorialHandoff = false);
    bool PlayRecording(std::string& outMsg);
    void Cancel();
    bool IsActive();
    Phase GetPhase();

    RestoreResult PeekTutorialRestore(std::string& outMessage);
    void AcknowledgeTutorialRestore();
}

bool IsPracticeAutomationSuppressed();

namespace Runner {
    enum class Phase { Idle, InProgress, Complete, Dropped };

    bool Load(const std::string& missionPath, std::string& outMsg,
              bool forceFreshMatch = true);
    bool LoadLatestRecorded(std::string& outMsg);
    void Unload();
    // Logical-only retry for legacy v1/v2 schedules; strict-v3 entity schedules
    // require RequestBaselineRestore instead.
    void Reset();
    bool RequestBaselineRestore(std::string& outMsg);
    bool HasBaseline();

    bool  IsActive();
    Phase GetPhase();
    bool  HasDemo();
    int   CurrentStep();
    int   FailedStep();    // latched failed recipe index (-1 until/after a clean retry)
    int   StepCount();
    int   Attempts();
    int   BestTier();      // highest score tier met on the completing run (-1 = none)
    int   CurrentStepHits();
    bool  CurrentStepArmed();
    bool  IsReadyForPlayer();

    bool GetRenderSnapshot(::Mission::Mission& missionOut, int& currentStepOut,
                           int& failedStepOut, bool& armedOut, int& currentHitsOut,
                           std::vector<int>* entityContactsSeenOut = nullptr,
                           std::vector<int>* entityComboHitsSeenOut = nullptr,
                           int* entitySegmentOut = nullptr,
                           int* failedEntityRequirementOut = nullptr,
                           bool requireVisibleRecipe = false,
                           std::vector<int>* entityLifecyclesSeenOut = nullptr);
}

}
