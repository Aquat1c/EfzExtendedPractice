#include "../../include/utils/pause_integration.h"
#include "../include/core/logger.h"
#include "../include/game/game_state.h"
#include "../include/utils/utilities.h" // FindEFZWindow
#include "../include/core/constants.h"
#include "../include/core/memory.h"
#include "../include/game/practice_offsets.h"
#include "../include/game/efzrevival_addrs.h"
#include "../include/utils/network.h" // GetEfzRevivalVersion, EfzRevivalVersion
#include "../include/utils/minhook_utils.h"
#include "../include/runtime/native_game_profile.h"
#include "../../include/input/framestep.h"
#include "../../include/utils/visual_freeze_policy.h"
#include "../3rdparty/minhook/include/MinHook.h"
#include <windows.h>
#include <atomic>
#include <cstring>
#include <mutex>
#include <sstream>

namespace {
    typedef void* (__stdcall *tGetModeStruct)(int idx);
    static void* Seh_GetModeStruct(tGetModeStruct fn, int idx) {
        void* candPtr = nullptr;
        __try { candPtr = fn ? fn(idx) : nullptr; }
        __except (EXCEPTION_EXECUTE_HANDLER) { candPtr = nullptr; }
        return candPtr;
    }
    std::atomic<bool> s_menuVisible{false};
    std::mutex s_menuSurfaceMutex;
    uint8_t s_menuSurfaceMask = 0; // guarded by s_menuSurfaceMutex
    struct MenuSurfaceRefreshState {
        bool active = false;
        PauseIntegration::MenuSurface surface =
            PauseIntegration::MenuSurface::TutorialPage;
        uint64_t token = 0;
    };
    MenuSurfaceRefreshState s_menuSurfaceRefresh; // guarded by s_menuSurfaceMutex
    uint64_t s_nextMenuSurfaceRefreshToken = 1;   // guarded by s_menuSurfaceMutex
    std::atomic<bool> s_menuSurfaceRefreshActive{false};
    std::atomic<void*> s_practicePtr{nullptr};
    std::atomic<bool> s_practiceHooksInstalled{false};
    std::atomic<bool> s_practiceHooksEnabled{false};
    std::atomic<bool> s_practiceTickHookCreated{false};
    std::atomic<bool> s_togglePauseHookCreated{false};
    std::atomic<bool> s_loggedValidateOnce{false};
    std::atomic<bool> s_csActive{false};
    std::atomic<bool> s_scanSuppressedThisCS{false};
    std::atomic<bool> s_loggedScanFailOnce{false};

    std::atomic<bool> s_weUsedOfficialToggle{false};
    std::atomic<bool> s_weForcedFlagPause{false};
    std::atomic<bool> s_weAppliedPatchFreeze{false};
    bool BeginRevivalPatchToggle();
    void EndRevivalPatchToggle(bool retake);

    std::atomic<void*> s_battleContext{nullptr};
    std::atomic<bool> s_weFrozeGamespeed{false};
    std::atomic<uint8_t> s_prevGamespeed{3};
    std::atomic<uintptr_t> s_gamespeedAddr{0};
    std::atomic<uint32_t> s_lastStepCounter{0};
    std::atomic<PauseIntegration::StepAdvanceCallback> s_stepAdvanceCallback{nullptr};
    std::atomic<bool> s_weAppliedVisualPatches{false};
    uintptr_t s_practiceTickTarget = 0;
    uintptr_t s_togglePauseTarget = 0;
    std::atomic<bool> s_battleContextHookInstalled{false};
    std::atomic<bool> s_battleContextHookEnabled{false};
    std::atomic<bool> s_battleContextHookLoggedFail{false};
    std::atomic<bool> s_battleContextHookUnavailable{false};
    uintptr_t s_battleContextHookTarget = 0;

    bool IsExecutableProtection(DWORD protection) {
        switch (protection & 0xFFu) {
        case PAGE_EXECUTE:
        case PAGE_EXECUTE_READ:
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY:
            return true;
        default:
            return false;
        }
    }

