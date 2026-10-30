#include <windows.h>
#include <string>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <mutex>
#include "../include/core/memory.h"
#include "../include/core/logger.h"
#include "../include/game/game_state.h"

#include "../include/core/constants.h"
#include "../include/utils/utilities.h"
#include "../include/utils/network.h"

#include "../include/game/practice_patch.h"
#include "../include/game/per_frame_sample.h"
#include "../include/gui/overlay.h"
#include "../include/input/input_core.h"  // Include this to get AI_CONTROL_FLAG_OFFSET
#include "../include/input/input_hook.h"  // normal-pulse raw-register lease
#include "../include/input/input_motion.h" // for direction/stance constants
#include "../include/game/guard_overrides.h" // character/move grounded overheads
#include "../include/game/character_settings.h" // character ID from name
#include "../include/game/auto_action.h" // g_p2ControlOverridden
#include "../include/game/auto_action_charge.h"
#include "../include/game/kaori_recoil_duck.h"
#include "../include/input/auto_action_motion_transaction.h"
#include "../include/game/practice_offsets.h"
#include "../include/utils/pause_integration.h"
#include "../include/utils/config.h"
#include "../include/utils/switch_players.h"
#include "../include/utils/xp_compat.h"
#include "../include/game/frame_analysis.h"

const uintptr_t P2_CPU_FLAG_OFFSET = 4932; // RIGHT side shutter (P2's spatial position)
const uintptr_t PRACTICE_BLOCK_MODE_OFFSET = 4934;   // 0..2
const uintptr_t PRACTICE_AUTO_BLOCK_OFFSET = 4936;   // dword, 0/1

namespace {
std::mutex g_autoBlockWriteMutex;
std::atomic<uint64_t> g_autoBlockWriteGeneration{0};
std::atomic<uint64_t> g_tutorialAutoBlockToken{0};
std::atomic<uint64_t> g_nextTutorialAutoBlockToken{1};
}

std::string FormatHexAddress(uintptr_t address);
void DumpPracticeModeState();
void LogPlayerInputsInPracticeMode();
static bool WriteP2BlockStance(uint8_t stance);

bool EnablePlayer2InPracticeMode() {
    // Controller-role changes serialize with the auto-action producer under g_p2ControlMutex.
    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    if (!IsPracticeContext()) return false;
    KaoriRecoilDuck::Cancel(2, "Practice enabled P2 human control");
    CancelAutoActionChargeFollowup(2, "Practice enabled P2 human control");
    CancelP2AutoActionMotionTransaction("Practice enabled P2 human control");
    if (IsP2AutoActionMotionTransactionActive()) return false;

    LogOut("[PRACTICE_PATCH] Attempting to enable Player 2 controls in Practice mode...", true);

    uintptr_t efzBase = GetEFZBase();
    if (!efzBase) {
        LogOut("[PRACTICE_PATCH] Failed to get EFZ base address", true);
        return false;
    }
    LogOut("[PRACTICE_PATCH] EFZ base address: 0x" + std::to_string(efzBase), true);

    uintptr_t gameStatePtr = GetGameStatePtr();
    if (!gameStatePtr) {
        LogOut("[PRACTICE_PATCH] Failed to resolve game state pointer", true);
        return false;
    }
    LogOut("[PRACTICE_PATCH] Game state pointer: 0x" + std::to_string(gameStatePtr), true);

    uint8_t currentControlFlag = 0;
    if (SafeReadMemory(gameStatePtr + P2_CPU_FLAG_OFFSET, &currentControlFlag, sizeof(uint8_t))) {
        LogOut("[PRACTICE_PATCH] Current P2 CPU control flag value: " + std::to_string(currentControlFlag), true);
    }
    
    // 0 = human, 1 = CPU
    uint8_t humanControlled = 0;
    if (!SafeWriteMemory(gameStatePtr + P2_CPU_FLAG_OFFSET, &humanControlled, sizeof(uint8_t))) {
        LogOut("[PRACTICE_PATCH] Failed to patch Player 2 CPU flag", true);
        return false;
    }
    LogOut("[PRACTICE_PATCH] Successfully patched P2 CPU control flag to 0 (human controlled)", true);

    uintptr_t p2CharPtr = GetPlayerBase(2);
    if (!p2CharPtr) {
        LogOut("[PRACTICE_PATCH] Failed to read Player 2 character pointer", true);
        return false;
    }
    LogOut("[PRACTICE_PATCH] Player 2 character pointer: 0x" + std::to_string(p2CharPtr), true);

    bool aiPatchSuccess = false;
    
    if (p2CharPtr) {
        uint32_t currentAIFlag = 0;
        if (SafeReadMemory(p2CharPtr + AI_CONTROL_FLAG_OFFSET, &currentAIFlag, sizeof(uint32_t))) {
            LogOut("[PRACTICE_PATCH] Current P2 AI control flag value: " + std::to_string(currentAIFlag), true);
        }
        
        uint32_t aiControlFlag = 0;
        
        if (SafeWriteMemory(p2CharPtr + AI_CONTROL_FLAG_OFFSET, &aiControlFlag, sizeof(uint32_t))) {
            LogOut("[PRACTICE_PATCH] Successfully patched P2 AI control flag to 0", true);
            aiPatchSuccess = true;
        } else {
            LogOut("[PRACTICE_PATCH] Failed to patch Player 2 AI control flag", true);
        }
    }
    else {
        LogOut("[PRACTICE_PATCH] Player 2 character not initialized yet, skipping AI flag patch", true);
    }

    LogOut("[PRACTICE_PATCH] " + std::string(aiPatchSuccess ? "Successfully" : "Partially") + 
           " enabled Player 2 controls in Practice mode", true);
    
    LogOut("[PRACTICE_PATCH] Verifying patch application with final memory read...", true);
    
    uint8_t verifyControlFlag = 0xFF;
    if (SafeReadMemory(gameStatePtr + P2_CPU_FLAG_OFFSET, &verifyControlFlag, sizeof(uint8_t))) {
        if (verifyControlFlag == 0) {
            LogOut("[PRACTICE_PATCH] Verification SUCCESS: P2 CPU flag is properly set to 0", true);
        } else {
            LogOut("[PRACTICE_PATCH] Verification FAILED: P2 CPU flag is " + std::to_string(verifyControlFlag) + 
                  " (expected 0)", true);
        }
    }
    
    if (p2CharPtr) {
        uint32_t verifyAIFlag = 0xFFFFFFFF;
        if (SafeReadMemory(p2CharPtr + AI_CONTROL_FLAG_OFFSET, &verifyAIFlag, sizeof(uint32_t))) {
            if (verifyAIFlag == 0) {
                LogOut("[PRACTICE_PATCH] Verification SUCCESS: P2 AI flag is properly set to 0", true);
            } else {
                LogOut("[PRACTICE_PATCH] Verification FAILED: P2 AI flag is " + std::to_string(verifyAIFlag) + 
                      " (expected 0)", true);
                
                LogOut("[PRACTICE_PATCH] Attempting additional repair of AI flag...", true);
                
                DWORD oldProtect;
                if (VirtualProtect((LPVOID)(p2CharPtr + AI_CONTROL_FLAG_OFFSET), sizeof(uint32_t), PAGE_READWRITE, &oldProtect)) {
                    uint32_t zero = 0;
                    memcpy((void*)(p2CharPtr + AI_CONTROL_FLAG_OFFSET), &zero, sizeof(uint32_t));
                    VirtualProtect((LPVOID)(p2CharPtr + AI_CONTROL_FLAG_OFFSET), sizeof(uint32_t), oldProtect, &oldProtect);
                    
                    if (SafeReadMemory(p2CharPtr + AI_CONTROL_FLAG_OFFSET, &verifyAIFlag, sizeof(uint32_t)) && 
                        verifyAIFlag == 0) {
                        LogOut("[PRACTICE_PATCH] Additional repair SUCCESS: P2 AI flag is now 0", true);
                    } else {
                        LogOut("[PRACTICE_PATCH] Additional repair FAILED: P2 AI flag still not 0", true);
                    }
                } else {
                    LogOut("[PRACTICE_PATCH] Failed to change memory protection for additional repair", true);
                }
            }
        }
    }
    
    DumpPracticeModeState();
    
    return true;
}

