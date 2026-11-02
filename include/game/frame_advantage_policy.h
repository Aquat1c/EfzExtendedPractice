#pragma once

namespace FrameAdvantagePolicy {

// A landing reads as actionable but the defender isn't free until it ends. Leaving one counts
// even into an attack or jump, which aren't "actionable" states, unless a new stun took over.
inline bool DefenderFreeEdge(bool prevActionable, bool prevLanding, bool freeNow,
                             bool landingNow, bool stunnedNow) {
    if (landingNow) return false;
    if (prevLanding) return !stunnedNow;
    return !prevActionable && freeNow;
}

}
