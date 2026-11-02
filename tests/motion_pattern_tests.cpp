#include "input/motion_pattern.h"
#include "input/auto_action_motion_policy.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "motion_pattern_tests: " << message << '\n';
        std::exit(1);
    }
}

std::vector<uint8_t> MirrorIndependently(std::vector<uint8_t> pattern) {
    for (uint8_t& value : pattern) {
        const uint8_t horizontal = static_cast<uint8_t>(
            value & (GAME_INPUT_LEFT | GAME_INPUT_RIGHT));
        value = static_cast<uint8_t>(
            value & ~(GAME_INPUT_LEFT | GAME_INPUT_RIGHT));
        if (horizontal == GAME_INPUT_LEFT) value |= GAME_INPUT_RIGHT;
        if (horizontal == GAME_INPUT_RIGHT) value |= GAME_INPUT_LEFT;
        if (horizontal == (GAME_INPUT_LEFT | GAME_INPUT_RIGHT)) {
            value |= horizontal;
        }
    }
    return pattern;
}

struct MotionFamily {
    const char* name;
    std::array<int, 4> motions;
    std::vector<uint8_t> rightDirections;
};

void TestSpecialFamilies() {
    const uint8_t R = GAME_INPUT_RIGHT;
    const uint8_t L = GAME_INPUT_LEFT;
    const uint8_t D = GAME_INPUT_DOWN;
    const uint8_t DR = static_cast<uint8_t>(D | R);
    const uint8_t DL = static_cast<uint8_t>(D | L);
    const std::array<uint8_t, 4> buttons = {
        GAME_INPUT_A, GAME_INPUT_B, GAME_INPUT_C, GAME_INPUT_D,
    };

    const std::vector<MotionFamily> families = {
        {"623", {MOTION_623A, MOTION_623B, MOTION_623C, MOTION_623D},
         {0, 0, R, R, R, D, D, DR, DR, DR}},
        {"214", {MOTION_214A, MOTION_214B, MOTION_214C, MOTION_214D},
         {0, 0, D, D, D, DL, DL, L, L, L}},
        {"236", {MOTION_236A, MOTION_236B, MOTION_236C, MOTION_236D},
         {0, 0, D, D, D, DR, DR, R, R, R}},
        {"421", {MOTION_421A, MOTION_421B, MOTION_421C, MOTION_421D},
         {0, 0, L, L, D, D, DL, DL, DL}},
        {"41236", {MOTION_41236A, MOTION_41236B, MOTION_41236C, MOTION_41236D},
         {0, 0, L, L, DL, DL, D, D, DR, DR, R, R}},
        {"214214", {MOTION_214214A, MOTION_214214B, MOTION_214214C, MOTION_214214D},
         {0, 0, D, D, D, DL, DL, L, L, D, D, D, DL, DL, L, L, L}},
        {"236236", {MOTION_236236A, MOTION_236236B, MOTION_236236C, MOTION_236236D},
         {0, 0, D, D, D, DR, DR, R, R, D, D, D, DR, DR, R, R, R}},
        {"641236", {MOTION_641236A, MOTION_641236B, MOTION_641236C, MOTION_641236D},
         {0, 0, R, R, L, L, DL, DL, D, D, DR, DR, R, R}},
        {"412", {MOTION_412A, MOTION_412B, MOTION_412C, MOTION_412D},
         {0, 0, L, L, DL, DL, D, D}},
        {"22", {MOTION_22A, MOTION_22B, MOTION_22C, MOTION_22D},
         {0, 0, D, D, 0, 0, D, D}},
        {"2141236", {MOTION_2141236A, MOTION_2141236B, MOTION_2141236C, MOTION_2141236D},
         {0, 0, D, D, DL, DL, L, L, DL, DL, D, D, DR, DR, R, R}},
        {"463214", {MOTION_463214A, MOTION_463214B, MOTION_463214C, MOTION_463214D},
         {0, 0, L, L, R, R, DR, DR, D, D, DL, DL, L, L}},
        {"4123641236",
         {MOTION_4123641236A, MOTION_4123641236B,
          MOTION_4123641236C, MOTION_4123641236D},
         {0, 0, L, L, DL, DL, D, D, DR, DR, R, R,
          L, L, DL, DL, D, D, DR, DR, R, R}},
        {"6321463214",
         {MOTION_6321463214A, MOTION_6321463214B,
          MOTION_6321463214C, MOTION_6321463214D},
         {0, 0, R, R, DR, DR, D, D, DL, DL, L, L,
          R, R, DR, DR, D, D, DL, DL, L, L}},
    };

    for (const MotionFamily& family : families) {
        for (size_t buttonIndex = 0; buttonIndex < buttons.size(); ++buttonIndex) {
            std::vector<uint8_t> expectedRight = family.rightDirections;
            expectedRight.back() = static_cast<uint8_t>(
                expectedRight.back() | buttons[buttonIndex]);

            std::vector<uint8_t> right;
            Require(MotionPattern::Build(family.motions[buttonIndex],
                                         buttons[buttonIndex], true, right),
                    std::string(family.name) + " right-facing build failed");
            Require(right == expectedRight,
                    std::string(family.name) + " right-facing sequence drifted");
            Require(right.size() <= P1_INPUT_BUFFER_SIZE,
                    std::string(family.name) + " exceeded EFZ's ring size");
            Require(right.size() >= 3 && right[0] == 0 && right[1] == 0,
                    std::string(family.name) + " lost neutral padding");
            for (size_t i = 0; i + 1 < right.size(); ++i) {
                Require((right[i] & 0xF0u) == 0,
                        std::string(family.name) + " pressed a button before its final poll");
            }
            Require((right.back() & 0xF0u) == buttons[buttonIndex],
                    std::string(family.name) + " final button is wrong");

            const std::vector<uint8_t> expectedLeft =
                MirrorIndependently(expectedRight);
            std::vector<uint8_t> left;
            Require(MotionPattern::Build(family.motions[buttonIndex],
                                         buttons[buttonIndex], false, left),
                    std::string(family.name) + " left-facing build failed");
            Require(left == expectedLeft,
                    std::string(family.name) + " left-facing sequence drifted");

            std::vector<uint8_t> mirrored = right;
            MotionPattern::MirrorHorizontally(mirrored);
            Require(mirrored == left,
                    std::string(family.name) + " runtime facing mirror drifted");
            MotionPattern::MirrorHorizontally(mirrored);
            Require(mirrored == right,
                    std::string(family.name) + " double mirror was not reversible");
        }
    }

    std::vector<uint8_t> qcb;
    std::vector<uint8_t> reverseDp;
    Require(MotionPattern::Build(MOTION_214A, GAME_INPUT_A, true, qcb),
            "214 regression build failed");
    Require(MotionPattern::Build(MOTION_421A, GAME_INPUT_A, true, reverseDp),
            "421 regression build failed");
    Require(reverseDp != qcb,
            "421 regressed to the old, incorrect 214 queue encoding");
}

