#include "game/palette_live.h"

#include "core/constants.h"
#include "core/memory.h"
#include "game/auto_action.h"
#include "game/game_state.h"
#include "utils/utilities.h"

#include <algorithm>
#include <cstring>
#include <mutex>

namespace PaletteEditor::Live {
bool Detail::PreviewFlushDue(uint32_t now, uint32_t previous, bool hasPrevious, bool force) {
    return force || !hasPrevious || static_cast<uint32_t>(now - previous) >= 100;
}
Detail::NativeRange Detail::PaletteRange(uint8_t side, bool complete) {
    return complete ? NativeRange{0, 256} : NativeRange{95u + side * 40u, 40};
}

namespace {
uintptr_t RowAddress(const Owner& owner) {
    return owner.gameSystem + 4273 + static_cast<uintptr_t>(owner.side) * 160;
}
}

bool Owner::operator==(const Owner& other) const {
    return moduleBase == other.moduleBase && gameSystem == other.gameSystem
        && playerObject == other.playerObject && generation == other.generation
        && side == other.side && characterId == other.characterId && paletteSlot == other.paletteSlot
        && resource == other.resource;
}

bool Session::Request(const Owner& owner, const Palette& palette) {
    if (stale_) return false;
    if (captured_ && !(owner == owner_)) {
        Refuse();
        return false;
    }
    if (!owner.moduleBase || !owner.gameSystem || !owner.playerObject || !owner.generation
        || owner.side > 1 || owner.paletteSlot > 5) {
        status_ = Status::Refused;
        return false;
    }
    if (captured_ && !pending_ && !restoreRequested_ && desired_ == palette && status_ == Status::Applied) return true;
    owner_ = owner;
    desired_ = palette;
    pending_ = true;
    restoreRequested_ = false;
    status_ = Status::Pending;
    return true;
}

bool Session::ReadOwned(const Owner& current, const Access& access, NativeRow& row) {
    if (!(current == owner_)) {
        Refuse();
        return false;
    }
    if (!access.read || !access.read(access.context, RowAddress(owner_), row)) {
        status_ = Status::Refused;
        return false;
    }
    if (captured_ && row != lastWritten_) {
        Refuse();
        return false;
    }
    return true;
}

bool Session::Pump(const Owner& current, bool offlineAllowed, const Access& access) {
    if (!offlineAllowed) {
        status_ = Status::Refused;
        return false;
    }
    if (restoreRequested_) return Restore(current, access);
    if (!pending_) return status_ == Status::Applied;
    NativeRow row{};
    if (!ReadOwned(current, access, row)) return false;
    if (!captured_) {
        original_ = row;
        lastWritten_ = row;
        captured_ = true;
    }
    NativeRow replacement = row;
    for (size_t i = 0; i < desired_.size(); ++i) {
        replacement[4 * i] = desired_[i].r;
        replacement[4 * i + 1] = desired_[i].g;
        replacement[4 * i + 2] = desired_[i].b;
    }
    if (replacement != row) {
        if (!access.write || !access.write(access.context, RowAddress(owner_), replacement)) {
            status_ = Status::Refused;
            return false;
        }
        lastWritten_ = replacement;
        refreshPending_ = true;
    }
    if (refreshPending_ && (!access.refresh || !access.refresh(access.context, owner_))) {
        status_ = Status::Refused;
        return false;
    }
    refreshPending_ = false;
    pending_ = false;
    status_ = Status::Applied;
    return true;
}

void Session::RequestRestore() {
    pending_ = false;
    restoreRequested_ = captured_;
    status_ = captured_ ? Status::RestorePending : Status::Restored;
}

bool Session::Restore(const Owner& current, const Access& access) {
    if (!captured_) {
        pending_ = false;
        restoreRequested_ = false;
        return true;
    }
    RequestRestore();
    NativeRow row{};
    if (!ReadOwned(current, access, row)) return false;
    if (row != original_) {
        if (!access.write || !access.write(access.context, RowAddress(owner_), original_)) {
            status_ = Status::RestorePending;
            return false;
        }
        lastWritten_ = original_;
        refreshPending_ = true;
    }
    if (refreshPending_ && (!access.refresh || !access.refresh(access.context, owner_))) {
        status_ = Status::RestorePending;
        return false;
    }
    Invalidate();
    status_ = Status::Restored;
    return true;
}

bool Session::Commit(const Owner& current, const Access& access) {
    if (!captured_ || pending_ || restoreRequested_ || status_ != Status::Applied) return false;
    NativeRow row{};
    if (!ReadOwned(current, access, row)) return false;
    Invalidate();
    status_ = Status::Committed;
    return true;
}

void Session::Invalidate() {
    owner_ = {};
    pending_ = false;
    captured_ = false;
    restoreRequested_ = false;
    refreshPending_ = false;
    stale_ = false;
    status_ = Status::Idle;
}

void Session::Refuse() {
    Invalidate();
    stale_ = true;
    status_ = Status::Refused;
}

namespace {
constexpr uintptr_t kGraphicsRva = 0x390150;
constexpr uintptr_t kGenerateMappingsRva = 0xB8A0;
constexpr uintptr_t kSetPaletteRangeRva = 0xBD70;
std::mutex s_mutex;
std::array<Session, 2> s_sessions;
DWORD s_ownerThread = 0;
int s_selectedSide = 0;
Status s_status = Status::Idle;
uintptr_t s_verifiedBase = 0;
uint32_t s_lastApplyTick = 0;
bool s_haveApplyTick = false;
bool s_backendUsed = false;
bool s_presentationRefreshPending = false;

bool OfflineAllowed() {
    return g_featuresEnabled.load(std::memory_order_acquire)
        && PracticeBattleHooksOpen() && IsPracticeContext();
}

bool CaptureOwner(int side, Owner& owner) {
    owner = {};
    owner.moduleBase = GetEFZBase();
    owner.side = static_cast<uint8_t>(side);
    owner.generation = GetRuntimeLifecycleGeneration();
    uint8_t screen = 0;
    uintptr_t playerSystem = 0;
    return owner.moduleBase && side >= 0 && side < 2 && owner.generation
        && SafeReadMemory(owner.moduleBase + EFZ_BASE_OFFSET_SCREEN_STATE, &screen, sizeof(screen))
        && screen == 3
        && SafeReadMemory(owner.moduleBase + EFZ_BASE_OFFSET_GAME_STATE, &owner.gameSystem, sizeof(owner.gameSystem))
        && owner.gameSystem
        && SafeReadMemory(owner.moduleBase + (side == 0 ? EFZ_BASE_OFFSET_P1 : EFZ_BASE_OFFSET_P2),
                          &owner.playerObject, sizeof(owner.playerObject))
        && owner.playerObject
        && SafeReadMemory(owner.playerObject + 124, &playerSystem, sizeof(playerSystem))
        && playerSystem == owner.gameSystem
        && SafeReadMemory(owner.playerObject + 141, &owner.characterId, sizeof(owner.characterId))
        && SafeReadMemory(owner.playerObject + 142, &owner.paletteSlot, sizeof(owner.paletteSlot))
        && owner.paletteSlot < 6
        && SafeReadMemory(owner.playerObject + 148, owner.resource.data(), owner.resource.size())
        && std::find(owner.resource.begin(), owner.resource.end(), '\0') != owner.resource.end();
}

bool VerifyNative(uintptr_t base) {
    if (s_verifiedBase == base) return true;
    constexpr uint8_t mappings[] = {0x55,0x8B,0xEC,0x83,0xEC,0x78,0x56,0x89,0x4D,0x88,0x8B,0x45,0x88,0x05,0x06,0x06};
    constexpr uint8_t paletteRange[] = {0x55,0x8B,0xEC,0x51,0x89,0x4D,0xFC,0x0F,0xB6,0x45,0x0C,0x8B,0x4D,0x08,0x8D,0x14};
    uint8_t observed[16]{};
    if (!SafeReadMemory(base + kGenerateMappingsRva, observed, sizeof(observed))
        || std::memcmp(observed, mappings, sizeof(observed)) != 0
        || !SafeReadMemory(base + kSetPaletteRangeRva, observed, sizeof(observed))
        || std::memcmp(observed, paletteRange, sizeof(observed)) != 0) return false;
    s_verifiedBase = base;
    return true;
}

bool ResolvePaletteTarget(uintptr_t moduleBase, uintptr_t& paletteObject, uintptr_t& setEntries) {
    uintptr_t vtable = 0;
    return moduleBase
        && SafeReadMemory(moduleBase + kGraphicsRva + 0x20814, &paletteObject, sizeof(paletteObject))
        && paletteObject
        && SafeReadMemory(paletteObject, &vtable, sizeof(vtable)) && vtable
        && SafeReadMemory(vtable + 24, &setEntries, sizeof(setEntries)) && setEntries;
}

bool RefreshNativePalette(const Owner& owner, bool complete) {
    using Generate = int(__thiscall*)(void*, const uint8_t*);
    using SetEntries = HRESULT(__stdcall*)(void*, DWORD, DWORD, DWORD, const void*);
    uintptr_t paletteObject = 0;
    uintptr_t setEntries = 0;
    if (!ResolvePaletteTarget(owner.moduleBase, paletteObject, setEntries)) return false;
    __try {
        // Revival's battle palette injection uses this direct graphics object.
        void* graphics = reinterpret_cast<void*>(owner.moduleBase + kGraphicsRva);
        const uintptr_t palette = owner.gameSystem + 3893;
        const auto range = Detail::PaletteRange(owner.side, complete);
        // The native wrapper discards SetEntries' HRESULT.
        if (FAILED(reinterpret_cast<SetEntries>(setEntries)(reinterpret_cast<void*>(paletteObject),
            0, range.first, range.count, reinterpret_cast<const void*>(palette + 4 * range.first)))) return false;
        // Lost surfaces must not trigger repeated full blend-map rebuilds.
        reinterpret_cast<Generate>(owner.moduleBase + kGenerateMappingsRva)(graphics,
            reinterpret_cast<const uint8_t*>(palette));
        s_backendUsed = true;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool RefreshNative(void*, const Owner& owner) { return RefreshNativePalette(owner, false); }

const Access kNativeAccess{nullptr,
    [](void*, uintptr_t address, NativeRow& row) { return SafeReadMemory(address, row.data(), row.size()); },
    [](void*, uintptr_t address, const NativeRow& row) {
        uintptr_t paletteObject = 0;
        uintptr_t setEntries = 0;
        if (!ResolvePaletteTarget(GetEFZBase(), paletteObject, setEntries)
            || !SafeWriteMemory(address, row.data(), row.size())) return false;
        s_backendUsed = true;
        return true;
    },
    RefreshNative};

bool ExpectedResource(const Owner& owner, const std::string& expected) {
    return !expected.empty() && _stricmp(owner.resource.data(), expected.c_str()) == 0;
}

bool RestoreOwnedSessions() {
    bool restored = true;
    for (int side = 0; side < 2; ++side) {
        Session& session = s_sessions[side];
        if (!session.HasOwnership() && !session.HasWork()) continue;
        Owner current;
        if (!CaptureOwner(side, current)) {
            session.Refuse();
            restored = false;
            continue;
        }
        restored = session.Restore(current, kNativeAccess) && restored;
    }
    return restored;
}

bool RefreshCurrentOwned() {
    Owner owner;
    if ((!CaptureOwner(0, owner) && !CaptureOwner(1, owner)) || !VerifyNative(owner.moduleBase)
        || !RefreshNativePalette(owner, true)) return false;
    s_presentationRefreshPending = false;
    return true;
}
}

bool Apply(int player, const std::string& expectedResource, const Palette& palette, std::string& error) {
    std::lock_guard<std::recursive_mutex> control(g_p2ControlMutex);
    std::lock_guard<std::mutex> lock(s_mutex);
    if (player < 1 || player > 2 || !OfflineAllowed()) {
        s_status = Status::Refused;
        error = "Live palette preview requires offline Practice.";
        return false;
    }
    Owner owner;
    if (!CaptureOwner(player - 1, owner) || !ExpectedResource(owner, expectedResource) || !VerifyNative(owner.moduleBase)) {
        s_status = Status::Refused;
        error = "The fighter changed or the native palette functions could not be verified.";
        return false;
    }
    const int side = player - 1;
    if (!s_sessions[side].Request(owner, palette)) {
        s_status = Status::Refused;
        error = "The live palette belongs to an earlier fighter or scene.";
        return false;
    }
    s_selectedSide = side;
    s_sessions[1 - s_selectedSide].RequestRestore();
    s_status = s_sessions[s_selectedSide].GetStatus();
    error.clear();
    return true;
}

void BeginPreview() {
    std::lock_guard<std::recursive_mutex> control(g_p2ControlMutex);
    std::lock_guard<std::mutex> lock(s_mutex);
    for (Session& session : s_sessions) {
        if (!session.HasOwnership()) session.Invalidate();
    }
    s_status = s_sessions[s_selectedSide].GetStatus();
}

void Pump(bool force) {
    std::lock_guard<std::recursive_mutex> control(g_p2ControlMutex);
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!OfflineAllowed()) {
        s_presentationRefreshPending = false;
        if (s_sessions[0].HasWork() || s_sessions[1].HasWork()) s_status = Status::Refused;
        return;
    }
    const DWORD thread = GetCurrentThreadId();
    if (!s_ownerThread) s_ownerThread = thread;
    if (thread != s_ownerThread) return;
    const uint32_t now = GetTickCount();
    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 1 && !Detail::PreviewFlushDue(now, s_lastApplyTick, s_haveApplyTick, force)) break;
        if (pass == 1 && s_presentationRefreshPending) {
            s_lastApplyTick = now;
            s_haveApplyTick = true;
            const bool refreshed = RefreshCurrentOwned();
            if (!refreshed || !force) return;
        }
        for (int side = 0; side < 2; ++side) {
            Session& session = s_sessions[side];
            if (!session.HasWork()) continue;
            const bool restoring = session.HasRestoreRequest();
            if (restoring != (pass == 0)) continue;
            if (!restoring) {
                s_lastApplyTick = now;
                s_haveApplyTick = true;
            }
            Owner current;
            if (!CaptureOwner(side, current)) {
                session.Refuse();
                s_status = Status::Refused;
                continue;
            }
            session.Pump(current, true, kNativeAccess);
        }
        if (pass == 0 && s_sessions[1 - s_selectedSide].HasOwnership()) {
            s_status = Status::RestorePending;
            return;
        }
    }
    s_status = s_sessions[s_selectedSide].GetStatus();
}

