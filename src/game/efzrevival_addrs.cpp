#include "../include/game/efzrevival_addrs.h"
#include "../include/utils/network.h"
#include "../include/core/logger.h"
#include <atomic>
#include <sstream>
#include <windows.h>

static inline bool IsE() {
    EfzRevivalVersion v = GetEfzRevivalVersion();
    return v == EfzRevivalVersion::Revival102e || v == EfzRevivalVersion::Revival102g;
}
static inline bool IsFClassic() {
    return GetEfzRevivalVersion() == EfzRevivalVersion::Revival102f && IsEfzRevival102fClassicBuild();
}
static inline bool IsFSubframe() {
    return GetEfzRevivalVersion() == EfzRevivalVersion::Revival102f && IsEfzRevival102fSubframeBuild();
}
static inline bool IsLegacyEFamily() {
    return GetEfzRevivalVersion() == EfzRevivalVersion::Revival102e || IsFClassic();
}
static inline bool IsH() {
    EfzRevivalVersion v = GetEfzRevivalVersion();
    return v == EfzRevivalVersion::Revival102h || v == EfzRevivalVersion::Revival102i;
}
static inline bool IsI() {
    return GetEfzRevivalVersion() == EfzRevivalVersion::Revival102i;
}
static inline const EfzRevival102jProfile* JProfile() {
    if (GetEfzRevivalVersion() != EfzRevivalVersion::Revival102j) return nullptr;
    return FindEfzRevival102jProfileByBuild(GetEfzRevival102jBuild());
}
static inline bool IsJ() {
    return JProfile() != nullptr;
}

bool EFZ_SupportsNativePracticeSideSwitch() {
    const EfzRevivalVersion v = GetEfzRevivalVersion();
    return IsEfzRevivalVersionSupported(v)
        && v != EfzRevivalVersion::Vanilla
        && v != EfzRevivalVersion::Revival102j;
}

static void LogAddrOnce(const char* label, uintptr_t rva) {
    static std::atomic<bool> s_once{false};
    if (s_once.exchange(true)) return;
    std::ostringstream oss;
    const char* verName = "other";
    if (IsE() || GetEfzRevivalVersion() == EfzRevivalVersion::Revival102f) {
        EfzRevivalVersion vdet = GetEfzRevivalVersion();
        verName = (vdet == EfzRevivalVersion::Revival102g) ? "1.02g" :
                  (vdet == EfzRevivalVersion::Revival102f) ? EfzRevivalDllFlavorName(GetEfzRevivalDllFlavor()) : "1.02e";
    } else if (IsH()) {
        EfzRevivalVersion vdet = GetEfzRevivalVersion();
        verName = (vdet == EfzRevivalVersion::Revival102i) ? "1.02i" : "1.02h";
    } else if (IsJ()) {
        verName = "1.02j";
    }
    oss << "[ADDR] Version=" << verName
        << " | " << label << " RVA=0x" << std::hex << rva;
    HMODULE h = GetModuleHandleA("EfzRevival.dll");
    if (h && rva) {
        oss << " VA=0x" << (reinterpret_cast<uintptr_t>(h) + rva);
    }
    LogOut(oss.str(), true);
}

int EFZ_PatchToggleUnfreezeParam() {
    EfzRevivalVersion v = GetEfzRevivalVersion();
    if (IsFSubframe() || v == EfzRevivalVersion::Revival102g || IsH() || IsJ()) return 3; // 0=freeze, 3=normal speed
    return 1; // 1.02e and classic 1.02f behavior
}

uintptr_t EFZ_RVA_PatchToggler() {
    uintptr_t r = 0;
    EfzRevivalVersion v = GetEfzRevivalVersion();
    if (IsFSubframe() || v == EfzRevivalVersion::Revival102g) r = 0x006B4C0;
    else if (IsLegacyEFamily()) r = 0x006B2A0;
    else if (IsI()) r = 0x006BD50; // 1.02i (different from 1.02h)
    else if (IsH()) r = 0x006BB00; // 1.02h
    else if (const EfzRevival102jProfile* j = JProfile()) r = j->patchTogglerRva;
    LogAddrOnce("PatchToggler", r);
    return r;
}

