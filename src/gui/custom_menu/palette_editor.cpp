#include "gui/custom_menu/palette_editor.h"
#include "game/palette_editor.h"
#include "core/constants.h"
#include "utils/i18n.h"
#include "game/mission/move_notation_tables.h"
#include "game/mission/entity_notation_tables.h"
#include "game/mission/mission_movedata.h"
#include "gui/custom_menu/input.h"
#include "gui/custom_menu/layout.h"
#include "gui/custom_menu/scale.h"
#include "gui/custom_menu/screens.h"
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <d3d9.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <functional>
#include <memory>
#include <random>

namespace CustomMenu::PaletteEditorPanel {
namespace {
namespace Model = ::PaletteEditor;
ContextProvider g_contextProvider = nullptr;
UseSlot g_useSlot = nullptr;
ApplyColors g_applyColors = nullptr;
RestorePreview g_restorePreview = nullptr;
std::atomic<bool> g_suspendRequested{false};
std::atomic<bool> g_open{false};
IDirect3DDevice9* g_device = nullptr;
struct Texture {
    IDirect3DTexture9* value = nullptr;
    int sprite = -1, width = 0, height = 0, marked = -1;
    Model::Palette256 colors{};
    void Release() { if (value) value->Release(); value = nullptr; sprite = -1; }
};
Texture g_originalTexture, g_workingTexture;
struct State {
    bool open = false, loaded = false;
    enum class ColorControls { Picker, RGB, HSV } colorControls = ColorControls::Picker;
    enum class PickerInput { Idle, DragSV, DragHue, AdjustSV, AdjustHue } pickerInput = PickerInput::Idle;
    int player = 1, popupFocus = 0, editorFocus = 22, color = 0, ramp = 0, target = 1;
    int helpControl = -1;
    bool mouseHelp = false;
    float helpMouseX = -1, helpMouseY = -1;
    I18n::Language language = I18n::English;
    int category = 1, moveChoice = 0, toolChoice = 0;
    std::optional<Model::Color> copiedColor;
    bool closeRequested = false, focusAbove = false, panMode = false, classesKnown = false, keyboardGesture = false;
    int keyboardChannel = -1;
    int components[3]{}, stock = 0;
    int animation = 0, frame = 0, zoom = 100, panX = 0, panY = 0, compare = 0;
    bool playing = true, marking = false, pin = false, dragging = false;
    bool panning = false, live = true, alternate = false, previewWheel = false, autoFit = true;
    int sliderRow = -1;
    float sliderLeft = 0, sliderWidth = 0;
    float panMouseX = 0, panMouseY = 0;
    double elapsed = 0;
    Context context;
    Model::CharacterAssets assets;
    Model::Palette256 shared{};
    Model::Document document;
    std::vector<std::string> moveLabels;
    std::vector<const char*> moveOptions;
    std::vector<int> moveCategories, moveIndices, choiceCategories;
    std::optional<Model::PaletteFile> source;
    Screens::ScrollState popupScroll;
    DWORD statusTick = 0;
    std::string status, assetWarning, liveStatus, nativeName;
    std::function<void()> confirm;
    std::string confirmTitle, confirmText, confirmVerb;
    int confirmFocus = 0, modalOpenedFrame = -1;
    enum class TextKind { None, Hex } textKind = TextKind::None;
    std::string text;
    int textFocus = 0;
    bool lowerCase = false;
};
State g;
using FileDialog = bool (*)(bool save,const std::filesystem::path& initial,std::filesystem::path& selected,std::string& error);
FileDialog g_fileDialog = nullptr;

bool HsvControls() {return g.colorControls!=State::ColorControls::RGB;}
bool Modal() { return bool(g.confirm) || g.textKind != State::TextKind::None; }
void Neutral() { g.pickerInput=State::PickerInput::Idle;g.document.EndGesture();g.keyboardGesture=false;g.sliderRow=-1;g.dragging=false; if(ImGui::GetCurrentContext()) Input::ResetEdges(); Screens::ResetMouseTracking(); }
void Status(const std::string& text) { g.status = I18n::Tr(text.c_str());g.statusTick=GetTickCount(); }
void Text(ImDrawList* dl, float x, float y, const std::string& text, ImU32 color = Theme::kTextActive) {
    Layout::DrawString(dl, Layout::BodyFont(), Scale::Get().bodyPx, x, y, color, text.c_str());
}
void FitText(ImDrawList* dl, float x, float y, float width, const std::string& text, ImU32 color = Theme::kTextActive) {
    dl->PushClipRect(ImVec2(x,y), ImVec2(x+width,y+Scale::Get().bodyPx+4), true);
    Text(dl,x,y,text,color);
    dl->PopClipRect();
}
std::filesystem::path NativePath(int slot, bool existing) {
    const auto filename = g.context.resourceName + std::to_string(slot + 1) + ".pal";
    auto path = g.context.gameDirectory / g.context.resourceName / filename;
    std::error_code error;
    if (existing && !std::filesystem::exists(path,error)) {
        auto fallback = g.context.gameDirectory / "system" / g.context.resourceName / filename;
        if (std::filesystem::exists(fallback,error)) return fallback;
    }
    return path;
}
void Confirm(const char* title, const std::string& text, const char* verb, std::function<void()> action) {
    g.document.EndGesture();
    g.dragging = false;g.sliderRow=-1;
    g.confirmTitle = title;
    g.confirmText = text;
    g.confirmVerb = verb;
    g.confirmFocus = 0;
    g.confirm = std::move(action);
    g.modalOpenedFrame=ImGui::GetCurrentContext()?ImGui::GetFrameCount():-1;
    Neutral();
}
void GuardDraft(std::function<void()> action) {
    if (g.loaded && g.document.Dirty())
        Confirm("UNSAVED COLORS", "Keep editing or discard this draft?", "DISCARD", std::move(action));
    else action();
}
void SyncColor() {
    if (g.document.Ramps().empty()) g.target = 0;
    g.color = (std::clamp)(g.color,0,39);
    g.ramp = (std::clamp)(g.ramp,0,(std::max)(0,int(g.document.Ramps().size())-1));
    if (g.target && !g.document.Ramps().empty()) g.color = int(g.document.Ramps()[g.ramp].key);
    const auto c = g.target ? g.document.RampColor(unsigned(g.ramp)) : g.document.Colors()[g.color];
    const auto hsv = g.target ? g.document.RampHsv(unsigned(g.ramp)) : g.document.ColorHsv(g.color);
    g.components[0] = HsvControls() ? int(std::lround(hsv.h)) : c.r;
    g.components[1] = HsvControls() ? int(std::lround(hsv.s)) : c.g;
    g.components[2] = HsvControls() ? int(std::lround(hsv.v)) : c.b;
    g.pin = g.document.Pinned(g.color);
}
void CycleColorControls() {
    using Controls=State::ColorControls;
    g.colorControls=g.colorControls==Controls::Picker?Controls::RGB:g.colorControls==Controls::RGB?Controls::HSV:Controls::Picker;
    Neutral();SyncColor();
}
void ApplyToMatch() {
    std::string error;
    if(!g_applyColors || !g_applyColors(g.player,g.context.resourceName,g.document.Colors(),error)) {
        Status(error.empty()?"Practice palette application is unavailable.":error);return;
    }
    g.compare=0;
    Status("Apply requested for this match. No file is written.");
}
void RestoreMatch() {
    g.live=false;
    if(g_restorePreview) g_restorePreview(true);
    Status("Preview off; draft edits kept.");
}
void SelectColor() { g.document.EndGesture();g.keyboardGesture=false;g.sliderRow=-1;g.dragging=false; g.target = 0; SyncColor(); }
void SelectRamp() { g.document.EndGesture();g.keyboardGesture=false;g.sliderRow=-1;g.dragging=false; g.target = g.document.Ramps().empty() ? 0 : 1; SyncColor(); }
void ApplyComponents() {
    if (HsvControls()) {
        const Model::Hsv hsv{float(g.components[0]),float(g.components[1]),float(g.components[2])};
        if (g.target) g.document.SetRampHsv(unsigned(g.ramp),hsv);
        else g.document.SetHsv(unsigned(g.color),hsv);
    } else {
        const Model::Color c{uint8_t(g.components[0]),uint8_t(g.components[1]),uint8_t(g.components[2])};
        if (g.target) g.document.SetRampColor(unsigned(g.ramp),c);
        else g.document.SetColor(unsigned(g.color),c);
    }
}
constexpr const char* kMoveCategories[]={"All moves","Normals","Specials","Supers","Entities / effects","System","Unclassified"};
int Category(Mission::MoveData::Cls type) {
    using Mission::MoveData::Cls;
    switch(type) {
    case Cls::A:case Cls::B:case Cls::C:case Cls::Command:return 1;
    case Cls::Special:return 2;
    case Cls::Super:return 3;
    case Cls::Projectile:case Cls::SuperEntity:case Cls::Entity:return 4;
    case Cls::System:return 5;
    default:return 6;
    }
}
void FilterMoves() {
    g.moveOptions.clear();g.moveIndices.clear();g.choiceCategories.clear();
    for(int i=0;i<int(g.assets.animations.size());++i) {
        if(g.assets.animations[i].frames.empty()) continue;
        g.moveIndices.push_back(i);g.moveOptions.push_back(g.moveLabels[i].c_str());g.choiceCategories.push_back(g.moveCategories[i]);
    }
    const auto current=std::find(g.moveIndices.begin(),g.moveIndices.end(),g.animation);
    g.moveChoice=current==g.moveIndices.end()?0:int(current-g.moveIndices.begin());
    if(!g.moveIndices.empty()) g.animation=g.moveIndices[g.moveChoice];
    g.frame=0;g.elapsed=0;
}
const char* CommonStateLabel(int id) {
    switch(id) {
    case IDLE_MOVE_ID:return "Idle";
    case WALK_FWD_ID:return "Walk forward";
    case WALK_BACK_ID:return "Walk backward";
    case CROUCH_ID:return "Crouch";
    case CROUCH_TO_STAND_ID:return "Stand up";
    case PREJUMP_ID:return "Jump startup";
    case STRAIGHT_JUMP_ID:return "Jump";
    case FORWARD_JUMP_ID:return "Jump forward";
    case BACKWARD_JUMP_ID:return "Jump backward";
    case FALLING_ID:return "Falling";
    case DOUBLE_JUMP_NEUTRAL_ID:return "Double jump";
    case DOUBLE_JUMP_FWD_ID:return "Double jump forward";
    case DOUBLE_JUMP_BACK_ID:return "Double jump backward";
    case LANDING_1_ID:case LANDING_2_ID:case LANDING_3_ID:case LANDING_ID:return "Landing";
    case STANDING_BLOCK_LVL1:case STAND_GUARD_ID:case STANDING_BLOCK_LVL3:return "Standing guard";
    case CROUCH_GUARD_ID:case CROUCH_GUARD_STUN1:case CROUCH_GUARD_STUN2:return "Crouching guard";
    case AIR_GUARD_ID:return "Air guard";
    case RG_STAND_ID:return "Standing recoil guard";
    case RG_CROUCH_ID:return "Crouching recoil guard";
    case RG_AIR_ID:return "Air recoil guard";
    case SWEEP_HITSTUN:return "Sweep hitstun";
    case FORWARD_AIRTECH:return "Air recovery forward";
    case BACKWARD_AIRTECH:return "Air recovery backward";
    case GROUNDTECH_PRE:return "Ground recovery startup";
    case GROUNDTECH_RECOVERY:return "Ground recovery finish";
    case GROUND_FORWARD_DASH_ID:return "Dash forward";
    case GROUND_BACKWARD_DASH_ID:return "Dash backward";
    case AIR_FORWARD_DASH_ID:return "Air dash forward";
    case AIR_BACKWARD_DASH_ID:return "Air dash backward";
    case FORWARD_DASH_RECOVERY_SENTINEL_ID:return "Dash recovery";
    case GROUND_IC_ID:return "Ground instant charge";
    case AIR_IC_ID:return "Air instant charge";
    case FIRE_STATE:return "Burning hitstun";
    case ELECTRIC_STATE:return "Electric hitstun";
    default:break;
    }
    if(id>=STAND_HITSTUN_START && id<=STAND_HITSTUN_END) return "Standing hitstun";
    if(id>=CROUCH_HITSTUN_START && id<=CROUCH_HITSTUN_END) return "Crouching hitstun";
    if(id>=LAUNCHED_HITSTUN_START && id<=LAUNCHED_HITSTUN_END) return "Air hitstun";
    if(id>=GROUNDTECH_START && id<=GROUNDTECH_END) return "Ground recovery";
    if(id>=FROZEN_STATE_START && id<=FROZEN_STATE_END) return "Frozen hitstun";
    if((id>=THROWN_STATE_START && id<=THROWN_STATE_END) ||
       (id>=THROWN2_STATE_START && id<=THROWN2_STATE_END) ||
       (id>=THROWN3_STATE_START && id<=THROWN3_STATE_END) ||
       (id>=THROWN4_STATE_START && id<=THROWN4_STATE_END) ||
       (id>=THROWN5_STATE_START && id<=THROWN5_STATE_END)) return "Thrown";
    return nullptr;
}
void BuildMoveChoices() {
    g.moveLabels.clear();g.moveCategories.clear();g.language=I18n::GetLanguage();
    bool known=false;
    for(const auto& animation:g.assets.animations) {
        const auto type=Mission::MoveData::GetClass(g.context.characterId,animation.id);
        g.moveCategories.push_back(Category(type));known=known || type!=Mission::MoveData::Cls::Unknown;
        const char* label=Mission::MoveNames::Lookup(g.context.resourceName.c_str(),short(animation.id));
        if(!label || !*label) label=Mission::EntityNames::Lookup(g.context.resourceName.c_str(),animation.id);
        if(!label || !*label) label=CommonStateLabel(animation.id);
        if(!label || !*label) label=Mission::MoveData::ClassName(type);
        g.moveLabels.push_back(std::to_string(animation.id)+" - "+I18n::Tr(label && *label?label:"Unclassified"));
    }
    g.category=1;
    if(g.animation>=0 && g.animation<int(g.moveCategories.size()) && g.assets.animations[g.animation].id==g.context.moveId)
        g.category=g.moveCategories[g.animation];
    g.classesKnown=known;
    if(!known) {g.category=0;Status("Move classifications unavailable; showing actual PAT IDs.");}
    FilterMoves();
    if(g.moveIndices.empty()) {g.category=0;FilterMoves();}
}
void ResetPreview() { g.animation=0; g.frame=0; g.elapsed=0; g.panX=0; g.panY=0;g.autoFit=true; }
void LoadPlayer(int player) {
    Context context;
    std::string error;
    if (!g_contextProvider || !g_contextProvider(player,context,error)) {
        Status(error.empty() ? "Character context is unavailable." : error);
        return;
    }
    Model::CharacterAssets assets;
    if (!Model::LoadCharacterAssets(context.gameDirectory,context.resourceName,assets,error)) {
        Status(error);
        return;
    }
    g_originalTexture.sprite=g_workingTexture.sprite=-1;
    g.context=std::move(context);
    g.assets=std::move(assets);
    g.player=player;
    g.alternate=false;
    g.stock=(std::clamp)(g.context.stockSlot,0,5);
    g.document.LoadStock(g.context.resourceName,unsigned(g.stock),g.assets.stockPalettes[g.stock]);
    g.source.reset();g.nativeName.clear();
    std::string nativeError;
    if(g.context.hasCustom) {
        Model::Palette colors;
        const auto path=NativePath(g.stock,true);
        if(Model::ReadNativePalette(path,colors,nativeError)) {
            g.document.Import(colors);g.document.MarkSaved();g.nativeName=path.filename().u8string();
        } else g.live=false;
    }
    g.shared={};
    g.assetWarning.clear();
    if (!Model::LoadSharedBattlePalette(g.context.gameDirectory,g.context.stageId,g.shared,error))
        g.assetWarning=I18n::TrFormat("Shared effect colors unavailable: %s",I18n::Tr(error.c_str()));
g.loaded=true;
    ResetPreview();
    for (size_t i=0;i<g.assets.animations.size();++i)
        if (g.assets.animations[i].id==g.context.moveId) g.animation=int(i);
    BuildMoveChoices();
    g.target=1;
    SyncColor();
    Status(!nativeError.empty()?I18n::TrFormat("Custom PAL could not load; live preview disabled: %s",I18n::Tr(nativeError.c_str())):
        !g.nativeName.empty()?I18n::TrFormat("Loaded %s.",g.nativeName.c_str()):I18n::TrFormat("Stock %d loaded.",g.stock+1));
    Neutral();
}
void SwitchPlayer() { const int player=3-g.player; GuardDraft([player]{LoadPlayer(player);}); }
void LoadStock() {
    const int slot=g.stock;g.stock=int(g.document.State().stockSlot);
    GuardDraft([slot]{
        g.stock=slot;
        g.document.LoadStock(g.context.resourceName,unsigned(slot),g.assets.stockPalettes[slot]);
        g.source.reset();g.nativeName.clear(); g.target=1; SyncColor();
        Status(I18n::TrFormat("Stock %d loaded.",slot+1)); Neutral();
    });
}
bool NoRamps() { return g.document.Ramps().empty(); }
bool NoUndo() { return g.document.UndoCount()==0; }
bool NoAnimations() { return g.assets.animations.empty() || g.moveIndices.empty(); }
std::wstring WideText(const char* text) {
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,-1,nullptr,0);
    if(count<=0) return {};
    std::wstring result(size_t(count),L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,-1,&result[0],count);
    result.pop_back();return result;
}
bool NativeFileDialog(bool save,const std::filesystem::path& initial,std::filesystem::path& selected,std::string& error) {
    std::array<wchar_t,32768> filename{};
    const auto initialName=initial.filename().wstring(),directory=initial.parent_path().wstring();
    if(save) wcsncpy_s(filename.data(),filename.size(),initialName.c_str(),_TRUNCATE);
    std::wstring filters;
    const auto append=[&](const char* label,const wchar_t* pattern) {
        filters+=WideText(I18n::Tr(label));filters.push_back(L'\0');filters+=pattern;filters.push_back(L'\0');
    };
    if(!save) append("Palette files (*.json;*.pal)",L"*.json;*.pal");
    append("Palette document (*.json)",L"*.json");append("Native palette (*.pal)",L"*.pal");filters.push_back(L'\0');
    const auto title=WideText(I18n::Tr(save?"Save palette as":"Open palette"));
    OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=GetForegroundWindow();
    dialog.lpstrFile=filename.data();dialog.nMaxFile=DWORD(filename.size());dialog.lpstrInitialDir=directory.c_str();
    dialog.lpstrFilter=filters.c_str();dialog.lpstrTitle=title.c_str();dialog.lpstrDefExt=L"json";dialog.nFilterIndex=1;
    dialog.Flags=OFN_EXPLORER|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    error.clear();
    const bool accepted=save?GetSaveFileNameW(&dialog)!=FALSE:GetOpenFileNameW(&dialog)!=FALSE;
    if(!accepted) {if(CommDlgExtendedError()) error="The palette file dialog could not open.";return false;}
    selected=std::filesystem::path(filename.data());return true;
}
std::filesystem::path InitialPalettePath() {
    return g.source?g.source->path:g.context.libraryDirectory/(g.context.resourceName+".json");
}
bool LoadPalettePath(const std::filesystem::path& path) {
    Model::PaletteFile file;std::string error;
    if(!Model::ReadPaletteFile(path,file,error)) {Status(error);return false;}
    if(!file.native && file.palette.character!=g.context.resourceName) {Status("Palette belongs to a different character.");return false;}
    if(file.native) {
        g.document.LoadStock(g.context.resourceName,unsigned(g.stock),g.assets.stockPalettes[g.stock]);
        g.document.Import(file.palette.colors);g.document.SetName(file.palette.name);g.document.MarkSaved();
    } else if(file.palette.stockSlot>=6 || !g.document.LoadSaved(file.palette,g.assets.stockPalettes[file.palette.stockSlot],error)) {
        Status(error.empty()?"Invalid stock slot.":error);return false;
    } else g.stock=int(file.palette.stockSlot);
    g.source=std::move(file);g.nativeName.clear();g.target=1;SyncColor();Neutral();
    Status(I18n::TrFormat("Loaded %s.",g.source->path.filename().u8string().c_str()));return true;
}
void LoadFile() {
    std::filesystem::path path;std::string error;
    const bool accepted=(g_fileDialog?g_fileDialog:NativeFileDialog)(false,InitialPalettePath(),path,error);
    Neutral();if(!accepted) {if(!error.empty()) Status(error);return;}
    GuardDraft([path]{LoadPalettePath(path);});
}
bool SavePalettePath(const std::filesystem::path& path,std::optional<uint64_t> expected) {
    Model::PaletteFile saved;std::string error;
    if(!Model::SavePaletteFile(path,g.document.State(),expected,saved,error)) {Status(error);return false;}
    g.source=std::move(saved);g.document.SetName(g.source->palette.name);g.document.MarkSaved();
    Status(I18n::TrFormat(g.source->native?"Saved colors to %s. PAL files do not store ramps or pins.":"Saved %s.",g.source->path.filename().u8string().c_str()));return true;
}
void SaveAs() {
    std::filesystem::path path;std::string error;
    const bool accepted=(g_fileDialog?g_fileDialog:NativeFileDialog)(true,InitialPalettePath(),path,error);
    Neutral();if(!accepted) {if(!error.empty()) Status(error);return;}
    std::error_code ec;std::optional<uint64_t> expected;
    if(std::filesystem::exists(path,ec)) {
        uint64_t fingerprint=0;
        if(!Model::ReadPaletteFileIdentity(path,fingerprint,error)) {Status(error);return;}
        expected=fingerprint;
    } else if(ec) {Status("Could not read palette file.");return;}
    SavePalettePath(path,expected);
}
void Save() {if(g.source) SavePalettePath(g.source->path,g.source->fingerprint);else SaveAs();}
void StartText(State::TextKind kind) {
    g.textKind=kind;g.modalOpenedFrame=ImGui::GetCurrentContext()?ImGui::GetFrameCount():-1;
    g.text=Model::FormatHex(g.target?g.document.RampColor(unsigned(g.ramp)):g.document.Colors()[g.color]);g.textFocus=0;
    if(ImGui::GetCurrentContext()) ImGui::GetIO().InputQueueCharacters.resize(0);
    Neutral();
}
void EditHex() {StartText(State::TextKind::Hex);}
void Pin() { g.document.SetPinned(unsigned(g.color),g.pin); }
void ResetSelected() {
    if (g.target) g.document.ResetRamp(unsigned(g.ramp)); else g.document.ResetEntry(unsigned(g.color));
    SyncColor();
}
void Undo() { g.document.Undo(); SyncColor(); }
void ResetAll() { Confirm("RESET COLORS", "Reset all colors and pins to the original stock palette?", "RESET", []{g.document.ResetAll();SyncColor();}); }
void Revert() { Confirm("REVERT DRAFT", "Restore the last loaded or saved state?", "REVERT", []{g.document.Revert();SyncColor();}); }
const char* HexValue() { static std::string text; text=Model::FormatHex(g.target ? g.document.RampColor(unsigned(g.ramp)) : g.document.Colors()[g.color]); return text.c_str(); }
void VariantChanged() {
    Model::CharacterAssets assets;
    std::string error;
    if(!Model::LoadCharacterAssets(g.context.gameDirectory,g.context.resourceName,assets,error,
        g.alternate?Model::AnimationVariant::Alternate:Model::AnimationVariant::Standard)) {
        g.alternate=!g.alternate;Status(error);return;
    }
    g.assets=std::move(assets);ResetPreview();BuildMoveChoices();
    g_originalTexture.sprite=g_workingTexture.sprite=-1;
    Status(g.alternate?"Alternate PAT animations loaded.":"Standard PAT animations loaded.");
}
void AnimationChanged() { g.frame=0;g.elapsed=0; }
const Model::Animation* Animation() { return NoAnimations()?nullptr:&g.assets.animations[g.animation]; }
void FrameChanged() { g.playing=false;g.elapsed=0; }
void Recenter() { g.panX=g.panY=0;g.autoFit=true; }
void ZoomChanged(){g.autoFit=false;}
void Step(int direction) {
    const auto* animation=Animation();
    if (!animation || animation->frames.empty()) return;
    const int count=int(animation->frames.size());
    g.frame=(g.frame+count+direction)%count;
    FrameChanged();
}
void PrevFrame(){Step(-1);}
void NextFrame(){Step(1);}
void AdvancePreview() {
    const auto* animation=Animation();
    if (!animation || animation->frames.empty()) return;
    g.frame=(std::clamp)(g.frame,0,int(animation->frames.size())-1);
    if (!g.playing) return;
    g.elapsed+=(std::min)(double(ImGui::GetIO().DeltaTime),0.25);
    const int count=int(animation->frames.size());
    for (int skipped=0;skipped<count;++skipped) {
        const double duration=animation->frames[g.frame].durationFrames/64.0;
        if (duration>0 && g.elapsed<duration) return;
        if (duration>0) g.elapsed-=duration;
        g.frame=(g.frame+1)%count;
    }
    g.elapsed=0;
}
bool UpdateTexture(Texture& texture,int spriteId,const Model::Palette256& colors,int marked) {
    if (!g_device || spriteId<0 || spriteId>=int(g.assets.sprites.size())) return false;
    const auto& sprite=g.assets.sprites[spriteId];
    if (sprite.width<=0 || sprite.height<=0) return false;
    if (texture.value && texture.sprite==spriteId && texture.colors==colors && texture.marked==marked) return true;
    int width=1,height=1;
    while(width<sprite.width) width*=2;
    while(height<sprite.height) height*=2;
    if (!texture.value || texture.width!=width || texture.height!=height) {
        texture.Release();
        if (FAILED(g_device->CreateTexture(width,height,1,D3DUSAGE_DYNAMIC,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&texture.value,nullptr))) return false;
        texture.width=width;texture.height=height;
    }
    D3DLOCKED_RECT lock{};
    if (FAILED(texture.value->LockRect(0,&lock,nullptr,D3DLOCK_DISCARD))) return false;
    for(int y=0;y<height;++y) {
        auto* pixels=reinterpret_cast<DWORD*>(static_cast<char*>(lock.pBits)+y*lock.Pitch);
        for(int x=0;x<width;++x) {
            if (x>=sprite.width || y>=sprite.height) {pixels[x]=0;continue;}
            const auto index=sprite.indices[size_t(y)*sprite.width+x];
            const auto c=colors[index];
            pixels[x]=index==g.assets.transparentKey?0:(index==marked?0xFFFFFFFFu:D3DCOLOR_ARGB(255,c.r,c.g,c.b));
        }
    }
    if (FAILED(texture.value->UnlockRect(0))) return false;
    texture.sprite=spriteId;texture.colors=colors;texture.marked=marked;
    return true;
}
void PointSampler(const ImDrawList*,const ImDrawCmd*) {
    if (!g_device) return;
    g_device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);
    g_device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
}
void DrawPreview(ImDrawList* dl,float x,float y,float width,float height,bool input) {
    dl->AddRectFilled(ImVec2(x,y),ImVec2(x+width,y+height),IM_COL32(10,14,19,235));
    for(int cy=0;cy<int(height);cy+=16) for(int cx=0;cx<int(width);cx+=16)
        if (((cx+cy)/16)%2==0) dl->AddRectFilled(ImVec2(x+cx,y+cy),ImVec2((std::min)(x+cx+16,x+width),(std::min)(y+cy+16,y+height)),IM_COL32(24,29,34,255));
    dl->AddRect(ImVec2(x,y),ImVec2(x+width,y+height),Theme::kRuleDim);
    const auto* animation=Animation();
    if (!animation || animation->frames.empty()) {Text(dl,x+12,y+12,"No drawable animation.");return;}
    const auto& frame=animation->frames[g.frame];
    if (frame.spriteId<0 || frame.spriteId>=int(g.assets.sprites.size())) return;
    const auto& sprite=g.assets.sprites[frame.spriteId];
    if(g.autoFit) {
        g.zoom=(std::clamp)(int(100*(std::min)(width*0.9f/sprite.width,height*0.75f/sprite.height)),25,100);
        g.autoFit=false;
    }
    const float scale=g.zoom/100.0f;
    const ImVec2 top(x+width*0.5f+g.panX-frame.originX*scale,y+height*0.80f+g.panY-frame.originY*scale);
    const ImVec2 bottom(top.x+sprite.width*scale,top.y+sprite.height*scale);
    const auto working=Model::BuildPreviewColors(g.document.Colors(),g.shared);
    const auto original=Model::BuildPreviewColors(g.document.Original(),g.shared);
    const int marked=g.marking && int(ImGui::GetTime()*4)%2?g.color:-1;
    const bool ready=UpdateTexture(g_workingTexture,frame.spriteId,working,marked) && UpdateTexture(g_originalTexture,frame.spriteId,original,-1);
    dl->PushClipRect(ImVec2(x+1,y+1),ImVec2(x+width-1,y+height-1),true);
    if (ready) {
        dl->AddCallback(PointSampler,nullptr);
        const auto draw=[&](Texture& texture) {
            dl->AddImage(reinterpret_cast<ImTextureID>(texture.value),top,bottom,ImVec2(0,0),ImVec2(float(sprite.width)/texture.width,float(sprite.height)/texture.height));
        };
        if (g.compare==2) {
            dl->PushClipRect(ImVec2(x,y),ImVec2(x+width/2,y+height),true);draw(g_originalTexture);dl->PopClipRect();
            dl->PushClipRect(ImVec2(x+width/2,y),ImVec2(x+width,y+height),true);draw(g_workingTexture);dl->PopClipRect();
            dl->AddLine(ImVec2(x+width/2,y),ImVec2(x+width/2,y+height),Theme::kSelectedLine);
        } else draw(g.compare==1?g_originalTexture:g_workingTexture);
        dl->AddCallback(ImDrawCallback_ResetRenderState,nullptr);
    } else Text(dl,x+12,y+12,"Preview texture unavailable.");
    dl->PopClipRect();
    if (!input) return;
    const auto mouse=Input::GetMouse();
    if (Input::MouseHovering(x,y,width,height)) {
        if(g.mouseHelp) g.helpControl=27;
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        const float wheel=Input::MouseWheel();
        if (wheel) {g.zoom=(std::clamp)(g.zoom+(wheel>0?25:-25),25,400);g.autoFit=false;g.previewWheel=true;}
        if (Input::MouseRightClickedIn(x,y,width,height)) {g.panning=true;g.panMouseX=mouse.x;g.panMouseY=mouse.y;}
        if (Input::MouseClickedIn(x,y,width,height)) {
            const int px=int(std::floor((mouse.x-top.x)/scale)),py=int(std::floor((mouse.y-top.y)/scale));
            if (px>=0 && py>=0 && px<sprite.width && py<sprite.height) {
                const int index=sprite.indices[size_t(py)*sprite.width+px];
                if (index==g.assets.transparentKey) Status("Transparent pixel: no visible color to pick.");
                else if (index>=40) Status("Shared effect palette: read-only; not stored in the character PAL.");
                else {
                    if(g.target) {
                        const auto& ramps=g.document.Ramps();
                        const auto found=std::find_if(ramps.begin(),ramps.end(),[index](const Model::Ramp& ramp){return unsigned(index)>=ramp.first && unsigned(index)<=ramp.last;});
                        if(found==ramps.end()) {Status("This shade has no ramp; switch to Shade to edit it.");return;}
                        g.ramp=int(found-ramps.begin());SelectRamp();
                    } else {g.color=index;SelectColor();}
                    Status(g.target?I18n::TrFormat("Picked ramp %d from shade %d.",g.ramp,index):I18n::TrFormat("Picked shade %d.",index));
                }
            }
        }
    }
    if (g.panning && ImGui::IsMouseDown(ImGuiMouseButton_Right) && mouse.valid) {
        g.panX=(std::clamp)(g.panX+int(mouse.x-g.panMouseX),-2048,2048);
        g.panY=(std::clamp)(g.panY+int(mouse.y-g.panMouseY),-2048,2048);
        g.panMouseX=mouse.x;g.panMouseY=mouse.y;
    } else g.panning=false;
}
void DrawSwatches(ImDrawList* dl,float x,float y,float width,bool input,float availableHeight=0) {
    const float cell=width/8.0f;
    float height=(std::max)(20.0f,Scale::Get().bodyPx+8);
    if(availableHeight>0) height=(std::min)(height,availableHeight/5);
    for(int i=0;i<40;++i) {
        const float sx=x+(i%8)*cell,sy=y+(i/8)*height;
        const auto c=g.document.Colors()[i];
        dl->AddRectFilled(ImVec2(sx+2,sy+2),ImVec2(sx+cell-2,sy+height-2),IM_COL32(c.r,c.g,c.b,255));
        if(i==g.color) dl->AddRect(ImVec2(sx,sy),ImVec2(sx+cell,sy+height),Theme::kSelectedLine,0,0,2);
        const auto color=(int(c.r)*299+int(c.g)*587+int(c.b)*114)>140000?IM_COL32(0,0,0,255):IM_COL32(255,255,255,255);
        if(height>=Scale::Get().bodyPx+4) Text(dl,sx+3,sy+(height-Scale::Get().bodyPx)/2,std::to_string(i),color);
        if(g.mouseHelp && Input::MouseHovering(sx,sy,cell,height)) g.helpControl=0;
        if (g.document.Pinned(i)) dl->AddCircleFilled(ImVec2(sx+cell-6,sy+6),2,Theme::kInfoAccent);
        if (input && Input::MouseClickedIn(sx,sy,cell,height)) {g.color=i;SelectColor();g.editorFocus=0;}
        if (input && Input::MouseRightClickedIn(sx,sy,cell,height)) {
            g.document.EndGesture();g.keyboardGesture=false;g.sliderRow=-1;g.dragging=false;
            g.document.SetPinned(i,!g.document.Pinned(i));SyncColor();
        }
    }
}
void MoveChoiceChanged() {
    if(g.moveIndices.empty()) return;
    g.moveChoice=(std::clamp)(g.moveChoice,0,int(g.moveIndices.size())-1);
    g.animation=g.moveIndices[g.moveChoice];AnimationChanged();
}
std::string ButtonLabel(const std::string& text,float width) {
    const std::string translated=I18n::Tr(text.c_str());
    ImFont* font=Layout::BodyFont();const float px=font?font->FontSize:Scale::Get().bodyPx;
    if(Layout::MeasureTextW(font,px,translated.c_str())<=width) return translated;
    std::string fitted=translated;
    while(!fitted.empty() && Layout::MeasureTextW(font,px,(fitted+"...").c_str())>width) {
        size_t end=fitted.size();
        do {--end;} while(end && (static_cast<unsigned char>(fitted[end])&0xC0)==0x80);
        fitted.resize(end);
    }
    return fitted+"...";
}
bool EditorButton(ImDrawList* dl,int focus,float x,float y,float width,float height,
                  const std::string& label,bool input,bool disabled=false,bool tabStyle=false) {
    if(g.mouseHelp && Input::MouseHovering(x,y,width,height)) g.helpControl=focus;
    const bool clicked=input && !disabled && Input::MouseClickedIn(x,y,width,height);
    if(clicked) g.editorFocus=focus;
    if(clicked || (input && g.editorFocus==focus && Input::Activate())) {g.document.EndGesture();g.keyboardGesture=false;g.sliderRow=-1;g.dragging=false;}
    dl->PushClipRect(ImVec2(x,y),ImVec2(x+width,y+height),true);
    const std::string fitted=ButtonLabel(label,width-16);
    dl->AddRectFilled(ImVec2(x,y),ImVec2(x+width,y+height),Theme::kStrip | IM_COL32_A_MASK);
    if(tabStyle) {
        const bool focused=g.editorFocus==focus || g.helpControl==focus;
        const ImU32 color=disabled?Theme::kTextDisabled:focused?Theme::kTextActive:Theme::kTextInactive;
        if(focused) dl->AddRectFilled(ImVec2(x,y),ImVec2(x+width,y+height),Theme::kSelectedFill | IM_COL32_A_MASK);
        dl->AddLine(ImVec2(x+4,y+height-2),ImVec2(x+width-4,y+height-2),focused?Theme::kSelectedLine:Theme::kTextInactive,focused?2.0f:1.0f);
        const float textWidth=Layout::MeasureTextW(Layout::BodyFont(),Scale::Get().bodyPx,fitted.c_str());
        Text(dl,x+(width-textWidth)/2,y+(height-Scale::Get().bodyPx)/2-1,fitted,color);
    } else Layout::DrawButton(dl,x,y,width,height,fitted.c_str(),g.editorFocus==focus,disabled);
    dl->PopClipRect();
    return !disabled && (clicked || (input && g.editorFocus==focus && Input::Activate()));
}
bool EditorIcon(ImDrawList* dl,int focus,float x,float y,bool input,bool disabled=false) {
    const bool clicked=EditorButton(dl,focus,x,y,26,26,"",input,disabled);
    const float cx=x+13,cy=y+13;
    const ImU32 color=disabled?Theme::kTextDisabled:focus==6 && g.pin?Theme::kInfoAccent:Theme::kTextActive;
    switch(focus) {
    case 6:
        dl->AddRect(ImVec2(cx-4,cy-7),ImVec2(cx+4,cy-3),color);
        dl->AddLine(ImVec2(cx-2,cy-3),ImVec2(cx-4,cy+2),color,1.5f);
        dl->AddLine(ImVec2(cx+2,cy-3),ImVec2(cx+4,cy+2),color,1.5f);
        dl->AddLine(ImVec2(cx-5,cy+2),ImVec2(cx+5,cy+2),color,1.5f);
        dl->AddLine(ImVec2(cx,cy+2),ImVec2(cx,cy+8),color,1.5f);break;
    case 7:
        dl->PathArcTo(ImVec2(cx,cy),6,-2.5f,2.5f,16);dl->PathStroke(color,0,1.5f);
        dl->AddTriangleFilled(ImVec2(cx-7,cy-6),ImVec2(cx-1,cy-6),ImVec2(cx-6,cy),color);break;
    case 8:
        dl->AddBezierCubic(ImVec2(cx+5,cy+6),ImVec2(cx+11,cy-5),ImVec2(cx-1,cy-8),ImVec2(cx-6,cy-2),color,1.5f);
        dl->AddTriangleFilled(ImVec2(cx-7,cy-2),ImVec2(cx-1,cy-6),ImVec2(cx-1,cy+2),color);break;
    case 29:
        dl->AddCircle(ImVec2(cx,cy),3,color,12,1.5f);
        for(int i=0;i<8;++i) {
            const float angle=i*0.78539816f,dx=std::cos(angle),dy=std::sin(angle);
            dl->AddLine(ImVec2(cx+5*dx,cy+5*dy),ImVec2(cx+8*dx,cy+8*dy),color,1.5f);
        }break;
    case 30:
        dl->PathLineTo(ImVec2(cx+3,cy-7));
        dl->PathBezierCubicCurveTo(ImVec2(cx-9,cy-7),ImVec2(cx-9,cy+8),ImVec2(cx+3,cy+7));
        dl->PathBezierCubicCurveTo(ImVec2(cx-3,cy+4),ImVec2(cx-3,cy-4),ImVec2(cx+3,cy-7));
        dl->PathStroke(color,ImDrawFlags_Closed,1.5f);break;
    case 31:
        dl->AddRect(ImVec2(cx-3,cy-3),ImVec2(cx+7,cy+7),color,0,0,1.5f);
        dl->PathLineTo(ImVec2(cx-7,cy+3));dl->PathLineTo(ImVec2(cx-7,cy-7));
        dl->PathLineTo(ImVec2(cx+3,cy-7));dl->PathLineTo(ImVec2(cx+3,cy-5));dl->PathStroke(color,0,1.5f);break;
    case 32:
        dl->AddRect(ImVec2(cx-6,cy-5),ImVec2(cx+6,cy+8),color,0,0,1.5f);
        dl->AddRectFilled(ImVec2(cx-3,cy-8),ImVec2(cx+3,cy-3),color);
        dl->AddLine(ImVec2(cx-3,cy+1),ImVec2(cx+3,cy+1),color);
        dl->AddLine(ImVec2(cx-3,cy+4),ImVec2(cx+3,cy+4),color);break;
    }
    return clicked;
}
enum class ColorTool { Generate, Randomize, GradientRgb, GradientHsv, Lighten, Darken, Copy, Paste };
constexpr const char* kColorTools[]={"Generate palette","Randomize selection","RGB gradient","HSV gradient"};
const char* ColorToolHelp(const Screens::Row&,int choice) {
    static const char* help[]={
        "Generate new ramp colors for the whole palette. Pins and transparency are protected; Undo restores the draft.",
        "Randomize the selected ramp or shade. Pins and transparency are protected.",
        "Blend between this ramp's endpoint colors in RGB. Changed shades become pinned overrides; existing pins are kept.",
        "Blend this ramp's endpoints along the shortest hue path. Changed shades become pinned overrides; existing pins are kept.",
        "Lighten the selected ramp or shade by 16 RGB steps. Ramp shading and pinned shades are preserved.",
        "Darken the selected ramp or shade by 16 RGB steps. Ramp shading and pinned shades are preserved.",
        "Copy the selected ramp's target color or individual shade for use elsewhere in this editor.",
        "Paste the copied color into this ramp or shade. Ramp shading and pinned shades are preserved."
    };
    return choice>=0 && choice<int(std::size(help))?help[choice]:"";
}
void RunColorTool(ColorTool tool) {
    Neutral();
    const auto current=g.target?g.document.RampColor(unsigned(g.ramp)):g.document.Colors()[g.color];
    if(tool==ColorTool::Copy) {g.copiedColor=current;Status("Color copied.");return;}
    if(tool==ColorTool::Paste && !g.copiedColor) {Status("Copy a color before pasting.");return;}
    bool changed=false;
    if(tool==ColorTool::Generate || tool==ColorTool::Randomize) {
        static std::mt19937 random(GetTickCount());
        const auto scope=tool==ColorTool::Generate?Model::ColorScope::Palette:g.target?Model::ColorScope::Ramp:Model::ColorScope::Shade;
        changed=g.document.Generate(random(),scope,unsigned(g.target?g.ramp:g.color),g.assets.transparentKey);
    } else if(tool==ColorTool::GradientRgb || tool==ColorTool::GradientHsv) {
        const auto& ramps=g.document.Ramps();
        const auto ramp=std::find_if(ramps.begin(),ramps.end(),[](const auto& r){return unsigned(g.color)>=r.first && unsigned(g.color)<=r.last;});
        if(ramp!=ramps.end()) changed=g.document.Gradient(unsigned(ramp-ramps.begin()),g.assets.transparentKey,tool==ColorTool::GradientHsv);
    } else if(tool==ColorTool::Lighten || tool==ColorTool::Darken || tool==ColorTool::Paste) {
        const int step=tool==ColorTool::Lighten?16:-16;
        const auto shift=[step](unsigned value){return uint8_t((std::clamp)(int(value)+step,0,255));};
        const auto color=tool==ColorTool::Paste?*g.copiedColor:Model::Color{shift(current.r),shift(current.g),shift(current.b)};
        if(!g.target && unsigned(g.color)==g.assets.transparentKey) {Status("The transparent shade is protected.");return;}
        changed=g.target?g.document.SetRampColor(unsigned(g.ramp),color):g.document.SetColor(unsigned(g.color),color);
    }
    SyncColor();Status(changed?"Palette tool applied. Undo restores the previous edit.":"No editable colors changed. Check pins and the selected ramp.");
}
void ToolChoiceChanged() {RunColorTool(static_cast<ColorTool>(g.toolChoice));}
void OpenEditorChoice(const Screens::Row& row) {
    Screens::OpenDropdownPopup(row);Neutral();
}
void CompactSlider(ImDrawList* dl,int component,float x,float y,float width,bool input) {
    const int focus=component+3,maximum=HsvControls()?(component?100:359):255;
    const float height=22,left=x+27,trackWidth=width-76;
    const auto mouse=Input::GetMouse();
    if(g.mouseHelp && Input::MouseHovering(x,y,width,height)) g.helpControl=focus;
    if(input && Input::MouseClickedIn(x,y,width,height)) g.editorFocus=focus;
    if(input && Input::MouseClickedIn(left-4,y,trackWidth+8,height)) {
        g.sliderRow=focus;g.sliderLeft=left;g.sliderWidth=trackWidth;
        g.document.EndGesture();g.keyboardGesture=false;g.document.BeginGesture();g.dragging=true;
    }
    if(input && g.sliderRow==focus && ImGui::IsMouseDown(ImGuiMouseButton_Left) && mouse.valid) {
        const float fraction=(std::clamp)((mouse.x-g.sliderLeft)/g.sliderWidth,0.0f,1.0f);
        const int value=int(std::lround(fraction*maximum));
        if(g.components[component]!=value) {g.components[component]=value;ApplyComponents();}
    }
    dl->AddRectFilled(ImVec2(x,y),ImVec2(x+width,y+height),Theme::kStrip | IM_COL32_A_MASK);
    Layout::DrawButton(dl,x,y,width,height,"",g.editorFocus==focus);
    const float textY=y+(height-Scale::Get().bodyPx)/2;
    const char* rgb[]={"R","G","B"};const char* hsv[]={"H","S","V"};
    Text(dl,x+10,textY,HsvControls()?hsv[component]:rgb[component],Theme::kTextInactive);
    const float trackY=y+height/2;
    dl->AddRectFilled(ImVec2(left,trackY-2),ImVec2(left+trackWidth,trackY+2),Theme::kSliderTrackOff);
    const float knob=left+trackWidth*g.components[component]/float(maximum);
    dl->AddRectFilled(ImVec2(left,trackY-2),ImVec2(knob,trackY+2),Theme::kSelectedLine);
    dl->AddCircleFilled(ImVec2(knob,trackY),3,Theme::kTextActive);
    Text(dl,x+width-42,textY,std::to_string(g.components[component]));
}
void DrawColorPicker(ImDrawList* dl,float x,float y,float width,bool input) {
    using Pick=State::PickerInput;
    const float px=x+6,py=y+6,svWidth=width-44,height=73,hueX=x+width-25,hueWidth=19;
    const auto mouse=Input::GetMouse();
    if(g.mouseHelp && Input::MouseHovering(px,py,svWidth,height)) g.helpControl=3;
    if(g.mouseHelp && Input::MouseHovering(hueX,py,hueWidth,height)) g.helpControl=4;
    const bool dragging=g.pickerInput==Pick::DragSV || g.pickerInput==Pick::DragHue;
    if(g.pickerInput!=Pick::Idle && (!input || (dragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left)))) {
        g.document.EndGesture();g.pickerInput=Pick::Idle;
    }
    if(input && g.pickerInput==Pick::Idle) {
        Pick next=Pick::Idle;
        if(Input::MouseClickedIn(px,py,svWidth,height)) {next=Pick::DragSV;g.editorFocus=3;}
        else if(Input::MouseClickedIn(hueX,py,hueWidth,height)) {next=Pick::DragHue;g.editorFocus=4;}
        else if(Input::Activate() && (g.editorFocus==3 || g.editorFocus==4)) next=g.editorFocus==3?Pick::AdjustSV:Pick::AdjustHue;
        if(next!=Pick::Idle) {g.document.EndGesture();g.document.BeginGesture();g.pickerInput=next;}
    } else if(input && (g.pickerInput==Pick::AdjustSV || g.pickerInput==Pick::AdjustHue)) {
        if(Input::Back() || Input::Activate()) Neutral();
        else {
            const int step=ImGui::GetIO().KeyShift?10:1;
            const int horizontal=(Input::NavRight()?step:0)-(Input::NavLeft()?step:0);
            const int vertical=(Input::NavDown()?step:0)-(Input::NavUp()?step:0);
            if(horizontal || vertical) {
                if(g.pickerInput==Pick::AdjustSV) {
                    g.components[1]=(std::clamp)(g.components[1]+horizontal,0,100);
                    g.components[2]=(std::clamp)(g.components[2]-vertical,0,100);
                } else g.components[0]=(g.components[0]+horizontal+vertical+360)%360;
                ApplyComponents();
            }
        }
    }
    if(input && mouse.valid && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if(g.pickerInput==Pick::DragSV) {
            g.components[1]=int(std::lround(100*(std::clamp)((mouse.x-px)/(svWidth-1),0.0f,1.0f)));
            g.components[2]=int(std::lround(100*(1-(std::clamp)((mouse.y-py)/(height-1),0.0f,1.0f))));
            ApplyComponents();
        } else if(g.pickerInput==Pick::DragHue) {
            g.components[0]=int(std::lround(359*(std::clamp)((mouse.y-py)/(height-1),0.0f,1.0f)));
            ApplyComponents();
        }
    }
    const auto hsv=g.target?g.document.RampHsv(unsigned(g.ramp)):g.document.ColorHsv(unsigned(g.color));
    const auto packed=[](Model::Hsv value) {const auto color=Model::HsvToRgb(value);return IM_COL32(color.r,color.g,color.b,255);};
    const ImU32 hue=packed({hsv.h,100,100}),white=IM_COL32(255,255,255,255),black=IM_COL32(0,0,0,255);
    dl->AddRectFilled(ImVec2(x,y),ImVec2(x+width,y+85),Theme::kStrip | IM_COL32_A_MASK);
    dl->AddRectFilledMultiColor(ImVec2(px,py),ImVec2(px+svWidth,py+height),white,hue,hue,white);
    dl->AddRectFilledMultiColor(ImVec2(px,py),ImVec2(px+svWidth,py+height),0,0,black,black);
    for(int segment=0;segment<6;++segment) {
        const auto top=packed({float(segment*60),100,100}),bottom=packed({float((segment+1)*60),100,100});
        dl->AddRectFilledMultiColor(ImVec2(hueX,py+height*segment/6),ImVec2(hueX+hueWidth,py+height*(segment+1)/6),top,top,bottom,bottom);
    }
    dl->AddRect(ImVec2(px-1,py-1),ImVec2(px+svWidth+1,py+height+1),g.editorFocus==3?Theme::kSelectedLine:Theme::kTextInactive);
    dl->AddRect(ImVec2(hueX-1,py-1),ImVec2(hueX+hueWidth+1,py+height+1),g.editorFocus==4?Theme::kSelectedLine:Theme::kTextInactive);
    const ImVec2 selected(px+(svWidth-1)*hsv.s/100,py+(height-1)*(1-hsv.v/100));
    dl->AddCircle(selected,4,black,12,3);dl->AddCircle(selected,4,white,12,1);
    const float hueY=py+(height-1)*(std::min)(hsv.h,359.0f)/359;
    dl->AddLine(ImVec2(hueX-2,hueY),ImVec2(hueX+hueWidth+2,hueY),black,4);
    dl->AddLine(ImVec2(hueX-2,hueY),ImVec2(hueX+hueWidth+2,hueY),white,2);
}
struct RampGridLayout {int repRows,shadeRows;float repHeight,shadeY,shadeHeight;};
RampGridLayout RampGridGeometry(const std::vector<Model::Ramp>& ramps,int selected,float height) {
    const int repRows=(int(ramps.size())+9)/10;
    int maxShadeRows=1;
    for(const auto& ramp:ramps) maxShadeRows=(std::max)(maxShadeRows,(int(ramp.last-ramp.first)+8)/8);
    const float labelHeight=Scale::Get().bodyPx+9;
    const float repHeight=(std::min)(24.0f,(height-labelHeight-(std::max)(24.0f,maxShadeRows*8.0f))/repRows);
    const auto& ramp=ramps[selected];const int shadeRows=(int(ramp.last-ramp.first)+8)/8;
    const float shadeY=repRows*repHeight+labelHeight;
    return {repRows,shadeRows,repHeight,shadeY,(std::min)(25.0f,(height-shadeY)/shadeRows)};
}
void DrawRampGrid(ImDrawList* dl,float x,float y,float width,float height,bool input) {
    const auto& ramps=g.document.Ramps();
    if(ramps.empty()) {Text(dl,x,y,"No shade groups found.",Theme::kTextInactive);return;}
    const int columns=10;
    const auto geometry=RampGridGeometry(ramps,g.ramp,height);
    const float repWidth=width/columns;
    dl->PushClipRect(ImVec2(x,y),ImVec2(x+width,y+height),true);
    for(int i=0;i<int(ramps.size());++i) {
        const float sx=x+(i%columns)*repWidth,sy=y+(i/columns)*geometry.repHeight;
        const auto color=g.document.RampColor(unsigned(i));
        dl->AddRectFilled(ImVec2(sx+2,sy+2),ImVec2(sx+repWidth-2,sy+geometry.repHeight-2),IM_COL32(color.r,color.g,color.b,255));
        if(i==g.ramp) dl->AddRect(ImVec2(sx,sy),ImVec2(sx+repWidth,sy+geometry.repHeight),Theme::kSelectedLine,0,0,2);
        if(g.mouseHelp && Input::MouseHovering(sx,sy,repWidth,geometry.repHeight)) g.helpControl=0;
        if(input && Input::MouseClickedIn(sx,sy,repWidth,geometry.repHeight)) {g.ramp=i;SelectRamp();g.editorFocus=0;}
    }
    const auto& ramp=ramps[g.ramp];
    const float labelY=y+geometry.repRows*geometry.repHeight+3;
    Text(dl,x,labelY,I18n::TrFormat("SHADES %d - %d",ramp.first,ramp.last),Theme::kTextInactive);
    const int shadeRows=(int(ramp.last-ramp.first)+8)/8;
    const float shadeY=y+geometry.shadeY,shadeHeight=(std::min)(25.0f,(height-geometry.shadeY)/shadeRows),shadeWidth=width/8;
    for(unsigned i=ramp.first;i<=ramp.last;++i) {
        const unsigned slot=i-ramp.first;
        const float sx=x+(slot%8)*shadeWidth,sy=shadeY+(slot/8)*shadeHeight;
        const auto color=g.document.Colors()[i];
        dl->AddRectFilled(ImVec2(sx+2,sy+2),ImVec2(sx+shadeWidth-2,sy+shadeHeight-2),IM_COL32(color.r,color.g,color.b,255));
        if(g.document.Pinned(i)) dl->AddCircleFilled(ImVec2(sx+shadeWidth-6,sy+6),2,Theme::kInfoAccent);
        if(g.mouseHelp && Input::MouseHovering(sx,sy,shadeWidth,shadeHeight)) g.helpControl=0;
        if(input && Input::MouseClickedIn(sx,sy,shadeWidth,shadeHeight)) {g.color=int(i);SelectColor();g.editorFocus=0;}
    }
    dl->PopClipRect();
}
void DrawEditor(ImDrawList* dl,float top,bool input) {
    constexpr float left=28,right=296,width=252,previewWidth=316;
    const bool alternate=Model::HasAlternateAnimations(g.context.resourceName);
    if(g.keyboardGesture && (!input || (!Input::NavLeftHeld() && !Input::NavRightHeld()) || g.editorFocus!=g.keyboardChannel)) {
        g.document.EndGesture();g.keyboardGesture=false;
    }
    if(!ImGui::IsMouseDown(ImGuiMouseButton_Left) && g.sliderRow>=0) {g.document.EndGesture();g.dragging=false;g.sliderRow=-1;}
    if(input && g.panMode) {
        if(Input::NavLeft()) g.panX=(std::max)(-2048,g.panX-4);if(Input::NavRight()) g.panX=(std::min)(2048,g.panX+4);
        if(Input::NavUp()) g.panY=(std::max)(-2048,g.panY-4);if(Input::NavDown()) g.panY=(std::min)(2048,g.panY+4);
        if(Input::Activate() || Input::Back()) {g.panMode=false;Neutral();input=false;}
    } else if(input && g.sliderRow<0 && g.pickerInput==State::PickerInput::Idle) {
        static const int order[]={22,9,28,0,1,2,3,4,5,6,7,8,29,30,31,32,10,26,11,12,13,14,15,16,17,18,19,23,24,25,20,21};
        if(Input::NavUp() || Input::NavDown()) {
            int position=int(std::find(std::begin(order),std::end(order),g.editorFocus)-std::begin(order));
            if(position==0 && Input::NavUp()) {g.focusAbove=true;Neutral();input=false;}
            else {
                const int step=Input::NavDown()?1:-1;
                do {position=(position+step+int(std::size(order)))%int(std::size(order));}
                while((order[position]==5 && g.colorControls==State::ColorControls::Picker) || (order[position]==26 && !alternate));
                g.editorFocus=order[position];
            }
        }
        if(g.keyboardGesture && g.editorFocus!=g.keyboardChannel) {g.document.EndGesture();g.keyboardGesture=false;}
        const int direction=Input::NavRight()?1:Input::NavLeft()?-1:0;
        if(direction) switch(g.editorFocus) {
        case 0:
            if(g.target && !NoRamps()) {g.ramp=(g.ramp+direction+int(g.document.Ramps().size()))%int(g.document.Ramps().size());SelectRamp();}
            else {g.color=(g.color+direction+40)%40;SelectColor();}break;
        case 1:CycleColorControls();break;
        case 3:case 4:case 5: {
            if(g.colorControls==State::ColorControls::Picker) break;
            if(!g.keyboardGesture || g.keyboardChannel!=g.editorFocus) {g.document.EndGesture();g.document.BeginGesture();g.keyboardGesture=true;g.keyboardChannel=g.editorFocus;}
            const int component=g.editorFocus-3,maximum=HsvControls()?(component?100:359):255;
            g.components[component]=(std::clamp)(g.components[component]+direction*(ImGui::GetIO().KeyShift?10:1),0,maximum);ApplyComponents();break;
        }
        case 6:g.pin=direction>0;Pin();break;
        case 9:g.stock=(g.stock+direction+6)%6;LoadStock();break;
        case 10:if(!g.moveIndices.empty()){g.moveChoice=(g.moveChoice+direction+int(g.moveIndices.size()))%int(g.moveIndices.size());MoveChoiceChanged();}break;
        case 11:case 12:case 13:Step(direction);break;
        case 14:g.zoom=(std::clamp)(g.zoom+direction*25,25,400);ZoomChanged();break;
        case 17:g.compare=(g.compare+direction+3)%3;break;
        case 18:g.marking=direction>0;break;
        case 19:g.live=direction>0;break;
        case 22:SwitchPlayer();break;
        case 26:g.alternate=!g.alternate;VariantChanged();break;
        default:break;
        }
        if(Input::Back()) {g.closeRequested=true;Neutral();input=false;}
    }
    bool controls=input && !g.panMode && g.pickerInput==State::PickerInput::Idle && !Modal();
    if(EditorButton(dl,22,left,top,64,20,"P"+std::to_string(g.player)+"  <>",controls)) SwitchPlayer();
    static const char* slots[]={"1","2","3","4","5","6"};
    if(EditorButton(dl,9,left+70,top,100,20,I18n::TrFormat("Stock %d",g.stock+1),controls))
        OpenEditorChoice(Screens::DropdownRow("Stock palette",&g.stock,slots,6,LoadStock));
    if(EditorButton(dl,28,left+176,top,76,20,"TOOLS",controls)) {
        g.toolChoice=0;
        auto row=Screens::DropdownRow("Palette tools",&g.toolChoice,kColorTools,int(std::size(kColorTools)),ToolChoiceChanged);
        row.choiceHelpFormatter=ColorToolHelp;OpenEditorChoice(row);
    }
    const std::string selection=g.target?I18n::TrFormat("RAMP %d  /  SHADE",g.ramp):I18n::TrFormat("SHADE %d  /  RAMP",g.color);
    if(EditorButton(dl,0,left,top+24,width,20,selection,controls)) {g.target=g.target?0:1;SyncColor();}
    if(g.target) DrawRampGrid(dl,left,top+50,width,80,controls);
    else DrawSwatches(dl,left,top+50,width,controls,80);
    const char* colorMode=g.colorControls==State::ColorControls::Picker?"PICKER":HsvControls()?"HSV":"RGB";
    if(EditorButton(dl,1,left,top+136,88,20,colorMode,controls)) CycleColorControls();
    if(EditorButton(dl,2,left+94,top+136,158,20,HexValue(),controls)) EditHex();
    if(g.colorControls==State::ColorControls::Picker) DrawColorPicker(dl,left,top+162,width,input && !g.panMode && !Modal());
    else for(int component=0;component<3;++component) CompactSlider(dl,component,left,top+162+component*26,width,controls);
    if(EditorIcon(dl,6,left+5,top+254,controls)) {g.pin=!g.pin;Pin();}
    if(EditorIcon(dl,7,left+41,top+254,controls)) ResetSelected();
    if(EditorIcon(dl,8,left+77,top+254,controls,NoUndo())) Undo();
    if(EditorIcon(dl,29,left+113,top+254,controls)) RunColorTool(ColorTool::Lighten);
    if(EditorIcon(dl,30,left+149,top+254,controls)) RunColorTool(ColorTool::Darken);
    if(EditorIcon(dl,31,left+185,top+254,controls)) RunColorTool(ColorTool::Copy);
    if(EditorIcon(dl,32,left+221,top+254,controls,!g.copiedColor)) RunColorTool(ColorTool::Paste);
    const std::string move=g.moveOptions.empty()?I18n::Tr("No drawable animations"):g.moveOptions[g.moveChoice];
    if(EditorButton(dl,10,right,top,previewWidth,22,move,controls,g.moveOptions.empty())) {
        auto row=Screens::DropdownRow("Move",&g.moveChoice,g.moveOptions.data(),int(g.moveOptions.size()),MoveChoiceChanged);
        row.choiceCategoryMap=g.choiceCategories.data();row.categoryChoices=kMoveCategories;row.categoryCount=int(std::size(kMoveCategories));
        OpenEditorChoice(row);
    }
    const float previewHeight=376-top-90;
    DrawPreview(dl,right,top+28,previewWidth,previewHeight,controls);
    if(g.compare==2) {
        FitText(dl,right+7,top+28+previewHeight-20,previewWidth/2-12,"ORIGINAL",Theme::kTextInactive);
        FitText(dl,right+previewWidth/2+7,top+28+previewHeight-20,previewWidth/2-12,"WORKING",Theme::kTextInactive);
    }
    if(alternate && EditorButton(dl,26,right+previewWidth-70,top+33,64,20,g.alternate?"ALT":"STD",controls)) {g.alternate=!g.alternate;VariantChanged();}
    const auto* animation=Animation();const int frames=animation?int(animation->frames.size()):0;
    const float rowY=top+28+previewHeight+5;
    if(EditorButton(dl,11,right,rowY,38,20,"<",controls,frames==0)) PrevFrame();
    if(EditorButton(dl,12,right+44,rowY,228,20,frames?I18n::TrFormat(g.playing?"PAUSE  %d / %d":"PLAY  %d / %d",g.frame+1,frames):I18n::Tr("NO FRAMES"),controls,frames==0)) g.playing=!g.playing;
    if(EditorButton(dl,13,right+278,rowY,38,20,">",controls,frames==0)) NextFrame();
    if(EditorButton(dl,14,right,rowY+24,96,20,std::to_string(g.zoom)+"%",controls)) {g.zoom=g.zoom>=400?25:(std::min)(400,g.zoom+25);ZoomChanged();}
    if(EditorButton(dl,15,right+102,rowY+24,100,20,g.panMode?"PAN*":"PAN",input)) {g.panMode=!g.panMode;Neutral();}
    if(EditorButton(dl,16,right+208,rowY+24,108,20,"CENTER",controls)) Recenter();
    static const char* compares[]={"WORKING","ORIGINAL","SPLIT"};
    if(EditorButton(dl,17,right,rowY+48,110,20,compares[g.compare],controls)) g.compare=(g.compare+1)%3;
    if(EditorButton(dl,18,right+116,rowY+48,80,20,"MARK",controls)) g.marking=!g.marking;
    if(EditorButton(dl,19,right+202,rowY+48,114,20,"PREVIEW",controls)) g.live=!g.live;
    dl->AddCircleFilled(ImVec2(right+189,rowY+52),2,g.marking?Theme::kInfoAccent:Theme::kTextDisabled);
    dl->AddCircleFilled(ImVec2(right+309,rowY+52),2,g.live?Theme::kInfoAccent:Theme::kTextDisabled);
    controls=input && !g.panMode && g.pickerInput==State::PickerInput::Idle && !Modal() && !Screens::IsPopupActive();
    if(EditorButton(dl,23,28,392,112,24,"LOAD",controls,false,true)) LoadFile();
    if(EditorButton(dl,24,146,392,112,24,"SAVE",controls,false,true)) Save();
    if(EditorButton(dl,25,264,392,112,24,"SAVE AS",controls,false,true)) SaveAs();
    if(EditorButton(dl,20,382,392,112,24,"APPLY",controls,!g_applyColors,true)) ApplyToMatch();
    if(EditorButton(dl,21,500,392,112,24,"RESTORE",controls,false,true)) RestoreMatch();
}
const char* ControlHelp(int control) {
    if(control>=29 && control<=32) return ColorToolHelp(Screens::Row{},control-25);
    static const char* help[]={
        "Choose a ramp to recolor its shades together, or edit one shade.",
        "Switch between the visual picker, RGB sliders and HSV sliders.",
        "Enter a three or six digit hex color for the selected ramp or shade.",
        "Confirm to adjust saturation and brightness with the arrows. Back finishes editing.",
        "Confirm to adjust hue with the arrows. Back finishes editing.",
        "Adjust the blue channel or brightness of the selected ramp or shade.",
        "Pin this shade to protect it from ramp edits. Unpin it to follow the ramp color.",
        "Reset the selected ramp or shade to its original stock colors.",
        "Undo the last edit. A continuous drag or held adjustment is one edit.",
        "Choose an original stock palette. Replacing an unsaved draft requires confirmation.",
        "Choose an animation using the classification tabs. Preview never advances the match.",
        "Pause the animation and show its previous frame.",
        "Play or pause the sprite animation using its original frame durations.",
        "Pause the animation and show its next frame.",
        "Change sprite magnification. The mouse wheel also zooms over the preview.",
        "Pan with the arrows or right-drag the sprite. Confirm or Back finishes panning.",
        "Center the sprite and fit it inside the preview.",
        "Compare original and edited colors, or show both side by side.",
        "Highlight pixels using the selected shade in the sprite preview.",
        "Temporarily preview edits on the fighter. Apply keeps them when the menu closes.",
        "Keep these colors in the current Practice match. This does not save a file.",
        "Stop the temporary preview and restore the last applied colors. Keep the editable draft.",
        "Choose the fighter to edit. Switching away from an unsaved draft requires confirmation.",
        "Open a palette with Windows Open. JSON includes ramps and pins; PAL contains colors only.",
        "Save to the current file. A new draft opens Windows Save As.",
        "Use Windows Save As to choose another name or location. JSON keeps ramps and pins.",
        "Switch between the character's normal and alternate sprite animations.",
        "Click a sprite pixel to select its ramp or shade. Wheel zooms; right-drag pans.",
        "Generate palette colors or create RGB/HSV gradients in the selected ramp."
    };
    if(g.colorControls!=State::ColorControls::Picker) {
        if(control==3) return HsvControls()?"Adjust hue for the selected ramp or shade.":"Adjust the red channel for the selected ramp or shade.";
        if(control==4) return HsvControls()?"Adjust saturation for the selected ramp or shade.":"Adjust the green channel for the selected ramp or shade.";
    }
    return control>=0 && control<int(std::size(help))?help[control]:help[0];
}
void DrawConfirmation(ImDrawList* dl,bool inputEnabled=true) {
    const bool active=inputEnabled && Input::IsGameWindowActive() && ImGui::GetFrameCount()!=g.modalOpenedFrame;
    dl->AddRectFilled(ImVec2(0,0),ImVec2(640,480),IM_COL32(0,0,0,175));
    dl->AddRectFilled(ImVec2(76,150),ImVec2(564,330),Theme::kStrip | IM_COL32_A_MASK);
    Layout::DrawInfoBox(dl,76,150,488,180);
    Text(dl,94,166,g.confirmTitle,Theme::kInfoAccent);
    FitText(dl,94,204,452,g.confirmText);
    if(active && (Input::NavLeft() || Input::NavRight())) g.confirmFocus=1-g.confirmFocus;
    const bool keep=active && Input::MouseClickedIn(96,272,207,32);
    const bool proceed=active && Input::MouseClickedIn(337,272,207,32);
    Layout::DrawButton(dl,96,272,207,32,"KEEP EDITING",g.confirmFocus==0);
    Layout::DrawButton(dl,337,272,207,32,g.confirmVerb.c_str(),g.confirmFocus==1);
    if(!active) return;
    if(Input::Back() || keep || (Input::Activate() && g.confirmFocus==0)) {g.confirm={};Neutral();}
    else if(proceed || (Input::Activate() && g.confirmFocus==1)) {
        auto action=std::move(g.confirm);g.confirm={};Neutral();action();
    }
}
void CommitText() {
    const bool ok=g.target?g.document.SetRampHex(unsigned(g.ramp),g.text):g.document.SetHex(unsigned(g.color),g.text);
    Model::Color parsed;
    if(!ok && !Model::ParseHex(g.text,parsed)) {Status("Use 3 or 6 hexadecimal digits.");return;}
    SyncColor();g.textKind=State::TextKind::None;Neutral();
}
void DrawTextEditor(ImDrawList* dl,bool inputEnabled=true) {
    const bool hex=g.textKind==State::TextKind::Hex;
    auto& io=ImGui::GetIO();
    const size_t maximum=hex?7:32;
    const bool active=inputEnabled && Input::IsGameWindowActive() && ImGui::GetFrameCount()!=g.modalOpenedFrame;
    bool textKeys=io.InputQueueCharacters.Size>0 || ImGui::IsKeyDown(ImGuiKey_Space) || ImGui::IsKeyDown(ImGuiKey_Backspace);
    for(int key=ImGuiKey_0;key<=ImGuiKey_Z;++key) textKeys=textKeys || ImGui::IsKeyDown(ImGuiKey(key));
    for(int i=0;active && i<io.InputQueueCharacters.Size;++i) {
        const auto c=io.InputQueueCharacters[i];
        if(c>=32 && c<127 && g.text.size()<maximum && (!hex || std::isxdigit(int(c)) || c=='#')) g.text.push_back(char(c));
    }
    io.InputQueueCharacters.resize(0);
    if(active && ImGui::IsKeyPressed(ImGuiKey_Backspace) && !g.text.empty()) g.text.pop_back();
    if(active && ImGui::IsKeyPressed(ImGuiKey_Enter,false)) {CommitText();return;}
    if(active && !textKeys && Input::Back()) {g.textKind=State::TextKind::None;Neutral();return;}
    const std::string alphabet=hex?"0123456789ABCDEF":(g.lowerCase?"abcdefghijklmnopqrstuvwxyz0123456789 -_":"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_");
    const int chars=int(alphabet.size()),count=chars+4,columns=10;
    if(active && !textKeys && Input::NavLeft()) g.textFocus=(g.textFocus+count-1)%count;
    if(active && !textKeys && Input::NavRight()) g.textFocus=(g.textFocus+1)%count;
    if(active && !textKeys && Input::NavUp()) g.textFocus=(g.textFocus+count-columns)%count;
    if(active && !textKeys && Input::NavDown()) g.textFocus=(g.textFocus+columns)%count;
    dl->AddRectFilled(ImVec2(0,0),ImVec2(640,480),IM_COL32(0,0,0,185));
    dl->AddRectFilled(ImVec2(55,85),ImVec2(585,405),Theme::kStrip | IM_COL32_A_MASK);
    Layout::DrawInfoBox(dl,55,85,530,320);
    Text(dl,73,100,"HEX COLOR (RGB / RRGGBB)",Theme::kInfoAccent);
    std::string visible=g.text+"_";
    while(Layout::MeasureTextW(Layout::BodyFont(),Scale::Get().bodyPx,visible.c_str())>490 && visible.size()>1) {
        size_t next=1;while(next<visible.size() && (static_cast<unsigned char>(visible[next])&0xC0)==0x80) ++next;
        visible.erase(0,next);
    }
    FitText(dl,73,134,490,visible);
    Text(dl,73,164,"TYPE OR USE ARROWS + CONFIRM.",Theme::kTextInactive);
    for(int i=0;i<count;++i) {
        const float x=73+(i%columns)*49.0f,y=198+(i/columns)*32.0f;
        const char* actions[]={"DEL","aA","OK","ESC"};
        std::string label=i<chars?std::string(1,alphabet[i]):actions[i-chars];
        if(i<chars && alphabet[i]==' ') label="_";
        Layout::DrawButton(dl,x,y,45,28,label.c_str(),g.textFocus==i);
        const bool clicked=active && Input::MouseClickedIn(x,y,45,28);
        if(clicked) g.textFocus=i;
        if(!clicked && !(active && !textKeys && Input::Activate() && g.textFocus==i)) continue;
        if(i<chars) {if(g.text.size()<maximum) g.text.push_back(alphabet[i]);}
        else if(i==chars) {if(!g.text.empty()) g.text.pop_back();}
        else if(i==chars+1) g.lowerCase=!g.lowerCase;
        else if(i==chars+2) {CommitText();return;}
        else {g.textKind=State::TextKind::None;Neutral();return;}
        break;
    }
    FitText(dl,73,380,490,g.status,Theme::kTextInactive);
}
}

