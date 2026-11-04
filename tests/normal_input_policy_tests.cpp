#include "input/normal_input_policy.h"
#include "input/input_hook.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "normal_input_policy_tests: " << message << '\n';
        std::exit(1);
    }
}

void CheckMotionGroup(int firstMotion,
                      NormalInputPolicy::RelativeDirection direction,
                      bool airborne,
                      uint8_t rightDirection,
                      uint8_t leftDirection) {
    constexpr uint8_t buttons[4] = {
        NormalInputPolicy::kInputA,
        NormalInputPolicy::kInputB,
        NormalInputPolicy::kInputC,
        NormalInputPolicy::kInputD,
    };
    for (int index = 0; index < 4; ++index) {
        const auto intent = NormalInputPolicy::IntentFromMotion(firstMotion + index);
        Require(static_cast<bool>(intent), "normal motion did not produce an intent");
        Require(intent.direction == direction, "normal direction mapping drifted");
        Require(intent.button == buttons[index], "normal button mapping drifted");
        Require(intent.airborne == airborne, "normal posture mapping drifted");
        Require(NormalInputPolicy::ResolveMask(intent, true) ==
                    static_cast<uint8_t>(rightDirection | buttons[index]),
                "right-facing mask is wrong");
        Require(NormalInputPolicy::ResolveMask(intent, false) ==
                    static_cast<uint8_t>(leftDirection | buttons[index]),
                "left-facing mask is wrong");
    }
}

void TestAllNormalMappings() {
    CheckMotionGroup(MOTION_5A, NormalInputPolicy::RelativeDirection::Neutral,
                     false, 0, 0);
    CheckMotionGroup(MOTION_2A, NormalInputPolicy::RelativeDirection::Down,
                     false, NormalInputPolicy::kInputDown,
                     NormalInputPolicy::kInputDown);
    CheckMotionGroup(MOTION_JA, NormalInputPolicy::RelativeDirection::Neutral,
                     true, 0, 0);
    CheckMotionGroup(MOTION_6A, NormalInputPolicy::RelativeDirection::Forward,
                     false, NormalInputPolicy::kInputRight,
                     NormalInputPolicy::kInputLeft);
    CheckMotionGroup(MOTION_4A, NormalInputPolicy::RelativeDirection::Back,
                     false, NormalInputPolicy::kInputLeft,
                     NormalInputPolicy::kInputRight);
    CheckMotionGroup(MOTION_1A, NormalInputPolicy::RelativeDirection::DownBack,
                     false,
                     NormalInputPolicy::kInputDown | NormalInputPolicy::kInputLeft,
                     NormalInputPolicy::kInputDown | NormalInputPolicy::kInputRight);
    CheckMotionGroup(MOTION_3A, NormalInputPolicy::RelativeDirection::DownForward,
                     false,
                     NormalInputPolicy::kInputDown | NormalInputPolicy::kInputRight,
                     NormalInputPolicy::kInputDown | NormalInputPolicy::kInputLeft);
    CheckMotionGroup(MOTION_J2A, NormalInputPolicy::RelativeDirection::Down,
                     true, NormalInputPolicy::kInputDown,
                     NormalInputPolicy::kInputDown);
    CheckMotionGroup(MOTION_J6A, NormalInputPolicy::RelativeDirection::Forward,
                     true, NormalInputPolicy::kInputRight,
                     NormalInputPolicy::kInputLeft);

    const auto ground = NormalInputPolicy::IntentFromMotion(MOTION_5B);
    const auto air = NormalInputPolicy::IntentFromMotion(MOTION_JB);
    Require(NormalInputPolicy::PostureEligible(ground, true, false),
            "ground normal rejected grounded actionability");
    Require(!NormalInputPolicy::PostureEligible(ground, false, true),
            "ground normal was allowed in the air");
    Require(NormalInputPolicy::PostureEligible(air, false, true),
            "air normal rejected air actionability");
    Require(!NormalInputPolicy::PostureEligible(air, true, false),
            "air normal was allowed on the ground");

    Require(!NormalInputPolicy::IsNormalMotion(MOTION_236A),
            "a special was classified as a normal");

    NormalInputPolicy::Intent dashLow{
        NormalInputPolicy::RelativeDirection::DownForward,
        NormalInputPolicy::kInputB,
        false,
    };
    Require(NormalInputPolicy::ResolveMask(dashLow, true) ==
                (NormalInputPolicy::kInputDown |
                 NormalInputPolicy::kInputRight |
                 NormalInputPolicy::kInputB),
            "right-facing dash-low mask is wrong");
    Require(NormalInputPolicy::ResolveMask(dashLow, false) ==
                (NormalInputPolicy::kInputDown |
                 NormalInputPolicy::kInputLeft |
                 NormalInputPolicy::kInputB),
            "left-facing dash-low mask is wrong");

    const uint8_t downB = NormalInputPolicy::kInputDown |
                          NormalInputPolicy::kInputB;
    Require(NormalInputPolicy::NativeHistorySample(
                downB, NormalInputPolicy::kInputB, false) == downB,
            "native history lost a new button edge");
    Require(NormalInputPolicy::NativeHistorySample(
                downB, NormalInputPolicy::kInputB, true) ==
                NormalInputPolicy::kInputDown,
            "native history incorrectly stored a held button as a new edge");
}

