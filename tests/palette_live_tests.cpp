#include "game/palette_live.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <mutex>

std::atomic<bool> g_featuresEnabled{false};
std::recursive_mutex g_p2ControlMutex;
int g_nativeReadCalls = 0;
int g_nativeWriteCalls = 0;
bool g_practiceAllowed = false;
bool PracticeBattleHooksOpen() { return g_practiceAllowed; }
bool IsPracticeContext() { return g_practiceAllowed; }
uintptr_t GetEFZBase() { return 0; }
uint32_t GetRuntimeLifecycleGeneration() { return 1; }
bool SafeReadMemory(uintptr_t, void*, size_t) { ++g_nativeReadCalls; return false; }
bool SafeWriteMemory(uintptr_t, const void*, size_t) { ++g_nativeWriteCalls; return false; }

namespace {
using namespace PaletteEditor;
using namespace PaletteEditor::Live;

void Check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}

struct Engine {
    std::array<uint8_t, 6000> memory{};
    int reads = 0;
    int writes = 0;
    int refreshes = 0;
    bool refreshWorks = true;
    uint32_t rng = 0x12345678;
    Owner owner{0x400000, 100, 50, 7, 0, 1, 2};

    Access Operations() {
        return {this,
            [](void* opaque, uintptr_t address, NativeRow& row) {
                auto& engine = *static_cast<Engine*>(opaque);
                ++engine.reads;
                if (address + row.size() > engine.memory.size()) return false;
                std::copy_n(engine.memory.begin() + address, row.size(), row.begin());
                return true;
            },
            [](void* opaque, uintptr_t address, const NativeRow& row) {
                auto& engine = *static_cast<Engine*>(opaque);
                ++engine.writes;
                if (address + row.size() > engine.memory.size()) return false;
                std::copy(row.begin(), row.end(), engine.memory.begin() + address);
                return true;
            },
            [](void* opaque, const Owner&) {
                auto& engine = *static_cast<Engine*>(opaque);
                ++engine.refreshes;
                return engine.refreshWorks;
            }};
    }
};
}

