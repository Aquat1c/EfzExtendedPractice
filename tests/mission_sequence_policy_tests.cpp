#include "game/mission/mission_sequence_policy.h"
#include "game/mission/mission_engine.h"
#include "game/mission/mission_setup.h"
#include "game/mission/contact_event.h"
#include "game/mission/recorder_entity_trace.h"
#include "game/mission/mission_semantic_source_policy.h"
#include "game/mission/savestate_entity_layout.h"
#include "utils/pause_integration.h"

#include <cstring>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void Check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}

}

int main() {
    using namespace Mission::SequencePolicy;

    Check(RecorderCountInTicksFromMs(0) == 0,
          "zero milliseconds disables the visible authoring count-in");
    Check(RecorderCountInTicksFromMs(500) == 96,
          "the default half-second count-in is exactly 96 monitor ticks");
    Check(RecorderCountInTicksFromMs(3000) == 576,
          "the maximum three-second count-in retains the legacy duration");
    Check(RecorderCountInRemainingMs(96) == 500 &&
          RecorderCountInRemainingMs(1) == 6,
          "count-in HUD time rounds upward and never announces an early start");
    Check(RecorderMenuNeutralPollCount(true, true, 0, 1) == 0 &&
          RecorderMenuNeutralPollCount(false, false, 0, 1) == 1 &&
          RecorderMenuNeutralPollCount(false, true, 4, 1) == 0 &&
          RecorderMenuNeutralPollCount(false, true, 0, 0) == 1 &&
          RecorderMenuNeutralPollCount(false, true, 0, 1) == 2 &&
          !RecorderMenuReleaseReady(true, 2) &&
          !RecorderMenuReleaseReady(false, 1) &&
          RecorderMenuReleaseReady(false, 2),
          "recorder menus resume only after both UI surfaces close and two fresh neutral polls");
    Check(!RecorderPhysicalPauseReleaseReady(false, false) &&
          !RecorderPhysicalPauseReleaseReady(true, true) &&
          RecorderPhysicalPauseReleaseReady(true, false) &&
          !RecorderMenuReleaseReady(false, 0),
          "a completed hidden handoff releases physical pause before native neutral polls release its input lease");
    using PauseSurface = PauseIntegration::MenuSurface;
    constexpr uint8_t recorderPause = PauseIntegration::UpdatedMenuSurfaceMask(
        0, PauseSurface::MissionPause, true);
    constexpr uint8_t nestedPractice = PauseIntegration::UpdatedMenuSurfaceMask(
        recorderPause, PauseSurface::ImGui, true);
    constexpr uint8_t returnedToRecorder = PauseIntegration::UpdatedMenuSurfaceMask(
        nestedPractice, PauseSurface::ImGui, false);
    constexpr uint8_t resumeHandoff = PauseIntegration::UpdatedMenuSurfaceMask(
        returnedToRecorder, PauseSurface::RecorderHandoff, true);
    constexpr uint8_t hiddenDuringHandoff = PauseIntegration::UpdatedMenuSurfaceMask(
        resumeHandoff, PauseSurface::MissionPause, false);
    constexpr uint8_t commandAndCaptureHandoffs =
        PauseIntegration::UpdatedMenuSurfaceMask(
            hiddenDuringHandoff, PauseSurface::RecorderCommandHandoff, true);
    constexpr uint8_t commandHandoffOnly =
        PauseIntegration::UpdatedMenuSurfaceMask(
            commandAndCaptureHandoffs, PauseSurface::RecorderHandoff, false);
    constexpr uint8_t demoTransitionNested =
        PauseIntegration::UpdatedMenuSurfaceMask(
            commandHandoffOnly, PauseSurface::DemoTransition, true);
    constexpr uint8_t demoTransitionOnly =
        PauseIntegration::UpdatedMenuSurfaceMask(
            demoTransitionNested,
            PauseSurface::RecorderCommandHandoff, false);
    Check(recorderPause != 0 && nestedPractice != 0 &&
          returnedToRecorder == recorderPause &&
          hiddenDuringHandoff != 0 &&
          commandAndCaptureHandoffs != hiddenDuringHandoff &&
          commandHandoffOnly == PauseIntegration::MenuSurfaceBit(
              PauseSurface::RecorderCommandHandoff) &&
          PauseIntegration::UpdatedMenuSurfaceMask(
              commandHandoffOnly,
              PauseSurface::RecorderCommandHandoff, false) == 0 &&
          demoTransitionOnly == PauseIntegration::MenuSurfaceBit(
              PauseSurface::DemoTransition) &&
          PauseIntegration::UpdatedMenuSurfaceMask(
              demoTransitionOnly, PauseSurface::DemoTransition, false) == 0,
          "nested Practice, recorder handoffs, and demo restore transactions transfer pause without an unowned gap");
    Check(RecorderAttackBatchIsFresh(112, 100, 12) &&
          !RecorderAttackBatchIsFresh(113, 100, 12) &&
          !RecorderAttackBatchIsFresh(99, 100, 12),
          "only a nearby pending press can classify the current follow-up");
    Check(ExpectedAttackMaskFromNotation("5A") == kAttackButtonA,
          "notation derives A strength");
    Check(ExpectedAttackMaskFromNotation("c.5B") == kAttackButtonB,
          "notation derives close B strength");
    Check(ExpectedAttackMaskFromNotation("623C finish") == kAttackButtonC,
          "notation derives C without descriptive suffix noise");
    Check(ExpectedAttackMaskFromNotation("214A/B/C") ==
              (kAttackButtonA | kAttackButtonB | kAttackButtonC),
          "notation derives alternative strengths");
    Check(ExpectedAttackMaskFromFinalNotationStep(
              "41236*~236A~236B") == kAttackButtonB &&
          ExpectedAttackMaskFromFinalNotationStep(
              "FM (S)~A~B~C~A") == kAttackButtonA &&
          ExpectedAttackMaskFromFinalNotationStep(
              "214*~5B~4/5/6") == kAttackButtonB &&
          ExpectedAttackMaskFromFinalNotationStep(
              "214A/B/C") ==
              (kAttackButtonA | kAttackButtonB | kAttackButtonC),
          "follow-up causality uses the final entered route step");
    Check(ExpectedAttackMaskFromNotation("BIC") == kAttackButtonC &&
              ExpectedAttackMaskFromNotation("j.IC") == kAttackButtonC,
          "IC-family notation derives C");
    Check(ExpectedAttackMaskFromNotation("5S") == kAttackButtonD &&
              ExpectedAttackMaskFromNotation("5S (magic charged)") ==
                  kAttackButtonD,
          "S notation maps to D without treating magic as IC");
    Check(ExpectedAttackMaskFromNotation("magic") == 0 &&
              ExpectedAttackMaskFromNotation("FM") == 0,
          "descriptive letters do not become attack strengths");
    Check(ValidExpectedAttackMask(0) &&
          ValidExpectedAttackMask(kAttackButtonA) &&
          ValidExpectedAttackMask(kAttackButtonA | kAttackButtonC) &&
          !ValidExpectedAttackMask(-1) &&
          !ValidExpectedAttackMask(1) &&
          !ValidExpectedAttackMask(256),
          "persisted causal masks accept only zero or A/B/C/D bits");
    Check(ExpectedAttackMaskForAction("", 200) == kAttackButtonA &&
          ExpectedAttackMaskForAction("", 208) == kAttackButtonB &&
          ExpectedAttackMaskForAction("", 167) == kAttackButtonC &&
          ExpectedAttackMaskForAction("FM", 313) == 0,
          "universal move IDs provide a conservative notation fallback");
    constexpr RecorderAttackEdgeCandidate causalCandidates[] = {
        {40, kAttackButtonB, 100},
        {41, kAttackButtonA, 103},
        {44, kAttackButtonB, 104},
    };
    Check(SelectNewestEligibleAttackEdge(
              causalCandidates, 3, 42, 108,
              kRecorderInputCausalityWindowTicks, kAttackButtonB) == 0,
          "newest eligible matching edge skips a fresher wrong button and a future poll");
    Check(SelectNewestEligibleAttackEdge(
              causalCandidates, 3, 42, 108,
              kRecorderInputCausalityWindowTicks, kAttackButtonA) == 1,
          "the matching fresh edge is selected for its own action strength");
    Check(SelectNewestEligibleAttackEdge(
              causalCandidates, 3, 42, 200, 12, kAttackButtonB) == -1,
          "historical matching input cannot become a later action's cause");
    Check(BoundCausalAttackMask(kAttackButtonA | kAttackButtonB,
                                kAttackButtonB) == kAttackButtonB &&
          BoundCausalAttackMask(kAttackButtonA | kAttackButtonB, 0) ==
              (kAttackButtonA | kAttackButtonB),
          "known actions persist their causal strength, while unknown actions retain the observed edge");
    Check(ShouldFoldKnownAutomaticPhase(true) &&
          !ShouldFoldKnownAutomaticPhase(false),
          "catalogued automatic phases always fold regardless of input noise");
    Check(DeadlineAttackEdgeMatches(kAttackButtonB, kAttackButtonB) &&
          !DeadlineAttackEdgeMatches(kAttackButtonA, kAttackButtonB) &&
          DeadlineAttackEdgeMatches(kAttackButtonA, 0),
          "new delayed grace is strength-specific while legacy zero remains any-edge");
    Check(IsLegacyUnmatchedInputDiagnostic(
              "2 attack input batch(es) did not become recorded moves: A at frame 996 (poll 2791); retake or remove those presses") &&
          !IsLegacyUnmatchedInputDiagnostic(
              "exact contact journal overflowed during recording") &&
          !IsLegacyUnmatchedInputDiagnostic(
              "attack input batch did not become a move") &&
          !IsLegacyUnmatchedInputDiagnostic(
              "integrity failed: 2 attack input batch(es) did not become recorded moves: A; retake or remove those presses"),
          "only the exact historical unmatched-input review text is nonblocking");
    Check(!RecorderStopWaitExpired(false, false, 384, 384) &&
          RecorderStopWaitExpired(false, false, 385, 384) &&
          !RecorderStopWaitExpired(true, false, 999, 384) &&
          !RecorderStopWaitExpired(false, true, 999, 384),
          "recording stop has a bounded running-game watchdog only");
    Check(!PracticeAutomationSuppressed(false, false) &&
          PracticeAutomationSuppressed(true, false) &&
          PracticeAutomationSuppressed(false, true) &&
          PracticeAutomationSuppressed(true, true),
          "persistent Practice automation is quarantined for recorder capture and every demo phase");
    Check(EmbeddedSavestateCanLoad(false, false) &&
          EmbeddedSavestateCanLoad(false, true) &&
          EmbeddedSavestateCanLoad(true, true) &&
          !EmbeddedSavestateCanLoad(true, false),
          "an embedded savestate fails closed when exact restore is unavailable");
    Check(DecideDemoBaselineSource(false, true, true) ==
              DemoBaselineSource::RunnerCheckpoint,
          "loaded demonstrations prefer the verified same-session checkpoint");
    Check(DecideDemoBaselineSource(true, true, true) ==
              DemoBaselineSource::EmbeddedDump,
          "unsaved recorder previews restore their own frame-zero dump");
    Check(DecideDemoBaselineSource(false, false, false) ==
              DemoBaselineSource::None,
          "demonstrations never substitute value setup for a missing restore source");
    Check(DemoTrialRecipeActive(true, false, true, true) &&
          !DemoTrialRecipeActive(false, false, true, true) &&
          !DemoTrialRecipeActive(true, true, true, true) &&
          !DemoTrialRecipeActive(true, false, false, true) &&
          !DemoTrialRecipeActive(true, false, true, false),
          "only a loaded Playing demonstration with runner steps owns the trial recipe bar");
    Check(kDemoTerminalRecipeHoldTicks >= 4,
          "a terminal demo recipe crosses multiple ordinary render frames before restore");
    Check(kDemoRecipeSettleTimeoutTicks > kDemoTerminalRecipeHoldTicks,
          "post-input recipe settlement outlives the short frozen presentation hold");
    using DemoEnd = DemoRecipeEndAction;
    Check(DecideDemoRecipeEnd(false, false, 0) ==
              DemoEnd::ContinuePlayback &&
          DecideDemoRecipeEnd(false, true, 0) ==
              DemoEnd::ContinuePlayback &&
          DecideDemoRecipeEnd(true, false, 0) ==
              DemoEnd::SettleFinalContacts &&
          DecideDemoRecipeEnd(true, false, 20) ==
              DemoEnd::SettleFinalContacts &&
          DecideDemoRecipeEnd(true, true, 20) ==
              DemoEnd::HoldTerminalFrame &&
          DecideDemoRecipeEnd(true, false, 1) ==
              DemoEnd::FailThenHold,
          "demo input completion settles pending contacts before terminal hold or bounded failure");
    Check(RunnerDropAttemptDelta(false) == 1 &&
          RunnerDropRetryDelay(false, 96) == 96 &&
          RunnerDropAttemptDelta(true) == 0 &&
          RunnerDropRetryDelay(true, 96) == 0,
          "demo recipe divergence cannot count a player try or schedule checkpoint retry");
    using Mission::SequencePolicy::DecideDemoRoundEvent;
    using Mission::SequencePolicy::DemoRoundEventAction;
    Check(DecideDemoRoundEvent(false, 12, true) ==
              DemoRoundEventAction::Continue &&
          DecideDemoRoundEvent(true, 12, true) ==
              DemoRoundEventAction::NormalizeRestoreTransient &&
          DecideDemoRoundEvent(true, 1, true) ==
              DemoRoundEventAction::NormalizeRestoreTransient &&
          DecideDemoRoundEvent(true, 0, true) ==
              DemoRoundEventAction::RestoreBaseline &&
          DecideDemoRoundEvent(true, 12, false) ==
              DemoRoundEventAction::RestoreBaseline,
          "post-restore round fields normalize only inside the bounded demo startup window while both fighters remain alive");
    Check(SnapshotCanResumeAfterRestore(false, false, false) &&
          !SnapshotCanResumeAfterRestore(true, false, false) &&
          !SnapshotCanResumeAfterRestore(false, true, false) &&
          !SnapshotCanResumeAfterRestore(false, false, true),
          "runner validation never consumes the pre-restore demo sample");
    using Mission::Engine::Demo::RequiresTutorialRestoreAcknowledgement;
    Check(RequiresTutorialRestoreAcknowledgement(true, false, true),
          "a lesson-command loaded demo waits for tutorial restore acknowledgement");
    Check(!RequiresTutorialRestoreAcknowledgement(false, false, true) &&
          !RequiresTutorialRestoreAcknowledgement(true, true, true) &&
          !RequiresTutorialRestoreAcknowledgement(true, false, false),
          "generic mission demos, recorder previews, and sessions without a tutorial release normally");

    using PendingCancel =
        Mission::Engine::PendingLaunchPolicy::CancelEffect;
    using Mission::Engine::PendingLaunchPolicy::DecideCancel;
    Check(DecideCancel(true) == PendingCancel::ClearSpecificMission,
          "a selected mission path is owned by the canceled title launch");
    Check(DecideCancel(false) == PendingCancel::PreserveGenericBrowser,
          "generic Practice-to-browser mode survives unrelated launch cleanup");
    using PendingConsume =
        Mission::Engine::PendingLaunchPolicy::ConsumeEffect;
    using Mission::Engine::PendingLaunchPolicy::DecideConsume;
    Check(DecideConsume(true, true) == PendingConsume::LoadPreparedMission,
          "a title-picked mission consumes its pinned path and parsed value together");
    Check(DecideConsume(false, false) == PendingConsume::OpenGenericBrowser,
          "an empty pending transaction retains generic Practice-to-browser mode");
    Check(DecideConsume(true, false) == PendingConsume::RejectIncompleteTransaction &&
          DecideConsume(false, true) == PendingConsume::RejectIncompleteTransaction,
          "partial title transactions fail visibly instead of reparsing or opening the browser");
    using Mission::Engine::PendingLaunchPolicy::ReachedSelectorExit;
    Check(ReachedSelectorExit(true, true, true),
          "a pending title launch retires after Character Select returns to Menu");
    Check(!ReachedSelectorExit(true, false, true) &&
          !ReachedSelectorExit(true, true, false) &&
          !ReachedSelectorExit(false, true, true),
          "selector-exit cancellation requires the complete pending launch transition");
    using Mission::Engine::PendingLaunchPolicy::StartupInputOwned;
    Check(StartupInputOwned(true, false, false) &&
          StartupInputOwned(false, true, false) &&
          StartupInputOwned(false, false, true),
          "P1 remains neutral across pending-title and runner baseline ownership");
    Check(!StartupInputOwned(false, false, false),
          "P1 startup neutral releases only after both launch owners retire");
    using Mission::Engine::PendingLaunchPolicy::CanReleaseStartupInput;
    Check(CanReleaseStartupInput(false, false, false, false, false),
          "an ordinary mission releases P1 when its baseline is ready");
    Check(!CanReleaseStartupInput(false, false, false, true, false) &&
          CanReleaseStartupInput(false, false, false, true, true),
          "a tutorial releases P1 only after freeze/task ownership is established");
    Check(!CanReleaseStartupInput(true, false, false, true, true) &&
          !CanReleaseStartupInput(false, true, false, true, true) &&
          !CanReleaseStartupInput(false, false, true, true, true),
          "tutorial handoff cannot bypass pending title or baseline ownership");

    using Mission::Engine::StartupRestorePolicy::CanAttempt;
    Check(CanAttempt(true, true),
          "exact-state restore starts only after world and Practice ownership settle");
    Check(!CanAttempt(true, false),
          "a clear Match cannot race restore ahead of PracticeTick controller capture");
    Check(!CanAttempt(false, true) && !CanAttempt(false, false),
          "Practice controller readiness cannot bypass ordinary restore blockers");

    using StartupFailure = Mission::Engine::StartupFailurePolicy::Effect;
    using Mission::Engine::StartupFailurePolicy::Decide;
    Check(Decide(true) == StartupFailure::RetainTutorialError &&
          Decide(false) == StartupFailure::AbortRunner,
          "startup failure retains a durable tutorial recovery surface only for lessons");

    using SetupEffect = Mission::Setup::DeferredPolicy::SettleEffect;
    using Mission::Setup::DeferredPolicy::DecideSettle;
    using SetupPhase = Mission::Setup::DeferredPolicy::Phase;
    Check(DecideSettle(true, false, true, true, SetupPhase::Idle) == SetupEffect::Wait,
          "an idle setup cannot consume a stale completion receipt");
    Check(DecideSettle(false, true, false, false, SetupPhase::AwaitingValues) == SetupEffect::Wait &&
          DecideSettle(false, false, false, false, SetupPhase::AwaitingStateRestore) == SetupEffect::Wait,
          "deferred setup waits while reload owns the transaction or before Match");
    Check(DecideSettle(false, false, true, false, SetupPhase::AwaitingValues) == SetupEffect::Fail,
          "not-busy Match without a matching receipt is a failed reload");
    Check(DecideSettle(false, false, true, true, SetupPhase::AwaitingValues) == SetupEffect::ApplyValues,
          "a matching receipt releases value setup");
    Check(DecideSettle(false, false, true, true, SetupPhase::AwaitingStateRestore) ==
              SetupEffect::ReleaseForStateRestore,
          "a matching receipt releases an embedded-state restore without value writes");
    Check(DecideSettle(true, true, false, false, SetupPhase::AwaitingValues) == SetupEffect::Fail,
          "deferred setup timeout remains terminal even while reload reports busy");

    using ResourceFamily = Mission::Setup::ResourcePolicy::Family;
    using ResourceVerification = Mission::Setup::ResourcePolicy::Verification;
    using Mission::Setup::ResourcePolicy::Classify;
    Check(Classify(ResourceFamily::Ikumi, "levelGauge") ==
              ResourceVerification::Immediate &&
          Classify(ResourceFamily::Rumi, "kimchiActive") ==
              ResourceVerification::Immediate,
          "direct character resource fields have an immediate verification contract");
    Check(Classify(ResourceFamily::Rumi, "barehanded") ==
              ResourceVerification::RequiresActionable,
          "Rumi's weapon table swap remains explicitly actionable-gated");
    Check(Classify(ResourceFamily::Rumi, "poisonTimer") ==
              ResourceVerification::Unsupported &&
          Classify(ResourceFamily::None, "barehanded") ==
              ResourceVerification::Unsupported,
          "unknown or character-mismatched resource keys cannot be accepted silently");

    using Recorder = Mission::Engine::Recorder::Phase;
    using Advance = Mission::Engine::Recorder::AdvanceEffect;
    Check(!Mission::Engine::Recorder::UsesDedicatedPauseMenu(Recorder::Idle) &&
          Mission::Engine::Recorder::UsesDedicatedPauseMenu(Recorder::PreRecord) &&
          Mission::Engine::Recorder::UsesDedicatedPauseMenu(Recorder::CountIn) &&
          Mission::Engine::Recorder::UsesDedicatedPauseMenu(Recorder::Recording) &&
          Mission::Engine::Recorder::UsesDedicatedPauseMenu(Recorder::Review),
          "every live mission-authoring phase owns the dedicated Recording menu");
    Check(!Mission::Engine::Recorder::IsCapturePhase(Recorder::Idle) &&
          !Mission::Engine::Recorder::IsCapturePhase(Recorder::PreRecord) &&
          Mission::Engine::Recorder::IsCapturePhase(Recorder::CountIn) &&
          Mission::Engine::Recorder::IsCapturePhase(Recorder::Recording) &&
          !Mission::Engine::Recorder::IsCapturePhase(Recorder::Review),
          "only countdown and live recording own capture suspension");
    Check(Mission::Engine::Recorder::NeedsCaptureMenuHandoff(
              true, Recorder::Recording) &&
          Mission::Engine::Recorder::NeedsCaptureMenuHandoff(
              true, Recorder::CountIn) &&
          !Mission::Engine::Recorder::NeedsCaptureMenuHandoff(
              true, Recorder::Review) &&
          !Mission::Engine::Recorder::NeedsCaptureMenuHandoff(
              false, Recorder::Recording),
          "only an owned live-capture suspension transfers to neutral handoff");
    Check(Mission::Engine::Recorder::DecideAdvance(Recorder::PreRecord) ==
              Advance::BeginCountIn,
          "Macro Record starts the authoring countdown");
    Check(Mission::Engine::Recorder::DecideAdvance(Recorder::CountIn) ==
              Advance::CancelCountIn,
          "Macro Record cancels an unstarted countdown safely");
    Check(Mission::Engine::Recorder::DecideAdvance(Recorder::Recording) ==
              Advance::StopToReview,
          "Macro Record stops active capture into Review");
    Check(Mission::Engine::Recorder::DecideAdvance(Recorder::Review) ==
              Advance::KeepReview,
          "Macro Record cannot erase a Review take");

    using EntityTransition =
        Mission::RecorderEntityTrace::SampledSlotTransition;
    using Mission::RecorderEntityTrace::ClassifySampledSlotTransition;
    Check(ClassifySampledSlotTransition(false, false, 0, true, 400) ==
              EntityTransition::Baseline,
          "a live frame-zero entity is sampled as baseline, not spawn");
    Check(ClassifySampledSlotTransition(true, false, 0, true, 400) ==
              EntityTransition::Spawn,
          "a dead-to-live slot edge is sampled as spawn");
    Check(ClassifySampledSlotTransition(true, true, 400, true, 405) ==
              EntityTransition::Morph,
          "a live slot pattern change is sampled as morph");
    Check(ClassifySampledSlotTransition(true, true, 405, false, 0) ==
              EntityTransition::Despawn,
          "a live-to-dead slot edge is sampled as despawn");
    Check(Mission::RecorderEntityTrace::
              AllocationAdvancedWithoutObservedSpawn(true, 9, 12, 0),
          "allocator movement exposes a spawn retired between samples");
    Check(!Mission::RecorderEntityTrace::
               AllocationAdvancedWithoutObservedSpawn(true, 9, 12, 1),
          "an observed spawn explains ordinary allocator movement");
    Check(Mission::RecorderEntityTrace::ContactGradesLifecycle(12, 12) &&
              !Mission::RecorderEntityTrace::ContactGradesLifecycle(11, 12),
          "a contact grades only its nearest lifecycle producer phase");
    using namespace Mission::SemanticSourcePolicy;
    Check(AccumulateCandidateAction(kExplicitUnresolved, 4) == 4 &&
              AccumulateCandidateAction(4, 4) == 4,
          "repeated lifecycle phases from one action retain one exact source");
    Check(AccumulateCandidateAction(4, 9) == kAmbiguousSelection &&
              FinalizeCandidateAction(kAmbiguousSelection) ==
                  kExplicitUnresolved,
          "an old child overlapping a later identical setter stays standalone");
    Check(ResolveOrdinaryBirthAction(true, false, 2, 9, true) == 2,
          "a slow ordinary projectile keeps its spawn action instead of a later contact morph");
    Check(ResolveOrdinaryBirthAction(false, true, -1, 9, true) ==
              kExplicitUnresolved &&
              ResolveOrdinaryBirthAction(true, false, 2, 9, false) ==
                  kExplicitUnresolved,
          "baseline and action-order-ambiguous ordinary projectiles stay standalone");
    Check(CandidateLifecycleEligible(true, false, false, true, true, true) &&
              !CandidateLifecycleEligible(true, false, true, false, true,
                                          true),
          "a baseline puppet accepts an exact command morph, never a fake birth");
    Check(CandidateLifecycleEligible(false, true, true, false, false, true) &&
              !CandidateLifecycleEligible(false, true, false, true, false,
                                          true),
          "a new entity accepts its controller birth but not an unrelated later morph");
    CandidateSelection source;
    source = AccumulateCandidate(source, 4, 253);
    source = AccumulateCandidate(source, 4, 253);
    Check(FinalizeCandidate(source).action == 4 &&
              FinalizeCandidate(source).move == 253,
          "duplicate lifecycle edges retain one exact action/move source");
    source = AccumulateCandidate(source, 4, 254);
    Check(IsExplicitUnresolved(FinalizeCandidate(source).action,
                               FinalizeCandidate(source).move),
          "two competing source move IDs fail closed after primary precedence");
    const CandidateSelection mizukabPrimary = FinalizeCandidate(
        AccumulateStepMoveCandidate({}, 4, 314, true, 315, true));
    const CandidateSelection mioAlias = FinalizeCandidate(
        AccumulateStepMoveCandidate({}, 7, 250, false, 273, true));
    Check(mizukabPrimary.action == 4 && mizukabPrimary.move == 314 &&
              mioAlias.action == 7 && mioAlias.move == 273,
          "primary Mizukab context and Mio automatic-only context resolve deterministically");
    CandidateSelection otherAction;
    otherAction = AccumulateCandidate(otherAction, 4, 253);
    otherAction = AccumulateCandidate(otherAction, 9, 253);
    Check(IsExplicitUnresolved(FinalizeCandidate(otherAction).action,
                               FinalizeCandidate(otherAction).move),
          "the same move from two distinct actions fails closed");
    Check(IsLegacyAbsent(kLegacyAbsent, kLegacyAbsent) &&
              IsExplicitUnresolved(kExplicitUnresolved,
                                   kExplicitUnresolved) &&
              IsExact(2, 253) &&
              !ValidPersistedPair(kLegacyAbsent, kExplicitUnresolved),
          "legacy, unresolved, and exact provenance cannot collapse together");

    Check(IsMoveInstanceEdge(200, 0, 0, 0),
          "changing onto a move starts an action instance");
    Check(IsMoveInstanceEdge(200, 0, 200, 7),
          "same-ID restart at the animation head preserves a chained repeated normal");
    Check(IsMoveInstanceEdge(200, kMoveRestartFrameMax, 200, 9),
          "a restart within the head window still counts");
    Check(!IsMoveInstanceEdge(200, 8, 200, 7),
          "ordinary animation advance is not a new action instance");
    Check(!IsMoveInstanceEdge(171, 5, 171, 17),
          "a looping animation wrapping to a mid-anim frame is not a new instance");
    Check(!IsCausallyCredibleMoveInstanceEdge(207, 2, 207, 3, false),
          "an uncorroborated j.A 3->2 animation wrap is not a second action");
    Check(IsCausallyCredibleMoveInstanceEdge(200, 2, 200, 8, true),
          "a fresh matching A edge preserves a repeated same-ID normal");
    Check(IsCausallyCredibleMoveInstanceEdge(208, 0, 207, 3, false),
          "a move-ID transition stays authoritative without sampled input");
    Check(!FlexibleMultiHitCanFinalize(true, 0, true, false, false),
          "a flexible multi-hit still has to connect at least once");
    Check(!FlexibleMultiHitCanFinalize(true, 4, false, false, false),
          "partial contacts alone do not advance an active multi-hit early");
    Check(FlexibleMultiHitCanFinalize(true, 4, false, true, false) &&
              FlexibleMultiHitCanFinalize(true, 4, false, false, true),
          "a correct continuation or combo boundary accepts a partial multi-hit");
    Check(!FlexibleMultiHitCanFinalize(false, 4, true, true, true),
          "authored strict hit counts never inherit recorder leniency");
    using StrictDecision = StrictActionInstanceDecision;
    Check(DecideStrictActionInstance(
              true, true, false,
              false, false, true, true,
              false, false, false) == StrictDecision::RejectUnexpected,
          "a same-ID restart of the completed previous step is an extra strict action");
    Check(DecideStrictActionInstance(
              false, true, true,
              true, true, false, false,
              false, false, false) == StrictDecision::Ignore,
          "an advancing same-ID multi-hit phase is not mistaken for another action");
    Check(DecideStrictActionInstance(
              true, true, true,
              true, true, false, false,
              true, false, false) == StrictDecision::AcceptAutomaticPhase,
          "an ID-changing alias within the armed step remains one automatic action");
    Check(DecideStrictActionInstance(
              true, true, false,
              false, false, true, true,
              true, false, false) == StrictDecision::AcceptAutomaticPhase,
          "a flattened automatic follow-up may finish after its step advances");
    Check(DecideStrictActionInstance(
              true, true, true,
              true, true, false, false,
              false, false, false) == StrictDecision::RejectUnexpected,
          "restarting an already armed same-ID step cannot replace its first instance");
    Check(DecideStrictActionInstance(
              true, true, true,
              true, true, false, false,
              false, true, false) == StrictDecision::AcceptNextOverlap,
          "a repeated-ID next step survives same-sample contact/cancel overlap");
    Check(DecideStrictActionInstance(
              true, true, false,
              false, false, false, false,
              true, false, true) == StrictDecision::SkipOptional,
          "a fresh action may still skip authored optional steps");
    Check(DecideStrictActionInstance(
              true, true, false,
              true, false, false, false,
              true, false, false) == StrictDecision::AcceptExpected,
          "the unarmed expected action starts normally");
    Check(DirectTransitionSatisfied(true, true, true),
          "a fresh destination directly following its authored source is a cancel transition");
    Check(!DirectTransitionSatisfied(true, true, false),
          "the same destination after landing or another move is not the authored cancel");
    Check(!DirectTransitionSatisfied(false, true, true),
          "remaining inside the destination animation cannot mint another transition");
    Check(DirectTransitionSatisfied(true, false, false),
          "ordinary actions without a source contract still use their instance edge");

    Check(FreshBlockReactionSatisfied(true, false, 151, 0, 0, 0),
          "entering blockstun is a fresh blocked-contact reaction");
    Check(FreshBlockReactionSatisfied(true, false, 151, 0, 151, 9),
          "a same-ID blockstun restart credits the next hit in a blockstring");
    Check(FreshBlockReactionSatisfied(true, false, 152, 0, 151, 9),
          "changing blockstun strength credits a consecutive blocked hit");
    Check(!FreshBlockReactionSatisfied(true, false, 151, 8, 151, 7),
          "remaining inside old blockstun cannot credit a newly armed strike");
    Check(!FreshBlockReactionSatisfied(true, true, 168, 0, 151, 9),
          "Recoil Guard is not credited as an ordinary blocked hit");
    Check(!FreshBlockReactionSatisfied(false, false, 0, 0, 151, 9),
          "leaving blockstun is not a blocked-contact reaction");

    Check(AkikoVacuumHitPhase(263) == 264 &&
          AkikoVacuumHitPhase(265) == 266 &&
          AkikoVacuumHitPhase(267) == 268,
          "Akiko 41236 strengths map suction startup to their automatic hit phase");
    Check(IsAkikoVacuumHitTransition(263, 264) &&
          IsAkikoVacuumHitTransition(265, 266) &&
          IsAkikoVacuumHitTransition(267, 268),
          "Akiko 41236 automatic phase transitions are recognized");
    Check(!IsAkikoVacuumHitTransition(263, 266) &&
          !IsAkikoVacuumHitTransition(257, 258),
          "other strengths and Akiko's 623 rekka are not vacuum phase transitions");

    Check(IsRumiCommandThrowSuccessTransition(250, 251) &&
          IsRumiCommandThrowSuccessTransition(263, 251) &&
          IsRumiCommandThrowSuccessTransition(264, 251),
          "Rumi 41236 strengths share the automatic successful-throw phase");
    Check(!IsRumiCommandThrowSuccessTransition(250, 252) &&
          !IsRumiCommandThrowSuccessTransition(251, 262) &&
          !IsRumiCommandThrowSuccessTransition(264, 306),
          "Rumi's selectable 236 follow-ups remain separate player inputs");

    using namespace Mission::SavestateEntityLayout;
    const std::size_t characterSizes[] = {
        13424, 13392, 13400, 13392, 13392, 13384, 13488, 13384, 13400,
        13400, 13408, 13400, 13416, 13392, 13432, 13432, 13416, 13400,
        13408, 13400, 13432, 13400, 13408, 13448, 13408,
    };
    for (std::size_t size : characterSizes) {
        Check(PlayerStateContainsEntityRing(size),
              "every supported character state contains the complete entity ring");
    }
    Check(SlotAnimPointerField(0) == 0x4D8 &&
          SlotAnimPointerField(63) == 0x2A40 &&
          kEntityStateEnd == 0x2AD0,
          "entity slot pointer fields and ring end match the verified EFZ layout");
    std::vector<uint8_t> savedEntityState(kEntityStateEnd, 0);
    const uint16_t savedTail = 7;
    const uint16_t savedNext = 9;
    const uint32_t alive = 1;
    const uint16_t patternA = 403;
    const uint16_t patternB = 421;
    std::memcpy(savedEntityState.data() + kCursorTailOffset,
                &savedTail, sizeof(savedTail));
    std::memcpy(savedEntityState.data() + kCursorNextOffset,
                &savedNext, sizeof(savedNext));
    std::memcpy(savedEntityState.data() + kAliveFlagsOffset + 7 * sizeof(uint32_t),
                &alive, sizeof(alive));
    std::memcpy(savedEntityState.data() + kAliveFlagsOffset + 9 * sizeof(uint32_t),
                &alive, sizeof(alive));
    std::memcpy(savedEntityState.data() + SlotOffset(7) + kSlotPatternOffset,
                &patternA, sizeof(patternA));
    std::memcpy(savedEntityState.data() + SlotOffset(9) + kSlotPatternOffset,
                &patternB, sizeof(patternB));
    const auto savedInventory = Inspect(savedEntityState.data(), savedEntityState.size());
    Check(savedInventory.valid && savedInventory.aliveCount == 2 &&
          savedInventory.aliveMask == ((uint64_t{1} << 7) | (uint64_t{1} << 9)) &&
          savedInventory.tail == savedTail && savedInventory.next == savedNext,
          "savestate entity inventory retains cursors and every live slot");
    std::vector<uint8_t> reconciledEntityState = savedEntityState;
    const uint32_t freshPointer = 0x12345678;
    std::memcpy(reconciledEntityState.data() + SlotAnimPointerField(7),
                &freshPointer, sizeof(freshPointer));
    Check(Equivalent(savedInventory,
                     Inspect(reconciledEntityState.data(), reconciledEntityState.size())),
          "session-local slot pointer reconciliation does not change entity identity");
    reconciledEntityState[SlotOffset(7) + kSlotPatternOffset] ^= 1;
    Check(!Equivalent(savedInventory,
                      Inspect(reconciledEntityState.data(), reconciledEntityState.size())),
          "entity inventory detects gameplay-state corruption after restore");

    Check(!StepDamageDiverged(0, 4000),
          "a step without a recorded damage delta never checks damage");
    Check(!StepDamageDiverged(1200, 1200 + kStepDamageTolerance),
          "variance at the tolerance bound is accepted");
    Check(StepDamageDiverged(1200, 1200 + kStepDamageTolerance + 1),
          "a clean-hit/crit surplus beyond tolerance drops");
    Check(StepDamageDiverged(4000, 1200),
          "a missing crit (much lower damage) also drops");

    Check(DecideComboEnd(false, false) == ComboEndDecision::None,
          "no recovery edge must not change the run");
    Check(DecideComboEnd(true, true) == ComboEndDecision::ConsumeRequired,
          "authored recovery edge must be consumed");
    Check(DecideComboEnd(true, false) == ComboEndDecision::UnexpectedDrop,
          "legacy one-piece combo must still drop on recovery");

    Check(DetectSampledComboBoundary(true, false, 3, 0) ==
              SampledComboBoundary::VisibleRecovery,
          "a visible N->0 recovery is a sampled boundary");
    Check(DetectSampledComboBoundary(true, true, 3, 1) ==
              SampledComboBoundary::CounterRollover,
          "a frame-perfect N->1 wake-up hit is retained as a sampled boundary");
    Check(DetectSampledComboBoundary(true, true, 2, 3) ==
              SampledComboBoundary::None,
          "ordinary increasing combo counts are not boundaries");
    Check(ContactBeginsNewComboBaseline(0, 1) &&
          ContactBeginsNewComboBaseline(2, 1) &&
          !ContactBeginsNewComboBaseline(1, 2) &&
          !ContactBeginsNewComboBaseline(2, 2),
          "exact contact baselines accept zero and positive counter rollovers only");
    Check(ExactContactBeginsNewCombo(true, true, 0, 1, 41, 37) &&
          ExactContactBeginsNewCombo(true, true, 2, 1, 42, 41),
          "ordered zero and 2-to-1 contacts prove unsampled Part-2 boundaries");
    Check(!ExactContactBeginsNewCombo(false, true, 0, 1, 1, 0) &&
              !ExactContactBeginsNewCombo(true, false, 0, 1, 41, 37) &&
              !ExactContactBeginsNewCombo(true, true, 1, 2, 41, 37) &&
              !ExactContactBeginsNewCombo(true, true, 0, 1, 37, 37),
          "first-ever, defended, continuous, and stale contacts do not invent boundaries");

    Check(DecideStepSatisfaction(false, true, false) ==
              StepSatisfactionDecision::Wait,
          "a pre-started meaty may stay armed while recovery is pending");
    Check(DecideStepSatisfaction(true, true, true) ==
              StepSatisfactionDecision::Advance,
          "move-only okizeme setup may advance before recovery");
    Check(DecideStepSatisfaction(true, true, false) ==
              StepSatisfactionDecision::PrematureContactDrop,
          "contact before the required recovery proves a continuous combo");
    Check(DecideStepSatisfaction(true, false, false) ==
              StepSatisfactionDecision::Advance,
          "the pre-started meaty may land after recovery is consumed");
    Check(RecordedActionBaselineAfterBoundary(3, 3, 2, false) == 0,
          "a pre-started unlanded Part-2 action rebases from N to zero");
    Check(RecordedActionBaselineAfterBoundary(3, 2, 2, true) == 3,
          "the landed Part-1 boundary action keeps its baseline");
    Check(RunnerArmBaseline(1, true) == 0,
          "same-sample wake-up action+hit arms against the new segment baseline");
    Check(RunnerArmBaseline(3, false) == 3,
          "ordinary action arms against the current combo count");

    DelayedActionWindow gap;
    Check(!gap.Tick(false, false, false, 3), "gap tick 1 remains open");
    Check(!gap.Tick(false, false, false, 3), "gap tick 2 remains open");
    Check(!gap.Tick(false, false, true, 3),
          "attack input on the last legal tick latches commit grace");
    Check(!gap.Tick(false, true, false, 3) && gap.elapsed == 0,
          "matching action on the next sample wins and resets the window");
    Check(!gap.Tick(false, false, false, 1), "fresh one-tick window stays open");
    Check(gap.Tick(false, false, false, 1),
          "an expired gap with no committed input drops");

    Check(RecordedGapAllowance(0) == 0, "zero gap stays default");
    Check(RecordedGapAllowance(200) == 460,
          "supplied Misuzu recording preserves its 200->460 allowance");
    Check(RecordedGapAllowance(300) == 660,
          "delays longer than the former 576 cap remain recognizable");
    Check(RecordedGapAllowance(4000) == 5760, "malformed delay is safety-capped");

    SegmentScore score;
    score.Observe(1, 300);
    score.Observe(3, 1200);
    score.FinalizeSegment();
    score.Observe(0, 1200);
    score.Observe(2, 800);
    Check(score.TotalHits() == 5, "hits accumulate across explicit combo parts");
    Check(score.TotalDamage() == 2000, "damage accumulates across explicit combo parts");
    score.Reset();
    Check(score.TotalHits() == 0 && score.TotalDamage() == 0,
          "retry/reset clears every segment accumulator");

    // Raw +0x168 alone is insufficient: a FIC script may write 3 on a complete whiff.
    using Mission::Contact::DirectEvidence;
    using ContactResult = Mission::Contact::Result;
    DirectEvidence contact;
    contact.rawAttackerState = 3;
    Check(Mission::Contact::ClassifyDirect(contact) == ContactResult::None,
          "raw hit-state 3 without resolver consumption emits no contact");
    contact = {};
    contact.resolved = true; contact.comboIncreased = true;
    contact.rawAttackerState = 3;
    Check(Mission::Contact::ClassifyDirect(contact) == ContactResult::Hit,
          "a consumed direct collision with combo gain is a hit");
    Check(Mission::Contact::CorroboratesComboRestart(
              2, 1, 9336, 8645, true, 3),
          "damage plus hit reaction proves an atomic 2-to-1 combo restart");
    Check(!Mission::Contact::CorroboratesComboRestart(
               2, 1, 9336, 8645, false, 3) &&
              !Mission::Contact::CorroboratesComboRestart(
                  2, 1, 9336, 9336, true, 3) &&
              !Mission::Contact::CorroboratesComboRestart(
                  2, 1, 9336, 8645, true, 2),
          "counter rollover alone cannot masquerade as a committed hit");
    contact = {};
    contact.resolved = true; contact.comboRestarted = true;
    contact.rawAttackerState = 3;
    Check(Mission::Contact::ClassifyDirect(contact) == ContactResult::Hit,
          "a corroborated direct combo restart is an ordinary hit");
    contact = {};
    contact.resolved = true; contact.defenderBlocked = true;
    contact.rawAttackerState = 2;
    Check(Mission::Contact::ClassifyDirect(contact) == ContactResult::Block,
          "block is classified from defender reaction, not raw 2 alone");
    contact.defenderRecoilGuard = true;
    Check(Mission::Contact::ClassifyDirect(contact) == ContactResult::RecoilGuard,
          "RG wins over the shared raw block value");
    contact = {};
    contact.resolved = true; contact.exactThrowBranch = true;
    contact.rawAttackerState = 6;
    Check(Mission::Contact::ClassifyDirect(contact) == ContactResult::Throw,
          "throw requires the exact paired throw branch");
    contact = {};
    contact.resolved = true; contact.exactGuardPointBranch = true;
    contact.rawAttackerState = 2;
    Check(Mission::Contact::ClassifyDirect(contact) == ContactResult::GuardPoint,
          "guard point requires the exact frame-flag overlap branch");
    contact = {};
    contact.resolved = true; contact.rawAttackerState = 6;
    Check(Mission::Contact::ClassifyDirect(contact) == ContactResult::Unknown,
          "ambiguous raw 6 counter/throw evidence stays unknown");

    // Entity +0x80 is likewise a producer latch; the exact 0x7697D0
    // resolver supplies independent branch evidence.
    using Mission::Contact::EntityEvidence;
    EntityEvidence entityContact;
    entityContact.rawEntityState = 3;
    Check(Mission::Contact::ClassifyEntity(entityContact) == ContactResult::None,
          "entity raw state without a committed resolver path emits no contact");
    entityContact = {};
    entityContact.resolved = true; entityContact.rawEntityState = 3;
    Check(Mission::Contact::ClassifyEntity(entityContact) == ContactResult::Unknown,
          "entity raw hit latch without combo mutation stays unknown");
    entityContact.comboIncreased = true;
    Check(Mission::Contact::ClassifyEntity(entityContact) == ContactResult::Hit,
          "entity state 3 plus resolver-local combo gain is a hit");
    entityContact.comboIncreased = false;
    entityContact.comboRestarted = true;
    Check(Mission::Contact::ClassifyEntity(entityContact) == ContactResult::Hit,
          "entity state 3 plus a corroborated combo restart is a hit");
    entityContact.comboRestarted = false;
    entityContact.comboIncreased = true;
    entityContact.rawEntityState = 7;
    Check(Mission::Contact::ClassifyEntity(entityContact) == ContactResult::SpecialHit,
          "entity state 7 plus resolver-local combo gain is the hit variant");
    entityContact = {};
    entityContact.resolved = true; entityContact.rawEntityState = 2;
    Check(Mission::Contact::ClassifyEntity(entityContact) == ContactResult::Unknown,
          "entity state 2 alone does not conflate block, RG, and guard break");
    entityContact.defenderBlocked = true;
    Check(Mission::Contact::ClassifyEntity(entityContact) == ContactResult::Block,
          "entity block requires the defender block reaction");
    entityContact.defenderRecoilGuard = true;
    Check(Mission::Contact::ClassifyEntity(entityContact) == ContactResult::RecoilGuard,
          "entity RG wins over the shared state-2 latch");

    Mission::Contact::Journal<4> journal;
    Mission::Contact::Event event;
    event.batchId = 2; event.attacker = 1; event.defender = 2;
    event.source = Mission::Contact::Source::DirectPlayer;
    event.result = ContactResult::Hit;
    event.defenderMoveBefore = 97;
    event.defenderMove = 150;
    journal.Publish(event);
    Mission::Contact::Event readEvents[4] = {};
    auto read = journal.ReadAfter(0, journal.Epoch(), 1, readEvents, 4);
    Check(read.count == 0 && read.consumedThrough == 0,
          "an unclosed Battle batch is never exposed");
    read = journal.ReadAfter(0, journal.Epoch(), 2, readEvents, 4);
    Check(read.count == 1 && readEvents[0].result == ContactResult::Hit &&
          readEvents[0].defenderMoveBefore == 97 &&
          readEvents[0].defenderMove == 150,
          "a closed contact batch preserves pre/post defender state in order");
    event.source = Mission::Contact::Source::Entity;
    event.result = ContactResult::Block;
    event.attackerMove = 405;
    event.entitySlot = 17;
    event.entityPattern = 405;
    journal.Publish(event);
    read = journal.ReadAfter(1, journal.Epoch(), 2, readEvents, 4);
    Check(read.count == 1 &&
          readEvents[0].source == Mission::Contact::Source::Entity &&
          readEvents[0].entitySlot == 17 && readEvents[0].entityPattern == 405,
          "the fixed journal preserves exact entity owner-slot-pattern evidence");
    const uint32_t oldEpoch = journal.Epoch();
    journal.ResetEpoch();
    read = journal.ReadAfter(0, journal.Epoch(), 2, readEvents, 4);
    Check(read.count == 0 && oldEpoch != journal.Epoch(),
          "a world reset rejects stale contact events");
    for (int i = 0; i < 6; ++i) {
        event.batchId = 3;
        journal.Publish(event);
    }
    read = journal.ReadAfter(1, journal.Epoch(), 3, readEvents, 4);
    Check(read.overflow, "contact journal overflow is an explicit integrity failure");

    std::cout << "mission_sequence_policy_tests passed\n";
    return 0;
}