void TestUnheldPulseProtocol() {
    NormalInputPolicy::PulseState state;
    uint64_t generation = 0;
    const auto intent = NormalInputPolicy::IntentFromMotion(MOTION_5B);
    Require(state.Submit(intent, NormalInputPolicy::Timing::WhenActionable,
                         &generation) ==
                NormalInputPolicy::SubmitResult::Accepted,
            "first pulse was not accepted");
    Require(generation != 0, "accepted pulse has no generation");
    Require(state.Current().phase == NormalInputPolicy::Phase::AwaitingStart,
            "pulse did not begin in AwaitingStart");
    Require(state.Prepare(false), "unheld pulse could not be prepared");

    auto press = state.Current();
    Require(press.phase == NormalInputPolicy::Phase::Press,
            "unheld pulse inserted an unnecessary pre-neutral");
    auto stale = press;
    ++stale.generation;
    Require(!state.Acknowledge(stale), "stale generation advanced the pulse");
    Require(state.Acknowledge(press), "matching press was not acknowledged");
    Require(state.Current().phase == NormalInputPolicy::Phase::ReleaseNeutral,
            "press did not require a native neutral release");

    const auto release = state.Current();
    Require(state.Acknowledge(release), "release neutral was not acknowledged");
    Require(!state.Active(), "pulse stayed active after its neutral release");
}

void TestHeldAndRepeatedButtonProtocol() {
    NormalInputPolicy::PulseState state;
    const auto intent = NormalInputPolicy::IntentFromMotion(MOTION_5B);
    uint64_t firstGeneration = 0;
    Require(state.Submit(intent, NormalInputPolicy::Timing::WhenActionable,
                          &firstGeneration) ==
                NormalInputPolicy::SubmitResult::Accepted,
            "held-button pulse was not accepted");
    uint64_t queuedGeneration = 0;
    Require(state.Submit(intent, NormalInputPolicy::Timing::WhenActionable,
                         &queuedGeneration) ==
                NormalInputPolicy::SubmitResult::Accepted,
            "release-tick successor was dropped");
    Require(queuedGeneration != 0 && queuedGeneration != firstGeneration,
            "queued successor has no distinct generation");
    Require(state.HasPending(), "accepted successor was not retained");
    Require(state.Submit(intent, NormalInputPolicy::Timing::WhenActionable) ==
                NormalInputPolicy::SubmitResult::Busy,
            "a full successor slot was overwritten");
    Require(state.Prepare(true), "held-button pulse could not be prepared");

    const auto preNeutral = state.Current();
    Require(preNeutral.phase == NormalInputPolicy::Phase::PreNeutral,
            "held button did not receive a pre-neutral edge");
    Require(state.Acknowledge(preNeutral), "pre-neutral was not acknowledged");
    const auto press = state.Current();
    Require(press.phase == NormalInputPolicy::Phase::Press,
            "pre-neutral did not advance to press");
    Require(state.Acknowledge(press), "held-button press was not acknowledged");
    Require(state.Acknowledge(state.Current()),
            "held-button release was not acknowledged");
    Require(state.Current().phase == NormalInputPolicy::Phase::AwaitingStart &&
                state.Current().generation == queuedGeneration,
            "successor was not promoted after neutral release");
    Require(!state.HasPending(), "promoted successor remained pending");
    Require(state.Prepare(false), "promoted successor could not start");
    Require(state.Acknowledge(state.Current()),
            "successor press was not acknowledged");
    Require(state.Acknowledge(state.Current()),
            "successor release was not acknowledged");
    Require(!state.Active(), "successor did not complete");

    uint64_t afterQueueGeneration = 0;
    Require(state.Submit(intent, NormalInputPolicy::Timing::WhenActionable,
                         &afterQueueGeneration) ==
                NormalInputPolicy::SubmitResult::Accepted,
            "same button could not be queued after completion");
    Require(afterQueueGeneration != queuedGeneration,
            "completed queue reused a generation");
    Require(state.Submit(intent, NormalInputPolicy::Timing::Immediate) ==
                NormalInputPolicy::SubmitResult::Busy,
            "exact-tick immediate pulse was incorrectly deferred");
    Require(state.Submit(intent, NormalInputPolicy::Timing::WhenActionable) ==
                NormalInputPolicy::SubmitResult::Accepted,
            "reset test could not queue a successor");
    state.Reset();
    Require(!state.Active() && !state.HasPending(),
            "reset left active or pending pulse state");

    Require(state.Submit(intent, NormalInputPolicy::Timing::Immediate) ==
                NormalInputPolicy::SubmitResult::Accepted,
            "idle exact-tick pulse was not accepted");
    Require(state.Submit(intent, NormalInputPolicy::Timing::WhenActionable) ==
                NormalInputPolicy::SubmitResult::Busy,
            "successor was incorrectly accepted behind an exact-tick pulse");
    Require(!state.HasPending(),
            "rejected exact-tick successor occupied the pending slot");
    state.Reset();
}

