#include "../include/input/motion_system.h"
#include "../include/input/input_core.h"
#include "../include/core/memory.h"
#include "../include/core/logger.h"
#include "../include/utils/utilities.h"
#include "../include/core/constants.h"
#include "../include/game/auto_action_helpers.h"
#include "../include/game/per_frame_sample.h" // unified per-frame sample accessor
#include "../include/game/auto_action.h"
#include "../include/game/game_state.h"
#include "../include/game/macro_controller.h"
#include "../include/input/auto_action_motion_transaction.h"
#include "../include/input/immediate_input.h"
#include "../include/input/injection_control.h"
#include "../include/input/input_buffer.h"
#include "../include/input/input_freeze.h"
#include "../include/input/input_hook.h"
#include "../include/input/scoped_input_reservation.h"
#include "../include/input/motion_constants.h"  
#include <vector>
#include <sstream>
#include <algorithm>
#include <atomic>
#include <mutex>

std::vector<InputFrame> p1InputQueue;
std::vector<InputFrame> p2InputQueue;
int p1QueueIndex = 0;
int p2QueueIndex = 0;
int p1FrameCounter = 0;
int p2FrameCounter = 0;
bool p1QueueActive = false;
bool p2QueueActive = false;

int p1CurrentMotionType = MOTION_NONE;
int p2CurrentMotionType = MOTION_NONE;

namespace {
std::atomic<uint64_t> g_tutorialQueueToken[3]{};
std::atomic<uint64_t> g_nextTutorialQueueToken{1};
std::recursive_mutex g_motionQueueMutex;

// Admission must fail rather than cache delayed input behind another producer.
bool MotionQueueInputLaneBusy(int playerNum) {
    if (playerNum < 1 || playerNum > 2 || !IsPracticeContext()) return true;
    const bool queueActive = playerNum == 1 ? p1QueueActive : p2QueueActive;
    return queueActive ||
           IsScopedInputReserved(playerNum) ||
           IsAutoActionMotionTransactionActive(playerNum) ||
           IsAutoActionNormalPulseActive(playerNum) ||
           ImmediateInput::TutorialLeaseActive(playerNum) ||
           TutorialBufferFreezeLeaseActive() ||
           g_manualInputOverride[playerNum].load(std::memory_order_acquire) ||
           g_pollOverrideActive[playerNum].load(std::memory_order_acquire) ||
           ImmediateInput::GetCurrentDesired(playerNum) != 0 ||
           ImmediateInput::GetRemainingTicks(playerNum) != 0 ||
           (g_bufferFreezingActive.load(std::memory_order_acquire) &&
            (g_activeFreezePlayer.load(std::memory_order_acquire) == 0 ||
             g_activeFreezePlayer.load(std::memory_order_acquire) == playerNum)) ||
           (MacroController::IsExclusivePlayback() &&
            MacroController::GetPlaybackPlayer() == playerNum);
}
}

uint8_t DetermineButtonFromMotionType(int motionType) {
    switch (motionType) {
    case MOTION_5A: case MOTION_2A: case MOTION_JA: case MOTION_6A: case MOTION_4A:
    case MOTION_1A: case MOTION_3A: case MOTION_J2A: case MOTION_J6A:
        case MOTION_236A: case MOTION_623A: case MOTION_214A:
        case MOTION_421A: case MOTION_41236A:
    case MOTION_236236A: case MOTION_214214A: case MOTION_641236A:
        case MOTION_412A: case MOTION_22A: case MOTION_2141236A:
        case MOTION_463214A: case MOTION_4123641236A: case MOTION_6321463214A:
            return GAME_INPUT_A;
            
    case MOTION_5B: case MOTION_2B: case MOTION_JB: case MOTION_6B: case MOTION_4B:
    case MOTION_1B: case MOTION_3B: case MOTION_J2B: case MOTION_J6B:
        case MOTION_236B: case MOTION_623B: case MOTION_214B:
        case MOTION_421B: case MOTION_41236B:
    case MOTION_236236B: case MOTION_214214B: case MOTION_641236B:
        case MOTION_412B: case MOTION_22B: case MOTION_2141236B:
        case MOTION_463214B: case MOTION_4123641236B: case MOTION_6321463214B:
            return GAME_INPUT_B;
            
    case MOTION_5C: case MOTION_2C: case MOTION_JC: case MOTION_6C: case MOTION_4C:
    case MOTION_1C: case MOTION_3C: case MOTION_J2C: case MOTION_J6C:
        case MOTION_236C: case MOTION_623C: case MOTION_214C:
        case MOTION_421C: case MOTION_41236C:
    case MOTION_236236C: case MOTION_214214C: case MOTION_641236C:
        case MOTION_412C: case MOTION_22C: case MOTION_2141236C:
        case MOTION_463214C: case MOTION_4123641236C: case MOTION_6321463214C:
            return GAME_INPUT_C;
    
    case MOTION_5D: case MOTION_2D: case MOTION_JD: case MOTION_6D: case MOTION_4D:
    case MOTION_1D: case MOTION_3D: case MOTION_J2D: case MOTION_J6D:
        case MOTION_236D: case MOTION_623D: case MOTION_214D:
        case MOTION_421D: case MOTION_41236D:
    case MOTION_236236D: case MOTION_214214D: case MOTION_641236D:
        case MOTION_412D: case MOTION_22D: case MOTION_2141236D:
        case MOTION_463214D: case MOTION_4123641236D: case MOTION_6321463214D:
            return GAME_INPUT_D;
            
        default:
            return GAME_INPUT_A;
    }
}

