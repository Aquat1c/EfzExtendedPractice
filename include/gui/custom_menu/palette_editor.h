#pragma once

#include "game/palette_assets.h"
#include <filesystem>
#include <string>

struct IDirect3DDevice9;
struct ImDrawList;

namespace CustomMenu::PaletteEditorPanel {
struct Context {
    std::filesystem::path gameDirectory, libraryDirectory;
    std::string resourceName, displayName;
    int characterId = -1;
    int stockSlot = 0;
    int stageId = 0;
    int moveId = -1;
    bool hasCustom = false;
};
using ContextProvider = bool (*)(int player, Context& context, std::string& error);
using UseSlot = bool (*)(int player, const std::string& expectedResource, int slot, std::string& error);
using ApplyColors = bool (*)(int player, const std::string& expectedResource, const ::PaletteEditor::Palette& colors, std::string& error);
using RestorePreview = void (*)(bool cancelPendingApply);

// Except IsOpen/RequestSuspend, calls belong to the render owner thread.
void Configure(ContextProvider contextProvider, UseSlot useSlot, ApplyColors applyColors = nullptr, RestorePreview restorePreview = nullptr);
void Open(int player = 0);
bool IsOpen();
void Render(ImDrawList* drawList = nullptr, float contentTop = 66, bool inputEnabled = true);
bool BlocksNavigation();
bool ConsumeFocusAboveRequest();
bool ConsumeCloseRequest();
void Close();
void Suspend();
void RequestSuspend() noexcept;
void Reset();
void SetDevice(IDirect3DDevice9* device);
void ReleaseTextures();
bool GetLivePreview(int& player, std::string& resource, ::PaletteEditor::Palette& colors);
void SetLivePreviewStatus(const std::string& status);
}