void TestDashesAndMirrorEdges() {
    std::vector<uint8_t> pattern;
    Require(MotionPattern::Build(MOTION_FORWARD_DASH, 0, true, pattern),
            "right-facing forward dash failed");
    Require(pattern == std::vector<uint8_t>({0, 0, GAME_INPUT_RIGHT,
                                             GAME_INPUT_RIGHT, 0,
                                             GAME_INPUT_RIGHT}),
            "right-facing forward dash sequence drifted");
    Require(MotionPattern::Build(MOTION_BACK_DASH, 0, true, pattern),
            "right-facing backdash failed");
    Require(pattern == std::vector<uint8_t>({0, 0, GAME_INPUT_LEFT,
                                             GAME_INPUT_LEFT, 0,
                                             GAME_INPUT_LEFT}),
            "right-facing backdash sequence drifted");
    Require(MotionPattern::Build(MOTION_FORWARD_DASH, 0, false, pattern),
            "left-facing forward dash failed");
    Require(pattern == std::vector<uint8_t>({0, 0, GAME_INPUT_LEFT,
                                             GAME_INPUT_LEFT, 0,
                                             GAME_INPUT_LEFT}),
            "left-facing forward dash was not mirrored exactly");
    Require(MotionPattern::Build(MOTION_BACK_DASH, 0, false, pattern),
            "left-facing backdash failed");
    Require(pattern == std::vector<uint8_t>({0, 0, GAME_INPUT_RIGHT,
                                             GAME_INPUT_RIGHT, 0,
                                             GAME_INPUT_RIGHT}),
            "left-facing backdash was not mirrored exactly");
    for (uint8_t value : pattern) {
        Require((value & 0xF0u) == 0, "dash unexpectedly contains a button");
    }

    std::vector<uint8_t> mixed = {
        static_cast<uint8_t>(GAME_INPUT_DOWN | GAME_INPUT_LEFT | GAME_INPUT_A),
        static_cast<uint8_t>(GAME_INPUT_LEFT | GAME_INPUT_RIGHT |
                             GAME_INPUT_DOWN | GAME_INPUT_D),
    };
    MotionPattern::MirrorHorizontally(mixed);
    Require(mixed[0] == static_cast<uint8_t>(
                GAME_INPUT_DOWN | GAME_INPUT_RIGHT | GAME_INPUT_A),
            "mirror damaged down/button bits");
    Require(mixed[1] == static_cast<uint8_t>(
                GAME_INPUT_LEFT | GAME_INPUT_RIGHT |
                GAME_INPUT_DOWN | GAME_INPUT_D),
            "mirror changed an intentionally mixed horizontal mask");

    for (int raw = 0; raw <= 0xFF; ++raw) {
        std::vector<uint8_t> value = {static_cast<uint8_t>(raw)};
        const std::vector<uint8_t> expected = MirrorIndependently(value);
        MotionPattern::MirrorHorizontally(value);
        Require(value == expected,
                "horizontal mirror drifted for raw mask " +
                    std::to_string(raw));
        MotionPattern::MirrorHorizontally(value);
        Require(value[0] == static_cast<uint8_t>(raw),
                "horizontal mirror was not reversible for raw mask " +
                    std::to_string(raw));
    }
}

