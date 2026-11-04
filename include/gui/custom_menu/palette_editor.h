#pragma once

#include "game/palette_assets.h"
#include <filesystem>
#include <string>

struct IDirect3DDevice9;

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

// Except IsOpen/RequestSuspend, calls belong to the render owner thread.
void Configure(ContextProvider contextProvider, UseSlot useSlot);
void Open(int player = 1);
bool IsOpen();
void Render();
void Close();
void Suspend();
void RequestSuspend() noexcept;
void Reset();
void SetDevice(IDirect3DDevice9* device);
void ReleaseTextures();
bool GetLivePreview(int& player, std::string& resource, ::PaletteEditor::Palette& colors);
void SetLivePreviewStatus(const std::string& status);
}