std::string GetMotionTypeName(int motionType) {
    switch (motionType) {
        case MOTION_5A: return "5A";
        case MOTION_5B: return "5B";
        case MOTION_5C: return "5C";
        case MOTION_2A: return "2A";
        case MOTION_2B: return "2B";
        case MOTION_2C: return "2C";
    case MOTION_JA: return "j.A";
    case MOTION_6A: return "6A";
    case MOTION_6B: return "6B";
    case MOTION_6C: return "6C";
    case MOTION_4A: return "4A";
    case MOTION_4B: return "4B";
    case MOTION_4C: return "4C";
    case MOTION_1A: return "1A";
    case MOTION_1B: return "1B";
    case MOTION_1C: return "1C";
    case MOTION_1D: return "1S";
    case MOTION_3A: return "3A";
    case MOTION_3B: return "3B";
    case MOTION_3C: return "3C";
    case MOTION_3D: return "3S";
    case MOTION_J2A: return "j.2A";
    case MOTION_J2B: return "j.2B";
    case MOTION_J2C: return "j.2C";
    case MOTION_J2D: return "j.2S";
    case MOTION_J6A: return "j.6A";
    case MOTION_J6B: return "j.6B";
    case MOTION_J6C: return "j.6C";
    case MOTION_J6D: return "j.6S";
    case MOTION_5D: return "5S";
    case MOTION_2D: return "2S";
    case MOTION_JD: return "j.S";
    case MOTION_6D: return "6S";
    case MOTION_4D: return "4S";
        case MOTION_JB: return "j.B";
        case MOTION_JC: return "j.C";
        case MOTION_236A: return "236A";
        case MOTION_236B: return "236B";
        case MOTION_236C: return "236C";
        case MOTION_236D: return "236S";
        case MOTION_623A: return "623A";
        case MOTION_623B: return "623B";
        case MOTION_623C: return "623C";
        case MOTION_623D: return "623S";
        case MOTION_214A: return "214A";
        case MOTION_214B: return "214B";
        case MOTION_214C: return "214C";
        case MOTION_214D: return "214S";
        case MOTION_421A: return "421A";
        case MOTION_421B: return "421B";
        case MOTION_421C: return "421C";
        case MOTION_421D: return "421S";
        case MOTION_41236A: return "41236A";
        case MOTION_41236B: return "41236B";
        case MOTION_41236C: return "41236C";
        case MOTION_41236D: return "41236S";
        case MOTION_236236A: return "236236A";
        case MOTION_236236B: return "236236B";
        case MOTION_236236C: return "236236C";
        case MOTION_236236D: return "236236S";
        case MOTION_214214A: return "214214A";
        case MOTION_214214B: return "214214B";
        case MOTION_214214C: return "214214C";
        case MOTION_214214D: return "214214S";
        case MOTION_641236A: return "641236A";
        case MOTION_641236B: return "641236B";
        case MOTION_641236C: return "641236C";
        case MOTION_641236D: return "641236S";
        case MOTION_412A: return "412A";
        case MOTION_412B: return "412B";
        case MOTION_412C: return "412C";
        case MOTION_412D: return "412S";
        case MOTION_22A: return "22A";
        case MOTION_22B: return "22B";
        case MOTION_22C: return "22C";
        case MOTION_22D: return "22S";
        case MOTION_2141236A: return "2141236A";
        case MOTION_2141236B: return "2141236B";
        case MOTION_2141236C: return "2141236C";
        case MOTION_2141236D: return "2141236S";
        case MOTION_463214A: return "463214A";
        case MOTION_463214B: return "463214B";
        case MOTION_463214C: return "463214C";
        case MOTION_463214D: return "463214S";
        case MOTION_4123641236A: return "4123641236A";
        case MOTION_4123641236B: return "4123641236B";
        case MOTION_4123641236C: return "4123641236C";
        case MOTION_4123641236D: return "4123641236S";
    case MOTION_6321463214A: return "6321463214A";
    case MOTION_6321463214B: return "6321463214B";
    case MOTION_6321463214C: return "6321463214C";
    case MOTION_6321463214D: return "6321463214S";
    case MOTION_FORWARD_DASH: return "Forward Dash";
    case MOTION_BACK_DASH: return "Back Dash";
        default: return "Unknown";
    }
}

