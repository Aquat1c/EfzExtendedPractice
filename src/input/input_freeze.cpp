#include "../include/input/input_freeze.h"
#include "../include/core/globals.h"
#include "../include/input/input_core.h"
#include "../include/core/memory.h"  
#include "../include/core/logger.h"
#include "../include/core/constants.h"
#include "../include/input/input_buffer.h"
#include "../include/input/auto_action_motion_transaction.h"
#include "../include/input/immediate_input.h"
#include "../include/input/injection_control.h"
#include "../include/input/input_hook.h"
#include "../include/utils/utilities.h"
#include "../include/game/game_state.h"
#include "../include/game/macro_controller.h"
#include "../include/input/motion_system.h"
#include "../include/input/input_motion.h" 
#include "../include/input/scoped_input_reservation.h"
#include "../include/game/frame_monitor.h"
#include "../include/game/auto_action.h"
#include "../include/utils/xp_compat.h"
// These functions are implemented in input_buffer.cpp
extern bool CaptureAndFreezeBuffer(int playerNum, uint16_t startIndex, uint16_t length, int motionType, int buttonMask);
extern bool FreezeBufferIndex(int playerNum, uint16_t indexValue);
extern void StopBufferFreezing(void);
extern void StopBufferFreezingIgnoringTutorialLease(void);
extern int g_frozenMotionType;
extern int g_frozenButtonMask;
extern bool g_lastKnownFacing;

namespace {
std::atomic<uint64_t> g_tutorialFreezeToken{0};
std::atomic<uint64_t> g_nextTutorialFreezeToken{1};
struct FreezeSessionState {
    std::atomic<bool> active{false};
    std::atomic<bool> threadRunning{false};
    uint16_t originalIndex{0};
    bool originalIndexValid{false};
};
FreezeSessionState g_freezeSession[3]; // 1,2 (ignore 0)

// Every freeze entry point gates here before it writes the pattern.
bool BufferFreezeInputLaneBusy(int playerNum) {
    if (playerNum < 1 || playerNum > 2 || !IsPracticeContext()) return true;
    return IsScopedInputReserved(playerNum) ||
           IsAutoActionMotionTransactionActive(playerNum) ||
           IsAutoActionNormalPulseActive(playerNum) ||
           ImmediateInput::TutorialLeaseActive(playerNum) ||
           TutorialMotionQueueLeaseActive(playerNum) ||
           g_manualInputOverride[playerNum].load(std::memory_order_acquire) ||
           g_pollOverrideActive[playerNum].load(std::memory_order_acquire) ||
           ImmediateInput::GetCurrentDesired(playerNum) != 0 ||
           ImmediateInput::GetRemainingTicks(playerNum) != 0 ||
           GetMotionQueueSnapshot(playerNum).active ||
           (MacroController::IsExclusivePlayback() &&
            MacroController::GetPlaybackPlayer() == playerNum);
}

// A refused worker must not leave the written pattern or the index lock behind.
bool StartFreezeOrRollback(int playerNum, uintptr_t playerPtr, uint16_t writtenLength) {
    if (StartBufferFreezeWorker(playerNum) != 0) return true;
    g_indexFreezingActive = false;
    g_frozenBufferLength = 0;
    g_frozenBufferValues.clear();
    if (IsPracticeContext()) {
        std::vector<uint8_t> zeros(writtenLength, 0x00);
        SafeWriteMemory(playerPtr + INPUT_BUFFER_OFFSET, zeros.data(), writtenLength);
    }
    LogOut("[BUFFER_FREEZE] Freeze worker refused; pattern rolled back P" + std::to_string(playerNum), true);
    return false;
}
}
bool FreezePerfectDragonPunch(int playerNum) {
    std::lock_guard<std::recursive_mutex> p2ControlLock(g_p2ControlMutex);
    std::lock_guard<std::recursive_mutex> controlLock(g_bufferFreezeControlMutex);
    if (g_onlineModeActive.load(std::memory_order_acquire)) return false;
    if (BufferFreezeInputLaneBusy(playerNum)) return false;
    if (TutorialBufferFreezeLeaseActive()) return false;
    StopBufferFreezing();
    
    if (detailedLogging.load()) {
        LogOut("[BUFFER_FREEZE] Starting Dragon Punch buffer freeze for P" + std::to_string(playerNum), true);
    }
    
    std::vector<uint8_t> dpMotion = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x02, 0x04, 
        0x04, 0x04, 0x26, 0x06, 0x06, 0x06, 0x00, 0x00
    };
    
    if (detailedLogging.load()) {
        std::stringstream ss;
        ss << "[BUFFER_FREEZE] DP motion buffer values: ";
        for (size_t i = 0; i < dpMotion.size(); i++) {
            if (dpMotion[i] != 0) {
                ss << std::hex << std::setw(2) << std::setfill('0') 
                   << static_cast<int>(dpMotion[i]) << "(" << DecodeInputMask(dpMotion[i]) << ") ";
            } else if (i > 0 && dpMotion[i-1] != 0) {
                ss << "00 ";
            }
        }
        LogOut(ss.str(), true);
    }
    
    g_frozenBufferValues = dpMotion;
    g_frozenBufferLength = static_cast<uint16_t>(dpMotion.size());
    g_frozenBufferStartIndex = 128;
    
    g_frozenIndexValue = (g_frozenBufferStartIndex + 20) % INPUT_BUFFER_SIZE;
    g_indexFreezingActive = true;
    
    StartBufferFreezeWorker(playerNum);
    
    if (detailedLogging.load()) {
        LogOut("[BUFFER_FREEZE] Perfect Dragon Punch motion freezing activated at index " + 
               std::to_string(g_frozenIndexValue), true);
    }
    return true;
}

