#include "../include/utils/switch_players.h"
#include "../include/core/logger.h"
#include "../include/core/memory.h"
#include "../include/game/game_state.h"
#include "../include/utils/pause_integration.h"
#include "../include/game/practice_offsets.h"
#include "../include/game/efzrevival_addrs.h"
#include "../include/input/input_motion.h"
#include "../include/input/input_hook.h"
#include "../include/input/input_core.h"
#include "../include/utils/utilities.h"
#include "../include/utils/network.h"
#include "../include/utils/debug_log.h"
#include <windows.h>
#include <atomic>
#include <sstream>
#include <iomanip>

namespace {
    // Side switch = engine control-map swap + game-state flags; only +0x24 is mirrored into
    // Practice (+0x24 = 1 drives P1, 0 drives P2).
    static inline bool RevivalDllPresent() {
        return GetModuleHandleA("EfzRevival.dll") != nullptr;
    }
    static inline bool UseRevivalDisplaySideField() {
        return RevivalDllPresent();
    }
    static bool SetRevivalPracticeDisplaySide(int desiredLocal, const char* reason, bool logMissingPractice = true) {
        if (!UseRevivalDisplaySideField()) {
            return true;
        }
        if (desiredLocal != 0 && desiredLocal != 1) {
            return false;
        }

        // +0x24 semantics: 1 = display/drive P1, 0 = display/drive P2; the ctor writes 1.
        // Input history uses ((+0x24 + 1) mod 2) * 0x74 + *(+0xC8).
        const int displaySideField = (desiredLocal == 0) ? 1 : 0;

        PauseIntegration::EnsurePracticePointerCapture();
        void* practiceRaw = PauseIntegration::GetPracticeControllerPtr();
        if (!practiceRaw) {
            practiceRaw = PauseIntegration::ResolvePracticeControllerPtrNow(reason ? reason : "display side sync");
        }

        static std::atomic<bool> s_loggedMissingDispPractice{false};
        if (!practiceRaw) {
            if (logMissingPractice && !s_loggedMissingDispPractice.exchange(true, std::memory_order_relaxed)) {
                std::ostringstream oss;
                oss << "[SWITCH][DISP][WARN] Practice pointer unavailable; display side not synced"
                    << " desiredLocal=" << desiredLocal
                    << " reason=" << (reason ? reason : "unspecified");
                LogOut(oss.str(), true);
            }
            return false;
        }
        s_loggedMissingDispPractice.store(false, std::memory_order_relaxed);

        uintptr_t fieldAddr = reinterpret_cast<uintptr_t>(practiceRaw) + PRACTICE_OFF_GUI_POS;
        int before = -1;
        int after = -1;
        const bool okBefore = SafeReadMemory(fieldAddr, &before, sizeof(before));
        const bool okWrite = SafeWriteMemory(fieldAddr, &displaySideField, sizeof(displaySideField));
        const bool okAfter = SafeReadMemory(fieldAddr, &after, sizeof(after));
        const bool ok = okWrite && okAfter && after == displaySideField;

        if (!ok || !okBefore || before != displaySideField) {
            std::ostringstream oss;
            oss << "[SWITCH][DISP] Display side sync"
                << " practice=0x" << std::hex << reinterpret_cast<uintptr_t>(practiceRaw)
                << " field=0x" << fieldAddr
                << std::dec
                << " desiredLocal=" << desiredLocal
                << " write=" << displaySideField
                << " before=" << (okBefore ? before : -1)
                << " after=" << (okAfter ? after : -1)
                << " ok=" << (ok ? 1 : 0)
                << " reason=" << (reason ? reason : "unspecified");
            LogOut(oss.str(), true);
        }

        return ok;
    }
    static std::atomic<bool> s_sidesAreSwapped{false};

    static inline void SetSwapFlagForLocalSide(int sideIdx) {
        s_sidesAreSwapped.store(sideIdx == 1, std::memory_order_relaxed);
    }
}