// Manages queue state only; input writes happen in the input hook.
void ProcessInputQueues() {
    // Lock order: controller -> queue.
    std::lock_guard<std::recursive_mutex> p2ControlLock(g_p2ControlMutex);
    std::lock_guard<std::recursive_mutex> queueLock(g_motionQueueMutex);
    if (g_onlineModeActive.load(std::memory_order_acquire)) return;
    const PerFrameSample &sample = GetCurrentPerFrameSample();
    if (!p1QueueActive && !p2QueueActive) return;
    if (p1QueueActive) {
        if (p1QueueIndex >= 0 && (size_t)p1QueueIndex < p1InputQueue.size()) {
            if (++p1FrameCounter >= p1InputQueue[p1QueueIndex].durationFrames) {
                p1FrameCounter = 0;
                p1QueueIndex++;
                
                if (p1QueueIndex >= (int)p1InputQueue.size()) {
                    p1QueueActive = false;
                    p1QueueIndex = 0;
                    LogOut("[INPUT_QUEUE] P1 queue completed", true);
                    if (detailedLogging.load()) {
                        DumpInputBuffer(1, "AFTER_QUEUE_COMPLETE");
                    }
                }
            }
        } else {
            p1QueueActive = false;
            LogOut("[INPUT_QUEUE] P1 queue deactivated (invalid index)", true);
        }
    }
    
    if (p2QueueActive) {
        if (p2QueueIndex >= 0 && (size_t)p2QueueIndex < p2InputQueue.size()) {
            if (++p2FrameCounter >= p2InputQueue[p2QueueIndex].durationFrames) {
                p2FrameCounter = 0;
                p2QueueIndex++;
                
                if (p2QueueIndex >= (int)p2InputQueue.size()) {
                    p2QueueActive = false;
                    p2QueueIndex = 0;
                    LogOut("[INPUT_QUEUE] P2 queue completed", true);
                    if (detailedLogging.load()) {
                        DumpInputBuffer(2, "AFTER_QUEUE_COMPLETE");
                    }
                }
            }
        } else {
            p2QueueActive = false;
            LogOut("[INPUT_QUEUE] P2 queue deactivated (invalid index)", true);
        }
    }
}