bool FreezePerfectDragonPunchEnhanced(int playerNum) {
    std::lock_guard<std::recursive_mutex> p2ControlLock(g_p2ControlMutex);
    std::lock_guard<std::recursive_mutex> controlLock(g_bufferFreezeControlMutex);
    if (g_onlineModeActive.load(std::memory_order_acquire)) return false;
    if (BufferFreezeInputLaneBusy(playerNum)) return false;
    if (TutorialBufferFreezeLeaseActive()) return false;
    StopBufferFreezing();
    
    if (detailedLogging.load()) {
        LogOut("[BUFFER_DEBUG] Starting enhanced DP buffer freeze for P" + std::to_string(playerNum), true);
    }
    
    uintptr_t playerPtr = GetPlayerPointer(playerNum);
    if (!playerPtr) {
        LogOut("[BUFFER_DEBUG] Failed to get player pointer", true);
        return false;
    }
    
    uint16_t currentIndex = 0;
    if (!SafeReadMemory(playerPtr + INPUT_BUFFER_INDEX_OFFSET, &currentIndex, sizeof(uint16_t))) {
        LogOut("[BUFFER_DEBUG] Failed to read current buffer index", true);
        return false;
    }
    
    if (detailedLogging.load()) {
        DumpInputBuffer(playerNum);
    }
    
    std::vector<uint8_t> dpMotion = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x02, 0x04, 
        0x04, 0x04, 0x26, 0x06, 0x06, 0x00, 0x00, 0x00
    };
    
    g_frozenBufferStartIndex = (currentIndex + INPUT_BUFFER_SIZE - dpMotion.size()) % INPUT_BUFFER_SIZE;
    g_frozenBufferValues = dpMotion;
    g_frozenBufferLength = static_cast<uint16_t>(dpMotion.size());
    
    g_frozenIndexValue = (g_frozenBufferStartIndex + 20) % INPUT_BUFFER_SIZE;
    g_indexFreezingActive = true;
    
    if (detailedLogging.load()) {
        LogOut("[BUFFER_DEBUG] Current buffer index: " + std::to_string(currentIndex), true);
        LogOut("[BUFFER_DEBUG] Setting buffer start at: " + std::to_string(g_frozenBufferStartIndex), true);
        LogOut("[BUFFER_DEBUG] Setting frozen index to: " + std::to_string(g_frozenIndexValue), true);
    }
    
    if (detailedLogging.load()) {
        std::stringstream ss;
        ss << "[BUFFER_DEBUG] DP motion sequence: ";
        for (size_t i = 0; i < dpMotion.size(); i++) {
            ss << std::hex << std::setw(2) << std::setfill('0') 
               << static_cast<int>(dpMotion[i]) << "(" << DecodeInputMask(dpMotion[i]) << ") ";
        }
        LogOut(ss.str(), true);
    }
    
    StartBufferFreezeWorker(playerNum);
    
    if (detailedLogging.load()) {
        LogOut("[BUFFER_DEBUG] Enhanced DP buffer freeze activated for P" + 
               std::to_string(playerNum), true);
    }
    return true;
}

