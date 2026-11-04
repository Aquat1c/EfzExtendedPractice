#include "../src/gui/custom_menu/palette_editor.cpp"
#include "gui/custom_menu/fonts.h"
#include "gui/custom_menu/sound.h"
#include "utils/i18n.h"
#include "utils/config.h"
#include "utils/utilities.h"
#include "utils/xinput_shim.h"
#include "../3rdparty/imgui/backends/imgui_impl_dx9.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <map>
#include <nlohmann/json.hpp>

namespace {
ImFont* bodyFont=nullptr;
ImFont* headerFont=nullptr;
struct TestInput {
    bool up=false,down=false,left=false,right=false,leftHeld=false,rightHeld=false,activate=false,back=false;
    bool switchPlayer=false,previous=false,next=false,click=false,rightClick=false;
    float x=-100,y=-100,wheel=0;
} input;
void Require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
bool Inside(float x,float y,float w,float h) {return input.x>=x && input.x<x+w && input.y>=y && input.y<y+h;}
}
namespace CustomMenu::Input {
bool IsGameWindowActive(){return true;}
void ResetEdges(){
    const auto previous=input;input={};
    input.click=previous.click;input.rightClick=previous.rightClick;
    input.x=previous.x;input.y=previous.y;input.wheel=previous.wheel;
}
void SetSuspended(bool){}
bool IsSuspended(){return false;}
bool NavUp(){return input.up;} bool NavDown(){return input.down;}
bool NavLeft(){return input.left;} bool NavRight(){return input.right;}
bool NavLeftHeld(){return input.leftHeld;} bool NavRightHeld(){return input.rightHeld;}
bool Activate(){return input.activate;} bool Back(){return input.back;}
bool SwitchPlayer(){return input.switchPlayer;}
bool TopTabPrev(){return input.previous;} bool TopTabNext(){return input.next;}
bool SubTabPrev(){return false;} bool SubTabNext(){return false;}
int TopTabNumberEdge(){return 0;}
MousePos GetMouse(){return {input.x,input.y,true};}
float MouseWheel(){return input.wheel;}
bool MouseHovering(float x,float y,float w,float h){return Inside(x,y,w,h);}
bool MouseClickedIn(float x,float y,float w,float h){return input.click && Inside(x,y,w,h);}
bool MouseRightClickedIn(float x,float y,float w,float h){return input.rightClick && Inside(x,y,w,h);}
bool MouseLeftEdge(){return input.click;}
}
namespace CustomMenu::Sound {
void PlayCursor(){}
void PlayDecision(){}
void PlayCancel(){}
}
namespace {
std::map<int,Mission::MoveData::Cls> testMoveClasses{{0,Mission::MoveData::Cls::System},{105,Mission::MoveData::Cls::A}};
}
namespace Mission::MoveData {
Cls GetClass(int,int moveId){const auto found=testMoveClasses.find(moveId);return found==testMoveClasses.end()?Cls::Unknown:found->second;}
const char* ClassName(Cls type){static const char* labels[]={"","System","A","B","C","Command","Special","Super","Projectile","Super entity","Entity"};return labels[int(type)];}
}
HMODULE g_hSelfModule=GetModuleHandleW(nullptr);
void LogOut(const std::string&,bool){}
namespace Mission::Render {void InvalidateTextLayouts(){}}
namespace I18n {
const char* Tr(const char* text){return text;}
Language GetLanguage(){return English;}
const unsigned short* JapaneseGlyphRanges(){static const unsigned short ranges[]={0x20,0x7E,0};return ranges;}
}
KeyBindings detectedBindings{};
namespace Config {
const Settings& GetSettings(){static Settings settings{};return settings;}
void SetSetting(const std::string&,const std::string&,const std::string&){}
bool IsMenuKeyAllowed(int){return true;}
bool IsVkBoundByEfz(int){return false;}
std::string GetGamepadButtonName(int){return "Button";}
void SwapPadNavigationConflict(const int*,int,int){}
}
namespace CollisionDisplay {bool IsGameHotkeyVk(int){return false;}}
namespace XInputShim {
void RefreshSnapshotOncePerFrame(){}
void CopySnapshot(Snapshot& snapshot){snapshot={};}
}
namespace CustomMenu::Screens {
void ResetTextEditor(){}
const char* FormatTriggerButtonRow(const Row&){return "";}
bool AdjustTriggerButtonRow(const Row&,int){return false;}
}
namespace {
using namespace CustomMenu;
namespace Panel=CustomMenu::PaletteEditorPanel;
void SetFonts(float scale) {
    Fonts::InvalidateAtlasReferences();
    Require(Fonts::Rebuild(scale),"Production font resources failed to load");
    bodyFont=Fonts::Body();headerFont=Fonts::Header();
    Require(bodyFont && headerFont && Fonts::IsLoaded(),"Production menu fonts unavailable");
}
void Seed() {
    Panel::g=Panel::State{};
    Panel::g.open=Panel::g.loaded=true;
    Panel::g_open.store(true);
    Panel::g.context.resourceName="ayu";
    Panel::g.context.displayName="Ayu";
    ::PaletteEditor::Palette colors{};
    for(unsigned i=0;i<40;++i) colors[i]={uint8_t(32+i*5),uint8_t(16+i*3),uint8_t(8+i*2)};
    Panel::g.document.LoadStock("ayu",0,colors);
    Panel::g.assets.stockPalettes.fill(colors);
    Panel::g.assets.transparentKey=0;
    Panel::g.assets.sprites.push_back({2,2,{0,1,2,39}});
    Panel::g.assets.animations.push_back({0,{{0,0,1,2,2,2},{0,4,1,2,2,2}}});
    Panel::g.assets.animations.push_back({105,{{0,4,1,2,2,2}}});
    Panel::BuildMoveChoices();Panel::SyncColor();
    Panel::g.status="Draft changes remain separate from saved palettes.";
    Panel::g.liveStatus="Live preview applied.";
    input={};
}
void DrawPalettePage(bool inputEnabled=true) {
    auto* dl=ImGui::GetBackgroundDrawList();
    const auto origin=Layout::DrawPanel(dl,"TRAINING SETTINGS");
    const auto& metrics=Scale::Get();
    const char* topTabs[]={"MAIN","AUTO","CHARS","PALETTE","SETTINGS","HELP"};
    const float top=Scale::Snap(origin.y+metrics.tabBarHeight+2);
    Require(Layout::TabBarWidth(topTabs,6)<=640,"Top tabs exceed the menu width");
    Layout::DrawTabBar(dl,0,origin.y,640,topTabs,6,3,-1);
    const int firstVertex=dl->VtxBuffer.Size;
    const bool hadOverlay=Panel::Modal() || Screens::IsPopupActive();
    Panel::Render(dl,top,inputEnabled);
    if(!hadOverlay && !Panel::Modal() && !Screens::IsPopupActive())
        for(int i=firstVertex;i<dl->VtxBuffer.Size;++i)
            Require(dl->VtxBuffer[i].pos.y>=top-2,"Palette content drew over the shared header or top tabs");
}
void BeginFrame() {
    input={};
    ImGui_ImplDX9_NewFrame();
    ImGui::NewFrame();
}
void EndFrame(IDirect3DDevice9* device) {
    ImGui::Render();
    if(!device) return;
    device->Clear(0,nullptr,D3DCLEAR_TARGET,D3DCOLOR_XRGB(35,40,46),1,0);
    Require(SUCCEEDED(device->BeginScene()),"BeginScene failed");
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    device->EndScene();
}
void NextFrame() {EndFrame(nullptr);BeginFrame();}
void SaveBitmap(IDirect3DDevice9* device,const std::filesystem::path& path) {
    IDirect3DSurface9* render=nullptr;IDirect3DSurface9* copy=nullptr;
    Require(SUCCEEDED(device->GetRenderTarget(0,&render)),"Missing render surface");
    D3DSURFACE_DESC desc{};render->GetDesc(&desc);
    Require(SUCCEEDED(device->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&copy,nullptr)),"Copy surface failed");
    Require(SUCCEEDED(device->GetRenderTargetData(render,copy)),"Readback failed");
    D3DLOCKED_RECT lock{};Require(SUCCEEDED(copy->LockRect(&lock,nullptr,D3DLOCK_READONLY)),"Readback lock failed");
    BITMAPFILEHEADER file{};BITMAPINFOHEADER info{};
    file.bfType=0x4D42;file.bfOffBits=sizeof(file)+sizeof(info);
    file.bfSize=file.bfOffBits+desc.Width*desc.Height*4;
    info.biSize=sizeof(info);info.biWidth=LONG(desc.Width);info.biHeight=-LONG(desc.Height);
    info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;
    std::ofstream output(path,std::ios::binary);
    output.write(reinterpret_cast<char*>(&file),sizeof(file));output.write(reinterpret_cast<char*>(&info),sizeof(info));
    for(unsigned y=0;y<desc.Height;++y) output.write(static_cast<char*>(lock.pBits)+y*lock.Pitch,desc.Width*4);
    copy->UnlockRect();copy->Release();render->Release();
    Require(bool(output),"Screenshot could not be saved");
}
std::filesystem::path contextFixture;
int contextStage=0;
bool TestContext(int,Panel::Context& context,std::string&) {
    context.gameDirectory=contextFixture;context.libraryDirectory=contextFixture/"library";
    context.resourceName="ayu";context.displayName="Ayu";context.hasCustom=true;context.stageId=contextStage;
    return true;
}
void WriteBytes(const std::filesystem::path& path,const std::vector<uint8_t>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path,std::ios::binary);stream.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    Require(bool(stream),"Fixture write failed");
}
void CheckSources(const std::filesystem::path& directory) {
    contextFixture=directory/("fixture-"+std::to_string(GetCurrentProcessId()));
    std::vector<uint8_t> dat(721);dat[0]=240;dat[1]=255;dat[2]=128;dat[3]=17;
    const uint8_t sprite[]={3,0,1,0,15,0x82};dat.insert(dat.end(),std::begin(sprite),std::end(sprite));
    WriteBytes(contextFixture/"ayu"/"ayu.dat",dat);
    std::vector<uint8_t> pat;
    const auto append=[&](int value){for(int byte=0;byte<4;++byte)pat.push_back(uint8_t(unsigned(value)>>(byte*8)));};
    for(int value:{1,0,0,0,1})append(value);
    for(int value:{4,0,3,1,1,1})append(value);
    for(int i=6;i<60;++i)append(0);
    WriteBytes(contextFixture/"ayu"/"ayu.pat",pat);
    ::PaletteEditor::Palette native{};native.fill({70,140,210});
    const auto nativeBytes=::PaletteEditor::EncodeNativePalette(native);
    WriteBytes(contextFixture/"ayu"/"ayu1.pal",std::vector<uint8_t>(nativeBytes.begin(),nativeBytes.end()));
    Panel::Configure(TestContext,nullptr);
    auto* queuedTexture=Panel::g_workingTexture.value;
    Panel::LoadPlayer(1);
    Require(Panel::g.loaded && Panel::g.document.Colors()==native,"Opening active custom PAL substituted stock colors");
    Require(!Panel::g.document.Dirty(),"Loaded native PAL was incorrectly dirty");
    Require(Panel::g.document.Original()!=native,"Native import overwrote immutable stock baseline");
    Require(Panel::g_workingTexture.value==queuedTexture,"Source switch released a queued texture");
    BeginFrame();
    Panel::Save();
    Require(Panel::g.textKind==Panel::State::TextKind::SaveAs,"Saving a new palette did not ask for its name");
    Panel::g.text="UI Test";Panel::CommitText();
    Require(Panel::g.source.has_value() && !Panel::g.document.Dirty(),"Save new failed");
    Panel::g.document.SetColor(1,{99,88,77});Panel::Save();
    Require(bool(Panel::g.confirm) && Panel::g.confirmFocus==0,"Replace confirmation was not safe by default");
    NextFrame();input.activate=true;Panel::DrawConfirmation(ImGui::GetForegroundDrawList());
    Require(Panel::g.document.Dirty(),"Keep editing committed replacement");
    Panel::Save();NextFrame();Panel::g.confirmFocus=1;input.activate=true;
    Panel::DrawConfirmation(ImGui::GetForegroundDrawList());
    Require(!Panel::g.document.Dirty(),"Confirmed replacement failed");
    ::PaletteEditor::SavedPalette saved;std::string error;
    Require(Panel::g.library->Read(*Panel::g.source,saved,error) && saved.colors[1]==::PaletteEditor::Color{99,88,77},"Replacement did not persist colors");
    Panel::DeleteOrRestore();NextFrame();Panel::g.confirmFocus=1;input.activate=true;
    Panel::DrawConfirmation(ImGui::GetForegroundDrawList());
    Panel::g.trash=true;Panel::ScanLibrary();Require(Panel::g.entries.size()==1,"Trash did not retain palette");
    Panel::DeleteOrRestore();NextFrame();Panel::g.confirmFocus=1;input.activate=true;
    Panel::DrawConfirmation(ImGui::GetForegroundDrawList());
    Panel::g.trash=false;Panel::ScanLibrary();Require(Panel::g.entries.size()==1,"Restore lost palette");
    const auto originalEntry=*Panel::g.source;
    Panel::g.document.SetColor(2,{55,66,77});const auto copyColors=Panel::g.document.Colors();
    Panel::SaveAs();
    Require(Panel::g.textKind==Panel::State::TextKind::SaveAs && Panel::g.text.empty(),"Save As did not ask for a different name");
    Panel::g.text="UI Test copy";Panel::CommitText();
    Require(Panel::g.source->path!=originalEntry.path && Panel::g.document.State().name=="UI Test copy" && !Panel::g.document.Dirty(),
        "Save As did not create a separate named palette");
    const auto copyEntry=*Panel::g.source;
    Require(Panel::g.library->Read(copyEntry,saved,error) && saved.colors==copyColors,"Save As colors did not round-trip");
    Require(Panel::g.library->Read(originalEntry,saved,error) && saved.colors[2]!=copyColors[2],"Save As overwrote the original palette");
    Panel::g.document.SetColor(3,{22,44,88});const auto collisionDraft=Panel::g.document.Colors();
    Panel::SaveAs();Panel::g.text="UI Test";Panel::CommitText();
    Require(Panel::g.textKind==Panel::State::TextKind::SaveAs && Panel::g.source->path==copyEntry.path &&
        Panel::g.document.State().name=="UI Test copy" && Panel::g.document.Colors()==collisionDraft && Panel::g.document.Dirty(),
        "A Save As name collision replaced the source or lost draft edits");
    Require(Panel::g.library->Read(originalEntry,saved,error) && saved.colors[2]!=copyColors[2],"Save As name collision overwrote the existing file");
    NextFrame();input.back=true;Panel::DrawTextEditor(ImGui::GetForegroundDrawList());
    Require(Panel::g.textKind==Panel::State::TextKind::None && Panel::g.document.Colors()==collisionDraft,
        "Cancelling Save As lost the draft");
    ::PaletteEditor::SavedPalette originalPalette;
    ::PaletteEditor::LibraryEntry trashedEntry,occupiedEntry,restoreResult;
    Require(Panel::g.library->Read(originalEntry,originalPalette,error),"Original palette fixture could not be read");
    Require(Panel::g.library->Delete(originalEntry,trashedEntry,error),"Original palette could not move to trash");
    auto replacement=originalPalette;replacement.colors[0]={15,25,35};
    Require(Panel::g.library->SaveNew(replacement,occupiedEntry,error),"Replacement palette could not occupy the original filename");
    Require(!Panel::g.library->Restore(trashedEntry,restoreResult,error),"Restore unexpectedly overwrote an occupied filename");
    Panel::g.trash=true;Panel::ScanLibrary();Panel::g.libraryIndex=0;
    auto recoveryRows=Panel::FilesRows();
    const auto loadDeleted=std::find_if(recoveryRows.begin(),recoveryRows.end(),[](const auto& row){return std::string(row.label)=="Load selected";});
    Require(loadDeleted!=recoveryRows.end() && loadDeleted->action,"Trash recovery has no load action after a filename collision");
    loadDeleted->action();
    Require(bool(Panel::g.confirm) && Panel::g.document.Colors()==collisionDraft,"Loading a deleted palette bypassed the draft guard");
    NextFrame();Panel::g.confirmFocus=1;input.activate=true;Panel::DrawConfirmation(ImGui::GetForegroundDrawList());
    Require(!Panel::g.trash && Panel::g.source->deleted && Panel::g.document.Colors()==originalPalette.colors,
        "Deleted palette did not load into the Save As workflow");
    Panel::SaveAs();Panel::g.text="Recovered original";Panel::CommitText();
    Require(Panel::g.library->Read(*Panel::g.source,saved,error) && saved.colors==originalPalette.colors,
        "Recovering a deleted palette with Save As changed its colors");
    Require(Panel::g.library->Read(trashedEntry,saved,error) && saved.colors==originalPalette.colors,
        "Recovery removed or changed the original trashed palette");
    Require(Panel::g.library->Read(occupiedEntry,saved,error) && saved.colors==replacement.colors,
        "Recovery overwrote the palette occupying the original filename");
    Panel::g.document.SetColor(2,{11,22,33});const auto draft=Panel::g.document.Colors();
    contextStage=2;Panel::Open(1);
    Require(Panel::g.context.stageId==2 && Panel::g.document.Colors()==draft,"Reopen did not refresh context while retaining draft");
    Panel::g.document.Revert();Panel::Open(2);
    Require(Panel::g.player==2 && !Panel::g.confirm,"P2 editor did not open");
    Panel::g.document.SetColor(5,{19,87,143});const auto p2Draft=Panel::g.document.Colors();
    Panel::Suspend();Panel::Open();
    Require(Panel::g.player==2 && Panel::g.document.Dirty() && Panel::g.document.Colors()==p2Draft && !Panel::g.confirm,
        "Default reopen changed player or prompted to discard the P2 draft");
    EndFrame(nullptr);
    WriteBytes(contextFixture/"ayu"/"ayu1.pal",{1,2});Panel::LoadPlayer(1);
    Require(!Panel::g.live && Panel::g.status.find("disabled")!=std::string::npos,"Unreadable active custom PAL silently enabled stock live preview");
}
void CheckOpeningClicks(const std::filesystem::path& directory) {
    SetFonts(1.5f);Seed();
    Panel::g.library=std::make_unique<::PaletteEditor::Library>(directory/("modal-"+std::to_string(GetCurrentProcessId())));
    ::PaletteEditor::LibraryEntry entry;std::string error;
    Require(Panel::g.library->SaveNew(Panel::g.document.State(),entry,error),"Modal fixture could not be saved");
    const auto savedColors=Panel::g.document.Colors();
    Panel::ScanLibrary();Panel::g.document.SetColor(3,{10,20,30});
    BeginFrame();input.click=true;input.x=430;input.y=110;DrawPalettePage();
    Require(Screens::IsPopupActive(),"Toolbar Load did not open saved palettes");
    NextFrame();input.activate=true;DrawPalettePage();
    Require(bool(Panel::g.confirm) && Panel::g.document.Dirty(),"Toolbar Load bypassed the draft guard");
    NextFrame();input.back=true;DrawPalettePage();EndFrame(nullptr);
    ::PaletteEditor::LibraryEntry deleted;
    Require(Panel::g.library->Delete(entry,deleted,error),"Recovery modal fixture could not move to trash");
    Panel::g.trash=true;Panel::ScanLibrary();Panel::g.view=Panel::State::View::Files;
    Panel::g.document.SetColor(3,{10,20,30});const auto draft=Panel::g.document.Colors();
    BeginFrame();input.click=true;input.x=400;input.y=295;DrawPalettePage();
    Require(bool(Panel::g.confirm) && Panel::g.document.Dirty() && Panel::g.document.Colors()==draft,
        "Clicking Load selected activated DISCARD in the newly opened confirmation");
    EndFrame(nullptr);BeginFrame();input.click=true;input.x=400;input.y=295;DrawPalettePage();
    Require(!Panel::g.confirm && Panel::g.document.Colors()==savedColors,"A later DISCARD click did not load the selected palette");
    EndFrame(nullptr);BeginFrame();input.click=true;input.x=300;input.y=110;DrawPalettePage();
    Require(Panel::g.textKind==Panel::State::TextKind::SaveAs && Panel::g.text.empty(),
        "Clicking Save As typed into the newly opened keyboard");
    EndFrame(nullptr);BeginFrame();input.click=true;input.x=100;input.y=240;DrawPalettePage();
    Require(Panel::g.text=="K","A later keyboard click did not type the selected letter");
    EndFrame(nullptr);BeginFrame();input.back=true;DrawPalettePage();EndFrame(nullptr);
    ::PaletteEditor::LibraryEntry replacement;
    auto saved=Panel::g.document.State();saved.name="Active saved palette";
    Require(Panel::g.library->SaveNew(saved,replacement,error),"Saved palette load fixture could not be written");
    Panel::g.view=Panel::State::View::Files;Panel::g.trash=true;Panel::ScanLibrary();
    BeginFrame();input.click=true;input.x=430;input.y=110;DrawPalettePage();
    Require(Screens::IsPopupActive() && !Panel::g.trash && Panel::g.view==Panel::State::View::Editor &&
        Panel::g.entries.size()==1 && !Panel::g.entries[0].deleted,
        "Toolbar Load offered trash instead of active saved palettes");
    NextFrame();input.activate=true;DrawPalettePage();
    Require(Panel::g.source && Panel::g.source->path==replacement.path,"Toolbar Load did not load the selected saved file");
    EndFrame(nullptr);SetFonts(1);
}
void CaptureClasses(const std::filesystem::path& directory,const std::string& character) {
    using Cls=Mission::MoveData::Cls;
    const std::map<std::string,Cls> classes{{"system",Cls::System},{"a",Cls::A},{"b",Cls::B},{"c",Cls::C},{"command",Cls::Command},
        {"special",Cls::Special},{"super",Cls::Super},{"projectile",Cls::Projectile},{"super_entity",Cls::SuperEntity},{"entity",Cls::Entity}};
    testMoveClasses.clear();
    std::vector<std::string> profiles{character};if(character=="ikumi") profiles.push_back("ikumi2");
    for(const auto& profile:profiles) {
        std::ifstream inputFile(directory/(profile+".json"));Require(bool(inputFile),"Capture move classifications missing");
        nlohmann::json data;inputFile>>data;
        for(const auto& item:data.at("moves").items()) {
            auto name=item.value().value("class","");for(char& c:name)c=char(std::tolower(static_cast<unsigned char>(c)));
            const auto found=classes.find(name);const int id=std::stoi(item.key());
            testMoveClasses[id]=Mission::MoveData::Detail::MergeProfileClass(testMoveClasses[id],found==classes.end()?Cls::Unknown:found->second,id);
        }
    }
}
void CaptureRealAssets(IDirect3DDevice9* device,const std::filesystem::path& game,const std::filesystem::path& output,const std::filesystem::path& classifications) {
    for(const std::string character:{"ayu","nayukib","ikumi"}) {
        CaptureClasses(classifications,character);
        for(float scale:{0.7f,1.0f,1.5f}) {
            SetFonts(scale);Seed();
            auto& state=Panel::g;state.context.gameDirectory=game;state.context.resourceName=character;state.context.displayName=character=="ayu"?"Ayu":character=="nayukib"?"Nayuki":"Ikumi";
            std::string error;
            const auto variant=character=="ikumi"?::PaletteEditor::AnimationVariant::Alternate : ::PaletteEditor::AnimationVariant::Standard;
            Require(::PaletteEditor::LoadCharacterAssets(game,character,state.assets,error,variant),error.c_str());
            state.alternate=variant==::PaletteEditor::AnimationVariant::Alternate;
            ::PaletteEditor::LoadSharedBattlePalette(game,0,state.shared,error);
            state.document.LoadStock(character,0,state.assets.stockPalettes[0]);

            Panel::ResetPreview();Panel::BuildMoveChoices();
            for(int choice=0;choice<int(state.moveIndices.size());++choice) {
                const int index=state.moveIndices[choice];
                if(Mission::MoveNames::Lookup(character.c_str(),short(state.assets.animations[index].id))) {state.moveChoice=choice;Panel::MoveChoiceChanged();break;}
            }
            std::array<unsigned,40> counts{};
            const auto& frame=state.assets.animations[state.animation].frames[0];
            for(auto index:state.assets.sprites[frame.spriteId].indices) if(index<40 && index!=state.assets.transparentKey) ++counts[index];
            const unsigned selected=unsigned(std::max_element(counts.begin(),counts.end())-counts.begin());
            state.color=int(selected);
            for(unsigned ramp=0;ramp<state.document.Ramps().size();++ramp) {
                const auto& span=state.document.Ramps()[ramp];
                if(selected>=span.first && selected<=span.last) {state.ramp=int(ramp);state.target=1;state.document.SetRampHsv(ramp,{185,85,90});break;}
            }
            state.playing=false;state.compare=2;
            Panel::g_originalTexture.sprite=Panel::g_workingTexture.sprite=-1;
            state.status="File preview: original left / working right. No live game in this harness.";
            state.liveStatus=state.moveLabels[state.animation];
            for(int target:{0,1}) {
                state.view=Panel::State::View::Editor;state.target=target;state.autoFit=true;BeginFrame();DrawPalettePage();EndFrame(device);
                SaveBitmap(device,output/(character+"-"+std::to_string(int(scale*100))+(target?"-ramps.bmp":"-editor.bmp")));
            }
        }
    }
}
void CheckFontFit() {
    const std::pair<const char*,int> labels[]={{"RGB > HSV",132},{"HSV > RGB",132},{"#FFFFFF",144},{"UNPIN",80},{"RESET",90},
        {"UNDO 16",100},{"400%",102},{"PAN*",78},{"CENTER",90},{"ORIGINAL",104},{"MARK",67},{"PREVIEW",99},{"APPLY",137},{"RESTORE",137},{"ALT",76},
        {"PAUSE 999 / 999",186},{"ALL / NO CLASS DATA",284},{"P1  <>",112},{"SAVE",112},{"SAVE AS",112},{"LOAD",112},{"FILES",112},{"EDITOR",112}};
    for(const auto& label:labels)
        Require(Layout::MeasureTextW(bodyFont,bodyFont->FontSize,label.first)<=label.second-16,
            (std::string(label.first)+" width="+std::to_string(Layout::MeasureTextW(bodyFont,bodyFont->FontSize,label.first))+" limit="+std::to_string(label.second-16)).c_str());
    for(const char* label:{"DEL","aA","OK","ESC"})
        Require(Layout::MeasureTextW(bodyFont,bodyFont->FontSize,label)<=45,label);
    const auto fitted=Panel::ButtonLabel("999 - Long projectile animation label",130);
    Require(fitted.find("999 - ")==0 && fitted.substr(fitted.size()-3)=="...","Long move label lost its leading PAT ID");
    Require(Layout::MeasureTextW(bodyFont,bodyFont->FontSize,fitted.c_str())<=130,"Fitted label exceeds glyph budget");
}
void CheckRampGeometry() {
    for(float scale:{0.7f,1.0f,1.5f}) {
        SetFonts(scale);CheckFontFit();Seed();BeginFrame();
        for(bool singles:{true,false}) {
            auto saved=Panel::g.document.State();saved.ramps.clear();
            if(singles) for(unsigned i=0;i<40;++i)saved.ramps.push_back({i,i,i});
            else saved.ramps.push_back({0,39,20});
            std::string error;Require(Panel::g.document.LoadSaved(saved,saved.baseline,error),"Extreme ramp fixture rejected");
            Panel::g.ramp=0;Panel::g.target=1;Panel::SyncColor();
            const auto geometry=Panel::RampGridGeometry(saved.ramps,0,124);
            Require(geometry.repHeight>=18 && geometry.shadeHeight>=8,"Ramp grid collapsed a valid shade row");
            Require(geometry.shadeY+geometry.shadeRows*geometry.shadeHeight<=124.01f,"Ramp grid exceeded its bounds");
            input={};input.click=true;
            if(singles) {input.x=28+9.5f*28.4f;input.y=100+3.5f*geometry.repHeight;}
            else {input.x=28+7.5f*35.5f;input.y=100+geometry.shadeY+4.5f*geometry.shadeHeight;}
            Panel::DrawRampGrid(ImGui::GetForegroundDrawList(),28,100,284,124,true);
            Require(singles?Panel::g.ramp==39:Panel::g.color==39,"Extreme ramp grid hit target was outside its visible shade");
        }
        EndFrame(nullptr);
    }
    SetFonts(1);
}
void CheckPreviewTexture(IDirect3DDevice9* device) {
    Seed();
    const auto draw = [&] {
        BeginFrame();
        const auto colors=::PaletteEditor::BuildPreviewColors(Panel::g.document.Colors(),Panel::g.shared);
        Require(Panel::UpdateTexture(Panel::g_workingTexture,0,colors,-1),"Sprite texture upload failed on the active D3D device");
        auto* dl=ImGui::GetForegroundDrawList();
        dl->AddCallback(Panel::PointSampler,nullptr);
        dl->AddImage(reinterpret_cast<ImTextureID>(Panel::g_workingTexture.value),ImVec2(0,0),ImVec2(64,64));
        dl->AddCallback(ImDrawCallback_ResetRenderState,nullptr);
        EndFrame(device);
        IDirect3DSurface9* target=nullptr;IDirect3DSurface9* copy=nullptr;
        Require(SUCCEEDED(device->GetRenderTarget(0,&target)),"Texture check render target missing");
        D3DSURFACE_DESC desc{};target->GetDesc(&desc);
        Require(SUCCEEDED(device->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&copy,nullptr)),"Texture check readback allocation failed");
        Require(SUCCEEDED(device->GetRenderTargetData(target,copy)),"Texture check readback failed");
        D3DLOCKED_RECT lock{};
        Require(SUCCEEDED(copy->LockRect(&lock,nullptr,D3DLOCK_READONLY)),"Texture check readback lock failed");
        std::array<DWORD,4> pixels{};
        for(int i=0;i<4;++i)
            pixels[i]=reinterpret_cast<DWORD*>(static_cast<char*>(lock.pBits)+(16+(i/2)*32)*lock.Pitch)[16+(i%2)*32]&0xFFFFFF;
        copy->UnlockRect();copy->Release();target->Release();
        return pixels;
    };
    const auto original=draw();
    Require(original==std::array<DWORD,4>{0x23282E,0x25130A,0x2A160C,0xE38556},"Sprite texture pixels or transparent key rendered incorrectly");
    Panel::g.document.SetColor(1,{255,1,2});
    const auto edited=draw();
    Require(edited==std::array<DWORD,4>{0x23282E,0xFF0102,0x2A160C,0xE38556},"Changed palette was not uploaded to the sprite texture");
    std::cout<<"Sprite texture readback and live color updates passed.\n";
}
void CheckEditorNavigation() {
    for(float scale:{0.7f,1.0f,1.5f}) {
        SetFonts(scale);Seed();BeginFrame();
        for(float y:{95.0f,123.0f}) {
            input={};input.x=350;input.y=y;input.click=true;
            Panel::DrawEditor(ImGui::GetForegroundDrawList(),72,true);
            Require(Screens::IsPopupActive(),"Bottom of editor picker button did not open its dropdown");
            Screens::ClosePopup();
        }
        EndFrame(nullptr);
    }
    SetFonts(1);Seed();Panel::g.target=0;Panel::g.color=5;Panel::SyncColor();Panel::g.editorFocus=3;
    const auto baseline=Panel::g.document.Colors();
    for(int step=0;step<21;++step) {
        BeginFrame();input.right=input.rightHeld=true;
        Panel::DrawEditor(ImGui::GetForegroundDrawList(),72,true);EndFrame(nullptr);
        BeginFrame();input.rightHeld=true;
        Panel::DrawEditor(ImGui::GetForegroundDrawList(),72,true);EndFrame(nullptr);
    }
    BeginFrame();Panel::DrawEditor(ImGui::GetForegroundDrawList(),72,true);
    Require(Panel::g.document.UndoCount()==1,"Held controller color adjustment consumed multiple undo slots");
    Panel::g.document.Undo();Require(Panel::g.document.Colors()==baseline,"Held adjustment lost its baseline");
    Panel::g.document.SetPinned(5,true);Panel::g.editorFocus=5;
    input={};input.left=input.leftHeld=true;Panel::DrawEditor(ImGui::GetForegroundDrawList(),72,true);
    const auto componentEdit=Panel::g.document.Colors();
    input={};input.down=input.left=input.leftHeld=true;Panel::DrawEditor(ImGui::GetForegroundDrawList(),72,true);
    Require(!Panel::g.document.Pinned(5),"Held direction did not reach the pin control");
    Panel::g.document.Undo();
    Require(Panel::g.document.Pinned(5) && Panel::g.document.Colors()==componentEdit,"Focus movement merged pin change into component gesture");
    Panel::g.editorFocus=3;input={};input.right=input.rightHeld=true;
    Panel::DrawEditor(ImGui::GetForegroundDrawList(),72,true);
    const auto beforeMousePin=Panel::g.document.Colors();
    input={};input.rightHeld=input.rightClick=true;input.x=28+5.5f*35.5f;input.y=110;
    Panel::DrawEditor(ImGui::GetForegroundDrawList(),72,true);
    Require(!Panel::g.document.Pinned(5),"Swatch right-click did not toggle the pin");
    Panel::g.document.Undo();
    Require(Panel::g.document.Pinned(5) && Panel::g.document.Colors()==beforeMousePin,"Mouse pin merged into held color gesture");
    Panel::g.animation=0;Panel::g.frame=0;Panel::g.playing=true;Panel::g.editorFocus=12;
    input={};input.right=true;Panel::DrawEditor(ImGui::GetForegroundDrawList(),72,true);
    Require(Panel::g.frame==1 && !Panel::g.playing,"Controller frame step did not pause and advance");
    input={};input.left=true;Panel::DrawEditor(ImGui::GetForegroundDrawList(),72,true);
    Require(Panel::g.frame==0 && !Panel::g.playing,"Controller reverse frame step failed");
    EndFrame(nullptr);
}
int appliedPlayer=0,restoreCalls=0;
bool cancelledApply=false;
::PaletteEditor::Palette appliedColors{};
bool ApplyFixture(int player,const std::string& resource,const ::PaletteEditor::Palette& colors,std::string&) {
    Require(resource=="ayu","Apply did not preserve the fighter resource");
    appliedPlayer=player;appliedColors=colors;return true;
}
void RestoreFixture(bool cancelPendingApply) {++restoreCalls;cancelledApply=cancelPendingApply;}
void CheckTabContent() {
    SetFonts(1);Seed();BeginFrame();
    Require(Panel::g.target==1,"Palette editor did not default to ramps");
    Require(Panel::g.moveLabels[0]=="0 - Idle","Idle state was not named");
    Panel::g.assets.animations.push_back({1,{{0,4,1,2,2,2}}});Panel::BuildMoveChoices();
    Require(Panel::g.moveLabels.back()=="1 - Walk forward","Walk state was not named");
    Require(std::string(Panel::CommonStateLabel(157))=="Air recovery forward" &&
        std::string(Panel::CommonStateLabel(98))=="Ground recovery" &&
        std::string(Panel::CommonStateLabel(100))=="Thrown" && Panel::CommonStateLabel(149)==nullptr,
        "Known recovery or thrown states were unnamed, or an unknown state was guessed");
    const auto filesRows=Panel::FilesRows();
    for(const auto& row:filesRows) Require(row.action!=Panel::Save && row.action!=Panel::SaveAs && row.action!=Panel::LoadCustom,
        "Files duplicated a primary toolbar action");
    Panel::g.trash=true;const auto recoveryRows=Panel::FilesRows();
    Require(!recoveryRows.empty() && std::string(recoveryRows[0].label)=="Back to files", "Recovery has no return to Files");
    Panel::g.trash=false;
    Panel::g.document.SetColor(5,{210,90,20});const auto draft=Panel::g.document.Colors();
    input.next=true;DrawPalettePage();
    Require(Panel::g.view==Panel::State::View::Editor,"Palette content consumed top-tab navigation");
    for(float x:{100.0f,550.0f}) {
        input={};input.click=true;input.x=x;input.y=10;DrawPalettePage(false);
        Require(!Panel::g.confirm && !Panel::ConsumeCloseRequest() && Panel::g.player==1,
            "Palette retained a hit target in the shared header");
    }
    Panel::g.editorFocus=3;input={};input.right=true;DrawPalettePage(false);
    Require(Panel::g.document.Colors()==draft,"Tab focus adjusted palette content");
    input={};input.click=true;input.x=100;input.y=76;DrawPalettePage(false);
    Require(bool(Panel::g.confirm),"Content player selector ignored a mouse click");
    NextFrame();input.back=true;DrawPalettePage();
    Require(!Panel::g.confirm && Panel::g.document.Colors()==draft,"Player switch cancellation lost the draft");
    NextFrame();input.click=true;input.x=300;input.y=76;DrawPalettePage();
    Require(Panel::g.textKind==Panel::State::TextKind::SaveAs && Panel::g.text.empty(),"Toolbar Save As did not open a fresh naming prompt");
    NextFrame();input.back=true;DrawPalettePage();
    NextFrame();input.click=true;input.x=550;input.y=76;DrawPalettePage();
    Require(Panel::g.view==Panel::State::View::Files && !Panel::Modal() && !Panel::ConsumeCloseRequest(),
        "Files toolbar action leaked its opening click");
    NextFrame();input.back=true;DrawPalettePage();
    Require(Panel::g.view==Panel::State::View::Editor && !Panel::ConsumeCloseRequest() && Panel::g.document.Colors()==draft,
        "Files Back did not return to the editor with the draft");
    NextFrame();input.back=true;DrawPalettePage();
    Require(Panel::ConsumeCloseRequest() && Panel::IsOpen() && Panel::g.document.Colors()==draft && !Panel::g.confirm,
        "Editor Back lost the draft or failed to return to game");
    NextFrame();Panel::g.editorFocus=0;input.up=true;DrawPalettePage();
    Require(Panel::g.editorFocus==-5 && !Panel::ConsumeFocusAboveRequest(),"Editor Up skipped the local toolbar");
    NextFrame();input.up=true;DrawPalettePage();
    Require(Panel::ConsumeFocusAboveRequest() && !Panel::ConsumeCloseRequest(),"Toolbar Up did not focus shell tabs");
    NextFrame();Panel::g.editorFocus=0;Panel::g.panMode=true;input.back=true;DrawPalettePage();
    Require(!Panel::g.panMode && !Panel::ConsumeCloseRequest(),"Back while panning closed the menu");
    Panel::StartText(Panel::State::TextKind::Name);
    input={};input.click=true;input.x=550;input.y=76;DrawPalettePage();
    Require(Panel::BlocksNavigation() && Panel::g.view==Panel::State::View::Editor,"Files clicked through text entry");
    NextFrame();input.back=true;DrawPalettePage();
    Require(!Panel::BlocksNavigation() && !input.back,"Closing text entry leaked its input");
    Panel::Confirm("TEST","Keep draft?","DISCARD",[]{});
    Require(Panel::BlocksNavigation(),"Confirmation did not block shell navigation");
    NextFrame();input.click=true;input.x=100;input.y=280;DrawPalettePage();
    Require(!Panel::BlocksNavigation() && Panel::g.document.Colors()==draft,"Closing confirmation changed the draft");
    Panel::OpenEditorChoice(Screens::DropdownRow("Category",&Panel::g.category,Panel::kMoveCategories,7,Panel::FilterMoves));
    Require(Panel::BlocksNavigation(),"Dropdown did not block shell navigation");
    Screens::ClosePopup();input={};
    Panel::Configure(nullptr,nullptr,ApplyFixture,RestoreFixture);
    Panel::g.compare=1;Panel::ApplyToMatch();
    Require(appliedPlayer==1 && appliedColors==draft && Panel::g.compare==0,"Apply did not show the applied edited colors");
    const int previousRestores=restoreCalls;Panel::RestoreMatch();
    Require(!Panel::g.live && cancelledApply && restoreCalls==previousRestores+1 && Panel::g.document.Colors()==draft,
        "Restore lost the draft or retained temporary preview");
    Panel::g.live=true;Panel::Suspend();
    Require(!Panel::IsOpen() && !cancelledApply && restoreCalls==previousRestores+2 && Panel::g.document.Colors()==draft,
        "Leaving palette failed to restore preview and retain draft");
    Panel::Configure(nullptr,nullptr);EndFrame(nullptr);
}
void CheckControls() {
    Seed();BeginFrame();
    Panel::g.target=0;Panel::g.color=5;Panel::SyncColor();
    const auto before=Panel::g.document.Colors();

    auto& io=ImGui::GetIO();
    const float right=28+31+284-87;
    input.x=28+31;input.y=113;
    input.click=true;io.MouseDown[0]=true;
    Panel::CompactSlider(ImGui::GetForegroundDrawList(),0,28,100,284,true);
    Require(Panel::g.sliderRow==3,"Slider did not capture click");
    input.click=false;input.x=right;
    Panel::CompactSlider(ImGui::GetForegroundDrawList(),0,28,100,284,true);
    Require(Panel::g.sliderRow==3,"Slider drag lost capture");
    Require(Panel::g.document.Colors()[5].r==255,"Slider did not reach end");
    io.MouseDown[0]=false;input={};Panel::DrawEditor(ImGui::GetForegroundDrawList(),72,true);
    Require(Panel::g.document.UndoCount()==1,"Slider drag created more than one undo step");
    Panel::g.document.Undo();Require(Panel::g.document.Colors()==before,"Undo did not restore drag baseline");
    input={};input.x=28+3*35.5f+6;input.y=100+2*20+6;input.click=true;
    Panel::DrawSwatches(ImGui::GetForegroundDrawList(),28,100,284,true);
    Require(Panel::g.color==19,"Swatch hit mapping selected wrong entry");
    input={};Panel::g.color=5;Panel::g.animation=0;Panel::g.frame=0;Panel::g.playing=false;
    input.click=true;input.x=128;input.y=184;
    Panel::DrawPreview(ImGui::GetForegroundDrawList(),28,90,200,120,true);
    Require(Panel::g.color==1,"Preview pixel pick failed");
    input={};Panel::g.playing=true;Panel::g.frame=0;Panel::g.elapsed=0;
    Panel::AdvancePreview();Require(Panel::g.frame==1,"Zero-duration PAT frame was not skipped");
    Panel::g.assets.animations[0].frames[1].durationFrames=0;
    Panel::g.elapsed=1;Panel::AdvancePreview();Require(Panel::g.frame==1,"All-zero animation did not hold deterministically");
    Panel::g.document.SetColor(1,{220,110,55});
    const auto closeDraft=Panel::g.document.Colors();
    Panel::Close();
    Require(!Panel::IsOpen() && Panel::g.document.Dirty() && Panel::g.document.Colors()==closeDraft && !Panel::g.confirm,
        "Closing the panel discarded its draft or forced a discard prompt");
    EndFrame(nullptr);
    Seed();BeginFrame();
    Require(Panel::g.moveLabels[1].find("105 - ")==0 && Panel::g.moveIndices==std::vector<int>{1},"Classified sparse move picker did not preserve actual PAT indices");
    Panel::g.assets.animations.push_back({999,{{0,4,1,2,2,2}}});
    Panel::g.assets.animations.push_back({233,{}});Panel::BuildMoveChoices();
    Panel::g.category=5;Panel::FilterMoves();Require(Panel::g.moveIndices==std::vector<int>{0},"System filter selected a non-system move");
    Panel::g.category=6;Panel::FilterMoves();Require(Panel::g.moveIndices==std::vector<int>{2},"Unclassified filter included an empty PAT animation");
    Require(Panel::g.moveLabels[2]=="999 - Unclassified" && Panel::g.moveLabels[0]!="0 - ","Move fallback label was blank");
    Panel::g.moveChoice=99;Panel::MoveChoiceChanged();Require(Panel::g.animation==2,"Filtered move selection was not clamped");
    Panel::g.category=3;Panel::FilterMoves();Require(Panel::g.category==3 && Panel::g.moveIndices.empty() && Panel::Animation()==nullptr,"Empty category silently changed category or retained another preview");
    Panel::StartText(Panel::State::TextKind::Name);Panel::g.text.clear();
    NextFrame();input.activate=true;Panel::DrawTextEditor(ImGui::GetForegroundDrawList());
    Require(Panel::g.text=="A","Controller keyboard did not enter selected character");
    ImGui::GetIO().InputQueueCharacters.push_back('B');input.activate=input.back=true;
    Panel::DrawTextEditor(ImGui::GetForegroundDrawList());
    Require(Panel::g.text=="AB" && Panel::g.textKind==Panel::State::TextKind::Name,"Typed fighter bindings activated or cancelled the text editor");
    input={};
    EndFrame(nullptr);
    ImGui::GetIO().AddInputCharactersUTF8("bcdefghijklmnopqrstuvwxyz0123456789");
    BeginFrame();
    Panel::DrawTextEditor(ImGui::GetForegroundDrawList());
    Require(Panel::g.text.size()==32,"Palette name limit was not enforced");
    Panel::CommitText();Require(Panel::g.document.State().name.size()==32,"Palette name commit failed");
    EndFrame(nullptr);
    Seed();BeginFrame();
    Panel::g.document.SetColor(1,{220,110,55});
    int player=0;std::string resource;::PaletteEditor::Palette colors;
    Require(Panel::GetLivePreview(player,resource,colors) && colors==Panel::g.document.Colors(),"Working live snapshot failed");
    Panel::g.compare=1;Require(Panel::GetLivePreview(player,resource,colors) && colors==Panel::g.document.Original(),"Original live snapshot failed");
    Panel::RequestSuspend();Require(!Panel::IsOpen(),"Suspend request was not immediately observable");
    DrawPalettePage();Require(Panel::g.document.Dirty(),"Suspend discarded document");
    EndFrame(nullptr);
}
}
int main(int argc,char** argv) {
    try {
        const bool extended=argc>1 && std::string(argv[1])=="--d3d9ex";
        if(extended) {--argc;++argv;}
        ImGui::CreateContext();
        auto& io=ImGui::GetIO();io.DisplaySize=ImVec2(640,480);io.DeltaTime=1.0f/60;io.IniFilename=nullptr;
        HWND window=CreateWindowExW(0,L"STATIC",L"Palette UI test",WS_POPUP,0,0,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        IDirect3D9* d3d=nullptr;IDirect3D9Ex* d3dEx=nullptr;
        if(extended) {
            using Create9Ex=HRESULT (WINAPI*)(UINT,IDirect3D9Ex**);
            const auto create9Ex=reinterpret_cast<Create9Ex>(GetProcAddress(GetModuleHandleW(L"d3d9.dll"),"Direct3DCreate9Ex"));
            if(!create9Ex) {std::cout<<"D3D9Ex is unavailable on this OS.\n";DestroyWindow(window);ImGui::DestroyContext();return 77;}
            Require(SUCCEEDED(create9Ex(D3D_SDK_VERSION,&d3dEx)),"D3D9Ex unavailable");
            d3d=d3dEx;
        } else d3d=Direct3DCreate9(D3D_SDK_VERSION);
        Require(d3d!=nullptr,"D3D9 unavailable");
        D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;
        pp.BackBufferWidth=640;pp.BackBufferHeight=480;pp.BackBufferFormat=D3DFMT_X8R8G8B8;
        IDirect3DDevice9* device=nullptr;
        HRESULT result;
        if(d3dEx) {
            IDirect3DDevice9Ex* deviceEx=nullptr;
            result=d3dEx->CreateDeviceEx(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,nullptr,&deviceEx);
            device=deviceEx;
        } else {
            result=d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&device);
            if(FAILED(result)) result=d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_REF,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&device);
        }
        Require(SUCCEEDED(result),"Hidden D3D9 device unavailable");
        ImGui_ImplDX9_Init(device);Panel::SetDevice(device);
        SetFonts(1);CheckPreviewTexture(device);CheckControls();CheckRampGeometry();CheckEditorNavigation();CheckTabContent();
        const std::filesystem::path output=argc>1?argv[1]:"palette-ui-smoke";
        std::filesystem::create_directories(output);
        CheckSources(output);CheckOpeningClicks(output);
        Panel::Configure(nullptr,nullptr,ApplyFixture,RestoreFixture);
        for(float scale:{0.7f,1.0f,1.5f}) {
            SetFonts(scale);CheckFontFit();Seed();
            for(int page=0;page<2;++page) {
                Panel::g.view=page?Panel::State::View::Files:Panel::State::View::Editor;BeginFrame();DrawPalettePage();EndFrame(device);
                Require(ImGui::GetDrawData()->TotalVtxCount>100,"Panel produced no geometry");
                Require(page==0 || scale<1 || Panel::g.scroll[page].maxScrollPx>0,"Long library controls did not expose scrolling");
                SaveBitmap(device,output/(std::to_string(int(scale*100))+"-"+std::to_string(page)+".bmp"));
                Panel::g.focus[page]=page==0?0:15;
                if(page==0) Panel::g.target=1;
                BeginFrame();DrawPalettePage();EndFrame(device);
                Require(Panel::g.scroll[page].maxScrollPx==0 || Panel::g.scroll[page].scrollPx>0,"Focused bottom row did not scroll into view");
                SaveBitmap(device,output/(std::to_string(int(scale*100))+"-"+std::to_string(page)+"-bottom.bmp"));
            }
        }
        if(argc>2) CaptureRealAssets(device,argv[2],output,argc>3?argv[3]:"assets/movedata");
        Require(Panel::g_workingTexture.value!=nullptr,"Sprite texture was not created");
        Panel::SetDevice(nullptr);
        Require(!Panel::g_workingTexture.value && !Panel::g_originalTexture.value,"Device identity change retained preview textures");
        Panel::SetDevice(device);
        ImGui_ImplDX9_InvalidateDeviceObjects();Panel::ReleaseTextures();
        Require(SUCCEEDED(device->Reset(&pp)),"Device reset failed after releasing editor resources");
        Panel::g.view=Panel::State::View::Editor;
        BeginFrame();DrawPalettePage();EndFrame(device);
        Require(Panel::g_workingTexture.value!=nullptr,"Preview texture did not recover after reset");
        Panel::ReleaseTextures();Require(!Panel::g_workingTexture.value && !Panel::g_originalTexture.value,"Preview textures were not released");
        Panel::Reset();ImGui_ImplDX9_Shutdown();ImGui::DestroyContext();device->Release();d3d->Release();DestroyWindow(window);
        std::cout<<"Palette UI controls, layout scales and preview textures passed.\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
