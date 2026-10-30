#include <fstream>
#include <iomanip>
#include <chrono>
#include "../include/core/memory.h"
#include "../include/core/constants.h"
#include "../include/utils/utilities.h"
#include "../include/utils/config.h"

#include "../include/core/logger.h"
#include <windows.h>
#include <atomic>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cstring>
// For global shutdown flag
#include "../include/core/globals.h"

static double saved_x1 = 240.0, saved_y1 = 0.0;
static double saved_x2 = 400.0, saved_y2 = 0.0;

static bool TryCopyMemory(void* destination, const void* source, size_t size) {
    __try {
        memcpy(destination, source, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// A protect pair on a live data page races other writers' restores and can leave it executable.
static bool RangeAlreadyWritable(uintptr_t address, size_t size) {
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info))
        || info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD)) {
        return false;
    }
    const DWORD protection = info.Protect & 0xFF;
    return (protection == PAGE_READWRITE || protection == PAGE_WRITECOPY
            || protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY)
        && size <= reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize - address;
}

bool SafeWriteMemory(uintptr_t address, const void* data, size_t size) {
    if (!address || !data || size == 0) return false;

    if (RangeAlreadyWritable(address, size)) {
        return TryCopyMemory(reinterpret_cast<void*>(address), data, size);
    }

    DWORD oldProtect;
    if (!VirtualProtect((LPVOID)address, size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
    }

    const bool success = TryCopyMemory(reinterpret_cast<void*>(address), data, size);

    VirtualProtect((LPVOID)address, size, oldProtect, &oldProtect);
    return success;
}

bool SafeReadMemory(uintptr_t address, void* buffer, size_t size) {
    if (!address || !buffer || size == 0) {
        return false;
    }

    return TryCopyMemory(buffer, reinterpret_cast<const void*>(address), size);
}

uintptr_t ResolvePointer(uintptr_t base, uintptr_t baseOffset, uintptr_t offset) {
    // Prefer cached player bases for EFZ player offsets; avoids repeated reads in gameplay loops.
    if (baseOffset == EFZ_BASE_OFFSET_P1 || baseOffset == EFZ_BASE_OFFSET_P2) {
        const int playerIndex = (baseOffset == EFZ_BASE_OFFSET_P1) ? 1 : 2;
        uintptr_t playerBase = GetPlayerBase(playerIndex);
        if (playerBase) {
            uintptr_t finalAddr = playerBase + offset;
            if (finalAddr >= 0x1000 && finalAddr <= 0xFFFFFFFF) {
                return finalAddr;
            }
        }
        // Fall through to legacy resolution on cache miss.
    }

    if (base == 0) return 0;

    uintptr_t ptrAddr = base + baseOffset;
    if (ptrAddr < 0x1000 || ptrAddr > 0xFFFFFFFF) return 0;

    uintptr_t ptrValue = 0;
    if (!SafeReadMemory(ptrAddr, &ptrValue, sizeof(uintptr_t))) return 0;

    if (ptrValue == 0 || ptrValue > 0xFFFFFFFF) return 0;

    uintptr_t finalAddr = ptrValue + offset;
    if (finalAddr < 0x1000 || finalAddr > 0xFFFFFFFF) return 0;

    return finalAddr;
}

void WriteGameMemory(uintptr_t address, const void* data, size_t size) {
    if (address == 0 || !data) {
        LogOut("[MEMORY] Invalid address or data pointer", true); // Keep errors visible
        return;
    }

    if (address < 0x10000 || address > 0x7FFFFFFF) {
        LogOut("[MEMORY] Address out of valid range: " + std::to_string(address), true); // Keep errors visible
        return;
    }

    DWORD oldProtect;
    if (VirtualProtect((LPVOID)address, size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        memcpy((void*)address, data, size);
        VirtualProtect((LPVOID)address, size, oldProtect, &oldProtect);
        LogOut("[MEMORY] WriteGameMemory: Successfully wrote " + std::to_string(size) + 
               " bytes to 0x" + std::to_string(address), detailedLogging.load());
    } else {
        LogOut("[MEMORY] Failed to change memory protection at address: 0x" + 
               std::to_string(address), true); // Keep errors visible
    }
}

bool PatchMemory(uintptr_t address, const char* bytes, size_t length) {
    DWORD oldProtect;

    if (!VirtualProtect((LPVOID)address, length, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    memcpy((void*)address, bytes, length);

    VirtualProtect((LPVOID)address, length, oldProtect, &oldProtect);

    return true;
}

bool NopMemory(uintptr_t address, size_t length) {
    DWORD oldProtect;

    if (!VirtualProtect((LPVOID)address, length, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    memset((void*)address, 0x90, length);

    VirtualProtect((LPVOID)address, length, oldProtect, &oldProtect);

    return true;
}

// Mission/tutorial setup uses the checked entry point so a failed teleport
// cannot quietly become the baseline for the rest of a lesson.
bool TrySetPlayerPosition(uintptr_t base, uintptr_t playerOffset, double x, double y, bool updateMoveID) {
    if (!std::isfinite(x) || !std::isfinite(y)) {
        LogOut("[MEMORY] Refused a non-finite fighter position", true);
        return false;
    }
    uintptr_t xAddr = ResolvePointer(base, playerOffset, XPOS_OFFSET);
    uintptr_t yAddr = ResolvePointer(base, playerOffset, YPOS_OFFSET);
    uintptr_t moveIDAddr = ResolvePointer(base, playerOffset, MOVE_ID_OFFSET);
    
    if (!xAddr || !yAddr) {
        LogOut("[MEMORY] Failed to resolve position pointers", true);
        return false;
    }
    bool ok = true;

    auto writeAndVerify = [&](uintptr_t address, const void* value, size_t size,
                              const char* field) {
        if (!address || !SafeWriteMemory(address, value, size)) {
            LogOut(std::string("[MEMORY] Failed to write ") + field, true);
            ok = false;
            return;
        }
        unsigned char observed[sizeof(double)] = {};
        if (size > sizeof(observed) ||
            !SafeReadMemory(address, observed, size) ||
            std::memcmp(observed, value, size) != 0) {
            LogOut(std::string("[MEMORY] Failed to verify ") + field, true);
            ok = false;
        }
    };
    
    // EFZ's world Y is zero on the floor and negative above it.
    const bool willBeGrounded = y >= -0.5;

    // Reset animation frame counters for BOTH players; a stuck cloud on one side can freeze both counters.
    uintptr_t p1FrameAddr = ResolvePointer(base, EFZ_BASE_OFFSET_P1, CURRENT_FRAME_INDEX_OFFSET);
    uintptr_t p2FrameAddr = ResolvePointer(base, EFZ_BASE_OFFSET_P2, CURRENT_FRAME_INDEX_OFFSET);
    short zeroFrame = 0;
    if (p1FrameAddr) SafeWriteMemory(p1FrameAddr, &zeroFrame, sizeof(short));
    if (p2FrameAddr) SafeWriteMemory(p2FrameAddr, &zeroFrame, sizeof(short));

    writeAndVerify(xAddr, &x, sizeof(x), "X position");

    writeAndVerify(yAddr, &y, sizeof(y), "Y position");

    uintptr_t xVelAddr = ResolvePointer(base, playerOffset, XVEL_OFFSET);
    if (xVelAddr) {
        double zeroVel = 0.0;
        writeAndVerify(xVelAddr, &zeroVel, sizeof(zeroVel), "X velocity");
    } else {
        LogOut("[MEMORY] Failed to resolve X velocity pointer", true);
        ok = false;
    }
    
    uintptr_t yVelAddr = ResolvePointer(base, playerOffset, YVEL_OFFSET);
    if (yVelAddr) {
        double zeroVel = 0.0;
        writeAndVerify(yVelAddr, &zeroVel, sizeof(zeroVel), "Y velocity");
    } else {
        LogOut("[MEMORY] Failed to resolve Y velocity pointer", true);
        ok = false;
    }
    
    uintptr_t frameCounterAddr = ResolvePointer(base, playerOffset, CURRENT_FRAME_INDEX_OFFSET);
    if (frameCounterAddr) {
        short zeroFrame = 0;
        writeAndVerify(frameCounterAddr, &zeroFrame, sizeof(zeroFrame), "animation frame");
    } else {
        LogOut("[MEMORY] Failed to resolve animation frame pointer", true);
        ok = false;
    }
    
    if (updateMoveID && moveIDAddr) {
        short newMoveID;

        if (!willBeGrounded) {
            newMoveID = FALLING_ID; // air position: falling state
        } else {
            newMoveID = IDLE_MOVE_ID; // ground: idle
        }
        
        writeAndVerify(moveIDAddr, &newMoveID, sizeof(newMoveID), "move ID");
    } else if (updateMoveID) {
        LogOut("[MEMORY] Failed to resolve move ID pointer", true);
        ok = false;
    }
    
    LogOut("[MEMORY] Set position - X: " + std::to_string(x) + ", Y: " + std::to_string(y), detailedLogging.load());
    return ok;
}

void SetPlayerPosition(uintptr_t base, uintptr_t playerOffset, double x, double y,
                       bool updateMoveID) {
    (void)TrySetPlayerPosition(base, playerOffset, x, y, updateMoveID);
}


// CRITICAL: Never modify game state during online mode
bool SetRFValuesDirect(double p1RF, double p2RF) {
    if (g_onlineModeActive.load()) return false;

    uintptr_t base = GetEFZBase();
    if (!base) return false;

    uintptr_t* p1Ptr = (uintptr_t*)(base + EFZ_BASE_OFFSET_P1);
    uintptr_t* p2Ptr = (uintptr_t*)(base + EFZ_BASE_OFFSET_P2);

    if (IsBadReadPtr(p1Ptr, sizeof(uintptr_t)) || IsBadReadPtr(p2Ptr, sizeof(uintptr_t))) {
        LogOut("[RF] Invalid player base pointers", detailedLogging);
        return false;
    }

    uintptr_t p1Base = *p1Ptr;
    uintptr_t p2Base = *p2Ptr;

    if (!p1Base || !p2Base) {
        LogOut("[RF] Player structures not initialized", detailedLogging);
        return false;
    }

    double* p1RFAddr = (double*)(p1Base + RF_OFFSET);
    double* p2RFAddr = (double*)(p2Base + RF_OFFSET);

    if (IsBadWritePtr(p1RFAddr, sizeof(double)) || IsBadWritePtr(p2RFAddr, sizeof(double))) {
        LogOut("[RF] Invalid RF addresses", detailedLogging);
        return false;
    }

    DWORD oldProtect1, oldProtect2;
    bool success = true;

    if (VirtualProtect(p1RFAddr, sizeof(double), PAGE_EXECUTE_READWRITE, &oldProtect1)) {
        *p1RFAddr = p1RF; // Direct write - no memcpy
        VirtualProtect(p1RFAddr, sizeof(double), oldProtect1, &oldProtect1);

        double verification = 0.0;
        memcpy(&verification, p1RFAddr, sizeof(double));
        if (verification != p1RF) {
            LogOut("[RF] P1 RF verification failed: wrote " + std::to_string(p1RF) + 
                   " but read back " + std::to_string(verification), true);
            success = false;
        }
    } else {
        LogOut("[RF] P1 VirtualProtect failed", true);
        success = false;
    }
    
    // P2 RF write
    if (VirtualProtect(p2RFAddr, sizeof(double), PAGE_EXECUTE_READWRITE, &oldProtect2)) {
        *p2RFAddr = p2RF; // Direct write - no memcpy
        VirtualProtect(p2RFAddr, sizeof(double), oldProtect2, &oldProtect2);

        double verification = 0.0;
        memcpy(&verification, p2RFAddr, sizeof(double));
        if (verification != p2RF) {
            LogOut("[RF] P2 RF verification failed: wrote " + std::to_string(p2RF) + 
                   " but read back " + std::to_string(verification), true);
            success = false;
        }
    } else {
        LogOut("[RF] P2 VirtualProtect failed", true);
        success = false;
    }
    
    return success;
}

// Read engine regen params (A/B); the engine keeps both players in sync, fallback to P2 if P1 fails.
bool ReadEngineRegenParams(uint16_t& outParamA, uint16_t& outParamB) {
    outParamA = 0; outParamB = 0;
    uintptr_t base = GetEFZBase(); if (!base) return false;
    uintptr_t p1PtrAddr = base + EFZ_BASE_OFFSET_P1;
    uintptr_t p2PtrAddr = base + EFZ_BASE_OFFSET_P2;
    uintptr_t p1Base = 0, p2Base = 0;
    if (!SafeReadMemory(p1PtrAddr, &p1Base, sizeof(p1Base)) || !p1Base) {
        SafeReadMemory(p2PtrAddr, &p2Base, sizeof(p2Base));
    }
    uintptr_t useBase = p1Base ? p1Base : p2Base;
    if (!useBase) return false;
    uint16_t a=0,b=0;
    if (!SafeReadMemory(useBase + PLAYER_PARAM_A_COPY_OFFSET, &a, sizeof(a))) return false;
    if (!SafeReadMemory(useBase + PLAYER_PARAM_B_COPY_OFFSET, &b, sizeof(b))) return false;
    outParamA = a; outParamB = b; return true;
}

// Write regen params to both players; true if at least one player's params were written.
bool WriteEngineRegenParams(uint16_t paramA, uint16_t paramB) {
    uintptr_t base = GetEFZBase(); if (!base) return false;
    uintptr_t p1PtrAddr = base + EFZ_BASE_OFFSET_P1;
    uintptr_t p2PtrAddr = base + EFZ_BASE_OFFSET_P2;
    uintptr_t p1Base = 0, p2Base = 0; bool any = false;
    SafeReadMemory(p1PtrAddr, &p1Base, sizeof(p1Base));
    SafeReadMemory(p2PtrAddr, &p2Base, sizeof(p2Base));
    if (p1Base) {
        any |= SafeWriteMemory(p1Base + PLAYER_PARAM_A_COPY_OFFSET, &paramA, sizeof(paramA));
        any |= SafeWriteMemory(p1Base + PLAYER_PARAM_B_COPY_OFFSET, &paramB, sizeof(paramB));
    }
    if (p2Base) {
        any |= SafeWriteMemory(p2Base + PLAYER_PARAM_A_COPY_OFFSET, &paramA, sizeof(paramA));
        any |= SafeWriteMemory(p2Base + PLAYER_PARAM_B_COPY_OFFSET, &paramB, sizeof(paramB));
    }
    if (any) {
        LogOut("[ENGINE][Regen] Wrote Param A/B: A=" + std::to_string((unsigned)paramA) + ", B=" + std::to_string((unsigned)paramB), detailedLogging.load());
    }
    return any;
}

// Force F5 preset: A=1000 or 2000, B=3332. Returns true if writes succeed.
bool ForceEngineF5Preset(uint16_t presetA) {
    if (presetA != 1000 && presetA != 2000) presetA = 1000;
    uint16_t b = 3332;
    bool ok = WriteEngineRegenParams(presetA, b);
    // GetEngineRegenStatus will see A/B next frame and mark F5.
    return ok;
}

// F5 "Full values": HP=9999 on both players, A=1000 (so F5 is detected) and B=9999
// (observed after the engine's A==2000 branch); no +5 cadence so it is not treated as F4.
bool ForceEngineF5Full() {
    uintptr_t base = GetEFZBase(); if (!base) return false;
    uintptr_t p1Base = 0, p2Base = 0; bool any=false;
    SafeReadMemory(base + EFZ_BASE_OFFSET_P1, &p1Base, sizeof(p1Base));
    SafeReadMemory(base + EFZ_BASE_OFFSET_P2, &p2Base, sizeof(p2Base));
    int hpFull = 9999;
    if (p1Base) {
        any |= SafeWriteMemory(p1Base + HP_OFFSET, &hpFull, sizeof(hpFull));
        any |= SafeWriteMemory(p1Base + HP_BAR_OFFSET, &hpFull, sizeof(hpFull));
    }
    if (p2Base) {
        any |= SafeWriteMemory(p2Base + HP_OFFSET, &hpFull, sizeof(hpFull));
        any |= SafeWriteMemory(p2Base + HP_BAR_OFFSET, &hpFull, sizeof(hpFull));
    }
    WriteEngineRegenParams(1000, 9999);
    LogOut("[ENGINE][Regen] Forced F5 Full: HP=9999, Params A=1000 B=9999", detailedLogging.load());
    return any;
}

// Force F4 value: clamp to [0..2000], round to nearest multiple of 5, set B=9999.
// Single write should produce a +5 cadence delta on next poll relative to previous A, engaging F4 heuristic.
bool ForceEngineF4Value(uint16_t targetA) {
    if (targetA > 2000) targetA = 2000;
    // round to nearest multiple of 5 for authenticity
    targetA = (uint16_t)((targetA + 2) / 5 * 5);
    if (targetA > 2000) targetA = 2000;
    uint16_t b = 9999;
    bool ok = WriteEngineRegenParams(targetA, b);
    return ok;
}

EngineRegenMode InferEngineRegenMode(uint16_t paramA, uint16_t paramB) {
    // F5 cycle sets A to 1000 or 2000 AND (at some point) B to 3332; it leaves B=3332 afterwards.
    bool f5Pattern = (paramA == 1000 || paramA == 2000 || paramB == 3332);
    // F4 fine-tune forces B=9999 while A steps 0..2000 by +5; A will often be multiples of 5.
    bool fineTunePattern = (paramB == 9999 && (paramA % 5 == 0) && paramA != 1000 && paramA != 2000);
    if (fineTunePattern) return EngineRegenMode::F4_FineTuneActive;
    if (f5Pattern) return EngineRegenMode::F5_FullOrPreset;
    return EngineRegenMode::Normal;
}

bool IsEngineRegenLikelyActive() {
    uint16_t a=0,b=0; if (!ReadEngineRegenParams(a,b)) return false;
    EngineRegenMode m = InferEngineRegenMode(a,b);
    return m == EngineRegenMode::F4_FineTuneActive || m == EngineRegenMode::F5_FullOrPreset;
}

// Stateful inference to avoid false positives:
// - Treat F5 as active when A==1000/2000 or B==3332; start a cooldown (s_f4Cooldown) suppressing F4 detection for N frames
// - Treat F4 only when B==9999 AND A is stepping by +5 with wrap for several consecutive frames (s_stepRun>=3)
// - Do NOT treat static B==9999 as F4 (HP max is common)
static uint16_t s_prevA = 0;
static uint16_t s_prevB = 0;
static int s_stepRun = 0;
static int s_f4Cooldown = 0;
static bool s_prevValid = false;

bool GetEngineRegenStatus(EngineRegenMode& outMode, uint16_t& outParamA, uint16_t& outParamB) {
    outMode = EngineRegenMode::Unknown; outParamA = outParamB = 0;
    uint16_t a=0,b=0; if (!ReadEngineRegenParams(a,b)) return false;
    outParamA = a; outParamB = b;

    bool f5 = (a == 1000 || a == 2000 || b == 3332);
    if (f5) {
        outMode = EngineRegenMode::F5_FullOrPreset;
        s_f4Cooldown = 30; // ~0.5s at 60fps
        s_stepRun = 0; s_prevValid = true; s_prevA = a; s_prevB = b; return true;
    }

    // Cooldown suppresses F4 after F5 activity
    if (s_f4Cooldown > 0) {
        s_f4Cooldown--; outMode = EngineRegenMode::Normal; s_prevValid = true; s_prevA = a; s_prevB = b; return true;
    }

    // F4 cadence: B==9999 AND A stepping forward in multiples of 5 (account for sampling gaps and wrap 2000->0)
    if (b == 9999 && s_prevValid) {
        int delta = (int)a - (int)s_prevA;
        bool forwardMulti5 = (delta > 0 && (delta % 5) == 0);
        bool wrappedForward = (s_prevA > a) && (((2000 - s_prevA) + a) % 5 == 0);
        if (forwardMulti5 || wrappedForward || (s_prevA == 2000 && a == 0)) {
            if (s_stepRun < 10) s_stepRun++;
        } else if (a != s_prevA) {
            s_stepRun = 0;
        }
    } else {
        s_stepRun = 0;
    }

    if (b == 9999 && s_stepRun >= 1) {
        outMode = EngineRegenMode::F4_FineTuneActive;
    } else {
        outMode = EngineRegenMode::Normal;
    }

    s_prevValid = true; s_prevA = a; s_prevB = b; return true;
}

// Derive RF gauge value and IC color from Param A:
// Param A 0..999.99  -> red gauge, RF = A (clamped 0..1000)
// Param A == 1000    -> blue gauge full (RF = 1000)
// Param A 1001..2000 -> blue gauge with RF = 2000 - A (so A=1980 -> RF=20)
// Returns true if mapping applied; outputs rfValue (float) and isBlueIC.
bool DeriveRfFromParamA(uint16_t paramA, float& rfValue, bool& isBlueIC) {
    // Accept 16-bit integer paramA; fractional .99 context ignored (engine stores word).
    if (paramA <= 1000) {
        if (paramA < 1000) {
            isBlueIC = false; rfValue = (float)paramA; return true;
        } else { // exactly 1000
            isBlueIC = true; rfValue = 1000.0f; return true;
        }
    }
    if (paramA <= 2000) {
        isBlueIC = true; rfValue = (float)(2000 - paramA); if (rfValue < 0.0f) rfValue = 0.0f; return true;
    }
    // Outside expected range; treat as unknown
    isBlueIC = false; rfValue = 0.0f; return false;
}

// Brute-force scan a small window around the expected offsets to find a plausible Param A/B pair.
// This helps when observed values appear incorrect (e.g., 0xFFFF / static) to confirm actual locations.
// Strategy:
//   Scan words in [0x30C0 .. 0x3120]; look for patterns:
//     - B candidates: 9999 (F4), 3332 (F5 cycle) -> preceding 2 bytes likely A.
//     - A candidates: 1000 or 2000 with following 2 bytes in {3332, 9999, <=2100 stepping}.
// Returns first strong candidate; offsets are relative to playerBase.
bool DebugScanRegenParamWindow(uintptr_t playerBase, uint32_t& outAOffset, uint16_t& outAVal, uint32_t& outBOffset, uint16_t& outBVal) {
    outAOffset = outBOffset = 0; outAVal = outBVal = 0;
    if (!playerBase) return false;
    const uint32_t START = 0x30C0; const uint32_t END = 0x3120; // conservative window
    uint16_t prevVal = 0;
    for (uint32_t off = START; off <= END; off += 2) {
        uint16_t val = 0; SafeReadMemory(playerBase + off, &val, sizeof(val));
        // B candidate first (strong identifiers)
        if (val == 9999 || val == 3332) {
            if (off >= 2) {
                uint16_t aVal = 0; SafeReadMemory(playerBase + off - 2, &aVal, sizeof(aVal));
                if ((aVal <= 2100) || aVal == 1000 || aVal == 2000) {
                    outAOffset = off - 2; outAVal = aVal; outBOffset = off; outBVal = val; return true;
                }
            }
        }
        // A candidate pattern
        if (val == 1000 || val == 2000) {
            uint16_t bVal = 0; SafeReadMemory(playerBase + off + 2, &bVal, sizeof(bVal));
            if (bVal == 3332 || bVal == 9999 || bVal <= 2100) {
                outAOffset = off; outAVal = val; outBOffset = off + 2; outBVal = bVal; return true;
            }
        }
        // F4 stepping heuristic: multiples of 5 rising towards 2000 while neighbor is 9999
        if ((val % 5) == 0 && val <= 2000) {
            uint16_t bVal = 0; SafeReadMemory(playerBase + off + 2, &bVal, sizeof(bVal));
            if (bVal == 9999) { outAOffset = off; outAVal = val; outBOffset = off + 2; outBVal = bVal; return true; }
        }
        prevVal = val;
    }
    return false;
}

// Set IC color values directly (similar to SetRFValuesDirect)
bool SetICColorDirect(bool p1BlueIC, bool p2BlueIC) {
    // CRITICAL: Never modify game state during online mode
    if (g_onlineModeActive.load()) return false;

    uintptr_t base = GetEFZBase();
    if (!base) return false;

    uintptr_t* p1Ptr = (uintptr_t*)(base + EFZ_BASE_OFFSET_P1);
    uintptr_t* p2Ptr = (uintptr_t*)(base + EFZ_BASE_OFFSET_P2);

    if (IsBadReadPtr(p1Ptr, sizeof(uintptr_t)) || IsBadReadPtr(p2Ptr, sizeof(uintptr_t))) {
        LogOut("[IC] Invalid player base pointers", detailedLogging.load());
        return false;
    }

    uintptr_t p1Base = *p1Ptr;
    uintptr_t p2Base = *p2Ptr;

    if (!p1Base || !p2Base) {
        LogOut("[IC] Player structures not initialized", detailedLogging.load());
        return false;
    }

    // IC color addresses (offset 0x120 = 4-byte int)
    int* p1ICAddr = (int*)(p1Base + IC_COLOR_OFFSET);
    int* p2ICAddr = (int*)(p2Base + IC_COLOR_OFFSET);

    if (IsBadWritePtr(p1ICAddr, sizeof(int)) || IsBadWritePtr(p2ICAddr, sizeof(int))) {
        LogOut("[IC] Invalid IC color addresses", detailedLogging.load());
        return false;
    }

    // 0=red IC, 1=blue IC
    int p1ICValue = p1BlueIC ? 1 : 0;
    int p2ICValue = p2BlueIC ? 1 : 0;

    std::ostringstream logMsg;
    logMsg << "[IC][WRITE] SetICColorDirect called: P1=" << (p1BlueIC ? "Blue" : "Red")
           << " P2=" << (p2BlueIC ? "Blue" : "Red")
           << " (values: P1=" << p1ICValue << ", P2=" << p2ICValue << ")";
    LogOut(logMsg.str(), true);

    // Write values with memory protection handling
    DWORD oldProtect1, oldProtect2;
    bool success = true;    // P1 IC color write
    if (VirtualProtect(p1ICAddr, sizeof(int), PAGE_EXECUTE_READWRITE, &oldProtect1)) {
        *p1ICAddr = p1ICValue;
        VirtualProtect(p1ICAddr, sizeof(int), oldProtect1, &oldProtect1);

        int verification = 0;
        memcpy(&verification, p1ICAddr, sizeof(int));
        if (verification != p1ICValue) {
            LogOut("[IC] P1 IC verification failed: wrote " + std::to_string(p1ICValue) + 
                   " but read back " + std::to_string(verification), true);
            success = false;
        }
    } else {
        LogOut("[IC] P1 VirtualProtect failed", true);
        success = false;
    }
    
    // P2 IC color write
    if (VirtualProtect(p2ICAddr, sizeof(int), PAGE_EXECUTE_READWRITE, &oldProtect2)) {
        *p2ICAddr = p2ICValue;
        VirtualProtect(p2ICAddr, sizeof(int), oldProtect2, &oldProtect2);

        int verification = 0;
        memcpy(&verification, p2ICAddr, sizeof(int));
        if (verification != p2ICValue) {
            LogOut("[IC] P2 IC verification failed: wrote " + std::to_string(p2ICValue) + 
                   " but read back " + std::to_string(verification), true);
            success = false;
        }
    } else {
        LogOut("[IC] P2 VirtualProtect failed", true);
        success = false;
    }
    
    return success;
}

// Write IC color for only one player while preserving the other's current color
bool SetICColorPlayer(int player, bool blueIC) {
    uintptr_t base = GetEFZBase();
    if (!base) return false;

    // Read current IC colors so we don't clobber the other side
    int p1IC = 0, p2IC = 0;
    uintptr_t p1ICAddr = ResolvePointer(base, EFZ_BASE_OFFSET_P1, IC_COLOR_OFFSET);
    uintptr_t p2ICAddr = ResolvePointer(base, EFZ_BASE_OFFSET_P2, IC_COLOR_OFFSET);
    if (p1ICAddr) SafeReadMemory(p1ICAddr, &p1IC, sizeof(p1IC));
    if (p2ICAddr) SafeReadMemory(p2ICAddr, &p2IC, sizeof(p2IC));

    bool p1Blue = (p1IC != 0);
    bool p2Blue = (p2IC != 0);

    std::ostringstream logMsg;
    logMsg << "[IC][WRITE] SetICColorPlayer called: Player=" << player
           << " NewColor=" << (blueIC ? "Blue" : "Red")
           << " CurrentState(P1=" << (p1Blue ? "Blue" : "Red")
           << ", P2=" << (p2Blue ? "Blue" : "Red") << ")";
    LogOut(logMsg.str(), true);
    
    if (player == 1) {
        p1Blue = blueIC;
    } else if (player == 2) {
        p2Blue = blueIC;
    } else {
        return false;
    }
    return SetICColorDirect(p1Blue, p2Blue);
}

void UpdatePlayerValues(uintptr_t base, uintptr_t baseOffsetP1, uintptr_t baseOffsetP2) {
    // Applies displayData values to the game (used by the GUI dialogs)
    uintptr_t hpAddr1 = ResolvePointer(base, baseOffsetP1, HP_OFFSET);
    uintptr_t meterAddr1 = ResolvePointer(base, baseOffsetP1, METER_OFFSET);
    uintptr_t rfAddr1 = ResolvePointer(base, baseOffsetP1, RF_OFFSET);
    uintptr_t xAddr1 = ResolvePointer(base, baseOffsetP1, XPOS_OFFSET);
    uintptr_t yAddr1 = ResolvePointer(base, baseOffsetP1, YPOS_OFFSET);

    uintptr_t hpAddr2 = ResolvePointer(base, baseOffsetP2, HP_OFFSET);
    uintptr_t meterAddr2 = ResolvePointer(base, baseOffsetP2, METER_OFFSET);
    uintptr_t rfAddr2 = ResolvePointer(base, baseOffsetP2, RF_OFFSET);
    uintptr_t xAddr2 = ResolvePointer(base, baseOffsetP2, XPOS_OFFSET);
    uintptr_t yAddr2 = ResolvePointer(base, baseOffsetP2, YPOS_OFFSET);

    if (hpAddr1) {
        WriteGameMemory(hpAddr1, &displayData.hp1, sizeof(int));
        uintptr_t hpBar1 = ResolvePointer(base, baseOffsetP1, HP_BAR_OFFSET);
        if (hpBar1) WriteGameMemory(hpBar1, &displayData.hp1, sizeof(int));
    }
    if (meterAddr1) WriteGameMemory(meterAddr1, &displayData.meter1, sizeof(WORD));

    // CRITICAL: RF is stored as float (4 bytes), not double
    if (rfAddr1) {
        float rf = static_cast<float>(displayData.rf1);
        WriteGameMemory(rfAddr1, &rf, sizeof(float));
    }
    
    if (xAddr1) WriteGameMemory(xAddr1, &displayData.x1, sizeof(double));
    if (yAddr1) WriteGameMemory(yAddr1, &displayData.y1, sizeof(double));

    if (hpAddr2) {
        WriteGameMemory(hpAddr2, &displayData.hp2, sizeof(int));
        uintptr_t hpBar2 = ResolvePointer(base, baseOffsetP2, HP_BAR_OFFSET);
        if (hpBar2) WriteGameMemory(hpBar2, &displayData.hp2, sizeof(int));
    }
    if (meterAddr2) WriteGameMemory(meterAddr2, &displayData.meter2, sizeof(WORD));

    // CRITICAL: RF is stored as float (4 bytes), not double
    if (rfAddr2) {
        float rf = static_cast<float>(displayData.rf2);
        WriteGameMemory(rfAddr2, &rf, sizeof(float));
    }
    
    if (xAddr2) WriteGameMemory(xAddr2, &displayData.x2, sizeof(double));
    if (yAddr2) WriteGameMemory(yAddr2, &displayData.y2, sizeof(double));

    LogOut("[MEMORY] Applied values from dialog: P1[HP:" + std::to_string(displayData.hp1) +
        " Meter:" + std::to_string(displayData.meter1) +
        " RF:" + std::to_string(displayData.rf1) +
        " X:" + std::to_string(displayData.x1) +
        " Y:" + std::to_string(displayData.y1) +
        "] P2[HP:" + std::to_string(displayData.hp2) +
        " Meter:" + std::to_string(displayData.meter2) +
        " RF:" + std::to_string(displayData.rf2) +
        " X:" + std::to_string(displayData.x2) +
        " Y:" + std::to_string(displayData.y2) + "]", true);
}

void SavePlayerPositions(uintptr_t base) {
    if (!base) return;

    uintptr_t xAddr1 = ResolvePointer(base, EFZ_BASE_OFFSET_P1, XPOS_OFFSET);
    uintptr_t yAddr1 = ResolvePointer(base, EFZ_BASE_OFFSET_P1, YPOS_OFFSET);
    uintptr_t xAddr2 = ResolvePointer(base, EFZ_BASE_OFFSET_P2, XPOS_OFFSET);
    uintptr_t yAddr2 = ResolvePointer(base, EFZ_BASE_OFFSET_P2, YPOS_OFFSET);

    if (xAddr1) SafeReadMemory(xAddr1, &saved_x1, sizeof(double));
    if (yAddr1) SafeReadMemory(yAddr1, &saved_y1, sizeof(double));
    if (xAddr2) SafeReadMemory(xAddr2, &saved_x2, sizeof(double));
    if (yAddr2) SafeReadMemory(yAddr2, &saved_y2, sizeof(double));

    LogOut("[MEMORY] Saved positions: P1(" + std::to_string(saved_x1) + "), P2(" + std::to_string(saved_x2) + ")", true);
}

void LoadPlayerPositions(uintptr_t base) {
    if (!base) return;

    LogOut("[POSITION] Teleporting to recorded positions - P1(" + 
           std::to_string(saved_x1) + "," + std::to_string(saved_y1) + 
           ") P2(" + std::to_string(saved_x2) + "," + std::to_string(saved_y2) + ")", true);

    // Set player positions. If Y is non-zero, we don't want to force an idle state.
    SetPlayerPosition(base, EFZ_BASE_OFFSET_P1, saved_x1, saved_y1, (saved_y1 == 0.0));
    SetPlayerPosition(base, EFZ_BASE_OFFSET_P2, saved_x2, saved_y2, (saved_y2 == 0.0));

    uintptr_t yAddr1 = ResolvePointer(base, EFZ_BASE_OFFSET_P1, YPOS_OFFSET);
    uintptr_t yAddr2 = ResolvePointer(base, EFZ_BASE_OFFSET_P2, YPOS_OFFSET);

    double verifyY1 = 0.0, verifyY2 = 0.0;
    if (yAddr1) SafeReadMemory(yAddr1, &verifyY1, sizeof(double));
    if (yAddr2) SafeReadMemory(yAddr2, &verifyY2, sizeof(double));

    LogOut("[POSITION] Verified Y positions after teleport - P1:" + 
          std::to_string(verifyY1) + ", P2:" + std::to_string(verifyY2), true);

    // Airborne Y is negative. If it was not applied, try setting it with air state.
    if ((saved_y1 < -0.1 && verifyY1 > -0.1) || (saved_y2 < -0.1 && verifyY2 > -0.1)) {
        LogOut("[POSITION] Y positions were reset by game, forcing air state", true);
        
        uintptr_t moveIDAddr1 = ResolvePointer(base, EFZ_BASE_OFFSET_P1, MOVE_ID_OFFSET);
        uintptr_t moveIDAddr2 = ResolvePointer(base, EFZ_BASE_OFFSET_P2, MOVE_ID_OFFSET);
        
        if (moveIDAddr1 && saved_y1 < -0.1) {
            short airState = FALLING_ID;
            SafeWriteMemory(moveIDAddr1, &airState, sizeof(short));
            SafeWriteMemory(yAddr1, &saved_y1, sizeof(double));
        }
        
        if (moveIDAddr2 && saved_y2 < -0.1) {
            short airState = FALLING_ID;
            SafeWriteMemory(moveIDAddr2, &airState, sizeof(short));
            SafeWriteMemory(yAddr2, &saved_y2, sizeof(double));
        }
    }
}

uint8_t GetPlayerInputs(int playerNum) {
    uintptr_t base = GetEFZBase();
    if (!base) return 0;
    
    uintptr_t baseOffset = (playerNum == 1) ? EFZ_BASE_OFFSET_P1 : EFZ_BASE_OFFSET_P2;
    uintptr_t playerPtr = 0;
    if (!SafeReadMemory(base + baseOffset, &playerPtr, sizeof(uintptr_t)) || !playerPtr) {
        return 0;
    }
    
    // Read input from offset 0xB8
    uint8_t inputs = 0;
    SafeReadMemory(playerPtr + P1_INPUT_OFFSET, &inputs, sizeof(uint8_t));
    return inputs;
}

bool GetPlayerFacingDirection(int playerNum) {
    uintptr_t base = GetEFZBase();
    if (!base) return true; // Default to facing right if can't read
    
    uintptr_t baseOffset = (playerNum == 1) ? EFZ_BASE_OFFSET_P1 : EFZ_BASE_OFFSET_P2;
    uintptr_t facingAddr = ResolvePointer(base, baseOffset, FACING_DIRECTION_OFFSET);
    
    if (!facingAddr) return true; // Default to facing right if can't resolve
    
    uint8_t facingValue = 0;
    if (!SafeReadMemory(facingAddr, &facingValue, sizeof(uint8_t))) {
        return true; // Default to facing right if can't read
    }
    
    // 1 = facing right, 255 (0xFF) = facing left
    return facingValue == 1;
}

void UpdatePlayerValuesExceptRF(uintptr_t base, uintptr_t baseOffsetP1, uintptr_t baseOffsetP2) {
    uintptr_t hpAddr1 = ResolvePointer(base, baseOffsetP1, HP_OFFSET);
    uintptr_t meterAddr1 = ResolvePointer(base, baseOffsetP1, METER_OFFSET);
    uintptr_t xAddr1 = ResolvePointer(base, baseOffsetP1, XPOS_OFFSET);
    uintptr_t yAddr1 = ResolvePointer(base, baseOffsetP1, YPOS_OFFSET);

    uintptr_t hpAddr2 = ResolvePointer(base, baseOffsetP2, HP_OFFSET);
    uintptr_t meterAddr2 = ResolvePointer(base, baseOffsetP2, METER_OFFSET);
    uintptr_t xAddr2 = ResolvePointer(base, baseOffsetP2, XPOS_OFFSET);
    uintptr_t yAddr2 = ResolvePointer(base, baseOffsetP2, YPOS_OFFSET);

    if (hpAddr1) {
        WriteGameMemory(hpAddr1, &displayData.hp1, sizeof(int));
        uintptr_t hpBar1 = ResolvePointer(base, baseOffsetP1, HP_BAR_OFFSET);
        if (hpBar1) WriteGameMemory(hpBar1, &displayData.hp1, sizeof(int));
    }
    if (meterAddr1) WriteGameMemory(meterAddr1, &displayData.meter1, sizeof(WORD));
    if (xAddr1) WriteGameMemory(xAddr1, &displayData.x1, sizeof(double));
    if (yAddr1) WriteGameMemory(yAddr1, &displayData.y1, sizeof(double));

    if (hpAddr2) {
        WriteGameMemory(hpAddr2, &displayData.hp2, sizeof(int));
        uintptr_t hpBar2 = ResolvePointer(base, baseOffsetP2, HP_BAR_OFFSET);
        if (hpBar2) WriteGameMemory(hpBar2, &displayData.hp2, sizeof(int));
    }
    if (meterAddr2) WriteGameMemory(meterAddr2, &displayData.meter2, sizeof(WORD));
    if (xAddr2) WriteGameMemory(xAddr2, &displayData.x2, sizeof(double));
    if (yAddr2) WriteGameMemory(yAddr2, &displayData.y2, sizeof(double));
}

std::atomic<bool> rfFreezing(false);
std::atomic<bool> rfFreezeP1Active(false);
std::atomic<bool> rfFreezeP2Active(false);
std::atomic<double> rfFreezeValueP1(0.0);
std::atomic<double> rfFreezeValueP2(0.0);
// Track provenance of RF freeze per-player
static std::atomic<int> rfFreezeOriginP1{ (int)RFFreezeOrigin::None };
static std::atomic<int> rfFreezeOriginP2{ (int)RFFreezeOrigin::None };
// Desired RF-freeze IC color lock settings (optional)
static bool rfFreezeColorP1Enabled = false;
static bool rfFreezeColorP1Blue = false;
static bool rfFreezeColorP2Enabled = false;
static bool rfFreezeColorP2Blue = false;

void StartRFFreeze(double p1Value, double p2Value) {
    rfFreezeValueP1.store(p1Value);
    rfFreezeValueP2.store(p2Value);

    rfFreezing.store(true);
    rfFreezeP1Active.store(true);
    rfFreezeP2Active.store(true);
    
    LogOut("[RF] Started freezing RF values: P1=" + std::to_string(p1Value) + 
           ", P2=" + std::to_string(p2Value), detailedLogging.load());
}

void StartRFFreezeOne(int player, double value) {
    if (player == 1) {
        // Deduplicate: if already active with same value, do nothing
        if (rfFreezeP1Active.load() && rfFreezeValueP1.load() == value) {
            return;
        }
        rfFreezeValueP1.store(value);
        rfFreezeP1Active.store(true);
    } else if (player == 2) {
        if (rfFreezeP2Active.load() && rfFreezeValueP2.load() == value) {
            return;
        }
        rfFreezeValueP2.store(value);
        rfFreezeP2Active.store(true);
    } else {
        return;
    }
    // Ensure global switch is on if any side is active
    rfFreezing.store(rfFreezeP1Active.load() || rfFreezeP2Active.load());
    LogOut(std::string("[RF] Started single-side RF freeze for P") + (player==1?"1":"2") +
           " value=" + std::to_string(value), detailedLogging.load());
}

void StopRFFreeze() {
    rfFreezing.store(false);
    rfFreezeP1Active.store(false);
    rfFreezeP2Active.store(false);
    LogOut("[RF] Stopped freezing RF values", detailedLogging.load());
}

void StopRFFreezePlayer(int player) {
    if (player == 1) {
        rfFreezeP1Active.store(false);
        // Clear origin when side becomes inactive
        rfFreezeOriginP1.store((int)RFFreezeOrigin::None);
    } else if (player == 2) {
        rfFreezeP2Active.store(false);
        rfFreezeOriginP2.store((int)RFFreezeOrigin::None);
    } else {
        return;
    }
    // If neither side is active, flip the global switch off
    if (!rfFreezeP1Active.load() && !rfFreezeP2Active.load()) {
        rfFreezing.store(false);
    }
    LogOut(std::string("[RF] Stopped RF freeze for P") + (player==1?"1":"2"), detailedLogging.load());
}

void SetRFFreezeNeutralOnly(bool enabled) {
    // Settings are read inside UpdateRFFreezeTick via Config::GetSettings(); setter kept for compatibility.
    (void)enabled; // no-op
}

// Single-tick updater driven by FrameDataMonitor
void UpdateRFFreezeTick() {
    if (!rfFreezing.load()) return;
    uintptr_t base = GetEFZBase();
    if (!base) return;
    uintptr_t p1Base = 0, p2Base = 0;
    if (!SafeReadMemory(base + EFZ_BASE_OFFSET_P1, &p1Base, sizeof(p1Base))) return;
    if (!SafeReadMemory(base + EFZ_BASE_OFFSET_P2, &p2Base, sizeof(p2Base))) return;
    if (!p1Base || !p2Base) return;
    double* p1RFAddr = (double*)(p1Base + RF_OFFSET);
    double* p2RFAddr = (double*)(p2Base + RF_OFFSET);
    // Optional neutral-only gating (own neutral) and optional both-neutral coupling to CR setting
    auto cfg = Config::GetSettings();
    bool neutralOnly = cfg.freezeRFOnlyWhenNeutral;
    bool requireBothNeutral = cfg.crRequireBothNeutral; // when enabled, require BOTH sides neutral for RF freeze writes
    int bothNeutralDelayMs = (cfg.crBothNeutralDelayMs < 0 ? 0 : cfg.crBothNeutralDelayMs);
    auto isAllowedNeutral = [](short m){ return (m==0 || m==1 || m==2 || m==3 || m==4 || m==7 || m==8 || m==9 || m==13); };
    short m1=0, m2=0;
    {
        short t1=0, t2=0;
        SafeReadMemory(p1Base + MOVE_ID_OFFSET, &t1, sizeof(t1));
        SafeReadMemory(p2Base + MOVE_ID_OFFSET, &t2, sizeof(t2));
        m1 = t1; m2 = t2;
    }
    // Track outcomes for logging outside SEH block
    bool didWriteP1 = false, didWriteP2 = false;
    double prevP1 = 0.0, newP1 = 0.0, prevP2 = 0.0, newP2 = 0.0;
    bool p1SkipNonNeutral = false, p2SkipNonNeutral = false;

    // Use SafeRead/Write to avoid SEH requirements
    double t1 = rfFreezeValueP1.load();
    double t2 = rfFreezeValueP2.load();
    bool p1Active = rfFreezeP1Active.load();
    bool p2Active = rfFreezeP2Active.load();
    bool p1Allowed = isAllowedNeutral(m1);
    bool p2Allowed = isAllowedNeutral(m2);
    bool bothAllowed = p1Allowed && p2Allowed;
    // Track both-neutral stability for delay (local to RF freeze)
    static bool s_prevBothNeutralRF = false;
    static unsigned long long s_bothNeutralStartRFMs = 0ULL;
    unsigned long long nowMs = (unsigned long long)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (neutralOnly && requireBothNeutral) {
        if (bothAllowed) {
            if (!s_prevBothNeutralRF) {
                s_bothNeutralStartRFMs = nowMs;
            }
        } else {
            s_bothNeutralStartRFMs = 0ULL;
        }
        s_prevBothNeutralRF = bothAllowed;
    } else {
        s_prevBothNeutralRF = false;
        s_bothNeutralStartRFMs = 0ULL;
    }
    unsigned long long bothElapsedMs = (s_bothNeutralStartRFMs == 0ULL) ? 0ULL : (nowMs - s_bothNeutralStartRFMs);
    bool bothAllowedWithDelay = bothAllowed && (!neutralOnly || !requireBothNeutral || (bothElapsedMs >= (unsigned long long)bothNeutralDelayMs));
    // If neutralOnly is enabled and CR requires both-neutral, then RF freeze honors both sides neutrality AND delay.
    // Otherwise, it honors only the target side's neutrality.
    bool canP1 = p1Active && (!neutralOnly || (requireBothNeutral ? bothAllowedWithDelay : p1Allowed));
    bool canP2 = p2Active && (!neutralOnly || (requireBothNeutral ? bothAllowedWithDelay : p2Allowed));
    if (canP1 && p1RFAddr) {
        double cur1 = 0.0;
        if (SafeReadMemory((uintptr_t)p1RFAddr, &cur1, sizeof(cur1)) && cur1 != t1) {
            if (SafeWriteMemory((uintptr_t)p1RFAddr, &t1, sizeof(t1))) {
                didWriteP1 = true; prevP1 = cur1; newP1 = t1;
            }
        }
    }
    if (canP2 && p2RFAddr) {
        double cur2 = 0.0;
        if (SafeReadMemory((uintptr_t)p2RFAddr, &cur2, sizeof(cur2)) && cur2 != t2) {
            if (SafeWriteMemory((uintptr_t)p2RFAddr, &t2, sizeof(t2))) {
                didWriteP2 = true; prevP2 = cur2; newP2 = t2;
            }
        }
    }
    bool p1SkipDelay = false, p2SkipDelay = false;
    if (p1Active && !canP1 && neutralOnly) {
        if (requireBothNeutral && bothAllowed && (bothElapsedMs < (unsigned long long)bothNeutralDelayMs)) {
            p1SkipDelay = true;
        } else {
            p1SkipNonNeutral = true;
        }
    }
    if (p2Active && !canP2 && neutralOnly) {
        if (requireBothNeutral && bothAllowed && (bothElapsedMs < (unsigned long long)bothNeutralDelayMs)) {
            p2SkipDelay = true;
        } else {
            p2SkipNonNeutral = true;
        }
    }
    // Optional: enforce IC color while RF is frozen (per-player)
    static bool s_lastP1Color = false;
    static bool s_lastP1ColorEnabled = false;
    static bool s_lastP2Color = false;
    static bool s_lastP2ColorEnabled = false;
    if (rfFreezeP1Active.load() && rfFreezeColorP1Enabled) {
        if (!s_lastP1ColorEnabled || s_lastP1Color != rfFreezeColorP1Blue) {
            std::ostringstream logMsg;
            logMsg << "[IC][RF_FREEZE] P1 IC color enforced by RF freeze: "
                   << (rfFreezeColorP1Blue ? "Blue" : "Red")
                   << " (was " << (s_lastP1ColorEnabled ? (s_lastP1Color ? "Blue" : "Red") : "unmanaged") << ")";
            LogOut(logMsg.str(), true);
            SetICColorPlayer(1, rfFreezeColorP1Blue);
            s_lastP1ColorEnabled = true; s_lastP1Color = rfFreezeColorP1Blue;
        }
    } else {
        s_lastP1ColorEnabled = false; // allow re-apply next time it enables
    }
    if (rfFreezeP2Active.load() && rfFreezeColorP2Enabled) {
        if (!s_lastP2ColorEnabled || s_lastP2Color != rfFreezeColorP2Blue) {
            std::ostringstream logMsg;
            logMsg << "[IC][RF_FREEZE] P2 IC color enforced by RF freeze: "
                   << (rfFreezeColorP2Blue ? "Blue" : "Red")
                   << " (was " << (s_lastP2ColorEnabled ? (s_lastP2Color ? "Blue" : "Red") : "unmanaged") << ")";
            LogOut(logMsg.str(), true);
            SetICColorPlayer(2, rfFreezeColorP2Blue);
            s_lastP2ColorEnabled = true; s_lastP2Color = rfFreezeColorP2Blue;
        }
    } else {
        s_lastP2ColorEnabled = false;
    }

    // Log outside SEH for safety
    #if defined(ENABLE_FRAME_ADV_DEBUG)
    if (true) {
        if (didWriteP1) {
            char buf[256];
            snprintf(buf, sizeof(buf), "[RF][Freeze] P1 write %.1f->%.1f moveID=%d allowedNeutral=%s neutralOnly=%s bothNeutralGate=%s delayMs=%d elapsedMs=%llu oppMoveID=%d oppAllowed=%s",
                     prevP1, newP1, (int)m1, (isAllowedNeutral(m1)?"true":"false"), (neutralOnly?"true":"false"),
                     (requireBothNeutral?"true":"false"), bothNeutralDelayMs, (unsigned long long)bothElapsedMs, (int)m2, (isAllowedNeutral(m2)?"true":"false"));
            LogOut(buf, true);
        }
        if (didWriteP2) {
            char buf[256];
            snprintf(buf, sizeof(buf), "[RF][Freeze] P2 write %.1f->%.1f moveID=%d allowedNeutral=%s neutralOnly=%s bothNeutralGate=%s delayMs=%d elapsedMs=%llu oppMoveID=%d oppAllowed=%s",
                     prevP2, newP2, (int)m2, (isAllowedNeutral(m2)?"true":"false"), (neutralOnly?"true":"false"),
                     (requireBothNeutral?"true":"false"), bothNeutralDelayMs, (unsigned long long)bothElapsedMs, (int)m1, (isAllowedNeutral(m1)?"true":"false"));
            LogOut(buf, true);
        }
        if (p1SkipNonNeutral) {
            char buf[192];
            if (requireBothNeutral) {
                snprintf(buf, sizeof(buf), "[RF][Freeze] P1 skip: both-not-neutral selfMove=%d selfAllowed=%s oppMove=%d oppAllowed=%s",
                        (int)m1, (isAllowedNeutral(m1)?"true":"false"), (int)m2, (isAllowedNeutral(m2)?"true":"false"));
            } else {
                snprintf(buf, sizeof(buf), "[RF][Freeze] P1 skip non-neutral moveID=%d allowedNeutral=%s", (int)m1, (isAllowedNeutral(m1)?"true":"false"));
            }
            LogOut(buf, true);
        }
        if (p1SkipDelay) {
            char buf[192];
            snprintf(buf, sizeof(buf), "[RF][Freeze] P1 skip: both-neutral delay not met elapsed=%llums required=%dms selfMove=%d oppMove=%d",
                    (unsigned long long)bothElapsedMs, bothNeutralDelayMs, (int)m1, (int)m2);
            LogOut(buf, true);
        }
        if (p2SkipNonNeutral) {
            char buf[192];
            if (requireBothNeutral) {
                snprintf(buf, sizeof(buf), "[RF][Freeze] P2 skip: both-not-neutral selfMove=%d selfAllowed=%s oppMove=%d oppAllowed=%s",
                        (int)m2, (isAllowedNeutral(m2)?"true":"false"), (int)m1, (isAllowedNeutral(m1)?"true":"false"));
            } else {
                snprintf(buf, sizeof(buf), "[RF][Freeze] P2 skip non-neutral moveID=%d allowedNeutral=%s", (int)m2, (isAllowedNeutral(m2)?"true":"false"));
            }
            LogOut(buf, true);
        }
        if (p2SkipDelay) {
            char buf[192];
            snprintf(buf, sizeof(buf), "[RF][Freeze] P2 skip: both-neutral delay not met elapsed=%llums required=%dms selfMove=%d oppMove=%d",
                    (unsigned long long)bothElapsedMs, bothNeutralDelayMs, (int)m2, (int)m1);
            LogOut(buf, true);
        }
    }
    #endif
}

void SetRFFreezeColorDesired(int player, bool enabled, bool blueIC) {
    if (player == 1) {
        rfFreezeColorP1Enabled = enabled;
        rfFreezeColorP1Blue = blueIC;
    } else if (player == 2) {
        rfFreezeColorP2Enabled = enabled;
        rfFreezeColorP2Blue = blueIC;
    }
}

bool IsRFFreezeColorManaging(int player) {
    if (player == 1) {
        return rfFreezeP1Active.load() && rfFreezeColorP1Enabled;
    } else if (player == 2) {
        return rfFreezeP2Active.load() && rfFreezeColorP2Enabled;
    }
    return false;
}

void StartRFFreezeOneFromUI(int player, double value) {
    StartRFFreezeOne(player, value);
    if (player == 1) rfFreezeOriginP1.store((int)RFFreezeOrigin::ManualUI);
    else if (player == 2) rfFreezeOriginP2.store((int)RFFreezeOrigin::ManualUI);
}

void StartRFFreezeOneFromCR(int player, double value) {
    StartRFFreezeOne(player, value);
    if (player == 1) rfFreezeOriginP1.store((int)RFFreezeOrigin::ContinuousRecovery);
    else if (player == 2) rfFreezeOriginP2.store((int)RFFreezeOrigin::ContinuousRecovery);
}

bool GetRFFreezeStatus(int player, bool& isActive, double& value, bool& colorManaged, bool& colorBlue) {
    if (player == 1) {
        isActive = rfFreezeP1Active.load();
        value = rfFreezeValueP1.load();
        colorManaged = (isActive && rfFreezeColorP1Enabled);
        colorBlue = rfFreezeColorP1Blue;
        return true;
    } else if (player == 2) {
        isActive = rfFreezeP2Active.load();
        value = rfFreezeValueP2.load();
        colorManaged = (isActive && rfFreezeColorP2Enabled);
        colorBlue = rfFreezeColorP2Blue;
        return true;
    }
    return false;
}

RFFreezeOrigin GetRFFreezeOrigin(int player) {
    if (player == 1) return (RFFreezeOrigin)rfFreezeOriginP1.load();
    if (player == 2) return (RFFreezeOrigin)rfFreezeOriginP2.load();
    return RFFreezeOrigin::None;
}
