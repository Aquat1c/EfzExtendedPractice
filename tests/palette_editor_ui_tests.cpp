#include "../src/gui/custom_menu/palette_editor.cpp"
#include "gui/custom_menu/fonts.h"
#include "gui/imgui_impl.h"
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
bool inputSuspended=false;
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
void SetSuspended(bool value){inputSuspended=value;}
bool IsSuspended(){return inputSuspended;}
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
std::filesystem::path dialogSelection,dialogInitial;
std::string dialogError;
bool dialogAccepted=false,dialogSave=false;
int dialogCalls=0;
bool TestFileDialog(bool save,const std::filesystem::path& initial,std::filesystem::path& selected,std::string& error) {
    ++dialogCalls;dialogSave=save;dialogInitial=initial;error=dialogError;
    if(dialogAccepted) selected=dialogSelection;
    return dialogAccepted;
}

void Seed() {
    Panel::g=Panel::State{};
    Panel::g_fileDialog=TestFileDialog;
    dialogSelection.clear();dialogInitial.clear();dialogError.clear();dialogAccepted=false;dialogCalls=0;
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
    Panel::g.status=I18n::Tr("Palette changed on disk; open it again before saving.");
    Panel::g.liveStatus=I18n::Tr("Live preview applied");
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
            Require(dl->VtxBuffer[i].pos.y>=top-2 && dl->VtxBuffer[i].pos.y<=480,
                "Palette content escaped the shared content bounds");
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

std::filesystem::path CreateFixtureDirectory(const std::filesystem::path& output, const char* prefix) {
    const auto base=std::filesystem::absolute(output)/(std::string(prefix)+"-"+std::to_string(GetCurrentProcessId()));
    for (unsigned suffix=0;;++suffix) {
        auto candidate=base;
        if (suffix) candidate+="-"+std::to_string(suffix);
        std::error_code error;
        if (std::filesystem::create_directory(candidate,error)) return candidate;
        if (error) throw std::filesystem::filesystem_error("Could not create fixture directory",candidate,error);
    }
}
void CheckSources(const std::filesystem::path& directory) {
    contextFixture=CreateFixtureDirectory(directory,"fixture");
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
    Require(!Panel::g.document.Dirty() && !Panel::g.source,"Active game PAL became dirty or writable source");
    Require(Panel::g.document.Original()!=native,"Native import overwrote immutable stock baseline");
    Require(Panel::g_workingTexture.value==queuedTexture,"Source switch released a queued texture");
    Panel::g_fileDialog=TestFileDialog;dialogAccepted=true;dialogSelection=contextFixture/"UI Test.json";
    BeginFrame();Panel::Save();
    Require(dialogSave && Panel::g.source && Panel::g.source->path==dialogSelection && !Panel::g.document.Dirty(),
        "Saving a new palette did not use Save As");
    const int saveDialogs=dialogCalls;
    Panel::g.document.SetColor(1,{99,88,77});Panel::Save();
    Require(dialogCalls==saveDialogs && !Panel::g.document.Dirty(),"Plain Save reopened the dialog or failed");
    ::PaletteEditor::PaletteFile saved;std::string error;
    Require(::PaletteEditor::ReadPaletteFile(dialogSelection,saved,error) && saved.palette.colors[1]==::PaletteEditor::Color{99,88,77},
        "Plain Save did not persist colors");
    const auto originalEntry=*Panel::g.source;
    Panel::g.document.SetColor(2,{55,66,77});const auto copyColors=Panel::g.document.Colors();
    dialogSelection=contextFixture/"UI Test copy.json";Panel::SaveAs();
    Require(Panel::g.source->path==dialogSelection && Panel::g.document.State().name=="UI Test copy" && !Panel::g.document.Dirty(),
        "Save As did not create a separate named palette");
    const auto copyEntry=*Panel::g.source;
    Require(::PaletteEditor::ReadPaletteFile(copyEntry.path,saved,error) && saved.palette.colors==copyColors,"Save As colors did not round-trip");
    Require(::PaletteEditor::ReadPaletteFile(originalEntry.path,saved,error) && saved.palette.colors[2]!=copyColors[2],
        "Save As overwrote the original palette");
    Panel::g.document.SetColor(3,{22,44,88});const auto draft=Panel::g.document.Colors();
    dialogAccepted=false;Panel::SaveAs();
    Require(Panel::g.source->path==copyEntry.path && Panel::g.source->fingerprint==copyEntry.fingerprint &&
        Panel::g.document.State().name=="UI Test copy" && Panel::g.document.Colors()==draft && Panel::g.document.Dirty(),
        "Cancelled Save As changed source, name, or draft");
    dialogError="Dialog fixture failed";Panel::SaveAs();dialogError.clear();
    Require(Panel::g.source->path==copyEntry.path && Panel::g.document.Colors()==draft && Panel::g.document.Dirty(),
        "Failed Save As changed source or draft");
    dialogAccepted=true;dialogSelection=contextFixture/"unsupported.txt";Panel::SaveAs();
    Require(Panel::g.source->path==copyEntry.path && Panel::g.document.State().name=="UI Test copy" &&
        Panel::g.document.Colors()==draft && Panel::g.document.Dirty(),"Rejected Save As changed source, name, or draft");
    auto external=Panel::g.document.State();external.colors[1]={9,8,7};
    Require(::PaletteEditor::SavePaletteFile(copyEntry.path,external,copyEntry.fingerprint,saved,error),"External edit fixture failed");
    Panel::Save();
    Require(Panel::g.document.Dirty() && Panel::g.document.Colors()==draft && Panel::g.source->fingerprint==copyEntry.fingerprint,
        "Stale Save lost its draft or adopted changed disk contents");
    Require(::PaletteEditor::ReadPaletteFile(copyEntry.path,saved,error) && saved.palette.colors==external.colors,
        "Stale Save overwrote external changes");
    dialogAccepted=true;dialogSelection=contextFixture/"Malformed.json";WriteBytes(dialogSelection,{'{','}'});Panel::SaveAs();
    Require(Panel::g.source->path==dialogSelection && !Panel::g.document.Dirty() &&
        ::PaletteEditor::ReadPaletteFile(dialogSelection,saved,error) && saved.palette.colors==draft,
        "Approved Save As could not replace malformed destination");
    dialogSelection=contextFixture/"Native copy.pal";Panel::SaveAs();
    Require(Panel::g.source->native && !Panel::g.document.Dirty(),"Native Save As failed");
    Panel::g.document.SetColor(4,{25,50,75});const auto nativeDraft=Panel::g.document.Colors();Panel::Save();
    Require(::PaletteEditor::ReadPaletteFile(dialogSelection,saved,error) && saved.native && saved.palette.colors==nativeDraft,
        "Plain Save did not preserve native format");
    const auto nativeSource=*Panel::g.source;
    WriteBytes(contextFixture/"invalid.json",{'{','}'});
    Require(!Panel::LoadPalettePath(contextFixture/"invalid.json") && Panel::g.source->path==nativeSource.path &&
        Panel::g.document.Colors()==nativeDraft,"Failed load changed source or colors");
    dialogAccepted=false;Panel::LoadFile();
    Require(!dialogSave && Panel::g.source->path==nativeSource.path && Panel::g.document.Colors()==nativeDraft,
        "Cancelled Load changed source or colors");
    const auto baseline=Panel::g.document.Original();
    Require(Panel::LoadPalettePath(originalEntry.path) && Panel::g.source->path==originalEntry.path &&
        Panel::g.document.Colors()[1]==::PaletteEditor::Color{99,88,77},"External JSON load failed");
    Require(Panel::LoadPalettePath(nativeSource.path) && Panel::g.source->native && Panel::g.document.Colors()==nativeDraft &&
        Panel::g.document.Original()==baseline && !Panel::g.document.Dirty(),"Native load lost stock baseline or source");
    Panel::g.document.SetColor(2,{11,22,33});const auto retained=Panel::g.document.Colors();
    contextStage=2;Panel::Open(1);
    Require(Panel::g.context.stageId==2 && Panel::g.document.Colors()==retained,"Reopen did not refresh context while retaining draft");
    const auto englishMove=Panel::g.moveLabels[0];
    I18n::SetLanguage(I18n::Japanese);Panel::Open(1);DrawPalettePage();
    Require(Panel::g.document.Colors()==retained && Panel::g.moveLabels[0]!=englishMove,
        "Changing language did not refresh move labels while preserving the draft");
    I18n::SetLanguage(I18n::English);Panel::Open(1);DrawPalettePage();
    Require(Panel::g.document.Colors()==retained && Panel::g.moveLabels[0]==englishMove,
        "Returning to English lost the draft or retained Japanese move labels");
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
    const auto path=CreateFixtureDirectory(directory,"modal")/"Selected.json";
    ::PaletteEditor::PaletteFile saved;std::string error;
    Require(::PaletteEditor::SavePaletteFile(path,Panel::g.document.State(),std::nullopt,saved,error),"Modal fixture could not be saved");
    const auto savedColors=Panel::g.document.Colors();
    Panel::g.document.SetColor(3,{10,20,30});const auto draft=Panel::g.document.Colors();
    dialogAccepted=true;dialogSelection=path;
    BeginFrame();input.click=true;input.x=80;input.y=400;DrawPalettePage();
    Require(bool(Panel::g.confirm) && Panel::g.document.Dirty() && Panel::g.document.Colors()==draft,
        "Load click bypassed the draft guard or activated its new confirmation");
    NextFrame();input.back=true;DrawPalettePage();
    Require(!Panel::g.confirm && Panel::g.document.Colors()==draft,"Cancelling Load confirmation changed the draft");
    NextFrame();input.click=true;input.x=80;input.y=400;DrawPalettePage();
    NextFrame();Panel::g.confirmFocus=1;input.activate=true;DrawPalettePage();
    Require(!Panel::g.confirm && Panel::g.source && Panel::g.source->path==path && Panel::g.document.Colors()==savedColors,
        "Confirmed Load did not load the selected file");
    dialogAccepted=false;const int before=dialogCalls;
    NextFrame();input.click=true;input.x=320;input.y=400;DrawPalettePage();
    Require(dialogCalls==before+1 && dialogSave && !Panel::Modal() && Panel::g.source->path==path,
        "Save As action did not use the dialog or leaked cancellation");
    NextFrame();Panel::StartText(Panel::State::TextKind::Hex);
    input.click=true;input.x=320;input.y=400;DrawPalettePage();
    Require(Panel::g.textKind==Panel::State::TextKind::Hex && dialogCalls==before+1,
        "Bottom Save As clicked through a text modal");
    NextFrame();input.back=true;DrawPalettePage();EndFrame(nullptr);SetFonts(1);
}
void CaptureClasses(const std::filesystem::path& directory,const std::string& character) {
    using Cls=Mission::MoveData::Cls;
    const std::map<std::string,Cls> classes{{"system",Cls::System},{"a",Cls::A},{"b",Cls::B},{"c",Cls::C},{"command",Cls::Command},
        {"special",Cls::Special},{"super",Cls::Super},{"projectile",Cls::Projectile},{"super_entity",Cls::SuperEntity},{"entity",Cls::Entity}};
    testMoveClasses.clear();
    std::vector<std::string> profiles{character};if(::PaletteEditor::HasAlternateAnimations(character)) profiles.push_back(character+"2");
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
    unsigned largestCategory=0;int packs=0;
    for(const auto& entry:std::filesystem::directory_iterator(game)) {
        const auto character=entry.path().filename().string();
        if(!entry.is_directory() || !std::filesystem::exists(entry.path()/(character+".pat"))) continue;
        CaptureClasses(classifications,character);
        for(const auto variant:{::PaletteEditor::AnimationVariant::Standard,::PaletteEditor::AnimationVariant::Alternate}) {
            if(variant==::PaletteEditor::AnimationVariant::Alternate && !::PaletteEditor::HasAlternateAnimations(character)) continue;
            ::PaletteEditor::CharacterAssets assets;std::string error;
            Require(::PaletteEditor::LoadCharacterAssets(game,character,assets,error,variant),error.c_str());
            std::array<unsigned,7> categories{};
            for(const auto& animation:assets.animations)
                if(!animation.frames.empty()) ++categories[Panel::Category(Mission::MoveData::GetClass(0,animation.id))];
            largestCategory=(std::max)(largestCategory,*std::max_element(categories.begin(),categories.end()));
            ++packs;
        }
    }
    std::cout<<"Checked "<<packs<<" PAT tables; largest move category: "<<largestCategory<<'\n';
    Require(packs>0 && largestCategory>0,"No classified local animations were checked");
    for(const auto language:{I18n::English,I18n::Japanese}) {
    I18n::SetLanguage(language);
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
                state.target=target;state.autoFit=true;BeginFrame();DrawPalettePage();EndFrame(device);
                SaveBitmap(device,output/(std::string(language==I18n::Japanese?"ja-":"en-")+character+"-"+std::to_string(int(scale*100))+(target?"-ramps.bmp":"-editor.bmp")));
            }
            state.editorFocus=10;BeginFrame();input.activate=true;DrawPalettePage();EndFrame(device);
            Require(Screens::IsPopupActive(),"Real move picker did not open");
            BeginFrame();DrawPalettePage();EndFrame(device);
            SaveBitmap(device,output/(std::string(language==I18n::Japanese?"ja-":"en-")+character+"-"+std::to_string(int(scale*100))+"-moves.bmp"));
            Screens::ClosePopup();
            state.editorFocus=28;BeginFrame();input.activate=true;DrawPalettePage();EndFrame(device);
            BeginFrame();DrawPalettePage();EndFrame(device);
            SaveBitmap(device,output/(std::string(language==I18n::Japanese?"ja-":"en-")+character+"-"+std::to_string(int(scale*100))+"-tools.bmp"));
            Screens::ClosePopup();
        }
    }
    }
}
void CheckFontFit() {
    const std::pair<std::string,int> labels[]={{"TOOLS",76},{"PICKER",88},{"RGB",88},{"HSV",88},{"#FFFFFF",158},{"UNPIN",76},{"RESET",76},
        {"400%",96},{"PAN*",100},{"CENTER",108},{"ORIGINAL",110},{"MARK",80},{"PREVIEW",114},{"APPLY",112},{"RESTORE",112},
        {"ALT",64},{"SAVE",112},{"SAVE AS",112},{"LOAD",112},
        {I18n::TrFormat("UNDO %zu",size_t(16)),88},{I18n::TrFormat("Stock %d",6),100},
        {I18n::TrFormat("PAUSE  %d / %d",999,999),228}};
    for(const auto& label:labels) {
        const auto* translated=I18n::Tr(label.first.c_str());
        Require(Layout::MeasureTextW(bodyFont,bodyFont->FontSize,translated)<=label.second-16,
            (std::string(label.first)+" width="+std::to_string(Layout::MeasureTextW(bodyFont,bodyFont->FontSize,translated))+" limit="+std::to_string(label.second-16)).c_str());
    }
    for(const char* label:{"DEL","OK","ESC"})
        Require(Layout::MeasureTextW(bodyFont,bodyFont->FontSize,I18n::Tr(label))<=45,label);
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
        for(float x:{180.0f,350.0f}) {
            input={};input.x=x;input.y=90;input.click=true;
            Panel::DrawEditor(ImGui::GetForegroundDrawList(),72,true);
            Require(Screens::IsPopupActive(),"Bottom of editor picker button did not open its dropdown");
            Screens::ClosePopup();
        }
        EndFrame(nullptr);
    }
    SetFonts(1);Seed();Panel::g.colorControls=Panel::State::ColorControls::RGB;Panel::g.target=0;Panel::g.color=5;Panel::SyncColor();Panel::g.editorFocus=3;
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
    input={};input.rightHeld=input.rightClick=true;input.x=28+5.5f*31.5f;input.y=130;
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
    Panel::g.document.SetColor(5,{210,90,20});const auto draft=Panel::g.document.Colors();
    for(float x:{100.0f,550.0f}) {
        input={};input.click=true;input.x=x;input.y=10;DrawPalettePage(false);
        Require(!Panel::g.confirm && !Panel::ConsumeCloseRequest() && Panel::g.player==1,
            "Palette retained a hit target in the shared header");
    }
    Panel::g.editorFocus=3;input={};input.right=true;DrawPalettePage(false);
    Require(Panel::g.document.Colors()==draft,"Tab focus adjusted palette content");
    input={};input.click=true;input.x=80;input.y=76;DrawPalettePage(false);
    Require(!Panel::g.confirm && Panel::g.player==1,"Disabled content accepted a player switch click");
    input={};input.click=true;input.x=80;input.y=76;DrawPalettePage();
    Require(bool(Panel::g.confirm),"Content player selector ignored a mouse click");
    NextFrame();input.back=true;DrawPalettePage();
    Require(!Panel::g.confirm && Panel::g.document.Colors()==draft,"Player switch cancellation lost the draft");
    NextFrame();input.click=true;input.x=320;input.y=400;DrawPalettePage();
    Require(dialogCalls==1 && dialogSave && !Panel::Modal() && Panel::g.document.Colors()==draft,
        "Bottom Save As did not use the dialog or retain cancelled draft");
    NextFrame();input.back=true;DrawPalettePage();
    Require(Panel::ConsumeCloseRequest() && Panel::IsOpen() && Panel::g.document.Colors()==draft && !Panel::g.confirm,
        "Editor Back lost the draft or failed to return to game");
    NextFrame();Panel::g.editorFocus=22;input.up=true;DrawPalettePage();
    Require(Panel::ConsumeFocusAboveRequest() && !Panel::ConsumeCloseRequest(),"Player-row Up did not focus shell tabs");
    NextFrame();Panel::g.editorFocus=0;Panel::g.panMode=true;input.back=true;DrawPalettePage();
    Require(!Panel::g.panMode && !Panel::ConsumeCloseRequest(),"Back while panning closed the menu");
    Panel::StartText(Panel::State::TextKind::Hex);
    const int before=dialogCalls;
    input={};input.click=true;input.x=320;input.y=400;DrawPalettePage();
    Require(Panel::BlocksNavigation() && dialogCalls==before,"Save As clicked through text entry");
    NextFrame();input.back=true;DrawPalettePage();
    Require(!Panel::BlocksNavigation() && !input.back,"Closing text entry leaked its input");
    Panel::Confirm("TEST","Keep draft?","DISCARD",[]{});
    Require(Panel::BlocksNavigation(),"Confirmation did not block shell navigation");
    NextFrame();input.click=true;input.x=100;input.y=280;DrawPalettePage();
    Require(!Panel::BlocksNavigation() && Panel::g.document.Colors()==draft,"Closing confirmation changed the draft");
    Panel::OpenEditorChoice(Screens::DropdownRow("Move",&Panel::g.moveChoice,Panel::g.moveOptions.data(),int(Panel::g.moveOptions.size()),Panel::MoveChoiceChanged));
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
void CheckColorPicker() {
    using Pick=Panel::State::PickerInput;
    Seed();BeginFrame();
    Panel::g.target=0;Panel::g.color=5;
    Panel::g.document.SetHsv(5,{0,100,100});Panel::SyncColor();
    const auto before=Panel::g.document.Colors();const auto undos=Panel::g.document.UndoCount();
    auto& io=ImGui::GetIO();auto* dl=ImGui::GetForegroundDrawList();
    const auto draw=[&]{Panel::DrawColorPicker(dl,28,200,252,true);};
    io.MouseDown[0]=true;input.click=true;input.x=260;input.y=206+72.0f*120/359;
    draw();
    Require(Panel::g.pickerInput==Pick::DragHue && Panel::g.document.Colors()[5]==::PaletteEditor::Color{0,255,0},
        "Visual hue picker did not recolor the selected shade");
    for(unsigned i=0;i<40;++i) if(i!=5) Require(Panel::g.document.Colors()[i]==before[i],"Shade picker recolored another entry");
    input.click=false;input.y=1000;draw();
    Require(Panel::g.document.ColorHsv(5).h==359,"Hue drag did not clamp below the picker");
    input.y=-1000;draw();Require(Panel::g.document.ColorHsv(5).h==0,"Hue drag did not clamp above the picker");
    io.MouseDown[0]=false;input={};draw();
    Require(Panel::g.pickerInput==Pick::Idle && Panel::g.document.UndoCount()==undos+1,"Hue drag did not end as one undo gesture");
    Panel::g.document.Undo();Require(Panel::g.document.Colors()==before,"Hue picker undo lost the shade baseline");
    Panel::SyncColor();io.MouseDown[0]=true;input.click=true;input.x=100;input.y=230;draw();
    Require(Panel::g.pickerInput==Pick::DragSV,"SV picker did not capture drag");
    input.click=false;input.x=-1000;input.y=1000;draw();
    Require(Panel::g.document.ColorHsv(5).s==0 && Panel::g.document.ColorHsv(5).v==0,"SV drag did not clamp to black");
    input.x=1000;input.y=-1000;draw();
    Require(Panel::g.document.ColorHsv(5).s==100 && Panel::g.document.ColorHsv(5).v==100,"SV drag lost capture beyond its bounds");
    io.MouseDown[0]=false;input={};draw();
    Require(Panel::g.document.UndoCount()==undos+1,"SV drag created multiple undo checkpoints");
    Panel::g.document.Undo();Require(Panel::g.document.Colors()==before,"SV undo lost baseline");
    for(const auto hsv:{::PaletteEditor::Hsv{73,0,50},::PaletteEditor::Hsv{73,88,0}}) {
        Panel::g.document.SetHsv(5,hsv);Panel::SyncColor();const auto baseline=Panel::g.document.Colors();
        const auto count=Panel::g.document.UndoCount();Panel::g.editorFocus=4;
        input.activate=true;draw();Require(Panel::g.pickerInput==Pick::AdjustHue,"Confirm did not start keyboard hue adjustment");
        input={};input.right=true;draw();input={};input.right=true;draw();
        Require(Panel::g.document.ColorHsv(5).h==75 && Panel::g.document.Colors()==baseline,
            "Hue-only keyboard edit lost black or gray HSV state");
        input={};input.back=true;Panel::DrawEditor(dl,72,true);
        Require(Panel::g.pickerInput==Pick::Idle && !input.back && !Panel::ConsumeCloseRequest(),"Picker Back leaked into menu close");
        Require(Panel::g.document.UndoCount()==count+1,"Keyboard hue adjustment created multiple undo checkpoints");
        Panel::g.document.Undo();Require(Panel::g.document.ColorHsv(5).h==73,"Hue-only undo lost retained HSV");
    }
    Panel::g.document.SetHsv(5,{120,40,40});Panel::SyncColor();Panel::g.editorFocus=3;
    input={};input.activate=true;draw();input={};input.right=input.up=true;draw();
    Require(Panel::g.document.ColorHsv(5).s==41 && Panel::g.document.ColorHsv(5).v==41,"Keyboard SV adjustment did not change both axes");
    input={};input.back=true;draw();EndFrame(nullptr);
    Seed();BeginFrame();dl=ImGui::GetForegroundDrawList();
    auto state=Panel::g.document.State();state.ramps={{0,39,20}};std::string error;
    Require(Panel::g.document.LoadSaved(state,state.baseline,error),"Ramp picker fixture invalid");
    Panel::g.document.SetPinned(5,true);Panel::g.document.SetRampHsv(0,{0,100,100});
    Panel::g.target=1;Panel::g.ramp=0;Panel::SyncColor();
    const auto pinned=Panel::g.document.Colors()[5];const auto pinnedHsv=Panel::g.document.ColorHsv(5);
    const auto rampBefore=Panel::g.document.Colors();const auto rampUndos=Panel::g.document.UndoCount();
    io.MouseDown[0]=true;input.click=true;input.x=260;input.y=206+72.0f*120/359;draw();
    Require(Panel::g.document.RampColor(0)==::PaletteEditor::Color{0,255,0} && Panel::g.document.Colors()[20]==::PaletteEditor::Color{0,255,0},
        "Ramp hue picker did not apply the ramp target");
    Require(Panel::g.document.Pinned(5) && Panel::g.document.Colors()[5]==pinned && Panel::g.document.ColorHsv(5).h==pinnedHsv.h,
        "Ramp picker changed a pinned shade or its HSV state");
    io.MouseDown[0]=false;input={};draw();
    Require(Panel::g.document.UndoCount()==rampUndos+1,"Ramp picker did not create one undo checkpoint");
    Panel::g.document.Undo();Require(Panel::g.document.Colors()==rampBefore,"Ramp picker undo lost pinned composition");
    Panel::g.animation=0;Panel::g.frame=0;Panel::g.playing=false;Panel::g.zoom=100;Panel::g.autoFit=false;
    const auto pick=[&](float x,float y){input={};input.click=true;input.x=x;input.y=y;Panel::DrawPreview(dl,28,90,200,120,true);};
    pick(128,184);Require(Panel::g.target==1,"Sprite eyedropper silently changed Ramp mode to Shade mode");
    const auto ramp=Panel::g.document.Ramps()[Panel::g.ramp];
    Require(ramp.first<=1 && ramp.last>=1,"Sprite eyedropper selected the wrong ramp");
    Panel::g.target=0;Panel::g.color=5;Panel::SyncColor();pick(128,184);
    Require(Panel::g.target==0 && Panel::g.color==1,"Shade eyedropper did not select its clicked shade");
    const auto pickedColors=Panel::g.document.Colors();pick(127,184);
    Require(Panel::g.color==1 && Panel::g.document.Colors()==pickedColors,"Transparent eyedropper changed selection or colors");
    Panel::g.assets.sprites[0].indices[1]=40;pick(128,184);
    Require(Panel::g.color==1 && Panel::g.document.Colors()==pickedColors,"Shared-effect eyedropper changed editable palette");
    EndFrame(nullptr);
}
void CheckIconClicks() {
    Seed();BeginFrame();Panel::g.target=0;Panel::g.color=3;Panel::SyncColor();
    const auto original=Panel::g.document.Colors()[3];
    const auto click=[&](int column,int focus) {
        input={};input.click=true;input.x=46+36.0f*column;input.y=339;
        Panel::g.mouseHelp=true;Panel::g.helpControl=-1;
        Panel::DrawEditor(ImGui::GetForegroundDrawList(),72,true);
        Require(Panel::g.helpControl==focus,"Square icon hover missed its description");
        Panel::SyncColor();
    };
    click(0,6);Require(Panel::g.document.Pinned(3),"Pin icon missed its hit target");
    click(1,7);Require(!Panel::g.document.Pinned(3),"Reset icon missed its hit target");
    click(2,8);Require(Panel::g.document.Pinned(3),"Undo icon missed its hit target");
    click(5,31);click(3,29);
    Require(Panel::g.document.Colors()[3].r==(std::min)(255,int(original.r)+16),"Lighten icon missed its hit target");
    click(6,32);Require(Panel::g.document.Colors()[3]==original,"Paste icon missed its hit target");
    click(4,30);Require(Panel::g.document.Colors()[3].r==(std::max)(0,int(original.r)-16),"Darken icon missed its hit target");
    EndFrame(nullptr);
}
void CheckTools() {
    Seed();BeginFrame();
    const auto openTool=[&](int choice) {
        Panel::g.editorFocus=28;NextFrame();input.activate=true;DrawPalettePage();
        Require(Screens::IsPopupActive(),"Palette Tools button did not open");
        const auto colors=Panel::g.document.Colors();
        for(int i=0;i<choice;++i) {NextFrame();input.down=true;DrawPalettePage();}
        Require(Panel::g.document.Colors()==colors,"Tool navigation applied an edit before confirmation");
        const char* help=Screens::CurrentPopupHelpText();
        Require(help && std::string(help)==Panel::ColorToolHelp(Screens::Row{},choice),"Tool lacks focused description");
    };
    const auto runTool=[&](int choice){
        if(choice<4) {openTool(choice);NextFrame();input.activate=true;DrawPalettePage();}
        else {Panel::g.editorFocus=choice+25;NextFrame();input.activate=true;DrawPalettePage();}
        Require(!Screens::IsPopupActive(),"Tool action left a popup open");
    };
    const auto original=Panel::g.document.Colors();const auto undos=Panel::g.document.UndoCount();
    runTool(7);Require(Panel::g.document.Colors()==original && Panel::g.document.UndoCount()==undos,"Empty paste changed the draft");
    openTool(0);NextFrame();input.back=true;DrawPalettePage();
    Require(Panel::g.document.Colors()==original && !Panel::ConsumeCloseRequest(),"Cancelling tools edited colors or closed the menu");
    runTool(0);Require(Panel::g.document.Colors()!=original && Panel::g.document.Colors()[0]==original[0] &&
        Panel::g.document.UndoCount()==undos+1,"Generate did not update the palette in one protected edit");
    int player=0;std::string resource;::PaletteEditor::Palette live;
    Require(Panel::GetLivePreview(player,resource,live) && live==Panel::g.document.Colors(),"Generated palette missed the live preview");
    Panel::g.document.Undo();Panel::g.target=0;Panel::g.color=4;Panel::SyncColor();
    runTool(6);const auto copied=Panel::g.document.Colors()[4];
    Panel::g.color=5;Panel::SyncColor();runTool(7);
    Require(Panel::g.document.Colors()[5]==copied,"Copy/paste shade failed");
    for(unsigned i=0;i<40;++i) if(i!=5) Require(Panel::g.document.Colors()[i]==original[i],"Paste changed another shade");
    Panel::g.document.Undo();Panel::g.target=1;Panel::g.ramp=0;Panel::SyncColor();runTool(7);
    Require(Panel::g.document.RampColor(0)==copied,"Paste did not target the selected ramp");
    Panel::g.document.Undo();Panel::g.target=0;Panel::g.color=4;Panel::SyncColor();runTool(4);
    Require(Panel::g.document.Colors()[4].r==(std::min)(255,int(original[4].r)+16),"Lighten does not use reference step");
    Panel::g.document.Undo();runTool(5);
    Require(Panel::g.document.Colors()[4].r==(std::max)(0,int(original[4].r)-16),"Darken does not use reference step");
    Panel::g.document.Undo();runTool(1);
    Require(Panel::g.document.Colors()[4]!=original[4],"Selected shade randomization was not wired");
    Panel::g.document.Undo();auto state=Panel::g.document.State();state.ramps={{0,4,2},{5,39,20}};
    state.colors[1]={255,0,0};state.colors[4]={0,0,255};std::string error;
    Require(Panel::g.document.LoadSaved(state,state.baseline,error),"UI gradient fixture");
    Panel::g.color=2;Panel::SyncColor();runTool(2);
    Require(Panel::g.document.Colors()[2]==::PaletteEditor::Color{170,0,85},"RGB gradient did not find the selected shade's ramp");
    Panel::g.document.Undo();runTool(3);
    Require(Panel::g.document.Colors()[2]==::PaletteEditor::Color{255,0,170},"HSV gradient action was not wired");
    EndFrame(nullptr);
}
void CheckLargeMoveCategory() {
    Seed();Panel::g.assets.animations.clear();
    for(int id=1000;id<1200;++id) Panel::g.assets.animations.push_back({id,{{0,4,1,2,2,2}}});
    Panel::BuildMoveChoices();Panel::g.moveChoice=199;Panel::MoveChoiceChanged();Panel::g.editorFocus=10;
    BeginFrame();input.activate=true;DrawPalettePage();
    Require(Screens::IsPopupActive(),"Large move category did not open");
    NextFrame();input.activate=true;DrawPalettePage();
    Require(!Screens::IsPopupActive() && Panel::g.animation==199,"Categorized popup truncated its final move");
    EndFrame(nullptr);
}
void CheckClosingInput() {
    Seed();BeginFrame();Panel::EditHex();NextFrame();Panel::g.text.clear();
    const auto text=Panel::g.text;const auto colors=Panel::g.document.Colors();
    Input::SetSuspended(true);ImGui::GetIO().InputQueueCharacters.push_back(ImWchar('1'));
    DrawPalettePage();
    Require(Panel::g.text==text && Panel::g.document.Colors()==colors,"Closing menu still accepted raw hex text input");
    Input::SetSuspended(false);Panel::g.textKind=Panel::State::TextKind::None;EndFrame(nullptr);
}
void CheckRampHoverHelp() {
    Seed();BeginFrame();Panel::g.editorFocus=24;Panel::g.helpControl=-1;Panel::g.mouseHelp=true;
    input.x=30;input.y=102;
    Panel::DrawRampGrid(ImGui::GetForegroundDrawList(),28,100,252,80,true);
    Require(Panel::g.helpControl==0,"Ramp swatch hover retained unrelated Save description");
    const auto geometry=Panel::RampGridGeometry(Panel::g.document.Ramps(),Panel::g.ramp,80);
    Panel::g.helpControl=-1;input.y=100+geometry.shadeY+2;
    Panel::DrawRampGrid(ImGui::GetForegroundDrawList(),28,100,252,80,true);
    Require(Panel::g.helpControl==0,"Shade swatch hover retained unrelated Save description");
    EndFrame(nullptr);
}
void CheckControls() {
    Seed();Panel::g.colorControls=Panel::State::ColorControls::RGB;BeginFrame();
    Panel::g.target=0;Panel::g.color=5;Panel::SyncColor();
    const auto before=Panel::g.document.Colors();

    auto& io=ImGui::GetIO();
    const float right=28+27+284-76;
    input.x=28+27;input.y=110;
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
    Require(Panel::g.moveLabels[1].find("105 - ")==0 && Panel::g.moveIndices==std::vector<int>{0,1},
        "Classified move picker did not retain all drawable PAT indices");
    Panel::g.assets.animations.push_back({999,{{0,4,1,2,2,2}}});
    Panel::g.assets.animations.push_back({233,{}});Panel::BuildMoveChoices();
    Require(Panel::g.moveIndices==std::vector<int>{0,1,2} && Panel::g.choiceCategories.size()==Panel::g.moveIndices.size(),
        "Single move picker omitted a class or included an empty animation");
    for(unsigned i=0;i<Panel::g.moveIndices.size();++i)
        Require(Panel::g.choiceCategories[i]==Panel::g.moveCategories[Panel::g.moveIndices[i]],"Move category lost its animation mapping");
    Require(Panel::g.moveLabels[2]=="999 - Unclassified" && Panel::g.moveLabels[0]!="0 - ","Move fallback label was blank");
    Panel::g.moveChoice=99;Panel::MoveChoiceChanged();Require(Panel::g.animation==2,"Move selection was not clamped");
    Panel::StartText(Panel::State::TextKind::Hex);Panel::g.text.clear();
    NextFrame();input.activate=true;Panel::DrawTextEditor(ImGui::GetForegroundDrawList());
    Require(Panel::g.text=="0","Controller keyboard did not enter selected hex digit");
    ImGui::GetIO().InputQueueCharacters.push_back('B');input.activate=input.back=true;
    Panel::DrawTextEditor(ImGui::GetForegroundDrawList());
    Require(Panel::g.text=="0B" && Panel::g.textKind==Panel::State::TextKind::Hex,"Typed fighter bindings activated or cancelled the hex editor");
    input={};EndFrame(nullptr);
    ImGui::GetIO().AddInputCharactersUTF8("cdef0123456789z");BeginFrame();
    Panel::DrawTextEditor(ImGui::GetForegroundDrawList());
    Require(Panel::g.text.size()==7 && Panel::g.text.find('z')==std::string::npos,"Hex input limit or character validation failed");
    Panel::g.text="#123456";Panel::CommitText();
    Require(Panel::g.textKind==Panel::State::TextKind::None && std::string(Panel::HexValue())=="#123456","Hex color commit failed");
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
        SetFonts(1);CheckPreviewTexture(device);CheckColorPicker();CheckIconClicks();CheckTools();CheckLargeMoveCategory();CheckClosingInput();CheckRampHoverHelp();CheckControls();CheckRampGeometry();CheckEditorNavigation();CheckTabContent();
        const std::filesystem::path output=argc>1?argv[1]:"palette-ui-smoke";
        std::filesystem::create_directories(output);
        CheckSources(output);
        for (int attempt=0;attempt<2;++attempt) CheckOpeningClicks(output);
        Panel::Configure(nullptr,nullptr,ApplyFixture,RestoreFixture);
        for(const auto language:{I18n::English,I18n::Japanese}) {
            I18n::SetLanguage(language);
            for(float scale:{0.7f,1.0f,1.25f,1.5f}) {
                if(language==I18n::Japanese && scale<1) continue;
                SetFonts(scale);CheckFontFit();Seed();
                for(int target:{0,1}) {
                    Panel::g.target=target;Panel::g.editorFocus=target?19:3;
                    BeginFrame();DrawPalettePage();EndFrame(device);
                    Require(ImGui::GetDrawData()->TotalVtxCount>100,"Panel produced no geometry");
                    SaveBitmap(device,output/(std::string(language==I18n::Japanese?"ja-":"en-")+std::to_string(int(scale*100))+(target?"-ramps.bmp":"-shades.bmp")));
                }
            }
        }
        I18n::SetLanguage(I18n::English);SetFonts(1);
        if(argc>2) CaptureRealAssets(device,argv[2],output,argc>3?argv[3]:"assets/movedata");
        Require(Panel::g_workingTexture.value!=nullptr,"Sprite texture was not created");
        Panel::SetDevice(nullptr);
        Require(!Panel::g_workingTexture.value && !Panel::g_originalTexture.value,"Device identity change retained preview textures");
        Panel::SetDevice(device);
        BeginFrame();DrawPalettePage();EndFrame(device);
        Require(Panel::g_workingTexture.value!=nullptr,"Reset fixture has no live preview texture");
        ImGuiImpl::InvalidateDeviceObjects();
        Require(!Panel::g_workingTexture.value && !Panel::g_originalTexture.value,"Device invalidation retained preview textures");
        Require(SUCCEEDED(device->Reset(&pp)),"Device reset failed after releasing editor resources");
        BeginFrame();DrawPalettePage();EndFrame(device);
        Require(Panel::g_workingTexture.value!=nullptr,"Preview texture did not recover after reset");
        Panel::ReleaseTextures();Require(!Panel::g_workingTexture.value && !Panel::g_originalTexture.value,"Preview textures were not released");
        Panel::Reset();ImGui_ImplDX9_Shutdown();ImGui::DestroyContext();device->Release();d3d->Release();DestroyWindow(window);
        std::cout<<"Palette UI controls, layout scales and preview textures passed.\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