bool RefreshCurrent() {
    std::lock_guard<std::recursive_mutex> control(g_p2ControlMutex);
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!OfflineAllowed()) return false;
    if (!s_backendUsed) return true;
    s_presentationRefreshPending = true;
    if (!s_ownerThread || GetCurrentThreadId() != s_ownerThread) return false;
    s_lastApplyTick = GetTickCount();
    s_haveApplyTick = true;
    return RefreshCurrentOwned();
}

bool Restore() {
    std::lock_guard<std::recursive_mutex> control(g_p2ControlMutex);
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!OfflineAllowed()) {
        for (Session& session : s_sessions) session.Refuse();
        s_status = Status::Refused;
        return false;
    }
    bool work = false;
    for (Session& session : s_sessions) {
        work = session.HasOwnership() || session.HasWork() || work;
        session.RequestRestore();
    }
    if (!work) { s_status = Status::Restored; return true; }
    if (!s_ownerThread || GetCurrentThreadId() != s_ownerThread) {
        s_status = Status::RestorePending;
        return false;
    }
    const bool restored = RestoreOwnedSessions();
    s_status = restored ? Status::Restored
        : (s_sessions[0].HasOwnership() || s_sessions[1].HasOwnership())
            ? Status::RestorePending : Status::Refused;
    return restored;
}

