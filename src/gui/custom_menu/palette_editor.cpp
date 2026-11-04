#include "gui/custom_menu/palette_editor.h"
#include "game/palette_editor.h"
#include "core/constants.h"
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
    enum class View { Editor, Files } view = View::Editor;
    int player = 1, focus[2]{}, editorFocus = 0, color = 0, ramp = 0, target = 1, space = 0;
    int viewOpenedFrame = -1;
    int category = 1, moveChoice = 0;
    bool closeRequested = false, focusAbove = false, panMode = false, classesKnown = false, keyboardGesture = false;
    int keyboardChannel = -1;
    int components[3]{}, stock = 0, slot = 0, libraryIndex = 0;
    int animation = 0, frame = 0, zoom = 100, panX = 0, panY = 0, compare = 0;
    bool playing = true, marking = false, pin = false, trash = false, dragging = false;
    bool panning = false, live = true, alternate = false, previewWheel = false, autoFit = true;
    int sliderRow = -1;
    float sliderLeft = 0, sliderWidth = 0;
    float panMouseX = 0, panMouseY = 0;
    double elapsed = 0;
    Context context;
    Model::CharacterAssets assets;
    Model::Palette256 shared{};
    Model::Document document;
    std::unique_ptr<Model::Library> library;
    std::vector<Model::LibraryEntry> entries;
    std::vector<std::string> moveLabels, entryLabels;
    std::vector<const char*> moveOptions, entryOptions;
    std::vector<int> moveCategories, moveIndices;
    std::optional<Model::LibraryEntry> source;
    Screens::ScrollState scroll[2];
    std::string status, assetWarning, liveStatus, nativeName;
    std::function<void()> confirm;
    std::string confirmTitle, confirmText, confirmVerb;
    int confirmFocus = 0, modalOpenedFrame = -1;
    enum class TextKind { None, Name, SaveAs, Hex } textKind = TextKind::None;
    std::string text;
    int textFocus = 0;
    bool lowerCase = false;
};
State g;

