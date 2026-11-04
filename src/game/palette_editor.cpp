#include "game/palette_editor.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <set>
#include <random>
#include <sstream>
#include <stdexcept>
#include <nlohmann/json.hpp>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace PaletteEditor {
namespace {
constexpr double pi = 3.14159265358979323846;
using Json = nlohmann::json;
double Wrap(double x, double period) { return x - std::floor(x / period) * period; }
bool Same(const SavedPalette& a, const SavedPalette& b) {
    return a.name == b.name && a.colors == b.colors && a.pins == b.pins && a.targets == b.targets;
}
bool SafePart(const std::string& value, bool label) {
    if (value.empty() || value.size() > 32 || value == "." || value == ".." ||
        value.back() == '.' || value.back() == ' ') return false;
    for (unsigned char c : value) {
        if (c < 32 || c == 127 || std::string("<>:\"/\\|?*").find(char(c)) != std::string::npos) return false;
        if (!label && !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    std::string upper = value.substr(0, value.find('.'));
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    if (upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL") return false;
    return !(upper.size() == 4 && (upper.substr(0, 3) == "COM" || upper.substr(0, 3) == "LPT") && upper[3] >= '0' && upper[3] <= '9');
}
struct Lab { double l, a, b; };
double Linear(double x) { return x <= 0.04045 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4); }
Lab ToLab(Color c) {
    const double r = Linear(c.r / 255.), g = Linear(c.g / 255.), b = Linear(c.b / 255.);
    const double l = std::cbrt(0.4122214708*r + 0.5363325363*g + 0.0514459929*b);
    const double m = std::cbrt(0.2119034982*r + 0.6806995451*g + 0.1073969566*b);
    const double s = std::cbrt(0.0883024619*r + 0.2817188376*g + 0.6299787005*b);
    return {0.2104542553*l + 0.7936177850*m - 0.0040720468*s,
        1.9779984951*l - 2.4285922050*m + 0.4505937099*s,
        0.0259040371*l + 0.7827717662*m - 0.8086757660*s};
}
std::array<double, 3> LinearRgb(Lab c) {
    double l = c.l + 0.3963377774*c.a + 0.2158037573*c.b;
    double m = c.l - 0.1055613458*c.a - 0.0638541728*c.b;
    double s = c.l - 0.0894841775*c.a - 1.2914855480*c.b;
    l *= l*l; m *= m*m; s *= s*s;
    return {4.0767416621*l - 3.3077115913*m + 0.2309699292*s,
        -1.2684380046*l + 2.6097574011*m - 0.3413193965*s,
        -0.0041960863*l - 0.7034186147*m + 1.7076147010*s};
}
Color FromLab(Lab lab) {
    lab.l = std::clamp(lab.l, 0., 1.);
    auto rgb = LinearRgb(lab);
    const auto fits = [](const auto& c) { return std::all_of(c.begin(), c.end(), [](double v) { return v >= -1e-7 && v <= 1.0000001; }); };
    if (!fits(rgb)) {
        double low = 0, high = 1;
        for (unsigned n = 0; n < 22; ++n) {
            const double mid = (low + high) / 2;
            if (fits(LinearRgb({lab.l, lab.a*mid, lab.b*mid}))) low = mid; else high = mid;
        }
        rgb = LinearRgb({lab.l, lab.a*low, lab.b*low});
    }
    const auto byte = [](double v) {
        v = std::clamp(v, 0., 1.);
        const double srgb = v <= 0.0031308 ? 12.92*v : 1.055*std::pow(v, 1./2.4) - 0.055;
        return std::uint8_t(std::clamp(std::lround(srgb*255), 0l, 255l));
    };
    return {byte(rgb[0]), byte(rgb[1]), byte(rgb[2])};
}
Color Shade(const Palette& original, Ramp ramp, unsigned index, Color target) {
    const Color key = original[ramp.key];
    if (key == target) return original[index];
    if (original[index] == key) return target;
    const Lab k = ToLab(key), t = ToLab(target), c = ToLab(original[index]);
    const double kc = std::hypot(k.a, k.b), tc = std::hypot(t.a, t.b);
    double top = k.l;
    for (unsigned i = ramp.first; i <= ramp.last; ++i) top = std::max(top, ToLab(original[i]).l);
    const double scale = k.l > 1e-6 ? std::min(1., t.l/k.l) : 1.;
    const double up = top > k.l + 1e-9 && t.l + (top-k.l)*scale > 1 ? (1-t.l)/(top-k.l) : scale;
    const double lightness = t.l + (c.l-k.l)*(c.l > k.l ? up : scale);
    if (kc < 1e-4) return FromLab({lightness, c.a+t.a-k.a, c.b+t.b-k.b});
    const double kh = std::atan2(k.b, k.a), th = std::atan2(t.b, t.a);
    const double weight = std::max(0., 1-std::abs(std::remainder(th-kh, 2*pi))/(pi/2));
    const double chroma = std::hypot(c.a, c.b)*tc/kc;
    const double hue = std::hypot(c.a, c.b) < 1e-4 ? th : th+weight*std::remainder(std::atan2(c.b,c.a)-kh,2*pi);
    return FromLab({lightness, chroma*std::cos(hue), chroma*std::sin(hue)});
}
bool Valid(const SavedPalette& p, std::string& error, bool libraryName = true) {
    const bool validName = libraryName ? SafePart(p.name,true) :
        !p.name.empty() && p.name.size() <= 1024 &&
        std::none_of(p.name.begin(),p.name.end(),[](unsigned char c) { return c < 32 || c == 127; });
    if (!validName || !SafePart(p.character, false) || p.stockSlot >= 6) {
        error = "Invalid palette name, character or stock slot."; return false;
    }
    try { (void)Json(p.name).dump(); }
    catch (const std::exception&) { error = "Palette name is not valid UTF-8."; return false; }
    unsigned next = 0;
    std::bitset<40> keys;
    for (const auto& r : p.ramps) {
        if (r.first != next || r.first > r.last || r.last >= 40 || r.key < r.first || r.key > r.last) {
            error = "Invalid palette ramp layout."; return false;
        }
        next = r.last+1; keys.set(r.key);
    }
    if (next != 40) { error = "Palette ramps must cover all 40 colors."; return false; }
    for (unsigned i = 0; i < 40; ++i) if (p.targets[i] && !keys[i]) {
        error = "Ramp target is not a ramp key."; return false;
    }
    return true;
}
Json ColorJson(Color c) { return Json::array({c.r,c.g,c.b}); }
Json Encode(const SavedPalette& p) {
    Json baseline = Json::array(), colors = Json::array(), pins = Json::array(), ramps = Json::array(), targets = Json::array();
    for (unsigned i = 0; i < 40; ++i) {
        baseline.push_back(ColorJson(p.baseline[i])); colors.push_back(ColorJson(p.colors[i]));
        pins.push_back(p.pins[i]); targets.push_back(p.targets[i] ? ColorJson(*p.targets[i]) : Json());
    }
    for (auto r : p.ramps) ramps.push_back(Json::array({r.first,r.last,r.key}));
    return Json{{"version",1},{"name",p.name},{"character",p.character},{"stock_slot",p.stockSlot},
        {"baseline",baseline},{"colors",colors},{"pins",pins},{"ramps",ramps},{"targets",targets}};
}
unsigned Integer(const Json& j, unsigned maximum) {
    if (!j.is_number_integer() || (j.is_number_unsigned() ? j.get<std::uint64_t>() > maximum :
        j.get<std::int64_t>() < 0 || std::uint64_t(j.get<std::int64_t>()) > maximum)) throw std::runtime_error("Invalid palette integer.");
    return j.get<unsigned>();
}
Color DecodeColor(const Json& j) {
    if (!j.is_array() || j.size() != 3) throw std::runtime_error("Invalid RGB color.");
    return {std::uint8_t(Integer(j[0],255)),std::uint8_t(Integer(j[1],255)),std::uint8_t(Integer(j[2],255))};
}
bool Decode(const std::vector<std::uint8_t>& bytes, SavedPalette& out, std::string& error, bool libraryName = true) {
    try {
        std::vector<std::set<std::string>> objects;
        bool duplicate = false;
        const auto callback = [&](int depth, Json::parse_event_t event, Json& parsed) {
            if (depth > 8) throw std::runtime_error("Palette nesting is too deep.");
            if (event == Json::parse_event_t::object_start) objects.emplace_back();
            if (event == Json::parse_event_t::key && !objects.back().insert(parsed.get<std::string>()).second) duplicate = true;
            if (event == Json::parse_event_t::object_end) objects.pop_back();
            return true;
        };
        const Json j = Json::parse(bytes.begin(), bytes.end(), callback);
        if (duplicate || !j.is_object() || j.size() != 9 || Integer(j.at("version"),1) != 1) throw std::runtime_error("Invalid palette format.");
        SavedPalette p;
        p.name = j.at("name").get<std::string>(); p.character = j.at("character").get<std::string>();
        p.stockSlot = Integer(j.at("stock_slot"),5);
        for (const char* key : {"baseline","colors","pins","targets"})
            if (!j.at(key).is_array() || j.at(key).size() != 40) throw std::runtime_error("Palette must contain 40 colors.");
        for (unsigned i = 0; i < 40; ++i) {
            p.baseline[i] = DecodeColor(j.at("baseline")[i]); p.colors[i] = DecodeColor(j.at("colors")[i]);
            if (!j.at("pins")[i].is_boolean()) throw std::runtime_error("Invalid pin flag.");
            p.pins[i] = j.at("pins")[i].get<bool>();
            if (!j.at("targets")[i].is_null()) p.targets[i] = DecodeColor(j.at("targets")[i]);
        }
        if (!j.at("ramps").is_array() || j.at("ramps").size() > 40) throw std::runtime_error("Invalid ramp list.");
        for (const auto& r : j.at("ramps")) {
            if (!r.is_array() || r.size() != 3) throw std::runtime_error("Invalid ramp.");
            p.ramps.push_back({Integer(r[0],39),Integer(r[1],39),Integer(r[2],39)});
        }
        if (!Valid(p,error,libraryName)) return false;
        out = std::move(p); return true;
    } catch (const std::exception&) { error = "Invalid or truncated palette file."; return false; }
}
std::uint64_t Fingerprint(const std::vector<std::uint8_t>& bytes) {
    std::uint64_t hash = 14695981039346656037ull;
    for (auto byte : bytes) { hash ^= byte; hash *= 1099511628211ull; }
    return hash;
}
bool ReadBytes(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes, std::string& error, std::uintmax_t maximum = 65536) {
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(path,ec);
    if (ec || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)) {
        error = "Palette file is missing or is not a regular file."; return false;
    }
    const auto size = std::filesystem::file_size(path,ec);
    if (ec || size > maximum) { error = "Palette file is too large."; return false; }
    std::ifstream file(path,std::ios::binary);
    std::vector<std::uint8_t> candidate(std::size_t(size),0);
    if (!file || (size && !file.read(reinterpret_cast<char*>(candidate.data()),std::streamsize(size))) || file.peek() != EOF) {
        error = "Couldn't read palette file."; return false;
    }
    bytes = std::move(candidate); return true;
}
bool DirectoryReady(const std::filesystem::path& path, std::string& error) {
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(path,ec);
    if (!ec && std::filesystem::exists(status)) {
        if (!std::filesystem::is_directory(status) || std::filesystem::is_symlink(status)) {
            error = "Palette folder is not a regular directory."; return false;
        }
        return true;
    }
    ec.clear();
    if (!std::filesystem::create_directories(path,ec) && ec) { error = "Couldn't create palette folder."; return false; }
    return true;
}
bool AtomicWrite(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes, bool replace, std::string& error) {
    if (!DirectoryReady(path.parent_path(),error)) return false;
    std::error_code ec;
    const auto existing = std::filesystem::symlink_status(path,ec);
    if (!ec && std::filesystem::exists(existing) && (!replace || !std::filesystem::is_regular_file(existing) || std::filesystem::is_symlink(existing))) {
        error = replace ? "Palette target is not a regular file." : "A palette with that name already exists."; return false;
    }
    static std::atomic<unsigned long> serial{0};
    auto temp = path;
#ifdef _WIN32
    temp += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++serial);
    HANDLE file = CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file == INVALID_HANDLE_VALUE) { error = "Couldn't create temporary palette file."; return false; }
    DWORD written = 0;
    bool ok = WriteFile(file,bytes.data(),DWORD(bytes.size()),&written,nullptr) && written == bytes.size();
    if (ok) ok = FlushFileBuffers(file) != FALSE;
    if (!CloseHandle(file)) ok = false;
    if (ok) ok = MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0)) != FALSE;