static bool QueueMotionInputImpl(int playerNum, int motionType, int buttonMask) {
    if (playerNum < 1 || playerNum > 2) return false;
    
    bool facingRight = GetPlayerFacingDirection(playerNum);
    LogOut("[INPUT_MOTION] Player " + std::to_string(playerNum) + " is facing " + 
           (facingRight ? "right" : "left"), true);
    
    std::vector<InputFrame>& queue = (playerNum == 1) ? p1InputQueue : p2InputQueue;
    if (!queue.empty()) {
        LogOut(std::string("[INPUT_MOTION][TRACE] Clearing existing queue (size=") + std::to_string(queue.size()) + ") for P" + std::to_string(playerNum), true);
    }
    queue.clear();

    if (buttonMask == 0) {
        buttonMask = DetermineButtonFromMotionType(motionType);
    }
    
    const int DIR_FRAMES = 3;
    const int BTN_FRAMES = 6;
    const int NEUTRAL_FRAMES = 2;
    
    auto getDirectionMask = [facingRight](uint8_t dirMask) -> uint8_t {
        if (facingRight) {
            return dirMask;
        } else {
            uint8_t result = dirMask & ~(GAME_INPUT_LEFT | GAME_INPUT_RIGHT);
            
            if (dirMask & GAME_INPUT_LEFT) result |= GAME_INPUT_RIGHT;
            if (dirMask & GAME_INPUT_RIGHT) result |= GAME_INPUT_LEFT;
            
            return result;
        }
    };

    auto addInput = [&](uint8_t dirMask, uint8_t btnMask, int frames) {
        uint8_t adjustedDirMask = getDirectionMask(dirMask);
        for (int i = 0; i < frames; ++i)
            queue.push_back({static_cast<uint8_t>(adjustedDirMask | btnMask), 1});
    };

    if (motionType == MOTION_FORWARD_DASH || motionType == MOTION_BACK_DASH) {
        LogOut(std::string("[INPUT_MOTION][TRACE] Building dash motion sequence for P") + std::to_string(playerNum) + " type=" + GetMotionTypeName(motionType) + (buttonMask?" (unexpected buttonMask)":""), true);
    }
    switch (motionType) {
        case MOTION_236A: case MOTION_236B: case MOTION_236C:
            // QCF: Down, Down-Forward, Forward + Button
            addInput(GAME_INPUT_DOWN, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN | GAME_INPUT_RIGHT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_RIGHT, buttonMask, BTN_FRAMES);
            break;
            
        case MOTION_623A: case MOTION_623B: case MOTION_623C:
            // DP: Forward, Down, Down-Forward + Button
            addInput(GAME_INPUT_RIGHT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN | GAME_INPUT_RIGHT, buttonMask, BTN_FRAMES);
            break;
            
        case MOTION_214A: case MOTION_214B: case MOTION_214C:
            // QCB: Down, Down-Back, Back + Button
            addInput(GAME_INPUT_DOWN, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN | GAME_INPUT_LEFT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_LEFT, buttonMask, BTN_FRAMES);
            break;
            
        case MOTION_41236A: case MOTION_41236B: case MOTION_41236C:
            // HCF: Back, Down-Back, Down, Down-Forward, Forward + Button
            addInput(GAME_INPUT_LEFT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN | GAME_INPUT_LEFT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN | GAME_INPUT_RIGHT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_RIGHT, buttonMask, BTN_FRAMES);
            break;
            
        case MOTION_236236A: case MOTION_236236B: case MOTION_236236C:
            // Double QCF: (Down, Down-Forward, Forward) x2 + Button on final Forward
            addInput(GAME_INPUT_DOWN, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN | GAME_INPUT_RIGHT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_RIGHT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN | GAME_INPUT_RIGHT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_RIGHT, buttonMask, BTN_FRAMES);
            break;

        case MOTION_214214A: case MOTION_214214B: case MOTION_214214C:
            // Double QCB: (Down, Down-Back, Back) x2 + Button on final Back
            addInput(GAME_INPUT_DOWN, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN | GAME_INPUT_LEFT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_LEFT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN | GAME_INPUT_LEFT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_LEFT, buttonMask, BTN_FRAMES);
            break;

        case MOTION_641236A: case MOTION_641236B: case MOTION_641236C:
            // 641236 (pretzel-like) : Forward, Back, Down-Back, Down, Down-Forward, Forward + Button
            addInput(GAME_INPUT_RIGHT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_LEFT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN | GAME_INPUT_LEFT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN | GAME_INPUT_RIGHT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_RIGHT, buttonMask, BTN_FRAMES);
            break;

        case MOTION_412A: case MOTION_412B: case MOTION_412C:
            // 412: Back, Down-Back, Down + Button
            addInput(GAME_INPUT_LEFT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN | GAME_INPUT_LEFT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN, buttonMask, BTN_FRAMES);
            break;

        case MOTION_22A: case MOTION_22B: case MOTION_22C:
            // 22: Down, (neutral optional), Down + Button
            addInput(GAME_INPUT_DOWN, 0, DIR_FRAMES);
            addInput(0, 0, NEUTRAL_FRAMES);
            addInput(GAME_INPUT_DOWN, buttonMask, BTN_FRAMES);
            break;

        case MOTION_2141236A: case MOTION_2141236B: case MOTION_2141236C:
            // 2141236: Down, Down-Back, Back, Down-Back, Down, Down-Forward, Forward + Button
            addInput(GAME_INPUT_DOWN, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN | GAME_INPUT_LEFT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_LEFT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN | GAME_INPUT_LEFT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN | GAME_INPUT_RIGHT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_RIGHT, buttonMask, BTN_FRAMES);
            break;

        case MOTION_463214A: case MOTION_463214B: case MOTION_463214C:
            // 463214: Left, Right, Down-Right, Down, Down-Left, Left + Button
            addInput(GAME_INPUT_LEFT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_RIGHT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN | GAME_INPUT_RIGHT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN | GAME_INPUT_LEFT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_LEFT, buttonMask, BTN_FRAMES);
            break;

        case MOTION_4123641236A: case MOTION_4123641236B: case MOTION_4123641236C:
            // 4123641236: (41236) x2 without button until final Forward
            // First 41236
            addInput(GAME_INPUT_LEFT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN | GAME_INPUT_LEFT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN | GAME_INPUT_RIGHT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_RIGHT, 0, DIR_FRAMES);
            // Second 41236 with button at end
            addInput(GAME_INPUT_LEFT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN | GAME_INPUT_LEFT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN | GAME_INPUT_RIGHT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_RIGHT, buttonMask, BTN_FRAMES);
            break;

        case MOTION_6321463214A: case MOTION_6321463214B: case MOTION_6321463214C:
            // 6321463214: 6,3,2,1,4,6,3,2,1,4 + Button
            addInput(GAME_INPUT_RIGHT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN | GAME_INPUT_RIGHT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN | GAME_INPUT_LEFT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_LEFT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_RIGHT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN | GAME_INPUT_RIGHT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN | GAME_INPUT_LEFT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_LEFT, buttonMask, BTN_FRAMES);
            break;
            
        case MOTION_421A: case MOTION_421B: case MOTION_421C:
            // 421: Back, Down, Down-Back + Button; same shape as MotionPattern::Build.
            addInput(GAME_INPUT_LEFT, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN, 0, DIR_FRAMES);
            addInput(GAME_INPUT_DOWN | GAME_INPUT_LEFT, buttonMask, BTN_FRAMES);
            break;
            
        case MOTION_FORWARD_DASH: {
            // Always supply canonical F/B; getDirectionMask() applies the single facing flip.
            addInput(GAME_INPUT_RIGHT, 0, DIR_FRAMES);
            addInput(0, 0, NEUTRAL_FRAMES);
            addInput(GAME_INPUT_RIGHT, 0, DIR_FRAMES);
            break;
        }

        case MOTION_BACK_DASH: {
            // Back dash: canonical B, N, B; same single flip.
            addInput(GAME_INPUT_LEFT, 0, DIR_FRAMES);
            addInput(0, 0, NEUTRAL_FRAMES);
            addInput(GAME_INPUT_LEFT, 0, DIR_FRAMES);
            break;
        }
            
        case MOTION_5A: case MOTION_5B: case MOTION_5C:
            addInput(0, buttonMask, BTN_FRAMES);
            break;
            
        case MOTION_2A: case MOTION_2B: case MOTION_2C:
            addInput(GAME_INPUT_DOWN, buttonMask, BTN_FRAMES);
            break;
            
        case MOTION_JA: case MOTION_JB: case MOTION_JC:
            addInput(GAME_INPUT_UP, buttonMask, BTN_FRAMES);
            break;
        case MOTION_6A: case MOTION_6B: case MOTION_6C:
            // Pass canonical forward; addInput() applies the facing flip.
            addInput(GAME_INPUT_RIGHT, buttonMask, BTN_FRAMES);
            break;
        case MOTION_4A: case MOTION_4B: case MOTION_4C:
            // Canonical back; addInput() applies the facing flip.
            addInput(GAME_INPUT_LEFT, buttonMask, BTN_FRAMES);
            break;
            
        default:
            LogOut("[INPUT_MOTION] WARNING: Unknown motion type " + std::to_string(motionType), true);
            return false;
    }

    // Dash patterns must be written to the buffer in full immediately; frame-by-frame injection gets contaminated by neutral writes.
    if (motionType == MOTION_FORWARD_DASH || motionType == MOTION_BACK_DASH) {
        LogOut("[INPUT_MOTION] Writing dash pattern directly to buffer (all frames at once)", true);
        for (const auto& frame : queue) {
            if (!WritePlayerInputToBuffer(playerNum, frame.inputMask)) {
                LogOut("[INPUT_MOTION] ERROR: Failed to write dash frame to buffer!", true);
                return false;
            }
        }
        if (detailedLogging.load()) {
            DumpInputBuffer(playerNum, "AFTER_DIRECT_DASH_WRITE");
        }
    }
    
    if (playerNum == 1) {
        p1QueueActive = true;
        p1QueueIndex = 0;
        p1FrameCounter = 0;
    } else {
        p2QueueActive = true;
        p2QueueIndex = 0;
        p2FrameCounter = 0;
    }

    if (playerNum == 1) p1CurrentMotionType = motionType; else p2CurrentMotionType = motionType;
    LogOut("[INPUT_MOTION] Queued motion " + GetMotionTypeName(motionType) + 
           " for P" + std::to_string(playerNum) + " with " + std::to_string(queue.size()) + 
           " inputs (index reset=0)", true);
    if (motionType == MOTION_FORWARD_DASH || motionType == MOTION_BACK_DASH) {
        std::ostringstream oss;
        oss << "[INPUT_MOTION][TRACE] Dumping dash queue (size=" << queue.size() << "):";
        LogOut(oss.str(), true);
        int idx=0; for (auto &f : queue) {
            LogOut(std::string("[INPUT_MOTION][TRACE]   ") + std::to_string(idx++) + ": mask=" + std::to_string((int)f.inputMask) + " dur=" + std::to_string(f.durationFrames), true);
        }
    }
    
    return true;
}

