#pragma once

namespace MoveTiming {

// startup counts up to and including the first active tick.
struct Record {
    short moveId = 0;
    const char* label = nullptr;
    int startup = 0;
    int active = 0;
    int recovery = 0;
    bool valid = false;
    bool hasActive = false;
    bool interrupted = false;
};

struct Tick {
    bool inAttack = false;
    bool interrupted = false;    // stun ended the attack state
    short moveId = 0;
    const char* label = nullptr; // must outlive the tracker
    bool isCommand = false;      // id is entered by input, not a phase of the current move
    bool hitActive = false;      // boxes can connect this tick
    bool boxesPresent = false;   // frame data has attack boxes
    bool contact = false;        // collision resolver committed for this attacker this tick
};

// Contact closes the engine gate for the rest of the window, so the contact tick
// and later box frames stay active: hit, block and whiff measure alike.
inline bool ActiveThisTick(bool hitActive, bool contact, bool contactedEarlier, bool boxesPresent) {
    return hitActive || contact || (contactedEarlier && boxesPresent);
}

// Scripts run only while own +330 and the opponent's +332 are clear (efz.c:12231).
// Both decrement after collision, so the previous sample's values gate this tick.
inline bool SideFrozen(short ownHitstop, short opponentFlash) {
    return ownHitstop > 0 || opponentFlash > 0;
}

// Ids with no table row: attack-class ids are commands, anything else a phase.
inline bool StartsNewRecord(bool hasRow, bool rowIsInput, bool attackClass) {
    return hasRow ? rowIsInput : attackClass;
}

// A throw writes +0x168 = 6 on both sides (efz.c:200270); only the side not
// attacking is being thrown.
inline bool ReadsAsThrown(int contactLatch, bool thrownMove, bool attacking) {
    return thrownMove || (contactLatch == 6 && !attacking);
}

// Feed once per unfrozen tick, plus any frozen tick with contact. A change to a
// command id starts a new record; a change to any other id is a phase of the same move.
class Tracker {
public:
    void Reset() { *this = Tracker{}; }

    void Feed(const Tick& t) {
        if (!t.inAttack) {
            if (running_) Finish(t.interrupted);
            return;
        }
        if (running_ && t.moveId != currentMove_) {
            currentMove_ = t.moveId;
            currentLabel_ = t.label;
            if (t.isCommand) Finish(false);
        }
        if (!running_) Begin(t);

        const bool active = ActiveThisTick(t.hitActive, t.contact, contacted_, t.boxesPresent);
        if (t.contact) contacted_ = true;
        if (active) {
            if (!current_.hasActive) {
                current_.hasActive = true;
                current_.moveId = t.moveId;
                current_.label = t.label;
                current_.startup = elapsed_ + 1;
                current_.active = 1;
                current_.valid = true;
            } else {
                ++current_.active;
                current_.recovery = 0;
            }
            latest_ = current_;
        } else if (current_.hasActive) {
            ++current_.recovery;
            latest_ = current_;
        } else {
            ++elapsed_;
        }
    }

    const Record& Latest() const { return latest_; }

private:
    void Begin(const Tick& t) {
        running_ = true;
        current_ = Record{};
        current_.moveId = t.moveId;
        current_.label = t.label;
        currentMove_ = t.moveId;
        currentLabel_ = t.label;
        elapsed_ = 0;
        contacted_ = false;
    }

    void Finish(bool interrupted) {
        running_ = false;
        if (current_.hasActive) {
            current_.interrupted = interrupted;
            latest_ = current_;
        } else if (!interrupted && elapsed_ > 0) {
            // No character hit box, e.g. a projectile throw: report its length only.
            current_.recovery = elapsed_;
            current_.valid = true;
            latest_ = current_;
        }
    }

    Record latest_{};
    Record current_{};
    short currentMove_ = 0;
    const char* currentLabel_ = nullptr;
    int elapsed_ = 0;
    bool running_ = false;
    bool contacted_ = false;
};

} // namespace MoveTiming
