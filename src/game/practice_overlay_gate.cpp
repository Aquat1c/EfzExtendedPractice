#include "../../include/game/practice_offsets.h"
#include "../../include/game/efzrevival_addrs.h"
#include "../../include/core/logger.h"
#include "../../include/game/practice_hotkey_gate.h"
#include "../../include/utils/minhook_utils.h"
#include "../../include/utils/network.h"
#include "../../3rdparty/minhook/include/MinHook.h"
#include <windows.h>
#include <atomic>

namespace {
    std::atomic<bool> g_overlayHooksInstalled{false};
    std::atomic<bool> g_menuVisible{false};

    extern void PracticeHotkeyGate::NotifyMenuVisibility(bool visible);

    bool MenuVisible() { return g_menuVisible.load(std::memory_order_relaxed); }

    template<typename T> bool MakeHook(void* target, void* detour, T** original, PracticeHooks::CallbackTicket& ticket) {
        if (!target) return false;
        return MinHookUtils::CreateAndEnableHook(target,
                                                 detour,
                                                 reinterpret_cast<void**>(original),
                                                 "[HOTKEY]",
                                                 "practice overlay gate", nullptr, nullptr, &ticket);
    }

    using ToggleFn = void(__thiscall*)(void* self);
    static ToggleFn oToggleHurt = nullptr;
    static ToggleFn oToggleHit  = nullptr;
    static ToggleFn oToggleDisp = nullptr;

    void __stdcall HookedToggleHurt(void* self) {
        auto execution=MinHookUtils::EnterExecution(MinHookUtils::TicketFor<&HookedToggleHurt>());
        if (execution.Admitted() && MenuVisible()) return;
        if (oToggleHurt) oToggleHurt(self);
    }
    void __stdcall HookedToggleHit(void* self) {
        auto execution=MinHookUtils::EnterExecution(MinHookUtils::TicketFor<&HookedToggleHit>());
        if (execution.Admitted() && MenuVisible()) return;
        if (oToggleHit) oToggleHit(self);
    }
    void __stdcall HookedToggleDisp(void* self) {
        auto execution=MinHookUtils::EnterExecution(MinHookUtils::TicketFor<&HookedToggleDisp>());
        if (execution.Admitted() && MenuVisible()) return;
        if (oToggleDisp) oToggleDisp(self);
    }

    void InstallOverlayHooksInternal() {
        if (g_overlayHooksInstalled.load()) return;
        HMODULE hRev = GetModuleHandleA("EfzRevival.dll");
        if (!hRev) return;

        EfzRevivalVersion ver = GetEfzRevivalVersion();
        bool supported = IsEfzRevivalVersionSupported();
        char buf[256];
        snprintf(buf, sizeof(buf), "[HOTKEY] Overlay gate: version=%s supported=%d", 
                 EfzRevivalVersionName(ver), supported);
        LogOut(buf, true);

        // DISABLED: hitbox/hurtbox/frame display hooks crash on all Revival versions
        LogOut("[HOTKEY] Overlay hooks disabled (hitbox/hurtbox/frame display hooks cause crashes)", true);
        return;

        if (!supported) {
            LogOut("[HOTKEY] Overlay hooks skipped for unsupported Revival version", true);
            return;
        }

        uintptr_t hurtRva = EFZ_RVA_ToggleHurtboxDisplay();
        uintptr_t hitRva = EFZ_RVA_ToggleHitboxDisplay();
        uintptr_t dispRva = EFZ_RVA_ToggleFrameDisplay();

        if (!hurtRva || !hitRva || !dispRva) {
            snprintf(buf, sizeof(buf), "[HOTKEY] Overlay hooks skipped (missing RVAs: hurt=%#x hit=%#x disp=%#x)", 
                     hurtRva, hitRva, dispRva);
            LogOut(buf, true);
            return;
        }
        
        int installed = 0;
        if (MakeHook(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(hRev)+hurtRva), &HookedToggleHurt, &oToggleHurt, MinHookUtils::TicketFor<&HookedToggleHurt>())) { ++installed; }
        if (MakeHook(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(hRev)+hitRva), &HookedToggleHit, &oToggleHit, MinHookUtils::TicketFor<&HookedToggleHit>())) { ++installed; }
        if (MakeHook(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(hRev)+dispRva), &HookedToggleDisp, &oToggleDisp, MinHookUtils::TicketFor<&HookedToggleDisp>())) { ++installed; }
        if (installed) {
            g_overlayHooksInstalled.store(true);
            LogOut("[HOTKEY] Overlay/Pause toggle hooks installed (" + std::to_string(installed) + ")", true);
        }
    }
}

namespace PracticeOverlayGate {
    void SetMenuVisible(bool v) { g_menuVisible.store(v, std::memory_order_relaxed); }
    void EnsureInstalled() { InstallOverlayHooksInternal(); }
}
