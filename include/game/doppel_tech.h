// Polls Doppel's tech latch with at most one decision per stage entry; never a per-frame write.
// Outside the follow-up states the latch is generic scratch: character and move-ID gates are mandatory.
#pragma once

namespace DoppelTech {

enum Mode {
    MODE_OFF    = 0,  // do not touch the latch at all
    MODE_NEVER  = 1,  // latch A, which escapes nothing
    MODE_TECH_B = 2,  // latch B, which escapes the B-branch follow-ups
    MODE_TECH_C = 3,  // latch C, which escapes the A-branch follow-ups
    MODE_ALWAYS = 4,  // follow whichever branch Doppel actually committed to
    MODE_RANDOM = 5,  // uniform over B / C, rolled once per stage entry
    MODE_COUNT  = 6
};

enum Stage {
    STAGE_ALL = 0,
    STAGE_1   = 1,  // the capture hold
    STAGE_2   = 2,  // Maiden Crash
    STAGE_3   = 3,  // Barrage / Inner-Soul Fist
    STAGE_COUNT = 4
};

void SetMode(int playerNum, int mode);
void SetStage(int playerNum, int stage);
int  GetMode(int playerNum);
int  GetStage(int playerNum);

// Per-tick poll; reads move ID and frame index itself because the engine may have advanced
// since values sampled earlier in the monitor iteration.
void Tick();

void ResetState();

}