void Invalidate() {
    std::lock_guard<std::recursive_mutex> control(g_p2ControlMutex);
    std::lock_guard<std::mutex> lock(s_mutex);
    for (Session& session : s_sessions) session.Invalidate();
    s_presentationRefreshPending = false;
    s_haveApplyTick = false;
    s_status = Status::Idle;
}

bool Commit(int player, const std::string& expectedResource, std::string& error) {
    std::lock_guard<std::recursive_mutex> control(g_p2ControlMutex);
    std::lock_guard<std::mutex> lock(s_mutex);
    if (player < 1 || player > 2 || !OfflineAllowed()) {
        s_status = Status::Refused;
        error = "Using a live palette requires offline Practice.";
        return false;
    }
    Owner current;
    if (!CaptureOwner(player - 1, current) || !ExpectedResource(current, expectedResource)
        || !s_sessions[player - 1].Commit(current, kNativeAccess)) {
        error = "Wait for the live preview to apply before using this palette.";
        return false;
    }
    s_selectedSide = player - 1;
    s_status = Status::Committed;
    error.clear();
    return true;
}

Status GetStatus() {
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_status;
}

bool HasPendingWork() {
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_sessions[0].HasWork() || s_sessions[1].HasWork();
}

const char* GetStatusText() {
    switch (GetStatus()) {
        case Status::Idle: return "Live preview idle";
        case Status::Pending: return "Live preview pending";
        case Status::Applied: return "Live preview applied";
        case Status::RestorePending: return "Restoring original colors";
        case Status::Restored: return "Original colors restored";
        case Status::Committed: return "Palette in use";
        case Status::Refused: return "Live preview unavailable or fighter changed";
    }
    return "Live preview unavailable";
}
}