void ScanForPotentialAIFlags() {
    // CRITICAL: Never scan/write game memory during online mode
    if (g_onlineModeActive.load()) return;

    LogOut("[PRACTICE_PATCH] Scanning for potential AI control flags...", true);
    
    uintptr_t p2CharPtr = GetPlayerBase(2);
    if (!p2CharPtr) {
        LogOut("[PRACTICE_PATCH] Failed to read Player 2 character pointer", true);
        return;
    }
    
    LogOut("[PRACTICE_PATCH] P2 character pointer: " + FormatHexAddress(p2CharPtr), true);
    
    const int SCAN_SIZE = 512;
    
    LogOut("[PRACTICE_PATCH] Scanning " + std::to_string(SCAN_SIZE) + " bytes for potential control flags...", true);
    
    for (int offset = 0; offset < SCAN_SIZE; offset += 4) {
        uint32_t value = 0;
        if (SafeReadMemory(p2CharPtr + offset, &value, sizeof(uint32_t))) {
            if (value == 1) {
                std::stringstream ss;
                ss << "[PRACTICE_PATCH] Potential control flag found at offset " 
                   << offset << " (0x" << std::hex << std::uppercase 
                   << std::setw(4) << std::setfill('0') << offset << ") = " << std::dec 
                   << value;
                LogOut(ss.str(), true);
                
                uint32_t zero = 0;
                if (SafeWriteMemory(p2CharPtr + offset, &zero, sizeof(uint32_t))) {
                    LogOut("[PRACTICE_PATCH] Successfully patched potential control flag at offset " + 
                           std::to_string(offset) + " to 0", true);
                } else {
                    LogOut("[PRACTICE_PATCH] Failed to patch potential control flag at offset " + 
                           std::to_string(offset), true);
                }
            }
        }
    }
    
    LogOut("[PRACTICE_PATCH] Completed scanning for potential AI control flags", true);
}
void MonitorAndPatchPracticeMode() {
    // CRITICAL: Never run during online mode
    if (g_onlineModeActive.load()) return;

    LogOut("[PRACTICE_PATCH] Starting practice mode monitor thread", true);
    
    int consecutiveFailures = 0;
    GameMode lastMode = GameMode::Unknown;
    int cycleCount = 0;
    
    bool lastP2CpuControlled = true;
    bool lastP2AIControlled = true;
    
    while (true) {
        if (g_onlineModeActive.load()) {
            LogOut("[PRACTICE_PATCH] Online mode detected, stopping practice monitor thread", true);
            return;
        }

        GameMode currentMode = GetCurrentGameMode();
        cycleCount++;
        
        if (currentMode != lastMode) {
            LogOut("[PRACTICE_PATCH] Game mode changed: " + GetGameModeName(lastMode) + 
                   " -> " + GetGameModeName(currentMode), true);
            lastMode = currentMode;
            
            lastP2CpuControlled = true;
            lastP2AIControlled = true;
            
            DumpPracticeModeState();
        }
        
    if (currentMode == GameMode::Practice) {
        {
            // Resolve P2 pointers/flags only under g_p2ControlMutex (same barrier as native P2 production and netplay).
            std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
            if (g_onlineModeActive.load(std::memory_order_acquire)) {
                LogOut("[PRACTICE_PATCH] Netplay won P2 control lock; stopping practice monitor", true);
                return;
            }
            const bool stillPractice =
                GetCurrentGameMode() == GameMode::Practice;
            if (stillPractice) {
            uintptr_t gameStatePtr = GetGameStatePtr();
            uint8_t p2CpuFlag = 1;

            if (gameStatePtr &&
                SafeReadMemory(gameStatePtr + P2_CPU_FLAG_OFFSET, &p2CpuFlag, sizeof(uint8_t))) {
                
                bool p2CpuControlled = (p2CpuFlag != 0);
                
                if (p2CpuControlled != lastP2CpuControlled) {
                    LogOut("[PRACTICE_PATCH] P2 CPU control state changed: " + 
                           std::string(lastP2CpuControlled ? "CPU" : "Human") + " -> " + 
                           std::string(p2CpuControlled ? "CPU" : "Human"), true);
                    lastP2CpuControlled = p2CpuControlled;
                }
                
                // The P2 AI flag mirrors the game-state CPU flag (human=0, CPU=1).
                uintptr_t p2CharPtr = GetPlayerBase(2);
                uint32_t p2AIFlag = 1;
                bool p2CharacterInitialized = (p2CharPtr != 0);

                if (p2CharacterInitialized) {
                    SafeReadMemory(p2CharPtr + AI_CONTROL_FLAG_OFFSET, &p2AIFlag, sizeof(uint32_t));

                    bool p2AIControlled = (p2AIFlag != 0);

                    if (p2AIControlled != lastP2AIControlled && p2CharacterInitialized) {
                        LogOut("[PRACTICE_PATCH] P2 AI control state changed: " +
                               std::string(lastP2AIControlled ? "AI" : "Human") + " -> " +
                               std::string(p2AIControlled ? "AI" : "Human"), true);
                        lastP2AIControlled = p2AIControlled;
                    }

                    uint32_t desiredAIFlag = p2CpuControlled ? 1u : 0u;
                    if (p2AIFlag != desiredAIFlag) {
                        // Never write outside Match phase.
                        if (GetCurrentGamePhase() != GamePhase::Match) {
                            lastP2AIControlled = (p2AIFlag != 0);
                        } else
                        
                        if (g_p2ControlOverridden) {
                            // During auto-action/macro control override, do not fight temporary human control.
                            if (detailedLogging.load()) {
                                LogOut("[PRACTICE_PATCH] Skip AI sync (override active)", true);
                            }
                        } else {
                            uint32_t before = 0xFFFFFFFF, after = 0xFFFFFFFF;
                            SafeReadMemory(p2CharPtr + AI_CONTROL_FLAG_OFFSET, &before, sizeof(before));
                            bool okWrite = SafeWriteMemory(p2CharPtr + AI_CONTROL_FLAG_OFFSET, &desiredAIFlag, sizeof(uint32_t));
                            SafeReadMemory(p2CharPtr + AI_CONTROL_FLAG_OFFSET, &after, sizeof(after));
                            std::ostringstream oss; oss << "[AUDIT][AI] PracticeSync P2 @0x" << std::hex << (p2CharPtr + AI_CONTROL_FLAG_OFFSET)
                                                        << " " << std::dec << before << "->" << after
                                                        << " (want " << desiredAIFlag << ")" << (okWrite?"":" (fail)");
                            LogOut(oss.str(), true);
                        }
                    }
                }

                // The P2 CPU flag is owned by UI actions and SwitchPlayers; never auto-toggle here.
            }
            }
        }
            
            if (cycleCount % 30 == 0) {
                LogOut("[PRACTICE_PATCH] Performing periodic state check", detailedLogging.load());
                DumpPracticeModeState();
                
                if (cycleCount % 600 == 0) {
                    ScanForPotentialAIFlags();
                }
            }
            
            if (cycleCount % 10 == 0) {
                LogPlayerInputsInPracticeMode();
            }
        }
        
        Sleep(500); // 0.5 s poll
    }
}

std::string FormatHexAddress(uintptr_t address) {
    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << address;
    return ss.str();
}

void DumpPracticeModeState() {
    LogOut("[PRACTICE_PATCH] --------- Practice Mode Debug Dump ---------", true);
    
    uintptr_t efzBase = GetEFZBase();
    if (!efzBase) {
        LogOut("[PRACTICE_PATCH] Cannot dump state: Failed to get EFZ base address", true);
        return;
    }
    
    LogOut("[PRACTICE_PATCH] EFZ base address: " + FormatHexAddress(efzBase), true);
    
    uintptr_t gameStatePtr = GetGameStatePtr();
    if (!gameStatePtr) {
        LogOut("[PRACTICE_PATCH] Cannot dump state: Failed to resolve game state pointer", true);
        return;
    }
    
    LogOut("[PRACTICE_PATCH] Game state pointer: " + FormatHexAddress(gameStatePtr), true);
    
    uint8_t gameMode = 0;
    if (SafeReadMemory(gameStatePtr + GAME_MODE_OFFSET, &gameMode, sizeof(uint8_t))) {
        LogOut("[PRACTICE_PATCH] Current game mode value: " + std::to_string(gameMode), true);
    } else {
        LogOut("[PRACTICE_PATCH] Failed to read game mode", true);
    }
    
    uint8_t p2CpuFlag = 0;
    if (SafeReadMemory(gameStatePtr + P2_CPU_FLAG_OFFSET, &p2CpuFlag, sizeof(uint8_t))) {
        LogOut("[PRACTICE_PATCH] P2 CPU control flag: " + std::to_string(p2CpuFlag) + 
                " (" + (p2CpuFlag ? "CPU controlled" : "Human controlled") + ")", true);
    } else {
        LogOut("[PRACTICE_PATCH] Failed to read P2 CPU control flag", true);
    }
    
    uintptr_t p2CharPtr = GetPlayerBase(2);
    if (p2CharPtr) {
        LogOut("[PRACTICE_PATCH] P2 character pointer: " + FormatHexAddress(p2CharPtr), true);
        
        uint32_t aiFlag = 0;
        if (SafeReadMemory(p2CharPtr + AI_CONTROL_FLAG_OFFSET, &aiFlag, sizeof(uint32_t))) {
            LogOut("[PRACTICE_PATCH] P2 AI control flag: " + std::to_string(aiFlag) + 
                    " (" + (aiFlag ? "AI controlled" : "Player controlled") + ")", true);
        } else {
            LogOut("[PRACTICE_PATCH] Failed to read P2 AI control flag", true);
        }
    } else {
        LogOut("[PRACTICE_PATCH] Failed to read Player 2 character pointer", true);
    }
    
    LogOut("[PRACTICE_PATCH] --------- End of Debug Dump ---------", true);
}

