#include "../include/input/input_buffer.h"
#include "../include/input/input_core.h"
#include "../include/core/constants.h"
#include "../include/core/memory.h"
#include "../include/core/logger.h"
#include "../include/utils/utilities.h"
#include "../include/input/motion_constants.h"
#include "../include/core/globals.h"
#include "../include/input/shared_constants.h"
#include "../include/input/input_debug.h"
#include "../include/input/motion_system.h"
#include "../include/input/input_motion.h"  
#include "../include/game/game_state.h"  
#include "../include/game/auto_action.h"
#include "../include/input/input_freeze.h"
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <cstring>
#include "runtime/practice_runtime.h"
#include "runtime/practice_session.h"
#include "runtime/practice_worker.h"

// The buffer is 180 bytes; a 0x180 (384) length would overrun adjacent fields
// including the index at +0x260.
const uint16_t INPUT_BUFFER_SIZE = 180;
const uintptr_t INPUT_BUFFER_OFFSET = 0x1AB;
const uintptr_t INPUT_BUFFER_INDEX_OFFSET = 0x260;

std::atomic<bool> g_bufferFreezingActive(false);
std::atomic<bool> g_indexFreezingActive(false);
std::vector<uint8_t> g_frozenBufferValues;
uint16_t g_frozenBufferStartIndex = 0;
uint16_t g_frozenBufferLength = 0;
uint16_t g_frozenIndexValue = 0;
std::atomic<int> g_activeFreezePlayer{0};
std::recursive_mutex g_bufferFreezeControlMutex;