uintptr_t EFZ_RVA_PatchCtx() {
    uintptr_t r = 0;
    EfzRevivalVersion v = GetEfzRevivalVersion();
    if (v == EfzRevivalVersion::Revival102f) r = 0x00A0760;
    else if (IsE()) r = 0x00A0760;
    else if (IsI()) r = 0x00A1790; // 1.02i
    else if (IsH()) r = 0x00A0780; // 1.02h
    else if (const EfzRevival102jProfile* j = JProfile()) r = j->patchContextRva;
    LogAddrOnce("PatchCtx", r);
    return r;
}

uintptr_t EFZ_RVA_TogglePause() {
    uintptr_t r = 0;
    EfzRevivalVersion v = GetEfzRevivalVersion();
    if (IsFSubframe()) r = 0x0075990;
    else if (IsFClassic()) r = 0x0075750;
        else if (v == EfzRevivalVersion::Revival102e) r = 0x0075720;
        else if (v == EfzRevivalVersion::Revival102g) r = 0x00759C0;
    else if (IsI()) r = 0x0076710;  // 1.02i
    else if (IsH()) r = 0x0076170;  // 1.02h
    else if (const EfzRevival102jProfile* j = JProfile()) r = j->togglePauseRva;
    LogAddrOnce("TogglePause", r);
    return r;
}

uintptr_t EFZ_RVA_PracticeTick() {
    uintptr_t r = 0;
        EfzRevivalVersion v = GetEfzRevivalVersion();
        if (v == EfzRevivalVersion::Revival102e) r = 0x0074F70;
        else if (IsFSubframe()) r = 0x00759E0;  // 1.02f subframe per-frame Practice tick
        else if (IsFClassic()) r = 0x0074FA0;  // 1.02f classic active Practice tick
        else if (v == EfzRevivalVersion::Revival102g) r = 0x0075210;
        // 1.02h/i: per-frame Practice update loop (ECX = Practice controller);
        // the 0x74F40/0x74FF0 helper path is unreliable for match-entry capture.
        else if (IsI()) r = 0x0075F60;  // 1.02i
        else if (IsH()) r = 0x00759C0;  // 1.02h
        else if (const EfzRevival102jProfile* j = JProfile()) r = j->practiceStepRenderRva;
    LogAddrOnce("PracticeTick", r);
    return r;
}

uintptr_t EFZ_RVA_RenderBattleScreen() {
    uintptr_t r = 0;
    // efz.exe RVA (VA 0x7642A0 at image base 0x400000); callers add the efz.exe base.
    r = 0x003642A0;
    LogAddrOnce("RenderBattleScreen", r);
    return r;
}

uintptr_t EFZ_RVA_RenderContextGlobal() {
    uintptr_t r = 0;
    EfzRevivalVersion v = GetEfzRevivalVersion();
    if (v == EfzRevivalVersion::Revival102f || IsE()) r = 0x00A0778;
    else if (IsH()) r = 0x00A0798;
    else if (IsI()) r = 0x00A17A8;
    else if (const EfzRevival102jProfile* j = JProfile()) r = j->renderContextGlobalRva;
    LogAddrOnce("RenderContextGlobal", r);
    return r;
}

uintptr_t EFZ_RVA_PracticeControllerPtr() {
    // Disabled: these RVAs are Revival session-pointer globals used by
    // InGameNetplay, not Practice objects.
    return 0;
}

uintptr_t EFZ_RVA_PracticeDispatcher() {
    uintptr_t r = 0;
    EfzRevivalVersion v = GetEfzRevivalVersion();
    if (v == EfzRevivalVersion::Revival102e) r = 0x00759F0;
    else if (IsFSubframe()) r = 0x0075C90;
    else if (IsFClassic()) r = 0x0075A20;
    else if (v == EfzRevivalVersion::Revival102g) r = 0x0075CC0;
    else if (IsI()) r = 0x0076A30;
    else if (IsH()) r = 0x0076490;
    else if (const EfzRevival102jProfile* j = JProfile()) r = j->practiceDispatcherRva;
    // Unsupported versions return 0; never guess addresses.
    LogAddrOnce("PracticeDispatcher", r);
    return r;
}

