#include "gui/custom_menu/palette_editor_integration.h"
#include "gui/custom_menu/palette_editor.h"
#include "gui/imgui_gui.h"
#include "game/character_settings.h"
#include "game/character_hotswap.h"
#include "game/game_state.h"
#include "game/palette_editor.h"
#include "game/palette_live.h"
#include "game/mission/mission_movedata.h"
#include "game/auto_action.h"
#include "core/logger.h"
#include "core/memory.h"
#include "utils/config.h"
#include "utils/network.h"
#include "utils/utilities.h"
#include "core/constants.h"
#include <windows.h>
#include <mutex>

namespace CustomMenu {
namespace {
uint32_t s_useGeneration = 0;
int s_usePlayer = 0;
bool s_forceUse = false;
std::string s_useResource;

bool ReadContext(int player, PaletteEditorPanel::Context& context, std::string& error) {
    if (player < 1 || player > 2 || !IsPracticeWorldLive() ||
        IsNetplaySuspendActive() || IsNetplaySessionActive()) {
        error = "Open the palette editor in an offline Practice match.";
        return false;
    }
    ImGuiGui::RefreshLocalData();
    const auto& data = ImGuiGui::guiState.localData;
    const int character = player == 1 ? data.p1CharID : data.p2CharID;
    const std::string resource = CharacterSettings::GetCharacterInternalName(character);
    const int selection = CharacterHotswap::GetSelectIdForResourceName(resource.c_str());
    if (selection < 0) { error = "The current character is unavailable."; return false; }
    Mission::MoveData::EnsureLoaded(character);
    wchar_t executable[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
    if (!length || length == MAX_PATH) { error = "The game directory is unavailable."; return false; }
    CharacterHotswap::PaletteSelection palettes{};
    if (!CharacterHotswap::ReadCurrentPaletteSelection(palettes)) {
        error = "The current palette selection is unavailable.";
        return false;
    }
    uint8_t stage = 0;
    const uintptr_t game = GetGameStatePtr();
    if (!game || !SafeReadMemory(game + 3890, &stage, sizeof(stage))) {
        error = "The current stage palette is unavailable.";
        return false;
    }
    PaletteEditorPanel::Context candidate;
    candidate.gameDirectory = std::filesystem::path(executable).parent_path();
    candidate.libraryDirectory = std::filesystem::path(Config::GetConfigFilePath()).parent_path() / "palettes";
    candidate.resourceName = resource;
    candidate.characterId = character;
    candidate.displayName = CharacterSettings::GetCharacterName(character);
    candidate.stockSlot = player == 1 ? palettes.p1Color : palettes.p2Color;
    if (candidate.stockSlot < 0 || candidate.stockSlot >= 6) {
        error = "The current palette slot is invalid.";
        return false;
    }
    candidate.hasCustom = player == 1 ? palettes.p1UseCustomPalette : palettes.p2UseCustomPalette;
    candidate.stageId = stage;
    short move = -1;
    const uintptr_t fighter = GetPlayerBase(player);
    if (fighter && SafeReadMemory(fighter + MOVE_ID_OFFSET, &move, sizeof(move))) candidate.moveId = move;
    context = std::move(candidate);
    error.clear();
    return true;
}

bool ApplyCurrentPalette(int player, const std::string& resource, const PaletteEditor::Palette& colors,
                         std::string& error) {
    std::lock_guard<std::recursive_mutex> control(g_p2ControlMutex);
    if (!PaletteEditor::Live::Apply(player, resource, colors, error)) return false;
    s_usePlayer = player;
    s_forceUse = true;
    s_useResource = resource;
    s_useGeneration = GetRuntimeLifecycleGeneration();
    return true;
}

void RestorePalettePreview(bool cancelPendingApply) {
    std::lock_guard<std::recursive_mutex> control(g_p2ControlMutex);
    if (s_usePlayer && !cancelPendingApply) return;
    s_usePlayer = 0;
    s_forceUse = false;
    PaletteEditor::Live::Restore();
}

bool UseExportedPalette(int player, const std::string& resource, int slot, std::string& error) {
    std::lock_guard<std::recursive_mutex> control(g_p2ControlMutex);
    PaletteEditorPanel::Context context;
    if (slot < 0 || slot >= 6 || !ReadContext(player, context, error)) return false;
    if (context.resourceName != resource) { error = "The current fighter changed."; return false; }
    const auto filename = resource + std::to_string(slot + 1) + ".pal";
    auto path = context.gameDirectory / resource / filename;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) path = context.gameDirectory / "system" / resource / filename;
    PaletteEditor::Palette colors;
    if (!PaletteEditor::ReadNativePalette(path, colors, error)) return false;
    CharacterHotswap::InvalidateCustomPaletteCache();
    return ApplyCurrentPalette(player, resource, colors, error);
}
}

void OpenPaletteEditor(int player) {
    std::lock_guard<std::recursive_mutex> control(g_p2ControlMutex);
    if (s_usePlayer && s_useGeneration != GetRuntimeLifecycleGeneration()) s_usePlayer = 0;
    if (!s_usePlayer) PaletteEditor::Live::BeginPreview();
    PaletteEditorPanel::Configure(ReadContext, UseExportedPalette, ApplyCurrentPalette, RestorePalettePreview);
    PaletteEditorPanel::Open(player);
}

void SyncPalettePreview() {
    std::lock_guard<std::recursive_mutex> control(g_p2ControlMutex);
    if (!IsPracticeWorldLive() || IsNetplaySuspendActive() || IsNetplaySessionActive())
        PaletteEditorPanel::RequestSuspend();
    if (s_usePlayer && s_useGeneration != GetRuntimeLifecycleGeneration()) s_usePlayer = 0;
    if (!s_usePlayer) {
        int player = 0;
        std::string resource, error;
        PaletteEditor::Palette colors;
        if (PaletteEditorPanel::GetLivePreview(player, resource, colors)) {
            if (!PaletteEditor::Live::Apply(player, resource, colors, error)) {
                PaletteEditorPanel::SetLivePreviewStatus(error);
                return;
            }
        } else {
            PaletteEditor::Live::Restore();
        }
    }
    PaletteEditorPanel::SetLivePreviewStatus(PaletteEditor::Live::GetStatusText());
}

void UpdatePalettePreview() {
    std::lock_guard<std::recursive_mutex> control(g_p2ControlMutex);
    if (s_usePlayer && s_useGeneration != GetRuntimeLifecycleGeneration()) s_usePlayer = 0;
    if (!s_usePlayer && !PaletteEditorPanel::IsOpen()) PaletteEditor::Live::Restore();
    PaletteEditor::Live::Pump(s_usePlayer && s_forceUse);
    s_forceUse = false;
    if (!s_usePlayer) return;
    const auto status = PaletteEditor::Live::GetStatus();
    if (status == PaletteEditor::Live::Status::Applied) {
        std::string error;
        if (!PaletteEditor::Live::Commit(s_usePlayer, s_useResource, error))
            LogOut("[PALETTE] " + error, true);
        s_usePlayer = 0;
    } else if (!PaletteEditor::Live::HasPendingWork()) {
        s_usePlayer = 0;
    }
}
}
