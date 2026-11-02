#pragma once

#include "../core/constants.h"

namespace KaoriRecoilDuckPolicy {

// updateKaoriMovementState checks the frame index at +0x0A before advancing it.
// A forward dash token is consumed only on frames 4 or 5, which enters move 251.
constexpr short kBackdashMove = GROUND_BACKWARD_DASH_ID;
constexpr short kDestinationMove = KAORI_RECOIL_DUCK_ID;
constexpr short kFirstWindowFrame = 4;
constexpr short kLastWindowFrame = 5;

inline bool IsBackdash(short move) {
    return move == kBackdashMove;
}

inline bool IsForwardInputWindow(short move, short frame) {
    return IsBackdash(move) &&
           frame >= kFirstWindowFrame && frame <= kLastWindowFrame;
}

inline bool IsExactDestination(short move) {
    return move == kDestinationMove;
}

}