std::atomic<bool> g_monitorInputs(true);
uint8_t lastP1Input = 0;
uint8_t lastP2Input = 0;

void LogPlayerInputsInPracticeMode() {
    uint8_t p1Input = GetPlayerInputs(1);
    uint8_t p2Input = GetPlayerInputs(2);
    
    if (p1Input != lastP1Input && p1Input != 0) {
        LogOut("[INPUT] P1: 0x" + std::to_string(p1Input) + " (" + DecodeInputMask(p1Input) + ")", true);
    }
    
    if (p2Input != lastP2Input && p2Input != 0) {
        LogOut("[INPUT] P2: 0x" + std::to_string(p2Input) + " (" + DecodeInputMask(p2Input) + ")", true);
    }
    
    lastP1Input = p1Input;
    lastP2Input = p2Input;
}

std::string GetDirectionName(uint8_t inputBits) {
    if (inputBits == 0) return "NONE";
    
    std::string result;
    
    if (inputBits & INPUT_UP)    result += "UP ";
    if (inputBits & INPUT_DOWN)  result += "DOWN ";
    if (inputBits & INPUT_LEFT)  result += "LEFT ";
    if (inputBits & INPUT_RIGHT) result += "RIGHT ";
    if (inputBits & INPUT_A)     result += "A ";
    if (inputBits & INPUT_B)     result += "B ";
    if (inputBits & INPUT_C)     result += "C ";
    if (inputBits & INPUT_D)     result += "D ";
    
    if (!result.empty()) {
        result.pop_back();
    }
    
    return result;
}

bool DisablePlayer2InPracticeMode() {
    // Writing P2 CPU/AI flags on one peer of an online match desyncs it; Practice context only.
    if (!IsPracticeContext()) return false;

    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    if (!IsPracticeContext()) return false;
    KaoriRecoilDuck::Cancel(2, "Practice restored P2 AI control");
    CancelAutoActionChargeFollowup(2, "Practice restored P2 AI control");
    CancelP2AutoActionMotionTransaction("Practice restored P2 AI control");
    if (IsP2AutoActionMotionTransactionActive()) return false;

    LogOut("[PRACTICE_PATCH] Attempting to disable Player 2 controls in Practice mode...", true);

    // After a Revival side-switch P2 can be the local human side; never force it back to AI.
    if (GetCurrentGameMode() == GameMode::Practice && GetCurrentGamePhase() == GamePhase::Match) {
        const int localSide = SwitchPlayers::GetLocalSide();
        if (localSide == 1) {
            LogOut("[PRACTICE_PATCH] DisableP2Control: Skipping CPU/AI reset because P2 is the current local side", true);
            return true;
        }
    }

    uintptr_t gameStatePtr = GetGameStatePtr();
    if (!gameStatePtr) {
        LogOut("[PRACTICE_PATCH] Failed to resolve game state pointer", true);
        return false;
    }

    uint8_t cpuControlled = 1;
    if (!SafeWriteMemory(gameStatePtr + P2_CPU_FLAG_OFFSET, &cpuControlled, sizeof(uint8_t))) {
        LogOut("[PRACTICE_PATCH] Failed to restore Player 2 CPU flag", true);
        return false;
    }
    LogOut("[PRACTICE_PATCH] Restored P2 CPU control flag to 1 (CPU controlled)", true);

    uintptr_t p2CharPtr = GetPlayerBase(2);
    if (p2CharPtr) {
        uint32_t aiControlFlag = 1;
        if (!SafeWriteMemory(p2CharPtr + AI_CONTROL_FLAG_OFFSET, &aiControlFlag, sizeof(uint32_t))) {
            LogOut("[PRACTICE_PATCH] Failed to restore Player 2 AI control flag", true);
            return false;
        }
        LogOut("[PRACTICE_PATCH] Restored P2 AI control flag to 1 (AI controlled)", true);
    } else {
        LogOut("[PRACTICE_PATCH] Player 2 character not initialized yet, skipping AI flag restore", true);
        return false;
    }

    DumpPracticeModeState();
    return true;
}

void EnsureDefaultControlFlagsOnMatchStart() {
    // Only modify control flags in offline Practice mode
    if (GetCurrentGameMode() != GameMode::Practice) return;
    if (IsNetplaySuspendActive()) return;

    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    if (IsNetplaySuspendActive()) return;
    KaoriRecoilDuck::Cancel(2, "Practice match-start controller reset");
    CancelAutoActionChargeFollowup(2, "Practice match-start controller reset");
    CancelP2AutoActionMotionTransaction("Practice match-start controller reset");
    if (IsP2AutoActionMotionTransactionActive()) return;
    
    uintptr_t gameStatePtr = GetGameStatePtr();
    if (gameStatePtr) {
        uint8_t p2Cpu = 1;
        if (SafeWriteMemory(gameStatePtr + P2_CPU_FLAG_OFFSET, &p2Cpu, sizeof(uint8_t))) {
            LogOut("[PRACTICE_PATCH] MatchStart: P2 CPU flag set to 1 (CPU)", true);
        } else {
            LogOut("[PRACTICE_PATCH] MatchStart: Failed to set P2 CPU flag", true);
        }
    } else {
        LogOut("[PRACTICE_PATCH] MatchStart: Failed to read game state ptr", true);
    }

    uintptr_t p1CharPtr = GetPlayerBase(1);
    uintptr_t p2CharPtr = GetPlayerBase(2);

    if (p1CharPtr) {
        uint32_t p1Ai = 0;
        if (SafeWriteMemory(p1CharPtr + AI_CONTROL_FLAG_OFFSET, &p1Ai, sizeof(uint32_t))) {
            LogOut("[PRACTICE_PATCH] MatchStart: P1 AI flag set to 0 (Player)", true);
        } else {
            LogOut("[PRACTICE_PATCH] MatchStart: Failed to set P1 AI flag", true);
        }
    } else {
        LogOut("[PRACTICE_PATCH] MatchStart: P1 character pointer invalid", true);
    }

    if (p2CharPtr) {
        uint32_t p2Ai = 1;
        if (SafeWriteMemory(p2CharPtr + AI_CONTROL_FLAG_OFFSET, &p2Ai, sizeof(uint32_t))) {
            LogOut("[PRACTICE_PATCH] MatchStart: P2 AI flag set to 1 (AI)", true);
        } else {
            LogOut("[PRACTICE_PATCH] MatchStart: Failed to set P2 AI flag", true);
        }
    } else {
        LogOut("[PRACTICE_PATCH] MatchStart: P2 character pointer invalid", true);
    }

    // GUI_POS(+0x24): 1 = P1 local/human, 0 = P2 local; default enforcement makes P1 human.
    PauseIntegration::EnsurePracticePointerCapture();
    void* practice = PauseIntegration::GetPracticeControllerPtr();
    if (practice && EFZ_SupportsNativePracticeSideSwitch()) {
        uint8_t guiPos = 1u; // P1
        bool okWrite = SafeWriteMemory((uintptr_t)practice + PRACTICE_OFF_GUI_POS, &guiPos, sizeof(guiPos));
        uint8_t verify = 0xFF; SafeReadMemory((uintptr_t)practice + PRACTICE_OFF_GUI_POS, &verify, sizeof(verify));
        std::ostringstream oss; oss << "[PRACTICE_PATCH] MatchStart: GUI_POS(+0x24) set to " << (int)verify << (okWrite?"":" (fail)");
        LogOut(oss.str(), true);
    } else if (EFZ_SupportsNativePracticeSideSwitch()) {
        LogOut("[PRACTICE_PATCH] MatchStart: Practice controller not yet confirmed, GUI_POS not updated", true);
    }
}

bool GetPracticeAutoBlockEnabled(bool &enabledOut) {
    enabledOut = false;
    if (GetCurrentGameMode() != GameMode::Practice) return false;
    uintptr_t gs = GetGameStatePtr(); if (!gs) return false;
    uint32_t val = 0; if (!SafeReadMemory(gs + PRACTICE_AUTO_BLOCK_OFFSET, &val, sizeof(val))) return false;
    enabledOut = (val != 0);
    return true;
}