bool ComboFreezeDP(int playerNum) {
    std::lock_guard<std::recursive_mutex> p2ControlLock(g_p2ControlMutex);
    std::lock_guard<std::recursive_mutex> controlLock(g_bufferFreezeControlMutex);
    if (g_onlineModeActive.load(std::memory_order_acquire)) return false;
    if (BufferFreezeInputLaneBusy(playerNum)) return false;
    if (TutorialBufferFreezeLeaseActive()) return false;
    StopBufferFreezing();
    
    if (detailedLogging.load()) {
        LogOut("[BUFFER_COMBO] Starting CheatEngine-style DP freeze for P" + std::to_string(playerNum), true);
    }
    
    uintptr_t playerPtr = GetPlayerPointer(playerNum);
    if (!playerPtr) {
        LogOut("[BUFFER_COMBO] Failed to get player pointer", true);
        return false;
    }

    uint16_t currentIndex = 0;
    SafeReadMemory(playerPtr + INPUT_BUFFER_INDEX_OFFSET, &currentIndex, sizeof(uint16_t));
    if (detailedLogging.load()) {
        LogOut("[BUFFER_COMBO] Initial buffer index: " + std::to_string(currentIndex), true);
    }
    
    std::vector<uint8_t> dpMotion = {
        0x02, 0x02, 0x02,  // LEFT x3
        0x04, 0x04, 0x04,  // DOWN x3
        0x26,              // DOWN+LEFT+BUTTON
        0x06, 0x06, 0x06, 0x06, // DOWN+LEFT x4
        0x26, 0x26, 0x26, 0x26  // DOWN+LEFT+BUTTON x4
    };
    
    g_frozenIndexValue = 149;
    g_frozenBufferValues = dpMotion;
    g_frozenBufferStartIndex = 144;
    g_frozenBufferLength = static_cast<uint16_t>(dpMotion.size());
    
    g_indexFreezingActive = true;
    StartBufferFreezeWorker(playerNum);
    
    if (detailedLogging.load()) { LogOut("[BUFFER_COMBO] DP buffer pattern freeze activated", true); }
    return true;
}

