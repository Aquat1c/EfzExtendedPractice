#pragma once

#include <cstdint>
#include <vector>

#include "input_core.h"
#include "motion_constants.h"

// EFZ's input-history head points at the NEXT write slot; the final pattern
// byte is delivered through EFZ's native poll.
namespace MotionPattern {

inline uint8_t ExpectedButtonForMotion(int motionType) {
    switch (motionType) {
        case MOTION_236A: case MOTION_623A: case MOTION_214A: case MOTION_421A:
        case MOTION_236236A: case MOTION_214214A: case MOTION_641236A:
        case MOTION_41236A: case MOTION_412A: case MOTION_22A:
        case MOTION_2141236A: case MOTION_463214A:
        case MOTION_4123641236A: case MOTION_6321463214A:
            return GAME_INPUT_A;
        case MOTION_236B: case MOTION_623B: case MOTION_214B: case MOTION_421B:
        case MOTION_236236B: case MOTION_214214B: case MOTION_641236B:
        case MOTION_41236B: case MOTION_412B: case MOTION_22B:
        case MOTION_2141236B: case MOTION_463214B:
        case MOTION_4123641236B: case MOTION_6321463214B:
            return GAME_INPUT_B;
        case MOTION_236C: case MOTION_623C: case MOTION_214C: case MOTION_421C:
        case MOTION_236236C: case MOTION_214214C: case MOTION_641236C:
        case MOTION_41236C: case MOTION_412C: case MOTION_22C:
        case MOTION_2141236C: case MOTION_463214C:
        case MOTION_4123641236C: case MOTION_6321463214C:
            return GAME_INPUT_C;
        case MOTION_236D: case MOTION_623D: case MOTION_214D: case MOTION_421D:
        case MOTION_236236D: case MOTION_214214D: case MOTION_641236D:
        case MOTION_41236D: case MOTION_412D: case MOTION_22D:
        case MOTION_2141236D: case MOTION_463214D:
        case MOTION_4123641236D: case MOTION_6321463214D:
            return GAME_INPUT_D;
        default:
            return 0;
    }
}

inline bool ButtonContractValid(int motionType, uint8_t buttonMask) {
    if (motionType == MOTION_FORWARD_DASH || motionType == MOTION_BACK_DASH) {
        return buttonMask == 0;
    }
    const uint8_t expected = ExpectedButtonForMotion(motionType);
    return expected != 0 && buttonMask == expected;
}

inline bool Build(int motionType, uint8_t buttonMask, bool facingRight,
                  std::vector<uint8_t>& out) {
    const uint8_t fwd = facingRight ? GAME_INPUT_RIGHT : GAME_INPUT_LEFT;
    const uint8_t back = facingRight ? GAME_INPUT_LEFT : GAME_INPUT_RIGHT;
    const uint8_t down = GAME_INPUT_DOWN;
    const uint8_t downFwd = static_cast<uint8_t>(down | fwd);
    const uint8_t downBack = static_cast<uint8_t>(down | back);

    out.clear();
    if (!ButtonContractValid(motionType, buttonMask)) {
        return false;
    }
    switch (motionType) {
        case MOTION_623A: case MOTION_623B: case MOTION_623C: case MOTION_623D:
            out = {0, 0, fwd, fwd, fwd, down, down, downFwd, downFwd,
                   static_cast<uint8_t>(downFwd | buttonMask)};
            break;
        case MOTION_214A: case MOTION_214B: case MOTION_214C: case MOTION_214D:
            out = {0, 0, down, down, down, downBack, downBack, back, back,
                   static_cast<uint8_t>(back | buttonMask)};
            break;
        case MOTION_236A: case MOTION_236B: case MOTION_236C: case MOTION_236D:
            out = {0, 0, down, down, down, downFwd, downFwd, fwd, fwd,
                   static_cast<uint8_t>(fwd | buttonMask)};
            break;
        case MOTION_421A: case MOTION_421B: case MOTION_421C: case MOTION_421D:
            out = {0, 0, back, back, down, down, downBack, downBack,
                   static_cast<uint8_t>(downBack | buttonMask)};
            break;
        case MOTION_41236A: case MOTION_41236B: case MOTION_41236C: case MOTION_41236D:
            out = {0, 0, back, back, downBack, downBack, down, down,
                   downFwd, downFwd, fwd,
                   static_cast<uint8_t>(fwd | buttonMask)};
            break;
        case MOTION_214214A: case MOTION_214214B: case MOTION_214214C: case MOTION_214214D:
            out = {0, 0, down, down, down, downBack, downBack, back, back,
                   down, down, down, downBack, downBack, back, back,
                   static_cast<uint8_t>(back | buttonMask)};
            break;
        case MOTION_236236A: case MOTION_236236B: case MOTION_236236C: case MOTION_236236D:
            out = {0, 0, down, down, down, downFwd, downFwd, fwd, fwd,
                   down, down, down, downFwd, downFwd, fwd, fwd,
                   static_cast<uint8_t>(fwd | buttonMask)};
            break;
        case MOTION_641236A: case MOTION_641236B: case MOTION_641236C: case MOTION_641236D:
            out = {0, 0, fwd, fwd, back, back, downBack, downBack, down,
                   down, downFwd, downFwd, fwd,
                   static_cast<uint8_t>(fwd | buttonMask)};
            break;
        case MOTION_412A: case MOTION_412B: case MOTION_412C: case MOTION_412D:
            out = {0, 0, back, back, downBack, downBack, down,
                   static_cast<uint8_t>(down | buttonMask)};
            break;
        case MOTION_22A: case MOTION_22B: case MOTION_22C: case MOTION_22D:
            out = {0, 0, down, down, 0, 0, down,
                   static_cast<uint8_t>(down | buttonMask)};
            break;
        case MOTION_2141236A: case MOTION_2141236B: case MOTION_2141236C: case MOTION_2141236D:
            out = {0, 0, down, down, downBack, downBack, back, back,
                   downBack, downBack, down, down, downFwd, downFwd, fwd,
                   static_cast<uint8_t>(fwd | buttonMask)};
            break;
        case MOTION_463214A: case MOTION_463214B: case MOTION_463214C: case MOTION_463214D:
            out = {0, 0, back, back, fwd, fwd, downFwd, downFwd, down,
                   down, downBack, downBack, back,
                   static_cast<uint8_t>(back | buttonMask)};
            break;
        case MOTION_4123641236A: case MOTION_4123641236B:
        case MOTION_4123641236C: case MOTION_4123641236D:
            out = {0, 0,
                   back, back, downBack, downBack, down, down, downFwd, downFwd, fwd, fwd,
                   back, back, downBack, downBack, down, down, downFwd, downFwd, fwd,
                   static_cast<uint8_t>(fwd | buttonMask)};
            break;
        case MOTION_6321463214A: case MOTION_6321463214B:
        case MOTION_6321463214C: case MOTION_6321463214D:
            out = {0, 0,
                   fwd, fwd, downFwd, downFwd, down, down, downBack, downBack, back, back,
                   fwd, fwd, downFwd, downFwd, down, down, downBack, downBack, back,
                   static_cast<uint8_t>(back | buttonMask)};
            break;
        case MOTION_FORWARD_DASH:
            out = {0, 0, fwd, fwd, 0, fwd};
            break;
        case MOTION_BACK_DASH:
            out = {0, 0, back, back, 0, back};
            break;
        default:
            return false;
    }
    return !out.empty();
}

inline void MirrorHorizontally(std::vector<uint8_t>& pattern) {
    for (uint8_t& value : pattern) {
        const bool leftOnly = (value & GAME_INPUT_LEFT) != 0 &&
                              (value & GAME_INPUT_RIGHT) == 0;
        const bool rightOnly = (value & GAME_INPUT_RIGHT) != 0 &&
                               (value & GAME_INPUT_LEFT) == 0;
        if (leftOnly || rightOnly) {
            value ^= static_cast<uint8_t>(GAME_INPUT_LEFT | GAME_INPUT_RIGHT);
        }
    }
}

}