bool SetPracticeAutoBlockEnabled(bool enabled, const char* reason) {
    std::lock_guard<std::mutex> lk(g_autoBlockWriteMutex);
    if (GetCurrentGameMode() != GameMode::Practice) return false;
    if (IsNetplaySuspendActive()) return false;
    uintptr_t gs = GetGameStatePtr(); if (!gs) return false;
    uint32_t val = enabled ? 1u : 0u;
    uint32_t cur = 0;
    SafeReadMemory(gs + PRACTICE_AUTO_BLOCK_OFFSET, &cur, sizeof(cur));
    if (cur == val) {
        // Count no-op setter calls too, or the tutorial compare-and-restore undoes an intentional same-value set.
        g_autoBlockWriteGeneration.fetch_add(1, std::memory_order_release);
        return true;
    }
    bool ok = SafeWriteMemory(gs + PRACTICE_AUTO_BLOCK_OFFSET, &val, sizeof(val));
    if (ok) {
        g_autoBlockWriteGeneration.fetch_add(1, std::memory_order_release);
        std::ostringstream oss;
        oss << "Auto-Block " << (enabled ? "ON" : "OFF")
            << " (was " << (cur ? "ON" : "OFF") << ")";
        if (reason && *reason) {
            oss << " [reason: " << reason << "]";
        }
        LogOut(std::string("[PRACTICE_PATCH] ") + oss.str(), true);
    }
    return ok;
}

uint64_t GetPracticeAutoBlockWriteGeneration() {
    return g_autoBlockWriteGeneration.load(std::memory_order_acquire);
}

bool RestorePracticeAutoBlockIfUnchanged(bool enabled,
                                         uint64_t expectedGeneration,
                                         bool expectedCurrent,
                                         const char* reason) {
    std::lock_guard<std::mutex> lk(g_autoBlockWriteMutex);
    if (g_autoBlockWriteGeneration.load(std::memory_order_relaxed) != expectedGeneration)
        return false;
    if (GetCurrentGameMode() != GameMode::Practice || IsNetplaySuspendActive()) return false;
    uintptr_t gs = GetGameStatePtr(); if (!gs) return false;
    uint32_t cur = 0;
    if (!SafeReadMemory(gs + PRACTICE_AUTO_BLOCK_OFFSET, &cur, sizeof(cur)) ||
        (cur != 0) != expectedCurrent) return false;
    const uint32_t val = enabled ? 1u : 0u;
    if (cur == val) return true;
    if (!SafeWriteMemory(gs + PRACTICE_AUTO_BLOCK_OFFSET, &val, sizeof(val))) return false;
    g_autoBlockWriteGeneration.fetch_add(1, std::memory_order_release);
    std::ostringstream oss;
    oss << "Auto-Block " << (enabled ? "ON" : "OFF")
        << " (conditional tutorial restore)";
    if (reason && *reason) oss << " [reason: " << reason << "]";
    LogOut(std::string("[PRACTICE_PATCH] ") + oss.str(), true);
    return true;
}

bool GetPracticeBlockMode(int &modeOut) {
    modeOut = 0;
    if (GetCurrentGameMode() != GameMode::Practice) return false;
    uintptr_t gs = GetGameStatePtr(); if (!gs) return false;
    uint8_t v = 0; if (!SafeReadMemory(gs + PRACTICE_BLOCK_MODE_OFFSET, &v, sizeof(v))) return false;
    modeOut = (int)v; if (modeOut < 0 || modeOut > 2) modeOut = 0;
    return true;
}

bool SetPracticeBlockMode(int mode) {
    if (GetCurrentGameMode() != GameMode::Practice) return false;
    if (IsNetplaySuspendActive()) return false;
    if (mode < 0) mode = 0; if (mode > 2) mode = 2;
    uintptr_t gs = GetGameStatePtr(); if (!gs) return false;
    uint8_t current = 0; SafeReadMemory(gs + PRACTICE_BLOCK_MODE_OFFSET, &current, sizeof(current));
    uint8_t v = (uint8_t)mode;
    if (current == v) return true;
    bool ok = SafeWriteMemory(gs + PRACTICE_BLOCK_MODE_OFFSET, &v, sizeof(v));
    if (ok) {
        // Block mode: 0=stand, 1=first, 2=crouch.
        if (v == 0) {
            LogOut("[PRACTICE_PATCH] Block Stance -> Stand", detailedLogging.load());
            WriteP2BlockStance(0);
        } else if (v == 2) {
            LogOut("[PRACTICE_PATCH] Block Stance -> Crouch", detailedLogging.load());
            WriteP2BlockStance(1);
        } else {
            LogOut("[PRACTICE_PATCH] Block Mode -> First", detailedLogging.load());
        }
    }
    return ok;
}

bool CyclePracticeBlockMode() {
    int m = 0; if (!GetPracticeBlockMode(m)) return false;
    m = (m + 1) % 3; return SetPracticeBlockMode(m);
}

static std::atomic<int> g_dummyAutoBlockMode{0};
static std::atomic<bool> g_firstEventSeen{false};
// Detect stun via moveIDs; guardstun/hitstun counters are unreliable.
static std::atomic<bool> g_abWindowActive{false};
static std::atomic<unsigned long long> g_abWindowDeadlineMs{0};
static std::atomic<bool> g_adaptiveStance{false};
static std::atomic<bool> g_adaptiveForceTick{false};
static std::atomic<bool> g_abOverrideActive{false}; // when false, follow the game's autoblock flag
static std::atomic<bool> g_desiredAbOn{false};
// While set, MonitorDummyAutoBlock must not write/sync +4936 (Random Block owns it).
static std::atomic<bool> g_externalAbController{false};

static const char* GetDabModeName(int mode) {
    switch (mode) {
        case DAB_None:                 return "None";
        case DAB_All:                  return "All";
        case DAB_FirstHitThenOff:      return "FirstHitThenOff";
        case DAB_EnableAfterFirstHit:  return "EnableAfterFirstHit";
        case DAB_Adaptive:             return "Adaptive (deprecated)";
        default:                       return "Unknown";
    }
}

