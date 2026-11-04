#include "game/palette_live.h"
#include "gui/custom_menu/palette_editor.h"
#include "gui/custom_menu/palette_editor_integration.h"
#include "gui/imgui_gui.h"
#include "game/character_hotswap.h"
#include "game/character_settings.h"
#include "core/constants.h"
#include <windows.h>
#include <cstring>

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
namespace Fixture {
bool enabled = false;
bool panelOpen = false;
bool online = false;
uint32_t generation = 1;
std::array<uint8_t, 6000> memory{};
PaletteEditor::Palette draft{};
CustomMenu::PaletteEditorPanel::ApplyColors apply = nullptr;
constexpr uintptr_t player = 0x10000;
constexpr uintptr_t paletteObject = 0x20000;
constexpr uintptr_t paletteVtable = 0x30000;
int __fastcall GenerateMappings(void*, void*, const uint8_t*) { return 1; }
HRESULT __stdcall SetEntries(void*, DWORD, DWORD, DWORD, const void*) { return S_OK; }
uintptr_t Module() { return reinterpret_cast<uintptr_t>(&GenerateMappings) - 0xB8A0; }
uintptr_t Game() { return reinterpret_cast<uintptr_t>(memory.data()); }
template<class T>
bool Read(void* output, size_t size, const T& value) {
    if (size != sizeof(value)) return false;
    std::memcpy(output, &value, size);
    return true;
}
}
uintptr_t GetEFZBase() { return Fixture::enabled ? Fixture::Module() : 0; }
uint32_t GetRuntimeLifecycleGeneration() { return Fixture::generation; }
bool SafeReadMemory(uintptr_t address, void* output, size_t size) {
    ++g_nativeReadCalls;
    if (!Fixture::enabled) return false;
    using namespace Fixture;
    const uintptr_t module = Module();
    if (address == module + EFZ_BASE_OFFSET_SCREEN_STATE) return Read(output, size, uint8_t{3});
    if (address == module + EFZ_BASE_OFFSET_GAME_STATE || address == player + 124) return Read(output, size, Game());
    if (address == module + EFZ_BASE_OFFSET_P1) return Read(output, size, player);
    if (address == player + 141) return Read(output, size, uint8_t{1});
    if (address == player + 142) return Read(output, size, uint8_t{0});
    if (address == player + 148) {
        const std::array<char, 16> resource{'a','y','u'};
        return Read(output, size, resource);
    }
    if (address == module + 0xB8A0) {
        const uint8_t bytes[] = {0x55,0x8B,0xEC,0x83,0xEC,0x78,0x56,0x89,0x4D,0x88,0x8B,0x45,0x88,0x05,0x06,0x06};
        return Read(output, size, bytes);
    }
    if (address == module + 0xBD70) {
        const uint8_t bytes[] = {0x55,0x8B,0xEC,0x51,0x89,0x4D,0xFC,0x0F,0xB6,0x45,0x0C,0x8B,0x4D,0x08,0x8D,0x14};
        return Read(output, size, bytes);
    }
    if (address == module + 0x390150 + 0x20814) return Read(output, size, paletteObject);
    if (address == paletteObject) return Read(output, size, paletteVtable);
    if (address == paletteVtable + 24) return Read(output, size, reinterpret_cast<uintptr_t>(&SetEntries));
    if (address == Game() + 4273 && size == 160) {
        std::memcpy(output, memory.data() + 4273, size);
        return true;
    }
    return false;
}
bool SafeWriteMemory(uintptr_t address, const void* bytes, size_t size) {
    ++g_nativeWriteCalls;
    if (!Fixture::enabled || address != Fixture::Game() + 4273 || size != 160) return false;
    std::memcpy(Fixture::memory.data() + 4273, bytes, size);
    return true;
}
bool IsPracticeWorldLive() { return g_practiceAllowed && !Fixture::online; }
bool IsNetplaySuspendActive() { return Fixture::online; }
bool IsNetplaySessionActive() { return Fixture::online; }
uintptr_t GetGameStatePtr() { return Fixture::Game(); }
uintptr_t GetPlayerBase(int) { return Fixture::player; }
void LogOut(const std::string&, bool) {}
namespace ImGuiGui {
ImGuiGuiState guiState{};
void RefreshLocalData() {}
}
namespace CharacterSettings {
std::string GetCharacterName(int) { return "Ayu"; }
std::string GetCharacterInternalName(int) { return "ayu"; }
}
namespace CharacterHotswap {
int GetSelectIdForResourceName(const char*) { return 0; }
bool ReadCurrentPaletteSelection(PaletteSelection&) { return false; }
void InvalidateCustomPaletteCache() {}
}
namespace Mission::MoveData { bool EnsureLoaded(int) { return true; } }
namespace Config { std::string GetConfigFilePath() { return {}; } }
namespace PaletteEditor {
bool ReadNativePalette(const std::filesystem::path&, Palette&, std::string&) { return false; }
}
namespace CustomMenu::PaletteEditorPanel {
void Configure(ContextProvider, UseSlot, ApplyColors apply, RestorePreview) { Fixture::apply = apply; }
void Open(int) { Fixture::panelOpen = true; }
bool IsOpen() { return Fixture::panelOpen; }
void RequestSuspend() noexcept { Fixture::panelOpen = false; }
bool GetLivePreview(int& player, std::string& resource, PaletteEditor::Palette& colors) {
    if (!IsOpen()) return false;
    player = 1;
    resource = "ayu";
    colors = Fixture::draft;
    return true;
}
void SetLivePreviewStatus(const std::string&) {}
}

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