void TestButtonContractAndInvalidMotions() {
    const std::array<uint8_t, 4> buttons = {
        GAME_INPUT_A, GAME_INPUT_B, GAME_INPUT_C, GAME_INPUT_D,
    };
    const std::array<int, 4> qcf = {
        MOTION_236A, MOTION_236B, MOTION_236C, MOTION_236D,
    };
    for (size_t i = 0; i < qcf.size(); ++i) {
        Require(MotionPattern::ExpectedButtonForMotion(qcf[i]) == buttons[i],
                "expected-button mapping drifted");
        for (size_t j = 0; j < buttons.size(); ++j) {
            std::vector<uint8_t> pattern = {1, 2, 3};
            const bool built = MotionPattern::Build(qcf[i], buttons[j], true,
                                                     pattern);
            Require(built == (i == j),
                    "motion suffix accepted the wrong attack button");
            if (i != j) {
                Require(pattern.empty(),
                        "rejected button mismatch retained a stale pattern");
            }
        }
    }

    const std::array<uint8_t, 6> malformed = {
        0,
        static_cast<uint8_t>(GAME_INPUT_A | GAME_INPUT_B),
        static_cast<uint8_t>(GAME_INPUT_A | GAME_INPUT_RIGHT),
        static_cast<uint8_t>(GAME_INPUT_C | GAME_INPUT_DOWN),
        static_cast<uint8_t>(GAME_INPUT_A | GAME_INPUT_D),
        0xFFu,
    };
    for (uint8_t mask : malformed) {
        std::vector<uint8_t> pattern = {1, 2, 3};
        Require(!MotionPattern::Build(MOTION_236A, mask, true, pattern),
                "malformed special button mask was accepted");
        Require(pattern.empty(),
                "malformed special button mask retained stale output");
    }

    std::vector<uint8_t> pattern = {1, 2, 3};
    Require(!MotionPattern::Build(MOTION_FORWARD_DASH, GAME_INPUT_A, true,
                                  pattern),
            "dash accepted an attack button");
    Require(pattern.empty(), "rejected dash retained a stale pattern");

    pattern = {1, 2, 3};
    Require(!MotionPattern::Build(MOTION_NONE, GAME_INPUT_A, true, pattern),
            "invalid motion was accepted");
    Require(pattern.empty(), "invalid motion retained a stale pattern");
}