// level: 0=None, 1=High, 2=Low, 3=Any; blockable = GuardProps present.
static bool SampleAttackerFrameFlags(int attacker /*1=P1, 2=P2*/, int &level, bool &blockable,
                                    uint16_t* outAtk=nullptr, uint16_t* outHit=nullptr, uint16_t* outGrd=nullptr,
                                    uint16_t* outState=nullptr, uint16_t* outFrame=nullptr,
                                    int* outNextLevel=nullptr, bool* outNextBlockable=nullptr,
                                    int* outNext2Level=nullptr, bool* outNext2Blockable=nullptr) {
    static uintptr_t s_lastPBase[3]      = {0, 0, 0};
    static uintptr_t s_lastAnimTab[3]    = {0, 0, 0};
    static uint16_t  s_lastState[3]      = {0xFFFF, 0xFFFF, 0xFFFF};
    static uintptr_t s_lastFramesPtr[3]  = {0, 0, 0};
    static uint32_t  s_cacheGeneration   = 0;

    const uint32_t generationNow = GetRuntimeLifecycleGeneration();
    if (s_cacheGeneration != generationNow) {
        for (int i = 0; i < 3; ++i) {
            s_lastPBase[i] = 0;
            s_lastAnimTab[i] = 0;
            s_lastState[i] = 0xFFFF;
            s_lastFramesPtr[i] = 0;
        }
        if (detailedLogging.load()) {
            LogOut("[PRACTICE_PATCH] Reset attacker frame sampler caches for lifecycle generation "
                + std::to_string(generationNow), true);
        }
        s_cacheGeneration = generationNow;
    }

    uintptr_t pBase = 0;
    if (attacker == 1 || attacker == 2) {
        pBase = GetPlayerBase(attacker);
    }
    if (!pBase) return false;
    uint16_t state = 0, frame = 0; uintptr_t animTab = 0, framesPtr = 0, frameBlock = 0;
    const PerFrameSample &sample = GetCurrentPerFrameSample();
    if (attacker == 1) {
        state = static_cast<uint16_t>(sample.moveID1);
    } else if (attacker == 2) {
        state = static_cast<uint16_t>(sample.moveID2);
    } else {
        if (!SafeReadMemory(pBase + MOVE_ID_OFFSET, &state, sizeof(state))) return false;
    }
    if (!SafeReadMemory(pBase + CURRENT_FRAME_INDEX_OFFSET, &frame, sizeof(frame))) return false;
    if (!SafeReadMemory(pBase + ANIM_TABLE_OFFSET, &animTab, sizeof(animTab))) return false;
    if (!animTab) return false;

    if (s_lastPBase[attacker] != pBase || s_lastAnimTab[attacker] != animTab) {
        s_lastPBase[attacker] = pBase;
        s_lastAnimTab[attacker] = animTab;
        s_lastState[attacker] = 0xFFFF;
        s_lastFramesPtr[attacker] = 0;
    }
    if (s_lastState[attacker] != state || s_lastFramesPtr[attacker] == 0) {
        uintptr_t entryAddr = animTab + (static_cast<uintptr_t>(state) * ANIM_ENTRY_STRIDE) + ANIM_ENTRY_FRAMES_PTR_OFFSET;
        if (!SafeReadMemory(entryAddr, &framesPtr, sizeof(framesPtr)) || !framesPtr) return false;
        s_lastState[attacker] = state;
        s_lastFramesPtr[attacker] = framesPtr;
    } else {
        framesPtr = s_lastFramesPtr[attacker];
    }
    frameBlock = framesPtr + (static_cast<uintptr_t>(frame) * FRAME_BLOCK_STRIDE);
    uint16_t atk=0, hit=0, grd=0; if (!SafeReadMemory(frameBlock + FRAME_ATTACK_PROPS_OFFSET, &atk, sizeof(atk))) return false;
    SafeReadMemory(frameBlock + FRAME_HIT_PROPS_OFFSET, &hit, sizeof(hit));
    SafeReadMemory(frameBlock + FRAME_GUARD_PROPS_OFFSET, &grd, sizeof(grd));
    if (outAtk) *outAtk = atk; if (outHit) *outHit = hit; if (outGrd) *outGrd = grd;
    if (outState) *outState = state; if (outFrame) *outFrame = frame;
    bool isHigh = (atk & 0x1) != 0;
    bool isLow  = (atk & 0x2) != 0;
    level = isHigh && isLow ? 3 : (isHigh ? 1 : (isLow ? 2 : 0));
    // blockable := GuardProps present.
    blockable = (grd != 0);
    // A guardable window without HIGH/LOW counts as ANY.
    if (level == 0 && blockable) {
        level = 3; // ANY
    }

    if (outNextLevel || outNextBlockable || outNext2Level || outNext2Blockable) {
        int nextLevel = -1; bool nextBlk = false;
        uintptr_t nextFrameBlock = framesPtr + (static_cast<uintptr_t>(frame + 1) * FRAME_BLOCK_STRIDE);
        uint16_t nAtk=0, nHit=0, nGrd=0;
        if (SafeReadMemory(nextFrameBlock + FRAME_ATTACK_PROPS_OFFSET, &nAtk, sizeof(nAtk))) {
            SafeReadMemory(nextFrameBlock + FRAME_HIT_PROPS_OFFSET, &nHit, sizeof(nHit));
            SafeReadMemory(nextFrameBlock + FRAME_GUARD_PROPS_OFFSET, &nGrd, sizeof(nGrd));
            bool nHigh = (nAtk & 0x1) != 0;
            bool nLow  = (nAtk & 0x2) != 0;
            nextLevel = nHigh && nLow ? 3 : (nHigh ? 1 : (nLow ? 2 : 0));
            nextBlk = (nGrd != 0);
            if (nextLevel == 0 && nextBlk) nextLevel = 3;
        }
        int next2Level = -1; bool next2Blk = false;
        uintptr_t next2FrameBlock = framesPtr + (static_cast<uintptr_t>(frame + 2) * FRAME_BLOCK_STRIDE);
        uint16_t n2Atk=0, n2Hit=0, n2Grd=0;
        if (SafeReadMemory(next2FrameBlock + FRAME_ATTACK_PROPS_OFFSET, &n2Atk, sizeof(n2Atk))) {
            SafeReadMemory(next2FrameBlock + FRAME_HIT_PROPS_OFFSET, &n2Hit, sizeof(n2Hit));
            SafeReadMemory(next2FrameBlock + FRAME_GUARD_PROPS_OFFSET, &n2Grd, sizeof(n2Grd));
            bool n2High = (n2Atk & 0x1) != 0;
            bool n2Low  = (n2Atk & 0x2) != 0;
            next2Level = n2High && n2Low ? 3 : (n2High ? 1 : (n2Low ? 2 : 0));
            next2Blk = (n2Grd != 0);
            if (next2Level == 0 && next2Blk) next2Level = 3;
        }
        if (outNextLevel) *outNextLevel = nextLevel;
        if (outNextBlockable) *outNextBlockable = nextBlk;
        if (outNext2Level) *outNext2Level = next2Level;
        if (outNext2Blockable) *outNext2Blockable = next2Blk;
    }
    return true;
}

// Sync path: never engages override or writes baselines.
static void SetDummyAutoBlockModeFromSync(int mode) {
    if (mode < 0) mode = 0; if (mode > 4) mode = 4;
    // Deprecated DAB_Adaptive maps to All.
    if (mode == DAB_Adaptive) { mode = DAB_All; }
    int prev = g_dummyAutoBlockMode.load();
    g_dummyAutoBlockMode.store(mode);
    if (prev != mode) {
        std::ostringstream oss; oss << "Sync: mode " << GetDabModeName(prev) << " -> " << GetDabModeName(mode);
        LogOut(std::string("[DUMMY_AB] ") + oss.str(), true);
    }
    ResetDummyAutoBlockState();
}

// Tunables (ms)
static constexpr unsigned long long AB_DELAY_FIRST_HIT_THEN_OFF_MS = 2000ULL;
static constexpr unsigned long long AB_DELAY_AFTER_FIRST_HIT_MS    = 1000ULL;

void SetDummyAutoBlockMode(int mode) {
    if (mode < 0) mode = 0; if (mode > 4) mode = 4;
    if (mode == DAB_Adaptive) { mode = DAB_All; }
    int prev = g_dummyAutoBlockMode.load();
    g_dummyAutoBlockMode.store(mode);
    if (prev != mode) {
        std::ostringstream oss; oss << "Set: mode " << GetDabModeName(prev) << " -> " << GetDabModeName(mode);
        LogOut(std::string("[DUMMY_AB] ") + oss.str(), true);
    }
    ResetDummyAutoBlockState();
    // Override only for the two custom modes; None/All keep the native F7 authoritative.
    bool wantOverride = (mode == DAB_FirstHitThenOff) || (mode == DAB_EnableAfterFirstHit);
    g_abOverrideActive.store(wantOverride);
    switch (mode) {
        case DAB_All:
            SetPracticeAutoBlockEnabled(true, "SetDummyAutoBlockMode: DAB_All baseline");
            break;
        case DAB_None:
            SetPracticeAutoBlockEnabled(false, "SetDummyAutoBlockMode: DAB_None baseline");
            break;
        case DAB_FirstHitThenOff:
            SetPracticeAutoBlockEnabled(true, "SetDummyAutoBlockMode: DAB_FirstHitThenOff baseline");
            break;
        case DAB_EnableAfterFirstHit:
            SetPracticeAutoBlockEnabled(false, "SetDummyAutoBlockMode: DAB_EnableAfterFirstHit baseline");
            break;
        case DAB_Adaptive:
            SetPracticeAutoBlockEnabled(true, "SetDummyAutoBlockMode: DAB_Adaptive->All baseline");
            break;
    }
}

int GetDummyAutoBlockMode() { return g_dummyAutoBlockMode.load(); }

bool SyncAutoBlockModeFromGameFlag(bool clearOverride) {
    if (GetCurrentGameMode() != GameMode::Practice) return false;
    uintptr_t gs = GetGameStatePtr(); if (!gs) return false;
    uint32_t f = 0; if (!SafeReadMemory(gs + PRACTICE_AUTO_BLOCK_OFFSET, &f, sizeof(f))) return false;
    SetDummyAutoBlockModeFromSync((f != 0) ? DAB_All : DAB_None);
    if (clearOverride) {
        g_abOverrideActive.store(false);
    }
    return true;
}

void ResetDummyAutoBlockState() {
    g_firstEventSeen.store(false);
    g_abWindowActive.store(false);
    g_abWindowDeadlineMs.store(0);
    g_desiredAbOn.store(false);
}

void SetAdaptiveStanceEnabled(bool enabled) {
    g_adaptiveStance.store(enabled);
    if (enabled) {
        g_adaptiveForceTick.store(true);
    }
}
bool GetAdaptiveStanceEnabled() { return g_adaptiveStance.load(); }

bool GetCurrentDesiredAutoBlockOn(bool &onOut) {
    if (GetCurrentGameMode() != GameMode::Practice) { onOut = false; return false; }
    onOut = g_desiredAbOn.load();
    return true;
}