void CheckPreviewIntegration() {
    using namespace PaletteEditor::Live;
    namespace Panel = CustomMenu::PaletteEditorPanel;
    const auto reset = [] {
        Fixture::enabled = true;
        Fixture::online = false;
        g_featuresEnabled = true;
        g_practiceAllowed = true;
        ++Fixture::generation;
        Fixture::memory.fill(0xA5);
        Fixture::draft.fill({1, 2, 3});
        Invalidate();
        CustomMenu::OpenPaletteEditor(1);
    };
    const auto preview = [] {
        CustomMenu::SyncPalettePreview();
        Pump(true);
        Check(GetStatus() == Status::Applied, "integration fixture applies through the native runtime adapter");
    };

    reset();
    const auto original = Fixture::memory;
    preview();
    Check(Fixture::memory != original, "preview changes the fighter row before close");
    Panel::RequestSuspend();
    CustomMenu::UpdatePalettePreview();
    Check(Fixture::memory == original,
          "hidden fallback close restores the preview without another render or SyncPalettePreview");

    reset();
    preview();
    Panel::RequestSuspend();
    CustomMenu::SyncPalettePreview();
    CustomMenu::UpdatePalettePreview();
    Check(Fixture::memory == original, "normal close restores the preview with hidden game rendering");

    reset();
    std::string error;
    Check(Fixture::apply && Fixture::apply(1, "ayu", Fixture::draft, error), "explicit Apply is queued");
    Panel::RequestSuspend();
    CustomMenu::UpdatePalettePreview();
    Check(GetStatus() == Status::Committed && Fixture::memory != original,
          "closing before the next update still commits an explicit Apply");
    const auto committed = Fixture::memory;
    CustomMenu::UpdatePalettePreview();
    CustomMenu::UpdatePalettePreview();
    CustomMenu::SyncPalettePreview();
    CustomMenu::UpdatePalettePreview();
    Check(Fixture::memory == committed, "closed updates preserve Apply after its pending request is cleared");

    CustomMenu::OpenPaletteEditor(1);
    Fixture::draft.fill({9, 8, 7});
    preview();
    Check(Fixture::memory != committed, "editing after Apply produces a temporary preview");
    Panel::RequestSuspend();
    CustomMenu::UpdatePalettePreview();
    Check(Fixture::memory == committed, "ForceHide suspend restores later edits to the committed palette");

    reset();
    preview();
    Fixture::online = true;
    g_practiceAllowed = false;
    Panel::RequestSuspend();
    const int reads = g_nativeReadCalls, writes = g_nativeWriteCalls;
    CustomMenu::UpdatePalettePreview();
    CustomMenu::SyncPalettePreview();
    Check(g_nativeReadCalls == reads && g_nativeWriteCalls == writes,
          "netplay suspend cannot restore into the new owner's palette");
}
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

    colors[0] = {90, 80, 70};
    Check(session.Request(engine.owner, colors) && session.Pump(engine.owner, true, access),
          "edits after Apply capture the committed palette as their baseline");
    Check(session.Restore(engine.owner, access) && engine.memory == committed,
          "Restore after a further edit returns to the last applied palette");
    const int writesBeforeCancel = engine.writes;
    Check(session.Request(engine.owner, colors), "an edit can wait for the next admitted callback");
    session.RequestRestore();
    session.Pump(engine.owner, true, access);
    Check(session.GetStatus() == Status::Restored && !session.HasWork()
              && engine.writes == writesBeforeCancel && engine.memory == committed,
          "Restore cancels a pending edit before it writes or commits");

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
    CheckPreviewIntegration();
    std::cout << "palette_live_tests passed\n";
}
