// Conditionally polls Sayuri's counter memory and armed flag; no blanket writes or code patches.
// Both fields are shared slots on other characters: the character gate is mandatory.
#pragma once

namespace SayuriCounter {

enum MemoryChoice {
    MEMORY_OFF          = 0,  // do not touch the memory at all
    MEMORY_NOTHING      = 1,  // hold it empty
    MEMORY_LAST_BLOCKED = 2,  // load whatever she most recently blocked
    kFirstMoveChoice    = 3
};

enum CutterMode {
    CUTTER_NORMAL       = 0,  // only the remembered move opens the window
    CUTTER_ALWAYS_READY = 1,  // every grounded block opens it
    CUTTER_COUNT        = 2
};

enum AutoCutterMode {
    AUTO_CUTTER_OFF   = 0,
    AUTO_CUTTER_ON    = 1,
    AUTO_CUTTER_COUNT = 2
};

// Picker list is owned by the menu/render thread; Tick() only uses the ID set through SetMemory().

void RefreshMoveChoices();

// playerNum is 1 or 2 and names the side SAYURI is on.
const char* const* MoveChoiceItems(int playerNum);
int  MoveChoiceCount(int playerNum);
int  MoveIdForChoice(int playerNum, int idx);
bool OpponentListAvailable(int playerNum);
bool MoveListTruncated(int playerNum);
int  ValidateChoiceIndex(int playerNum, int idx);

// The index is presentation only; the module stores the resolved move ID, so an index meaning
// "5A" against one opponent can't silently mean a different move against the next.
void SetMemory(int playerNum, int choiceIndex, int resolvedMoveId);
void SetCutter(int playerNum, int mode);
void SetAutoCutter(int playerNum, int mode);
void SetAutoCutterDelay(int playerNum, int visualFrames);
int  GetMemory(int playerNum);
int  GetCutter(int playerNum);
int  GetAutoCutter(int playerNum);
int  GetAutoCutterDelay(int playerNum);
constexpr int kAutoCutterDelayMax = 60;

// Per-tick poll from the frame monitor; reads Sayuri's move ID itself rather
// than a value sampled earlier in the monitor iteration.
void Tick();

void ResetState();

}