void SetExternalAutoBlockController(bool enabled) {
    g_externalAbController.store(enabled);
}

bool AcquireTutorialAutoBlockController(uint64_t& tokenOut) {
    tokenOut = 0;
    uint64_t expected = 0;
    uint64_t token = g_nextTutorialAutoBlockToken.fetch_add(1, std::memory_order_relaxed);
    if (token == 0) token = g_nextTutorialAutoBlockToken.fetch_add(1, std::memory_order_relaxed);
    if (!g_tutorialAutoBlockToken.compare_exchange_strong(
            expected, token, std::memory_order_acq_rel)) return false;
    tokenOut = token;
    return true;
}

void ReleaseTutorialAutoBlockController(uint64_t token) {
    if (token == 0) return;
    uint64_t expected = token;
    (void)g_tutorialAutoBlockToken.compare_exchange_strong(
        expected, 0, std::memory_order_acq_rel);
}

static bool ReadP2BlockFields(uint8_t &dirOut, uint8_t &stanceOut) {
    uintptr_t p2 = GetPlayerBase(SwitchPlayers::GetRemotePlayerIndex()); if (!p2) return false;
    uint8_t dir=0, stance=0;
    SafeReadMemory(p2 + 392, &dir, sizeof(dir));
    SafeReadMemory(p2 + 393, &stance, sizeof(stance));
    dirOut = dir; stanceOut = stance;
    return true;
}

static bool WriteP2BlockStance(uint8_t stance) {
    const int dummyPlayer = SwitchPlayers::GetRemotePlayerIndex();
    if (!TryAcquireImmediateInputWriteLease(dummyPlayer)) return false;
    uintptr_t p2 = GetPlayerBase(dummyPlayer);
    const bool ok = p2 && SafeWriteMemory(p2 + 393, &stance, sizeof(stance));
    ReleaseImmediateInputWriteLease(dummyPlayer);
    return ok;
}

// guardLevel: 0=None, 1=High, 2=Low, 3=Any
static void AssistP2DirectionForStance(uint8_t desiredStance, bool wantGuard, int guardLevel) {
    const int dummyPlayer = SwitchPlayers::GetRemotePlayerIndex();
    if (!TryAcquireImmediateInputWriteLease(dummyPlayer)) return;
    uintptr_t p2 = GetPlayerBase(dummyPlayer);
    if (!p2) {
        ReleaseImmediateInputWriteLease(dummyPlayer);
        return;
    }
    uint8_t dir = 0; SafeReadMemory(p2 + 392, &dir, sizeof(dir));
    if (desiredStance == 1) {
        dir &= ~INPUT_UP;
        dir |= INPUT_DOWN;
    } else {
        dir &= ~INPUT_DOWN;
    }

    if (wantGuard) {
        bool facingRight = GetPlayerFacingDirection(dummyPlayer); // true = facing right
        uint8_t backBit = facingRight ? INPUT_LEFT : INPUT_RIGHT;
        uint8_t fwdBit  = facingRight ? INPUT_RIGHT : INPUT_LEFT;
        dir &= ~fwdBit;
        dir |= backBit;
        if (guardLevel == 2 || desiredStance == 1) {
            dir |= INPUT_DOWN;
            dir &= ~INPUT_UP;
        }
    }
    SafeWriteMemory(p2 + 392, &dir, sizeof(dir));
    ReleaseImmediateInputWriteLease(dummyPlayer);
}

static bool ReadPositions(double &p1Y, double &p2Y) {
    static uintptr_t s_p1YAddr = 0;
    static uintptr_t s_p2YAddr = 0;
    static int s_cacheCounter = 0;
    static uint32_t s_cacheGeneration = 0;

    uintptr_t base = GetEFZBase();
    if (!base) { s_p1YAddr = s_p2YAddr = 0; return false; }

    const uint32_t generationNow = GetRuntimeLifecycleGeneration();
    if (generationNow != s_cacheGeneration) {
        s_p1YAddr = 0;
        s_p2YAddr = 0;
        s_cacheCounter = 0;
        s_cacheGeneration = generationNow;
    }

    if (!s_p1YAddr || !s_p2YAddr || (++s_cacheCounter >= 192)) {
        s_p1YAddr = ResolvePointer(base, EFZ_BASE_OFFSET_P1, YPOS_OFFSET);
        s_p2YAddr = ResolvePointer(base, EFZ_BASE_OFFSET_P2, YPOS_OFFSET);
        s_cacheCounter = 0;
    }

    if (!s_p1YAddr || !s_p2YAddr) return false;

    bool ok1 = SafeReadMemory(s_p1YAddr, &p1Y, sizeof(p1Y));
    bool ok2 = SafeReadMemory(s_p2YAddr, &p2Y, sizeof(p2Y));
    if (!ok1 || !ok2) {
        if (!ok1) s_p1YAddr = 0;
        if (!ok2) s_p2YAddr = 0;
        return false;
    }
    return true;
}

static inline bool IsP2BlockingOrBlockstun(short moveId) {
    // 150..156 = standing/crouching/air guard + early guardstun.
    return (moveId >= 150 && moveId <= 156);
}
static inline bool IsP2InHitstun(short moveId) {
    // Use constants.h ranges for hitstun (standing/crouch/launch/sweep)
    if ((moveId >= STAND_HITSTUN_START && moveId <= STAND_HITSTUN_END) ||
        (moveId >= CROUCH_HITSTUN_START && moveId <= CROUCH_HITSTUN_END) ||
        (moveId >= LAUNCHED_HITSTUN_START && moveId <= LAUNCHED_HITSTUN_END) ||
        (moveId == SWEEP_HITSTUN)) {
        return true;
    }
    return false;
}
static inline bool DidP2JustBlockThisFrame(short prevMoveId, short currMoveId) {
    return (!IsP2BlockingOrBlockstun(prevMoveId) && IsP2BlockingOrBlockstun(currMoveId));
}

