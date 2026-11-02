#pragma once

#include <cstdint>
#include <windows.h>
#include "../utils/config.h"
#include "game_state.h"

struct PerFrameSample {
    uint32_t        frame;            // Global internal frame counter (192Hz)
    unsigned long long tickMs;
    GamePhase       phase;
    GameMode        mode;
    bool            charsInitialized;
    short           moveID1;
    short           moveID2;
    short           prevMoveID1;
    short           prevMoveID2;
    bool            actionable1;
    bool            actionable2;
    bool            neutral1;
    bool            neutral2;
    uintptr_t       basePtr;
    uintptr_t       gameStatePtr;
    uintptr_t       p1Ptr;
    uintptr_t       p2Ptr;
    bool            online;
};

const PerFrameSample& GetCurrentPerFrameSample();
