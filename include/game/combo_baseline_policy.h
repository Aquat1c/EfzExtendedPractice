#pragma once

namespace ComboBaseline {

// After a reset the overlay ignores combo counters that were already running. A loaded state
// can hold them up indefinitely, so a new hit ends the wait as well as both sides going idle.
class Tracker {
public:
    void Arm() { captured_ = false; }

    bool SawNewHit(int p1Hits, int p2Hits, bool damageEdge) {
        if (!captured_) {
            captured_ = true;
            hits_[0] = p1Hits;
            hits_[1] = p2Hits;
            return damageEdge;
        }
        const bool newHit = damageEdge || p1Hits > hits_[0] || p2Hits > hits_[1];
        // A counter that fell restarted; its next rise is a new hit.
        if (p1Hits < hits_[0]) hits_[0] = p1Hits;
        if (p2Hits < hits_[1]) hits_[1] = p2Hits;
        return newHit;
    }

private:
    bool captured_ = false;
    int hits_[2] = {0, 0};
};

}
