#include "game/mission/tutorial_session.h"
#include "game/mission/tutorial_episode_policy.h"
#include "game/mission/tutorial_layout_policy.h"
#include "game/mission/tutorial_text_policy.h"
#include "game/mission/tutorial_state_policy.h"
#include "input/input_hook.h"
#include "input/physical_poll_sample_policy.h"

#include <cstdlib>
#include <iostream>

namespace {

void Check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}

}

int main() {
    using namespace Mission::TutorialSession;
    namespace StatePolicy = Mission::TutorialStatePolicy;
    namespace EpisodePolicy = Mission::TutorialEpisodePolicy;
    namespace LayoutPolicy = Mission::TutorialLayoutPolicy;
    namespace ColorPolicy = Mission::TutorialColorPolicy;
    namespace TextPolicy = Mission::TutorialTextPolicy;
    namespace HookPolicy = InputHookPolicy;

    Check(HookPolicy::TutorialP2Route(
              HookPolicy::TutorialP2InputSource::Neutral) ==
              HookPolicy::ProcessRoute::NativePoll &&
          HookPolicy::TutorialP2Route(
              HookPolicy::TutorialP2InputSource::MotionQueue) ==
              HookPolicy::ProcessRoute::NativePoll &&
          HookPolicy::TutorialP2Route(
              HookPolicy::TutorialP2InputSource::ImmediatePress) ==
              HookPolicy::ProcessRoute::NativePoll &&
          HookPolicy::TutorialP2Route(
              HookPolicy::TutorialP2InputSource::BufferFreeze) ==
              HookPolicy::ProcessRoute::NativePoll,
          "tutorial P2 queue, immediate, and frozen inputs all retain EFZ's native processor");
    Check(HookPolicy::TutorialP2AllowsNormalPulse(
              HookPolicy::TutorialP2InputSource::Neutral) &&
          !HookPolicy::TutorialP2AllowsNormalPulse(
              HookPolicy::TutorialP2InputSource::MotionQueue) &&
          !HookPolicy::TutorialP2AllowsNormalPulse(
              HookPolicy::TutorialP2InputSource::ImmediatePress) &&
          !HookPolicy::TutorialP2AllowsNormalPulse(
              HookPolicy::TutorialP2InputSource::BufferFreeze),
          "tutorial normals wait for exclusive scripted input owners to become neutral");

    Check(!SessionMenuAllowed(Phase::Preparing) &&
          !SessionMenuAllowed(Phase::DemoSuspended) &&
          SessionMenuAllowed(Phase::Intro) &&
          SessionMenuAllowed(Phase::TaskActive) &&
          SessionMenuAllowed(Phase::Error),
          "session menu cannot interrupt startup or the demo restore handoff");
    Check(PersistedAttemptDelta(-1) == 0 &&
          PersistedAttemptDelta(0) == 0 &&
          PersistedAttemptDelta(1) == 1 &&
          PersistedAttemptDelta(3) == 3,
          "progress persists real misses/restarts and preserves a flawless zero");
    Check(NeutralGatePublishedMask(0x00) == 0 &&
          NeutralGatePublishedMask(0x01) == 0 &&
          NeutralGatePublishedMask(0xFF) == 0 &&
          NeutralGatePollIsReleased(true, 0x00) &&
          !NeutralGatePollIsReleased(false, 0x00) &&
          !NeutralGatePollIsReleased(true, 0x01) &&
          !NeutralGatePollIsReleased(true, 0x10) &&
          !NeutralGatePollIsReleased(true, 0xFF),
          "tutorial task handoff suppresses every gameplay bit and waits for full release");
    Check(NeutralGateRequiredFreshPolls() == 2 &&
          NeutralGateObservedSerialBaseline(false, 81) == 0 &&
          NeutralGateObservedSerialBaseline(true, 81) == 81 &&
          NeutralGateNeutralPollCount(false, false, 0, 0) == 0 &&
          NeutralGateNeutralPollCount(true, false, 0, 1) == 1 &&
          NeutralGateNeutralPollCount(true, true, 0, 0) == 1 &&
          NeutralGateNeutralPollCount(true, true, 0, 1) == 2 &&
          NeutralGateNeutralPollCount(true, true, 0x08, 2) == 0,
          "tutorial release gate counts fresh neutral polls and resets on held directions");

    constexpr uint32_t packedPoll =
        PhysicalPollSamplePolicy::Pack(42, 0x91);
    Check(PhysicalPollSamplePolicy::Serial(packedPoll) == 42 &&
          PhysicalPollSamplePolicy::Mask(packedPoll) == 0x91 &&
          PhysicalPollSamplePolicy::Serial(
              PhysicalPollSamplePolicy::Next(packedPoll, 0x20)) == 43 &&
          PhysicalPollSamplePolicy::Mask(
              PhysicalPollSamplePolicy::Next(packedPoll, 0x20)) == 0x20 &&
          PhysicalPollSamplePolicy::Serial(
              PhysicalPollSamplePolicy::Next(
                  PhysicalPollSamplePolicy::Pack(
                      PhysicalPollSamplePolicy::kSerialBits, 0xFF),
                  0x00)) == 1,
          "physical poll mask/serial publication policy drifted");

    constexpr LayoutPolicy::Typography smallUi =
        LayoutPolicy::TypographyFor(0.70f);
    constexpr LayoutPolicy::Typography normalUi =
        LayoutPolicy::TypographyFor(1.00f);
    constexpr LayoutPolicy::Typography largeUi =
        LayoutPolicy::TypographyFor(1.50f);
    Check(smallUi.prosePx >= 12.0f && smallUi.taskPx >= 11.0f &&
          smallUi.metaPx >= 9.0f && smallUi.controlsPx >= 11.0f &&
          normalUi.prosePx == 13.0f && normalUi.controlsPx == 12.0f &&
          largeUi.prosePx <= 16.0f && largeUi.pageTitlePx <= 20.0f,
          "tutorial typography stays readable and bounded at every supported UI scale");
    using HudFocus = LayoutPolicy::HudFocus;
    Check(LayoutPolicy::ParseHudFocus("top") == HudFocus::Top &&
          LayoutPolicy::ParseHudFocus("bottom") == HudFocus::Bottom &&
          LayoutPolicy::ParseHudFocus("life") == HudFocus::Life &&
          LayoutPolicy::ParseHudFocus("meters") == HudFocus::Meters &&
          LayoutPolicy::ParseHudFocus("sp") == HudFocus::Sp &&
          LayoutPolicy::ParseHudFocus("rf") == HudFocus::Rf &&
          LayoutPolicy::ParseHudFocus("final_memory") == HudFocus::FinalMemory &&
          LayoutPolicy::ParseHudFocus("meter_states") == HudFocus::MeterStates &&
          LayoutPolicy::ParseHudFocus("rf_states") == HudFocus::RfStates &&
          LayoutPolicy::ParseHudFocus("red_ic") == HudFocus::RedIc &&
          LayoutPolicy::ParseHudFocus("blue_ic") == HudFocus::BlueIc &&
          LayoutPolicy::ParseHudFocus("blue_ic_meters") == HudFocus::BlueIcMeters &&
          LayoutPolicy::ParseHudFocus("juggle") == HudFocus::Juggle &&
          LayoutPolicy::ParseHudFocus("juggle_yellow") == HudFocus::JuggleYellow &&
          LayoutPolicy::ParseHudFocus("juggle_red") == HudFocus::JuggleRed &&
          LayoutPolicy::ParseHudFocus("") == HudFocus::None &&
          LayoutPolicy::ParseHudFocus("unknown") == HudFocus::None &&
          LayoutPolicy::ParseHudFocus(nullptr) == HudFocus::None,
          "authored HUD focus compiles once and preserves legacy focus aliases");
    constexpr LayoutPolicy::Rect fullDim =
        LayoutPolicy::PageBackdropDim(HudFocus::None);
    constexpr LayoutPolicy::Rect topDim =
        LayoutPolicy::PageBackdropDim(HudFocus::Life);
    constexpr LayoutPolicy::Rect bottomDim =
        LayoutPolicy::PageBackdropDim(HudFocus::Rf);
    constexpr LayoutPolicy::Rect bothDim =
        LayoutPolicy::PageBackdropDim(HudFocus::FinalMemory);
    constexpr LayoutPolicy::Rect juggleDim =
        LayoutPolicy::PageBackdropDim(HudFocus::Juggle);
    Check(fullDim.x == 0.0f && fullDim.y == 0.0f &&
          fullDim.w == 640.0f && fullDim.h == 480.0f &&
          topDim.y == 92.0f && topDim.h == 388.0f &&
          bottomDim.y == 0.0f && bottomDim.h == 412.0f &&
          bothDim.y == 92.0f && bothDim.h == 320.0f &&
          juggleDim.w == 0.0f && juggleDim.h == 0.0f &&
          LayoutPolicy::KeepsTopHudVisible(HudFocus::Top) &&
          LayoutPolicy::KeepsTopHudVisible(HudFocus::Life) &&
          !LayoutPolicy::KeepsTopHudVisible(HudFocus::Meters) &&
          LayoutPolicy::KeepsBottomHudVisible(HudFocus::Bottom) &&
          LayoutPolicy::KeepsBottomHudVisible(HudFocus::Sp) &&
          LayoutPolicy::KeepsBottomHudVisible(HudFocus::MeterStates) &&
          LayoutPolicy::KeepsBottomHudVisible(HudFocus::BlueIcMeters) &&
          !LayoutPolicy::KeepsBottomHudVisible(HudFocus::Life),
          "focused pages dim only complementary playfield and leave juggle previews clear");
    Check(LayoutPolicy::HudFocusRectCount(HudFocus::None) == 0 &&
          LayoutPolicy::HudFocusRectCount(HudFocus::Top) == 1 &&
          LayoutPolicy::HudFocusRectCount(HudFocus::Bottom) == 1 &&
          LayoutPolicy::HudFocusRectCount(HudFocus::Life) == 5 &&
          LayoutPolicy::HudFocusRectCount(HudFocus::Meters) == 6 &&
          LayoutPolicy::HudFocusRectCount(HudFocus::Sp) == 4 &&
          LayoutPolicy::HudFocusRectCount(HudFocus::Rf) == 2 &&
          LayoutPolicy::HudFocusRectCount(HudFocus::FinalMemory) == 3 &&
          LayoutPolicy::HudFocusRectCount(HudFocus::MeterStates) == 6 &&
          LayoutPolicy::HudFocusRectCount(HudFocus::RfStates) == 2 &&
          LayoutPolicy::HudFocusRectCount(HudFocus::RedIc) == 1 &&
          LayoutPolicy::HudFocusRectCount(HudFocus::BlueIc) == 1 &&
          LayoutPolicy::HudFocusRectCount(HudFocus::BlueIcMeters) == 3 &&
          LayoutPolicy::HudFocusRectCount(HudFocus::Juggle) == 0,
          "each HUD focus emits a fixed, bounded number of static outlines");
    const HudFocus outlinedFocuses[] = {
        HudFocus::Top, HudFocus::Bottom, HudFocus::Life,
        HudFocus::Meters, HudFocus::Sp, HudFocus::Rf,
        HudFocus::FinalMemory, HudFocus::MeterStates,
        HudFocus::RfStates, HudFocus::RedIc, HudFocus::BlueIc,
        HudFocus::BlueIcMeters,
    };
    bool allHudOutlinesWithinCanvas = true;
    for (HudFocus focus : outlinedFocuses) {
        for (int i = 0; i < LayoutPolicy::HudFocusRectCount(focus); ++i) {
            allHudOutlinesWithinCanvas = allHudOutlinesWithinCanvas &&
                LayoutPolicy::WithinCanvas(
                    LayoutPolicy::HudFocusRect(focus, i));
        }
    }
    Check(allHudOutlinesWithinCanvas &&
          LayoutPolicy::FpsBounds().x == 278.0f &&
          LayoutPolicy::FpsBounds().y == 6.0f &&
          LayoutPolicy::FpsBounds().w == 84.0f &&
          LayoutPolicy::FpsBounds().h == 60.0f &&
          LayoutPolicy::FpsBounds().x ==
              LayoutPolicy::P1LifeBounds().x +
                  LayoutPolicy::P1LifeBounds().w &&
          LayoutPolicy::FpsBounds().x + LayoutPolicy::FpsBounds().w ==
              LayoutPolicy::P2LifeBounds().x &&
          LayoutPolicy::P1RoundsBounds().w == 34.0f &&
          LayoutPolicy::P2RoundsBounds().x == 362.0f &&
          LayoutPolicy::P1SpBarBounds().y == 446.0f &&
          LayoutPolicy::P1SpBarBounds().h == 12.0f &&
          LayoutPolicy::P1RfBounds().y == 462.0f &&
          LayoutPolicy::P1RfBounds().h == 8.0f &&
          LayoutPolicy::P2RfBounds().y == 462.0f,
          "native Life, round, FPS, SP, and RF outlines use their mapped pixel footprints");
    Check(LayoutPolicy::JugglePreviewRefreshBattleUpdates() == 2u &&
          !LayoutPolicy::JugglePreviewRefreshComplete(100u, 101u) &&
          !LayoutPolicy::JugglePreviewRefreshComplete(102u, 101u) &&
          LayoutPolicy::JugglePreviewRefreshComplete(100u, 102u) &&
          LayoutPolicy::JugglePreviewRefreshComplete(0xFFFFFFFFu, 1u) &&
          !LayoutPolicy::BattleBatchReached(102u, 101u) &&
          LayoutPolicy::BattleBatchReached(102u, 102u) &&
          LayoutPolicy::BattleBatchReached(0xFFFFFFFFu, 1u) &&
          LayoutPolicy::JugglePreviewRefreshWatchdogTicks() >= 32,
          "juggle preview waits for two post-thaw battle calls and handles batch wraparound");
    Check(LayoutPolicy::WithinCanvas(LayoutPolicy::ActiveRequirements()) &&
          LayoutPolicy::WithinCanvas(LayoutPolicy::BelowStatsRequirements()) &&
          LayoutPolicy::WithinCanvas(LayoutPolicy::PageBanner()) &&
          LayoutPolicy::WithinCanvas(LayoutPolicy::PageCard()) &&
          LayoutPolicy::WithinCanvas(LayoutPolicy::PageActions()) &&
          LayoutPolicy::WithinCanvas(LayoutPolicy::JugglePageBanner()) &&
          LayoutPolicy::WithinCanvas(LayoutPolicy::JugglePageCard()) &&
          LayoutPolicy::WithinCanvas(LayoutPolicy::JugglePreviewRail()) &&
          LayoutPolicy::WithinCanvas(LayoutPolicy::JugglePageActions()) &&
          LayoutPolicy::WithinCanvas(LayoutPolicy::ModalFooter(0.70f)) &&
          LayoutPolicy::WithinCanvas(LayoutPolicy::ModalFooter(1.00f)) &&
          LayoutPolicy::WithinCanvas(LayoutPolicy::ModalFooter(1.50f)) &&
          LayoutPolicy::WithinCanvas(LayoutPolicy::ModalContent(0.70f)) &&
          LayoutPolicy::WithinCanvas(LayoutPolicy::ModalContent(1.50f)) &&
          LayoutPolicy::SafeBandsDoNotOverlap() &&
          LayoutPolicy::ActiveRequirements().x <= 16.0f &&
          LayoutPolicy::ActiveRequirements().y >= 96.0f &&
          LayoutPolicy::ActiveRequirements().x +
              LayoutPolicy::ActiveRequirements().w <= 400.0f &&
          LayoutPolicy::PageBanner().y >= 92.0f &&
          LayoutPolicy::ModalContent(1.50f).y +
              LayoutPolicy::ModalContent(1.50f).h <
              LayoutPolicy::ModalFooter(1.50f).y,
          "live, reading, preview, and modal surfaces remain inside the 640x480 safe canvas");
    Check(LayoutPolicy::HudFocusTone(HudFocus::Life, 0) ==
              ColorPolicy::Tone::Life &&
          LayoutPolicy::HudFocusTone(HudFocus::Life, 2) ==
              ColorPolicy::Tone::Fps &&
          LayoutPolicy::HudFocusTone(HudFocus::Life, 3) ==
              ColorPolicy::Tone::Rounds &&
          LayoutPolicy::HudFocusTone(HudFocus::MeterStates, 0) ==
              ColorPolicy::Tone::Sp &&
          LayoutPolicy::HudFocusTone(HudFocus::MeterStates, 4) ==
              ColorPolicy::Tone::RedIc &&
          LayoutPolicy::HudFocusTone(HudFocus::MeterStates, 5) ==
              ColorPolicy::Tone::BlueIc &&
          ColorPolicy::ParseTone("life") == ColorPolicy::Tone::Life &&
          ColorPolicy::ParseTone("blue_ic") == ColorPolicy::Tone::BlueIc &&
          ColorPolicy::ParseTone("not_a_tone") == ColorPolicy::Tone::Default,
          "HUD outlines and authored prose share semantic color roles");
    using JuggleBand = LayoutPolicy::JuggleBand;
    Check(LayoutPolicy::JuggleBandForUntech(0) == JuggleBand::Hidden &&
          LayoutPolicy::JuggleBandForUntech(1) == JuggleBand::Red &&
          LayoutPolicy::JuggleBandForUntech(30) == JuggleBand::Red &&
          LayoutPolicy::JuggleBandForUntech(31) == JuggleBand::Yellow &&
          LayoutPolicy::JuggleBandForUntech(60) == JuggleBand::Yellow &&
          LayoutPolicy::JuggleBandForUntech(61) == JuggleBand::Normal &&
          LayoutPolicy::JuggleWidth(99) == 99 &&
          LayoutPolicy::JuggleWidth(100) == 100 &&
          LayoutPolicy::JuggleWidth(101) == 100 &&
          LayoutPolicy::JugglePreviewUntech(HudFocus::Juggle) == 100 &&
          LayoutPolicy::JugglePreviewUntech(HudFocus::JuggleYellow) == 60 &&
          LayoutPolicy::JugglePreviewUntech(HudFocus::JuggleRed) == 20 &&
          LayoutPolicy::IsKnownFacingByte(0x01u) &&
          LayoutPolicy::IsKnownFacingByte(0xFFu) &&
          !LayoutPolicy::IsKnownFacingByte(0x00u) &&
          !LayoutPolicy::IsKnownFacingByte(0x02u) &&
          LayoutPolicy::FacesRight(0x01u) &&
          !LayoutPolicy::FacesRight(0xFFu),
          "native juggle previews preserve exact gauge bands, width cap, and EFZ signed facing bytes");
    Check(LayoutPolicy::JugglePageCard().x +
              LayoutPolicy::JugglePageCard().w == 336.0f &&
          LayoutPolicy::JugglePreviewRail().x == 344.0f &&
          LayoutPolicy::JugglePreviewRail().w == 280.0f &&
          LayoutPolicy::JugglePageCard().x +
              LayoutPolicy::JugglePageCard().w <
              LayoutPolicy::JugglePreviewRail().x,
          "juggle reading pages reserve enough unobscured width for the full native gauge");
    constexpr LayoutPolicy::Rect upperRequirements =
        LayoutPolicy::RequirementBounds(false);
    constexpr LayoutPolicy::Rect belowStatsRequirements =
        LayoutPolicy::RequirementBounds(true);
    Check(upperRequirements.x == 8.0f && upperRequirements.y == 108.0f &&
          upperRequirements.w == 372.0f && upperRequirements.h == 252.0f &&
          belowStatsRequirements.x == 8.0f && belowStatsRequirements.y == 206.0f &&
          belowStatsRequirements.w == 372.0f && belowStatsRequirements.h == 206.0f &&
          belowStatsRequirements.y + belowStatsRequirements.h <= 412.0f,
          "authored requirement placement selects one stable stats-safe anchor");
    Check(LayoutPolicy::MaxVisibleRequirements() == 8 &&
          LayoutPolicy::RequirementWindowStart(12, 8, 0) == 0 &&
          LayoutPolicy::RequirementWindowStart(12, 8, 6) == 2 &&
          LayoutPolicy::RequirementWindowStart(12, 8, 11) == 4 &&
          LayoutPolicy::RequirementWindowStart(3, 8, 2) == 0,
          "oversized authored checklists keep the current requirement in view");
    Check(LayoutPolicy::ActionWindowStart(12, 8, 0) == 0 &&
          LayoutPolicy::ActionWindowStart(12, 8, 6) == 2 &&
          LayoutPolicy::ActionWindowStart(12, 8, 11) == 4 &&
          LayoutPolicy::ActionWindowStart(3, 8, 2) == 0 &&
          LayoutPolicy::ActionWindowStart(0, 8, 0) == 0,
          "long action rails keep the current authored step in view");
    using ActionStepState = LayoutPolicy::ActionStepVisualState;
    Check(LayoutPolicy::ResolveActionStepVisualState(0, 1, false, false, false) ==
              ActionStepState::Done &&
          LayoutPolicy::ResolveActionStepVisualState(1, 1, false, false, false) ==
              ActionStepState::Current &&
          LayoutPolicy::ResolveActionStepVisualState(1, 1, true, false, false) ==
              ActionStepState::Armed &&
          LayoutPolicy::ResolveActionStepVisualState(1, 1, true, true, false) ==
              ActionStepState::Failed &&
          LayoutPolicy::ResolveActionStepVisualState(2, 1, false, false, false) ==
              ActionStepState::Future &&
          LayoutPolicy::ResolveActionStepVisualState(2, 3, false, false, false) ==
              ActionStepState::Done &&
          LayoutPolicy::ResolveActionStepVisualState(4, 0, false, false, true) ==
              ActionStepState::Done,
          "tutorial action rails distinguish done, current, committed, failed, and future steps");
    Check(TextPolicy::NotationToRich("normal block") == "normal block" &&
          TextPolicy::NotationToRich("~5A") == "~ {input:5A}" &&
          TextPolicy::NotationToRich("delayed 6C / 4C throw") ==
              "delayed {input:6C}  /  {input:4C throw}" &&
          TextPolicy::NotationToRich("22C / BIC") ==
              "{input:22C}  /  {term:BIC}" &&
          TextPolicy::NotationToRich("j.B on wakeup") ==
              "{input:j.B on wakeup}",
          "tutorial notation separates inputs, annotations, delay marks, and IC terms");
    Check(TextPolicy::NormalizeUiText("it\xE2\x80\x99s") == "it's",
          "tutorial prose normalizes punctuation outside the compact font range");
    Check(TextPolicy::HumanizeIdentifier("notation.setup_marker") ==
              "SETUP MARKER",
          "unlabeled authored tasks never expose internal identifiers");

    Check(EpisodePolicy::IsInjectableAction("66A") &&
          EpisodePolicy::IsInjectableAction("66C") &&
          EpisodePolicy::IsInjectableAction("662A") &&
          EpisodePolicy::IsInjectableAction("5A>5B>2C") &&
          EpisodePolicy::IsDashNormalAction("66A") &&
          EpisodePolicy::IsDashNormalAction("66C") &&
          EpisodePolicy::IsContactChainAction("5A>5B>2C"),
          "RG drills expose their dedicated dash-normal and contact-chain scripts");
    Check(EpisodePolicy::IsForwardDashApproachState(163) &&
          EpisodePolicy::IsForwardDashApproachState(178) &&
          EpisodePolicy::IsForwardDashApproachState(250) &&
          !EpisodePolicy::IsForwardDashApproachState(164) &&
          !EpisodePolicy::IsForwardDashApproachState(165) &&
          !EpisodePolicy::IsForwardDashApproachState(166) &&
          !EpisodePolicy::IsForwardDashApproachState(1),
          "scripted approaches accept every forward-dash phase but never walking or backdash");
    Check(EpisodePolicy::FixedGuardPracticeMode("stand") == 0 &&
          EpisodePolicy::FixedGuardPracticeMode("crouch") == 2,
          "fixed guard episodes select the native Practice stand/crouch mode");
    Check(EpisodePolicy::IsSupportedScriptAction("66B", 0) &&
          !EpisodePolicy::IsSupportedScriptAction("66B", 1) &&
          EpisodePolicy::IsSupportedScriptAction("623C", 1) &&
          !EpisodePolicy::IsSupportedScriptAction("jump", 0) &&
          !EpisodePolicy::IsSupportedScriptAction("5A>5B>2C", 0) &&
          !EpisodePolicy::IsSupportedScriptAction("63214C", 0),
          "schema, preflight, and runtime share one multi-action script vocabulary");
    Check(EpisodePolicy::UsesNativeWakeProducer(
              "macro", "trigger", "onWakeup") &&
          !EpisodePolicy::NeedsTutorialWriterLeases(
              "macro", "trigger", "onWakeup") &&
          !EpisodePolicy::UsesNativeWakeProducer(
              "macro", "taskArmed", "onWakeup") &&
          EpisodePolicy::NeedsTutorialWriterLeases(
              "macro", "taskArmed", "onWakeup"),
          "native wake pre-buffering never competes with tutorial input writers");
    Check(EpisodePolicy::SuppressUserAutoActions(true, false) &&
          !EpisodePolicy::SuppressUserAutoActions(true, true) &&
          !EpisodePolicy::SuppressUserAutoActions(false, false) &&
          EpisodePolicy::ResolveRuntimeAutoActionTarget(true, false, 1) == 0 &&
          EpisodePolicy::ResolveRuntimeAutoActionTarget(true, true, 1) == 2 &&
          EpisodePolicy::ResolveRuntimeAutoActionTarget(false, false, 1) == 1,
          "tutorials suppress user auto-actions while native wake remains a P2-only exception");
    Check(EpisodePolicy::ExpectedAttackMatches({252}, 252) &&
          !EpisodePolicy::ExpectedAttackMatches({252}, 203) &&
          EpisodePolicy::ExpectedAttackMatches({}, 264) &&
          !EpisodePolicy::ExpectedAttackMatches({}, 3),
          "episode acknowledgement accepts only its authored attack states");
    int wakeAction = -1;
    int wakeStrength = -1;
    Check(EpisodePolicy::WakeAutoActionForAction(
              "214C", wakeAction, wakeStrength) &&
          wakeAction == ACTION_QCB && wakeStrength == 2 &&
          !EpisodePolicy::WakeAutoActionForAction(
              "63214C", wakeAction, wakeStrength),
          "the authored Misaki wakeup 214C maps to native QCB strength C only");
    Check(EpisodePolicy::IsScriptAttackInstanceEdge(263, 0, 231, 8) &&
          EpisodePolicy::IsScriptAttackInstanceEdge(200, 1, 200, 8) &&
          !EpisodePolicy::IsScriptAttackInstanceEdge(200, 9, 200, 8) &&
          !EpisodePolicy::IsScriptAttackInstanceEdge(200, 3, 200, 10) &&
          !EpisodePolicy::IsScriptAttackInstanceEdge(3, 0, 200, 8),
          "script acknowledgement accepts attack cancels and same-ID re-entry only at a new instance");
    Check(EpisodePolicy::ScriptCancelDelaySatisfied(8, 8, false) &&
          !EpisodePolicy::ScriptCancelDelaySatisfied(7, 8, false) &&
          !EpisodePolicy::ScriptCancelDelaySatisfied(8, 8, true),
          "a destination cancel delay advances only after its unfrozen authored wait");
    Check(!EpisodePolicy::CanInjectContactChainFollowup(true, true) &&
          !EpisodePolicy::CanInjectContactChainFollowup(false, false) &&
          EpisodePolicy::CanInjectContactChainFollowup(true, false),
          "a scripted RG follow-up waits until the observed contact freeze ends");
    Check(EpisodePolicy::RecoilGuardAnswerDelayTicks(RG_STAND_ID) ==
              RG_STAND_FREEZE_DURATION &&
          EpisodePolicy::RecoilGuardAnswerDelayTicks(RG_CROUCH_ID) ==
              RG_CROUCH_FREEZE_DURATION &&
          EpisodePolicy::RecoilGuardAnswerDelayTicks(RG_AIR_ID) ==
              RG_AIR_FREEZE_DURATION &&
          EpisodePolicy::RecoilGuardAnswerDelayTicks(0) < 0 &&
          !EpisodePolicy::RecoilGuardAnswerDelaySatisfied(RG_STAND_ID, 0) &&
          !EpisodePolicy::RecoilGuardAnswerDelaySatisfied(
              RG_STAND_ID, RG_STAND_FREEZE_DURATION - 1) &&
          EpisodePolicy::RecoilGuardAnswerDelaySatisfied(
              RG_STAND_ID, RG_STAND_FREEZE_DURATION),
          "an rg_answer never injects on the RG edge and becomes ready at the auto-action delay");
    Check(EpisodePolicy::HasTimedCue("GET READY", 45) &&
          !EpisodePolicy::HasTimedCue("", 45) &&
          !EpisodePolicy::HasTimedCue("GET READY", 0) &&
          EpisodePolicy::CueStartsOnArm("taskArmed") &&
          EpisodePolicy::CueStartsOnArm("playerState") &&
          !EpisodePolicy::CueStartsOnArm("trigger") &&
          EpisodePolicy::CueStartsOnArm("trigger", "onWakeup") &&
          !EpisodePolicy::CueStartsOnTrigger("trigger", false) &&
          EpisodePolicy::CueStartsOnTrigger("trigger", true),
          "episode cues start on the authored arm or trigger boundary");
    uint32_t variantSeed = 0x12345u;
    const auto balancedPair =
        EpisodePolicy::BuildBalancedVariantAssignment(variantSeed, 2, 2);
    Check(balancedPair.size() == 2 && balancedPair[0] != balancedPair[1] &&
          balancedPair[0] < 2 && balancedPair[1] < 2,
          "a two-exercise variant group receives both hidden outcomes exactly once");
    const auto balancedCycles =
        EpisodePolicy::BuildBalancedVariantAssignment(variantSeed, 6, 3);
    Check(balancedCycles.size() == 6 &&
          balancedCycles[0] != balancedCycles[1] &&
          balancedCycles[0] != balancedCycles[2] &&
          balancedCycles[1] != balancedCycles[2] &&
          balancedCycles[3] != balancedCycles[4] &&
          balancedCycles[3] != balancedCycles[5] &&
          balancedCycles[4] != balancedCycles[5],
          "each complete grouped-variant cycle is a without-replacement permutation");
    Check(ShouldCarryCommitEdge(true, true, true) &&
          !ShouldCarryCommitEdge(false, true, true) &&
          !ShouldCarryCommitEdge(true, true, false),
          "a cancel button edge crosses into the next commit only on its frozen boundary");
    Check(ShouldRejectUnprovenCommit(true, true, false) &&
          !ShouldRejectUnprovenCommit(true, true, true) &&
          !ShouldRejectUnprovenCommit(false, true, false),
          "a one-use system action with the wrong button becomes a formal failure");
    Check(!ShouldResumeReviewAfterRelease(false, true, false) &&
          !ShouldResumeReviewAfterRelease(true, false, false) &&
          !ShouldResumeReviewAfterRelease(true, true, true) &&
          ShouldResumeReviewAfterRelease(true, true, false),
          "review remains frozen until its closing physical UI input is released");
    Check(FlickerICProofSatisfied(true, true, true, true, true, false, true) &&
          !FlickerICProofSatisfied(true, true, true, true, false, false, true) &&
          !FlickerICProofSatisfied(true, true, true, true, true, true, true) &&
          !FlickerICProofSatisfied(true, true, false, true, true, false, true) &&
          !FlickerICProofSatisfied(true, true, true, false, true, false, true) &&
          !FlickerICProofSatisfied(true, true, true, true, true, false, false),
          "FIC proof needs the exact cancel, C edge, safe spacing, live projectile, and no contact");
    Check(IsNewForbiddenAbsenceAttack(true, 200) &&
          IsNewForbiddenAbsenceAttack(true, 220) &&
          !IsNewForbiddenAbsenceAttack(false, 200) &&
          !IsNewForbiddenAbsenceAttack(true, 199),
          "absence grading rejects only a newly started attack, not inherited recovery");
    Check(IsDummyAttackStart(96, 258) &&
          !IsDummyAttackStart(258, 260) &&
          !IsDummyAttackEnd(false, 258, 0) &&
          IsDummyAttackEnd(true, 258, 0),
          "reversal bait cannot finish until a real dummy attack starts and ends");
    Check(IsCarryContinuity("continue") &&
          !IsCarryContinuity("sameCombo") &&
          !IsCarryContinuity("independent") &&
          ContinuationRetryHead(
              {"independent", "continue", "continue", "sameCombo"}, 2) == 0 &&
          ContinuationRetryHead(
              {"independent", "continue", "continue", "sameCombo"}, 3) == 3 &&
          ContinuationRetryHead(
              {"sameCombo", "sameCombo"}, 1) == 1,
          "only explicit continue tasks rewind through a live-state carry chain");

    Check(StatePolicy::IsCoherentField("rf") &&
          !StatePolicy::IsCoherentField("untech"),
          "resource fields remain owned by coherent_state");
    Check(StatePolicy::IsJuggleField("untech") &&
          StatePolicy::IsJuggleField("launched") &&
          StatePolicy::IsJuggleField("airtech") &&
          !StatePolicy::IsJuggleField("recoverable"),
          "juggle_state exposes only proven counters and move classifiers");
    Check(StatePolicy::Compare(24.0, "gt", 0.0) &&
          StatePolicy::Compare(0.0, "eq", 0.0) &&
          !StatePolicy::Compare(1.0, "unknown", 1.0),
          "goal-state comparison is exact and rejects unknown operators");
    Check(StatePolicy::IsUntechActive(1.0) &&
          !StatePolicy::IsUntechActive(0.0) &&
          StatePolicy::IsUntechEmpty(0.0) &&
          StatePolicy::IsUntechEmpty(-1.0),
          "juggle observation opens on positive untech and closes only when it empties");
    Check(StatePolicy::IsWakeExit(true, false) &&
          !StatePolicy::IsWakeExit(true, true) &&
          !StatePolicy::IsWakeExit(false, false),
          "wakeup timing opens on every groundtech exit, including a direct meaty hit");
    Check(StatePolicy::IsWithinWakeWindow(0, 5) &&
          StatePolicy::IsWithinWakeWindow(5, 5) &&
          !StatePolicy::IsWithinWakeWindow(6, 5) &&
          !StatePolicy::IsWithinWakeWindow(-1, 5),
          "a five-tick meaty window rejects two full actionable visual frames");
    Check(StatePolicy::BranchPhaseForOutcomeEdge(true, false) == 1 &&
          StatePolicy::BranchPhaseForOutcomeEdge(false, true) == 2 &&
          StatePolicy::BranchPhaseForOutcomeEdge(false, false) == 3,
          "hit-confirm starter consumes same-snapshot hit/block edges instead of losing them");

    DemoResumeTarget target = DecideDemoResumeTarget(
        Phase::Feedback, true, true, 2, -1);
    Check(target.phase == Phase::Complete && target.task == -1,
          "a demo after final-task success resumes at lesson completion");

    target = DecideDemoResumeTarget(Phase::Feedback, true, false, 1, 2);
    Check(target.phase == Phase::TaskActive && target.task == 2,
          "a demo after mid-lesson success resumes at the first pending task");

    target = DecideDemoResumeTarget(Phase::Feedback, false, false, 1, 0);
    Check(target.phase == Phase::TaskActive && target.task == 1,
          "a demo after failed feedback retries the same task");

    target = DecideDemoResumeTarget(Phase::TaskActive, true, false, 2, 1);
    Check(target.phase == Phase::TaskActive && target.task == 1,
          "an active lesson resumes at its first pending task");

    target = DecideDemoResumeTarget(Phase::Review, true, true, 2, -1);
    Check(target.phase == Phase::Review && target.task == -1,
          "a demonstration opened while reviewing returns to the review page");

    target = DecideDemoResumeTarget(Phase::ConfirmExit, true, false, 0, 0);
    Check(target.phase == Phase::ConfirmExit && target.task == -1,
          "a demonstration preserves the page exit confirmation");

    std::cout << "tutorial_session_policy_tests passed\n";
    return 0;
}