    bool ValidateBattleContextHookTarget(uintptr_t target) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!target || VirtualQuery(reinterpret_cast<LPCVOID>(target), &mbi, sizeof(mbi)) == 0) {
            if (!s_battleContextHookLoggedFail.exchange(true)) {
                std::ostringstream oss;
                oss << "[PAUSE] RenderBattleScreen target validation failed at 0x" << std::hex << target
                    << " reason=VirtualQuery";
                LogOut(oss.str(), true);
            }
            s_battleContextHookUnavailable.store(true, std::memory_order_relaxed);
            return false;
        }

        if (mbi.State != MEM_COMMIT || !IsExecutableProtection(mbi.Protect)) {
            if (!s_battleContextHookLoggedFail.exchange(true)) {
                char modulePath[MAX_PATH] = {};
                std::ostringstream oss;
                oss << "[PAUSE] RenderBattleScreen target is not executable at 0x" << std::hex << target
                    << " state=0x" << mbi.State
                    << " protect=0x" << mbi.Protect;
                if (mbi.AllocationBase
                    && GetModuleFileNameA(reinterpret_cast<HMODULE>(mbi.AllocationBase), modulePath, MAX_PATH) > 0) {
                    oss << " module=" << modulePath;
                }
                LogOut(oss.str(), true);
            }
            s_battleContextHookUnavailable.store(true, std::memory_order_relaxed);
            return false;
        }

        return true;
    }

    bool SetCapturedHookEnabled(uintptr_t target, bool active, const char* label) {
        if (!target) {
            return false;
        }

        const bool changed = active
            ? MinHookUtils::EnableHook(reinterpret_cast<void*>(target), "[PAUSE]", label)
            : MinHookUtils::DisableHook(reinterpret_cast<void*>(target), "[PAUSE]", label);
        if (changed) return true;

        std::ostringstream oss;
        oss << "[PAUSE] Failed to " << (active ? "enable " : "disable ")
            << label << " hook at 0x" << std::hex << target;
        LogOut(oss.str(), true);
        return false;
    }
    
    bool IsEfzRevivalLoaded() {
        return GetModuleHandleA("EfzRevival.dll") != nullptr && IsEfzRevivalVersionSupported();
    }

    static bool ReadPracticeCandidateCore(uintptr_t cand,
                                          int& outLocal,
                                          int& outRemote,
                                          uint8_t& outGuiPos,
                                          uint8_t& outPauseFlag,
                                          uintptr_t& outPrimary,
                                          uintptr_t& outSecondary) {
        outLocal = -1;
        outRemote = -1;
        outGuiPos = 0xFF;
        outPauseFlag = 0xFF;
        outPrimary = 0;
        outSecondary = 0;
        if (!cand) return false;

        const uintptr_t pauseOff = EFZ_Practice_PauseFlagOffset();
        const bool okLocal = SafeReadMemory(cand + PRACTICE_OFF_LOCAL_SIDE_IDX, &outLocal, sizeof(outLocal));
        const bool okRemote = SafeReadMemory(cand + PRACTICE_OFF_REMOTE_SIDE_IDX, &outRemote, sizeof(outRemote));
        const bool okGui = SafeReadMemory(cand + PRACTICE_OFF_GUI_POS, &outGuiPos, sizeof(outGuiPos));
        const bool okPause = (pauseOff == 0) || SafeReadMemory(cand + pauseOff, &outPauseFlag, sizeof(outPauseFlag));
        const bool okPrimary = SafeReadMemory(cand + PRACTICE_OFF_SIDE_BUF_PRIMARY, &outPrimary, sizeof(outPrimary));
        const bool okSecondary = SafeReadMemory(cand + PRACTICE_OFF_SIDE_BUF_SECONDARY, &outSecondary, sizeof(outSecondary));
        return okLocal && okRemote && okGui && okPause && okPrimary && okSecondary;
    }

    static bool ValidatePracticeCandidate(uintptr_t cand) {
        if (!cand) return false;
        EfzRevivalVersion ver = GetEfzRevivalVersion();
        {
            if (!s_loggedValidateOnce.exchange(true, std::memory_order_relaxed)) {
                std::ostringstream oss; oss << "[PAUSE] Validate candidate=0x" << std::hex << cand
                    << " ver=" << (EfzRevivalVersionName(ver) ? EfzRevivalVersionName(ver) : "?")
                    << " localOff=0x" << PRACTICE_OFF_LOCAL_SIDE_IDX
                    << " remoteOff=0x" << PRACTICE_OFF_REMOTE_SIDE_IDX
                    << " pauseOff=0x" << EFZ_Practice_PauseFlagOffset();
                LogOut(oss.str(), detailedLogging.load());
            }
        }
        int local = -1, remote = -1;
        uint8_t guiPos = 0xFF, pauseFlag = 0xFF;
        uintptr_t primary = 0, secondary = 0;
        (void)ReadPracticeCandidateCore(cand, local, remote, guiPos, pauseFlag, primary, secondary);
        uintptr_t pauseOff = EFZ_Practice_PauseFlagOffset();
        bool sideOk = (local == 0 || local == 1);
        bool remoteOk = (remote == 0 || remote == 1) && (remote != local);
        bool pauseOk = (pauseOff == 0 || pauseFlag == 0 || pauseFlag == 1);

        // 1.02e only: primary buffer must point at a known embedded base block;
        // these offsets do not hold on h/i.
        if (ver == EfzRevivalVersion::Revival102e) {
            bool bufOk = (primary == (cand + PRACTICE_OFF_BUF_LOCAL_BASE)) || (primary == (cand + PRACTICE_OFF_BUF_REMOTE_BASE));
            return sideOk && remoteOk && pauseOk && bufOk;
        } else {
            bool guiOk = (guiPos == 0 || guiPos == 1);
            return sideOk && remoteOk && (pauseOk || guiOk);
        }
    }

    static bool ValidatePracticeCandidateLoose(uintptr_t cand) {
        if (!cand) return false;
        if (GetCurrentGameMode() != GameMode::Practice || !IsMatchPhase()) return false;
        if (IsNetplaySuspendActive()) return false;

        int local = -1, remote = -1;
        uint8_t guiPos = 0xFF, pauseFlag = 0xFF;
        uintptr_t primary = 0, secondary = 0;
        if (!ReadPracticeCandidateCore(cand, local, remote, guiPos, pauseFlag, primary, secondary)) {
            return false;
        }

        const bool sideOk = (local == 0 || local == 1) && (remote == 0 || remote == 1) && (local != remote);
        const bool guiOk = (guiPos == 0 || guiPos == 1);
        const bool pauseOk = (pauseFlag == 0 || pauseFlag == 1);
        const bool primaryOk = (primary > 0x10000);
        const bool secondaryOk = (secondary > 0x10000);
        return sideOk && (guiOk || pauseOk || (primaryOk && secondaryOk && primary != secondary));
    }

    bool TryResolvePracticePtrByScan(void*& outPtr,
                                     bool allowCharacterSelect = false,
                                     bool allowLooseValidation = false,
                                     const char* reason = nullptr) {
        if (!allowCharacterSelect && IsInCharacterSelectScreen()) {
            return false;
        }
        GameMode mode = GetCurrentGameMode();
        if (mode != GameMode::Practice) {
            return false;
        }
        if (IsNetplaySuspendActive()) {
            return false;
        }
        if (!allowCharacterSelect && IsInCharacterSelectScreen() && s_scanSuppressedThisCS.load(std::memory_order_relaxed)) {
            return false;
        }
        HMODULE hRev = GetModuleHandleA("EfzRevival.dll");
        if (!hRev) return false;
        auto base = reinterpret_cast<uintptr_t>(hRev);
        uintptr_t gmArrayRva = EFZ_RVA_GameModePtrArray();
        if (!gmArrayRva) return false;
        for (int idx = 0; idx < 16; ++idx) {
            uintptr_t slotAddr = base + gmArrayRva + 4 * idx;
            uintptr_t cand = 0;
            if (!SafeReadMemory(slotAddr, &cand, sizeof(cand)) || !cand) continue;
            if (ValidatePracticeCandidate(cand) || (allowLooseValidation && ValidatePracticeCandidateLoose(cand))) {
                outPtr = reinterpret_cast<void*>(cand);
                std::ostringstream oss;
                oss << "[PAUSE] Heuristic scan resolved practice=0x" << std::hex << cand
                    << " (slot=" << std::dec << idx << ")"
                    << " mode=" << (allowLooseValidation ? "loose" : "strict")
                    << " reason=" << (reason ? reason : "unspecified");
                LogOut(oss.str(), detailedLogging.load());
                return true;
            }
        }
        bool firstThisCS = allowCharacterSelect ? false : !s_scanSuppressedThisCS.exchange(true, std::memory_order_relaxed);
        if (allowCharacterSelect || firstThisCS) {
            if (!s_loggedScanFailOnce.exchange(true, std::memory_order_relaxed)) {
                std::ostringstream oss;
                oss << "[PAUSE] Heuristic scan failed to resolve Practice controller"
                    << " mode=" << (allowLooseValidation ? "loose" : "strict")
                    << " reason=" << (reason ? reason : "unspecified");
                LogOut(oss.str(), detailedLogging.load());
            }
        }
        return false;
    }

    void* ResolvePracticeControllerPtrInternal(bool allowCharacterSelect,
                                               bool allowLooseValidation,
                                               const char* reason) {
        if (void* cached = s_practicePtr.load(std::memory_order_relaxed)) {
            return cached;
        }

        if (!allowCharacterSelect && !IsMatchPhase()) {
            return nullptr;
        }

        void* resolved = nullptr;
        if (TryResolvePracticePtrByScan(resolved, allowCharacterSelect, allowLooseValidation, reason)) {
            s_practicePtr.store(resolved, std::memory_order_relaxed);
            return resolved;
        }
        return nullptr;
    }

    // Captures the Practice controller from the per-frame update (ECX = Practice "this").
    typedef int (__thiscall *tPracticeTick)(void* thisPtr);
    static tPracticeTick oPracticeTick = nullptr;
    static int __fastcall HookedPracticeTick(void* thisPtr, void* /*edx*/) {
    auto hookExecution = MinHookUtils::EnterExecution(MinHookUtils::TicketFor<&HookedPracticeTick>());
        if (!hookExecution.Admitted() || !PracticeBattleHooksOpen()) {
            return oPracticeTick ? oPracticeTick(thisPtr) : 0;
        }
        PauseIntegration::NotePracticeControllerCandidate(thisPtr, "PracticeTick");
        const auto stepCallback = s_stepAdvanceCallback.load(std::memory_order_relaxed);
        const bool wantNeutralize = s_menuVisible.load(std::memory_order_relaxed);
        const uintptr_t stepCounterOff = EFZ_Practice_StepCounterOffset();
        uint32_t before = 0;
        bool haveBefore = false;
        if (stepCallback || wantNeutralize) {
            haveBefore = SafeReadMemory(reinterpret_cast<uintptr_t>(thisPtr) + stepCounterOff, &before, sizeof(before));
        }
        int ret = oPracticeTick ? oPracticeTick(thisPtr) : 0;
        if (haveBefore) {
            uint32_t after = before;
            if (SafeReadMemory(reinterpret_cast<uintptr_t>(thisPtr) + stepCounterOff, &after, sizeof(after))) {
                if (wantNeutralize && after == before + 1) {
                    SafeWriteMemory(reinterpret_cast<uintptr_t>(thisPtr) + stepCounterOff, &before, sizeof(before));
                    after = before;
                }
                if (stepCallback && after != before) {
                    stepCallback(before, after);
                }
            }
        }
        return ret;
    }

    // Also captures the pointer via the Pause toggle entry when Space/P is pressed.
    typedef int (__thiscall *tTogglePause)(void* thisPtr);
    static tTogglePause oTogglePause = nullptr;
    static std::atomic<bool> s_internalPauseBypass{false};
    static int __fastcall HookedTogglePause(void* thisPtr, void* /*edx*/) {
    auto hookExecution = MinHookUtils::EnterExecution(MinHookUtils::TicketFor<&HookedTogglePause>());
        if (!hookExecution.Admitted() || !PracticeBattleHooksOpen()) {
            return oTogglePause ? oTogglePause(thisPtr) : 0;
        }
        PauseIntegration::NotePracticeControllerCandidate(thisPtr, "TogglePause");
        if (s_menuVisible.load(std::memory_order_relaxed) && !s_internalPauseBypass.load(std::memory_order_relaxed)) {
            return 0;
        }
        const bool retake = BeginRevivalPatchToggle();
        const int result = oTogglePause ? oTogglePause(thisPtr) : 0;
        EndRevivalPatchToggle(retake);
        return result;
    }

    static inline void UpdateCsCycleState() {
        const bool cs = IsInCharacterSelectScreen();
        if (cs) {
            if (!s_csActive.exchange(true, std::memory_order_relaxed)) {
                s_scanSuppressedThisCS.store(false, std::memory_order_relaxed);
            }
        } else {
            s_csActive.store(false, std::memory_order_relaxed);
        }
    }

    void EnsurePracticePtrHookInstalled() {
        // CRITICAL: Never install hooks during online mode.
        if (g_onlineModeActive.load()) return;

        GameMode mode = GetCurrentGameMode();
        if (mode != GameMode::Practice) return;

        if (!IsEfzRevivalLoaded()) return;

        UpdateCsCycleState();
        HMODULE hRev = GetModuleHandleA("EfzRevival.dll");
        if (!hRev) return;
        uintptr_t rvaTick = EFZ_RVA_PracticeTick();
        uintptr_t rvaPause = EFZ_RVA_TogglePause();
        void* tickTarget  = rvaTick ? EFZ_RVA_TO_VA(hRev, rvaTick) : nullptr;
        void* pauseTarget = rvaPause ? EFZ_RVA_TO_VA(hRev, rvaPause) : nullptr;
        if ((s_practiceTickTarget!=reinterpret_cast<uintptr_t>(tickTarget) &&
             MinHookUtils::HasOwnedTarget(reinterpret_cast<void*>(s_practiceTickTarget))) ||
            (s_togglePauseTarget!=reinterpret_cast<uintptr_t>(pauseTarget) &&
             MinHookUtils::HasOwnedTarget(reinterpret_cast<void*>(s_togglePauseTarget)))) return;
        s_practiceTickTarget = reinterpret_cast<uintptr_t>(tickTarget);
        s_togglePauseTarget = reinterpret_cast<uintptr_t>(pauseTarget);
        if (s_practiceHooksInstalled.load()) {
            if (!s_practiceHooksEnabled.load(std::memory_order_relaxed)) {
                bool anyEnabled = false;
                if (s_practiceTickHookCreated.load(std::memory_order_relaxed) && s_practiceTickTarget) {
                    anyEnabled |= SetCapturedHookEnabled(s_practiceTickTarget, true, "PracticeTick");
                }
                if (s_togglePauseHookCreated.load(std::memory_order_relaxed) && s_togglePauseTarget) {
                    anyEnabled |= SetCapturedHookEnabled(s_togglePauseTarget, true, "TogglePause");
                }
                if (anyEnabled) {
                    s_practiceHooksEnabled.store(true, std::memory_order_relaxed);
                    LogOut("[PAUSE] Practice capture hooks re-enabled", detailedLogging.load());
                }
            }
            return;
        }
        {
            std::ostringstream oss; oss << "[PAUSE] Installing hooks: PracticeTick=0x" << std::hex << (uintptr_t)tickTarget
                << " TogglePause=0x" << (uintptr_t)pauseTarget;
            LogOut(oss.str(), detailedLogging.load());
        }
        bool anyHook = false;
        if (tickTarget) {
            if (MinHookUtils::CreateAndEnableHook(tickTarget,
                                                  reinterpret_cast<void*>(&HookedPracticeTick),
                                                  reinterpret_cast<void**>(&oPracticeTick),
                                                  "[PAUSE]",
                                                  "PracticeTick", nullptr, nullptr, &MinHookUtils::TicketFor<&HookedPracticeTick>())) {
                s_practiceTickHookCreated.store(true, std::memory_order_relaxed);
                anyHook = true;
                LogOut("[PAUSE] PracticeTick hook active", detailedLogging.load());
            }
            s_practiceTickHookCreated.store(MinHookUtils::HasOwnedTarget(tickTarget), std::memory_order_relaxed);
        }
        if (pauseTarget
            && MinHookUtils::CreateAndEnableHook(pauseTarget,
                                                 reinterpret_cast<void*>(&HookedTogglePause),
                                                 reinterpret_cast<void**>(&oTogglePause),
                                                 "[PAUSE]",
                                                 "TogglePause", nullptr, nullptr, &MinHookUtils::TicketFor<&HookedTogglePause>())) {
            s_togglePauseHookCreated.store(true, std::memory_order_relaxed);
            anyHook = true; LogOut("[PAUSE] TogglePause hook active", detailedLogging.load());
        }
        s_togglePauseHookCreated.store(MinHookUtils::HasOwnedTarget(pauseTarget), std::memory_order_relaxed);
        if (anyHook) {
            s_practiceHooksInstalled.store(true);
            s_practiceHooksEnabled.store(true);
        }
    }

    // Practice pause flag: 1 = paused, 0 = running.
    bool ReadPracticePauseFlag(bool &outPaused) {
        void* p = s_practicePtr.load(); if (!p) return false;
        uintptr_t pauseOff = EFZ_Practice_PauseFlagOffset();
        uint8_t v = 0; if (!SafeReadMemory(reinterpret_cast<uintptr_t>(p) + pauseOff, &v, sizeof(v))) return false;
        outPaused = (v != 0); return true;
    }
    bool WritePracticePauseFlag(bool paused) {
        void* p = s_practicePtr.load(); if (!p) return false;
        uintptr_t pauseOff = EFZ_Practice_PauseFlagOffset();
        uint8_t v = paused ? 1u : 0u; return SafeWriteMemory(reinterpret_cast<uintptr_t>(p) + pauseOff, &v, sizeof(v));
    }
    bool WritePracticeStepFlag(bool stepRequested) {
        void* p = s_practicePtr.load(); if (!p) return false;
        uintptr_t stepOff = EFZ_Practice_StepFlagOffset();
        uint8_t v = stepRequested ? 1u : 0u;
        return SafeWriteMemory(reinterpret_cast<uintptr_t>(p) + stepOff, &v, sizeof(v));
    }

    bool ResetPracticeStepCounterToZero() {
        void* p = s_practicePtr.load(); if (!p) return false;
        uint32_t zero = 0;
        uintptr_t stepCounterOff = EFZ_Practice_StepCounterOffset();
        bool ok = SafeWriteMemory(reinterpret_cast<uintptr_t>(p) + stepCounterOff, &zero, sizeof(zero));
        if (ok) {
            std::ostringstream oss; oss << "[PAUSE] Reset step counter to 0 at +0x" << std::hex << stepCounterOff 
                << " for Practice=0x" << (uintptr_t)p;
            LogOut(oss.str(), true);
        } else {
            LogOut("[PAUSE] Failed to reset step counter (Practice ptr missing or write failed)", true);
        }
        return ok;
    }

    bool ResolvePracticeForFramestep(const char* reason) {
        EnsurePracticePtrHookInstalled();
        if (s_practicePtr.load(std::memory_order_relaxed)) {
            return true;
        }
        void* resolved = ResolvePracticeControllerPtrInternal(false, true, reason);
        if (resolved) {
            s_practicePtr.store(resolved, std::memory_order_relaxed);
            return true;
        }
        return false;
    }

    typedef BOOL (__thiscall *tRenderBattleScreen)(void* battleContext);
    static tRenderBattleScreen oRenderBattleScreen = nullptr;
    static BOOL __fastcall HookedRenderBattleScreen(void* battleContext, void* /*edx*/) {
    auto hookExecution = MinHookUtils::EnterExecution(MinHookUtils::TicketFor<&HookedRenderBattleScreen>());
        if (!hookExecution.Admitted() || !PracticeBattleHooksOpen()) {
            return oRenderBattleScreen ? oRenderBattleScreen(battleContext) : FALSE;
        }
        if (battleContext) {
            s_battleContext.store(battleContext, std::memory_order_relaxed);

            static uintptr_t s_lastLoggedBattleContext = 0;
            const uintptr_t current = reinterpret_cast<uintptr_t>(battleContext);
            if (current != s_lastLoggedBattleContext) {
                s_lastLoggedBattleContext = current;
                std::ostringstream oss;
                oss << "[PAUSE] RenderBattleScreen captured battleContext=0x" << std::hex << current;
                LogOut(oss.str(), detailedLogging.load());
            }
        }
        return oRenderBattleScreen ? oRenderBattleScreen(battleContext) : FALSE;
    }

    // Mirrors Revival's EFZ_GameMode_GetStructByIndex(3) battle-context fetch.
    static bool RefreshBattleContextFromGameModeArray() {
        uintptr_t efzBase = GetEFZBase(); if (!efzBase) return false;
        // RVA of the VA-0x790110 game-mode array (preferred load base 0x400000).
        constexpr uintptr_t RVA_GameModeArray = 0x00390110;
        void* bc = nullptr;
        uintptr_t slot3 = efzBase + RVA_GameModeArray + 4 * 3; // index 3 = battle context
        if (!SafeReadMemory(slot3, &bc, sizeof(bc)) || !bc) return false;
        s_battleContext.store(bc, std::memory_order_relaxed);
        return true;
    }

    static bool ResolveGamespeedAddr() {
        if (s_gamespeedAddr.load()) return true;
        Practice::PatchModule image;
        if (!Practice::GetQualifiedMemorialImage(image)) return false;
        uint8_t screen = 0;
        uintptr_t battle = 0;
        if (!SafeReadMemory(image.base + 0x390148, &screen, sizeof(screen)) || screen != 3 ||
            !SafeReadMemory(image.base + 0x39011c, &battle, sizeof(battle)) || !battle) return false;
        uint8_t speed = 0xff;
        const uintptr_t address = battle + 0x578;
        if (!SafeReadMemory(address, &speed, sizeof(speed)) || speed > 3) return false;
        // Only the qualified active Battle field may establish this cache.
        s_gamespeedAddr.store(address, std::memory_order_relaxed);
        return true;
    }

    void EnsureBattleContextHook() {
        if (s_battleContextHookUnavailable.load(std::memory_order_relaxed)) {
            return;
        }
        if (s_battleContextHookInstalled.load()) {
            if (!s_battleContextHookEnabled.load(std::memory_order_relaxed) && s_battleContextHookTarget) {
                if (SetCapturedHookEnabled(s_battleContextHookTarget, true, "RenderBattleScreen")) {
                    s_battleContextHookEnabled.store(true, std::memory_order_relaxed);
                    LogOut("[PAUSE] RenderBattleScreen hook re-enabled", detailedLogging.load());
                }
            }
            return;
        }
        // RenderBattleScreen lives in efz.exe, not EfzRevival.dll.
        uintptr_t efzBase = GetEFZBase();
        if (!efzBase) return;
        uintptr_t rva = EFZ_RVA_RenderBattleScreen();
        void* target = rva ? reinterpret_cast<void*>(efzBase + rva) : nullptr;
        if (!target) return;
        s_battleContextHookTarget = reinterpret_cast<uintptr_t>(target);
        if (!ValidateBattleContextHookTarget(s_battleContextHookTarget)) {
            return;
        }

        if (!MinHookUtils::CreateHook(target,
                reinterpret_cast<void*>(&HookedRenderBattleScreen),
                reinterpret_cast<void**>(&oRenderBattleScreen), "[PAUSE]", "RenderBattleScreen", nullptr, &MinHookUtils::TicketFor<&HookedRenderBattleScreen>())) {
            s_battleContextHookUnavailable.store(true, std::memory_order_relaxed);
            return;
        }
        // Remember creation before enable; the registry retains the original on
        // enable failure and retries the same owner on reactivation.
        s_battleContextHookInstalled.store(true);
        if (!MinHookUtils::EnableHook(target, "[PAUSE]", "RenderBattleScreen")) {
            s_battleContextHookEnabled.store(false);
            return;
        }

        s_battleContextHookInstalled.store(true);
        s_battleContextHookEnabled.store(true);
        LogOut("[PAUSE] RenderBattleScreen hook installed (capturing battleContext)", detailedLogging.load());
    }

    bool ReadGamespeed(uint8_t &out) {
        uintptr_t addr = s_gamespeedAddr.load();
        if (addr || ResolveGamespeedAddr()) {
            addr = s_gamespeedAddr.load();
            if (addr && SafeReadMemory(addr, &out, sizeof(out))) return true;
        }
        return false;
    }
    bool WriteGamespeed(uint8_t v) {
        uintptr_t addr = s_gamespeedAddr.load();
        if (!addr && !ResolveGamespeedAddr()) {
            return false;
        }
        addr = s_gamespeedAddr.load();
        bool ok = addr ? SafeWriteMemory(addr, &v, sizeof(v)) : false;
        std::ostringstream oss; oss << "[PAUSE] WriteGamespeed slot-addr 0x" << std::hex << addr
            << ": " << std::dec << (int)v << (ok ? " (OK)" : " (FAIL)");
        LogOut(oss.str(), true);
        return ok;
    }

    // Supported Revival patch togglers take a numeric speed; see EFZ_PatchToggleUnfreezeParam().
    typedef void (__thiscall *tPatchToggle)(void* patchCtx, uint8_t speed);
    // Official pause toggle (sub_10075720)
    typedef int (__thiscall *tOfficialToggle)(void* thisPtr);

    // SEH helpers must avoid C++ objects with destructors in scope.
    static bool SehCall_PatchToggleThis(void* ctx, tPatchToggle fn, uint8_t speed) {
        __try { fn(ctx, speed); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    // 1.02i is also __thiscall; no stdcall variant needed.
    static bool SehCall_OfficialToggle(void* thisPtr, tOfficialToggle fn) {
        __try { fn(thisPtr); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    tPatchToggle GetPatchToggleFn() {
        HMODULE hRev = GetModuleHandleA("EfzRevival.dll"); if (!hRev) return nullptr;
        uintptr_t rva = EFZ_RVA_PatchToggler();
        if (!rva) return nullptr;
        return reinterpret_cast<tPatchToggle>(EFZ_RVA_TO_VA(hRev, rva));
    }
    void* GetPatchCtxPtr() {
        HMODULE hRev = GetModuleHandleA("EfzRevival.dll"); if (!hRev) return nullptr;
        uintptr_t rva = EFZ_RVA_PatchCtx();
        if (!rva) return nullptr;
        return EFZ_RVA_TO_VA(hRev, rva);
    }
    bool ApplyPatchFreeze(bool freeze) {
        // Writes gamespeed and code bytes; never past the Practice boundary.
        if (!IsPracticeContext()) return false;
        void* ctx = GetPatchCtxPtr(); if (!ctx) return false;
        int enableParam = freeze ? 0 : EFZ_PatchToggleUnfreezeParam();
        {
            std::ostringstream oss; oss << "[PAUSE] PatchToggle freeze=" << (freeze?1:0) << " param=" << enableParam
                << " ctx=0x" << std::hex << (uintptr_t)ctx;
            LogOut(oss.str(), true);
        }
        auto fn = GetPatchToggleFn(); if (!fn) return false;
        const bool retake = BeginRevivalPatchToggle();
        const bool ok = SehCall_PatchToggleThis(ctx, fn, static_cast<uint8_t>(enableParam));
        EndRevivalPatchToggle(retake);
        return ok;
    }

    // Mirrors Revival's sub_1006B2A0 pause. Originals from the qualified
    // Memorial image: 5-byte sites are E8 rel32 CALLs, 3-byte sites are
    // SUB r32,1 animation/effect counter decrements.
    struct VisualSite {
        uintptr_t rva;
        uint8_t size;
        uint8_t original[5];
    };
    const VisualSite kVisualSites[] = {
        {0x36425F, 5, {0xE8, 0x4C, 0x33, 0xCA, 0xFF}},
        {0x35E183, 3, {0x83, 0xEA, 0x01}},
        {0x35DFB5, 3, {0x83, 0xE8, 0x01}},
        {0x35DFD0, 3, {0x83, 0xEA, 0x01}},
        {0x35E055, 3, {0x83, 0xE9, 0x01}},
        {0x35E0DA, 3, {0x83, 0xE8, 0x01}},
        {0x36420A, 5, {0xE8, 0x01, 0x3B, 0xCA, 0xFF}},
        {0x364AEB, 3, {0x83, 0xE8, 0x01}},
        {0x365E59, 5, {0xE8, 0x52, 0x01, 0xCA, 0xFF}},
        {0x365E7A, 5, {0xE8, 0x31, 0x01, 0xCA, 0xFF}},
    };
    const uint8_t kVisualNops[5] = {0x90, 0x90, 0x90, 0x90, 0x90};

    std::mutex s_visualFreezeMutex;
    VisualFreezeOwner s_visualFreeze;  // guarded by s_visualFreezeMutex

    bool VisualSiteHolds(uintptr_t base, const VisualSite& site, const uint8_t* bytes) {
        uint8_t live[5] = {};
        return SafeReadMemory(base + site.rva, live, site.size)
            && std::memcmp(live, bytes, site.size) == 0;
    }

    // Moves each site between the two known states only; foreign bytes are left alone.
    bool WriteVisualSites(uintptr_t base, bool nop) {
        bool ok = true;
        for (const auto& site : kVisualSites) {
            const uint8_t* to = nop ? kVisualNops : site.original;
            const uint8_t* from = nop ? site.original : kVisualNops;
            if (VisualSiteHolds(base, site, to)) continue;
            if (!VisualSiteHolds(base, site, from)) { ok = false; continue; }
            void* addr = reinterpret_cast<void*>(base + site.rva);
            DWORD oldProtect = 0;
            if (!VirtualProtect(addr, site.size, PAGE_EXECUTE_READWRITE, &oldProtect)) { ok = false; continue; }
            std::memcpy(addr, to, site.size);
            FlushInstructionCache(GetCurrentProcess(), addr, site.size);
            DWORD ignored = 0;
            VirtualProtect(addr, site.size, oldProtect, &ignored);
        }
        return ok;
    }

    struct LiveVisualSites {
        uintptr_t base;
        bool AllOriginal() const {
            for (const auto& site : kVisualSites) {
                if (!VisualSiteHolds(base, site, site.original)) return false;
            }
            return true;
        }
        bool MoveTo(bool nop) { return WriteVisualSites(base, nop); }
    };

    uintptr_t VisualSitesBase() {
        return reinterpret_cast<uintptr_t>(GetModuleHandleA("efz.exe"));
    }

    // Revival's own pause holds the NOPs legitimately; unknown state counts as held.
    bool RevivalMayHoldVisualSites() {
        bool paused = true;
        return !ReadPracticePauseFlag(paused) || paused
            || s_weAppliedPatchFreeze.load(std::memory_order_acquire);
    }

    bool BeginRevivalPatchToggle() {
        std::lock_guard<std::mutex> lock(s_visualFreezeMutex);
        LiveVisualSites sites{VisualSitesBase()};
        return sites.base && s_visualFreeze.BeginRevivalToggle(sites, RevivalMayHoldVisualSites());
    }

    void EndRevivalPatchToggle(bool retake) {
        if (!retake) return;
        std::lock_guard<std::mutex> lock(s_visualFreezeMutex);
        LiveVisualSites sites{VisualSitesBase()};
        if (sites.base) s_visualFreeze.EndRevivalToggle(sites, retake, IsPracticeContext());
    }

    void RepairOrphanedVisualSites() {
        std::lock_guard<std::mutex> lock(s_visualFreezeMutex);
        LiveVisualSites sites{VisualSitesBase()};
        if (sites.base) (void)s_visualFreeze.RepairOrphan(sites, RevivalMayHoldVisualSites());
    }

    bool ApplyVisualEffectPatches(bool freeze) {
        return PauseIntegration::SetVisualFreeze(PauseIntegration::VisualFreezeUser::Menu, freeze);
    }

    tOfficialToggle GetOfficialToggleFn() {
        HMODULE hRev = GetModuleHandleA("EfzRevival.dll"); if (!hRev) return nullptr;
    uintptr_t rva = EFZ_RVA_TogglePause();
    if (!rva) return nullptr;
    return reinterpret_cast<tOfficialToggle>(EFZ_RVA_TO_VA(hRev, rva));
    }
    bool InvokeOfficialToggle() {
        void* p = s_practicePtr.load(); if (!p) return false;

        auto fn = GetOfficialToggleFn(); if (!fn) return false;
        s_internalPauseBypass.store(true, std::memory_order_relaxed);
        const bool retake = BeginRevivalPatchToggle();
        bool ok = false;
        ok = SehCall_OfficialToggle(p, fn);
        EndRevivalPatchToggle(retake);
        LogOut(std::string("[PAUSE] InvokeOfficialToggle -> ") + (ok?"OK":"EXCEPTION"), true);
        s_internalPauseBypass.store(false, std::memory_order_relaxed);
        return ok;
    }
}

namespace PauseIntegration {
    void EnsurePracticePointerCapture() {
        EnsurePracticePtrHookInstalled();
        if (!RefreshBattleContextFromGameModeArray()) {
            EnsureBattleContextHook();
        }
    }
    void* GetPracticeControllerPtr() { return s_practicePtr.load(); }
    void NotePracticeControllerCandidate(void* practicePtr, const char* source) {
        if (!practicePtr) return;
        if (GetCurrentGameMode() != GameMode::Practice) return;
        // Objects captured under netplay suspend are dangling: the netplay mod
        // deletes the Practice controller once the online session initialises.
        if (IsNetplaySuspendActive()) return;
        void* previous = s_practicePtr.exchange(practicePtr, std::memory_order_relaxed);
        if (previous != practicePtr) {
            std::ostringstream oss;
            oss << "[PAUSE] Captured Practice controller via "
                << (source ? source : "unknown")
                << " practice=0x" << std::hex << reinterpret_cast<uintptr_t>(practicePtr);
            LogOut(oss.str(), true);
        }
    }
    void* ResolvePracticeControllerPtrNow(bool allowCharacterSelect,
                                          bool allowLooseValidation,
                                          const char* reason) {
        EnsurePracticePtrHookInstalled();
        void* resolved = ResolvePracticeControllerPtrInternal(allowCharacterSelect, allowLooseValidation, reason);
        if (resolved) {
            if (detailedLogging.load()) {
                std::ostringstream oss;
                oss << "[PAUSE] ResolvePracticeControllerPtrNow"
                    << " reason=" << (reason ? reason : "unspecified")
                    << " allowCS=" << (allowCharacterSelect ? 1 : 0)
                    << " allowLoose=" << (allowLooseValidation ? 1 : 0)
                    << " practice=0x" << std::hex << reinterpret_cast<uintptr_t>(resolved);
                LogOut(oss.str(), true);
            }
        } else if (!allowCharacterSelect && IsMatchPhase() && (allowLooseValidation || detailedLogging.load())) {
            std::ostringstream oss;
            oss << "[PAUSE] ResolvePracticeControllerPtrNow failed"
                << " reason=" << (reason ? reason : "unspecified")
                << " allowCS=" << (allowCharacterSelect ? 1 : 0)
                << " allowLoose=" << (allowLooseValidation ? 1 : 0);
            LogOut(oss.str(), true);
        }
        return resolved;
    }

    bool IsPracticePaused() { bool p=false; if (ReadPracticePauseFlag(p)) return p; return false; }
    // Unresolved gamespeed means "not frozen"; Revival pauses use the Practice flag.
    bool __cdecl IsGameSpeedFrozen() {
        uintptr_t addr = s_gamespeedAddr.load();
        if (!addr) { ResolveGamespeedAddr(); addr = s_gamespeedAddr.load(); }
        if (!addr) return false;
        uint8_t v = 3;
        if (!SafeReadMemory(addr, &v, sizeof(v))) return false;
        return v == 0;
    }
    bool IsPausedOrFrozen() { return IsPracticePaused() || IsGameSpeedFrozen(); }
    bool IsPracticePausedByMenu() {
        return s_weUsedOfficialToggle.load(std::memory_order_acquire)
            || s_weForcedFlagPause.load(std::memory_order_acquire);
    }

    bool SetPracticePausedForFramestep(bool paused) {
        if (!IsPracticeContext()) return false;
        if (!ResolvePracticeForFramestep(paused ? "framestep pause" : "framestep unpause")) {
            LogOut("[PAUSE][FRAMESTEP] Practice pointer unavailable", true);
            return false;
        }

        bool ok = true;
        ok = WritePracticeStepFlag(false) && ok;
        ok = WritePracticePauseFlag(paused) && ok;
        ok = ResetPracticeStepCounterToZero() && ok;
        ok = ApplyPatchFreeze(paused) && ok;

        std::ostringstream oss;
        oss << "[PAUSE][FRAMESTEP] SetPracticePausedForFramestep(" << (paused ? 1 : 0)
            << ") -> " << (ok ? "OK" : "FAIL");
        LogOut(oss.str(), true);
        return ok;
    }

    bool RequestPracticeSubframeStep() {
        if (!ResolvePracticeForFramestep("framestep step")) {
            LogOut("[PAUSE][FRAMESTEP] Cannot request native step: Practice pointer unavailable", true);
            return false;
        }

        bool paused = false;
        if (!ReadPracticePauseFlag(paused) || !paused) {
            if (!SetPracticePausedForFramestep(true)) {
                return false;
            }
        }

        bool ok = true;
        ok = WritePracticePauseFlag(true) && ok;
        ok = WritePracticeStepFlag(true) && ok;
        LogOut(std::string("[PAUSE][FRAMESTEP] RequestPracticeNativeStep -> ") + (ok ? "OK" : "FAIL"), true);
        return ok;
    }

    void MaintainFreezeWhileMenuVisible() {
        if (!s_menuVisible.load()) return;
        if (!IsPracticeContext()) return;
        // A surface-refresh lease owns the physical thaw; do not undo it here.
        if (s_menuSurfaceRefreshActive.load(std::memory_order_acquire)) return;

        HMODULE hRev = GetModuleHandleA("EfzRevival.dll");
        const bool unsupportedRevival = (hRev != nullptr) && !IsEfzRevivalVersionSupported();

        if (unsupportedRevival) {
            uint8_t cur=3;
            if (ReadGamespeed(cur) && cur!=0) {
                WriteGamespeed(0);
            }

            if (s_weAppliedVisualPatches.load()) {
                ApplyVisualEffectPatches(true);
            }
        }

        if (s_weForcedFlagPause.load()) {
            bool p=false; if (ReadPracticePauseFlag(p) && !p) WritePracticePauseFlag(true);
        }
    }

    void ApplyAggregateMenuVisibility(bool visible,
                                      bool publishLogicalVisibility = true) {
        if (publishLogicalVisibility) {
            s_menuVisible.store(visible, std::memory_order_release);
        }
        EnsurePracticePtrHookInstalled();
        GameMode mode = GetCurrentGameMode();
        const bool inPractice = (mode == GameMode::Practice);
        const bool revivalLoaded = (GetModuleHandleA("EfzRevival.dll") != nullptr) && IsEfzRevivalVersionSupported();
        std::ostringstream log; log << "[PAUSE] Menu=" << (visible?"open":"close") << " practice=" << (inPractice?1:0)
            << " prx=0x" << std::hex << (uintptr_t)s_practicePtr.load();
        LogOut(log.str(), true);

        if (visible) {
            s_weUsedOfficialToggle.store(false);
            s_weForcedFlagPause.store(false);
            s_weAppliedPatchFreeze.store(false);
            s_weFrozeGamespeed.store(false);

            // Gamespeed 0 and the shared NOPs already freeze the world; the
            // official toggle would only set gamespeed 3 again on close.
            if (revivalLoaded && inPractice && Framestep::HoldsEnginePause()) {
                LogOut("[PAUSE] Framestep already holds the pause; official toggle skipped", true);
                return;
            }

            if (revivalLoaded && inPractice && s_practicePtr.load()) {
                bool alreadyPaused=false; ReadPracticePauseFlag(alreadyPaused);
                if (!alreadyPaused) {
                    // Published first: framestep's snapshot on the monitor thread reads this after the flag.
                    s_weUsedOfficialToggle.store(true);
                    if (InvokeOfficialToggle()) {
                        bool nowPaused=false; if (ReadPracticePauseFlag(nowPaused) && nowPaused) {
                            LogOut("[PAUSE] Official toggle succeeded", true);
                            return;
                        }
                        LogOut("[PAUSE] Official toggle returned but state not paused; attempting fallback", true);
                    } else {
                        LogOut("[PAUSE] Official toggle call failed; attempting fallback", true);
                    }
                    s_weUsedOfficialToggle.store(false);
                } else {
                    LogOut("[PAUSE] Already paused before menu; will not auto-unpause", true);
                    return; // respect external pause
                }
            }
            // Mirror Revival's sub_1006B2A0 pause: gamespeed byte at +0x578 = 0
            // plus NOPping 10 animation/effect update CALLs in efz.exe.
            if (!revivalLoaded) {
                if (!RefreshBattleContextFromGameModeArray()) {
                    EnsureBattleContextHook();
                }

                bool appliedAny = false;

                uint8_t cur=3;
                if (ReadGamespeed(cur)) {
                    // Freeze only from the default value so user-set speeds are not overwritten.
                    if (cur == 3 && WriteGamespeed(0)) {
                        s_prevGamespeed.store(cur);
                        s_weFrozeGamespeed.store(true);
                        appliedAny = true;
                    } else if (cur != 3 && cur != 0) {
                        LogOut("[PAUSE][VANILLA] Gamespeed already modified (not default), skipping freeze", true);
                        return; // don't touch non-default gamespeed
                    }
                }
                
                if (ApplyVisualEffectPatches(true)) {
                    s_weAppliedVisualPatches.store(true);
                    appliedAny = true;
                }
                
                if (appliedAny) {
                    LogOut("[PAUSE][VANILLA] Full pause applied: gamespeed frozen + visual effects frozen", true);
                    return;
                } else {
                    LogOut("[PAUSE][VANILLA] Failed to apply full pause", true);
                }
            }

            bool appliedAny=false;
            if (revivalLoaded) {
                if (ApplyPatchFreeze(true)) { s_weAppliedPatchFreeze.store(true); appliedAny=true; LogOut("[PAUSE] Fallback: patch freeze applied", true); }
                if (s_practicePtr.load()) {
                    bool p=false; ReadPracticePauseFlag(p);
                    if (!p) {
                        s_weForcedFlagPause.store(true);
                        if (WritePracticePauseFlag(true)) { appliedAny=true; LogOut("[PAUSE] Fallback: pause flag set", true); }
                        else s_weForcedFlagPause.store(false);
                    }
                    ResetPracticeStepCounterToZero();
                }
            }
            if (!appliedAny) {
                uint8_t cur=3; if (ReadGamespeed(cur) && cur!=0) { s_prevGamespeed.store(cur); if (WriteGamespeed(0)) { s_weFrozeGamespeed.store(true); LogOut("[PAUSE] Emergency fallback: gamespeed frozen", true); } }
            }
        } else {
            // Once netplay is published the Practice controller may already be
            // deleted; drop ownership latches without writing. Code-patch restore
            // is process-global and always runs.
            const bool nativeWritesAllowed = !g_onlineModeActive.load(std::memory_order_acquire);
            if (s_weUsedOfficialToggle.load()) {
                if (nativeWritesAllowed) {
                    bool stillPaused=false; ReadPracticePauseFlag(stillPaused);
                    if (stillPaused) {
                        if (InvokeOfficialToggle()) LogOut("[PAUSE] Official unpause", true);
                        else LogOut("[PAUSE] Failed to invoke official unpause", true);
                    }
                }
                s_weUsedOfficialToggle.store(false);
            }
            if (s_weAppliedPatchFreeze.load()) {
                if (nativeWritesAllowed) {
                    ApplyPatchFreeze(false);
                    ResetPracticeStepCounterToZero();
                }
                s_weAppliedPatchFreeze.store(false);
            }
            if (s_weForcedFlagPause.load()) {
                if (nativeWritesAllowed) {
                    bool p=false; if (ReadPracticePauseFlag(p) && p) WritePracticePauseFlag(false);
                }
                s_weForcedFlagPause.store(false);
            }
            if (s_weFrozeGamespeed.load()) {
                if (nativeWritesAllowed) {
                    uint8_t cur=0; if (ReadGamespeed(cur) && cur==0) WriteGamespeed(s_prevGamespeed.load());
                }
                s_weFrozeGamespeed.store(false);
            }
            if (s_weAppliedVisualPatches.load()) {
                ApplyVisualEffectPatches(false);
                s_weAppliedVisualPatches.store(false);
            }
            // A toggler map saved before this build's guards still restores NOPs on every close.
            if (nativeWritesAllowed && revivalLoaded) {
                RepairOrphanedVisualSites();
            }
        }
    }

    void FinishMenuSurfaceRefreshLocked(bool reacquire,
                                        const char* reason) {
        if (!s_menuSurfaceRefresh.active) return;
        const uint64_t token = s_menuSurfaceRefresh.token;
        if (reacquire && s_menuSurfaceMask != 0) {
            ApplyAggregateMenuVisibility(true, false);
        }
        s_menuSurfaceRefresh = MenuSurfaceRefreshState{};
        s_menuSurfaceRefreshActive.store(false, std::memory_order_release);
        std::ostringstream oss;
        oss << "[PAUSE][SURFACE_REFRESH] end token=" << token
            << " reacquire=" << (reacquire ? 1 : 0)
            << " reason=" << (reason ? reason : "unspecified");
        LogOut(oss.str(), true);
    }

    void OnMenuSurfaceVisibilityChanged(MenuSurface surface, bool visible) {
        std::lock_guard<std::mutex> lock(s_menuSurfaceMutex);
        const uint8_t previous = s_menuSurfaceMask;
        const uint8_t next = UpdatedMenuSurfaceMask(previous, surface, visible);
        if (previous == next) {
            return;
        }

        FinishMenuSurfaceRefreshLocked(true, "surface ownership changed");

        s_menuSurfaceMask = next;
        const bool wasVisible = previous != 0;
        const bool isVisible = next != 0;
        if (wasVisible == isVisible) {
            s_menuVisible.store(isVisible, std::memory_order_release);
            std::ostringstream nested;
            nested << "[PAUSE] Surface mask 0x" << std::hex
                   << static_cast<int>(previous) << "->0x"
                   << static_cast<int>(next) << " (pause retained)";
            LogOut(nested.str(), detailedLogging.load(std::memory_order_relaxed));
            return;
        }

        ApplyAggregateMenuVisibility(isVisible);
    }

    bool BeginMenuSurfaceRefresh(MenuSurface surface, uint64_t& tokenOut) {
        tokenOut = 0;
        std::lock_guard<std::mutex> lock(s_menuSurfaceMutex);
        const uint8_t bit = MenuSurfaceBit(surface);
        if (s_menuSurfaceRefresh.active || s_menuSurfaceMask != bit ||
            !s_menuVisible.load(std::memory_order_acquire) ||
            !IsPausedOrFrozen()) {
            return false;
        }

        uint64_t token = s_nextMenuSurfaceRefreshToken++;
        if (token == 0) token = s_nextMenuSurfaceRefreshToken++;
        s_menuSurfaceRefresh.active = true;
        s_menuSurfaceRefresh.surface = surface;
        s_menuSurfaceRefresh.token = token;
        s_menuSurfaceRefreshActive.store(true, std::memory_order_release);

        ApplyAggregateMenuVisibility(false, false);
        if (IsPausedOrFrozen()) {
            FinishMenuSurfaceRefreshLocked(true,
                                           "physical thaw unavailable");
            return false;
        }

        tokenOut = token;
        std::ostringstream oss;
        oss << "[PAUSE][SURFACE_REFRESH] begin token=" << token
            << " surface=0x" << std::hex << static_cast<int>(bit);
        LogOut(oss.str(), true);
        return true;
    }

    bool EndMenuSurfaceRefresh(MenuSurface surface, uint64_t token) {
        std::lock_guard<std::mutex> lock(s_menuSurfaceMutex);
        if (!token || !s_menuSurfaceRefresh.active ||
            s_menuSurfaceRefresh.surface != surface ||
            s_menuSurfaceRefresh.token != token) {
            return false;
        }
        FinishMenuSurfaceRefreshLocked(true, "owner completed");
        return IsPausedOrFrozen();
    }

    void ForceCloseAllMenuSurfaces() {
        std::lock_guard<std::mutex> lock(s_menuSurfaceMutex);
        if (s_menuSurfaceMask == 0) {
            s_menuVisible.store(false, std::memory_order_release);
            return;
        }
        FinishMenuSurfaceRefreshLocked(true, "force close");
        s_menuSurfaceMask = 0;
        ApplyAggregateMenuVisibility(false);
    }

    void ReassertMenuSurfacePause(MenuSurface surface) {
        std::lock_guard<std::mutex> lock(s_menuSurfaceMutex);
        if ((s_menuSurfaceMask & MenuSurfaceBit(surface)) == 0 ||
            s_menuSurfaceMask == 0) {
            return;
        }
        if (s_menuSurfaceRefresh.active) {
            FinishMenuSurfaceRefreshLocked(true, "pause reasserted");
            return;
        }
        LogOut("[PAUSE] Visible surface lost physical pause; reasserting aggregate owner",
               true);
        ApplyAggregateMenuVisibility(true);
    }

    void OnMenuVisibilityChanged(bool visible) {
        OnMenuSurfaceVisibilityChanged(MenuSurface::ImGui, visible);
    }

    bool ReadStepCounter(uint32_t &outCounter) {
        void* p = s_practicePtr.load(); if (!p) return false;
        uintptr_t stepCounterOff = EFZ_Practice_StepCounterOffset();
        return SafeReadMemory(reinterpret_cast<uintptr_t>(p) + stepCounterOff, &outCounter, sizeof(outCounter));
    }

    bool ConsumeStepAdvance() {
        uint32_t cur = 0;
        if (!IsPracticePaused()) return false;
        if (!ReadStepCounter(cur)) return false;
        const uint32_t last = s_lastStepCounter.load(std::memory_order_relaxed);
        if (cur != last) {
            s_lastStepCounter.store(cur, std::memory_order_relaxed);
            return true;
        }
        return false;
    }

    void SetPracticeStepAdvanceCallback(StepAdvanceCallback callback) {
        s_stepAdvanceCallback.store(callback, std::memory_order_relaxed);
    }

    void ResetCachedPointers(const char* reason) {
        const bool keepPractice =
            (GetCurrentGameMode() == GameMode::Practice) &&
            !IsNetplaySuspendActive();
        void* practice = keepPractice
            ? s_practicePtr.load(std::memory_order_relaxed)
            : s_practicePtr.exchange(nullptr, std::memory_order_relaxed);
        void* battle = s_battleContext.exchange(nullptr, std::memory_order_relaxed);
        uintptr_t gamespeed = s_gamespeedAddr.exchange(0, std::memory_order_relaxed);
        s_lastStepCounter.store(0, std::memory_order_relaxed);

        if (practice || battle || gamespeed || detailedLogging.load()) {
            std::ostringstream oss;
            oss << "[PAUSE] Reset cached pointers"
                << " reason=" << (reason ? reason : "unspecified")
                << " keepPractice=" << (keepPractice ? "1" : "0")
                << " practice=0x" << std::hex << reinterpret_cast<uintptr_t>(practice)
                << " battle=0x" << reinterpret_cast<uintptr_t>(battle)
                << " gamespeed=0x" << gamespeed;
            LogOut(oss.str(), true);
        }
    }

    bool SetVisualFreeze(VisualFreezeUser user, bool freeze) {
        const uint8_t bit = static_cast<uint8_t>(user);
        std::lock_guard<std::mutex> lock(s_visualFreezeMutex);
        LiveVisualSites sites{VisualSitesBase()};
        if (!freeze) {
            const bool wasWritten = s_visualFreeze.written;
            if (!sites.base || !s_visualFreeze.Release(sites, bit)) {
                LogOut("[PAUSE][VISUAL] Restore incomplete; retained for the next release", true);
                return false;
            }
            if (wasWritten && !s_visualFreeze.written) LogOut("[PAUSE][VISUAL] Visual update sites restored", true);
            return true;
        }
        if (!sites.base) return false;
        // Under the mutex: a boundary restore that won it must not be undone.
        if (!IsPracticeContext()) return false;
        const bool wasWritten = s_visualFreeze.written;
        switch (s_visualFreeze.Acquire(sites, bit, RevivalMayHoldVisualSites())) {
        case VisualFreezeOwner::Acquired::Refused:
            LogOut("[PAUSE][VISUAL] Visual update sites are not the known originals; freeze refused", true);
            return false;
        case VisualFreezeOwner::Acquired::RolledBack:
            LogOut("[PAUSE][VISUAL] Visual freeze failed; sites rolled back", true);
            return false;
        case VisualFreezeOwner::Acquired::Held:
            break;
        }
        if (!wasWritten) LogOut("[PAUSE][VISUAL] Visual update sites NOPed", true);
        return true;
    }

    void RestoreVisualSitesAtBoundary(const char* reason) {
        s_weAppliedVisualPatches.store(false);
        std::lock_guard<std::mutex> lock(s_visualFreezeMutex);
        LiveVisualSites sites{VisualSitesBase()};
        if (!sites.base) return;
        const bool held = s_visualFreeze.written;
        const bool ok = s_visualFreeze.Boundary(sites);
        if (held || !ok) {
            std::ostringstream oss;
            oss << "[PAUSE][VISUAL] Boundary restore reason=" << (reason ? reason : "unspecified")
                << " held=" << (held ? 1 : 0) << (ok ? " OK" : " foreign bytes left alone");
            LogOut(oss.str(), true);
        }
    }

    void SetRuntimeHooksActive(bool active) {
        if (!active) {
            // Netplay entry: online mode is already published, so this only releases code bytes and bookkeeping.
            Framestep::CancelActiveState("netplay entry");
            RestoreVisualSitesAtBoundary("netplay entry");
            bool changed = false;
            if (s_practiceHooksInstalled.load(std::memory_order_relaxed)) {
                if (s_practiceTickHookCreated.load(std::memory_order_relaxed) && s_practiceTickTarget) {
                    changed |= SetCapturedHookEnabled(s_practiceTickTarget, false, "PracticeTick");
                }
                if (s_togglePauseHookCreated.load(std::memory_order_relaxed) && s_togglePauseTarget) {
                    changed |= SetCapturedHookEnabled(s_togglePauseTarget, false, "TogglePause");
                }
                s_practiceHooksEnabled.store(false, std::memory_order_relaxed);
            }
            if (s_battleContextHookInstalled.load(std::memory_order_relaxed) && s_battleContextHookTarget) {
                changed |= SetCapturedHookEnabled(s_battleContextHookTarget, false, "RenderBattleScreen");
                s_battleContextHookEnabled.store(false, std::memory_order_relaxed);
            }
            if (changed || detailedLogging.load()) {
                LogOut("[PAUSE] Runtime capture hooks suspended", true);
            }
            return;
        }

        if (g_onlineModeActive.load(std::memory_order_relaxed)) {
            return;
        }
        if (GetCurrentGameMode() != GameMode::Practice) {
            return;
        }

        EnsurePracticePtrHookInstalled();
        if (!RefreshBattleContextFromGameModeArray()) {
            EnsureBattleContextHook();
        }
    }
}