void TestRingPlacementAndConsumerPolicy() {
    using AutoActionMotionPolicy::PlanRingPlacement;

    Require(AutoActionMotionPolicy::OwnsCancellationGeneration(true, 42, 42),
            "matching active generation did not own cancellation");
    Require(!AutoActionMotionPolicy::OwnsCancellationGeneration(true, 43, 42),
            "stale generation could cancel its successor");
    Require(!AutoActionMotionPolicy::OwnsCancellationGeneration(false, 42, 42),
            "inactive transaction accepted cancellation");
    Require(!AutoActionMotionPolicy::OwnsCancellationGeneration(true, 0, 0),
            "zero generation accepted cancellation");

    Require(AutoActionMotionPolicy::GenericConsumerGateOpen(0, 0, 0),
            "open native consumer gate was rejected");
    Require(!AutoActionMotionPolicy::GenericConsumerGateOpen(1, 0, 0),
            "own +330 early-return gate was ignored");
    Require(!AutoActionMotionPolicy::GenericConsumerGateOpen(0, 1, 0),
            "own superflash early-return gate was ignored");
    Require(!AutoActionMotionPolicy::GenericConsumerGateOpen(0, 0, 1),
            "opponent +332 early-return gate was ignored");

    const auto ordinary = PlanRingPlacement(50, 10, P1_INPUT_BUFFER_SIZE);
    Require(ordinary.valid, "ordinary ring placement was rejected");
    Require(ordinary.prefixStart == 41 && ordinary.prefixLength == 9,
            "ordinary prefix was not staged immediately behind the head");
    Require(ordinary.ownedStart == 0 &&
                ordinary.ownedLength == P1_INPUT_BUFFER_SIZE,
            "transaction did not claim the complete detector history");
    Require(ordinary.expectedHeadAfterPoll == 51,
            "ordinary native head acknowledgement is wrong");

    const auto wrappedPrefix = PlanRingPlacement(3, 10, P1_INPUT_BUFFER_SIZE);
    Require(wrappedPrefix.valid && wrappedPrefix.prefixStart == 174 &&
                wrappedPrefix.expectedHeadAfterPoll == 4,
            "prefix wrap placement is wrong");
    const auto wrappedHead = PlanRingPlacement(P1_INPUT_BUFFER_SIZE - 1, 6,
                                               P1_INPUT_BUFFER_SIZE);
    Require(wrappedHead.valid && wrappedHead.prefixStart == 174 &&
                wrappedHead.expectedHeadAfterPoll == 0,
            "head wrap acknowledgement is wrong");
    Require(!PlanRingPlacement(P1_INPUT_BUFFER_SIZE, 6,
                               P1_INPUT_BUFFER_SIZE).valid,
            "out-of-range ring head was accepted");
    Require(!PlanRingPlacement(0, 0, P1_INPUT_BUFFER_SIZE).valid,
            "empty transaction pattern was accepted");
    Require(!PlanRingPlacement(0, P1_INPUT_BUFFER_SIZE + 1,
                               P1_INPUT_BUFFER_SIZE).valid,
            "oversized transaction pattern was accepted");
    const auto oneByte = PlanRingPlacement(7, 1, P1_INPUT_BUFFER_SIZE);
    Require(oneByte.valid && oneByte.prefixLength == 0 &&
                oneByte.ownedStart == 0 &&
                oneByte.ownedLength == P1_INPUT_BUFFER_SIZE &&
                oneByte.expectedHeadAfterPoll == 8,
            "single-poll transaction placement is wrong");
    const auto fullRing = PlanRingPlacement(17, P1_INPUT_BUFFER_SIZE,
                                            P1_INPUT_BUFFER_SIZE);
    Require(fullRing.valid && fullRing.prefixLength ==
                P1_INPUT_BUFFER_SIZE - 1 && fullRing.prefixStart == 18 &&
                fullRing.ownedStart == 0 &&
                fullRing.ownedLength == P1_INPUT_BUFFER_SIZE,
            "full-ring transaction placement is wrong");

    for (uint16_t ringSize = 1; ringSize <= P1_INPUT_BUFFER_SIZE; ++ringSize) {
        for (uint16_t head = 0; head < ringSize; ++head) {
            for (size_t length = 1; length <= ringSize; ++length) {
                const auto plan = PlanRingPlacement(head, length, ringSize);
                Require(plan.valid, "valid exhaustive ring placement was rejected");
                Require(plan.prefixLength == length - 1,
                        "exhaustive prefix length drifted");
                Require(plan.prefixStart < ringSize,
                        "exhaustive prefix start escaped the ring");
                Require(plan.expectedHeadAfterPoll ==
                            static_cast<uint16_t>((head + 1) % ringSize),
                        "exhaustive native head acknowledgement drifted");
                Require(plan.ownedStart == 0 &&
                            plan.ownedLength == ringSize,
                        "exhaustive ownership stopped covering detector history");
                const uint16_t expectedStart = static_cast<uint16_t>(
                    (head + ringSize - (length - 1)) % ringSize);
                Require(plan.prefixStart == expectedStart,
                        "exhaustive wrapped prefix placement drifted");
            }
        }
    }

    constexpr uint16_t noToken = 99;
    using AutoActionMotionPolicy::ConsumerAccepted;
    Require(ConsumerAccepted(MOTION_236A, 0, 0, 250, 0, 10, noToken),
            "recognized special transition was rejected");
    Require(!ConsumerAccepted(MOTION_236A, 0, 0, 250, 0,
                              noToken, noToken),
            "special without a detector token was accepted");
    Require(!ConsumerAccepted(MOTION_236A, 250, 3, 250, 4, 10, noToken),
            "unchanged special instance was accepted");
    Require(ConsumerAccepted(MOTION_236A, 250, 5, 250, 0, 10, noToken),
            "same-move restarted action instance was rejected");
    Require(ConsumerAccepted(MOTION_22C, 250, 5,
                             GROUND_IC_ID, 0, 52, noToken),
            "ground IC command transition was rejected");
    Require(ConsumerAccepted(MOTION_22C, 250, 5,
                             AIR_IC_ID, 0, 52, noToken),
            "air IC command transition was rejected");
    Require(!ConsumerAccepted(MOTION_22C, 0, 0,
                              250, 0, 52, noToken),
            "22C transaction accepted a non-IC special");
    Require(!ConsumerAccepted(MOTION_22C, 0, 0,
                              200, 0, 52, noToken),
            "22C transaction accepted an arbitrary attack");
    Require(!ConsumerAccepted(MOTION_22C, 250, 5,
                              GROUND_IC_ID, 0, noToken, noToken),
            "IC transition without a detector token was accepted");
    Require(!ConsumerAccepted(MOTION_22C, GROUND_IC_ID, 3,
                              GROUND_IC_ID, 4, 52, noToken),
            "unchanged IC action instance was accepted");
    Require(!ConsumerAccepted(MOTION_236A, 250, 5,
                              GROUND_IC_ID, 0, 10, noToken),
            "non-22C transaction accepted ground IC");
    Require(!ConsumerAccepted(MOTION_236A, 250, 5,
                              AIR_IC_ID, 0, 10, noToken),
            "non-22C transaction accepted air IC");
    Require(ConsumerAccepted(MOTION_FORWARD_DASH, 0, 0,
                             FORWARD_DASH_START_ID, 0, 0, noToken),
            "forward dash transition was rejected");
    Require(ConsumerAccepted(MOTION_BACK_DASH, 0, 0,
                             164, 0, 1, noToken),
            "ground backdash transition was rejected");
    Require(ConsumerAccepted(MOTION_FORWARD_DASH, 0, 0,
                             165, 0, 0, noToken),
            "forward air dash transition was rejected");
    Require(ConsumerAccepted(MOTION_BACK_DASH, 0, 0,
                             166, 0, 1, noToken),
            "backward air dash transition was rejected");
    Require(ConsumerAccepted(MOTION_FORWARD_DASH, 0, 0,
                             KAORI_FORWARD_DASH_START_ID, 0, 0, noToken),
            "Kaori dash transition was rejected");
    Require(ConsumerAccepted(MOTION_FORWARD_DASH,
                             GROUND_BACKWARD_DASH_ID, 4,
                             KAORI_RECOIL_DUCK_ID, 0, 0, noToken),
            "Kaori frame-window 44~66 transition was rejected");
    Require(ConsumerAccepted(MOTION_FORWARD_DASH,
                             GROUND_BACKWARD_DASH_ID, 5,
                             KAORI_RECOIL_DUCK_ID, 0, 1, noToken),
            "Kaori frame-5 44~66 transition was rejected");
    Require(!ConsumerAccepted(MOTION_FORWARD_DASH, 0, 0,
                              KAORI_RECOIL_DUCK_ID, 0, 0, noToken),
            "neutral-to-251 falsely accepted as Kaori 44~66");
    Require(!ConsumerAccepted(MOTION_FORWARD_DASH,
                              GROUND_BACKWARD_DASH_ID, 3,
                              KAORI_RECOIL_DUCK_ID, 0, 0, noToken),
            "pre-window backdash-to-251 falsely accepted");
    Require(!ConsumerAccepted(MOTION_FORWARD_DASH,
                              GROUND_BACKWARD_DASH_ID, 6,
                              KAORI_RECOIL_DUCK_ID, 0, 0, noToken),
            "post-window backdash-to-251 falsely accepted");
    Require(!ConsumerAccepted(MOTION_FORWARD_DASH, 0, 0,
                              164, 0, 0, noToken),
            "forward transaction accepted a ground backdash");
    Require(!ConsumerAccepted(MOTION_BACK_DASH, 0, 0,
                              163, 0, 1, noToken),
            "back transaction accepted a ground forward dash");
    Require(!ConsumerAccepted(MOTION_FORWARD_DASH, 0, 0,
                              FORWARD_DASH_START_ID, 0,
                              noToken, noToken),
            "dash without the native double-tap token was accepted");
    Require(!ConsumerAccepted(MOTION_FORWARD_DASH, 0, 0, 200, 0,
                              0, noToken),
            "non-dash move was accepted as a dash");
}

}

int main() {
    TestSpecialFamilies();
    TestDashesAndMirrorEdges();
    TestButtonContractAndInvalidMotions();
    TestRingPlacementAndConsumerPolicy();
    std::cout << "motion_pattern_tests: all checks passed\n";
    return 0;
}
