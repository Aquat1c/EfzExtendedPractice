#include "../include/game/hud_disable.h"
#include "../include/game/game_state.h"
#include "../include/core/logger.h"
#include "../include/core/memory.h"
#include "../include/core/globals.h"
#include "../include/utils/utilities.h"
#include "../include/utils/minhook_utils.h"
#include "../include/game/practice_patch.h"   // FormatHexAddress
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>

// efz.exe RVAs (base 0x400000); VAs in comments.
namespace {

constexpr uintptr_t RENDER_BATTLE_SCREEN_OFFSET = 0x3642A0; // VA 0x7642A0
constexpr uintptr_t RENDER_GAME_HUD_OFFSET      = 0x35E240; // VA 0x75E240 (top HUD)
constexpr uintptr_t RENDER_METERS_OFFSET        = 0x360060; // VA 0x760060 (bottom HUD)
constexpr uintptr_t RENDER_PLAYER_STATS_OFFSET  = 0x3623B0; // VA 0x7623B0 (combo panel)
constexpr uintptr_t BLIT_TRANSPARENCY_OFFSET    = 0x009A90; // VA 0x409A90
constexpr uintptr_t BLIT_PALETTE_OFFSET         = 0x00A0B0; // VA 0x40A0B0

// Exact EFZ 1.02e retail entry bytes; refuse every hook as a unit if any
// target differs - a generic x86 prologue proves nothing about this body.
constexpr uint8_t SIG_RENDER_BATTLE_SCREEN[] =
    {0x55,0x8B,0xEC,0x51,0x89,0x4D,0xFC,0x8B,0x45,0xFC,0x8B,0x48};
constexpr uint8_t SIG_RENDER_GAME_HUD[] =
    {0x55,0x8B,0xEC,0x81,0xEC,0xAC,0x04,0x00,0x00,0x53,0x89,0x8D};
constexpr uint8_t SIG_RENDER_METERS[] =
    {0x55,0x8B,0xEC,0x81,0xEC,0x88,0x05,0x00,0x00,0x53,0x56,0x89};
constexpr uint8_t SIG_RENDER_PLAYER_STATS[] =
    {0x55,0x8B,0xEC,0x81,0xEC,0x2C,0x01,0x00,0x00,0x89,0x8D,0xD4};
constexpr uint8_t SIG_BLIT_TRANSPARENCY[] =
    {0x55,0x8B,0xEC,0x81,0xEC,0x04,0x01,0x00,0x00,0x53,0x56,0x57};
constexpr uint8_t SIG_BLIT_PALETTE[] =
    {0x55,0x8B,0xEC,0x81,0xEC,0x08,0x01,0x00,0x00,0x53,0x56,0x57};

constexpr uintptr_t BATTLE_CTX_RESMGR_OFFSET = 28;     // battleContext+28 -> resourceManager
constexpr uintptr_t HUD_GATE_FIELD_OFFSET    = 82444;  // resourceManager+82444 (int16): HUD draws only when <= 0
constexpr int16_t   HUD_GATE_FORCE_VALUE     = 30000;  // the value the game writes during round intros

enum Seam { SEAM_NONE = 0, SEAM_TOP = 1, SEAM_BOTTOM = 2 };
// No static TLS in a dynamically loaded DLL on the XP target; Win32 TLS API.
DWORD s_seamTls = TLS_OUT_OF_INDEXES;

int GetCurrentSeam() {
    if (s_seamTls == TLS_OUT_OF_INDEXES) return SEAM_NONE;
    return static_cast<int>(reinterpret_cast<INT_PTR>(TlsGetValue(s_seamTls)));
}

void SetCurrentSeam(int seam) {
    if (s_seamTls == TLS_OUT_OF_INDEXES) return;
    (void)TlsSetValue(s_seamTls,
                      reinterpret_cast<LPVOID>(static_cast<INT_PTR>(seam)));
}

void ReleaseSeamTls() {
    if (s_seamTls == TLS_OUT_OF_INDEXES) return;
    (void)TlsFree(s_seamTls);
    s_seamTls = TLS_OUT_OF_INDEXES;
}

using tRenderBattleScreen = int(__thiscall*)(void* battleContext);
using tRenderGameHUD      = unsigned(__thiscall*)(void* gameSystem);
using tRenderMeters       = void(__thiscall*)(void* gameSystem);
using tRenderPlayerStats  = int(__thiscall*)(void* gameSystem, void* playerData);
using tBlitTransparency   = int(__thiscall*)(void* gfx, int dX, int dY, int dR, int dB,
    int surf, int sX, int sY, int sR, int sB, char key, int flip);
using tBlitPalette        = int(__thiscall*)(void* gfx, int dX, int dY, int dR, int dB,
    int surf, int sX, int sY, int sR, int sB, unsigned char key, int flip, char pal);

tRenderBattleScreen oRenderBattleScreen = nullptr;
tRenderGameHUD      oRenderGameHUD       = nullptr;
tRenderMeters       oRenderMeters        = nullptr;
tRenderPlayerStats  oRenderPlayerStats   = nullptr;
tBlitTransparency   oBlitTransparency    = nullptr;
tBlitPalette        oBlitPalette         = nullptr;

std::atomic<bool>     s_hidden{false};       // master (whole HUD)
std::atomic<uint32_t> s_elementMask{0};      // per-element disable bits
std::atomic<bool>     s_created{false};

struct HookRec { uintptr_t addr; const char* name; };
HookRec s_hooks[6] = {};
int     s_hookCount = 0;

// Blits are identified by seam + destination top-Y (320x240 space);
// true = skip this draw.
bool ShouldSkipElement(int seam, int destY, uint32_t mask) {
    if (seam == SEAM_TOP) {
        if ((mask & HudDisable::ElemTopBar)     && destY == 0)  return true;
        if ((mask & HudDisable::ElemTimer)      && destY == 9)  return true;
        if ((mask & HudDisable::ElemPortraits)  && destY == 14) return true;
        if ((mask & HudDisable::ElemHpBars)     && destY == 18) return true;
        if ((mask & HudDisable::ElemRoundDots)  && destY == 33) return true;
        if ((mask & HudDisable::ElemNameplates) && destY == 40) return true;
    } else if (seam == SEAM_BOTTOM) {
        if ((mask & HudDisable::ElemBottomBar)  && destY == 206) return true;
        if ((mask & HudDisable::ElemSpMeter)    && (destY == 224 || destY == 209)) return true;
        if ((mask & HudDisable::ElemRfGauge)    && destY == 232) return true;
    }
    return false;
}

// Whole-HUD suppression via the engine's own gate.
int __fastcall Hooked_renderBattleScreen(void* battleContext, void* /*edx*/) {
    auto execution=MinHookUtils::EnterExecution(MinHookUtils::TicketFor<&Hooked_renderBattleScreen>());
    if (!execution.Admitted()) return oRenderBattleScreen(battleContext);
    if (!s_hidden.load(std::memory_order_relaxed) ||
        !PracticeBattleHooksOpen() || !battleContext) {
        return oRenderBattleScreen(battleContext);
    }
    uintptr_t resMgr = 0;
    if (!SafeReadMemory(reinterpret_cast<uintptr_t>(battleContext) + BATTLE_CTX_RESMGR_OFFSET,
                        &resMgr, sizeof(resMgr)) || !resMgr) {
        return oRenderBattleScreen(battleContext);
    }
    const uintptr_t gateAddr = resMgr + HUD_GATE_FIELD_OFFSET;
    int16_t saved = 0;
    if (!SafeReadMemory(gateAddr, &saved, sizeof(saved))) {
        return oRenderBattleScreen(battleContext);
    }
    int16_t forced = HUD_GATE_FORCE_VALUE;
    SafeWriteMemory(gateAddr, &forced, sizeof(forced));
    const int result = oRenderBattleScreen(battleContext);
    SafeWriteMemory(gateAddr, &saved, sizeof(saved)); // restore within the same synchronous call
    return result;
}

// Seam markers: let the blit filter tell HUD draws from gameplay sprites.
unsigned __fastcall Hooked_renderGameHUD(void* gs, void* /*edx*/) {
    auto execution=MinHookUtils::EnterExecution(MinHookUtils::TicketFor<&Hooked_renderGameHUD>());
    if (!execution.Admitted()) return oRenderGameHUD(gs);
    if (!PracticeBattleHooksOpen()) return oRenderGameHUD(gs);
    const int prev = GetCurrentSeam(); SetCurrentSeam(SEAM_TOP);
    const unsigned r = oRenderGameHUD(gs);
    SetCurrentSeam(prev);
    return r;
}
void __fastcall Hooked_renderMeters(void* gs, void* /*edx*/) {
    auto execution=MinHookUtils::EnterExecution(MinHookUtils::TicketFor<&Hooked_renderMeters>());
    if (!execution.Admitted()) { oRenderMeters(gs); return; }
    if (!PracticeBattleHooksOpen()) { oRenderMeters(gs); return; }
    const int prev = GetCurrentSeam(); SetCurrentSeam(SEAM_BOTTOM);
    oRenderMeters(gs);
    SetCurrentSeam(prev);
}

// Skipped on master hide or the per-element combo bit.
int __fastcall Hooked_renderPlayerStats(void* gameSystem, void* /*edx*/, void* playerData) {
    auto execution=MinHookUtils::EnterExecution(MinHookUtils::TicketFor<&Hooked_renderPlayerStats>());
    if (!execution.Admitted()) return oRenderPlayerStats(gameSystem, playerData);
    if (PracticeBattleHooksOpen()) {
        if (s_hidden.load(std::memory_order_relaxed) ||
            (s_elementMask.load(std::memory_order_relaxed) & HudDisable::ElemComboPanel)) {
            return 0;
        }
    }
    return oRenderPlayerStats(gameSystem, playerData);
}

// Per-element filter, active only inside a HUD seam.
int __fastcall Hooked_blitTransparency(void* gfx, void* /*edx*/, int dX, int dY, int dR, int dB,
    int surf, int sX, int sY, int sR, int sB, char key, int flip) {
    auto execution=MinHookUtils::EnterExecution(MinHookUtils::TicketFor<&Hooked_blitTransparency>());
    if (!execution.Admitted()) return oBlitTransparency(gfx,dX,dY,dR,dB,surf,sX,sY,sR,sB,key,flip);
    const int seam = GetCurrentSeam();
    if (seam != SEAM_NONE && !s_hidden.load(std::memory_order_relaxed) &&
        PracticeBattleHooksOpen()) {
        const uint32_t mask = s_elementMask.load(std::memory_order_relaxed);
        if (mask && ShouldSkipElement(seam, dY, mask)) return 1;
    }
    return oBlitTransparency(gfx, dX, dY, dR, dB, surf, sX, sY, sR, sB, key, flip);
}
int __fastcall Hooked_blitPalette(void* gfx, void* /*edx*/, int dX, int dY, int dR, int dB,
    int surf, int sX, int sY, int sR, int sB, unsigned char key, int flip, char pal) {
    auto execution=MinHookUtils::EnterExecution(MinHookUtils::TicketFor<&Hooked_blitPalette>());
    if (!execution.Admitted()) return oBlitPalette(gfx,dX,dY,dR,dB,surf,sX,sY,sR,sB,key,flip,pal);
    const int seam = GetCurrentSeam();
    if (seam != SEAM_NONE && !s_hidden.load(std::memory_order_relaxed) &&
        PracticeBattleHooksOpen()) {
        const uint32_t mask = s_elementMask.load(std::memory_order_relaxed);
        if (mask && ShouldSkipElement(seam, dY, mask)) return 1;
    }
    return oBlitPalette(gfx, dX, dY, dR, dB, surf, sX, sY, sR, sB, key, flip, pal);
}

bool SignatureMatches(uintptr_t address, const uint8_t* expected, size_t length,
                      const char* name) {
    if (MinHookUtils::HasOwnedTarget(reinterpret_cast<void*>(address))) return true;
    uint8_t actual[16] = {};
    if (!expected || length == 0 || length > sizeof(actual) ||
        !SafeReadMemory(address, actual, static_cast<uint32_t>(length)) ||
        std::memcmp(actual, expected, length) != 0) {
        LogOut(std::string("[HUD_DISABLE] Refusing unsupported target signature: ") +
               (name ? name : "<unknown>"), true);
        return false;
    }
    return true;
}

bool Hook(uintptr_t base, uintptr_t off, void* detour, void** orig, const char* name,
          PracticeHooks::CallbackTicket& ticket) {
    const uintptr_t addr=base+off;
    int slot=0;
    while(slot<s_hookCount && s_hooks[slot].addr!=addr) ++slot;
    if(slot==s_hookCount && s_hookCount>=6) return false;
    if (!MinHookUtils::CreateHook(reinterpret_cast<void*>(addr),detour,orig,
            "[HUD_DISABLE]",name,nullptr,&ticket)) return false;
    // Same-owner retries reuse their slot; never erase a retained original.
    if(slot==s_hookCount) s_hooks[s_hookCount++]={addr,name};
    if(!MinHookUtils::EnableHook(reinterpret_cast<void*>(addr),"[HUD_DISABLE]",name)) {
        (void)MinHookUtils::RemoveHook(reinterpret_cast<void*>(addr),"[HUD_DISABLE]",name);
        return false;
    }
    return true;
}

bool RollBackHooks() {
    for (int i = s_hookCount - 1; i >= 0; --i) {
        (void)MinHookUtils::DisableHook(reinterpret_cast<void*>(s_hooks[i].addr),
                                        "[HUD_DISABLE]", s_hooks[i].name);
        if (MinHookUtils::RemoveHook(reinterpret_cast<void*>(s_hooks[i].addr),
                                     "[HUD_DISABLE]", s_hooks[i].name)) {
            s_hooks[i] = {};
        }
    }
    int retained = 0;
    for (const HookRec& hook : s_hooks) {
        if (hook.addr != 0) s_hooks[retained++] = hook;
    }
    for (int i = retained; i < 6; ++i) s_hooks[i] = {};
    s_hookCount = retained;
    if (retained != 0) {
        LogOut("[HUD_DISABLE] Hook removal incomplete; retaining trampolines and TLS", true);
        return false;
    }
    oRenderBattleScreen = nullptr;
    oRenderGameHUD = nullptr;
    oRenderMeters = nullptr;
    oRenderPlayerStats = nullptr;
    oBlitTransparency = nullptr;
    oBlitPalette = nullptr;
    return true;
}

} // namespace