namespace SwitchPlayers {
    static bool ApplyEngineOnlySet(int desiredLocal) {
        if (desiredLocal != 0 && desiredLocal != 1) return false;
        if (GetCurrentGameMode() != GameMode::Practice || !IsMatchPhase()) return false;

        uintptr_t efzBase = GetEFZBase();
        if (!efzBase) return false;
        uintptr_t gameStatePtr = 0;
        if (!SafeReadMemory(efzBase + EFZ_BASE_OFFSET_GAME_STATE, &gameStatePtr, sizeof(gameStatePtr)) || !gameStatePtr)
            return false;

        uint8_t activePlayer = static_cast<uint8_t>(desiredLocal); // 0=P1,1=P2
        uint8_t p2CpuFlag    = (desiredLocal == 1) ? 0u : 1u;
        uint8_t p1CpuFlag    = (uint8_t)(1u - p2CpuFlag);

        bool okA = SafeWriteMemory(gameStatePtr + GAMESTATE_OFF_ACTIVE_PLAYER, &activePlayer, sizeof(activePlayer));
        bool ok1 = SafeWriteMemory(gameStatePtr + GAMESTATE_OFF_P1_CPU_FLAG, &p1CpuFlag, sizeof(p1CpuFlag));
        bool ok2 = SafeWriteMemory(gameStatePtr + GAMESTATE_OFF_P2_CPU_FLAG, &p2CpuFlag, sizeof(p2CpuFlag));

        if (desiredLocal == 1) {
            SetAIControlFlag(1, false);
            SetAIControlFlag(2, true);
            LogOut("[SWITCH][VANILLA] Control roles: P1=AI, P2=Human", true);
        } else {
            SetAIControlFlag(1, true);
            SetAIControlFlag(2, false);
            LogOut("[SWITCH][VANILLA] Control roles: P1=Human, P2=AI", true);
        }

        const bool routingOk = SetVanillaSwapInputRouting(desiredLocal == 1);
        const bool jDisplayOk = SetRevivalPracticeDisplaySide(desiredLocal, "engine-only side set");

        std::ostringstream oss;
        oss << "[SWITCH][VANILLA] Engine-only swap -> active=" << (int)activePlayer
            << " P1CPU=" << (int)p1CpuFlag << " P2CPU=" << (int)p2CpuFlag
            << " gameState=0x" << std::hex << gameStatePtr
            << std::dec << " jDisplay=" << (jDisplayOk ? 1 : 0);
        LogOut(oss.str(), true);

        int aiPlayer = (desiredLocal == 1) ? 1 : 2;
    (void)NeutralizeMotionToken(aiPlayer);
        InputHook_ArmTokenNeutralize(aiPlayer, true);

        return okA && ok1 && ok2 && routingOk;
    }

    bool ToggleLocalSide() {
        // CRITICAL: Never modify game state during online mode.
        if (g_onlineModeActive.load()) return false;

        if (GetCurrentGameMode() != GameMode::Practice) return false;
        if (!IsMatchPhase()) {
            LogOut("[SWITCH] Ignored toggle outside of match phase", true);
            return false;
        }
        uintptr_t efzBase = GetEFZBase();
        if (!efzBase) return false;
        uintptr_t gs = 0; if (!SafeReadMemory(efzBase + EFZ_BASE_OFFSET_GAME_STATE, &gs, sizeof(gs)) || !gs) return false;
        uint8_t curActive = 0; SafeReadMemory(gs + GAMESTATE_OFF_ACTIVE_PLAYER, &curActive, sizeof(curActive));
        int desired = (curActive == 0) ? 1 : 0;
        bool success = ApplyEngineOnlySet(desired);
        if (success) {
            displayData.p2ControlEnabled = (desired == 1);
            s_sidesAreSwapped.store(desired == 1, std::memory_order_relaxed);
            LogOut(desired == 1 ? "[SWITCH] Toggle succeeded - sides now swapped (flag set)" : "[SWITCH] Toggle succeeded - returned to default (flag cleared)", true);
        }
        return success;
    }

    bool SetLocalSide(int sideIdx) {
        // CRITICAL: Never modify game state during online mode.
        if (g_onlineModeActive.load()) return false;

        if (GetCurrentGameMode() != GameMode::Practice) return false;
        if (!IsMatchPhase()) {
            LogOut("[SWITCH] Ignored set outside of match phase", true);
            return false;
        }
        const bool success = ApplyEngineOnlySet(sideIdx);
        if (success) {
            displayData.p2ControlEnabled = (sideIdx == 1);
            SetSwapFlagForLocalSide(sideIdx);
        }
        return success;
    }

    bool ReapplyLocalSide(int sideIdx) {
        if (g_onlineModeActive.load()) return false;

        if (GetCurrentGameMode() != GameMode::Practice) return false;
        if (!IsMatchPhase()) {
            LogOut("[SWITCH] Ignored reapply outside of match phase", true);
            return false;
        }

        const bool success = ApplyEngineOnlySet(sideIdx);
        if (success) {
            displayData.p2ControlEnabled = (sideIdx == 1);
            s_sidesAreSwapped.store(sideIdx == 1, std::memory_order_relaxed);
        }
        return success;
    }