uintptr_t EFZ_Practice_PauseFlagOffset() {
    if (IsJ()) return 0xDC;
    return 0xB4;
}

uintptr_t EFZ_Practice_StepFlagOffset() {
    if (IsJ()) return 0xD4;
    return 0xAC;
}

uintptr_t EFZ_Practice_StepCounterOffset() {
    if (IsJ()) return 0xD8;
    return 0xB0;
}

uintptr_t EFZ_Practice_PauseHotkeyOffset() {
    EfzRevivalVersion v = GetEfzRevivalVersion();
    if (IsJ()) return 0x200;
    if (v == EfzRevivalVersion::Revival102i) return 0x1D8;
    if (v == EfzRevivalVersion::Revival102e
        || v == EfzRevivalVersion::Revival102f
        || v == EfzRevivalVersion::Revival102g
        || v == EfzRevivalVersion::Revival102h) {
        return 0x1D4;
    }
    return 0;
}

uintptr_t EFZ_Practice_StepHotkeyOffset() {
    EfzRevivalVersion v = GetEfzRevivalVersion();
    if (IsJ()) return 0x204;
    if (v == EfzRevivalVersion::Revival102i) return 0x1DC;
    if (v == EfzRevivalVersion::Revival102e
        || v == EfzRevivalVersion::Revival102f
        || v == EfzRevivalVersion::Revival102g
        || v == EfzRevivalVersion::Revival102h) {
        return 0x1D8;
    }
    return 0;
}

uintptr_t EFZ_Practice_SaveHotkeyOffset() {
    EfzRevivalVersion v = GetEfzRevivalVersion();
    if (IsJ()) return 0x208;
    if (v == EfzRevivalVersion::Revival102i) return 0x1E0;
    if (v == EfzRevivalVersion::Revival102e
        || v == EfzRevivalVersion::Revival102f
        || v == EfzRevivalVersion::Revival102g
        || v == EfzRevivalVersion::Revival102h) {
        return 0x1DC;
    }
    return 0;
}

uintptr_t EFZ_Practice_LoadHotkeyOffset() {
    EfzRevivalVersion v = GetEfzRevivalVersion();
    if (IsJ()) return 0x20C;
    if (v == EfzRevivalVersion::Revival102i) return 0x1E4;
    if (v == EfzRevivalVersion::Revival102e
        || v == EfzRevivalVersion::Revival102f
        || v == EfzRevivalVersion::Revival102g
        || v == EfzRevivalVersion::Revival102h) {
        return 0x1E0;
    }
    return 0;
}

uintptr_t EFZ_RVA_ToggleHurtboxDisplay() {
    uintptr_t r = 0;
    EfzRevivalVersion v = GetEfzRevivalVersion();
    if (IsFSubframe()) r = 0x00753B0;  // every version listed here toggles this[183]
    else if (IsFClassic()) r = 0x0075170;
    else if (v == EfzRevivalVersion::Revival102e) r = 0x0075140;
    else if (v == EfzRevivalVersion::Revival102g) r = 0x00753E0;
    LogAddrOnce("ToggleHurtboxDisplay", r);
    return r;
}

uintptr_t EFZ_RVA_ToggleHitboxDisplay() {
    uintptr_t r = 0;
    EfzRevivalVersion v = GetEfzRevivalVersion();
    if (IsFSubframe()) r = 0x00753D0;  // every version listed here toggles this[182]
    else if (IsFClassic()) r = 0x0075190;
    else if (v == EfzRevivalVersion::Revival102e) r = 0x0075160;
    else if (v == EfzRevivalVersion::Revival102g) r = 0x0075400;
    LogAddrOnce("ToggleHitboxDisplay", r);
    return r;
}

uintptr_t EFZ_RVA_ToggleFrameDisplay() {
    uintptr_t r = 0;
    EfzRevivalVersion v = GetEfzRevivalVersion();
    if (IsFSubframe()) r = 0x0075950;  // every version listed here toggles this[181]
    else if (IsFClassic()) r = 0x0075710;
    else if (v == EfzRevivalVersion::Revival102e) r = 0x00756E0;
    else if (v == EfzRevivalVersion::Revival102g) r = 0x0075980;
    LogAddrOnce("ToggleFrameDisplay", r);
    return r;
}