#else
    temp += ".tmp-" + std::to_string(getpid()) + "-" + std::to_string(++serial);
    const int file = open(temp.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
    if (file < 0) { error = "Couldn't create temporary palette file."; return false; }
    bool ok = write(file,bytes.data(),bytes.size()) == static_cast<ssize_t>(bytes.size());
    if (ok) ok = fsync(file) == 0;
    if (close(file) != 0) ok = false;
    if (ok) {
        if (replace) ok = rename(temp.c_str(),path.c_str()) == 0;
        else { ok = link(temp.c_str(),path.c_str()) == 0; if (ok) unlink(temp.c_str()); }
    }
#endif
    if (!ok) { std::filesystem::remove(temp,ec); error = "Couldn't save palette; existing file was retained."; }
    return ok;
}
bool MoveNew(const std::filesystem::path& from, const std::filesystem::path& to, std::string& error) {
    if (!DirectoryReady(to.parent_path(),error)) return false;
#ifdef _WIN32
    if (!MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_WRITE_THROUGH)) {
        error = "Couldn't move palette; destination may already exist."; return false;
    }
#else
    if (link(from.c_str(),to.c_str()) != 0) { error = "Couldn't move palette; destination may already exist."; return false; }
    if (unlink(from.c_str()) != 0) { unlink(to.c_str()); error = "Couldn't remove original palette."; return false; }