void Configure(ContextProvider contextProvider,UseSlot useSlot,ApplyColors applyColors,RestorePreview restorePreview) {
    g_contextProvider=contextProvider;g_useSlot=useSlot;g_applyColors=applyColors;g_restorePreview=restorePreview;
}
void SetDevice(IDirect3DDevice9* device) {
    if(g_device==device) return;
    ReleaseTextures();g_device=device;
}
void ReleaseTextures() {g_originalTexture.Release();g_workingTexture.Release();}
void RequestSuspend() noexcept {g_suspendRequested.store(true,std::memory_order_release);}
void Suspend() {
    if(g.open && g_restorePreview) g_restorePreview(false);
    g.open=false;g_open.store(false,std::memory_order_release);
    g.document.EndGesture();g.dragging=false;g.sliderRow=-1;g.panning=false;g.panMode=false;g.closeRequested=false;g.focusAbove=false;g.confirm={};g.textKind=State::TextKind::None;
    Screens::ClosePopup();
    Neutral();
}
void Reset() {Suspend();ReleaseTextures();g=State{};g_device=nullptr;}
bool IsOpen() {return g_open.load(std::memory_order_acquire) && !g_suspendRequested.load(std::memory_order_acquire);}
bool GetLivePreview(int& player,std::string& resource,Model::Palette& colors) {
    if(!IsOpen() || !g.loaded || !g.live) return false;
    player=g.player;resource=g.context.resourceName;
    colors=g.compare==1?g.document.Original():g.document.Colors();
    return true;
}
void SetLivePreviewStatus(const std::string& status) {g.liveStatus=status;}
void Open(int player) {
    if(g_suspendRequested.exchange(false,std::memory_order_acq_rel)) Suspend();
    g.open=true;g_open.store(true,std::memory_order_release);
    Screens::ClosePopup();Screens::ResetSubmenus();Screens::ResetTextEditor();
    player=player==0?g.player:player==2?2:1;
    Context current;std::string error;
    if(g.loaded && g_contextProvider && g_contextProvider(player,current,error) && player==g.player && current.resourceName==g.context.resourceName) {
        g.context=std::move(current);g.shared={};g.assetWarning.clear();
        if(!Model::LoadSharedBattlePalette(g.context.gameDirectory,g.context.stageId,g.shared,error)) g.assetWarning=error;
        Neutral();return;
    }
    GuardDraft([player]{LoadPlayer(player);});
    Neutral();
}
void Close() {Suspend();}
bool BlocksNavigation() {return IsOpen() && (Modal() || Screens::IsPopupActive());}
bool ConsumeFocusAboveRequest() {const bool result=g.focusAbove;g.focusAbove=false;return result;}
bool ConsumeCloseRequest() {const bool result=g.closeRequested;g.closeRequested=false;return result;}
void Render(ImDrawList* dl,float contentY,bool inputEnabled) {
    if(g_suspendRequested.exchange(false,std::memory_order_acq_rel)) Suspend();
    if(!g.open || !ImGui::GetCurrentContext()) return;
    if(!dl) dl=ImGui::GetForegroundDrawList();
    if(g.loaded && g.language!=I18n::GetLanguage()) {Screens::ClosePopup();BuildMoveChoices();g.status.clear();}
    const auto& metrics=Scale::Get();
    const bool popupWasOpen=Screens::IsPopupActive();
    const bool active=inputEnabled && !Input::IsSuspended() && Input::IsGameWindowActive();
    const bool input=active && !Modal() && !popupWasOpen;
    g.previewWheel=false;g.helpControl=-1;
    const auto mouse=Input::GetMouse();
    if(!mouse.valid) g.mouseHelp=false;
    else {
        const float dx=mouse.x-g.helpMouseX,dy=mouse.y-g.helpMouseY;
        if(dx*dx+dy*dy>1) g.mouseHelp=true;
        g.helpMouseX=mouse.x;g.helpMouseY=mouse.y;
    }
    if(Input::NavUp() || Input::NavDown() || Input::NavLeft() || Input::NavRight() || Input::Activate() || Input::Back()) g.mouseHelp=false;
    dl->AddRectFilled(ImVec2(20,contentY-2),ImVec2(620,420),Theme::kStrip | IM_COL32_A_MASK);
    dl->AddRectFilled(ImVec2(20,423),ImVec2(620,480),Theme::kStrip | IM_COL32_A_MASK);
    if(!g.loaded) {
        FitText(dl,28,contentY,584,g.status.empty()?I18n::Tr("Choose a character in Practice to open its palettes."):g.status);
        if(input && Input::Back()) {g.closeRequested=true;Neutral();}
        else if(input && Input::NavUp()) g.focusAbove=true;
    } else {
        if(input && Input::SwitchPlayer()) SwitchPlayer();
        SyncColor();AdvancePreview();
        DrawEditor(dl,contentY,input && !Modal());
        Screens::ScreenLayout popup{0,metrics.panelPadX,Theme::kPanelW-2*metrics.panelPadX,contentY,420,0,0,active && !Modal()};
        if(popupWasOpen && popup.inputEnabled) Screens::HandleListInput(popup,nullptr,0,g.popupFocus,g.popupScroll);
        if(Screens::IsPopupActive()) Screens::TickPopupIfOpen(dl,popup);
        const std::string context="P"+std::to_string(g.player)+" / "+I18n::Tr(g.context.displayName.c_str())+(g.document.Dirty()?" *  ":"  ")+(!g.status.empty() && DWORD(GetTickCount()-g.statusTick)<5000?g.status:I18n::Tr(g.liveStatus.c_str()));
        FitText(dl,28,425,584,context,Theme::kTextInactive);
        const char* help=ControlHelp(g.helpControl>=0?g.helpControl:g.editorFocus);
        if(Screens::IsPopupActive() && g.editorFocus==28) help=Screens::CurrentPopupHelpText();
        else if(Modal() && !g.status.empty()) help=g.status.c_str();
        else if(g.panMode) help=ControlHelp(15);
        dl->PushClipRect(ImVec2(28,443),ImVec2(612,480),true);
        dl->AddText(Layout::BodyFont(),metrics.bodyPx,ImVec2(28,444),Theme::kInfoAccent,I18n::Tr(help),nullptr,584);
        dl->PopClipRect();
    }
    if(g.confirm) DrawConfirmation(dl,active);
    else if(g.textKind!=State::TextKind::None) DrawTextEditor(dl,active);
}
}
