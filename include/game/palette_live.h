#pragma once

#include "game/palette_assets.h"

namespace PaletteEditor::Live {

enum class Status { Idle, Pending, Applied, RestorePending, Restored, Committed, Refused };

namespace Detail {
bool PreviewFlushDue(uint32_t now, uint32_t previous, bool hasPrevious, bool force);
struct NativeRange { uint32_t first; uint32_t count; };
NativeRange PaletteRange(uint8_t side, bool complete);
}

struct Owner {
    uintptr_t moduleBase = 0;
    uintptr_t gameSystem = 0;
    uintptr_t playerObject = 0;
    uint32_t generation = 0;
    uint8_t side = 0;
    uint8_t characterId = 0;
    uint8_t paletteSlot = 0;
    std::array<char, 16> resource{};
    bool operator==(const Owner& other) const;
};

using NativeRow = std::array<uint8_t, 160>;

struct Access {
    void* context = nullptr;
    bool (*read)(void*, uintptr_t, NativeRow&) = nullptr;
    bool (*write)(void*, uintptr_t, const NativeRow&) = nullptr;
    bool (*refresh)(void*, const Owner&) = nullptr;
};

class Session {
public:
    bool Request(const Owner& owner, const Palette& palette);
    bool Pump(const Owner& current, bool offlineAllowed, const Access& access);
    void RequestRestore();
    bool Restore(const Owner& current, const Access& access);
    bool Commit(const Owner& current, const Access& access);
    void Invalidate();
    void Refuse();
    Status GetStatus() const { return status_; }
    const Owner& GetOwner() const { return owner_; }
    bool HasWork() const { return pending_ || restoreRequested_; }
    bool HasOwnership() const { return captured_; }
    bool HasRestoreRequest() const { return restoreRequested_; }

private:
    bool ReadOwned(const Owner& current, const Access& access, NativeRow& row);
    Owner owner_{};
    Palette desired_{};
    NativeRow original_{};
    NativeRow lastWritten_{};
    bool pending_ = false;
    bool captured_ = false;
    bool restoreRequested_ = false;
    bool refreshPending_ = false;
    bool stale_ = false;
    Status status_ = Status::Idle;
};

bool Apply(int player, const std::string& expectedResource, const Palette& palette, std::string& error);
// Explicit editor entry clears stale requests while retaining an owned restore baseline.
void BeginPreview();
// Pump belongs only to an admitted game callback, never the fallback UI thread.
void Pump(bool force = false);
// Used backends retry presentation recovery on Pump; unused backends need no refresh.
bool RefreshCurrent();
// Off-owner calls request restoration; the next owner-thread Pump performs it.
bool Restore();
void Invalidate();
bool Commit(int player, const std::string& expectedResource, std::string& error);
Status GetStatus();
bool HasPendingWork();
const char* GetStatusText();

}