namespace {
std::atomic<uint64_t> g_bufferFreezeGeneration{0};
EfzTmEntryV1 bufferWorld{};
bool bufferWorldBound=false;
std::atomic<bool> bufferWorkClosed{false},bufferWorkerFault{false};
HANDLE bufferWake=nullptr;
struct BufferThreadRecord {HANDLE thread=nullptr;HMODULE module=nullptr;};
std::vector<BufferThreadRecord> bufferThreads;
struct BufferWrites {
    uint8_t before[180]{},last[180]{};
    bool owned[180]{};
    uint16_t indexBefore=0,indexLast=0;
    bool indexOwned=false;
};
BufferWrites bufferWrites[2];
bool SameBufferWorld(const EfzTmEntryV1& a,const EfzTmEntryV1& b) {
    return a.id.providerIncarnation==b.id.providerIncarnation && a.id.practiceSession==b.id.practiceSession &&
        a.id.battleWorld==b.id.battleWorld && a.id.timeline==b.id.timeline &&
        a.gameSystem==b.gameSystem && a.battleContext==b.battleContext && a.player1==b.player1 && a.player2==b.player2 &&
        a.efzBase==b.efzBase && a.revivalBase==b.revivalBase && a.profileId==b.profileId;
}
bool CopyBufferData(uintptr_t address,const void* source,size_t size) {
    if(!address || !source || !size || address+size<address)return false;
    uintptr_t cursor=address;
    while(cursor<address+size) {
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(reinterpret_cast<void*>(cursor),&info,sizeof(info)) || info.State!=MEM_COMMIT ||
           (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        const DWORD protection=info.Protect&0xff;
        if(protection!=PAGE_READWRITE && protection!=PAGE_WRITECOPY &&
           protection!=PAGE_EXECUTE_READWRITE && protection!=PAGE_EXECUTE_WRITECOPY)return false;
        const uintptr_t end=reinterpret_cast<uintptr_t>(info.BaseAddress)+info.RegionSize;
        if(end<=cursor)return false;
        cursor=(std::min)(end,address+size);
    }
    __try {memcpy(reinterpret_cast<void*>(address),source,size);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool WriteFreezeMemory(uintptr_t address,const void* source,size_t size) {
    if(!bufferWorldBound)return SafeWriteMemory(address,source,size);
    for(int player=0;player<2;++player) {
        const uintptr_t base=player?bufferWorld.player2:bufferWorld.player1;
        auto& record=bufferWrites[player];
        if(address>=base+INPUT_BUFFER_OFFSET && address+size>=address && address+size<=base+INPUT_BUFFER_OFFSET+INPUT_BUFFER_SIZE) {
            const size_t offset=address-base-INPUT_BUFFER_OFFSET;
            for(size_t n=0;n<size;++n) {
                const size_t index=offset+n;
                if(!record.owned[index] && !SafeReadMemory(address+n,&record.before[index],1))return false;
                record.owned[index]=true;record.last[index]=static_cast<const uint8_t*>(source)[n];
            }
            return CopyBufferData(address,source,size);
        }
        if(address==base+INPUT_BUFFER_INDEX_OFFSET && size==sizeof(uint16_t)) {
            if(!record.indexOwned && !SafeReadMemory(address,&record.indexBefore,sizeof(uint16_t)))return false;
            record.indexOwned=true;memcpy(&record.indexLast,source,sizeof(uint16_t));
            return CopyBufferData(address,source,size);
        }
    }
    return false;
}
uintptr_t FreezePlayerPointer(int player) {
    return bufferWorldBound?(player==1?bufferWorld.player1:player==2?bufferWorld.player2:0):GetPlayerPointer(player);
}
bool FreezeFacing(int player) {
    if(!bufferWorldBound)return GetPlayerFacingDirection(player);
    uint8_t facing=0;
    return SafeReadMemory(FreezePlayerPointer(player)+FACING_DIRECTION_OFFSET,&facing,sizeof(facing)) && facing==1;
}
bool ReapBufferThreads(bool drained=false) {
    for(auto it=bufferThreads.begin();it!=bufferThreads.end();) {
        const DWORD result=WaitForSingleObject(it->thread,drained?INFINITE:0);
        if(result==WAIT_TIMEOUT){++it;continue;}
        if(result!=WAIT_OBJECT_0 || !CloseHandle(it->thread)){bufferWorkerFault.store(true);++it;continue;}
        if(it->module)FreeLibrary(it->module);
        it=bufferThreads.erase(it);
    }
    return bufferThreads.empty();
}
std::atomic<uint64_t> g_freezeInitializedGeneration{0};

struct BufferFreezeSnapshot {
    uint64_t generation = 0;
    int playerNum = 0;
    std::vector<uint8_t> values;
    uint16_t startIndex = 0;
    uint16_t length = 0;
    bool freezeIndex = false;
    uint16_t indexValue = 0;
    int motionType = -1;
    int buttonMask = 0;
    bool facingRight = false;
    bool scoped=false;
    EfzTmEntryV1 world{};
};

bool OwnsFreezeGeneration(uint64_t generation) {
    return generation != 0 &&
           g_bufferFreezeGeneration.load(std::memory_order_acquire) == generation &&
           !bufferWorkClosed.load(std::memory_order_acquire);
}

uint64_t NextFreezeGeneration() {
    uint64_t next = g_bufferFreezeGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (next == 0) {
        next = g_bufferFreezeGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
    }
    return next;
}

template <typename Fn>
bool WithOwnedFreezeMemory(uint64_t generation, Fn&& fn) {
    std::lock_guard<std::recursive_mutex> lock(g_bufferFreezeControlMutex);
    if (!OwnsFreezeGeneration(generation) ||
        !g_bufferFreezingActive.load(std::memory_order_acquire) ||
        !IsPracticeContext()) {
        return false;
    }
    fn();
    return true;
}
}

int g_frozenMotionType = -1;
int g_frozenButtonMask = 0;
bool g_lastKnownFacing = false;

static bool RegeneratePatternForFacing(int playerNum, int motionType, int buttonMask,
                                       bool facingRight, uint16_t startIndex,
                                       std::vector<uint8_t>& frozenValues,
                                       uint16_t& frozenLength) {
    uint8_t fwd = facingRight ? GAME_INPUT_RIGHT : GAME_INPUT_LEFT;
    uint8_t back = facingRight ? GAME_INPUT_LEFT : GAME_INPUT_RIGHT;
    uint8_t down = GAME_INPUT_DOWN;
    uint8_t downFwd = down | fwd;
    uint8_t downBack = down | back;
    
    std::vector<uint8_t> pattern;
    
    // Patterns must match FreezeBufferForMotionImpl in input_freeze.cpp
    switch (motionType) {
        case MOTION_623A: case MOTION_623B: case MOTION_623C: {
            // Dragon Punch (623): Forward, Down, Down-Forward + Button
            pattern = {
                0x00, 0x00,
                fwd, fwd, fwd,
                down, down,
                downFwd, downFwd,
                (uint8_t)(downFwd | buttonMask),
                (uint8_t)(downFwd | buttonMask),
            };
            break;
        }
        case MOTION_214A: case MOTION_214B: case MOTION_214C: {
            // QCB: Down, Down-Back, Back + Button
            pattern = {
                0x00, 0x00,
                down, down, down,
                downBack, downBack,
                back, back,
                (uint8_t)(back | buttonMask),
                (uint8_t)(back | buttonMask),
            };
            break;
        }
        case MOTION_236A: case MOTION_236B: case MOTION_236C: {
            // QCF: Down, Down-Forward, Forward + Button
            pattern = {
                0x00, 0x00,
                down, down, down,
                downFwd, downFwd,
                fwd, fwd,
                (uint8_t)(fwd | buttonMask),
                (uint8_t)(fwd | buttonMask),
            };
            break;
        }
        case MOTION_421A: case MOTION_421B: case MOTION_421C: {
            // 421: Back, Down, Down-Back + Button
            pattern = {
                0x00, 0x00,
                back, back,
                down, down,
                downBack, downBack,
                (uint8_t)(downBack | buttonMask),
                (uint8_t)(downBack | buttonMask),
            };
            break;
        }
        case MOTION_41236A: case MOTION_41236B: case MOTION_41236C: {
            // HCF: Back, Down-Back, Down, Down-Forward, Forward + Button
            pattern = {
                0x00, 0x00,
                back, back,
                downBack, downBack,
                down, down,
                downFwd, downFwd,
                fwd,
                (uint8_t)(fwd | buttonMask),
                (uint8_t)(fwd | buttonMask),
            };
            break;
        }
        case MOTION_214214A: case MOTION_214214B: case MOTION_214214C: {
            // Double QCB: (Down, Down-Back, Back)x2 + Button
            pattern = {
                0x00, 0x00,
                down, down, down,
                downBack, downBack,
                back, back,
                down, down, down,
                downBack, downBack,
                back, back,
                (uint8_t)(back | buttonMask),
                (uint8_t)(back | buttonMask),
            };
            break;
        }
        case MOTION_236236A: case MOTION_236236B: case MOTION_236236C: {
            // Double QCF: (Down, Down-Forward, Forward)x2 + Button
            pattern = {
                0x00, 0x00,
                down, down, down,
                downFwd, downFwd,
                fwd, fwd,
                down, down, down,
                downFwd, downFwd,
                fwd, fwd,
                (uint8_t)(fwd | buttonMask),
                (uint8_t)(fwd | buttonMask),
            };
            break;
        }
        case MOTION_641236A: case MOTION_641236B: case MOTION_641236C: {
            // 641236: Forward, Back, Down-Back, Down, Down-Forward, Forward + Button
            pattern = {
                0x00, 0x00,
                fwd, fwd,
                back, back,
                downBack, downBack,
                down, down,
                downFwd, downFwd,
                fwd,
                (uint8_t)(fwd | buttonMask),
                (uint8_t)(fwd | buttonMask),
            };
            break;
        }
        case MOTION_412A: case MOTION_412B: case MOTION_412C: {
            // 412: Back, Down-Back, Down + Button
            pattern = {
                0x00, 0x00,
                back, back,
                downBack, downBack,
                down,
                (uint8_t)(down | buttonMask),
                (uint8_t)(down | buttonMask),
            };
            break;
        }
        case MOTION_22A: case MOTION_22B: case MOTION_22C: {
            // 22: Down, (small neutral), Down + Button
            pattern = {
                0x00, 0x00,
                down, down,
                0x00, 0x00,
                down,
                (uint8_t)(down | buttonMask),
                (uint8_t)(down | buttonMask),
            };
            break;
        }
        case MOTION_2141236A: case MOTION_2141236B: case MOTION_2141236C: {
            // 2141236: Down, Down-Back, Back, Down-Back, Down, Down-Forward, Forward + Button
            pattern = {
                0x00, 0x00,
                down, down,
                downBack, downBack,
                back, back,
                downBack, downBack,
                down, down,
                downFwd, downFwd,
                fwd,
                (uint8_t)(fwd | buttonMask),
                (uint8_t)(fwd | buttonMask),
            };
            break;
        }
        case MOTION_463214A: case MOTION_463214B: case MOTION_463214C: {
            // 463214: Back, Forward, Down-Forward, Down, Down-Back, Back + Button
            pattern = {
                0x00, 0x00,
                back, back,
                fwd, fwd,
                downFwd, downFwd,
                down, down,
                downBack, downBack,
                back,
                (uint8_t)(back | buttonMask),
                (uint8_t)(back | buttonMask),
            };
            break;
        }
        case MOTION_4123641236A: case MOTION_4123641236B: case MOTION_4123641236C: {
            // 41236 41236: (Back, Down-Back, Down, Down-Forward, Forward)x2 + Button
            pattern = {
                0x00, 0x00,
                back, back, downBack, downBack, down, down, downFwd, downFwd, fwd, fwd,
                back, back, downBack, downBack, down, down, downFwd, downFwd, fwd,
                (uint8_t)(fwd | buttonMask),
                (uint8_t)(fwd | buttonMask),
            };
            break;
        }
        case MOTION_6321463214A: case MOTION_6321463214B: case MOTION_6321463214C: {
            // 6321463214: (Forward, Down-Forward, Down, Down-Back, Back)x2 + Button
            pattern = {
                0x00, 0x00,
                fwd, fwd,
                downFwd, downFwd,
                down, down,
                downBack, downBack,
                back, back,
                fwd, fwd,
                downFwd, downFwd,
                down, down,
                downBack, downBack,
                back,
                (uint8_t)(back | buttonMask),
                (uint8_t)(back | buttonMask),
            };
            break;
        }
        default:
            LogOut("[BUFFER_FREEZE][CROSSUP] Cannot regenerate pattern for motion type " + std::to_string(motionType), true);
            return false;
    }
    
    if (pattern.empty()) {
        return false;
    }
    
    frozenValues = pattern;
    frozenLength = static_cast<uint16_t>(pattern.size());
    
    uintptr_t playerPtr = FreezePlayerPointer(playerNum);
    if (playerPtr) {
        const uint16_t len = static_cast<uint16_t>(pattern.size());
        const uint16_t len1 = static_cast<uint16_t>(
            std::min<uint16_t>(len, INPUT_BUFFER_SIZE - startIndex));
        const uint16_t len2 = static_cast<uint16_t>(len - len1);
        if (len1 != 0) {
            WriteFreezeMemory(playerPtr + INPUT_BUFFER_OFFSET + startIndex,
                            pattern.data(), len1);
        }
        if (len2 != 0) {
            WriteFreezeMemory(playerPtr + INPUT_BUFFER_OFFSET,
                            pattern.data() + len1, len2);
        }
        if (detailedLogging.load()) {
            LogOut("[BUFFER_FREEZE][CROSSUP] Pattern regenerated for new facing direction", true);
        }
        return true;
    }
    return false;
}

static void FreezeBufferValuesThread(BufferFreezeSnapshot state) {
    const int playerNum = state.playerNum;
    const uint64_t generation = state.generation;
    auto work=state.scoped?Practice::TryEnterMonitorWork():Practice::WorkLease{};
    if(state.scoped && (!work || !SameBufferWorld(work.Context(),state.world)))return;
    // CRITICAL: Never run during online mode
    if (g_onlineModeActive.load()) {
        std::lock_guard<std::recursive_mutex> lock(g_bufferFreezeControlMutex);
        if (OwnsFreezeGeneration(generation)) {
            g_bufferFreezingActive.store(false, std::memory_order_release);
            g_indexFreezingActive.store(false, std::memory_order_release);
            g_freezeInitializedGeneration.store(0, std::memory_order_release);
            int expectedPlayer = playerNum;
            (void)g_activeFreezePlayer.compare_exchange_strong(expectedPlayer, 0);
        }
        return;
    }

    if (detailedLogging.load()) {
        std::stringstream ss;
        ss << "[INPUT_BUFFER] Starting buffer freeze thread for P" << playerNum
           << " generation=" << generation
           << " startIdx=" << state.startIndex
           << " len=" << state.length
           << " idxLock=" << (state.freezeIndex ? std::to_string(state.indexValue) : std::string("off"))
           << " motionType=" << state.motionType
           << " btnMask=0x" << std::hex << state.buttonMask << std::dec
           << " facing=" << (state.facingRight ? "right" : "left");
        LogOut(ss.str(), true);
    }
    uintptr_t initialPlayerPtr = FreezePlayerPointer(playerNum);
    if (!initialPlayerPtr) {
        LogOut("[INPUT_BUFFER] Invalid player pointer at thread start, aborting", true);
        std::lock_guard<std::recursive_mutex> lock(g_bufferFreezeControlMutex);
        if (OwnsFreezeGeneration(generation)) {
            g_bufferFreezingActive.store(false, std::memory_order_release);
            g_indexFreezingActive.store(false, std::memory_order_release);
            g_freezeInitializedGeneration.store(0, std::memory_order_release);
            int expectedPlayer = playerNum;
            (void)g_activeFreezePlayer.compare_exchange_strong(expectedPlayer, 0);
        }
        return;
    }
    
    static std::atomic<bool> s_layoutChecked{false};
    if (!s_layoutChecked.load()) {
        uintptr_t testPlayer = initialPlayerPtr;
        if (testPlayer) {
            uintptr_t bufStart = testPlayer + INPUT_BUFFER_OFFSET;
            uintptr_t bufEnd   = bufStart + INPUT_BUFFER_SIZE - 1;
            uintptr_t idxAddr  = testPlayer + INPUT_BUFFER_INDEX_OFFSET;
            if (detailedLogging.load()) {
                std::stringstream ss;
                ss << "[INPUT_BUFFER] Layout: start=0x" << std::hex << bufStart
                   << " end=0x" << bufEnd << " index=0x" << idxAddr
                   << std::dec;
                LogOut(ss.str(), true);
            }
            if (bufEnd >= idxAddr && idxAddr >= bufStart) {
                LogOut("[INPUT_BUFFER][WARN] Buffer region overlaps index! Adjust sizes/offsets.", true);
            }
        }
        s_layoutChecked.store(true);
    }

    uintptr_t base = state.scoped?state.world.efzBase:GetEFZBase();
    uintptr_t moveIDAddr = state.scoped?initialPlayerPtr+MOVE_ID_OFFSET:ResolvePointer(base, 
        (playerNum == 1) ? EFZ_BASE_OFFSET_P1 : EFZ_BASE_OFFSET_P2, 
        MOVE_ID_OFFSET);
    
    uintptr_t motionTokenAddr = initialPlayerPtr + MOTION_TOKEN_OFFSET;
    
    short lastMoveID = -1;
    uint8_t lastMotionToken = 0;
    bool moveIDChanged = false;
    bool motionTokenChanged = false;
    int consecutiveMoveTicks = 0;
    const int freezeLimit = 300;

    SafeReadMemory(moveIDAddr, &lastMoveID, sizeof(short));
    SafeReadMemory(motionTokenAddr, &lastMotionToken, sizeof(uint8_t));
    
    {
        std::lock_guard<std::recursive_mutex> lock(g_bufferFreezeControlMutex);
        if (!OwnsFreezeGeneration(generation) ||
            !g_bufferFreezingActive.load(std::memory_order_acquire)) return;
        g_freezeInitializedGeneration.store(generation, std::memory_order_release);
    }

    const int aggressivePhaseFrames = 15;
    for (int i = 0; i < aggressivePhaseFrames &&
                    g_bufferFreezingActive.load(std::memory_order_acquire) &&
                    OwnsFreezeGeneration(generation) && (!state.scoped || work.IsCurrent()); i++) {
        if (!IsPracticeContext()) break;
        uintptr_t playerPtr = FreezePlayerPointer(playerNum);
        if (!playerPtr || playerPtr != initialPlayerPtr) {
            LogOut("[INPUT_BUFFER] Player pointer changed during aggressive phase, aborting", true);
            break;
        }
        
        if (state.motionType >= 0) {
            bool currentFacing = FreezeFacing(playerNum);
            if (currentFacing != state.facingRight) {
                if (detailedLogging.load()) {
                    LogOut("[BUFFER_FREEZE][CROSSUP] Facing direction changed: " +
                          std::string(state.facingRight ? "right" : "left") + " → " +
                          std::string(currentFacing ? "right" : "left") + " during freeze!", true);
                }
                if (!WithOwnedFreezeMemory(generation, [&]() {
                        state.facingRight = currentFacing;
                        RegeneratePatternForFacing(
                            playerNum, state.motionType, state.buttonMask,
                            currentFacing, state.startIndex,
                            state.values, state.length);
                    })) break;
            }
        }
        
        if (!WithOwnedFreezeMemory(generation, [&]() {
                if (state.length > 0) {
                    const uint16_t start = state.startIndex;
                    const uint16_t len1 = (uint16_t)std::min<uint16_t>(
                        state.length, INPUT_BUFFER_SIZE - start);
                    const uint16_t len2 = state.length - len1;
                    if (len1 > 0) {
                        WriteFreezeMemory(playerPtr + INPUT_BUFFER_OFFSET + start,
                                        state.values.data(), len1);
                    }
                    if (len2 > 0) {
                        WriteFreezeMemory(playerPtr + INPUT_BUFFER_OFFSET,
                                        state.values.data() + len1, len2);
                    }
                }
                if (state.freezeIndex) {
                    uint16_t curIdx = 0xFFFF;
                    if (!SafeReadMemory(playerPtr + INPUT_BUFFER_INDEX_OFFSET,
                                        &curIdx, sizeof(curIdx)) ||
                        curIdx != state.indexValue) {
                        WriteFreezeMemory(playerPtr + INPUT_BUFFER_INDEX_OFFSET,
                                        &state.indexValue, sizeof(state.indexValue));
                    }
                }
            })) break;
        
        short currentMoveID = 0;
        if (SafeReadMemory(moveIDAddr, &currentMoveID, sizeof(short))) {
            if (currentMoveID != lastMoveID) {
                if (detailedLogging.load()) {
                    LogOut("[BUFFER_FREEZE] MoveID changed: " + std::to_string(lastMoveID) + 
                          " → " + std::to_string(currentMoveID), true);
                }
                lastMoveID = currentMoveID;
                moveIDChanged = true;
            }
        }
        
        uint8_t currentMotionToken = 0;
        if (SafeReadMemory(motionTokenAddr, &currentMotionToken, sizeof(uint8_t))) {
            if (currentMotionToken != lastMotionToken && currentMotionToken != 0 && currentMotionToken != 0x63) {
                if (detailedLogging.load()) {
                    std::stringstream ss;
                    ss << "[BUFFER_FREEZE] Motion token changed: 0x" 
                       << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(lastMotionToken)
                       << " → 0x" << std::setw(2) << std::setfill('0') << static_cast<int>(currentMotionToken)
                       << " (motion queued, waiting for execution)";
                    LogOut(ss.str(), true);
                }
                motionTokenChanged = true;
            }
            lastMotionToken = currentMotionToken;
        }
        
        if(WaitForSingleObject(bufferWake,2)==WAIT_FAILED){bufferWorkerFault.store(true);break;}
    }
    
    int freezeCount = aggressivePhaseFrames;
    while (g_bufferFreezingActive.load(std::memory_order_acquire) &&
           OwnsFreezeGeneration(generation) && (!state.scoped || work.IsCurrent()) &&
           freezeCount < freezeLimit && !g_isShuttingDown.load()) {
        if (!IsPracticeContext()) { LogOut("[INPUT_BUFFER] Left Practice, stopping buffer freeze", true); break; }
        GamePhase currentPhase = state.scoped?GamePhase::Match:GetCurrentGamePhase();
        if (currentPhase != GamePhase::Match) {
            LogOut("[INPUT_BUFFER] Game phase changed, stopping buffer freeze", true);
            break;
        }
        
        uintptr_t currentPlayerPtr = FreezePlayerPointer(playerNum);
        if (!currentPlayerPtr || currentPlayerPtr != initialPlayerPtr) {
            LogOut("[INPUT_BUFFER] Player pointer changed, stopping buffer freeze", true);
            break;
        }
        
        if (state.motionType >= 0 && freezeCount % 3 == 0) {
            bool currentFacing = FreezeFacing(playerNum);
            if (currentFacing != state.facingRight) {
                if (detailedLogging.load()) {
                    LogOut("[BUFFER_FREEZE][CROSSUP] Facing direction changed: " +
                          std::string(state.facingRight ? "right" : "left") + " → " +
                          std::string(currentFacing ? "right" : "left") + " during freeze!", true);
                }
                if (!WithOwnedFreezeMemory(generation, [&]() {
                        state.facingRight = currentFacing;
                        RegeneratePatternForFacing(
                            playerNum, state.motionType, state.buttonMask,
                            currentFacing, state.startIndex,
                            state.values, state.length);
                    })) break;
            }
        }

        if (!WithOwnedFreezeMemory(generation, [&]() {
                if (freezeCount % 3 == 0 && state.length > 0) {
                    const uint16_t start = state.startIndex;
                    const uint16_t len1 = (uint16_t)std::min<uint16_t>(
                        state.length, INPUT_BUFFER_SIZE - start);
                    const uint16_t len2 = state.length - len1;
                    if (len1 > 0) {
                        WriteFreezeMemory(currentPlayerPtr + INPUT_BUFFER_OFFSET + start,
                                        state.values.data(), len1);
                    }
                    if (len2 > 0) {
                        WriteFreezeMemory(currentPlayerPtr + INPUT_BUFFER_OFFSET,
                                        state.values.data() + len1, len2);
                    }
                }
                if (state.freezeIndex) {
                    uint16_t curIdx = 0xFFFF;
                    if (!SafeReadMemory(currentPlayerPtr + INPUT_BUFFER_INDEX_OFFSET,
                                        &curIdx, sizeof(curIdx)) ||
                        curIdx != state.indexValue) {
                        WriteFreezeMemory(currentPlayerPtr + INPUT_BUFFER_INDEX_OFFSET,
                                        &state.indexValue, sizeof(state.indexValue));
                    }
                }
            })) break;
        
        short currentMoveID = 0;
        if (SafeReadMemory(moveIDAddr, &currentMoveID, sizeof(short))) {
            if (currentMoveID != lastMoveID) {
                if (detailedLogging.load()) {
                    LogOut("[BUFFER_FREEZE] MoveID changed: " + std::to_string(lastMoveID) + 
                          " → " + std::to_string(currentMoveID), true);
                }
                lastMoveID = currentMoveID;
                moveIDChanged = true;
            }
            
            if (currentMoveID != 0 && moveIDChanged) {
                consecutiveMoveTicks++;
                if (consecutiveMoveTicks >= 3) {
                    if (detailedLogging.load()) {
                        LogOut("[BUFFER_FREEZE] Motion recognized! Move ID: " + 
                              std::to_string(currentMoveID), true);
                    }
                    break;
                }
            } else {
                consecutiveMoveTicks = 0;
            }
        }
        
        uint8_t currentMotionToken = 0;
        if (SafeReadMemory(motionTokenAddr, &currentMotionToken, sizeof(uint8_t))) {
            if (currentMotionToken != lastMotionToken && currentMotionToken != 0 && currentMotionToken != 0x63) {
                if (detailedLogging.load()) {
                    std::stringstream ss;
                    ss << "[BUFFER_FREEZE] Motion token changed: 0x" 
                       << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(lastMotionToken)
                       << " → 0x" << std::setw(2) << std::setfill('0') << static_cast<int>(currentMotionToken)
                       << " (motion queued, waiting for execution)";
                    LogOut(ss.str(), true);
                }
                motionTokenChanged = true;
            }
            lastMotionToken = currentMotionToken;
        }
        
        freezeCount++;
        if(WaitForSingleObject(bufferWake,4)==WAIT_FAILED){bufferWorkerFault.store(true);break;}
    }
    
    if(state.scoped && !work.IsCurrent())return;
    // Do not wipe the whole buffer here; neutralize trailing inputs only.
    if (detailedLogging.load()) {
        LogOut("[BUFFER_FREEZE] Buffer freeze thread ended (counter=" + std::to_string(freezeCount) + ")", true);
    }
    
    {
        std::lock_guard<std::recursive_mutex> lock(g_bufferFreezeControlMutex);
        if (!OwnsFreezeGeneration(generation)) return;

        g_freezeInitializedGeneration.store(0, std::memory_order_release);
        if (!IsPracticeContext()) {
            // Outside Practice the fighters are not ours (netplay may own them); never issue cleanup writes.
            g_bufferFreezingActive.store(false, std::memory_order_release);
            g_indexFreezingActive.store(false, std::memory_order_release);
            int expectedPlayer = playerNum;
            (void)g_activeFreezePlayer.compare_exchange_strong(
                expectedPlayer, 0);
            return;
        }
        if (state.length > 0) {
            uintptr_t playerPtr = FreezePlayerPointer(playerNum);
            if (playerPtr) {
                const uint16_t start = state.startIndex;
                const uint16_t len1 = (uint16_t)std::min<uint16_t>(
                    state.length, INPUT_BUFFER_SIZE - start);
                const uint16_t len2 = state.length - len1;
                std::vector<uint8_t> zeros1(len1, 0x00);
                std::vector<uint8_t> zeros2(len2, 0x00);
                if (len1 > 0) {
                    WriteFreezeMemory(playerPtr + INPUT_BUFFER_OFFSET + start,
                                    zeros1.data(), len1);
                }
                if (len2 > 0) {
                    WriteFreezeMemory(playerPtr + INPUT_BUFFER_OFFSET,
                                    zeros2.data(), len2);
                }
                uint16_t curIdx = 0;
                SafeReadMemory(playerPtr + INPUT_BUFFER_INDEX_OFFSET,
                               &curIdx, sizeof(curIdx));
                for (int n = 0; n < 4; ++n) {
                    const uint16_t w = static_cast<uint16_t>(
                        (curIdx + INPUT_BUFFER_SIZE - n) % INPUT_BUFFER_SIZE);
                    const uint8_t neutral = 0;
                    WriteFreezeMemory(playerPtr + INPUT_BUFFER_OFFSET + w,
                                    &neutral, sizeof(neutral));
                }
            }
        }

        g_bufferFreezingActive.store(false, std::memory_order_release);
        g_indexFreezingActive.store(false, std::memory_order_release);
        int expectedPlayer = playerNum;
        (void)g_activeFreezePlayer.compare_exchange_strong(expectedPlayer, 0);
    }
    
    if (detailedLogging.load()) {
        LogOut("[BUFFER_FREEZE] End session P" + std::to_string(playerNum) + " (thread ended)", true);
    }
}

namespace {
DWORD WINAPI BufferThreadMain(void* payload) {
    std::unique_ptr<BufferFreezeSnapshot> state(static_cast<BufferFreezeSnapshot*>(payload));
    const uint64_t generation=state->generation;
    const int player=state->playerNum;
    try {FreezeBufferValuesThread(std::move(*state));}catch(...) {
        bufferWorkerFault.store(true,std::memory_order_release);
        // A dead worker must not keep the input lane.
        std::lock_guard<std::recursive_mutex> lock(g_bufferFreezeControlMutex);
        if(OwnsFreezeGeneration(generation)) {
            g_bufferFreezingActive.store(false,std::memory_order_release);
            g_indexFreezingActive.store(false,std::memory_order_release);
            g_freezeInitializedGeneration.store(0,std::memory_order_release);
            int expected=player;
            (void)g_activeFreezePlayer.compare_exchange_strong(expected,0);
        }
    }
    state.reset();
    Practice::NotifyRetirementProgress();
    return 0;
}
}
uint64_t StartBufferFreezeWorker(int playerNum) {
    std::lock_guard<std::recursive_mutex> lock(g_bufferFreezeControlMutex);
    if (!IsPracticeContext() || bufferWorkClosed.load(std::memory_order_acquire)) return 0;
    auto admission=bufferWorldBound?Practice::TryEnterMonitorWork():Practice::WorkLease{};
    if(bufferWorldBound && (!admission || !SameBufferWorld(admission.Context(),bufferWorld)))return 0;
    (void)ReapBufferThreads();
    if(bufferWorkerFault.load())return 0;
    if(!bufferWake)bufferWake=CreateEventA(nullptr,TRUE,FALSE,nullptr);
    if(!bufferWake)return 0;
    ResetEvent(bufferWake);
    BufferFreezeSnapshot state;
    state.scoped=bufferWorldBound;state.world=bufferWorld;
    state.generation = NextFreezeGeneration();
    state.playerNum = playerNum;
    state.values = g_frozenBufferValues;
    state.startIndex = g_frozenBufferStartIndex;
    state.length = static_cast<uint16_t>(std::min<size_t>(
        g_frozenBufferLength, state.values.size()));
    state.freezeIndex = g_indexFreezingActive.load(std::memory_order_acquire);
    state.indexValue = g_frozenIndexValue;
    state.motionType = g_frozenMotionType;
    state.buttonMask = g_frozenButtonMask;
    state.facingRight = g_lastKnownFacing;

    auto payload=std::make_unique<BufferFreezeSnapshot>(std::move(state));
    bufferThreads.emplace_back(); // Allocate durable ownership before launch.
    auto& record=bufferThreads.back();
    // A failed start owned nothing and wrote nothing, so it stays retryable and is not a fault.
    if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCSTR>(&BufferThreadMain),&record.module)) {
        bufferThreads.pop_back();return 0;
    }
    record.thread=CreateThread(nullptr,0,BufferThreadMain,payload.get(),CREATE_SUSPENDED,nullptr);
    if(!record.thread) {
        FreeLibrary(record.module);bufferThreads.pop_back();return 0;
    }
    payload.release();
    // Publish ownership only once the worker exists; it checks ownership as soon as it runs.
    g_freezeInitializedGeneration.store(0, std::memory_order_release);
    g_activeFreezePlayer.store(playerNum, std::memory_order_release);
    g_bufferFreezingActive.store(true, std::memory_order_release);
    ResumeThread(record.thread);
    return g_bufferFreezeGeneration.load(std::memory_order_acquire);
}

bool CaptureAndFreezeBuffer(int playerNum, uint16_t startIndex, uint16_t length, int motionType, int buttonMask) {
    std::lock_guard<std::recursive_mutex> p2ControlLock(g_p2ControlMutex);
    std::unique_lock<std::recursive_mutex> controlLock(g_bufferFreezeControlMutex);
    auto work=bufferWorldBound?Practice::TryEnterMonitorWork():Practice::WorkLease{};
    if(bufferWorldBound && (!work || !SameBufferWorld(work.Context(),bufferWorld) || bufferWorkClosed.load()))return false;
    if (TutorialBufferFreezeLeaseActive()) return false;
    // CRITICAL: Never run during online mode
    if (g_onlineModeActive.load()) return false;

    StopBufferFreezing();
    
    g_frozenMotionType = motionType;
    g_frozenButtonMask = buttonMask;
    g_lastKnownFacing = FreezeFacing(playerNum);
    
    uintptr_t playerPtr = FreezePlayerPointer(playerNum);
    if (!playerPtr) {
        LogOut("[INPUT_BUFFER] Cannot get player pointer", true);
        return false;
    }
    
    if (length > INPUT_BUFFER_SIZE) {
        LogOut("[INPUT_BUFFER] Length too large, capping at " + std::to_string(INPUT_BUFFER_SIZE), true);
        length = INPUT_BUFFER_SIZE;
    }
    startIndex = static_cast<uint16_t>(startIndex % INPUT_BUFFER_SIZE);
    
    uint16_t currentIndex = 0;
    if (!SafeReadMemory(playerPtr + INPUT_BUFFER_INDEX_OFFSET, &currentIndex, sizeof(uint16_t))) {
        LogOut("[INPUT_BUFFER] Failed to read buffer index", true);
        return false;
    }
    
    g_frozenBufferValues.resize(length);
    g_frozenBufferStartIndex = startIndex;
    g_frozenBufferLength = length;
    
    bool readSuccess = true;
    for (size_t i = 0; i < static_cast<size_t>(length); i++) {
        uint16_t readIdx = (startIndex + i) % INPUT_BUFFER_SIZE;
        if (!SafeReadMemory(playerPtr + INPUT_BUFFER_OFFSET + readIdx, &g_frozenBufferValues[i], sizeof(uint8_t))) {
            readSuccess = false;
            LogOut("[INPUT_BUFFER] Failed to read buffer value at " + std::to_string(readIdx), true);
        }
    }
    
    if (!readSuccess) {
        LogOut("[INPUT_BUFFER] Failed to read some buffer values, freezing may be inconsistent", true);
    }
    
    if (detailedLogging.load()) {
        std::stringstream ss;
        ss << "[INPUT_BUFFER] Captured values: ";
        for (size_t i = 0; i < static_cast<size_t>(length); i++) {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(g_frozenBufferValues[i]);
            if (i + 1 < static_cast<size_t>(length)) ss << " ";
        }
        LogOut(ss.str(), true);
    }
    
    const uint64_t expectedGeneration = StartBufferFreezeWorker(playerNum);
    if (expectedGeneration == 0) return false;
    // The worker publishes its ack under this mutex; unlock before waiting.
    controlLock.unlock();
    
    // CRITICAL: wait for worker init before returning; callers check g_bufferFreezingActive immediately.
    auto startWait = std::chrono::steady_clock::now();
    int waitIterations = 0;
    while (g_freezeInitializedGeneration.load(std::memory_order_acquire) !=
           expectedGeneration) {
        if (!OwnsFreezeGeneration(expectedGeneration) ||
            !g_bufferFreezingActive.load(std::memory_order_acquire) ||
            g_activeFreezePlayer.load(std::memory_order_acquire) != playerNum) {
            LogOut("[INPUT_BUFFER][WARN] Freeze worker lost ownership before startup acknowledgement", true);
            return false;
        }
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startWait).count();
        if (elapsed > 10) {
            LogOut("[INPUT_BUFFER][WARN] Thread startup timeout after " + std::to_string(elapsed) + 
                  "ms (waited " + std::to_string(waitIterations) + " iterations), active=" + 
                  std::to_string(g_bufferFreezingActive.load()), true);
            StopBufferFreezingIgnoringTutorialLease();
            return false;
        }
        if(bufferWorldBound && !work.IsCurrent())return false;
        waitIterations++;
        if(WaitForSingleObject(bufferWake,1)==WAIT_FAILED)return false;
    }
    