bool QueueMotionInput(int playerNum, int motionType, int buttonMask) {
    if (playerNum < 1 || playerNum > 2 ||
        IsScopedInputReserved(playerNum)) return false;
    std::lock_guard<std::recursive_mutex> p2ControlLock(g_p2ControlMutex);
    std::lock_guard<std::recursive_mutex> queueLock(g_motionQueueMutex);
    if (g_onlineModeActive.load(std::memory_order_acquire) ||
        MotionQueueInputLaneBusy(playerNum)) {
        return false;
    }
    if (g_tutorialQueueToken[playerNum].load(std::memory_order_acquire) != 0) {
        LogOut("[INPUT_MOTION] Queue rejected: tutorial owns P" +
               std::to_string(playerNum), true);
        return false;
    }
    return QueueMotionInputImpl(playerNum, motionType, buttonMask);
}

bool AcquireTutorialMotionQueue(int playerNum, uint64_t& tokenOut) {
    tokenOut = 0;
    if (playerNum < 1 || playerNum > 2) return false;
    std::lock_guard<std::recursive_mutex> p2ControlLock(g_p2ControlMutex);
    std::lock_guard<std::recursive_mutex> queueLock(g_motionQueueMutex);
    if (g_onlineModeActive.load(std::memory_order_acquire) ||
        MotionQueueInputLaneBusy(playerNum)) {
        return false;
    }
    const bool active = playerNum == 1 ? p1QueueActive : p2QueueActive;
    if (active) return false;
    uint64_t expected = 0;
    uint64_t token = g_nextTutorialQueueToken.fetch_add(1, std::memory_order_relaxed);
    if (token == 0) token = g_nextTutorialQueueToken.fetch_add(1, std::memory_order_relaxed);
    if (!g_tutorialQueueToken[playerNum].compare_exchange_strong(
            expected, token, std::memory_order_acq_rel)) return false;
    tokenOut = token;
    return true;
}