void TestPreparedPulseCanRelinquishAndRetry() {
    NormalInputPolicy::PulseState state;
    const auto intent = NormalInputPolicy::IntentFromMotion(MOTION_5B);
    uint64_t generation = 0;
    Require(state.Submit(intent, NormalInputPolicy::Timing::WhenActionable,
                         &generation) ==
                NormalInputPolicy::SubmitResult::Accepted,
            "retry pulse was not accepted");
    Require(state.Prepare(true), "retry pulse could not enter pre-neutral");
    Require(state.RestartAwaiting(),
            "pre-neutral pulse could not relinquish ownership");
    Require(state.Current().phase == NormalInputPolicy::Phase::AwaitingStart &&
                state.Current().generation == generation,
            "relinquished pulse lost its request identity");
    Require(state.Prepare(false), "relinquished pulse could not restart");
    Require(state.RestartAwaiting(),
            "prepared press could not relinquish ownership");
    Require(state.Prepare(false), "second retry could not restart");
    Require(state.Acknowledge(state.Current()),
            "retried press was not acknowledged");
    Require(!state.RestartAwaiting(),
            "release-neutral was incorrectly restartable");
    Require(state.Acknowledge(state.Current()),
            "retried release was not acknowledged");
}

void TestRetirePromotesAcceptedSuccessor() {
    NormalInputPolicy::PulseState state;
    const auto first = NormalInputPolicy::IntentFromMotion(MOTION_5A);
    const auto second = NormalInputPolicy::IntentFromMotion(MOTION_2B);
    uint64_t firstGeneration = 0;
    uint64_t secondGeneration = 0;
    Require(state.Submit(first, NormalInputPolicy::Timing::WhenActionable,
                         &firstGeneration) ==
                NormalInputPolicy::SubmitResult::Accepted,
            "retire test first pulse was not accepted");
    Require(state.Submit(second, NormalInputPolicy::Timing::WhenActionable,
                         &secondGeneration) ==
                NormalInputPolicy::SubmitResult::Accepted,
            "retire test successor was not accepted");
    Require(state.RetireCurrent(),
            "retiring current pulse did not promote its successor");
    const auto promoted = state.Current();
    Require(promoted.generation == secondGeneration &&
                promoted.phase == NormalInputPolicy::Phase::AwaitingStart &&
                promoted.intent.button == second.button &&
                promoted.intent.direction == second.direction,
            "retired pulse lost or changed its accepted successor");
    Require(!state.HasPending(),
            "promoted retire successor remained in the pending slot");
    Require(state.RetireCurrent() == false && !state.Active(),
            "retiring final pulse did not return to idle");
}

void TestMissionStartupPollRouting() {
    using InputHookPolicy::LogicalPollPlayer;
    using InputHookPolicy::P1StartupNeutralApplies;
    Require(LogicalPollPlayer(1, 2) == 1 &&
                LogicalPollPlayer(2, 1) == 2 &&
                LogicalPollPlayer(0, 1) == 1,
            "startup gate did not preserve process-context routing priority");
    Require(P1StartupNeutralApplies(true, 1, 2),
            "startup gate missed context-routed P1 input");
    Require(P1StartupNeutralApplies(true, 1, 1),
            "startup gate missed ordinary P1 character polling");
    Require(!P1StartupNeutralApplies(true, 2, 1) &&
                !P1StartupNeutralApplies(true, 0, 1) &&
                !P1StartupNeutralApplies(false, 1, 1),
            "startup gate suppressed P2, menu input, or an inactive gate");
}