// Call from the frame monitor after move IDs are read.
void MonitorDummyAutoBlock(short p1MoveID, short p2MoveID, short prevP1MoveID, short prevP2MoveID) {
    if (GetCurrentGameMode() != GameMode::Practice) return;
    if (IsNetplaySuspendActive()) return;

    const int attackerPlayer = SwitchPlayers::GetLocalPlayerIndex();
    const int dummyPlayer = SwitchPlayers::GetRemotePlayerIndex();
    const short dummyMoveID = (dummyPlayer == 1) ? p1MoveID : p2MoveID;
    const short prevDummyMoveID = (dummyPlayer == 1) ? prevP1MoveID : prevP2MoveID;
    
    static GamePhase s_lastPhase = GamePhase::Unknown;
    GamePhase phaseNow = GetCurrentGamePhase();
    if (phaseNow != s_lastPhase) {
        if (phaseNow == GamePhase::CharacterSelect) {
            g_abOverrideActive.store(false);
            uintptr_t gs = GetGameStatePtr(); if (gs) {
                uint32_t f=0; if (SafeReadMemory(gs + PRACTICE_AUTO_BLOCK_OFFSET, &f, sizeof(f))) {
                    SetDummyAutoBlockModeFromSync((f!=0) ? DAB_All : DAB_None);
                }
            }
        }
        s_lastPhase = phaseNow;
    }
    int mode = g_dummyAutoBlockMode.load();
    // Transition log throttle (5s)
    static unsigned long long s_lastAbLog = 0;
    auto log_ab = [&](const std::string& msg){
        unsigned long long now = XPCompat::GetTickCount64Compat();
        if (now - s_lastAbLog >= 5000ULL) {
            LogOut("[DUMMY_AB] " + msg, true);
            s_lastAbLog = now;
        }
    };
    static bool s_lastAbOn = false;
    bool abOn = false;
    static bool s_pendingAbOff = false; // defer turning OFF until blockstun ends/actionable

    const bool justBlocked = DidP2JustBlockThisFrame(prevDummyMoveID, dummyMoveID);
    const bool hitNow = (!IsP2InHitstun(prevDummyMoveID) && IsP2InHitstun(dummyMoveID));
    auto isAllowedNeutral = [](short m){
        // Allowed MoveIDs: 0,1,2,3,4,7,8,9,13 (same as Continuous Recovery)
        return (m == 0 || m == 1 || m == 2 || m == 3 || m == 4 || m == 7 || m == 8 || m == 9 || m == 13);
    };
    const bool neutralNow = isAllowedNeutral(dummyMoveID);
    const bool transitionedToNeutral = (!isAllowedNeutral(prevDummyMoveID) && neutralNow);
    int neutralTimeoutMs = Config::GetSettings().autoBlockNeutralTimeoutMs;
    if (neutralTimeoutMs < 0) neutralTimeoutMs = 0;
    const unsigned long long curMs = XPCompat::GetTickCount64Compat();

    static int s_lastMode = -999;
    static bool s_waitForHitAfterBlock = false; // DAB_FirstHitThenOff
    static bool s_waitForBlockAfterHit = false; // DAB_EnableAfterFirstHit
    static unsigned long long s_neutralStartMs = 0ULL;
    if (mode != s_lastMode) {
        s_waitForHitAfterBlock = false;
        s_waitForBlockAfterHit = false;
        s_neutralStartMs = 0ULL;
        g_abWindowActive.store(false);
        {
            std::ostringstream oss; oss << "Mode changed: " << GetDabModeName(s_lastMode) << " -> "
                                        << GetDabModeName(mode) << "; resetting autoblock state machine";
            log_ab(oss.str());
        }
        s_lastMode = mode;
    }

    if (mode == DAB_FirstHitThenOff) {
        if (justBlocked) {
            abOn = false;
            s_waitForHitAfterBlock = true;
            s_neutralStartMs = 0ULL;
            log_ab("FirstHitThenOff: blocked -> autoblock OFF (waiting for hit or neutral)");
        } else if (s_waitForHitAfterBlock) {
            abOn = false;
            if (hitNow) {
                abOn = true;
                s_waitForHitAfterBlock = false;
                s_neutralStartMs = 0ULL;
                log_ab("FirstHitThenOff: got hit -> autoblock ON");
            } else {
                if (neutralTimeoutMs == 0) {
                    if (transitionedToNeutral) {
                        abOn = true;
                        s_waitForHitAfterBlock = false;
                        s_neutralStartMs = 0ULL;
                        log_ab("FirstHitThenOff: neutral edge -> autoblock ON");
                    }
                } else if (neutralNow) {
                    if (s_neutralStartMs == 0ULL) s_neutralStartMs = curMs;
                    if (curMs - s_neutralStartMs >= (unsigned long long)neutralTimeoutMs) {
                        abOn = true;
                        s_waitForHitAfterBlock = false;
                        s_neutralStartMs = 0ULL;
                        log_ab("FirstHitThenOff: neutral timeout -> autoblock ON");
                    }
                } else {
                    s_neutralStartMs = 0ULL;
                }
            }
        } else {
            abOn = true;
        }
    }
    else if (mode == DAB_EnableAfterFirstHit) {
        if (hitNow && !s_waitForBlockAfterHit) {
            abOn = true;
            s_waitForBlockAfterHit = true;
            s_neutralStartMs = 0ULL;
            log_ab("AfterFirstHit: hit detected -> autoblock ON (waiting for block)");
        }
        if (s_waitForBlockAfterHit) {
            abOn = true;
            if (justBlocked) {
                abOn = false;
                s_waitForBlockAfterHit = false;
                s_neutralStartMs = 0ULL;
                log_ab("AfterFirstHit: blocked -> autoblock OFF");
            } else {
                if (neutralTimeoutMs == 0) {
                    if (transitionedToNeutral) {
                        abOn = false;
                        s_waitForBlockAfterHit = false;
                        s_neutralStartMs = 0ULL;
                        log_ab("AfterFirstHit: neutral edge -> autoblock OFF");
                    }
                } else if (neutralNow) {
                    if (s_neutralStartMs == 0ULL) s_neutralStartMs = curMs;
                    if (curMs - s_neutralStartMs >= (unsigned long long)neutralTimeoutMs) {
                        abOn = false;
                        s_waitForBlockAfterHit = false;
                        s_neutralStartMs = 0ULL;
                        log_ab("AfterFirstHit: neutral timeout -> autoblock OFF");
                    }
                } else {
                    s_neutralStartMs = 0ULL;
                }
            }
        } else if (!hitNow) {
            abOn = false;
        }
    }

    else if (mode == DAB_All) {
        abOn = true;
    }
    else if (mode == DAB_None) {
        abOn = false;
    }
    else {
        abOn = false;
    }

    if (mode == DAB_FirstHitThenOff || mode == DAB_EnableAfterFirstHit) {
        // P2 blockstun counter decrements 3 per visual frame.
        short p2Blockstun = 0;
        {
            uintptr_t base = GetEFZBase();
            if (base) {
                p2Blockstun = GetBlockstunValue(base, dummyPlayer);
            }
        }
        const bool inGuardNow = IsP2BlockingOrBlockstun(dummyMoveID) || (p2Blockstun > 0);
        const PerFrameSample &dabSample = GetCurrentPerFrameSample();
        const bool actionableNow = (dummyPlayer == 1)
            ? (dabSample.moveID1 == dummyMoveID ? dabSample.actionable1 : IsActionable(dummyMoveID))
            : (dabSample.moveID2 == dummyMoveID ? dabSample.actionable2 : IsActionable(dummyMoveID));
        const bool leftGuardNow = (IsP2BlockingOrBlockstun(prevDummyMoveID) && !IsP2BlockingOrBlockstun(dummyMoveID));

        if (!abOn) {
            if (inGuardNow || !actionableNow) {
                s_pendingAbOff = true;
                abOn = true;
            }
        }
        if (s_pendingAbOff) {
            if ((leftGuardNow && actionableNow) || (!inGuardNow && actionableNow)) {
                abOn = false;
                s_pendingAbOff = false;
                if (detailedLogging.load()) {
                    LogOut("[DUMMY_AB] OFF on guard end edge (moveID)", true);
                }
            } else {
                abOn = true;
            }
        }
    } else {
        s_pendingAbOff = false;
    }

    // Published for RandomBlock.
    g_desiredAbOn.store(abOn);

    // Write +4936 only under override/custom modes and with no external controller.
    bool overrideEffective = g_abOverrideActive.load() || (mode == DAB_FirstHitThenOff) || (mode == DAB_EnableAfterFirstHit);
    const bool externalController = g_externalAbController.load() ||
        g_tutorialAutoBlockToken.load(std::memory_order_acquire) != 0;
    if (overrideEffective && !externalController && abOn != s_lastAbOn) {
        const char* why = nullptr;
        if (mode == DAB_FirstHitThenOff) {
            why = abOn ? "FirstHitThenOff: re-enable (hit/neutral)" : "FirstHitThenOff: disable (block)";
        } else if (mode == DAB_EnableAfterFirstHit) {
            why = abOn ? "EnableAfterFirstHit: enable (hit)" : "EnableAfterFirstHit: disable (block/neutral)";
        } else {
            why = "OverrideEffective write";
        }
        SetPracticeAutoBlockEnabled(abOn, why);
        s_lastAbOn = abOn;
    }

    // Watch +4936 at 1 Hz.
    static int s_lastAbFlag = -1; // -1 = unknown, otherwise 0/1
    static unsigned long long s_lastAbFlagCheckMs = 0;
    unsigned long long nowMs = XPCompat::GetTickCount64Compat();
    if (nowMs - s_lastAbFlagCheckMs >= 1000ULL) {
        uintptr_t gsWatch = GetGameStatePtr();
        if (gsWatch) {
            uint32_t flagVal = 0;
            if (SafeReadMemory(gsWatch + PRACTICE_AUTO_BLOCK_OFFSET, &flagVal, sizeof(flagVal))) {
                int curFlag = (flagVal != 0) ? 1 : 0;
                if (s_lastAbFlag == -1) {
                    s_lastAbFlag = curFlag;
                    if (!externalController) {
                        int curMode = g_dummyAutoBlockMode.load();
                        if (curMode == DAB_All && curFlag == 0) {
                            SetDummyAutoBlockModeFromSync(DAB_None);
                            LogOut("[DUMMY_AB] Sync(init): Game autoblock OFF -> mode set to None", true);
                        } else if (curMode == DAB_None && curFlag == 1) {
                            SetDummyAutoBlockModeFromSync(DAB_All);
                            LogOut("[DUMMY_AB] Sync(init): Game autoblock ON -> mode set to All", true);
                        }
                    }
                } else if (curFlag != s_lastAbFlag) {
                    if (!externalController) {
                        // Tutorial episodes are temporary; never rewrite the user's mode.
                        DirectDrawHook::AddMessage(curFlag ? "Block: ON" : "Block: OFF",
                                                   "SYSTEM",
                                                   curFlag ? RGB(100, 255, 100) : RGB(255, 255, 100),
                                                   1500,
                                                   0,
                                                   100);
                        int curMode = g_dummyAutoBlockMode.load();
                        if (curMode == DAB_All && curFlag == 0) {
                            SetDummyAutoBlockModeFromSync(DAB_None);
                            LogOut("[DUMMY_AB] Sync: Game autoblock OFF -> mode set to None", true);
                        } else if (curMode == DAB_None && curFlag == 1) {
                            SetDummyAutoBlockModeFromSync(DAB_All);
                            LogOut("[DUMMY_AB] Sync: Game autoblock ON -> mode set to All", true);
                        }
                    }
                    s_lastAbFlag = curFlag;
                }
            }
        }
        s_lastAbFlagCheckMs = nowMs;
    }

    bool gameFlagOn = false; {
        uintptr_t gs = GetGameStatePtr();
        if (gs) {
            uint32_t f=0; SafeReadMemory(gs + PRACTICE_AUTO_BLOCK_OFFSET, &f, sizeof(f));
            gameFlagOn = (f!=0);
        }
    }
    bool activeAb = overrideEffective ? abOn : gameFlagOn;
    static bool s_prevActiveAb = false;
    if (g_adaptiveStance.load() && activeAb) {
        // Skip while P2 is leased to human control for auto-actions.
        uintptr_t baseAI = GetPlayerBase(dummyPlayer);
        if (!baseAI) return;
        uint32_t aiFlag = 1; SafeReadMemory(baseAI + AI_CONTROL_FLAG_OFFSET, &aiFlag, sizeof(aiFlag));
        // A tutorial lease is for guarding only; adaptive stance (a stance memory write) must still run.
        if (aiFlag == 0 && g_tutorialAutoBlockToken.load(std::memory_order_acquire) == 0) {
            static int s_aiGateDbg = 0; if (detailedLogging.load() && (s_aiGateDbg++ & 0x3F) == 0) {
                LogOut("[ADAPTIVE] Skipping stance logic (P2 under player control)", true);
            }
            return;
        }
        static unsigned long long s_lastAdaptiveMs = 0;
        unsigned long long now = XPCompat::GetTickCount64Compat();
        const unsigned long long ADAPTIVE_INTERVAL_MS = 0ULL; // every frame
        bool due = (now - s_lastAdaptiveMs >= ADAPTIVE_INTERVAL_MS) || g_adaptiveForceTick.load();
        if (due) {
            int dummyLevel=-1; bool dummyBlk=false; int dummyNL=-1; bool dummyNB=false; int dummyN2L=-1; bool dummyN2B=false;
            uint16_t atkFlags=0, hitFlags=0, grdFlags=0, st=0, fr=0;
            SampleAttackerFrameFlags(attackerPlayer, dummyLevel, dummyBlk, &atkFlags, &hitFlags, &grdFlags, &st, &fr, &dummyNL, &dummyNB, &dummyN2L, &dummyN2B);

            // Determine base stance: ground=crouch, air=stand
            double p1Y=0.0, p2Y=0.0; bool haveCached = TryGetCachedYPositions(p1Y, p2Y, 200);
            if (!haveCached && ReadPositions(p1Y, p2Y)) {
                UpdatePositionCache(0.0, p1Y, 0.0, p2Y);
                haveCached = true;
            }
            bool attackerAir = haveCached ? ((attackerPlayer == 1 ? p1Y : p2Y) < 0.0) : false;

            // Character-specific overrides: certain grounded moves are overheads => stand
            static int s_p1CharID = -1;
            static int s_p1CharPlayer = 0;
            static unsigned long long s_lastCharRefresh = 0;
            if (attackerPlayer != s_p1CharPlayer || now - s_lastCharRefresh > 500ULL || s_p1CharID < 0) {
                uintptr_t base = GetEFZBase();
                char nameBuf[32] = {0};
                const uintptr_t attackerOffset = (attackerPlayer == 1) ? EFZ_BASE_OFFSET_P1 : EFZ_BASE_OFFSET_P2;
                SafeReadMemory(ResolvePointer(base, attackerOffset, CHARACTER_NAME_OFFSET), nameBuf, sizeof(nameBuf)-1);
                s_p1CharID = CharacterSettings::GetCharacterID(std::string(nameBuf));
                s_p1CharPlayer = attackerPlayer;
                s_lastCharRefresh = now;
            }

            uint8_t desiredStance = attackerAir ? 0 : 1; // 0=stand,1=crouch
            if (!attackerAir && s_p1CharID >= 0) {
                uintptr_t p1Base = GetPlayerBase(attackerPlayer);
                if (GuardOverrides::IsGroundedOverhead(s_p1CharID, static_cast<int>(st), p1Base)) {
                    desiredStance = 0;
                }
            }
            if (attackerAir && s_p1CharID >= 0) {
                if (GuardOverrides::IsAirborneLow(s_p1CharID, static_cast<int>(st))) {
                    desiredStance = 1;
                }
            }

            uint8_t curDir=0, curStance=0;
            if (ReadP2BlockFields(curDir, curStance)) {
                if (curStance != desiredStance) {
                    // Preserve blockstun + blocking MoveID so stance writes don't cut guard.
                    uintptr_t p2 = GetPlayerBase(dummyPlayer);
                    short prevBlk=0; short prevMove=0;
                    bool prevWasBlocking=false;
                    if (p2) {
                        SafeReadMemory(p2 + BLOCKSTUN_OFFSET, &prevBlk, sizeof(prevBlk));
                        SafeReadMemory(p2 + MOVE_ID_OFFSET, &prevMove, sizeof(prevMove));
                        prevWasBlocking = IsP2BlockingOrBlockstun(prevMove);
                    }
                    WriteP2BlockStance(desiredStance);
                    SetPracticeBlockMode(desiredStance == 0 ? 0 : 2);
                    if (p2) {
                        if (prevBlk > 0) {
                            SafeWriteMemory(p2 + BLOCKSTUN_OFFSET, &prevBlk, sizeof(prevBlk));
                        }
                        if (prevWasBlocking) {
                            short curMove=0; SafeReadMemory(p2 + MOVE_ID_OFFSET, &curMove, sizeof(curMove));
                            if (!IsP2BlockingOrBlockstun(curMove)) {
                                SafeWriteMemory(p2 + MOVE_ID_OFFSET, &prevMove, sizeof(prevMove));
                            }
                        }
                    }
                    if (detailedLogging.load()) {
                        // The engine re-derives stance (+393) from the dummy's neutral input each frame; log on transition change, max 1 Hz.
                        static uint8_t s_lastLogFrom = 0xFF, s_lastLogTo = 0xFF;
                        static unsigned long long s_lastStanceLogMs = 0;
                        const bool sameTransition =
                            (s_lastLogFrom == curStance && s_lastLogTo == desiredStance);
                        if (!sameTransition || (now - s_lastStanceLogMs) >= 1000ULL) {
                            s_lastLogFrom = curStance;
                            s_lastLogTo = desiredStance;
                            s_lastStanceLogMs = now;
                            std::ostringstream os;
                            os << "[ADAPTIVE] stance change: P2 "
                               << (curStance==1?"crouch":"stand") << " -> "
                               << (desiredStance==1?"crouch":"stand")
                               << " | move=" << st
                               << " frame=" << fr
                               << " atk=0x" << std::hex << std::uppercase << atkFlags
                               << " hit=0x" << hitFlags
                               << " grd=0x" << grdFlags
                               << std::dec
                               << " t=" << now;
                            LogOut(os.str(), true);
                        }
                    }
                }
            } else {
                uintptr_t p2 = GetPlayerBase(dummyPlayer);
                short prevBlk=0; short prevMove=0; bool prevWasBlocking=false;
                if (p2) {
                    SafeReadMemory(p2 + BLOCKSTUN_OFFSET, &prevBlk, sizeof(prevBlk));
                    SafeReadMemory(p2 + MOVE_ID_OFFSET, &prevMove, sizeof(prevMove));
                    prevWasBlocking = IsP2BlockingOrBlockstun(prevMove);
                }
                WriteP2BlockStance(desiredStance);
                SetPracticeBlockMode(desiredStance == 0 ? 0 : 2);
                if (p2) {
                    if (prevBlk > 0) {
                        SafeWriteMemory(p2 + BLOCKSTUN_OFFSET, &prevBlk, sizeof(prevBlk));
                    }
                    if (prevWasBlocking) {
                        short curMove=0; SafeReadMemory(p2 + MOVE_ID_OFFSET, &curMove, sizeof(curMove));
                        if (!IsP2BlockingOrBlockstun(curMove)) {
                            SafeWriteMemory(p2 + MOVE_ID_OFFSET, &prevMove, sizeof(prevMove));
                        }
                    }
                }
            }

            s_lastAdaptiveMs = now;
            g_adaptiveForceTick.store(false);
        }
    }
}

void MonitorDummyAutoBlock(const PerFrameSample& sample) {
    MonitorDummyAutoBlock(sample.moveID1, sample.moveID2, sample.prevMoveID1, sample.prevMoveID2);
}