    bool RestoreEngineControlState(int sideIdx, uint8_t p1CpuFlag, uint8_t p2CpuFlag, bool armInputCleanup) {
        if (g_onlineModeActive.load()) return false;
        if (GetCurrentGameMode() != GameMode::Practice) return false;
        if (sideIdx != 0 && sideIdx != 1) return false;

        uintptr_t efzBase = GetEFZBase();
        if (!efzBase) return false;

        uintptr_t gameStatePtr = 0;
        if (!SafeReadMemory(efzBase + EFZ_BASE_OFFSET_GAME_STATE, &gameStatePtr, sizeof(gameStatePtr)) || !gameStatePtr) {
            return false;
        }

        const uint8_t activePlayer = static_cast<uint8_t>(sideIdx);
        const bool okActive = SafeWriteMemory(gameStatePtr + GAMESTATE_OFF_ACTIVE_PLAYER, &activePlayer, sizeof(activePlayer));
        const bool okP1 = SafeWriteMemory(gameStatePtr + GAMESTATE_OFF_P1_CPU_FLAG, &p1CpuFlag, sizeof(p1CpuFlag));
        const bool okP2 = SafeWriteMemory(gameStatePtr + GAMESTATE_OFF_P2_CPU_FLAG, &p2CpuFlag, sizeof(p2CpuFlag));
    const bool useVanillaRouting = (sideIdx == 1);

        SetAIControlFlag(1, p1CpuFlag == 0u);
        SetAIControlFlag(2, p2CpuFlag == 0u);
    const bool routingOk = SetVanillaSwapInputRouting(useVanillaRouting);
        const bool jDisplayOk = SetRevivalPracticeDisplaySide(sideIdx, "engine control restore");
        displayData.p2ControlEnabled = (sideIdx == 1);
        SetSwapFlagForLocalSide(sideIdx);

        const int aiPlayer = (p1CpuFlag != 0u && p2CpuFlag == 0u) ? 1
                            : (p2CpuFlag != 0u && p1CpuFlag == 0u) ? 2
                            : 0;
        if (armInputCleanup && aiPlayer != 0) {
            (void)NeutralizeMotionToken(aiPlayer);
            InputHook_ArmTokenNeutralize(aiPlayer, true);
        }

        std::ostringstream oss;
        oss << "[SWITCH][ENGINE_ONLY] Restored active=" << static_cast<int>(activePlayer)
            << " P1CPU=" << static_cast<int>(p1CpuFlag)
            << " P2CPU=" << static_cast<int>(p2CpuFlag)
                << " routeSwap=" << (useVanillaRouting ? 1 : 0)
            << " jDisplay=" << (jDisplayOk ? 1 : 0)
            << " inputCleanup=" << (armInputCleanup ? 1 : 0)
            << " gameState=0x" << std::hex << gameStatePtr;
        LogOut(oss.str(), true);

        return okActive && okP1 && okP2 && routingOk;
    }

    int GetLocalSide() {
        if (GetCurrentGameMode() != GameMode::Practice) return -1;

        uintptr_t efzBase = GetEFZBase();
        if (!efzBase) return -1;
        uintptr_t gameStatePtr = 0;
        if (!SafeReadMemory(efzBase + EFZ_BASE_OFFSET_GAME_STATE, &gameStatePtr, sizeof(gameStatePtr)) || !gameStatePtr)
            return -1;
        uint8_t activePlayer = 0;
        if (!SafeReadMemory(gameStatePtr + GAMESTATE_OFF_ACTIVE_PLAYER, &activePlayer, sizeof(activePlayer)))
            return -1;
        return (int)activePlayer;
    }

    int GetLocalPlayerIndex() {
        return GetLocalSide() == 1 ? 2 : 1;
    }

    int GetRemotePlayerIndex() {
        return GetLocalPlayerIndex() == 1 ? 2 : 1;
    }

    bool ResetControlMappingForMenusToP1() {
        // Never touch game state under netplay suspend or outside Practice.
        if (!IsPracticeContext()) return false;

        SetVanillaSwapInputRouting(false);

        bool wroteFlags = false;
        uintptr_t efzBase = GetEFZBase();
        if (efzBase) {
            uintptr_t gameStatePtr = 0;
            if (SafeReadMemory(efzBase + EFZ_BASE_OFFSET_GAME_STATE, &gameStatePtr, sizeof(gameStatePtr)) && gameStatePtr) {
                uint8_t activePlayer = 0u;
                uint8_t p1Human = 0u;
                uint8_t p2Cpu   = 1u;
                bool okActive = SafeWriteMemory(gameStatePtr + GAMESTATE_OFF_ACTIVE_PLAYER, &activePlayer, sizeof(activePlayer));
                bool okP1     = SafeWriteMemory(gameStatePtr + GAMESTATE_OFF_P1_CPU_FLAG, &p1Human, sizeof(p1Human));
                bool okP2     = SafeWriteMemory(gameStatePtr + GAMESTATE_OFF_P2_CPU_FLAG, &p2Cpu, sizeof(p2Cpu));
                wroteFlags = okActive && okP1 && okP2;
                if (wroteFlags) {
                    LogOut("[SWITCH][MENU] Vanilla routing reset + CPU flags restored (P1 human/P2 CPU)", true);
                } else {
                    LogOut("[SWITCH][MENU][WARN] Vanilla CPU flag restore failed", true);
                }
            }
        }
        SetRevivalPracticeDisplaySide(0, "menu reset", false);
        s_sidesAreSwapped.store(false, std::memory_order_relaxed);
        return wroteFlags;
    }

    void ClearSwapFlag() {
        s_sidesAreSwapped.store(false, std::memory_order_relaxed);
    }

    void MarkSwapped() {
        s_sidesAreSwapped.store(true, std::memory_order_relaxed);
    }
}