int main() {
    using namespace PaletteEditor;
    using namespace PaletteEditor::Live;
    Check(Detail::PreviewFlushDue(0, 0, false, false), "the first live edit flushes immediately");
    Check(!Detail::PreviewFlushDue(199, 100, true, false)
              && Detail::PreviewFlushDue(200, 100, true, false),
          "continuous edits rebuild native palette maps at most every 100ms");
    Check(Detail::PreviewFlushDue(101, 100, true, true), "explicit use can flush the latest edit immediately");
    Check(Detail::PreviewFlushDue(49, 0xFFFFFFCDu, true, false)
              && !Detail::PreviewFlushDue(48, 0xFFFFFFCDu, true, false),
          "preview throttling remains correct across the Windows tick counter wrap");
    const auto fullRange = Detail::PaletteRange(0, true);
    const auto playerRange = Detail::PaletteRange(1, false);
    Check(fullRange.first == 0 && fullRange.count == 256,
          "savestate refresh uploads all256 entries without byte-count truncation");
    Check(playerRange.first == 135 && playerRange.count == 40,
          "editor refresh uploads only its selected player bank");
    Engine engine;
    engine.memory.fill(0xA5);
    const auto before = engine.memory;
    const auto access = engine.Operations();
    Session session;
    Palette colors{};
    colors.fill({1, 2, 3});
    Check(session.Request(engine.owner, colors), "live edit is admitted as a pending snapshot");
    Check(!session.Pump(engine.owner, false, access) && engine.reads == 0 && engine.writes == 0,
          "offline gate refuses before all native reads or writes");
    Check(session.Pump(engine.owner, true, access), "admitted pump applies the edited row");
    for (size_t i = 0; i < engine.memory.size(); ++i) {
        const size_t begin = 100 + 3893 + 95 * 4;
        const bool inside = i >= begin && i < begin + 160;
        const uint8_t expected = inside && (i - begin) % 4 != 3
            ? static_cast<uint8_t>(1 + (i - begin) % 4) : 0xA5;
        Check(engine.memory[i] == expected, "only 40 selected RGB entries change; flags and other memory survive");
    }
    Check(engine.refreshes == 1 && engine.rng == 0x12345678, "palette refresh makes no simulation or RNG call");
    Check(session.Request(engine.owner, colors) && session.Pump(engine.owner, true, access)
              && engine.refreshes == 1, "unchanged preview does not rebuild native blend maps");
    colors[0] = {4, 5, 6};
    Check(session.Request(engine.owner, colors) && session.Pump(engine.owner, true, access), "second edit keeps original baseline");
    engine.refreshWorks = false;
    session.RequestRestore();
    Check(!session.Restore(engine.owner, access) && session.GetStatus() == Status::RestorePending,
          "failed restore refresh keeps the original for retry");
    engine.refreshWorks = true;
    Check(session.Restore(engine.owner, access) && engine.memory == before, "restore retry returns exact pre-edit bytes");

    Check(session.Request(engine.owner, colors) && session.Pump(engine.owner, true, access), "new preview captures a new baseline");
    Owner changed = engine.owner; ++changed.generation;
    const int writes = engine.writes;
    Check(!session.Restore(changed, access) && engine.writes == writes && !session.HasOwnership(),
          "scene epoch change discards stale restore data");
    session.Invalidate();
    Check(session.Request(engine.owner, colors) && session.Pump(engine.owner, true, access), "fresh preview after invalidation works");
    engine.memory[100 + 4273] = 77;
    Check(!session.Restore(engine.owner, access) && engine.memory[100 + 4273] == 77,
          "external palette mutation cannot be overwritten by stale restore");
    Check(!session.Request(engine.owner, colors),
          "an external mutation cannot be reapplied next frame without a fresh editor session");

    session.Invalidate();
    engine.owner.side = 1;
    Check(session.Request(engine.owner, colors) && session.Pump(engine.owner, true, access), "P2 preview selects bank135");
    Check(engine.memory[100 + 4433] == 4 && engine.memory[100 + 4273] == 77,
          "P2 does not alter P1 palette");
    Check(session.Commit(engine.owner, access) && session.GetStatus() == Status::Committed,
          "explicit use releases the restore snapshot");
    const auto committed = engine.memory;
    Check(session.Restore(engine.owner, access) && engine.memory == committed, "closing after explicit use keeps the applied colors");

    for (int field = 0; field < 4; ++field) {
        session.Invalidate();
        Check(session.Request(engine.owner, colors) && session.Pump(engine.owner, true, access), "identity test captures live ownership");
        Owner replacement = engine.owner;
        if (field == 0) ++replacement.playerObject;
        if (field == 1) ++replacement.characterId;
        if (field == 2) ++replacement.paletteSlot;
        if (field == 3) ++replacement.gameSystem;
        const int readsBefore = engine.reads;
        Check(!session.Restore(replacement, access) && engine.reads == readsBefore,
              "changed fighter identity refuses before accessing the replacement palette");
    }
    std::string error;
    Check(!Apply(1, "ayu", colors, error) && GetStatus() == Status::Refused,
          "runtime adapter refuses live preview while the Practice gate is closed");
    Pump();
    Check(!Restore() && GetStatus() == Status::Refused,
          "revoked Practice admission refuses cleanup without entering native graphics");
    BeginPreview();
    Check(GetStatus() == Status::Idle, "explicit editor open clears an unowned refusal");
    Check(!Commit(1, "ayu", error) && GetStatus() == Status::Refused,
          "commit is also refused when Practice admission is revoked");
    Check(!RefreshCurrent(), "full palette refresh refuses before native access when Practice is revoked");
    Check(g_nativeReadCalls == 0 && g_nativeWriteCalls == 0,
          "runtime offline gate touches no native memory");
    g_featuresEnabled = true;
    g_practiceAllowed = true;
    Check(RefreshCurrent(), "savestates need no palette refresh before the native editor backend is used");
    Invalidate();
    Check(RefreshCurrent() && g_nativeReadCalls == 0 && g_nativeWriteCalls == 0,
          "unused backend skips native verification and access across lifecycle invalidation");
    std::cout << "palette_live_tests passed\n";
}