bool Modal() { return bool(g.confirm) || g.textKind != State::TextKind::None; }
void Neutral() { g.document.EndGesture();g.keyboardGesture=false;g.sliderRow=-1;g.dragging=false; if(ImGui::GetCurrentContext()) Input::ResetEdges(); Screens::ResetMouseTracking(); }
void Status(const std::string& text) { g.status = text; }
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
    g.components[0] = g.space ? int(std::lround(hsv.h)) : c.r;
    g.components[1] = g.space ? int(std::lround(hsv.s)) : c.g;
    g.components[2] = g.space ? int(std::lround(hsv.v)) : c.b;
    g.pin = g.document.Pinned(g.color);
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
    if (g.space) {
        const Model::Hsv hsv{float(g.components[0]),float(g.components[1]),float(g.components[2])};
        if (g.target) g.document.SetRampHsv(unsigned(g.ramp),hsv);
        else g.document.SetHsv(unsigned(g.color),hsv);
    } else {
        const Model::Color c{uint8_t(g.components[0]),uint8_t(g.components[1]),uint8_t(g.components[2])};
        if (g.target) g.document.SetRampColor(unsigned(g.ramp),c);
        else g.document.SetColor(unsigned(g.color),c);
    }
}
void ScanLibrary() {
    if (!g.library) return;
    std::string error;
    g.entries = g.library->Scan(g.context.resourceName,g.trash,error);
    g.libraryIndex = (std::clamp)(g.libraryIndex,0,(std::max)(0,int(g.entries.size())-1));
    g.entryLabels.clear();g.entryOptions.clear();
    for(const auto& entry:g.entries) g.entryLabels.push_back((entry.error.empty()?"":"[invalid] ")+entry.name);
    for(const auto& label:g.entryLabels) g.entryOptions.push_back(label.c_str());
    if (!error.empty()) Status(error);
    else Status(g.trash ? "Recoverable trash rescanned." : "Palette library rescanned.");
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
    g.category=(std::clamp)(g.category,0,6);
    g.moveOptions.clear();g.moveIndices.clear();
    for(int i=0;i<int(g.assets.animations.size());++i) {
        if(g.assets.animations[i].frames.empty() || (g.category && g.moveCategories[i]!=g.category)) continue;
        g.moveIndices.push_back(i);g.moveOptions.push_back(g.moveLabels[i].c_str());
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
    g.moveLabels.clear();g.moveCategories.clear();
    bool known=false;
    for(const auto& animation:g.assets.animations) {
        const auto type=Mission::MoveData::GetClass(g.context.characterId,animation.id);
        g.moveCategories.push_back(Category(type));known=known || type!=Mission::MoveData::Cls::Unknown;
        const char* label=Mission::MoveNames::Lookup(g.context.resourceName.c_str(),short(animation.id));
        if(!label || !*label) label=Mission::EntityNames::Lookup(g.context.resourceName.c_str(),animation.id);
        if(!label || !*label) label=CommonStateLabel(animation.id);
        if(!label || !*label) label=Mission::MoveData::ClassName(type);
        g.moveLabels.push_back(std::to_string(animation.id)+" - "+(label && *label?label:"Unclassified"));
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
    g.stock=g.slot=(std::clamp)(g.context.stockSlot,0,5);
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
        g.assetWarning="Shared effect colors unavailable: " + error;
    g.library=std::make_unique<Model::Library>(g.context.libraryDirectory);
    g.trash=false;
    ScanLibrary();
    g.loaded=true;
    ResetPreview();
    for (size_t i=0;i<g.assets.animations.size();++i)
        if (g.assets.animations[i].id==g.context.moveId) g.animation=int(i);
    BuildMoveChoices();
    g.target=1;
    SyncColor();
    Status(!nativeError.empty()?"Custom PAL could not load; live preview disabled: "+nativeError:
        (!g.nativeName.empty()?"Loaded "+g.nativeName+".":"Stock "+std::to_string(g.stock+1)+" loaded.")+" Preview does not advance the match.");
    Neutral();
}
void SwitchPlayer() { const int player=3-g.player; GuardDraft([player]{LoadPlayer(player);}); }
void LoadStock() {
    const int slot=g.stock;
    GuardDraft([slot]{
        g.document.LoadStock(g.context.resourceName,unsigned(slot),g.assets.stockPalettes[slot]);
        g.source.reset();g.nativeName.clear(); g.target=1; SyncColor();
        Status("Stock " + std::to_string(slot+1) + " loaded."); Neutral();
    });
}
bool NoEntries() { return g.entries.empty(); }
bool NoRamps() { return g.document.Ramps().empty(); }
bool NoUndo() { return g.document.UndoCount()==0; }
bool NoSource() { return !g.source || g.source->deleted; }
bool NoAnimations() { return g.assets.animations.empty() || g.moveIndices.empty(); }
void LoadCustom() {
    if (NoEntries()) return;
    const auto entry=g.entries[g.libraryIndex];
    GuardDraft([entry]{
        Model::SavedPalette saved;
        std::string error;
        if (!g.library->Read(entry,saved,error) || saved.stockSlot>=6 ||
            !g.document.LoadSaved(saved,g.assets.stockPalettes[saved.stockSlot],error)) {
            Status(error.empty()?"Invalid stock slot.":error); return;
        }
        g.source=entry;g.nativeName.clear(); g.stock=int(saved.stockSlot); g.target=1; SyncColor();
        if(entry.deleted) {g.trash=false;g.view=State::View::Editor;ScanLibrary();g.editorFocus=-3;g.focus[1]=0;g.scroll[1]={};}
        Status("Loaded " + saved.name + (entry.deleted ? " from trash. Use Save As to keep a new copy." : ".")); Neutral();
    });
}
void StartText(State::TextKind kind) {
    g.textKind=kind;
    g.modalOpenedFrame=ImGui::GetCurrentContext()?ImGui::GetFrameCount():-1;
    g.text=kind==State::TextKind::Hex?Model::FormatHex(g.target ? g.document.RampColor(unsigned(g.ramp)) : g.document.Colors()[g.color]):g.document.State().name;
    if(kind==State::TextKind::SaveAs) g.text.clear();
    g.textFocus=0;
    if(ImGui::GetCurrentContext()) ImGui::GetIO().InputQueueCharacters.resize(0);
    Neutral();
}
void EditName() { StartText(State::TextKind::Name); }
void EditHex() { StartText(State::TextKind::Hex); }
bool SaveNew(const std::string& name) {
    Model::LibraryEntry saved;
    std::string error;
    auto palette=g.document.State();palette.name=name;
    if (!g.library || !g.library->SaveNew(palette,saved,error)) {
        Status(error.empty()?"Palette library is unavailable.":error);return false;
    }
    g.document.SetName(name);g.source=saved;g.document.MarkSaved();
    g.trash=false;ScanLibrary();Status("Saved " + saved.name + ".");
    return true;
}
void Replace() {
    if (NoSource()) return;
    const auto entry=*g.source;
    Confirm("REPLACE SAVED PALETTE", "Replace the loaded library entry?", "REPLACE", [entry]{
        Model::LibraryEntry saved; std::string error;
        if (!g.library->Replace(entry,g.document.State(),saved,error)) { Status(error); return; }
        g.source=saved; g.document.MarkSaved(); ScanLibrary(); Status("Replaced " + saved.name + ".");
    });
}
void SaveAs() {StartText(State::TextKind::SaveAs);}
void Save() {if(NoSource()) SaveAs();else Replace();}
void ShowView(State::View view) {
    if(g.view==view) return;
    g.view=view;g.viewOpenedFrame=ImGui::GetFrameCount();g.editorFocus=0;
    if(g.trash) {g.trash=false;ScanLibrary();}
    g.focus[1]=0;g.scroll[1]={};Neutral();
}
void ToggleTrash() {
    g.trash=!g.trash;ScanLibrary();g.focus[1]=0;g.scroll[1]={};Neutral();
}
void DeleteOrRestore() {
    if (NoEntries()) return;
    const auto entry=g.entries[g.libraryIndex];
    Confirm(entry.deleted?"RESTORE PALETTE":"MOVE PALETTE TO TRASH",entry.name,
        entry.deleted?"RESTORE":"TRASH",[entry]{
        Model::LibraryEntry result; std::string error;
        const bool ok=entry.deleted?g.library->Restore(entry,result,error):g.library->Delete(entry,result,error);
        if (!ok) { Status(error); return; }
        if (g.source && g.source->path==entry.path) g.source=result;
        ScanLibrary(); Status(entry.deleted?"Palette restored.":"Palette moved to recoverable trash.");
    });
}
void ImportNative() {
    const auto path=NativePath(g.slot,true);
    GuardDraft([path]{
        Model::Palette colors; std::string error;
        if (!Model::ReadNativePalette(path,colors,error)) { Status(error); return; }
        g.document.LoadStock(g.context.resourceName,unsigned(g.stock),g.assets.stockPalettes[g.stock]);
        g.document.Import(colors); g.source.reset();g.nativeName=path.filename().u8string(); g.target=1; SyncColor();
        Status("Imported " + path.filename().u8string() + ". Save to keep pins and ramp edits.");
    });
}
void ImportFile() {
    wchar_t filename[MAX_PATH]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize=sizeof(dialog);
    dialog.hwndOwner=GetForegroundWindow();
    dialog.lpstrFilter=L"EFZ palette (*.pal)\0*.pal\0\0";
    dialog.lpstrFile=filename;
    dialog.nMaxFile=MAX_PATH;
    dialog.lpstrTitle=L"Import EFZ native palette";
    dialog.Flags=OFN_EXPLORER|OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    const bool selected=GetOpenFileNameW(&dialog)!=FALSE;
    Neutral();
    if(!selected) {
        if(CommDlgExtendedError()) Status("The palette file chooser could not open.");
        return;
    }
    const std::filesystem::path path(filename);
    GuardDraft([path]{
        Model::Palette colors;std::string error;
        if(!Model::ReadNativePalette(path,colors,error)) {Status(error);return;}
        g.document.LoadStock(g.context.resourceName,unsigned(g.stock),g.assets.stockPalettes[g.stock]);
        g.document.Import(colors);g.source.reset();g.nativeName=path.filename().u8string();g.target=1;SyncColor();
        Status("Imported " + path.filename().u8string() + ". Save the draft to keep it.");
    });
}
void Export(bool use) {
    const int slot=g.slot;
    const auto path=NativePath(slot,true);
    std::error_code ec;
    const bool exists=std::filesystem::exists(path,ec);
    auto action=[path,slot,use,exists]{
        std::string error;
        if (!Model::ExportNativePalette(path,g.document.Colors(),exists,error)) { Status(error); return; }
        if (use && (!g_useSlot || !g_useSlot(g.player,g.context.resourceName,slot,error))) {
            Status("Exported; slot apply failed: " + error); return;
        }
        Status(use?"Exported; live application queued for the current match.":"Exported native PAL. Save library entry to retain pins and ramp edits.");
    };
    if (exists || use) Confirm(use?"EXPORT AND APPLY COLORS":"OVERWRITE NATIVE SLOT",
        "Slot " + std::to_string(slot+1) + (exists?" already exists; it will be replaced.":" will be written."),
        use?"EXPORT + APPLY":"OVERWRITE",std::move(action));
    else action();
}
void ExportNative() { Export(false); }
void ExportAndUse() { Export(true); }
void CopyFile() {
    if (NoSource()) return;
    const auto path=g.source->path.wstring();
    const SIZE_T bytes=sizeof(DROPFILES)+(path.size()+2)*sizeof(wchar_t);
    HGLOBAL handle=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,bytes);
    if (!handle) { Status("Could not allocate clipboard data."); return; }
    auto* drop=static_cast<DROPFILES*>(GlobalLock(handle));
    if (!drop) { GlobalFree(handle); Status("Could not prepare clipboard data."); return; }
    drop->pFiles=sizeof(DROPFILES); drop->fWide=TRUE;
    memcpy(reinterpret_cast<char*>(drop)+sizeof(DROPFILES),path.c_str(),(path.size()+1)*sizeof(wchar_t));
    GlobalUnlock(handle);
    if (!OpenClipboard(nullptr)) { GlobalFree(handle); Status("Clipboard is busy."); return; }
    const bool ok=EmptyClipboard() && SetClipboardData(CF_HDROP,handle)!=nullptr;
    CloseClipboard();
    if (!ok) GlobalFree(handle);
    Status(ok?"Saved palette file copied to clipboard.":"Could not copy palette file.");
}
void Pin() { g.document.SetPinned(unsigned(g.color),g.pin); }
void ResetSelected() {
    if (g.target) g.document.ResetRamp(unsigned(g.ramp)); else g.document.ResetEntry(unsigned(g.color));
    SyncColor();
}
void Undo() { g.document.Undo(); SyncColor(); }
void ResetAll() { Confirm("RESET COLORS", "Reset all colors and pins to the original stock palette?", "RESET", []{g.document.ResetAll();SyncColor();}); }
void Revert() { Confirm("REVERT DRAFT", "Restore the last loaded or saved state?", "REVERT", []{g.document.Revert();SyncColor();}); }
const char* HexValue() { static std::string text; text=Model::FormatHex(g.target ? g.document.RampColor(unsigned(g.ramp)) : g.document.Colors()[g.color]); return text.c_str(); }
const char* NameValue() { return g.document.State().name.c_str(); }
const char* SourceValue() { return g.source ? g.source->name.c_str() : "Stock / unsaved"; }
const char* EntryValue(const Screens::Row&) { return NoEntries()?"(empty)":g.entries[g.libraryIndex].name.c_str(); }
const char* UndoValue() { static char text[24]; std::snprintf(text,sizeof(text),"%zu / 16",g.document.UndoCount()); return text; }
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
std::vector<Screens::Row> FilesRows() {
    using namespace Screens;
    std::vector<Row> rows;
    static const char* slots[]={"1","2","3","4","5","6"};
    if(g.trash) {
        rows.push_back(Action("Back to files",ToggleTrash));
        rows.push_back(DropdownRow("Deleted palette",&g.libraryIndex,g.entryOptions.data(),int(g.entryOptions.size()),nullptr,NoEntries));
        rows.back().valueFormatter=EntryValue;
        rows.push_back(Action("Restore selected",DeleteOrRestore,nullptr,NoEntries));
        rows.back().helpText="Return the selected palette to the library. Your current draft is kept.";
        rows.push_back(Action("Load selected",LoadCustom,nullptr,NoEntries));
        rows.back().helpText="Load this deleted palette, then use Save As to recover it under a different name.";
        rows.push_back(Action("Rescan trash",ScanLibrary));
        return rows;
    }
    rows.push_back(DropdownRow("Stock palette",&g.stock,slots,6));
    rows.push_back(Action("Load stock",LoadStock));
    rows.push_back(Header("FILE IMPORT / EXPORT"));
    rows.push_back(DropdownRow("Native PAL slot",&g.slot,slots,6));
    rows.push_back(Action("Import native PAL",ImportNative));
    rows.back().helpText="Read the selected character's native slot file. PAL files contain exactly 40 RGB colors.";
    rows.push_back(Action("Export native PAL",ExportNative));
    rows.back().helpText="Write a native slot; replacing an existing file requires confirmation. Does not reload the match.";
    rows.push_back(Action("Export + apply",ExportAndUse));
    rows.back().helpText="Write the native slot and apply its colors in the current match. No gameplay frames advance.";
    rows.push_back(Action("Import PAL file...",ImportFile));
    rows.back().helpText="Choose a native PAL file. The Windows file chooser accepts keyboard or pointer input.";
    rows.push_back(Action("Copy saved file",CopyFile,nullptr,NoSource));
    rows.back().helpText="Copy the loaded library file to the Windows clipboard for sharing in Explorer.";
    rows.push_back(Header("PALETTE FILES"));
    rows.push_back(DropdownRow("Saved palette",&g.libraryIndex,g.entryOptions.data(),int(g.entryOptions.size()),nullptr,NoEntries));
    rows.back().valueFormatter=EntryValue;
    rows.push_back(Action("Revert draft",Revert));
    rows.push_back(Action("Reset all colors",ResetAll));
    rows.push_back(Action("Move selected to trash",DeleteOrRestore,nullptr,NoEntries));
    rows.back().helpText="Remove the selected palette from the library. Recover deleted palettes can restore it.";
    rows.push_back(Action("Recover deleted palettes",ToggleTrash));
    rows.push_back(Action("Rescan library",ScanLibrary));
    return rows;
}
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
        const float wheel=Input::MouseWheel();
        if (wheel) {g.zoom=(std::clamp)(g.zoom+(wheel>0?25:-25),25,400);g.autoFit=false;g.previewWheel=true;}
        if (Input::MouseRightClickedIn(x,y,width,height)) {g.panning=true;g.panMouseX=mouse.x;g.panMouseY=mouse.y;}
        if (Input::MouseClickedIn(x,y,width,height)) {
            const int px=int(std::floor((mouse.x-top.x)/scale)),py=int(std::floor((mouse.y-top.y)/scale));
            if (px>=0 && py>=0 && px<sprite.width && py<sprite.height) {
                const int index=sprite.indices[size_t(py)*sprite.width+px];
                if (index==g.assets.transparentKey) Status("Transparent pixel: no visible color to pick.");
                else if (index>=40) Status("Shared effect palette: read-only; not stored in the character PAL.");
                else {g.color=index;SelectColor();Status("Picked native color " + std::to_string(index) + ".");}
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
        Text(dl,sx+5,sy+(height-Scale::Get().bodyPx)/2,std::to_string(i),color);
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
    ImFont* font=Layout::BodyFont();const float px=font?font->FontSize:Scale::Get().bodyPx;
    if(Layout::MeasureTextW(font,px,text.c_str())<=width) return text;
    std::string fitted=text;
    while(!fitted.empty() && Layout::MeasureTextW(font,px,(fitted+"...").c_str())>width) {
        size_t end=fitted.size();
        do {--end;} while(end && (static_cast<unsigned char>(fitted[end])&0xC0)==0x80);
        fitted.resize(end);
    }
    return fitted+"...";
}
bool EditorButton(ImDrawList* dl,int focus,float x,float y,float width,float height,
                  const std::string& label,bool input,bool disabled=false) {
    const bool clicked=input && !disabled && Input::MouseClickedIn(x,y,width,height);
    if(clicked) g.editorFocus=focus;
    if(clicked || (input && g.editorFocus==focus && Input::Activate())) {g.document.EndGesture();g.keyboardGesture=false;g.sliderRow=-1;g.dragging=false;}
    dl->PushClipRect(ImVec2(x,y),ImVec2(x+width,y+height),true);
    const std::string fitted=ButtonLabel(label,width-16);
    dl->AddRectFilled(ImVec2(x,y),ImVec2(x+width,y+height),Theme::kStrip | IM_COL32_A_MASK);
    Layout::DrawButton(dl,x,y,width,height,fitted.c_str(),g.editorFocus==focus,disabled);
    dl->PopClipRect();
    return !disabled && (clicked || (input && g.editorFocus==focus && Input::Activate()));
}
void OpenEditorChoice(const Screens::Row& row) {
    Screens::OpenDropdownPopup(row);Neutral();
}
bool CategoryAvailable(const Screens::Row&,int category) {
    if(category==0) return true;
    for(int i=0;i<int(g.moveCategories.size());++i)
        if(g.moveCategories[i]==category && !g.assets.animations[i].frames.empty()) return true;
    return false;
}
void ChangeCategory(int direction) {
    for(int step=0;step<7;++step) {
        g.category=(g.category+direction+7)%7;
        if(CategoryAvailable(Screens::Row{},g.category)) {FilterMoves();return;}
    }
}
void CompactSlider(ImDrawList* dl,int component,float x,float y,float width,bool input) {
    const int focus=component+3,maximum=g.space?(component?100:359):255;
    const float height=27,left=x+31,trackWidth=width-87;
    const auto mouse=Input::GetMouse();
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
    Text(dl,x+10,textY,g.space?hsv[component]:rgb[component],Theme::kTextInactive);
    const float trackY=y+height/2;
    dl->AddRectFilled(ImVec2(left,trackY-2),ImVec2(left+trackWidth,trackY+2),Theme::kSliderTrackOff);
    const float knob=left+trackWidth*g.components[component]/float(maximum);
    dl->AddRectFilled(ImVec2(left,trackY-2),ImVec2(knob,trackY+2),Theme::kSelectedLine);
    dl->AddCircleFilled(ImVec2(knob,trackY),3,Theme::kTextActive);
    Text(dl,x+width-42,textY,std::to_string(g.components[component]));
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
        if(input && Input::MouseClickedIn(sx,sy,repWidth,geometry.repHeight)) {g.ramp=i;SelectRamp();g.editorFocus=0;}
    }
    const auto& ramp=ramps[g.ramp];
    const float labelY=y+geometry.repRows*geometry.repHeight+3;
    Text(dl,x,labelY,"SHADES "+std::to_string(ramp.first)+" - "+std::to_string(ramp.last),Theme::kTextInactive);
    const int shadeRows=(int(ramp.last-ramp.first)+8)/8;
    const float shadeY=y+geometry.shadeY,shadeHeight=(std::min)(25.0f,(height-geometry.shadeY)/shadeRows),shadeWidth=width/8;
    for(unsigned i=ramp.first;i<=ramp.last;++i) {
        const unsigned slot=i-ramp.first;
        const float sx=x+(slot%8)*shadeWidth,sy=shadeY+(slot/8)*shadeHeight;
        const auto color=g.document.Colors()[i];
        dl->AddRectFilled(ImVec2(sx+2,sy+2),ImVec2(sx+shadeWidth-2,sy+shadeHeight-2),IM_COL32(color.r,color.g,color.b,255));
        if(g.document.Pinned(i)) dl->AddCircleFilled(ImVec2(sx+shadeWidth-6,sy+6),2,Theme::kInfoAccent);
        if(input && Input::MouseClickedIn(sx,sy,shadeWidth,shadeHeight)) {g.color=int(i);SelectColor();g.editorFocus=0;}
    }
    dl->PopClipRect();
}
void DrawEditor(ImDrawList* dl,float top,bool input) {
    constexpr float left=28,right=328,width=284;
    const float compact=(std::max)(0.0f,top+305-416);
    const float previewCompact=(std::max)(0.0f,top+335-416);
    const bool alternate=Model::HasAlternateAnimations(g.context.resourceName);
    g.editorFocus=(std::clamp)(g.editorFocus,-5,alternate?22:21);
    if(g.keyboardGesture && (!input || (!Input::NavLeftHeld() && !Input::NavRightHeld()) || g.editorFocus!=g.keyboardChannel)) {
        g.document.EndGesture();g.keyboardGesture=false;
    }
    if(!ImGui::IsMouseDown(ImGuiMouseButton_Left) && g.sliderRow>=0) {g.document.EndGesture();g.dragging=false;g.sliderRow=-1;}
    if(input && g.panMode) {
        if(Input::NavLeft()) g.panX=(std::max)(-2048,g.panX-4);if(Input::NavRight()) g.panX=(std::min)(2048,g.panX+4);
        if(Input::NavUp()) g.panY=(std::max)(-2048,g.panY-4);if(Input::NavDown()) g.panY=(std::min)(2048,g.panY+4);
        if(Input::Activate() || Input::Back()) {g.panMode=false;Neutral();}
    } else if(input && g.sliderRow<0) {
        const int count=alternate?23:22;
        if(Input::NavUp()) {
            if(g.editorFocus==0) {g.editorFocus=-5;Neutral();input=false;}
            else --g.editorFocus;
        }
        if(Input::NavDown()) g.editorFocus=(g.editorFocus+1)%count;
        if(g.keyboardGesture && g.editorFocus!=g.keyboardChannel) {g.document.EndGesture();g.keyboardGesture=false;}
        const int direction=Input::NavRight()?1:Input::NavLeft()?-1:0;
        if(direction) switch(g.editorFocus) {
        case 0:
            if(g.target && !NoRamps()) {g.ramp=(g.ramp+direction+int(g.document.Ramps().size()))%int(g.document.Ramps().size());SelectRamp();}
            else {g.color=(g.color+direction+40)%40;SelectColor();}break;
        case 1:g.space=1-g.space;SyncColor();break;
        case 3:case 4:case 5: {
            if(!g.keyboardGesture || g.keyboardChannel!=g.editorFocus) {g.document.EndGesture();g.document.BeginGesture();g.keyboardGesture=true;g.keyboardChannel=g.editorFocus;}
            const int component=g.editorFocus-3,maximum=g.space?(component?100:359):255;
            g.components[component]=(std::clamp)(g.components[component]+direction*(ImGui::GetIO().KeyShift?10:1),0,maximum);ApplyComponents();break;
        }
        case 6:g.pin=direction>0;Pin();break;
        case 9:ChangeCategory(direction);break;
        case 10:if(!g.moveIndices.empty()){g.moveChoice=(g.moveChoice+direction+int(g.moveIndices.size()))%int(g.moveIndices.size());MoveChoiceChanged();}break;
        case 11:case 12:case 13:Step(direction);break;
        case 14:g.zoom=(std::clamp)(g.zoom+direction*25,25,400);ZoomChanged();break;
        case 17:g.compare=(g.compare+direction+3)%3;break;
        case 18:g.marking=direction>0;break;
        case 19:g.live=direction>0;break;
        case 22:g.alternate=!g.alternate;VariantChanged();break;
        default:break;
        }
        if(Input::Back()) {g.closeRequested=true;Neutral();input=false;}
    }
    const bool controls=input && !g.panMode;
    const std::string selection=g.target?"RAMP "+std::to_string(g.ramp)+"  /  SHADE":"SHADE "+std::to_string(g.color)+"  /  RAMP";
    if(EditorButton(dl,0,left,top,width,24,selection,controls)) {g.target=g.target?0:1;SyncColor();}
    if(g.target) DrawRampGrid(dl,left,top+28,width,124-compact,controls);
    else DrawSwatches(dl,left,top+28,width,controls,124-compact);
    if(EditorButton(dl,1,left,top+156-compact,132,24,g.space?"HSV > RGB":"RGB > HSV",controls)) {g.space=1-g.space;SyncColor();}
    if(EditorButton(dl,2,left+140,top+156-compact,144,24,HexValue(),controls)) EditHex();
    for(int component=0;component<3;++component) CompactSlider(dl,component,left,top+187-compact+component*29,width,controls);
    if(EditorButton(dl,6,left,top+280-compact,80,25,g.pin?"UNPIN":"PIN",controls)) {g.pin=!g.pin;Pin();}
    if(EditorButton(dl,7,left+87,top+280-compact,90,25,"RESET",controls)) ResetSelected();
    if(EditorButton(dl,8,left+184,top+280-compact,100,25,"UNDO "+std::to_string(g.document.UndoCount()),controls,NoUndo())) Undo();
    const std::string category=g.classesKnown?kMoveCategories[g.category]:"ALL / NO CLASS DATA";
    if(EditorButton(dl,9,right,top,width,24,category,controls)) {
        auto row=Screens::DropdownRow("Category",&g.category,kMoveCategories,7,FilterMoves);
        OpenEditorChoice(row);
    }
    const std::string move=g.moveOptions.empty()?"No drawable animations":g.moveOptions[g.moveChoice];
    if(EditorButton(dl,10,right,top+28,width,24,move,controls,g.moveOptions.empty()))
        OpenEditorChoice(Screens::DropdownRow("Move",&g.moveChoice,g.moveOptions.data(),int(g.moveOptions.size()),MoveChoiceChanged));
    DrawPreview(dl,right,top+56,width,158-previewCompact,controls);
    if(g.compare==2) {
        FitText(dl,right+7,top+194-previewCompact,width/2-12,"ORIGINAL",Theme::kTextInactive);
        FitText(dl,right+width/2+7,top+194-previewCompact,width/2-12,"WORKING",Theme::kTextInactive);
    }
    if(alternate && EditorButton(dl,22,right+width-82,top+61,76,21,g.alternate?"ALT":"STD",controls)) {g.alternate=!g.alternate;VariantChanged();}
    const auto* animation=Animation();
    const int frames=animation?int(animation->frames.size()):0;
    if(EditorButton(dl,11,right,top+220-previewCompact,42,25,"<",controls,frames==0)) PrevFrame();
    if(EditorButton(dl,12,right+49,top+220-previewCompact,186,25,(frames?(g.playing?"PAUSE  ":"PLAY  ")+std::to_string(g.frame+1)+" / "+std::to_string(frames):"NO FRAMES"),controls,frames==0)) g.playing=!g.playing;
    if(EditorButton(dl,13,right+242,top+220-previewCompact,42,25,">",controls,frames==0)) NextFrame();
    if(EditorButton(dl,14,right,top+250-previewCompact,102,24,std::to_string(g.zoom)+"%",controls)) {g.zoom=g.zoom>=400?25:(std::min)(400,g.zoom+25);ZoomChanged();}
    if(EditorButton(dl,15,right+109,top+250-previewCompact,78,24,g.panMode?"PAN*":"PAN",input)) {g.panMode=!g.panMode;Neutral();}
    if(EditorButton(dl,16,right+194,top+250-previewCompact,90,24,"CENTER",controls)) Recenter();
    static const char* compares[]={"WORKING","ORIGINAL","SPLIT"};
    if(EditorButton(dl,17,right,top+280-previewCompact,104,25,compares[g.compare],controls)) g.compare=(g.compare+1)%3;
    if(EditorButton(dl,18,right+111,top+280-previewCompact,67,25,"MARK",controls)) g.marking=!g.marking;
    if(EditorButton(dl,19,right+185,top+280-previewCompact,99,25,"PREVIEW",controls)) g.live=!g.live;
    dl->AddCircleFilled(ImVec2(right+172,top+285-previewCompact),2,g.marking?Theme::kInfoAccent:Theme::kTextDisabled);
    dl->AddCircleFilled(ImVec2(right+278,top+285-previewCompact),2,g.live?Theme::kInfoAccent:Theme::kTextDisabled);
    if(EditorButton(dl,20,right,top+310-previewCompact,137,25,"APPLY",controls,!g_applyColors)) ApplyToMatch();
    if(EditorButton(dl,21,right+147,top+310-previewCompact,137,25,"RESTORE",controls)) RestoreMatch();
}
void DrawConfirmation(ImDrawList* dl) {
    const bool active=Input::IsGameWindowActive() && ImGui::GetFrameCount()!=g.modalOpenedFrame;
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
    if(g.textKind==State::TextKind::Hex) {
        const bool ok=g.target?g.document.SetRampHex(unsigned(g.ramp),g.text):g.document.SetHex(unsigned(g.color),g.text);
        Model::Color parsed;
        if(!ok && !Model::ParseHex(g.text,parsed)) {Status("Use 3 or 6 hexadecimal digits.");return;}
        SyncColor();
    } else if(g.textKind==State::TextKind::SaveAs) {
        if(!SaveNew(g.text)) return;
    } else g.document.SetName(g.text);
    g.textKind=State::TextKind::None;Neutral();
}
void DrawTextEditor(ImDrawList* dl) {
    const bool hex=g.textKind==State::TextKind::Hex;
    auto& io=ImGui::GetIO();
    const size_t maximum=hex?7:32;
    const bool active=Input::IsGameWindowActive() && ImGui::GetFrameCount()!=g.modalOpenedFrame;
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
    Text(dl,73,100,hex?"HEX COLOR (RGB / RRGGBB)":g.textKind==State::TextKind::SaveAs?"SAVE AS - NEW NAME (32 CHARACTERS)":"PALETTE NAME (32 CHARACTERS)",Theme::kInfoAccent);
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
void Render(ImDrawList* dl,float toolbarY,bool inputEnabled) {
    if(g_suspendRequested.exchange(false,std::memory_order_acq_rel)) Suspend();
    if(!g.open || !ImGui::GetCurrentContext()) return;
    if(!dl) dl=ImGui::GetForegroundDrawList();
    const auto& metrics=Scale::Get();
    const float body=metrics.bodyPx,contentY=toolbarY+30;
    const bool popupWasOpen=Screens::IsPopupActive();
    const bool pointerInput=Input::IsGameWindowActive() && !Modal() && !popupWasOpen;
    bool input=inputEnabled && pointerInput;
    g.previewWheel=false;
    dl->AddRectFilled(ImVec2(20,toolbarY-2),ImVec2(620,420),Theme::kStrip | IM_COL32_A_MASK);
    dl->AddRectFilled(ImVec2(20,423),ImVec2(620,480),Theme::kStrip | IM_COL32_A_MASK);
    if(input && g.editorFocus<0) {
        if(Input::NavLeft()) g.editorFocus=g.editorFocus==-5?-1:g.editorFocus-1;
        if(Input::NavRight()) g.editorFocus=g.editorFocus==-1?-5:g.editorFocus+1;
        if(Input::NavUp()) {g.focusAbove=true;Neutral();input=false;}
        else if(Input::NavDown()) {g.editorFocus=0;Neutral();input=false;}
        else if(Input::Back()) {
            if(g.view==State::View::Files) ShowView(State::View::Editor);
            else {g.closeRequested=true;Neutral();}
            input=false;
        }
    }
    const auto button=[&](int index,const std::string& label,bool disabled=false) {
        const float x=28+118.0f*index;
        const bool mouse=pointerInput && Input::MouseClickedIn(x,toolbarY,112,24);
        return EditorButton(dl,index-5,x,toolbarY,112,24,label,input || mouse,disabled);
    };
    if(button(0,"P"+std::to_string(g.player)+"  <>")) SwitchPlayer();
    if(button(1,"SAVE",!g.loaded)) Save();
    if(button(2,"SAVE AS",!g.loaded)) SaveAs();
    if(button(3,"LOAD",!g.loaded)) {
        ShowView(State::View::Editor);ScanLibrary();
        if(NoEntries()) Status("No saved palettes. Use Save As to create one.");
        else OpenEditorChoice(Screens::DropdownRow("Saved palette",&g.libraryIndex,g.entryOptions.data(),int(g.entryOptions.size()),LoadCustom));
    }
    if(button(4,g.view==State::View::Editor?"FILES":"EDITOR",!g.loaded))
        ShowView(g.view==State::View::Editor?State::View::Files:State::View::Editor);
    if(input && !Modal() && Input::SwitchPlayer()) SwitchPlayer();
    input=input && !Modal() && !Screens::IsPopupActive() && g.viewOpenedFrame!=ImGui::GetFrameCount();
    if(input && Input::MouseLeftEdge() && Input::GetMouse().y>=contentY && g.editorFocus<0) g.editorFocus=0;
    const bool contentInput=input && g.editorFocus>=0;
    if(!g.loaded) {
        FitText(dl,28,contentY,584,g.status.empty()?"Choose a character in Practice to open its palettes.":g.status);
        if(contentInput && Input::Back()) {g.closeRequested=true;Neutral();}
        else if(contentInput && Input::NavUp()) {g.editorFocus=-5;Neutral();}
    } else {
        SyncColor();if(g.view==State::View::Editor) AdvancePreview();
        const bool listInput=inputEnabled && Input::IsGameWindowActive() && !Modal() &&
            (popupWasOpen || !Screens::IsPopupActive()) && g.viewOpenedFrame!=ImGui::GetFrameCount();
        Screens::ScreenLayout list{0,metrics.panelPadX,Theme::kPanelW-2*metrics.panelPadX,contentY,420,0,0,listInput};
        if(g.view==State::View::Editor) {
            DrawEditor(dl,contentY,contentInput);
            if(popupWasOpen && list.inputEnabled) Screens::HandleListInput(list,nullptr,0,g.focus[0],g.scroll[0]);
        } else {
            const std::string source=g.source?g.source->name:!g.nativeName.empty()?g.nativeName:"Stock "+std::to_string(g.document.State().stockSlot+1);
            FitText(dl,28,contentY,584,g.trash?"RECOVER DELETED PALETTES":"FILES / "+source,Theme::kTextHeader);
            FitText(dl,28,contentY+body+7,584,g.trash?"Load a deleted palette to save it under another name.":"Native PAL import, export and recoverable palette files.",Theme::kTextInactive);
            list.contentTopY=contentY+2*body+18;
            auto rows=FilesRows();
            Screens::ClampFocus(rows.data(),int(rows.size()),g.focus[1]);
            if(list.inputEnabled && (g.editorFocus>=0 || popupWasOpen) &&
                Screens::HandleListInput(list,rows.data(),int(rows.size()),g.focus[1],g.scroll[1])) ShowView(State::View::Editor);
            Screens::RenderList(dl,list,nullptr,rows.data(),int(rows.size()),g.focus[1],g.scroll[1]);
            if(Screens::ConsumeFocusAboveRequest()) {g.editorFocus=-5;Neutral();}
        }
        if(Screens::IsPopupActive()) Screens::TickPopupIfOpen(dl,list);
        const std::string context="P"+std::to_string(g.player)+" / "+g.context.displayName+(g.document.Dirty()?" *  ":"  ")+g.liveStatus;
        FitText(dl,28,425,584,context,Theme::kTextInactive);
        FitText(dl,28,443,584,g.status,Theme::kInfoAccent);
        const char* help=g.editorFocus<0?"LEFT / RIGHT: FILE CONTROLS   DOWN: EDITOR   UP: TABS":
            g.view==State::View::Files?Screens::CurrentHelpText():g.panMode?
            "Pan: arrows / right-drag. Confirm or Back finishes panning.":
            g.editorFocus==20?"APPLY keeps these colors in this Practice match. No file is written.":
            g.editorFocus==21?"RESTORE returns to the last applied or original match colors.":
            g.editorFocus==19?"PREVIEW is temporary; leaving this tab restores match colors.":
            "ARROWS: SELECT / ADJUST   CONFIRM: CHANGE   BACK: GAME";
        FitText(dl,28,461,584,help?help:"",Theme::kTextInactive);
    }
    if(g.confirm) DrawConfirmation(dl);
    else if(g.textKind!=State::TextKind::None) DrawTextEditor(dl);
}
}