namespace HudDisable {

void Install() {
    if (s_created.load(std::memory_order_acquire) && MinHookUtils::EnableOwnedTargets("[HUD_DISABLE]")) return;
    uintptr_t base = GetEFZBase();
    if (!base) { LogOut("[HUD_DISABLE] Failed to get game base address.", true); return; }

    const bool signaturesOk =
        SignatureMatches(base + RENDER_BATTLE_SCREEN_OFFSET, SIG_RENDER_BATTLE_SCREEN,
                         sizeof(SIG_RENDER_BATTLE_SCREEN), "renderBattleScreen") &&
        SignatureMatches(base + RENDER_GAME_HUD_OFFSET, SIG_RENDER_GAME_HUD,
                         sizeof(SIG_RENDER_GAME_HUD), "renderGameHUD") &&
        SignatureMatches(base + RENDER_METERS_OFFSET, SIG_RENDER_METERS,
                         sizeof(SIG_RENDER_METERS), "renderMeters") &&
        SignatureMatches(base + RENDER_PLAYER_STATS_OFFSET, SIG_RENDER_PLAYER_STATS,
                         sizeof(SIG_RENDER_PLAYER_STATS), "renderPlayerStatsPanel") &&
        SignatureMatches(base + BLIT_TRANSPARENCY_OFFSET, SIG_BLIT_TRANSPARENCY,
                         sizeof(SIG_BLIT_TRANSPARENCY), "blitTransparency") &&
        SignatureMatches(base + BLIT_PALETTE_OFFSET, SIG_BLIT_PALETTE,
                         sizeof(SIG_BLIT_PALETTE), "blitPalette");
    if (!signaturesOk) {
        LogOut("[HUD_DISABLE] HUD hiding unavailable on this game build; HUD options have no effect", true);
        return;
    }

    if (s_seamTls == TLS_OUT_OF_INDEXES) {
        s_seamTls = TlsAlloc();
        if (s_seamTls == TLS_OUT_OF_INDEXES) {
            LogOut("[HUD_DISABLE] TlsAlloc failed; HUD hooks disabled", true);
            return;
        }
    }

    bool installed = Hook(base, RENDER_BATTLE_SCREEN_OFFSET,
                          reinterpret_cast<void*>(&Hooked_renderBattleScreen),
                          reinterpret_cast<void**>(&oRenderBattleScreen), "renderBattleScreen", MinHookUtils::TicketFor<&Hooked_renderBattleScreen>());
    if (installed) installed = Hook(base, RENDER_PLAYER_STATS_OFFSET,
                                    reinterpret_cast<void*>(&Hooked_renderPlayerStats),
                                    reinterpret_cast<void**>(&oRenderPlayerStats), "renderPlayerStatsPanel", MinHookUtils::TicketFor<&Hooked_renderPlayerStats>());
    if (installed) installed = Hook(base, RENDER_GAME_HUD_OFFSET,
                                    reinterpret_cast<void*>(&Hooked_renderGameHUD),
                                    reinterpret_cast<void**>(&oRenderGameHUD), "renderGameHUD", MinHookUtils::TicketFor<&Hooked_renderGameHUD>());
    if (installed) installed = Hook(base, RENDER_METERS_OFFSET,
                                    reinterpret_cast<void*>(&Hooked_renderMeters),
                                    reinterpret_cast<void**>(&oRenderMeters), "renderMeters", MinHookUtils::TicketFor<&Hooked_renderMeters>());
    if (installed) installed = Hook(base, BLIT_TRANSPARENCY_OFFSET,
                                    reinterpret_cast<void*>(&Hooked_blitTransparency),
                                    reinterpret_cast<void**>(&oBlitTransparency), "blitTransparency", MinHookUtils::TicketFor<&Hooked_blitTransparency>());
    if (installed) installed = Hook(base, BLIT_PALETTE_OFFSET,
                                    reinterpret_cast<void*>(&Hooked_blitPalette),
                                    reinterpret_cast<void**>(&oBlitPalette), "blitPalette", MinHookUtils::TicketFor<&Hooked_blitPalette>());

    if (installed && s_hookCount == 6) {
        s_created.store(true, std::memory_order_release);
        LogOut("[HUD_DISABLE] Hooks installed transactionally (6 of 6)", true);
    } else {
        const bool removed = RollBackHooks();
        if (removed) ReleaseSeamTls();
        s_created.store(false, std::memory_order_release);
        LogOut("[HUD_DISABLE] Hook install rolled back; HUD toggle inactive.", true);
    }
}

void Remove() {
    const bool removed = RollBackHooks();
    if (removed) ReleaseSeamTls();
    s_created.store(false, std::memory_order_release);
}

void SetAdmitted(bool admitted) {
    if (!s_created.load(std::memory_order_acquire)) return;
    if (admitted) {
        (void)MinHookUtils::EnableOwnedTargets("[HUD_DISABLE]");
    } else {
        (void)MinHookUtils::CloseAdmission("[HUD_DISABLE]");
    }
}

void SetHidden(bool hidden) {
    const bool prev = s_hidden.exchange(hidden, std::memory_order_relaxed);
    if (prev != hidden) LogOut(std::string("[HUD_DISABLE] HUD ") + (hidden ? "hidden" : "shown"), true);
}
bool IsHidden() { return s_hidden.load(std::memory_order_relaxed); }

void SetElementDisabled(unsigned bit, bool disabled) {
    if (disabled) s_elementMask.fetch_or(bit, std::memory_order_relaxed);
    else s_elementMask.fetch_and(~static_cast<uint32_t>(bit),
                                 std::memory_order_relaxed);
}
bool IsElementDisabled(unsigned bit) {
    return (s_elementMask.load(std::memory_order_relaxed) & bit) != 0;
}

void SetElementMask(unsigned mask) {
    s_elementMask.store(mask, std::memory_order_relaxed);
}

void ResetVisible() {
    const bool wasHidden = s_hidden.exchange(false, std::memory_order_relaxed);
    const uint32_t hadMask = s_elementMask.exchange(0, std::memory_order_relaxed);
    if (wasHidden || hadMask) LogOut("[HUD_DISABLE] restored on match exit", true);
}

} // namespace HudDisable