#endif
    return true;
}
LibraryEntry Entry(const std::filesystem::path& path, const SavedPalette& palette, bool deleted, const std::vector<std::uint8_t>& bytes) {
    LibraryEntry e; e.path=path; e.name=palette.name; e.character=palette.character;
    e.stockSlot=palette.stockSlot; e.deleted=deleted; e.fingerprint=Fingerprint(bytes); return e;
}
std::vector<std::uint8_t> Bytes(const SavedPalette& p) {
    const auto text = Encode(p).dump(2)+"\n";
    return {text.begin(),text.end()};
}

bool FilePath(const std::filesystem::path& path, PaletteFile& file, std::string& error) {
    std::error_code ec;
    file.path=std::filesystem::absolute(path,ec).lexically_normal();
    if(ec) {error="Invalid palette path.";return false;}
    try {
        auto extension=file.path.extension().u8string();
        std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c) {return char(std::tolower(c));});
        if(extension!=".json"&&extension!=".pal") {error="Choose a .json or .pal palette file.";return false;}
        file.native=extension==".pal";file.palette.name=file.path.stem().u8string();
    } catch(const std::exception&) {error="Invalid palette path.";return false;}
    return true;
}
}

Hsv RgbToHsv(Color color) {
    const float r=color.r/255.f, g=color.g/255.f, b=color.b/255.f;
    const float high=std::max({r,g,b}), low=std::min({r,g,b}), delta=high-low;
    float h=0;
    if (delta>0) h=60*(high==r ? (g-b)/delta : high==g ? (b-r)/delta+2 : (r-g)/delta+4);
    return {float(Wrap(h,360)), high>0 ? 100*delta/high : 0, high*100};
}
Color HsvToRgb(Hsv color) {
    const double h=std::isfinite(color.h) ? Wrap(color.h,360)/60 : 0;
    const double s=std::isfinite(color.s) ? std::clamp(color.s,0.f,100.f)/100 : 0;
    const double v=std::isfinite(color.v) ? std::clamp(color.v,0.f,100.f)/100 : 0;
    const double c=v*s, x=c*(1-std::abs(std::fmod(h,2)-1)), m=v-c;
    double r=0,g=0,b=0;
    switch (int(h)) {
    case 0:r=c;g=x;break; case 1:r=x;g=c;break; case 2:g=c;b=x;break;
    case 3:g=x;b=c;break; case 4:r=x;b=c;break; default:r=c;b=x;break;
    }
    return {std::uint8_t(std::lround((r+m)*255)),std::uint8_t(std::lround((g+m)*255)),std::uint8_t(std::lround((b+m)*255))};
}
bool ParseHex(const std::string& text, Color& color) {
    if (text.size()>128) return false;
    const auto first=text.find_first_not_of(" \t\r\n"), last=text.find_last_not_of(" \t\r\n");
    if (first==std::string::npos) return false;
    auto value=text.substr(first,last-first+1);
    if (!value.empty() && value.front()=='#') value.erase(0,1);
    if (value.size()!=3 && value.size()!=6) return false;
    unsigned channels[6]{};
    for (std::size_t i=0;i<value.size();++i) {
        const char c=value[i];
        if (c>='0'&&c<='9') channels[i]=c-'0';
        else if(c>='a'&&c<='f') channels[i]=c-'a'+10;
        else if(c>='A'&&c<='F') channels[i]=c-'A'+10;
        else return false;
    }
    color=value.size()==3 ? Color{std::uint8_t(channels[0]*17),std::uint8_t(channels[1]*17),std::uint8_t(channels[2]*17)} :
        Color{std::uint8_t(channels[0]*16+channels[1]),std::uint8_t(channels[2]*16+channels[3]),std::uint8_t(channels[4]*16+channels[5])};
    return true;
}
std::string FormatHex(Color color) {
    std::ostringstream out;
    out << '#' << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << unsigned(color.r)
        << std::setw(2) << unsigned(color.g) << std::setw(2) << unsigned(color.b);
    return out.str();
}
std::vector<Ramp> DetectRamps(const Palette& original) {
    std::vector<Ramp> ramps;
    unsigned first=0; int direction=0;
    for(unsigned i=1;i<=40;++i) {
        bool split=i==40;
        int step=0;
        if (!split) {
            const auto a=RgbToHsv(original[i-1]), b=RgbToHsv(original[i]), anchor=RgbToHsv(original[first]);
            const double delta=ToLab(original[i]).l-ToLab(original[i-1]).l;
            step=delta>0.005 ? 1 : delta < -0.005 ? -1 : 0;
            const bool gray=a.s<=12 && b.s<=12;
            const bool hueClose=std::abs(std::remainder(b.h-a.h,360.))<=28 &&
                std::abs(std::remainder(b.h-anchor.h,360.))<=40;
            split=(!gray && !hueClose) || std::abs(b.s-a.s)>40 ||
                std::abs(delta)>0.32 || (step && direction && step!=direction);
        }
        if (split) { ramps.push_back({first,i-1,(first+i-1)/2}); first=i; direction=0; }
        else if (step) direction=step;
    }
    return ramps;
}
void Document::LoadStock(const std::string& character, unsigned slot, const Palette& stock) {
    state_={}; state_.character=character; state_.stockSlot=slot; state_.name="Palette";
    state_.baseline=state_.colors=stock; state_.ramps=DetectRamps(stock);
    loaded_=state_; history_.clear(); EndGesture();
    ResetHsv();
}
bool Document::LoadSaved(const SavedPalette& saved, const Palette& stock, std::string& error) {
    error.clear();
    if (!Valid(saved,error,false)) return false;
    state_=saved;
    if (stock!=saved.baseline) {
        state_.baseline=stock; state_.ramps=DetectRamps(stock); state_.targets={}; state_.pins.reset();
        for(unsigned i=0;i<40;++i) state_.pins[i]=state_.colors[i]!=stock[i];
    }
    loaded_=state_; history_.clear(); EndGesture();
    ResetHsv();
    return true;
}
void Document::Checkpoint() {
    if (gesture_ && gestureSaved_) return;
    history_.push_back({state_,hsv_,rampHsv_});
    if(history_.size()>16) history_.erase(history_.begin());
    if(gesture_) gestureSaved_=true;
}
bool Document::Dirty() const { return !Same(state_,loaded_); }
Hsv Document::ColorHsv(unsigned entry) const { return entry<40 ? hsv_[entry] : Hsv{}; }
Hsv Document::RampHsv(unsigned ramp) const { return ramp<state_.ramps.size() ? rampHsv_[ramp] : Hsv{}; }
Color Document::RampColor(unsigned ramp) const {
    if(ramp>=state_.ramps.size()) return {};
    const auto key=state_.ramps[ramp].key;
    return state_.targets[key].value_or(state_.baseline[key]);
}
void Document::ResetHsv() {
    for(unsigned i=0;i<40;++i) hsv_[i]=RgbToHsv(state_.colors[i]);
    for(unsigned i=0;i<state_.ramps.size();++i) {
        const auto key=state_.ramps[i].key;
        rampHsv_[i]=RgbToHsv(state_.targets[key].value_or(state_.baseline[key]));
    }
}
void Document::BeginGesture() { if (!gesture_) { gesture_=true; gestureSaved_=false; } }
void Document::EndGesture() { gesture_=gestureSaved_=false; }
bool Document::SetColor(unsigned entry, Color color) {
    if(entry>=40) return false;
    if(state_.colors[entry]==color && state_.pins[entry]) return true;
    Checkpoint(); state_.colors[entry]=color; state_.pins.set(entry); hsv_[entry]=RgbToHsv(color); return true;
}
bool Document::SetHsv(unsigned entry,Hsv color) {
    if(entry>=40 || !std::isfinite(color.h) || !std::isfinite(color.s) || !std::isfinite(color.v)) return false;
    color.h=float(Wrap(color.h,360)); color.s=std::clamp(color.s,0.f,100.f); color.v=std::clamp(color.v,0.f,100.f);
    const auto rgb=HsvToRgb(color);
    if(state_.colors[entry]==rgb && state_.pins[entry] &&
       (hsv_[entry].h!=color.h || hsv_[entry].s!=color.s || hsv_[entry].v!=color.v)) Checkpoint();
    if(!SetColor(entry,rgb)) return false;
    hsv_[entry]=color; return true;
}
bool Document::SetHex(unsigned entry,const std::string& text) { Color color{}; return ParseHex(text,color) && SetColor(entry,color); }
void Document::ComposeRamp(unsigned ramp) {
    const auto r=state_.ramps[ramp];
    for(unsigned i=r.first;i<=r.last;++i) if(!state_.pins[i]) {
        state_.colors[i]=state_.targets[r.key] ? Shade(state_.baseline,r,i,*state_.targets[r.key]) : state_.baseline[i];
        hsv_[i]=RgbToHsv(state_.colors[i]);
    }
}
bool Document::SetRampColor(unsigned ramp,Color color) {
    if(ramp>=state_.ramps.size()) return false;
    const auto key=state_.ramps[ramp].key;
    if(state_.targets[key] && *state_.targets[key]==color) return true;
    Checkpoint(); state_.targets[key]=color; rampHsv_[ramp]=RgbToHsv(color); ComposeRamp(ramp); return true;
}
bool Document::SetRampHsv(unsigned ramp,Hsv color) {
    if(ramp>=state_.ramps.size() || !std::isfinite(color.h) || !std::isfinite(color.s) || !std::isfinite(color.v)) return false;
    color.h=float(Wrap(color.h,360)); color.s=std::clamp(color.s,0.f,100.f); color.v=std::clamp(color.v,0.f,100.f);
    const auto rgb=HsvToRgb(color);
    const auto target=state_.targets[state_.ramps[ramp].key];
    if(target && *target==rgb &&
       (rampHsv_[ramp].h!=color.h || rampHsv_[ramp].s!=color.s || rampHsv_[ramp].v!=color.v)) Checkpoint();
    if(!SetRampColor(ramp,rgb)) return false;
    rampHsv_[ramp]=color; return true;
}
bool Document::SetRampHex(unsigned ramp,const std::string& text) { Color color{}; return ParseHex(text,color) && SetRampColor(ramp,color); }
bool Document::SetPinned(unsigned entry,bool pinned) {
    if(entry>=40) return false;
    if(state_.pins[entry]==pinned) return true;
    Checkpoint(); state_.pins[entry]=pinned;
    if(!pinned) for(unsigned i=0;i<state_.ramps.size();++i) if(entry>=state_.ramps[i].first && entry<=state_.ramps[i].last) { ComposeRamp(i); break; }
    return true;
}
bool Document::ResetEntry(unsigned entry) {
    if(entry>=40) return false;
    if(!state_.pins[entry]) return true;
    return SetPinned(entry,false);
}
bool Document::ResetRamp(unsigned ramp) {
    if(ramp>=state_.ramps.size()) return false;
    const auto r=state_.ramps[ramp];
    bool changed=bool(state_.targets[r.key]);
    for(unsigned i=r.first;i<=r.last;++i) changed=changed||state_.pins[i]||state_.colors[i]!=state_.baseline[i];
    if(!changed) return true;
    Checkpoint(); state_.targets[r.key].reset(); rampHsv_[ramp]=RgbToHsv(state_.baseline[r.key]);
    for(unsigned i=r.first;i<=r.last;++i) state_.pins.reset(i);
    ComposeRamp(ramp); return true;
}
bool Document::ResetAll() {
    if(state_.colors==state_.baseline && state_.pins.none() &&
       std::none_of(state_.targets.begin(),state_.targets.end(),[](const auto& x){return bool(x);})) return true;
    Checkpoint(); state_.colors=state_.baseline; state_.pins.reset(); state_.targets={};
    ResetHsv();
    return true;
}
bool Document::CommitGenerated(SavedPalette next) {
    if(Same(state_,next)) return false;
    EndGesture();Checkpoint();
    for(unsigned i=0;i<40;++i)
        if(next.colors[i]!=state_.colors[i]) hsv_[i]=RgbToHsv(next.colors[i]);
    for(unsigned i=0;i<state_.ramps.size();++i) {
        const auto key=state_.ramps[i].key;
        if(next.targets[key]!=state_.targets[key]) rampHsv_[i]=RgbToHsv(next.targets[key].value_or(next.baseline[key]));
    }
    state_=std::move(next);return true;
}
bool Document::Generate(std::uint32_t seed,ColorScope scope,unsigned selection,unsigned transparent) {
    if(state_.ramps.empty() || (scope==ColorScope::Ramp && selection>=state_.ramps.size()) ||
       (scope==ColorScope::Shade && selection>=40) ||
       (scope!=ColorScope::Palette && scope!=ColorScope::Ramp && scope!=ColorScope::Shade)) return false;
    std::mt19937 random(seed);
    const auto color=[&] {
        const float h=float(random()%360),s=float(10+random()%91)/100.f,l=float(20+random()%76)/100.f;
        const float v=l+s*(std::min)(l,1-l);
        return HsvToRgb({h,v>0?200*(1-l/v):0,100*v});
    };
    auto next=state_;
    if(scope==ColorScope::Shade) {
        if(selection==transparent || state_.pins[selection]) return false;
        next.colors[selection]=color();next.pins.set(selection);
    } else for(unsigned i=0;i<state_.ramps.size();++i) {
        if(scope==ColorScope::Ramp && i!=selection) continue;
        const auto ramp=state_.ramps[i];
        bool editable=false;
        for(unsigned index=ramp.first;index<=ramp.last;++index)
            editable=editable || (index!=transparent && !state_.pins[index]);
        if(!editable) continue;
        const auto target=color();next.targets[ramp.key]=target;
        for(unsigned index=ramp.first;index<=ramp.last;++index)
            if(index!=transparent && !state_.pins[index]) next.colors[index]=Shade(state_.baseline,ramp,index,target);
    }
    return CommitGenerated(std::move(next));
}
bool Document::Gradient(unsigned ramp,unsigned transparent,bool hsv) {
    if(ramp>=state_.ramps.size()) return false;
    unsigned first=state_.ramps[ramp].first,last=state_.ramps[ramp].last;
    if(first==last) return false;
    if(first==transparent) ++first;
    if(last==transparent) --last;
    if(first>=last) return false;
    const auto start=state_.colors[first],end=state_.colors[last];
    auto a=RgbToHsv(start),b=RgbToHsv(end);
    if(a.s==0) a.h=b.h;
    if(b.s==0) b.h=a.h;
    const float hueDelta=float(Wrap(b.h-a.h+180,360)-180);
    auto next=state_;
    for(unsigned i=first+1;i<last;++i) {
        if(i==transparent || state_.pins[i]) continue;
        const float t=float(i-first)/float(last-first);
        const auto mix=[t](float x,float y){return x+(y-x)*t;};
        const auto byte=[&](unsigned x,unsigned y){return std::uint8_t(std::lround(mix(float(x),float(y))));};
        const Color value=hsv?HsvToRgb({a.h+hueDelta*t,mix(a.s,b.s),mix(a.v,b.v)}):
            Color{byte(start.r,end.r),byte(start.g,end.g),byte(start.b,end.b)};
        if(value!=next.colors[i]) {next.colors[i]=value;next.pins.set(i);}
    }
    return CommitGenerated(std::move(next));
}
void Document::Import(const Palette& colors) {
    if(state_.colors==colors) return;
    Checkpoint(); state_.colors=colors; state_.targets={};
    for(unsigned i=0;i<40;++i) state_.pins[i]=colors[i]!=state_.baseline[i];
    ResetHsv();
}
bool Document::Undo() {
    EndGesture(); if(history_.empty()) return false;
    state_=history_.back().palette; hsv_=history_.back().hsv; rampHsv_=history_.back().rampHsv; history_.pop_back(); return true;
}
void Document::MarkSaved() { loaded_=state_; EndGesture(); }
void Document::Revert() {
    state_=loaded_; history_.clear(); EndGesture();
    ResetHsv();
}

