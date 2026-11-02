#pragma once
#include <vector>
#include <cstdint>
#include "../core/constants.h"
#include "../input/auto_action_motion_transaction.h"

struct FinalMemoryCommand {
    int characterId;
    const char* name;
    std::vector<uint8_t> pattern;
    bool (*gate)(int playerNum);
    const char* gateDesc;
    int indexAdvance{0};          // Extra neutral frames to advance past the last pattern element
};

const std::vector<FinalMemoryCommand>& GetFinalMemoryCommands();

bool ExecuteFinalMemory(int playerNum, int characterId,
                        int consumerWaitPasses = 1,
                        uint64_t* generationOut = nullptr,
                        P2AutoActionMotionSubmitResult* submitResultOut = nullptr);

// Token grammar: numpad digit or N, then buttons A/B/C/S/D, then optional *COUNT repeats.
// With no count the DEFAULT_*_FRAMES repeats apply. Facing is mirrored after construction.
std::vector<uint8_t> BuildPattern(const std::vector<const char*>& tokens, bool facingRight);
