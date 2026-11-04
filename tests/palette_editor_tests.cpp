#include "game/palette_editor.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <nlohmann/json.hpp>
using namespace PaletteEditor;
namespace {
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
Palette Stock() {
    Palette p{};
    for (unsigned i=0;i<40;++i) {
        const auto step=std::uint8_t(35+(i%5)*40);
        p[i]=(i/5)%2 ? Color{step,30,30} : Color{step,step,step};
    }
    return p;
}
void ColorTests() {
    Color color{1,2,3};
    Check(ParseHex(" #aBc ",color)&&color==Color{170,187,204},"three digit hex");
    Check(FormatHex(color)=="#AABBCC","canonical hex");
    const auto before=color;
    Check(!ParseHex("#12345z",color)&&color==before,"hex failure atomicity");
    for(unsigned r=0;r<256;r+=17)for(unsigned g=0;g<256;g+=17)for(unsigned b=0;b<256;b+=17) {
        Color rgb{std::uint8_t(r),std::uint8_t(g),std::uint8_t(b)};
        Check(HsvToRgb(RgbToHsv(rgb))==rgb,"HSV round trip");
    }
    Check(HsvToRgb({-120,100,100})==Color{0,0,255},"negative hue wraps");
    Document d;d.LoadStock("misaki",0,Stock());
    Check(d.SetHsv(0,{123,0,40}),"gray HSV");
    Check(d.ColorHsv(0).h==123,"gray hue retained");
    Check(d.SetHsv(0,{234,87,0})&&d.ColorHsv(0).s==87,"black saturation retained");
    Check(d.SetHsv(0,{234,87,50}),"color from black");
    Check(!d.SetColor(40,{})&&!d.SetRampColor(100,{}),"invalid selection");
}
void ScopedHsvTests() {
    Document d;d.LoadStock("misaki",0,Stock());
    const auto key=d.Ramps()[0].key;
    d.SetColor(key,{210,20,40});const auto pinned=d.ColorHsv(key);
    d.SetRampHsv(0,{180,100,60});
    Check(d.Colors()[key]==Color{210,20,40}&&d.ColorHsv(key).h==pinned.h&&d.ColorHsv(key).s==pinned.s,"pinned key keeps entry HSV");
    Check(d.RampHsv(0).h==180&&d.RampColor(0)==HsvToRgb({180,100,60}),"ramp target has independent HSV and RGB");
    d.SetHsv(0,{20,70,0});const auto before=d.UndoCount();
    d.BeginGesture();d.SetHsv(0,{90,40,0});d.SetHsv(0,{140,90,0});d.EndGesture();
    Check(d.UndoCount()==before+1&&d.ColorHsv(0).h==140,"black hue-only gesture checkpoint");
    d.BeginGesture();d.SetHsv(0,{210,10,0});d.SetHsv(0,{250,20,0});d.EndGesture();
    Check(d.Undo()&&d.ColorHsv(0).h==140&&d.ColorHsv(0).s==90,"black second gesture undo");
    Check(d.Undo()&&d.ColorHsv(0).h==20&&d.ColorHsv(0).s==70,"black first gesture undo");
    d.SetHsv(0,{10,0,50});d.BeginGesture();d.SetHsv(0,{80,0,50});d.SetHsv(0,{150,0,50});d.EndGesture();
    Check(d.Undo()&&d.ColorHsv(0).h==10,"gray hue-only gesture undo");
    d.SetRampHsv(0,{30,90,0});
    d.BeginGesture();d.SetRampHsv(0,{120,30,0});d.SetRampHsv(0,{160,70,0});d.EndGesture();
    Check(d.Undo()&&d.RampHsv(0).h==30&&d.RampHsv(0).s==90,"ramp black hue-only gesture undo");
    Check(d.ColorHsv(key).h==pinned.h,"ramp undo keeps pinned entry HSV");
}
void DocumentTests() {
    const auto stock=Stock(); Document d;d.LoadStock("misaki",2,stock);
    Check(!d.Dirty()&&d.Original()==stock,"stock is immutable");
    Check(d.Ramps().front().first==0&&d.Ramps().front().last==4,"conservative contiguous gray ramp");
    Check(d.SetPinned(0,true),"pin shade");const auto pinned=d.Colors()[0];
    Check(d.SetRampColor(0,{30,100,200}),"dominant edit");
    Check(d.Colors()[0]==pinned&&d.Colors()[d.Ramps()[0].key]==Color{30,100,200},"pin preserved, dominant exact");
    Check(d.Original()==stock,"ramp cannot edit baseline");
    Check(d.ResetEntry(0)&&!d.Pinned(0)&&d.Colors()[0]!=pinned,"reset follows target ramp");
    Check(d.ResetRamp(0)&&d.Colors()==stock,"ramp reset");
    d.MarkSaved();d.BeginGesture();
    for(unsigned i=0;i<30;++i)d.SetColor(2,{std::uint8_t(i),70,90});
    d.EndGesture();
    const auto count=d.UndoCount();
    Check(d.Undo()&&d.Colors()==stock&&d.UndoCount()+1==count,"one gesture one undo");
    for(unsigned i=0;i<30;++i)d.SetColor(2,{std::uint8_t(i),20,90});
    Check(d.UndoCount()==16,"bounded undo");
    unsigned undos=0;while(d.Undo())++undos;Check(undos==16,"all bounded history usable");
    d.Revert();Check(!d.Dirty()&&d.Colors()==stock,"revert loaded document");
    d.SetColor(4,{1,2,3});d.MarkSaved();d.SetColor(4,{4,5,6});d.Revert();
    Check(d.Colors()[4]==Color{1,2,3},"revert custom checkpoint");
    const auto saved=d.State();auto altered=stock;altered[39]={1,8,9};
    std::string error;Check(d.LoadSaved(saved,altered,error),"rebase load");
    Check(d.Colors()==saved.colors&&d.Pinned(4)&&d.Pinned(39),"rebase preserves RGB and pins differences");
    Check(std::none_of(d.State().targets.begin(),d.State().targets.end(),[](const auto& t){return bool(t);}),"rebase drops targets");
    auto imported=stock;imported[0]={9,8,7};d.Import(imported);
    Check(d.Colors()==imported&&d.Pinned(0),"native import");
    d.ResetAll();Check(d.Colors()==altered,"reset all uses current baseline");
}
void Write(const std::filesystem::path& path,const std::string& bytes) {
    std::ofstream out(path,std::ios::binary|std::ios::trunc);out<<bytes;out.close();Check(bool(out),"write fixture");
}
void PaletteFileTests(const std::filesystem::path& root) {
    Document d;d.LoadStock("misaki",3,Stock());
    d.SetName("Old name");d.SetRampColor(0,{20,50,180});d.SetPinned(0,true);
    const auto path=root/"user selected folder"/"Selected name.JSON";
    PaletteFile saved,loaded;std::string error;
    Check(SavePaletteFile(path,d.State(),std::nullopt,saved,error),"external JSON create");
    Check(saved.path==path&&!saved.native&&saved.palette.name=="Selected name","selected filename sets name");
    Check(ReadPaletteFile(path,loaded,error)&&loaded.fingerprint==saved.fingerprint&&
        loaded.palette.name=="Selected name"&&loaded.palette.character=="misaki"&&loaded.palette.stockSlot==3&&
        loaded.palette.baseline==d.Original()&&loaded.palette.colors==d.Colors()&&
        loaded.palette.pins==d.State().pins&&loaded.palette.targets==d.State().targets&&
        loaded.palette.ramps.size()==d.Ramps().size(),"external JSON metadata round trip");
    Document reopened;
    Check(reopened.LoadSaved(loaded.palette,Stock(),error)&&reopened.Colors()==d.Colors(),"external JSON loads document");
    auto result=saved;
    Check(!SavePaletteFile(path,d.State(),std::nullopt,result,error)&&!error.empty()&&result.fingerprint==saved.fingerprint,
        "external create collision leaves result");
    d.SetColor(2,{4,6,8});
    Check(SavePaletteFile(path,d.State(),saved.fingerprint,result,error)&&result.fingerprint!=saved.fingerprint,
        "external replacement updates fingerprint");
    Check(!SavePaletteFile(path,saved.palette,saved.fingerprint,loaded,error)&&!error.empty(),"stale save refused");
    Check(ReadPaletteFile(path,loaded,error)&&loaded.palette.colors==d.Colors(),"stale save preserves disk");
    const auto native=root/"user selected folder"/"Native colors.PAL";
    Check(SavePaletteFile(native,d.State(),std::nullopt,saved,error)&&saved.native,"external native create");
    Check(std::filesystem::file_size(native)==121,"external native format size");
    Check(ReadPaletteFile(native,loaded,error)&&loaded.native&&loaded.fingerprint==saved.fingerprint&&
        loaded.palette.colors==d.Colors()&&loaded.palette.character.empty()&&loaded.palette.name=="Native colors",
        "external native round trip has colors without invented metadata");
    auto changed=d.State();changed.colors[0]={7,8,9};
    Check(!SavePaletteFile(native,changed,std::nullopt,result,error),"external native collision refused");
    Check(SavePaletteFile(native,changed,saved.fingerprint,result,error),"external native replacement");
    Check(!SavePaletteFile(native,d.State(),saved.fingerprint,result,error),"external native stale save refused");
    Check(ReadPaletteFile(native,loaded,error)&&loaded.palette.colors==changed.colors,"native stale save preserves disk");
    const auto before=loaded;
    Write(native,"bad");
    Check(!ReadPaletteFile(native,loaded,error)&&!error.empty()&&loaded.fingerprint==before.fingerprint&&
        loaded.palette.colors==before.palette.colors,"malformed native leaves output");
    Write(path,"{}");
    Check(!ReadPaletteFile(path,loaded,error)&&!error.empty()&&loaded.fingerprint==before.fingerprint,
        "malformed JSON leaves output");
    std::uint64_t identity=0;
    Check(ReadPaletteFileIdentity(path,identity,error)&&SavePaletteFile(path,d.State(),identity,result,error)&&
        ReadPaletteFile(path,loaded,error)&&loaded.palette.colors==d.Colors(),"explicit malformed JSON replacement");
    Check(ReadPaletteFileIdentity(native,identity,error)&&SavePaletteFile(native,d.State(),identity,result,error)&&
        ReadPaletteFile(native,loaded,error)&&loaded.palette.colors==d.Colors(),"explicit malformed native replacement");
    const auto unicodeName=std::string(40,'a')+u8"\u9752\u8272\u306e\u30d1\u30ec\u30c3\u30c8";
    const auto unicodePath=root/std::filesystem::u8path(u8"\u30d1\u30ec\u30c3\u30c8")/std::filesystem::u8path(unicodeName+".json");
    Check(SavePaletteFile(unicodePath,d.State(),std::nullopt,saved,error)&&ReadPaletteFile(unicodePath,loaded,error)&&
        loaded.palette.name==unicodeName&&loaded.palette.colors==d.Colors(),"long Japanese filename round trip");
    Check(reopened.LoadSaved(loaded.palette,Stock(),error)&&reopened.State().name==unicodeName,
        "document accepts long Japanese display name");
    Library legacy(root/"legacy");LibraryEntry legacyEntry;
    Check(!legacy.SaveNew(loaded.palette,legacyEntry,error),"legacy library keeps name length restriction");
    auto badName=loaded.palette;badName.name="bad\nname";
    Check(!reopened.LoadSaved(badName,Stock(),error),"document rejects control characters in display name");
    badName.name=std::string(1,char(-1));
    Check(!reopened.LoadSaved(badName,Stock(),error),"document rejects invalid UTF-8 display name");
    Write(path,std::string(65537,'x'));
    Check(!ReadPaletteFile(path,loaded,error),"oversized external file refused");
    const auto previousIdentity=identity;
    Check(!ReadPaletteFileIdentity(path,identity,error)&&identity==previousIdentity,
        "oversized identity probe leaves fingerprint");
    Check(!SavePaletteFile(path,d.State(),previousIdentity,result,error)&&std::filesystem::file_size(path)==65537,
        "oversized replacement preserves existing file");
    Check(!ReadPaletteFile(root/"missing.json",loaded,error)&&!error.empty(),"missing external file refused");
    Check(!SavePaletteFile(root/"unknown.txt",d.State(),std::nullopt,result,error)&&!error.empty()&&
        !std::filesystem::exists(root/"unknown.txt"),"unsupported extension refused");
    Check(!SavePaletteFile(root/"missing.json",d.State(),saved.fingerprint,result,error)&&
        !std::filesystem::exists(root/"missing.json"),"replacement of deleted file refused");
    auto invalid=d.State();invalid.ramps.clear();
    Check(!SavePaletteFile(root/"invalid.json",invalid,std::nullopt,result,error)&&
        !std::filesystem::exists(root/"invalid.json"),"invalid metadata cannot be saved");
}

void LibraryTests(const std::filesystem::path& root) {
    Library library(root);Document d;d.LoadStock("misaki",3,Stock());
    d.SetName("Blue");d.SetRampColor(0,{20,50,180});d.SetPinned(0,true);
    LibraryEntry first;std::string error;SavedPalette loaded;
    Check(library.SaveNew(d.State(),first,error),"save new");
    Check(library.Read(first,loaded,error)&&loaded.colors==d.Colors()&&loaded.pins==d.State().pins&&loaded.targets==d.State().targets,"metadata round trip");
    LibraryEntry untouched=first;
    Check(!library.SaveNew(d.State(),untouched,error),"save new collision refused");
    Check(library.Read(first,loaded,error),"collision preserved file");
    d.SetName("Blue renamed");d.SetColor(2,{4,6,8});LibraryEntry replacement;
    Check(library.Replace(first,d.State(),replacement,error)&&replacement.path==first.path,"replace retains filename");
    Check(!library.Read(first,loaded,error),"stale entry refuses changed disk");
    Check(library.Read(replacement,loaded,error)&&loaded.name=="Blue renamed","renamed metadata");
    auto entries=library.Scan("misaki",false,error);
    Check(error.empty()&&entries.size()==1&&entries[0].name=="Blue renamed","scan");
    LibraryEntry deleted,restored;
    Check(library.Delete(replacement,deleted,error)&&!std::filesystem::exists(replacement.path),"recoverable delete");
    Check(library.Scan("misaki",true,error).size()==1,"trash scan");
    Check(library.Restore(deleted,restored,error)&&std::filesystem::exists(first.path),"restore");
    Check(library.Delete(restored,deleted,error),"delete again");
    Document collision;collision.LoadStock("misaki",0,Stock());collision.SetName("Blue");
    LibraryEntry occupied;Check(library.SaveNew(collision.State(),occupied,error),"new palette takes old filename");
    Check(!library.Restore(deleted,restored,error)&&std::filesystem::exists(deleted.path),"restore collision retains deleted file");
    LibraryEntry secondDeleted;Check(library.Delete(occupied,secondDeleted,error)&&secondDeleted.path!=deleted.path,"delete collision chooses a recoverable filename");
    Check(library.Restore(secondDeleted,occupied,error),"restore second deleted palette");
    auto bad=collision.State();bad.name="../escape";
    Check(!library.SaveNew(bad,untouched,error),"path traversal refused");
    bad=collision.State();bad.character="../escape";
    Check(!library.SaveNew(bad,untouched,error),"character traversal refused");
    bad=collision.State();bad.name=std::string(1,char(-1));
    Check(!library.SaveNew(bad,untouched,error),"invalid UTF-8 refused");
    bad=collision.State();bad.name="CON";
    Check(!library.SaveNew(bad,untouched,error),"reserved filename refused");
    LibraryEntry forged=occupied;forged.path=root.parent_path()/"outside.json";
    Check(!library.Read(forged,loaded,error),"outside path refused");
    Check(!library.Replace(deleted,collision.State(),untouched,error),"replace trash refused");
    const auto native=root/"export.pal";
    Check(ExportNativePalette(native,d.Colors(),false,error),"native export");
    Palette imported{};
    Check(ReadNativePalette(native,imported,error)&&imported==d.Colors(),"native round trip");
    Check(!ExportNativePalette(native,Stock(),false,error),"native no overwrite default");
    Check(ExportNativePalette(native,Stock(),true,error),"native explicit overwrite");
    Check(ReadNativePalette(native,imported,error)&&imported==Stock(),"native replaced bytes");
    const auto before=imported;Write(native,"bad");
    Check(!ReadNativePalette(native,imported,error)&&imported==before,"invalid native leaves output");
    const auto malformed=root/"misaki"/"Malformed.json";Write(malformed,"{}");
    entries=library.Scan("misaki",false,error);
    Check(entries.size()==2&&std::any_of(entries.begin(),entries.end(),[](const auto& e){return !e.error.empty();}),"bad files listed without hiding good files");
    auto invalid=occupied;invalid.fingerprint=0;
    std::ifstream file(occupied.path);nlohmann::json json;file>>json;file.close();
    auto candidate=loaded;const auto original=candidate.colors;
    json["colors"][0][0]=256;Write(occupied.path,json.dump());
    Check(!library.Read(invalid,candidate,error)&&candidate.colors==original,"out of range candidate atomicity");
    json["colors"][0][0]=1;json["targets"][1]={2,3,4};Write(occupied.path,json.dump());
    Check(!library.Read(invalid,candidate,error),"non-key target refused");
    json["targets"][1]=nullptr;
    const auto text=json.dump();Write(occupied.path,text.substr(0,text.size()-1)+",\"version\":1}");
    Check(!library.Read(invalid,candidate,error),"duplicate JSON field refused");
    Write(occupied.path,std::string(100,'[')+"0"+std::string(100,']'));
    Check(!library.Read(invalid,candidate,error),"deep nesting refused");
    Write(occupied.path,std::string(65537,'x'));
    Check(!library.Read(invalid,candidate,error),"oversized file refused");
}
}
int main() {
    const auto root=std::filesystem::temp_directory_path()/("efz_palette_editor_tests_"+std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    try {
        ColorTests();ScopedHsvTests();DocumentTests();PaletteFileTests(root);LibraryTests(root);
        std::error_code ec;std::filesystem::remove_all(root,ec);
        std::cout<<"Palette colors, gestures, ramps, persistence and recovery passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\nFixtures: "<<root<<'\n';return 1;}
}