    if (detailedLogging.load() && waitIterations > 0) {
        LogOut("[INPUT_BUFFER] Thread initialization confirmed after " + std::to_string(waitIterations) + 
              " wait iterations", true);
    }
    
    LogOut("[INPUT_BUFFER] Buffer freezing activated for P" + std::to_string(playerNum) + 
           " starting at index " + std::to_string(startIndex) +
           " with length " + std::to_string(length) +
           ", owner=P" + std::to_string(g_activeFreezePlayer.load()), true);
    return true;
}

bool FreezeBufferIndex(int playerNum, uint16_t indexValue) {
    std::lock_guard<std::recursive_mutex> p2ControlLock(g_p2ControlMutex);
    std::lock_guard<std::recursive_mutex> controlLock(g_bufferFreezeControlMutex);
    auto work=bufferWorldBound?Practice::TryEnterMonitorWork():Practice::WorkLease{};
    if(bufferWorldBound && (!work || !SameBufferWorld(work.Context(),bufferWorld) || bufferWorkClosed.load()))return false;
    if (TutorialBufferFreezeLeaseActive()) return false;
    if (g_onlineModeActive.load(std::memory_order_acquire)) return false;
    uintptr_t playerPtr = FreezePlayerPointer(playerNum);
    if (!playerPtr) {
        LogOut("[INPUT_BUFFER] Cannot get player pointer", true);
        return false;
    }
    
    g_frozenIndexValue = indexValue;
    g_indexFreezingActive = true;
    
    LogOut("[INPUT_BUFFER] Buffer index freezing activated, locked to " + std::to_string(indexValue), true);
    return true;
}