bool Library::CheckPath(const LibraryEntry& entry,std::string& error) const {
    if(!SafePart(entry.character,false) || entry.path.extension()!=".json" ||
       !SafePart(entry.path.stem().u8string(),true)) {error="Invalid palette path.";return false;}
    std::error_code ec;
    const auto folder=(entry.deleted ? directory_/entry.character/"deleted" : directory_/entry.character);
    if(std::filesystem::absolute(entry.path,ec).lexically_normal().parent_path()!=
       std::filesystem::absolute(folder,ec).lexically_normal()) {error="Palette is outside the library.";return false;}
    for(const auto& path:{directory_,directory_/entry.character,folder}) {
        const auto status=std::filesystem::symlink_status(path,ec);
        if(!ec && (std::filesystem::is_symlink(status) || (std::filesystem::exists(status)&&!std::filesystem::is_directory(status)))) {
            error="Palette folder is not a regular directory.";return false;
        }
    }
    return true;
}
std::vector<LibraryEntry> Library::Scan(const std::string& character,bool deleted,std::string& error) const {
    error.clear(); std::vector<LibraryEntry> entries;
    if(!SafePart(character,false)) {error="Invalid character folder.";return entries;}
    const auto folder=(deleted ? directory_/character/"deleted" : directory_/character);
    LibraryEntry check; check.character=character; check.deleted=deleted; check.path=folder/"check.json";
    if(!CheckPath(check,error)) return entries;
    std::error_code ec;
    if(!std::filesystem::exists(folder,ec)) return entries;
    for(std::filesystem::directory_iterator it(folder,ec),end;!ec&&it!=end;it.increment(ec)) {
        if(it->path().extension()!=".json") continue;
        LibraryEntry entry;entry.path=it->path();entry.character=character;entry.deleted=deleted;entry.name=it->path().stem().u8string();
        std::vector<std::uint8_t> bytes;SavedPalette p;
        if(CheckPath(entry,entry.error)&&ReadBytes(entry.path,bytes,entry.error)) {
            entry.fingerprint=Fingerprint(bytes);
            if(Decode(bytes,p,entry.error)) {
                if(p.character!=character) entry.error="Palette belongs to another character.";
                else entry=Entry(entry.path,p,deleted,bytes);
            }
        }
        entries.push_back(std::move(entry));
    }
    if(ec) error="Couldn't scan palette library.";
    std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){return a.path.filename()<b.path.filename();});
    return entries;
}
bool Library::Read(const LibraryEntry& entry,SavedPalette& out,std::string& error) const {
    error.clear(); if(!CheckPath(entry,error)) return false;
    std::vector<std::uint8_t> bytes;
    if(!ReadBytes(entry.path,bytes,error)) return false;
    if(entry.fingerprint && Fingerprint(bytes)!=entry.fingerprint) {error="Palette changed on disk; rescan first.";return false;}
    SavedPalette candidate;
    if(!Decode(bytes,candidate,error)) return false;
    if(candidate.character!=entry.character) {error="Palette belongs to another character.";return false;}
    out=std::move(candidate);return true;
}
bool Library::SaveNew(const SavedPalette& palette,LibraryEntry& out,std::string& error) const {
    error.clear();if(!Valid(palette,error)) return false;
    LibraryEntry entry;entry.character=palette.character;entry.path=directory_/palette.character/std::filesystem::u8path(palette.name+".json");
    if(!CheckPath(entry,error)||!DirectoryReady(directory_,error)||!DirectoryReady(entry.path.parent_path(),error)) return false;
    const auto bytes=Bytes(palette);
    if(!AtomicWrite(entry.path,bytes,false,error)) return false;
    out=Entry(entry.path,palette,false,bytes);return true;
}
bool Library::Replace(const LibraryEntry& entry,const SavedPalette& palette,LibraryEntry& out,std::string& error) const {
    error.clear();if(entry.deleted||!Valid(palette,error)||palette.character!=entry.character) {if(error.empty())error="Invalid replacement palette.";return false;}
    SavedPalette old;
    if(!Read(entry,old,error)) return false;
    const auto bytes=Bytes(palette);
    if(!AtomicWrite(entry.path,bytes,true,error)) return false;
    out=Entry(entry.path,palette,false,bytes);return true;
}
bool Library::Delete(const LibraryEntry& entry,LibraryEntry& deleted,std::string& error) const {
    error.clear();if(entry.deleted){error="Palette is already deleted.";return false;}
    SavedPalette p;if(!Read(entry,p,error)) return false;
    const auto folder=directory_/entry.character/"deleted";
    auto destination=folder/entry.path.filename();
    std::error_code ec;
    for(unsigned suffix=2;std::filesystem::exists(destination,ec);++suffix) {
        if(suffix>9999) {error="Palette trash is full.";return false;}
        destination=folder/("Deleted "+std::to_string(suffix)+".json");
    }
    if(ec) {error="Couldn't inspect palette trash.";return false;}
    LibraryEntry target=entry;target.path=destination;target.deleted=true;
    if(!CheckPath(target,error)||!MoveNew(entry.path,destination,error)) return false;
    deleted=target;return true;
}
bool Library::Restore(const LibraryEntry& deleted,LibraryEntry& restored,std::string& error) const {
    error.clear();if(!deleted.deleted){error="Palette is not deleted.";return false;}
    SavedPalette p;if(!Read(deleted,p,error)) return false;
    LibraryEntry target=deleted;target.path=directory_/deleted.character/deleted.path.filename();target.deleted=false;
    if(!CheckPath(target,error)||!MoveNew(deleted.path,target.path,error)) return false;
    restored=target;return true;
}
bool ReadPaletteFileIdentity(const std::filesystem::path& path,std::uint64_t& fingerprint,std::string& error) {
    error.clear();std::vector<std::uint8_t> bytes;
    if(!ReadBytes(path,bytes,error)) return false;
    fingerprint=Fingerprint(bytes);return true;
}
bool ReadPaletteFile(const std::filesystem::path& path,PaletteFile& out,std::string& error) {
    error.clear();PaletteFile candidate;
    if(!FilePath(path,candidate,error)) return false;
    std::vector<std::uint8_t> bytes;
    if(!ReadBytes(candidate.path,bytes,error,candidate.native?121:65536)) return false;
    if(candidate.native) {
        if(!DecodeNativePalette(bytes,candidate.palette.colors,error)) return false;
    } else if(!Decode(bytes,candidate.palette,error,false)) return false;
    candidate.fingerprint=Fingerprint(bytes);out=std::move(candidate);return true;
}
bool SavePaletteFile(const std::filesystem::path& path,const SavedPalette& palette,
    std::optional<std::uint64_t> expectedFingerprint,PaletteFile& out,std::string& error) {
    error.clear();PaletteFile candidate;
    if(!FilePath(path,candidate,error)) return false;
    const auto name=candidate.palette.name;candidate.palette=palette;candidate.palette.name=name;
    std::vector<std::uint8_t> bytes;
    if(candidate.native) {
        const auto native=EncodeNativePalette(palette.colors);bytes.assign(native.begin(),native.end());
    } else {
        if(!Valid(candidate.palette,error,false)) return false;
        bytes=Bytes(candidate.palette);
    }
    if(expectedFingerprint) {
        std::uint64_t current=0;
        if(!ReadPaletteFileIdentity(candidate.path,current,error)) return false;
        if(current!=*expectedFingerprint) {error="Palette changed on disk; open it again before saving.";return false;}
    }
    if(!AtomicWrite(candidate.path,bytes,expectedFingerprint.has_value(),error)) return false;
    candidate.fingerprint=Fingerprint(bytes);out=std::move(candidate);return true;
}

bool ReadNativePalette(const std::filesystem::path& path,Palette& out,std::string& error) {
    error.clear();std::vector<std::uint8_t> bytes;
    return ReadBytes(path,bytes,error,121)&&DecodeNativePalette(bytes,out,error);
}
bool ExportNativePalette(const std::filesystem::path& path,const Palette& colors,bool replace,std::string& error) {
    error.clear();const auto bytes=EncodeNativePalette(colors);
    return AtomicWrite(path,{bytes.begin(),bytes.end()},replace,error);
}
}