bool TutorialMotionQueueLeaseActive(int playerNum) {
    return playerNum >= 1 && playerNum <= 2 &&
        g_tutorialQueueToken[playerNum].load(std::memory_order_acquire) != 0;
}

bool QueueTutorialMotionInput(int playerNum, uint64_t token, int motionType,
                              int buttonMask) {
    if (playerNum < 1 || playerNum > 2 || token == 0) return false;
    std::lock_guard<std::recursive_mutex> p2ControlLock(g_p2ControlMutex);
    std::lock_guard<std::recursive_mutex> queueLock(g_motionQueueMutex);
    if (g_onlineModeActive.load(std::memory_order_acquire) ||
        MotionQueueInputLaneBusy(playerNum) ||
        g_tutorialQueueToken[playerNum].load(std::memory_order_acquire) != token)
        return false;
    return QueueMotionInputImpl(playerNum, motionType, buttonMask);
}

void ReleaseTutorialMotionQueue(int playerNum, uint64_t token) {
    if (playerNum < 1 || playerNum > 2 || token == 0) return;
    std::lock_guard<std::recursive_mutex> p2ControlLock(g_p2ControlMutex);
    std::lock_guard<std::recursive_mutex> queueLock(g_motionQueueMutex);
    if (
        g_tutorialQueueToken[playerNum].load(std::memory_order_acquire) != token)
        return;
    std::vector<InputFrame>& queue = playerNum == 1 ? p1InputQueue : p2InputQueue;
    bool& active = playerNum == 1 ? p1QueueActive : p2QueueActive;
    int& index = playerNum == 1 ? p1QueueIndex : p2QueueIndex;
    int& counter = playerNum == 1 ? p1FrameCounter : p2FrameCounter;
    int& motion = playerNum == 1 ? p1CurrentMotionType : p2CurrentMotionType;
    active = false;
    index = 0;
    counter = 0;
    motion = MOTION_NONE;
    queue.clear();
    uint64_t expected = token;
    (void)g_tutorialQueueToken[playerNum].compare_exchange_strong(
        expected, 0, std::memory_order_acq_rel);
}