// Recoil Guard is two PAT rows, both under move ID 168/169/170: row 0 is the defender freeze
// (cancel bit clear), rows 1+ the RG advantage window (cancel bit set).
void TestRecoilGuardCancelEligibility() {
    using NormalInputPolicy::RecoilGuardCancelEligible;

    Require(!RecoilGuardCancelEligible(true, false, 10, 30, true),
            "RG freeze row must not open the press window");
    Require(!RecoilGuardCancelEligible(true, false, 10, 10, false),
            "RG freeze row must not open the press window without an anchor");

    Require(RecoilGuardCancelEligible(true, true, 10, 10, true),
            "5A/2A (tier 10) must be startable from the RG advantage window");
    Require(RecoilGuardCancelEligible(true, true, 10, 20, true),
            "5B/2B (tier 20) must be startable from the RG advantage window");
    Require(RecoilGuardCancelEligible(true, true, 10, 30, true),
            "5C/2C (tier 30) must be startable from the RG advantage window");

    Require(RecoilGuardCancelEligible(true, true, 10, 0, false),
            "an anchorless intent must fall back to the cancel bit");

    Require(!RecoilGuardCancelEligible(true, true, 30, 10, true),
            "rank comparison must still reject a lower-tier destination");

    Require(!RecoilGuardCancelEligible(false, true, 10, 30, true),
            "non-RG states must not reach the RG relaxation");
}

// Must match DidConsumerStartRequestedNormal: neutral A/B/C -> 200/201/203,
// down A/B/C -> 204/205/206, everything else anchorless.
void TestGroundNormalRankAnchors() {
    using NormalInputPolicy::GroundNormalRankAnchor;
    using NormalInputPolicy::IntentFromMotion;

    Require(GroundNormalRankAnchor(IntentFromMotion(MOTION_5A)) == 200, "5A anchor drifted");
    Require(GroundNormalRankAnchor(IntentFromMotion(MOTION_5B)) == 201, "5B anchor drifted");
    Require(GroundNormalRankAnchor(IntentFromMotion(MOTION_5C)) == 203, "5C anchor drifted");
    Require(GroundNormalRankAnchor(IntentFromMotion(MOTION_2A)) == 204, "2A anchor drifted");
    Require(GroundNormalRankAnchor(IntentFromMotion(MOTION_2B)) == 205, "2B anchor drifted");
    Require(GroundNormalRankAnchor(IntentFromMotion(MOTION_2C)) == 206, "2C anchor drifted");

    Require(GroundNormalRankAnchor(IntentFromMotion(MOTION_5D)) == -1, "5D must be anchorless");
    Require(GroundNormalRankAnchor(IntentFromMotion(MOTION_2D)) == -1, "2D must be anchorless");
    Require(GroundNormalRankAnchor(IntentFromMotion(MOTION_6C)) == -1, "6C must be anchorless");
    Require(GroundNormalRankAnchor(IntentFromMotion(MOTION_4B)) == -1, "4B must be anchorless");
    Require(GroundNormalRankAnchor(IntentFromMotion(MOTION_JC)) == -1, "air intents have no ground anchor");
}

}

int main() {
    using Tail = InputHookPolicy::TailCleanupPhase;
    Require(InputHookPolicy::AfterTokenNeutralize(Tail::NeutralOnly) == Tail::Idle &&
            InputHookPolicy::AfterTokenNeutralize(Tail::NeutralThenCleanup) == Tail::AwaitingStableHead,
            "token cleanup keeps the requested full-tail obligation");
    Require(InputHookPolicy::AfterTokenNeutralize(Tail::AwaitingStableHead) == Tail::AwaitingStableHead,
            "stable-head wait does not rearm token neutralization");

    TestAllNormalMappings();
    TestUnheldPulseProtocol();
    TestHeldAndRepeatedButtonProtocol();
    TestPreparedPulseCanRelinquishAndRetry();
    TestRetirePromotesAcceptedSuccessor();
    TestMissionStartupPollRouting();
    TestRecoilGuardCancelEligibility();
    TestGroundNormalRankAnchors();
    std::cout << "normal_input_policy_tests passed\n";
    return 0;
}