uintptr_t EFZ_RVA_LoadState() {
    uintptr_t r = 0;
    EfzRevivalVersion v = GetEfzRevivalVersion();
    if (v == EfzRevivalVersion::Revival102e) r = 0x0075910;
    else if (IsFSubframe()) r = 0x0075BB0;
    else if (IsFClassic()) r = 0x0075940;
    else if (v == EfzRevivalVersion::Revival102g) r = 0x0075BE0;
    else if (v == EfzRevivalVersion::Revival102h) r = 0x00763B0;
    else if (v == EfzRevivalVersion::Revival102i) r = 0x0076950;
    else if (const EfzRevival102jProfile* j = JProfile()) r = j->loadStateRva;
    LogAddrOnce("LoadState", r);
    return r;
}

uintptr_t EFZ_RVA_SaveState() {
    uintptr_t r = 0;
    EfzRevivalVersion v = GetEfzRevivalVersion();
    if (v == EfzRevivalVersion::Revival102e) r = 0x0075980;
    else if (IsFSubframe()) r = 0x0075C20;
    else if (IsFClassic()) r = 0x00759B0;
    else if (v == EfzRevivalVersion::Revival102g) r = 0x0075C50;
    else if (v == EfzRevivalVersion::Revival102h) r = 0x0076420;
    else if (v == EfzRevivalVersion::Revival102i) r = 0x00769C0;
    else if (const EfzRevival102jProfile* j = JProfile()) r = j->saveStateRva;
    LogAddrOnce("SaveState", r);
    return r;
}

uintptr_t EFZ_RVA_PracticeHotkeyHandler() {
    uintptr_t r = 0;
    EfzRevivalVersion v = GetEfzRevivalVersion();
    // e/f/g/h: this+476=Save, this+480=Load; i: this+480=Save, this+484=Load
    if (v == EfzRevivalVersion::Revival102e) r = 0x00759F0;
    else if (IsFSubframe()) r = 0x0075C90;
    else if (IsFClassic()) r = 0x0075A20;
    else if (v == EfzRevivalVersion::Revival102g) r = 0x0075CC0;
    else if (v == EfzRevivalVersion::Revival102h) r = 0x0076490;
    else if (v == EfzRevivalVersion::Revival102i) r = 0x0076A30;
    else if (const EfzRevival102jProfile* j = JProfile()) r = j->practiceDispatcherRva;
    LogAddrOnce("PracticeHotkeyHandler", r);
    return r;
}

// Recording-mode savestate path only; NOT normal Practice mode.
uintptr_t EFZ_RVA_ReplayLoadState() {
    uintptr_t r = 0;
    EfzRevivalVersion v = GetEfzRevivalVersion();
    if (v == EfzRevivalVersion::Revival102e) r = 0x0077060;
    else if (v == EfzRevivalVersion::Revival102f) r = 0x0077090;
    else if (v == EfzRevivalVersion::Revival102g) r = 0x0077330;
    else if (v == EfzRevivalVersion::Revival102h) r = 0x0077B00;
    else if (v == EfzRevivalVersion::Revival102i) r = 0x00780E0;
    LogAddrOnce("ReplayLoadState", r);
    return r;
}

uintptr_t EFZ_RVA_ReplaySaveState() {
    uintptr_t r = 0;
    EfzRevivalVersion v = GetEfzRevivalVersion();
    if (v == EfzRevivalVersion::Revival102e) r = 0x00770C0;
    else if (v == EfzRevivalVersion::Revival102f) r = 0x00770F0;
    else if (v == EfzRevivalVersion::Revival102g) r = 0x0077390;
    else if (v == EfzRevivalVersion::Revival102h) r = 0x0077B60;
    else if (v == EfzRevivalVersion::Revival102i) r = 0x0078140;
    LogAddrOnce("ReplaySaveState", r);
    return r;
}