void StopBufferFreezingIgnoringTutorialLease() {
    std::lock_guard<std::recursive_mutex> p2ControlLock(g_p2ControlMutex);
    std::lock_guard<std::recursive_mutex> controlLock(g_bufferFreezeControlMutex);
    if (g_bufferFreezingActive) {
        using clock = std::chrono::steady_clock;
        auto tStart = clock::now();
        // Invalidate the generation first so a detached predecessor skips writes and cleanup.
        (void)NextFreezeGeneration();
        if(bufferWake)SetEvent(bufferWake);
        g_bufferFreezingActive.store(false, std::memory_order_release);
        g_indexFreezingActive.store(false, std::memory_order_release);
        g_freezeInitializedGeneration.store(0, std::memory_order_release);
        int owner = g_activeFreezePlayer.exchange(0);
        if (owner != 0) {
            LogOut("[INPUT_BUFFER] StopBufferFreezing() called (owner=P" + std::to_string(owner) + ")", true);
        } else {
            LogOut("[INPUT_BUFFER] StopBufferFreezing() called (no active owner)", true);
        }
        
        if (!IsPracticeContext()) {
            LogOut("[INPUT_BUFFER] Freeze bookkeeping retired without memory writes outside Practice", true);
            return;
        }

    auto tWaitStart = clock::now();
    (void)ReapBufferThreads();
    auto tWaitEnd = clock::now();
        
        // Neutralize only the freeze owner's buffer; clearing both players erases the other side's inputs.
        auto tCleanStart = clock::now();
        if (owner == 1 || owner == 2) {
            uintptr_t playerPtr = FreezePlayerPointer(owner);
            if (playerPtr) {
                uint16_t currentIndex = 0;
                if (SafeReadMemory(playerPtr + INPUT_BUFFER_INDEX_OFFSET,
                                   &currentIndex, sizeof(currentIndex))) {
                    const uint8_t neutral = 0;
                    for (int i = 0; i < 8; ++i) {
                        int w = static_cast<int>(currentIndex) - i;
                        w %= static_cast<int>(INPUT_BUFFER_SIZE);
                        if (w < 0) w += static_cast<int>(INPUT_BUFFER_SIZE);
                        const uint16_t writeIndex = static_cast<uint16_t>(w);
                        WriteFreezeMemory(playerPtr + INPUT_BUFFER_OFFSET + writeIndex,
                                        &neutral, sizeof(neutral));
                    }
                }
            }
        }
        auto tCleanEnd = clock::now();

        auto waitMs = std::chrono::duration_cast<std::chrono::milliseconds>(tWaitEnd - tWaitStart).count();
        auto cleanMs = std::chrono::duration_cast<std::chrono::milliseconds>(tCleanEnd - tCleanStart).count();
        auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(tCleanEnd - tStart).count();
        LogOut(std::string("[INPUT_BUFFER] Buffer freezing stopped ") +
               "wait=" + std::to_string(waitMs) + "ms " +
               "clean=" + std::to_string(cleanMs) + "ms " +
               "total=" + std::to_string(totalMs) + "ms", true);
    }
}