static bool FreezeBufferForMotionImpl(int playerNum, int motionType, int buttonMask,
                                      int optimalIndex) {
    if (g_onlineModeActive.load(std::memory_order_acquire)) return false;
    StopBufferFreezingIgnoringTutorialLease();
    
    uintptr_t playerPtr = GetPlayerPointer(playerNum);
    if (!playerPtr) {
        LogOut("[BUFFER_FREEZE] Invalid player pointer", true);
        return false;
    }
    
    bool facingRight = GetPlayerFacingDirection(playerNum);
    std::string motionLabel = GetMotionTypeName(motionType);
    if (detailedLogging.load()) {
        std::stringstream ss;
        ss << "[BUFFER_FREEZE] Starting buffer freeze for motion " << motionLabel 
           << " (P" << playerNum << ") facing=" << (facingRight ? "right" : "left")
           << " btnMask=0x" << std::hex << buttonMask << std::dec;
        LogOut(ss.str(), true);
        LogOut("[BUFFER_FREEZE] Begin session (" + motionLabel + ") P" + std::to_string(playerNum), true);
    }
    
    uint8_t fwd = facingRight ? GAME_INPUT_RIGHT : GAME_INPUT_LEFT;
    uint8_t back = facingRight ? GAME_INPUT_LEFT : GAME_INPUT_RIGHT;
    uint8_t down = GAME_INPUT_DOWN;
    uint8_t downFwd = down | fwd;
    uint8_t downBack = down | back;
    
    std::vector<uint8_t> pattern;
    
    switch (motionType) {
        case MOTION_623A: case MOTION_623B: case MOTION_623C: case MOTION_623D: {
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
        
        case MOTION_214A: case MOTION_214B: case MOTION_214C: case MOTION_214D: {
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
        
        case MOTION_236A: case MOTION_236B: case MOTION_236C: case MOTION_236D: {
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
        case MOTION_421A: case MOTION_421B: case MOTION_421C: case MOTION_421D: {
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
        case MOTION_41236A: case MOTION_41236B: case MOTION_41236C: case MOTION_41236D: {
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
        case MOTION_214214A: case MOTION_214214B: case MOTION_214214C: case MOTION_214214D: {
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
        case MOTION_236236A: case MOTION_236236B: case MOTION_236236C: case MOTION_236236D: {
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
        case MOTION_641236A: case MOTION_641236B: case MOTION_641236C: case MOTION_641236D: {
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
        case MOTION_412A: case MOTION_412B: case MOTION_412C: case MOTION_412D: {
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
        case MOTION_22A: case MOTION_22B: case MOTION_22C: case MOTION_22D: {
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
        case MOTION_2141236A: case MOTION_2141236B: case MOTION_2141236C: case MOTION_2141236D: {
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
        case MOTION_463214A: case MOTION_463214B: case MOTION_463214C: case MOTION_463214D: {
            // 463214: Left, Right, Down-Right, Down, Down-Left, Left + Button
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
        case MOTION_4123641236A: case MOTION_4123641236B: case MOTION_4123641236C: case MOTION_4123641236D: {
            // 41236 41236: Back, Down-Back, Down, Down-Forward, Forward x2 + Button only at final Forward
            pattern = {
                0x00, 0x00,
                back, back, downBack, downBack, down, down, downFwd, downFwd, fwd, fwd, // first 41236
                back, back, downBack, downBack, down, down, downFwd, downFwd, fwd,      // second 41236 up to final forward
                (uint8_t)(fwd | buttonMask),
                (uint8_t)(fwd | buttonMask),
            };
            break;
        }
        case MOTION_6321463214A: case MOTION_6321463214B: case MOTION_6321463214C: case MOTION_6321463214D: {
            // 6321463214: Fwd, Down-Fwd, Down, Down-Back, Back, Fwd, Down-Fwd, Down, Down-Back, Back + Button
            // Map: 6=fwd, 3=downFwd, 2=down, 1=downBack, 4=back
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

        case MOTION_FORWARD_DASH: {
            // Forward Dash: Forward, Neutral, Forward
            pattern = {
                0x00, 0x00,
                fwd, fwd,
                0x00,
                fwd, fwd,
            };
            break;
        }
        case MOTION_BACK_DASH: {
            // Back Dash: Back, Neutral, Back
            pattern = {
                0x00, 0x00,
                back, back,
                0x00,
                back, back,
            };
            break;
        }
        
        default:
            LogOut("[BUFFER_FREEZE] Unsupported motion type: " + std::to_string(motionType), true);
            return false;
    }
    
    if (detailedLogging.load()) {
        std::stringstream ss;
        ss << "[BUFFER_FREEZE] Pattern values for " << motionLabel << ": ";
        for (size_t i = 0; i < pattern.size(); i++) {
            ss << DecodeInputMask(pattern[i]) << " ";
        }
        LogOut(ss.str(), true);
    }
    
    const uint16_t startIndex = 0;
    
    const int clearPadding = 4;
    const uint16_t clearLength = static_cast<uint16_t>(pattern.size() + clearPadding);
    if (clearLength > 0) {
        std::vector<uint8_t> zeros(clearLength, 0x00);
        SafeWriteMemory(playerPtr + INPUT_BUFFER_OFFSET + startIndex, zeros.data(), clearLength);
    }
    if (!pattern.empty()) {
        SafeWriteMemory(playerPtr + INPUT_BUFFER_OFFSET + startIndex, pattern.data(), static_cast<uint32_t>(pattern.size()));
    }
    
    g_frozenBufferValues = pattern;
    g_frozenBufferStartIndex = startIndex;
    g_frozenBufferLength = static_cast<uint16_t>(pattern.size());
    
    g_frozenMotionType = motionType;
    g_frozenButtonMask = buttonMask;
    g_lastKnownFacing = facingRight;
    
    // Point the index at pattern end - 1 so the final button press is read.
    g_frozenIndexValue = (startIndex + g_frozenBufferLength - 1) % INPUT_BUFFER_SIZE;
    g_indexFreezingActive = true;

    if (!StartFreezeOrRollback(playerNum, playerPtr, clearLength)) return false;

    if (detailedLogging.load()) {
        LogOut("[BUFFER_FREEZE] Buffer freeze for " + motionLabel + " activated at index " + 
               std::to_string(g_frozenIndexValue), true);
    }
    return true;
}

bool FreezeBufferForMotion(int playerNum, int motionType, int buttonMask, int optimalIndex) {
    std::lock_guard<std::recursive_mutex> p2ControlLock(g_p2ControlMutex);
    std::lock_guard<std::recursive_mutex> controlLock(g_bufferFreezeControlMutex);
    if (g_onlineModeActive.load(std::memory_order_acquire)) return false;
    if (BufferFreezeInputLaneBusy(playerNum)) return false;
    if (g_tutorialFreezeToken.load(std::memory_order_acquire) != 0) {
        LogOut("[BUFFER_FREEZE] Start rejected: tutorial dummy owns the freeze engine", true);
        return false;
    }
    return FreezeBufferForMotionImpl(playerNum, motionType, buttonMask, optimalIndex);
}

bool AcquireTutorialBufferFreeze(uint64_t& tokenOut) {
    std::lock_guard<std::recursive_mutex> p2ControlLock(g_p2ControlMutex);
    std::lock_guard<std::recursive_mutex> controlLock(g_bufferFreezeControlMutex);
    tokenOut = 0;
    if (g_onlineModeActive.load(std::memory_order_acquire)) return false;
    if (BufferFreezeInputLaneBusy(1) || BufferFreezeInputLaneBusy(2)) return false;
    if (g_bufferFreezingActive.load(std::memory_order_acquire) ||
        g_freezeSession[1].active.load(std::memory_order_acquire) ||
        g_freezeSession[2].active.load(std::memory_order_acquire)) return false;
    uint64_t expected = 0;
    uint64_t token = g_nextTutorialFreezeToken.fetch_add(1, std::memory_order_relaxed);
    if (token == 0) token = g_nextTutorialFreezeToken.fetch_add(1, std::memory_order_relaxed);
    if (!g_tutorialFreezeToken.compare_exchange_strong(
            expected, token, std::memory_order_acq_rel)) return false;
    tokenOut = token;
    return true;
}

bool TutorialBufferFreezeLeaseActive() {
    return g_tutorialFreezeToken.load(std::memory_order_acquire) != 0;
}

bool FreezeBufferForTutorial(uint64_t token, int playerNum, int motionType,
                             int buttonMask, int optimalIndex) {
    std::lock_guard<std::recursive_mutex> p2ControlLock(g_p2ControlMutex);
    std::lock_guard<std::recursive_mutex> controlLock(g_bufferFreezeControlMutex);
    if (g_onlineModeActive.load(std::memory_order_acquire)) return false;
    if (token == 0 ||
        g_tutorialFreezeToken.load(std::memory_order_acquire) != token) return false;
    if (BufferFreezeInputLaneBusy(playerNum)) return false;
    return FreezeBufferForMotionImpl(playerNum, motionType, buttonMask, optimalIndex);
}

void StopTutorialBufferFreeze(uint64_t token) {
    std::lock_guard<std::recursive_mutex> p2ControlLock(g_p2ControlMutex);
    std::lock_guard<std::recursive_mutex> controlLock(g_bufferFreezeControlMutex);
    if (token == 0 ||
        g_tutorialFreezeToken.load(std::memory_order_acquire) != token) return;
    StopBufferFreezingIgnoringTutorialLease();
}

void ReleaseTutorialBufferFreeze(uint64_t token) {
    std::lock_guard<std::recursive_mutex> p2ControlLock(g_p2ControlMutex);
    std::lock_guard<std::recursive_mutex> controlLock(g_bufferFreezeControlMutex);
    if (token == 0 ||
        g_tutorialFreezeToken.load(std::memory_order_acquire) != token) return;
    StopBufferFreezingIgnoringTutorialLease();
    uint64_t expected = token;
    (void)g_tutorialFreezeToken.compare_exchange_strong(
        expected, 0, std::memory_order_acq_rel);
}

void ClearPlayerInputBuffer(int playerNum) {
    std::lock_guard<std::recursive_mutex> p2ControlLock(g_p2ControlMutex);
    std::lock_guard<std::recursive_mutex> freezeLock(g_bufferFreezeControlMutex);
    if (g_onlineModeActive.load(std::memory_order_acquire)) return;
    uintptr_t playerPtr = GetPlayerPointer(playerNum);
    if (!playerPtr) return;
    std::vector<uint8_t> zeros(INPUT_BUFFER_SIZE, 0x00);
    SafeWriteMemory(playerPtr + INPUT_BUFFER_OFFSET, zeros.data(), INPUT_BUFFER_SIZE);
    uint16_t idxZero = 0;
    SafeWriteMemory(playerPtr + INPUT_BUFFER_INDEX_OFFSET, &idxZero, sizeof(uint16_t));
    LogOut(std::string("[BUFFER_FREEZE] Cleared full buffer & index for P") + std::to_string(playerNum), true);
}

void BeginBufferFreezeSession(int playerNum, std::string_view label) {
    std::lock_guard<std::recursive_mutex> p2ControlLock(g_p2ControlMutex);
    std::lock_guard<std::recursive_mutex> controlLock(g_bufferFreezeControlMutex);
    if (g_onlineModeActive.load(std::memory_order_acquire)) return;
    if (BufferFreezeInputLaneBusy(playerNum)) {
        LogOut("[BUFFER_FREEZE] Session start rejected: another input owner is active", true);
        return;
    }
    if (TutorialBufferFreezeLeaseActive()) {
        LogOut("[BUFFER_FREEZE] Session start rejected: tutorial dummy owns the freeze engine", true);
        return;
    }
    StopBufferFreezing();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    auto &s = g_freezeSession[playerNum];
    s.active.store(true);
    s.threadRunning.store(false);
    s.originalIndexValid = false;
    uintptr_t playerPtr = GetPlayerPointer(playerNum);
    if (playerPtr) {
        uint16_t curIdx = 0;
        if (SafeReadMemory(playerPtr + INPUT_BUFFER_INDEX_OFFSET, &curIdx, sizeof(uint16_t))) {
            s.originalIndex = curIdx;
            s.originalIndexValid = true;
        }
    }
    LogOut(std::string("[BUFFER_FREEZE] Begin session (") + (label.empty() ? "" : std::string(label)) +
           ") P" + std::to_string(playerNum), true);
}

void EndBufferFreezeSession(int playerNum, const char* reason, bool clearGlobals) {
    std::lock_guard<std::recursive_mutex> p2ControlLock(g_p2ControlMutex);
    std::lock_guard<std::recursive_mutex> controlLock(g_bufferFreezeControlMutex);
    if (TutorialBufferFreezeLeaseActive()) return;
    auto &s = g_freezeSession[playerNum];
    if (!s.active.load()) return;
    if (g_onlineModeActive.load(std::memory_order_acquire)) {
        s.active.store(false, std::memory_order_release);
        s.threadRunning.store(false, std::memory_order_release);
        s.originalIndexValid = false;
        if (clearGlobals) {
            g_frozenBufferLength = 0;
            g_frozenBufferValues.clear();
        }
        return;
    }

    using clock = std::chrono::steady_clock;
    auto tStart = clock::now();

    StopBufferFreezingIgnoringTutorialLease();

    auto tWaitStart = clock::now();
    for (int i=0; i<20 && s.threadRunning.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    auto tWaitEnd = clock::now();

    // Neutralize only the written region and a small tail; a full clear causes a frame hitch.
    auto tCleanStart = clock::now();
    uintptr_t playerPtr = GetPlayerPointer(playerNum);
    if (playerPtr) {
        if (g_frozenBufferLength > 0) {
            uint8_t zero = 0x00;
            for (uint16_t i = 0; i < g_frozenBufferLength; ++i) {
                uint16_t idx = (g_frozenBufferStartIndex + i) % INPUT_BUFFER_SIZE;
                SafeWriteMemory(playerPtr + INPUT_BUFFER_OFFSET + idx, &zero, sizeof(uint8_t));
            }
        }

        uint16_t curIdx = 0;
        if (SafeReadMemory(playerPtr + INPUT_BUFFER_INDEX_OFFSET, &curIdx, sizeof(uint16_t))) {
            uint8_t zero = 0x00;
            for (int i = 0; i < 8; ++i) {
                int w = static_cast<int>(curIdx) - i;
                w %= static_cast<int>(INPUT_BUFFER_SIZE);
                if (w < 0) w += static_cast<int>(INPUT_BUFFER_SIZE);
                uint16_t idx = static_cast<uint16_t>(w);
                SafeWriteMemory(playerPtr + INPUT_BUFFER_OFFSET + idx, &zero, sizeof(uint8_t));
            }
        }

        if (s.originalIndexValid) {
            SafeWriteMemory(playerPtr + INPUT_BUFFER_INDEX_OFFSET, &s.originalIndex, sizeof(uint16_t));
        }
    }
    auto tCleanEnd = clock::now();

    s.active.store(false);
    s.threadRunning.store(false);

    if (clearGlobals) {
        g_frozenBufferLength = 0;
        g_frozenBufferValues.clear();
    }

    auto tEnd = clock::now();
    auto waitMs = std::chrono::duration_cast<std::chrono::milliseconds>(tWaitEnd - tWaitStart).count();
    auto cleanMs = std::chrono::duration_cast<std::chrono::milliseconds>(tCleanEnd - tCleanStart).count();
    auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart).count();
    LogOut(std::string("[BUFFER_FREEZE] End session P") + std::to_string(playerNum) +
           " (" + (reason?reason:"no reason") + ")" +
           " wait=" + std::to_string(waitMs) + "ms" +
           " clean=" + std::to_string(cleanMs) + "ms" +
           " total=" + std::to_string(totalMs) + "ms", true);
}

bool FreezeBufferWithPattern(int playerNum, const std::vector<uint8_t>& patternIn) {
    std::lock_guard<std::recursive_mutex> p2ControlLock(g_p2ControlMutex);
    std::lock_guard<std::recursive_mutex> controlLock(g_bufferFreezeControlMutex);
    if (g_onlineModeActive.load(std::memory_order_acquire)) return false;
    if (BufferFreezeInputLaneBusy(playerNum)) return false;
    if (TutorialBufferFreezeLeaseActive()) return false;
    StopBufferFreezing();
    if (patternIn.empty()) return false;
    uintptr_t playerPtr = GetPlayerPointer(playerNum);
    if (!playerPtr) return false;
    std::vector<uint8_t> pattern = patternIn;
    if (pattern.size() > 120) pattern.resize(120);

    const uint16_t startIndex = 0;
    const uint16_t clearLength = static_cast<uint16_t>(pattern.size() + 4);
    if (clearLength > 0) {
        std::vector<uint8_t> zeros(clearLength, 0x00);
        SafeWriteMemory(playerPtr + INPUT_BUFFER_OFFSET + startIndex, zeros.data(), clearLength);
    }
    if (!pattern.empty()) {
        SafeWriteMemory(playerPtr + INPUT_BUFFER_OFFSET + startIndex, pattern.data(), static_cast<uint32_t>(pattern.size()));
    }
    g_frozenBufferValues = pattern;
    g_frozenBufferStartIndex = startIndex;
    g_frozenBufferLength = static_cast<uint16_t>(pattern.size());
    g_frozenIndexValue = (startIndex + g_frozenBufferLength - 1) % INPUT_BUFFER_SIZE;
    g_indexFreezingActive = true;
    // A raw pattern has no motion to regenerate on a facing change.
    g_frozenMotionType = -1;
    g_frozenButtonMask = 0;
    if (!StartFreezeOrRollback(playerNum, playerPtr, clearLength)) return false;
    LogOut("[BUFFER_FREEZE] Generic pattern freeze active (len=" + std::to_string(pattern.size()) + ") P" + std::to_string(playerNum), true);
    return true;
}

bool FreezeBufferWithPattern(int playerNum, const std::vector<uint8_t>& patternIn, int extraNeutralFrames) {
    std::lock_guard<std::recursive_mutex> p2ControlLock(g_p2ControlMutex);
    std::lock_guard<std::recursive_mutex> controlLock(g_bufferFreezeControlMutex);
    if (g_onlineModeActive.load(std::memory_order_acquire)) return false;
    if (BufferFreezeInputLaneBusy(playerNum)) return false;
    if (TutorialBufferFreezeLeaseActive()) return false;
    if (extraNeutralFrames <= 0) {
        return FreezeBufferWithPattern(playerNum, patternIn);
    }
    StopBufferFreezing();
    if (patternIn.empty()) return false;
    uintptr_t playerPtr = GetPlayerPointer(playerNum);
    if (!playerPtr) return false;
    std::vector<uint8_t> pattern = patternIn;
    if (pattern.size() > 120) pattern.resize(120);
    if (extraNeutralFrames > 30) extraNeutralFrames = 30;
    const uint16_t startIndex = 0;
    const uint16_t clearLength = static_cast<uint16_t>(pattern.size() + extraNeutralFrames + 4);
    if (clearLength > 0) {
        std::vector<uint8_t> zeros(clearLength, 0x00);
        SafeWriteMemory(playerPtr + INPUT_BUFFER_OFFSET + startIndex, zeros.data(), clearLength);
    }
    if (!pattern.empty()) {
        SafeWriteMemory(playerPtr + INPUT_BUFFER_OFFSET + startIndex, pattern.data(), static_cast<uint32_t>(pattern.size()));
    }
    g_frozenBufferValues = pattern; // neutrals are virtual
    g_frozenBufferStartIndex = startIndex;
    g_frozenBufferLength = static_cast<uint16_t>(pattern.size());
    g_frozenIndexValue = (startIndex + g_frozenBufferLength + extraNeutralFrames - 1) % INPUT_BUFFER_SIZE;
    g_indexFreezingActive = true;
    g_frozenMotionType = -1;
    g_frozenButtonMask = 0;
    if (!StartFreezeOrRollback(playerNum, playerPtr, clearLength)) return false;
    LogOut("[BUFFER_FREEZE] Pattern freeze+advance (len=" + std::to_string(pattern.size()) + "+" + std::to_string(extraNeutralFrames) + ") idx=" + std::to_string(g_frozenIndexValue) + " P" + std::to_string(playerNum), true);
    return true;
}