MotionQueueSnapshot GetMotionQueueSnapshot(int playerNum) {
    MotionQueueSnapshot snapshot;
    if (playerNum < 1 || playerNum > 2) return snapshot;
    std::lock_guard<std::recursive_mutex> p2ControlLock(g_p2ControlMutex);
    std::lock_guard<std::recursive_mutex> queueLock(g_motionQueueMutex);
    const auto& queue = playerNum == 1 ? p1InputQueue : p2InputQueue;
    snapshot.active = playerNum == 1 ? p1QueueActive : p2QueueActive;
    snapshot.index = playerNum == 1 ? p1QueueIndex : p2QueueIndex;
    snapshot.frameCounter = playerNum == 1 ? p1FrameCounter : p2FrameCounter;
    snapshot.motionType = playerNum == 1 ? p1CurrentMotionType : p2CurrentMotionType;
    snapshot.size = queue.size();
    if (snapshot.active && snapshot.index >= 0 &&
        static_cast<std::size_t>(snapshot.index) < queue.size()) {
        snapshot.hasCurrentMask = true;
        snapshot.currentMask = queue[static_cast<std::size_t>(snapshot.index)].inputMask;
    }
    return snapshot;
}

bool ClearMotionInputQueue(int playerNum, bool includeTutorialOwned) {
    if (playerNum < 1 || playerNum > 2) return false;
    std::lock_guard<std::recursive_mutex> p2ControlLock(g_p2ControlMutex);
    std::lock_guard<std::recursive_mutex> queueLock(g_motionQueueMutex);
    if (!includeTutorialOwned &&
        g_tutorialQueueToken[playerNum].load(std::memory_order_acquire) != 0) {
        return false;
    }
    std::vector<InputFrame>& queue =
        playerNum == 1 ? p1InputQueue : p2InputQueue;
    bool& active = playerNum == 1 ? p1QueueActive : p2QueueActive;
    int& index = playerNum == 1 ? p1QueueIndex : p2QueueIndex;
    int& counter = playerNum == 1 ? p1FrameCounter : p2FrameCounter;
    int& motion = playerNum == 1 ? p1CurrentMotionType : p2CurrentMotionType;
    active = false;
    index = 0;
    counter = 0;
    motion = MOTION_NONE;
    queue.clear();
    return true;
}

int ConvertActionToMotion(int actionType, int triggerType) {
    return ConvertTriggerActionToMotion(actionType, triggerType);
}