void StopBufferFreezing() {
    std::lock_guard<std::recursive_mutex> p2ControlLock(g_p2ControlMutex);
    std::lock_guard<std::recursive_mutex> controlLock(g_bufferFreezeControlMutex);
    if (TutorialBufferFreezeLeaseActive()) {
        LogOut("[INPUT_BUFFER] Stop rejected: tutorial dummy owns the freeze engine", true);
        return;
    }
    StopBufferFreezingIgnoringTutorialLease();
}


bool BindBufferFreezeWorld(const EfzTmEntryV1& world) {
    std::lock_guard<std::recursive_mutex> lock(g_bufferFreezeControlMutex);
    if(world.size!=sizeof(world) || world.abiVersion!=EFZ_TM_LIFECYCLE_ABI || !world.id.battleWorld ||
       !world.id.timeline || !world.player1 || !world.player2 || world.nativeThreadId!=GetCurrentThreadId())return false;
    if(bufferWorldBound)return SameBufferWorld(bufferWorld,world) && !bufferWorkClosed.load();
    if(!ReapBufferThreads() || g_bufferFreezingActive.load() || bufferWorkerFault.load())return false;
    bufferWorld=world;bufferWorldBound=true;bufferWorkClosed.store(false,std::memory_order_release);
    return true;
}
void CancelBufferFreezeWork() {
    std::lock_guard<std::recursive_mutex> lock(g_bufferFreezeControlMutex);
    if(!bufferWorldBound)return;
    bufferWorkClosed.store(true,std::memory_order_release);
    (void)NextFreezeGeneration();
    g_bufferFreezingActive.store(false,std::memory_order_release);
    g_indexFreezingActive.store(false,std::memory_order_release);
    g_freezeInitializedGeneration.store(0,std::memory_order_release);
    if(bufferWake)SetEvent(bufferWake);
}
uint32_t RetireBufferFreezeWorld(const EfzTmEntryV1& heldWorld) {
    std::lock_guard<std::recursive_mutex> lock(g_bufferFreezeControlMutex);
    if(bufferWorkerFault.load())return EFZ_TM_FAULTED;
    if(!bufferWorldBound)return bufferThreads.empty()?EFZ_TM_READY:EFZ_TM_PENDING;
    if(!SameBufferWorld(bufferWorld,heldWorld) || heldWorld.nativeThreadId!=GetCurrentThreadId())return EFZ_TM_FAULTED;
    if(!bufferWorkClosed.load())return EFZ_TM_PENDING;
    // Scoped workers release this mutex and all native memory before their lease; joining here cannot wait on a lock we hold.
    if(!ReapBufferThreads(true))return bufferWorkerFault.load()?EFZ_TM_FAULTED:EFZ_TM_PENDING;
    bool restored=true;
    for(int player=0;player<2;++player) {
        auto& record=bufferWrites[player];
        const uintptr_t base=player?heldWorld.player2:heldWorld.player1;
        for(size_t index=0;index<INPUT_BUFFER_SIZE;++index)if(record.owned[index]) {
            uint8_t current=0;
            const uintptr_t address=base+INPUT_BUFFER_OFFSET+index;
            if(!SafeReadMemory(address,&current,1)){restored=false;continue;}
            // Return the preimage only while the byte still matches our last written value.
            if(current==record.last[index] && !CopyBufferData(address,&record.before[index],1)){restored=false;continue;}
            record.owned[index]=false;
        }
        if(record.indexOwned) {
            uint16_t current=0;
            const uintptr_t address=base+INPUT_BUFFER_INDEX_OFFSET;
            if(!SafeReadMemory(address,&current,sizeof(current)) ||
               (current==record.indexLast && !CopyBufferData(address,&record.indexBefore,sizeof(current))))restored=false;
            else record.indexOwned=false;
        }
    }
    if(!restored)return EFZ_TM_PENDING;
    if(bufferWake && !CloseHandle(bufferWake))return EFZ_TM_PENDING;
    bufferWake=nullptr;bufferWorldBound=false;bufferWorld={};
    bufferWrites[0]={};bufferWrites[1]={};
    g_activeFreezePlayer.store(0);g_frozenBufferValues.clear();g_frozenBufferLength=0;
    return bufferWorkerFault.load()?EFZ_TM_FAULTED:EFZ_TM_READY;
}
