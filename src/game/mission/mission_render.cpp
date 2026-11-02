#include "../../../include/game/mission/mission_render.h"
#include "../../../include/game/mission/tutorial_color_policy.h"
#include "../../../include/game/mission/mission_engine.h"
#include "../../../include/game/mission/mission_data.h"
#include "../../../include/game/mission/entity_notation_tables.h"
#include "../../../include/game/mission/entity_command_origin_policy.h"
#include "../../../include/game/mission/mission_entity_presentation_policy.h"
#include "../../../include/game/practice_menu/mission_title_screen.h"

#include "../../../3rdparty/imgui/imgui.h"

#include <windows.h>
#include <d3d9.h>
#include <gdiplus.h>

#include <cfloat>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "gdiplus.lib")

namespace Mission::Render {

namespace {

struct Icon { float u0 = 0, v0 = 0, u1 = 1, v1 = 1; UINT w = 0, h = 0; };

ULONG_PTR g_gdiplusToken = 0;
bool g_gdiplusOk = false;
bool g_iconsTried = false;
std::map<char, Icon> g_icons;
IDirect3DTexture9* g_atlasTex = nullptr;
IDirect3DDevice9* g_atlasDevice = nullptr;
uint64_t g_iconLayoutGeneration = 1;
// Font-atlas rebuilds recycle ImFont addresses; every font-dependent cache
// key carries a generation.
uint64_t g_layoutInvalidationGeneration = 1;

void ResetIconAtlas() {
    if (g_atlasTex) {
        g_atlasTex->Release();
        g_atlasTex = nullptr;
    }
    g_icons.clear();
    g_iconsTried = false;
    g_atlasDevice = nullptr;
    ++g_iconLayoutGeneration;
}

// status palette (AABBGGRR for ImGui)
constexpr ImU32 kBoxDone    = IM_COL32( 40, 120,  40, 200);
constexpr ImU32 kBoxCurrent = IM_COL32(200, 170,  20, 220);
constexpr ImU32 kBoxNext    = IM_COL32( 25,  25,  30, 190);
constexpr ImU32 kBoxFailed  = IM_COL32(150,  30,  30, 220);
constexpr ImU32 kBorder     = IM_COL32(255, 255, 255, 120);
constexpr ImU32 kTextCol    = IM_COL32(235, 235, 235, 255);

bool EnsureGdiplus() {
    if (g_gdiplusOk) return true;
    Gdiplus::GdiplusStartupInput in;
    g_gdiplusOk = (Gdiplus::GdiplusStartup(&g_gdiplusToken, &in, nullptr) == Gdiplus::Ok);
    return g_gdiplusOk;
}

std::wstring ControlsDir() {
    HMODULE mod = GetModuleHandleA("efz_training_mode.dll");
    char path[MAX_PATH] = {0};
    if (!mod || GetModuleFileNameA(mod, path, MAX_PATH) == 0) return std::wstring();
    std::string dir(path);
    const size_t slash = dir.find_last_of("\\/");
    if (slash == std::string::npos) return std::wstring();
    dir.resize(slash + 1);
    dir += "assets\\controls\\";
    std::wstring w(dir.begin(), dir.end());
    return w;
}

struct GlyphPixels { char key = 0; std::vector<uint32_t> px; UINT w = 0, h = 0; };
bool LoadGlyphPixels(const std::wstring& file, GlyphPixels& out) {
    Gdiplus::Bitmap bmp(file.c_str());
    if (bmp.GetLastStatus() != Gdiplus::Ok) return false;
    const UINT w = bmp.GetWidth(), h = bmp.GetHeight();
    if (!w || !h) return false;
    Gdiplus::Rect rect(0, 0, static_cast<INT>(w), static_cast<INT>(h));
    Gdiplus::BitmapData data{};
    if (bmp.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok)
        return false;
    out.px.resize(static_cast<size_t>(w) * h);
    const BYTE* scan0 = reinterpret_cast<const BYTE*>(data.Scan0);
    for (UINT y = 0; y < h; ++y) {
        const BYTE* src = data.Stride >= 0
            ? scan0 + static_cast<size_t>(y) * data.Stride
            : scan0 + static_cast<size_t>(h - 1 - y) * static_cast<size_t>(-data.Stride);
        memcpy(&out.px[static_cast<size_t>(y) * w], src, static_cast<size_t>(w) * sizeof(uint32_t));
    }
    bmp.UnlockBits(&data);
    out.w = w; out.h = h;
    return true;
}

void EnsureIcons(IDirect3DDevice9* dev) {
    if (g_atlasDevice && g_atlasDevice != dev) {
        // A managed texture must never be submitted to a different D3D device.
        ResetIconAtlas();
    }
    if (g_iconsTried || !dev) return;
    g_atlasDevice = dev;
    g_iconsTried = true;
    if (!EnsureGdiplus()) return;
    const std::wstring dir = ControlsDir();
    if (dir.empty()) return;

    struct Entry { char key; const wchar_t* file; };
    static const Entry entries[] = {
        {'1', L"1.gif"}, {'2', L"2.gif"}, {'3', L"3.gif"}, {'4', L"4.gif"},
        {'6', L"6.gif"}, {'7', L"7.gif"}, {'8', L"8.gif"}, {'9', L"9.gif"},
        {'A', L"A.png"}, {'B', L"B.png"}, {'C', L"C.png"}, {'D', L"D.png"}, {'S', L"S.png"},
    };
    std::vector<GlyphPixels> glyphs;
    glyphs.reserve(sizeof(entries) / sizeof(entries[0]));
    for (const Entry& e : entries) {
        GlyphPixels g; g.key = e.key;
        if (LoadGlyphPixels(dir + e.file, g)) glyphs.push_back(std::move(g));
    }
    if (glyphs.empty()) return;

    constexpr UINT kPad = 1;
    UINT atlasW = 0, atlasH = 0;
    for (const GlyphPixels& g : glyphs) { atlasW += g.w + kPad; atlasH = (std::max)(atlasH, g.h); }
    if (atlasW == 0 || atlasH == 0) return;

    IDirect3DTexture9* atlas = nullptr;
    if (FAILED(dev->CreateTexture(atlasW, atlasH, 1, 0, D3DFMT_A8R8G8B8,
                                  D3DPOOL_MANAGED, &atlas, nullptr))) return;
    D3DLOCKED_RECT lr{};
    if (FAILED(atlas->LockRect(0, &lr, nullptr, 0))) { atlas->Release(); return; }
    for (UINT y = 0; y < atlasH; ++y)
        memset(reinterpret_cast<BYTE*>(lr.pBits) + static_cast<size_t>(y) * lr.Pitch,
               0, static_cast<size_t>(atlasW) * sizeof(uint32_t));
    UINT x = 0;
    for (const GlyphPixels& g : glyphs) {
        for (UINT y = 0; y < g.h; ++y) {
            memcpy(reinterpret_cast<BYTE*>(lr.pBits) + static_cast<size_t>(y) * lr.Pitch
                       + static_cast<size_t>(x) * sizeof(uint32_t),
                   &g.px[static_cast<size_t>(y) * g.w], static_cast<size_t>(g.w) * sizeof(uint32_t));
        }
        Icon ic;
        ic.u0 = static_cast<float>(x) / atlasW;
        ic.v0 = 0.0f;
        ic.u1 = static_cast<float>(x + g.w) / atlasW;
        ic.v1 = static_cast<float>(g.h) / atlasH;
        ic.w = g.w; ic.h = g.h;
        g_icons[g.key] = ic;
        x += g.w + kPad;
    }
    atlas->UnlockRect(0);
    g_atlasTex = atlas;
}

// Notation grammar: optional prefix (j./dj./jc./c./f.), icon tokens (1-9, A/B/C/D/S, 5 = neutral),
// then a text suffix. Only the first whitespace token is parsed, case-sensitively.
void SplitNotation(const std::string& notation, std::string& prefixOut,
                   std::string& iconsOut, std::string& suffixOut) {
    prefixOut.clear(); iconsOut.clear(); suffixOut.clear();
    std::string source = notation;
    if (!source.empty() && source[0] == '~') {
        prefixOut = "~ ";
        source.erase(0, 1);
        const size_t first = source.find_first_not_of(" \t");
        source = first == std::string::npos ? std::string() : source.substr(first);
    }
    std::string upper = source;
    for (char& ch : upper) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    if (upper == "IC" || upper == "BIC" || upper == "FIC" ||
        upper == "RG" || upper == "FM" || upper == "RED IC" ||
        upper == "BLUE IC") {
        suffixOut = source;
        return;
    }

    const size_t sp = source.find(' ');
    const std::string head = source.substr(0, sp);
    if (sp != std::string::npos) suffixOut = source.substr(sp + 1);
    size_t i = 0;
    static const char* const knownPrefixes[] = {"dj.", "jc.", "j.", "c.", "f."};
    for (const char* known : knownPrefixes) {
        const size_t length = strlen(known);
        if (head.compare(0, length, known) == 0) {
            prefixOut += known;
            i = length;
            break;
        }
    }
    for (; i < head.size(); ++i) {
        const char c = head[i];
        if (c >= '1' && c <= '9') {
            iconsOut.push_back(c);
        } else if (c == 'A' || c == 'B' || c == 'C' || c == 'D' || c == 'S') {
            iconsOut.push_back(c);
        } else {
            const std::string rest = head.substr(i);
            suffixOut = suffixOut.empty() ? rest : rest + " " + suffixOut;
            break;
        }
    }
    if (iconsOut.size() > 1 && iconsOut[0] == '5') iconsOut.erase(0, 1);
    if (prefixOut.empty() && iconsOut.empty() && suffixOut.empty() && !head.empty())
        suffixOut = head;
}

enum class RichPieceKind { Text, Icon };

namespace TutorialColors = ::Mission::TutorialColorPolicy;

struct RichPiece {
    RichPieceKind kind = RichPieceKind::Text;
    std::string text;
    char icon = 0;
    TutorialColors::Tone tone = TutorialColors::Tone::Default;
};

struct RichAtom {
    std::vector<RichPiece> pieces;
    bool space = false;
    bool newline = false;
};

struct PositionedRichPiece {
    size_t atom = 0;
    size_t piece = 0;
    float x = 0.0f;
    float lineY = 0.0f;
    float width = 0.0f;
};

struct CachedRichLayout {
    ImFont* font = nullptr;
    uint64_t iconGeneration = 0;
    float fontFingerprint = 0.0f;
    float fontPx = 0.0f;
    float maxWidth = 0.0f;
    float iconHeight = 0.0f;
    float lineHeight = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    std::vector<PositionedRichPiece> pieces;
};

struct CachedRichDocument {
    std::vector<RichAtom> atoms;
    std::vector<CachedRichLayout> layouts;
};

struct RecipeFingerprint {
    uint64_t first = UINT64_C(1469598103934665603);
    uint64_t second = UINT64_C(1099511628211) ^ UINT64_C(0x9E3779B97F4A7C15);

    void AddByte(uint8_t value) {
        first ^= value;
        first *= UINT64_C(1099511628211);
        second ^= static_cast<uint64_t>(value) + UINT64_C(0x9D);
        second *= UINT64_C(14029467366897019727);
        second ^= second >> 29;
    }

    void AddU64(uint64_t value) {
        for (unsigned shift = 0; shift < 64; shift += 8) {
            AddByte(static_cast<uint8_t>(value >> shift));
        }
    }

    void AddI32(int value) {
        AddU64(static_cast<uint64_t>(static_cast<int64_t>(value)));
    }

    void AddBool(bool value) {
        AddByte(value ? uint8_t{1} : uint8_t{0});
    }

    void AddString(const std::string& value) {
        AddU64(static_cast<uint64_t>(value.size()));
        for (unsigned char ch : value) AddByte(ch);
    }
};

::Mission::EntityPresentationPolicy::RequirementCounts PresentationCounts(
    const ::Mission::EntityContactRequirement& requirement) {
    ::Mission::EntityPresentationPolicy::RequirementCounts counts;
    counts.recordedContacts = requirement.contactsRequired;
    counts.recordedComboHits = requirement.comboHitsRequired;
    counts.minimumContacts = requirement.minimumContactsRequired;
    counts.minimumComboHits = requirement.minimumComboHitsRequired;
    counts.fanout = !requirement.fanoutMembers.empty();
    return counts;
}

RecipeFingerprint FingerprintRecipe(const ::Mission::Mission& mission) {
    RecipeFingerprint fingerprint;
    fingerprint.AddString(mission.player.character);
    fingerprint.AddU64(static_cast<uint64_t>(mission.steps.size()));
    for (const ::Mission::Step& step : mission.steps) {
        fingerprint.AddString(step.notation);
        fingerprint.AddU64(static_cast<uint64_t>(step.moveIds.size()));
        for (int moveId : step.moveIds) fingerprint.AddI32(moveId);
        fingerprint.AddBool(step.entityCommand.present);
        fingerprint.AddI32(step.entityCommand.slot);
        fingerprint.AddI32(step.entityCommand.generation);
        fingerprint.AddI32(step.entityCommand.rootPattern);
        fingerprint.AddI32(step.entityCommand.activationPattern);
        fingerprint.AddI32(step.expectedAttackMask);
        fingerprint.AddByte(static_cast<uint8_t>(step.req));
        fingerprint.AddI32(step.hitsRequired);
        fingerprint.AddBool(step.allowPartialHits);
        fingerprint.AddBool(step.optional);
        fingerprint.AddI32(step.maxDelay);
        fingerprint.AddI32(step.maxGap);
        fingerprint.AddBool(step.comboEndAfter);
        fingerprint.AddI32(step.charState);
        fingerprint.AddI32(step.damage);
        fingerprint.AddBool(step.directContact);
        fingerprint.AddString(step.contactResult);
    }
    fingerprint.AddBool(mission.strictEntityContacts);
    fingerprint.AddU64(static_cast<uint64_t>(mission.entityContacts.size()));
    for (const ::Mission::EntityContactRequirement& requirement :
         mission.entityContacts) {
        fingerprint.AddString(requirement.notation);
        fingerprint.AddI32(requirement.owner);
        fingerprint.AddI32(requirement.target);
        fingerprint.AddI32(requirement.slot);
        fingerprint.AddI32(requirement.generation);
        fingerprint.AddU64(static_cast<uint64_t>(requirement.patterns.size()));
        for (int pattern : requirement.patterns) fingerprint.AddI32(pattern);
        fingerprint.AddString(requirement.result);
        fingerprint.AddI32(requirement.contactsRequired);
        fingerprint.AddI32(requirement.comboHitsRequired);
        fingerprint.AddI32(requirement.minimumContactsRequired);
        fingerprint.AddI32(requirement.minimumComboHitsRequired);
        fingerprint.AddU64(
            static_cast<uint64_t>(requirement.fanoutMembers.size()));
        fingerprint.AddString(requirement.producerLifecycle);
        fingerprint.AddI32(requirement.producerPattern);
        fingerprint.AddI32(requirement.producerPriorPattern);
        fingerprint.AddI32(requirement.opensAfterAction);
        fingerprint.AddI32(requirement.semanticSourceAction);
        fingerprint.AddI32(requirement.semanticSourceMove);
        fingerprint.AddI32(requirement.contactAfterAction);
        fingerprint.AddI32(requirement.afterStep);
        fingerprint.AddI32(requirement.afterStepContact);
        fingerprint.AddI32(requirement.dueBeforeStep);
        fingerprint.AddI32(requirement.dueBeforeStepContact);
        fingerprint.AddI32(requirement.segment);
        fingerprint.AddI32(requirement.maxDelay);
        fingerprint.AddI32(requirement.damage);
        fingerprint.AddBool(requirement.comboEndAfter);
    }
    fingerprint.AddU64(
        static_cast<uint64_t>(mission.entityLifecycles.size()));
    for (const ::Mission::EntityLifecycleRequirement& requirement :
         mission.entityLifecycles) {
        fingerprint.AddString(requirement.notation);
        fingerprint.AddI32(requirement.owner);
        fingerprint.AddI32(requirement.slot);
        fingerprint.AddI32(requirement.generation);
        fingerprint.AddString(requirement.lifecycle);
        fingerprint.AddI32(requirement.pattern);
        fingerprint.AddI32(requirement.priorPattern);
        fingerprint.AddI32(requirement.opensAfterAction);
        fingerprint.AddI32(requirement.segment);
        fingerprint.AddI32(requirement.maxDelay);
    }
    return fingerprint;
}

uint32_t FloatIdentity(float value) {
    uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "float identity assumes 32-bit float");
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

struct RecipeLayoutKey {
    uint64_t recipeFirst = 0;
    uint64_t recipeSecond = 0;
    uint64_t layoutGeneration = 0;
    uint64_t iconGeneration = 0;
    uintptr_t fontIdentity = 0;
    uint32_t fontSize = 0;
    uint32_t fontAdvanceM = 0;
    uint32_t fontAdvanceZero = 0;
    uint32_t originX = 0;
    uint32_t originY = 0;
    uint32_t scale = 0;
    int stepCount = 0;
    int currentStep = 0;
    int currentHits = 0;
    bool armed = false;

    bool operator==(const RecipeLayoutKey& other) const {
        return recipeFirst == other.recipeFirst &&
               recipeSecond == other.recipeSecond &&
               layoutGeneration == other.layoutGeneration &&
               iconGeneration == other.iconGeneration &&
               fontIdentity == other.fontIdentity &&
               fontSize == other.fontSize &&
               fontAdvanceM == other.fontAdvanceM &&
               fontAdvanceZero == other.fontAdvanceZero &&
               originX == other.originX &&
               originY == other.originY &&
               scale == other.scale &&
               stepCount == other.stepCount &&
               currentStep == other.currentStep &&
               currentHits == other.currentHits &&
               armed == other.armed;
    }
};

constexpr size_t kMaxRichDocuments = 512;
constexpr size_t kMaxLayoutsPerDocument = 12;
std::map<std::string, CachedRichDocument> g_richDocuments;

void AppendRichAtom(std::vector<RichAtom>& out, RichAtom atom, bool glue) {
    if (atom.pieces.empty()) return;
    if (glue && !out.empty() && !out.back().space && !out.back().newline) {
        std::vector<RichPiece>& destination = out.back().pieces;
        destination.insert(destination.end(),
                           std::make_move_iterator(atom.pieces.begin()),
                           std::make_move_iterator(atom.pieces.end()));
        return;
    }
    out.push_back(std::move(atom));
}

void AppendTextAtoms(
    std::vector<RichAtom>& out, const std::string& text,
    TutorialColors::Tone tone = TutorialColors::Tone::Default,
    bool glueFirst = false) {
    std::string word;
    bool glue = glueFirst;
    auto flush = [&]() {
        if (word.empty()) return;
        RichAtom atom;
        RichPiece piece;
        piece.text.swap(word);
        piece.tone = tone;
        atom.pieces.push_back(std::move(piece));
        AppendRichAtom(out, std::move(atom), glue);
        glue = false;
    };
    for (char ch : text) {
        if (ch == '\n') {
            flush();
            RichAtom atom; atom.newline = true; out.push_back(atom);
            glue = false;
        } else if (std::isspace(static_cast<unsigned char>(ch))) {
            flush();
            if (out.empty() || (!out.back().space && !out.back().newline)) {
                RichAtom atom; atom.space = true; out.push_back(atom);
            }
            glue = false;
        } else {
            word.push_back(ch);
        }
    }
    flush();
}

RichAtom NotationAtom(const std::string& notation) {
    RichAtom atom;
    std::string prefix, icons, suffix;
    SplitNotation(notation, prefix, icons, suffix);
    if (!prefix.empty()) atom.pieces.push_back(
        {RichPieceKind::Text, prefix, 0, TutorialColors::Tone::Default});
    for (char icon : icons) atom.pieces.push_back(
        {RichPieceKind::Icon, {}, icon, TutorialColors::Tone::Default});
    if (!suffix.empty()) {
        if (!atom.pieces.empty()) {
            atom.pieces.push_back(
                {RichPieceKind::Text, " ", 0, TutorialColors::Tone::Default});
        }
        atom.pieces.push_back(
            {RichPieceKind::Text, suffix, 0, TutorialColors::Tone::Default});
    }
    if (atom.pieces.empty()) atom.pieces.push_back(
        {RichPieceKind::Text, notation, 0, TutorialColors::Tone::Default});
    return atom;
}

std::vector<RichAtom> ParseRichAtoms(const std::string& source) {
    std::vector<RichAtom> out;
    std::string plain;
    bool plainGlue = false;
    bool glueNextPlain = false;
    auto appendPlain = [&](char ch) {
        if (plain.empty()) {
            plainGlue = glueNextPlain;
            glueNextPlain = false;
        }
        plain.push_back(ch);
    };
    auto flushPlain = [&]() {
        AppendTextAtoms(out, plain, TutorialColors::Tone::Default, plainGlue);
        plain.clear();
        plainGlue = false;
    };
    for (size_t i = 0; i < source.size();) {
        if (source[i] == '{' && i + 1 < source.size() && source[i + 1] == '{') {
            appendPlain('{'); i += 2; continue;
        }
        if (source[i] == '}' && i + 1 < source.size() && source[i + 1] == '}') {
            appendPlain('}'); i += 2; continue;
        }
        if (source[i] != '{') { appendPlain(source[i++]); continue; }
        const size_t close = source.find('}', i + 1);
        if (close == std::string::npos) { appendPlain(source[i++]); continue; }
        const bool glueToken = i > 0 &&
            !std::isspace(static_cast<unsigned char>(source[i - 1]));
        flushPlain();
        const std::string token = source.substr(i + 1, close - i - 1);
        const size_t colon = token.find(':');
        const std::string kind = colon == std::string::npos ? token : token.substr(0, colon);
        const std::string value = colon == std::string::npos ? std::string() : token.substr(colon + 1);
        if (kind == "dir" || kind == "btn" || kind == "input") {
            AppendRichAtom(out, NotationAtom(value), glueToken);
        } else if (kind == "term") {
            const size_t bar = value.find('|');
            RichAtom atom;
            atom.pieces.push_back({
                RichPieceKind::Text,
                bar == std::string::npos ? value
                    : value.substr(0, bar) + " (" + value.substr(bar + 1) + ")",
                0, TutorialColors::Tone::Accent});
            AppendRichAtom(out, std::move(atom), glueToken);
        } else if (kind == "tone") {
            const size_t bar = value.find('|');
            const std::string key = bar == std::string::npos
                ? value : value.substr(0, bar);
            const std::string label = bar == std::string::npos
                ? value : value.substr(bar + 1);
            RichAtom atom;
            atom.pieces.push_back({
                RichPieceKind::Text, label, 0,
                TutorialColors::ParseTone(key.c_str())});
            AppendRichAtom(out, std::move(atom), glueToken);
        } else if (kind == "ui") {
            std::string label = value;
            for (char& ch : label) ch = static_cast<char>(
                std::toupper(static_cast<unsigned char>(ch)));
            RichAtom atom;
            atom.pieces.push_back(
                {RichPieceKind::Text, "[" + label + "]", 0,
                 TutorialColors::Tone::Accent});
            AppendRichAtom(out, std::move(atom), glueToken);
        } else {
            RichAtom atom;
            atom.pieces.push_back(
                {RichPieceKind::Text, "{" + token + "}", 0,
                 TutorialColors::Tone::Accent});
            AppendRichAtom(out, std::move(atom), glueToken);
        }
        glueNextPlain = close + 1 < source.size() &&
            !std::isspace(static_cast<unsigned char>(source[close + 1]));
        i = close + 1;
    }
    flushPlain();
    return out;
}

float IconWidth(char icon, float iconHeight) {
    if (icon == '5') return iconHeight;
    const auto it = g_icons.find(icon);
    if (it != g_icons.end() && it->second.h > 0) {
        return iconHeight * static_cast<float>(it->second.w) /
               static_cast<float>(it->second.h);
    }
    return iconHeight * 0.78f;
}

float PieceWidth(ImFont* font, float fontPx, float iconHeight,
                 const RichPiece& piece) {
    if (piece.kind == RichPieceKind::Icon) return IconWidth(piece.icon, iconHeight);
    return piece.text.empty() ? 0.0f
        : font->CalcTextSizeA(fontPx, FLT_MAX, 0.0f, piece.text.c_str()).x;
}

CachedRichDocument& RichDocument(const std::string& source) {
    auto it = g_richDocuments.find(source);
    if (it != g_richDocuments.end()) return it->second;
    if (g_richDocuments.size() >= kMaxRichDocuments) g_richDocuments.clear();
    CachedRichDocument doc;
    doc.atoms = ParseRichAtoms(source);
    return g_richDocuments.emplace(source, std::move(doc)).first->second;
}

CachedRichLayout& RichLayout(CachedRichDocument& doc, ImFont* font,
                             float fontPx, float maxWidth) {
    const float fontFingerprint = font->FontSize +
        font->GetCharAdvance(static_cast<ImWchar>('M')) * 3.0f +
        font->GetCharAdvance(static_cast<ImWchar>('0')) * 5.0f;
    for (CachedRichLayout& layout : doc.layouts) {
        if (layout.font == font &&
            layout.iconGeneration == g_iconLayoutGeneration &&
            std::abs(layout.fontPx - fontPx) < 0.01f &&
            std::abs(layout.maxWidth - maxWidth) < 0.01f &&
            std::abs(layout.fontFingerprint - fontFingerprint) < 0.01f) {
            return layout;
        }
    }
    if (doc.layouts.size() >= kMaxLayoutsPerDocument) {
        doc.layouts.erase(doc.layouts.begin());
    }

    CachedRichLayout layout;
    layout.font = font;
    layout.iconGeneration = g_iconLayoutGeneration;
    layout.fontFingerprint = fontFingerprint;
    layout.fontPx = fontPx;
    layout.maxWidth = maxWidth;
    layout.iconHeight = (std::max)(12.0f, fontPx + 3.0f);
    layout.lineHeight = (std::max)(fontPx, layout.iconHeight);
    const float lineGap = 3.0f;
    const float spaceWidth = font->CalcTextSizeA(fontPx, FLT_MAX, 0.0f, " ").x;
    float x = 0.0f;
    float y = 0.0f;
    float maxUsedWidth = 0.0f;
    bool haveContent = false;

    for (size_t atomIndex = 0; atomIndex < doc.atoms.size(); ++atomIndex) {
        const RichAtom& atom = doc.atoms[atomIndex];
        if (atom.newline) {
            maxUsedWidth = (std::max)(maxUsedWidth, x);
            x = 0.0f;
            y += layout.lineHeight + lineGap;
            haveContent = true;
            continue;
        }
        if (atom.space) {
            if (x > 0.0f) x += spaceWidth;
            continue;
        }

        std::vector<float> widths;
        widths.reserve(atom.pieces.size());
        float atomWidth = 0.0f;
        for (const RichPiece& piece : atom.pieces) {
            const float width = PieceWidth(font, fontPx, layout.iconHeight, piece);
            widths.push_back(width);
            atomWidth += width;
        }
        if (x > 0.0f && x + atomWidth > maxWidth) {
            x = 0.0f;
            y += layout.lineHeight + lineGap;
        }
        float pieceX = x;
        for (size_t pieceIndex = 0; pieceIndex < atom.pieces.size(); ++pieceIndex) {
            layout.pieces.push_back(
                {atomIndex, pieceIndex, pieceX, y, widths[pieceIndex]});
            pieceX += widths[pieceIndex];
        }
        x += atomWidth;
        maxUsedWidth = (std::max)(maxUsedWidth, x);
        haveContent = true;
    }
    layout.width = maxUsedWidth;
    layout.height = haveContent ? y + layout.lineHeight : 0.0f;
    doc.layouts.push_back(std::move(layout));
    return doc.layouts.back();
}

void DrawNeutralTile(ImDrawList* dl, ImFont* font, float fontPx,
                     float x, float y, float size, ImU32 tint) {
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + size, y + size),
                      IM_COL32(15, 40, 44, 235), 2.0f);
    dl->AddRect(ImVec2(x, y), ImVec2(x + size, y + size),
                IM_COL32(105, 235, 235, 230), 2.0f, 0, 1.0f);
    const float w = font->CalcTextSizeA(fontPx, FLT_MAX, 0.0f, "5").x;
    dl->AddText(font, fontPx, ImVec2(x + (size - w) * 0.5f,
                y + (size - fontPx) * 0.5f), tint, "5");
}

float LayoutRichText(IDirect3DDevice9* dev, ImDrawList* dl, ImFont* font,
                     float fontPx, float x0, float y0, ImU32 color,
                     const std::string& text, float maxWidth, bool draw) {
    if (!font || text.empty() || maxWidth <= 0.0f) return 0.0f;
    EnsureIcons(dev);
    CachedRichDocument& doc = RichDocument(text);
    CachedRichLayout& layout = RichLayout(doc, font, fontPx, maxWidth);
    if (!draw || !dl) return layout.height;

    for (const PositionedRichPiece& placed : layout.pieces) {
        const RichPiece& piece = doc.atoms[placed.atom].pieces[placed.piece];
        if (piece.kind == RichPieceKind::Icon) {
            const float iconY = y0 + placed.lineY +
                                (layout.lineHeight - layout.iconHeight) * 0.5f;
            if (piece.icon == '5') {
                DrawNeutralTile(dl, font,
                                (std::min)(fontPx, layout.iconHeight - 2.0f),
                                x0 + placed.x, iconY, layout.iconHeight, color);
            } else {
                const auto it = g_icons.find(piece.icon);
                if (it != g_icons.end() && g_atlasTex) {
                    dl->AddImage(reinterpret_cast<ImTextureID>(g_atlasTex),
                                 ImVec2(x0 + placed.x, iconY),
                                 ImVec2(x0 + placed.x + placed.width,
                                        iconY + layout.iconHeight),
                                 ImVec2(it->second.u0, it->second.v0),
                                 ImVec2(it->second.u1, it->second.v1), color);
                } else {
                    const char glyph[2] = {piece.icon, 0};
                    dl->AddText(font, fontPx,
                                ImVec2(x0 + placed.x,
                                       y0 + placed.lineY +
                                       (layout.lineHeight - fontPx) * 0.5f),
                                color, glyph);
                }
            }
        } else {
            ImU32 pieceColor = color;
            if (piece.tone != TutorialColors::Tone::Default) {
                const TutorialColors::Rgba semantic =
                    TutorialColors::Color(piece.tone);
                pieceColor = IM_COL32(
                    semantic.r, semantic.g, semantic.b, semantic.a);
            }
            dl->AddText(font, fontPx,
                        ImVec2(x0 + placed.x,
                               y0 + placed.lineY +
                               (layout.lineHeight - fontPx) * 0.5f),
                        pieceColor, piece.text.c_str());
        }
    }
    return layout.height;
}

}

float MeasureRichTextHeight(void* device, ImFont* font, float fontPx,
                            const std::string& text, float maxWidth) {
    return MeasureRichText(device, font, fontPx, text, maxWidth).height;
}

RichTextMetrics MeasureRichText(void* device, ImFont* font, float fontPx,
                                const std::string& text, float maxWidth) {
    IDirect3DDevice9* dev = reinterpret_cast<IDirect3DDevice9*>(device);
    ImFont* resolvedFont = font ? font : ImGui::GetFont();
    if (!resolvedFont || text.empty() || maxWidth <= 0.0f) return {};
    EnsureIcons(dev);
    CachedRichDocument& doc = RichDocument(text);
    CachedRichLayout& layout = RichLayout(
        doc, resolvedFont, fontPx, maxWidth);
    RichTextMetrics metrics;
    metrics.width = layout.width;
    metrics.height = layout.height;
    return metrics;
}

void DrawRichText(void* device, ImDrawList* dl, ImFont* font, float fontPx,
                  float x, float y, unsigned int color,
                  const std::string& text, float maxWidth) {
    if (!dl) return;
    IDirect3DDevice9* dev = reinterpret_cast<IDirect3DDevice9*>(device);
    (void)LayoutRichText(dev, dl, font ? font : ImGui::GetFont(), fontPx,
                         x, y, static_cast<ImU32>(color), text, maxWidth, true);
}

bool WantsDraw() {
    return Engine::Runner::IsActive() &&
           !PracticeMenu::TitleScreen::WantsDraw();
}

void Draw(void* device, ImDrawList* dl, float ox, float oy, float scale) {
    if (!dl || !device || !WantsDraw()) return;
    ::Mission::Mission mission;
    int cur = 0;
    int failed = -1;
    int curHits = 0;
    int entitySegment = 0;
    int failedEntityRequirement = -1;
    bool armed = false;
    static std::vector<int> entityContactsSeen;
    static std::vector<int> entityComboHitsSeen;
    static std::vector<int> entityLifecyclesSeen;
    if (!Engine::Runner::GetRenderSnapshot(
            mission, cur, failed, armed, curHits,
            &entityContactsSeen, &entityComboHitsSeen,
            &entitySegment, &failedEntityRequirement,
            true,
            &entityLifecyclesSeen) ||
        mission.steps.empty()) return;
    if (failedEntityRequirement >= 0) failed = -1;
    const ::Mission::Mission* m = &mission;

    IDirect3DDevice9* dev = reinterpret_cast<IDirect3DDevice9*>(device);
    EnsureIcons(dev);

    const int total = static_cast<int>(m->steps.size());
    const bool tutorial = m->type == "tutorial";

    const float S = scale;
    const float iconH = 14.0f * S;
    const float pad = 2.0f * S;
    const float cellH = iconH + pad * 2.0f;
    const float cellGap = 14.0f * S;
    const float lineGap = 3.0f * S;
    const float fontSz = 11.0f * S;
    const float x0 = ox + 8.0f * S;
    const float xMax = ox + 632.0f * S;
    ImFont* font = ImGui::GetFont();

    if (tutorial) {
        const float panelX0 = ox + 8.0f * S;
        const float panelY0 = oy + 38.0f * S;
        const float panelX1 = ox + 632.0f * S;
        const float panelY1 = oy + 86.0f * S;
        dl->AddRectFilled(ImVec2(panelX0, panelY0), ImVec2(panelX1, panelY1),
                          IM_COL32(0, 0, 0, 185), 3.0f * S);
        dl->AddRectFilled(ImVec2(panelX0, panelY0), ImVec2(panelX0 + 3.0f * S, panelY1),
                          IM_COL32(105, 235, 235, 225));
        dl->AddText(font, 12.0f * S, ImVec2(panelX0 + 10.0f * S, panelY0 + 5.0f * S),
                    IM_COL32(255, 255, 255, 255), m->name.c_str());
        const std::string& guidance = !m->description.empty()
            ? m->description : (!m->hints.empty() ? m->hints.front() : std::string());
        if (!guidance.empty()) {
            dl->PushClipRect(ImVec2(panelX0 + 10.0f * S, panelY0 + 21.0f * S),
                             ImVec2(panelX1 - 8.0f * S, panelY1 - 3.0f * S), true);
            dl->AddText(font, 9.5f * S, ImVec2(panelX0 + 10.0f * S, panelY0 + 21.0f * S),
                        IM_COL32(190, 205, 210, 240), guidance.c_str(), nullptr,
                        panelX1 - panelX0 - 20.0f * S);
            dl->PopClipRect();
        }
    }

    struct Cell {
        std::string prefix, icons, text;
        float x = 0, y = 0, w = 0, pw = 0;
        bool setupBoundary = false;
    };
    struct EntityRow {
        std::string prefix;
        std::string icons;
        std::string text;
        std::string label;
        std::string result;
        const char* family = nullptr;
        const char* role = "ENTITY";
        ::Mission::EntityPresentationPolicy::Identity identity;
        std::vector<int> requirementIndices;
        int contactsRequired = 0;
        int comboHitsRequired = 0;
        bool comboEndAfter = false;
        bool customLabel = false;
        bool commandOrigin = false;
        float x = 0, y = 0, w = 0, pw = 0;
        int anchor = 0;
    };
    struct SetupMarker { float x = 0, y = 0, w = 0; };
    struct FlowDivider { float x = 0, y = 0; };
    struct FlowStrip { float y = 0, end = 0; };

    auto iconW = [&](char c) -> float {
        if (c == '5') return iconH;
        auto it = g_icons.find(c);
        if (it != g_icons.end() && it->second.h > 0) {
            return iconH * (static_cast<float>(it->second.w) / static_cast<float>(it->second.h));
        }
        return iconH * 0.75f;
    };

    const RecipeFingerprint recipeFingerprint = FingerprintRecipe(*m);
    RecipeLayoutKey layoutKey;
    layoutKey.recipeFirst = recipeFingerprint.first;
    layoutKey.recipeSecond = recipeFingerprint.second;
    layoutKey.layoutGeneration = g_layoutInvalidationGeneration;
    layoutKey.iconGeneration = g_iconLayoutGeneration;
    layoutKey.fontIdentity = reinterpret_cast<uintptr_t>(font);
    layoutKey.fontSize = FloatIdentity(font->FontSize);
    layoutKey.fontAdvanceM = FloatIdentity(
        font->GetCharAdvance(static_cast<ImWchar>('M')));
    layoutKey.fontAdvanceZero = FloatIdentity(
        font->GetCharAdvance(static_cast<ImWchar>('0')));
    layoutKey.originX = FloatIdentity(ox);
    layoutKey.originY = FloatIdentity(oy);
    layoutKey.scale = FloatIdentity(scale);
    layoutKey.stepCount = total;
    layoutKey.currentStep = cur;
    layoutKey.currentHits = curHits;
    layoutKey.armed = armed;

    static RecipeLayoutKey s_layoutKey;
    static bool s_haveLayoutKey = false;
    static std::vector<Cell> s_cells;
    static std::vector<EntityRow> s_entityRows;
    static std::vector<std::vector<int>> s_inlineRequirements;
    static std::vector<SetupMarker> s_setupMarkers;
    static std::vector<FlowDivider> s_flowDividers;
    static std::vector<FlowStrip> s_flowStrips;
    {
        if (!s_haveLayoutKey || !(s_layoutKey == layoutKey)) {
            s_layoutKey = layoutKey;
            s_haveLayoutKey = true;
            s_cells.assign(total, Cell{});
            s_entityRows.clear();
            s_inlineRequirements.assign(static_cast<std::size_t>(total), {});
            s_setupMarkers.clear();
            s_flowDividers.clear();
            s_flowStrips.clear();
            for (int i = 0; i < total; ++i) {
                const ::Mission::Step& st = m->steps[i];
                std::string note = st.notation;
                if (note.empty()) {
                    char b[16];
                    _snprintf_s(b, sizeof(b), _TRUNCATE, "#%d",
                                st.moveIds.empty() ? 0 : st.moveIds[0]);
                    note = b;
                }
                SplitNotation(note, s_cells[i].prefix, s_cells[i].icons, s_cells[i].text);
                const bool followsEntityBoundary = std::any_of(
                    m->entityContacts.begin(), m->entityContacts.end(),
                    [i](const ::Mission::EntityContactRequirement& requirement) {
                        return requirement.comboEndAfter &&
                               requirement.dueBeforeStep == i;
                    });
                if ((i > 0 && m->steps[i - 1].comboEndAfter) ||
                    followsEntityBoundary) {
                    s_cells[i].setupBoundary = true;
                }
                if (st.req == ::Mission::StepReq::Hits && st.hitsRequired > 1) {
                    char b[16];
                    if (st.allowPartialHits) {
                        if (i == cur && armed) {
                            _snprintf_s(b, sizeof(b), _TRUNCATE, " %d hit%s",
                                        curHits, curHits == 1 ? "" : "s");
                        } else {
                            _snprintf_s(b, sizeof(b), _TRUNCATE, " x1+");
                        }
                    } else if (i == cur && armed) {
                        _snprintf_s(b, sizeof(b), _TRUNCATE, " %d/%d", curHits,
                                    st.hitsRequired);
                    } else {
                        _snprintf_s(b, sizeof(b), _TRUNCATE, " x%d", st.hitsRequired);
                    }
                    s_cells[i].text += b;
                }
                if (st.maxDelay > 0) s_cells[i].prefix = "~" + s_cells[i].prefix;
                if (st.optional)     s_cells[i].text += "?";
            }

            for (std::size_t i = 0; i < m->entityContacts.size(); ++i) {
                const ::Mission::EntityContactRequirement& requirement =
                    m->entityContacts[i];
                const auto presentationCounts =
                    PresentationCounts(requirement);
                const int presentedContacts =
                    ::Mission::EntityPresentationPolicy::RequiredContacts(
                        presentationCounts);
                const int primaryPattern = !requirement.patterns.empty()
                    ? requirement.patterns.front() : 0;
                const bool declaredExactSemanticSource =
                    ::Mission::SemanticSourcePolicy::IsExact(
                        requirement.semanticSourceAction,
                        requirement.semanticSourceMove);
                const bool legacySemanticSource =
                    ::Mission::SemanticSourcePolicy::IsLegacyAbsent(
                        requirement.semanticSourceAction,
                        requirement.semanticSourceMove);
                const ::Mission::Step* declaredSourceStep =
                    declaredExactSemanticSource &&
                    requirement.semanticSourceAction >= 0 &&
                    requirement.semanticSourceAction < total
                        ? &m->steps[static_cast<std::size_t>(
                              requirement.semanticSourceAction)]
                        : nullptr;
                const bool exactSemanticSource = declaredSourceStep &&
                    std::find(declaredSourceStep->moveIds.begin(),
                              declaredSourceStep->moveIds.end(),
                              requirement.semanticSourceMove) !=
                        declaredSourceStep->moveIds.end();
                const int commandAction = requirement.opensAfterAction;
                const ::Mission::Step* declaredCommandStep =
                    commandAction >= 0 && commandAction < total &&
                    m->steps[static_cast<std::size_t>(commandAction)]
                        .entityCommand.present
                        ? &m->steps[static_cast<std::size_t>(commandAction)]
                        : nullptr;
                const auto* boundCommandOrigin = declaredCommandStep
                    ? ::Mission::EntityCommandOriginPolicy::
                          ValidateBoundCommand(
                              m->player.character.c_str(),
                              declaredCommandStep->entityCommand.slot,
                              declaredCommandStep->entityCommand.generation,
                              declaredCommandStep->entityCommand.rootPattern,
                              declaredCommandStep->entityCommand
                                  .activationPattern,
                              declaredCommandStep->expectedAttackMask)
                    : nullptr;
                const bool commandObjectiveMatches =
                    declaredCommandStep && requirement.fanoutMembers.empty() &&
                    ::Mission::EntityCommandOriginPolicy::
                        ContactObjectiveBindsCommand(
                            boundCommandOrigin,
                            declaredCommandStep->entityCommand.slot,
                            declaredCommandStep->entityCommand.generation,
                            requirement.slot,
                            requirement.generation,
                            requirement.patterns.size() == 1,
                            requirement.patterns.empty()
                                ? -1
                                : requirement.patterns.front(),
                            requirement.producerLifecycle == "morph",
                            requirement.producerLifecycle == "spawn",
                            requirement.producerPattern,
                            requirement.producerPriorPattern);
                const bool exactCommandSource = declaredCommandStep &&
                    declaredCommandStep->moveIds.empty() &&
                    requirement.opensAfterAction == commandAction &&
                    commandObjectiveMatches &&
                    requirement.contactAfterAction >= commandAction;
                const int ownerAction = exactCommandSource
                    ? commandAction
                    : ::Mission::EntityPresentationPolicy::SelectOwnerAction(
                          exactSemanticSource, legacySemanticSource,
                          requirement.semanticSourceAction,
                          requirement.opensAfterAction, total);
                const int producerMove = exactSemanticSource
                    ? requirement.semanticSourceMove
                    : ownerAction >= 0 &&
                              !m->steps[static_cast<std::size_t>(
                                  ownerAction)].moveIds.empty()
                        ? m->steps[static_cast<std::size_t>(ownerAction)]
                              .moveIds.front()
                        : -1;
                const auto* semantic = ::Mission::EntityNames::LookupSemantic(
                    m->player.character.c_str(), primaryPattern, producerMove);
                const char* outcome =
                    requirement.result == "hit" ? "HIT" :
                    requirement.result == "special" ? "SPECIAL HIT" :
                    requirement.result == "block" ? "BLOCK" :
                    requirement.result == "recoil_guard" ? "RG" :
                    requirement.result == "throw" ? "THROW" :
                    requirement.result == "guard_point" ? "GUARD POINT" :
                    "CONTACT";

                const auto* persistedSemanticCandidate =
                    requirement.patterns.size() == 1
                    ? ::Mission::EntityNames::
                          LookupSemanticForGeneratedContactNotation(
                              m->player.character.c_str(), primaryPattern,
                              outcome, presentedContacts,
                              requirement.notation)
                    : nullptr;
                const bool persistedSemanticMatchesSource =
                    ::Mission::EntityPresentationPolicy::
                        PersistedSemanticMatchesSource(
                            persistedSemanticCandidate != nullptr,
                            exactSemanticSource,
                            persistedSemanticCandidate
                                ? persistedSemanticCandidate->producerMove : -1,
                            producerMove);
                const auto* persistedSemantic =
                    persistedSemanticMatchesSource
                        ? persistedSemanticCandidate : nullptr;
                if (persistedSemantic &&
                    (!semantic || semantic->producerMove < 0) &&
                    (!exactSemanticSource ||
                     persistedSemantic->producerMove == producerMove)) {
                    semantic = persistedSemantic;
                }
                if (semantic && !::Mission::EntityNames::CanOwnRecordedContact(
                                    semantic->disposition)) {
                    continue;
                }
                const bool semanticResolved = semantic &&
                    ::Mission::EntityNames::HasResolvedContactPresentation(
                        semantic->disposition);
                const bool mapped = exactCommandSource || semanticResolved ||
                    (!semantic && primaryPattern > 0 &&
                     ::Mission::EntityNames::Lookup(
                         m->player.character.c_str(), primaryPattern) != nullptr);

                const bool generated = exactCommandSource ||
                    requirement.notation.empty() ||
                    persistedSemantic != nullptr ||
                    (requirement.patterns.size() == 1 &&
                     ::Mission::EntityNames::IsGeneratedContactNotation(
                        m->player.character.c_str(), primaryPattern, outcome,
                        presentedContacts, requirement.notation,
                        producerMove));
                const bool customLabel = !generated;
                const auto* commandOrigin = exactCommandSource &&
                        generated && !customLabel &&
                        requirement.patterns.size() == 1
                    ? boundCommandOrigin
                    : nullptr;

                ::Mission::EntityPresentationPolicy::Timing timing;
                timing.opensAfterAction = ownerAction;
                timing.contactAfterAction = requirement.contactAfterAction;
                timing.afterStep = requirement.afterStep;
                timing.afterStepContact = requirement.afterStepContact;
                timing.dueBeforeStep = requirement.dueBeforeStep;
                timing.dueBeforeStepContact = requirement.dueBeforeStepContact;
                timing.comboEndAfter = requirement.comboEndAfter;
                const bool failedRequirement =
                    static_cast<int>(i) == failedEntityRequirement;
                const bool semanticStandalone = semanticResolved &&
                    ::Mission::EntityNames::IsAlwaysStandalone(semantic->role);
                const ::Mission::Step* ownerStep = ownerAction >= 0
                    ? &m->steps[static_cast<std::size_t>(ownerAction)]
                    : nullptr;
                int sourceSegment = 0;
                if (ownerAction >= 0) {
                    for (int stepIndex = 0; stepIndex < ownerAction;
                         ++stepIndex) {
                        if (m->steps[static_cast<std::size_t>(stepIndex)]
                                .comboEndAfter) {
                            ++sourceSegment;
                        }
                    }
                    for (std::size_t priorIndex = 0; priorIndex < i;
                         ++priorIndex) {
                        const auto& prior = m->entityContacts[priorIndex];
                        if (prior.comboEndAfter &&
                            prior.dueBeforeStep >= 0 &&
                            prior.dueBeforeStep <= ownerAction) {
                            ++sourceSegment;
                        }
                    }
                }
                ::Mission::EntityPresentationPolicy::ExactProducerHit
                    exactProducerHit;
                exactProducerHit.action = ownerAction;
                exactProducerHit.exactSource =
                    exactSemanticSource || exactCommandSource;
                exactProducerHit.generated = generated && !customLabel;
                exactProducerHit.resolvedContact =
                    semanticResolved || exactCommandSource;
                exactProducerHit.actionEligible = ownerStep &&
                    !ownerStep->optional &&
                    (!ownerStep->moveIds.empty() || exactCommandSource);
                exactProducerHit.commandSource = exactCommandSource;
                exactProducerHit.ordinaryProjectile = semantic &&
                    semantic->role == ::Mission::EntityNames::
                        PresentationRole::InlineProjectile;
                exactProducerHit.producerMoveMatches = semantic && ownerStep &&
                    semantic->producerMove >= 0 &&
                    semantic->producerMove == producerMove;
                exactProducerHit.sourceSegmentMatches = ownerAction >= 0 &&
                    sourceSegment == requirement.segment;
                exactProducerHit.sourceComboEndBeforeContact = ownerStep &&
                    ownerStep->comboEndAfter;
                const bool inlineExactProducerHit =
                    ::Mission::EntityPresentationPolicy::
                        CanInlineExactProducerHit(exactProducerHit, timing);
                bool display = ::Mission::EntityPresentationPolicy::ShouldDisplay(
                    mapped,
                    (semanticStandalone && !inlineExactProducerHit) ||
                        customLabel,
                    failedRequirement, timing, inlineExactProducerHit);
                if (!display && ownerAction >= 0) {
                    s_inlineRequirements[static_cast<std::size_t>(ownerAction)]
                        .push_back(static_cast<int>(i));
                    continue;
                }

                EntityRow candidate;
                candidate.family = semanticResolved && !customLabel
                    ? semantic->family : nullptr;
                candidate.role = semanticResolved && !customLabel
                    ? ::Mission::EntityNames::RoleLabel(semantic->role)
                    : mapped ? "PROJECTILE" : "ENTITY";
                candidate.result = outcome;
                candidate.customLabel = customLabel;
                candidate.commandOrigin = commandOrigin != nullptr;
                candidate.comboEndAfter = requirement.comboEndAfter;
                candidate.label = customLabel
                    ? requirement.notation
                    : commandOrigin
                        ? commandOrigin->notation
                    : semanticResolved && semantic->label
                        ? semantic->label
                        : primaryPattern > 0
                            ? std::string("#") +
                                  std::to_string(primaryPattern)
                            : std::string("#?");
                if (candidate.commandOrigin &&
                    !::Mission::EntityNames::ContainsOutcomeCaseInsensitive(
                        candidate.label, outcome)) {
                    candidate.label += " (";
                    candidate.label += outcome;
                    candidate.label += ")";
                }
                if (!candidate.customLabel && !candidate.commandOrigin &&
                    !::Mission::EntityNames::ContainsOutcomeCaseInsensitive(
                        candidate.label, outcome)) {
                    candidate.label += " (";
                    candidate.label += outcome;
                    candidate.label += ")";
                }
                candidate.anchor = ::Mission::EntityPresentationPolicy::
                    DisplayAnchor(requirement.contactAfterAction,
                                  requirement.dueBeforeStep,
                                  requirement.afterStep, total);
                candidate.identity.family = candidate.family;
                candidate.identity.owner = requirement.owner;
                candidate.identity.target = requirement.target;
                candidate.identity.segment = requirement.segment;
                candidate.identity.slot = requirement.slot;
                candidate.identity.generation = requirement.generation;
                candidate.identity.producerAction = exactCommandSource
                    ? commandAction
                    : exactSemanticSource
                        ? requirement.semanticSourceAction
                        : -1;
                candidate.identity.contactAfterAction =
                    requirement.contactAfterAction;
                candidate.identity.afterStep = requirement.afterStep;
                candidate.identity.afterStepContact =
                    requirement.afterStepContact;
                candidate.identity.dueStep = requirement.dueBeforeStep;
                candidate.identity.dueContact =
                    requirement.dueBeforeStepContact;

                EntityRow* row = nullptr;
                if (candidate.family) {
                    for (auto existing = s_entityRows.rbegin();
                         existing != s_entityRows.rend(); ++existing) {
                        if (std::strcmp(existing->role, candidate.role) != 0 ||
                            existing->label != candidate.label ||
                            existing->comboEndAfter !=
                                candidate.comboEndAfter) {
                            continue;
                        }
                        if (::Mission::EntityPresentationPolicy::CanFold(
                                existing->identity, candidate.identity,
                                existing->result.c_str(),
                                candidate.result.c_str())) {
                            row = &*existing;
                            break;
                        }
                    }
                }
                if (!row) {
                    s_entityRows.push_back(std::move(candidate));
                    row = &s_entityRows.back();
                }
                row->requirementIndices.push_back(static_cast<int>(i));
                row->contactsRequired +=
                    ::Mission::EntityPresentationPolicy::
                        RequiredContacts(presentationCounts);
                row->comboHitsRequired +=
                    ::Mission::EntityPresentationPolicy::
                        RequiredComboHits(presentationCounts);
                row->comboEndAfter = row->comboEndAfter ||
                    requirement.comboEndAfter;
            }

            for (EntityRow& row : s_entityRows) {
                const std::string displayLabel = row.customLabel
                    ? row.label
                    : ::Mission::EntityPresentationPolicy::
                          FriendlyOutcomeLabel(row.label, row.result);
                SplitNotation(displayLabel, row.prefix, row.icons, row.text);
                if (!row.customLabel) {
                    row.text += ::Mission::EntityPresentationPolicy::
                        CompactCountSuffix(
                            row.contactsRequired, row.comboHitsRequired);
                }
            }

            auto textW = [&](const std::string& t) -> float {
                if (t.empty()) return 0.0f;
                return font->CalcTextSizeA(fontSz, FLT_MAX, 0.0f, t.c_str()).x + 1.0f * S;
            };
            std::vector<std::vector<std::size_t>> rowsAtAnchor(
                static_cast<std::size_t>(total + 1));
            for (std::size_t i = 0; i < s_entityRows.size(); ++i) {
                rowsAtAnchor[static_cast<std::size_t>(s_entityRows[i].anchor)]
                    .push_back(i);
            }
            float x = x0;
            float y = oy + 96.0f * S + fontSz + 4.0f * S;
            float lineEnd = x0;
            auto finishLine = [&]() {
                if (lineEnd > x0) s_flowStrips.push_back({y, lineEnd});
            };
            auto nextLine = [&]() {
                finishLine();
                x = x0;
                lineEnd = x0;
                y += cellH + lineGap;
            };
            auto placeFlowItem = [&](float width, float& itemX, float& itemY) {
                if (x + width > xMax && x > x0) nextLine();
                if (x > x0) {
                    s_flowDividers.push_back({x - cellGap * 0.5f, y});
                }
                itemX = x;
                itemY = y;
                lineEnd = (std::max)(lineEnd, x + width);
                x += width + cellGap;
            };
            for (int anchor = 0; anchor <= total; ++anchor) {
                for (std::size_t rowIndex :
                     rowsAtAnchor[static_cast<std::size_t>(anchor)]) {
                    EntityRow& row = s_entityRows[rowIndex];
                    row.pw = textW(row.prefix);
                    row.w = row.pw + textW(row.text);
                    for (char ch : row.icons) row.w += iconW(ch);
                    if (row.w < iconH) row.w = iconH;
                    placeFlowItem(row.w, row.x, row.y);
                }
                if (anchor == total) break;

                Cell& c = s_cells[static_cast<std::size_t>(anchor)];
                if (c.setupBoundary) {
                    SetupMarker marker;
                    marker.w = textW("SETUP") + 7.0f * S;
                    placeFlowItem(marker.w, marker.x, marker.y);
                    s_setupMarkers.push_back(marker);
                }
                c.pw = textW(c.prefix);
                float w = c.pw + textW(c.text);
                for (char ch : c.icons) w += iconW(ch);
                if (w < iconH) w = iconH;
                c.w = w;
                placeFlowItem(c.w, c.x, c.y);
            }
            finishLine();
        }
    }
    std::vector<Cell>& cells = s_cells;
    std::vector<EntityRow>& entityRows = s_entityRows;
    std::vector<SetupMarker>& setupMarkers = s_setupMarkers;
    std::vector<FlowDivider>& flowDividers = s_flowDividers;
    std::vector<FlowStrip>& flowStrips = s_flowStrips;
    const float headerY = oy + 96.0f * S;

    for (const FlowStrip& strip : flowStrips) {
        dl->AddRectFilled(ImVec2(x0 - 4.0f * S, strip.y - pad),
                          ImVec2(strip.end + 4.0f * S,
                                 strip.y + cellH + pad),
                          IM_COL32(0, 0, 0, 165), 3.0f * S);
    }

    {
        char b[32];
        const int shownStep = cur < total ? cur + 1 : total;
        _snprintf_s(b, sizeof(b), _TRUNCATE, "%s %d/%d",
                    tutorial ? "LESSON" : "TRIAL", shownStep, total);
        const float w = font->CalcTextSizeA(fontSz, FLT_MAX, 0.0f, b).x;
        dl->AddRectFilled(ImVec2(x0 - 4.0f * S, headerY - 2.0f * S),
                          ImVec2(x0 + w + 4.0f * S, headerY + fontSz + 2.0f * S),
                          IM_COL32(0, 0, 0, 165), 3.0f * S);
        dl->AddText(font, fontSz, ImVec2(x0, headerY),
                    IM_COL32(105, 235, 235, 255), b);
    }

    const ImU32 kTintDone    = IM_COL32(135, 235, 150, 235);
    const ImU32 kTintCurrent = IM_COL32(255, 255, 255, 255);
    const ImU32 kTintNext    = IM_COL32(255, 255, 255, 175);
    const ImU32 kTintFailed  = IM_COL32(255, 110, 110, 255);
    const ImU32 kTintPending = IM_COL32(255, 235, 110, 255);
    const ImU32 kSepCol      = IM_COL32(150, 160, 175, 190);
    auto entitySatisfied = [&](int requirementIndex) -> bool {
        if (requirementIndex < 0 ||
            requirementIndex >= static_cast<int>(m->entityContacts.size())) {
            return false;
        }
        const std::size_t index = static_cast<std::size_t>(requirementIndex);
        const ::Mission::EntityContactRequirement& requirement =
            m->entityContacts[index];
        const int contacts = index < entityContactsSeen.size()
            ? entityContactsSeen[index] : 0;
        const int comboHits = index < entityComboHitsSeen.size()
            ? entityComboHitsSeen[index] : 0;
        return ::Mission::EntityPresentationPolicy::RequirementSatisfied(
            PresentationCounts(requirement), contacts, comboHits);
    };
    auto inlinePending = [&](int stepIndex) -> bool {
        if (stepIndex < 0 ||
            stepIndex >= static_cast<int>(s_inlineRequirements.size())) {
            return false;
        }
        const auto& attached = s_inlineRequirements[
            static_cast<std::size_t>(stepIndex)];
        return std::any_of(attached.begin(), attached.end(),
            [&](int requirementIndex) {
                return !entitySatisfied(requirementIndex);
            });
    };
    auto inlineFailed = [&](int stepIndex) -> bool {
        if (stepIndex < 0 ||
            stepIndex >= static_cast<int>(s_inlineRequirements.size())) {
            return false;
        }
        const auto& attached = s_inlineRequirements[
            static_cast<std::size_t>(stepIndex)];
        return std::find(attached.begin(), attached.end(),
                         failedEntityRequirement) != attached.end();
    };
    auto lifecyclePending = [&](int stepIndex) -> bool {
        for (std::size_t i = 0; i < m->entityLifecycles.size(); ++i) {
            if (m->entityLifecycles[i].opensAfterAction != stepIndex) continue;
            if (i >= entityLifecyclesSeen.size() ||
                entityLifecyclesSeen[i] == 0) {
                return true;
            }
        }
        return false;
    };
    auto tintOf = [&](int i) -> ImU32 {
        if (i == failed || inlineFailed(i)) return kTintFailed;
        const bool inlineOwnerStarted = i < cur || (i == cur && armed);
        if ((inlinePending(i) || lifecyclePending(i)) &&
            inlineOwnerStarted) {
            return kTintPending;
        }
        if (i < cur)     return kTintDone;
        if (i == cur)    return kTintCurrent;
        return kTintNext;
    };
    int activeEntity = -1;
    for (std::size_t i = 0; i < m->entityContacts.size(); ++i) {
        if (!entitySatisfied(static_cast<int>(i))) {
            activeEntity = static_cast<int>(i);
            break;
        }
    }
    auto entityGateStarted = [&](int requirementIndex) -> bool {
        if (requirementIndex < 0 ||
            requirementIndex >= static_cast<int>(m->entityContacts.size())) {
            return false;
        }
        const ::Mission::EntityContactRequirement& requirement =
            m->entityContacts[static_cast<std::size_t>(requirementIndex)];
        const bool actionGate = requirement.opensAfterAction < 0 ||
            cur > requirement.opensAfterAction ||
            (cur == requirement.opensAfterAction && armed);
        const bool contactActionGate = requirement.contactAfterAction < 0 ||
            cur > requirement.contactAfterAction ||
            (cur == requirement.contactAfterAction && armed);
        const bool contactGate = requirement.afterStep < 0 ||
            cur > requirement.afterStep;
        return actionGate && contactActionGate && contactGate &&
               entitySegment == requirement.segment;
    };
    auto rowContains = [](const EntityRow& row, int requirementIndex) {
        return std::find(row.requirementIndices.begin(),
                         row.requirementIndices.end(), requirementIndex) !=
               row.requirementIndices.end();
    };
    auto entityRowSatisfied = [&](const EntityRow& row) -> bool {
        const int satisfiedMembers = static_cast<int>(std::count_if(
            row.requirementIndices.begin(), row.requirementIndices.end(),
            entitySatisfied));
        return ::Mission::EntityPresentationPolicy::AllSatisfied(
            satisfiedMembers,
            static_cast<int>(row.requirementIndices.size()));
    };
    auto entityRowGateStarted = [&](const EntityRow& row) -> bool {
        const auto unfinished = std::find_if(
            row.requirementIndices.begin(), row.requirementIndices.end(),
            [&](int requirementIndex) {
                return !entitySatisfied(requirementIndex);
            });
        return unfinished != row.requirementIndices.end() &&
               entityGateStarted(*unfinished);
    };
    auto entityRowHasProgress = [&](const EntityRow& row) -> bool {
        return std::any_of(
            row.requirementIndices.begin(), row.requirementIndices.end(),
            [&](int requirementIndex) {
                if (requirementIndex < 0) return false;
                const std::size_t index =
                    static_cast<std::size_t>(requirementIndex);
                return (index < entityContactsSeen.size() &&
                        entityContactsSeen[index] > 0) ||
                       (index < entityComboHitsSeen.size() &&
                        entityComboHitsSeen[index] > 0);
            });
    };
    auto entityTintOf = [&](const EntityRow& row) -> ImU32 {
        if (rowContains(row, failedEntityRequirement)) return kTintFailed;
        if (entityRowSatisfied(row)) return kTintDone;
        if (rowContains(row, activeEntity) &&
            entityRowGateStarted(row)) return kTintCurrent;
        return kTintNext;
    };

    for (const SetupMarker& marker : setupMarkers) {
        dl->AddRectFilled(ImVec2(marker.x, marker.y + 2.0f * S),
                          ImVec2(marker.x + 1.5f * S,
                                 marker.y + cellH - 2.0f * S),
                          IM_COL32(105, 235, 235, 230));
    }
    for (int i = 0; i < total; ++i) {
        const bool inlineOwnerStarted = i < cur || (i == cur && armed);
        const bool attachedPending = inlinePending(i) && inlineOwnerStarted;
        const bool attachedFailed = inlineFailed(i);
        if (i != cur && i != failed && !attachedPending && !attachedFailed) continue;
        const Cell& c = cells[i];
        ImU32 bar = IM_COL32(105, 235, 235, 230);
        if (i == failed || attachedFailed) {
            bar = IM_COL32(255, 90, 90, 240);
        } else if (armed || attachedPending) {
            bar = IM_COL32(255, 235, 110, 240);
        }
        dl->AddRectFilled(ImVec2(c.x - 1.0f * S, c.y + cellH + 1.0f * S),
                          ImVec2(c.x + c.w + 1.0f * S, c.y + cellH + 2.5f * S), bar);
    }
    for (const EntityRow& row : entityRows) {
        const bool entityFailed = rowContains(row, failedEntityRequirement);
        const bool entityActive = rowContains(row, activeEntity) &&
            entityRowGateStarted(row);
        if (!entityFailed && !entityActive) continue;
        const bool hasProgress = entityActive && entityRowHasProgress(row);
        const ImU32 bar = entityFailed
            ? IM_COL32(255, 90, 90, 240)
            : hasProgress
                ? IM_COL32(255, 235, 110, 240)
                : IM_COL32(105, 235, 235, 230);
        dl->AddRectFilled(ImVec2(row.x - 1.0f * S,
                                 row.y + cellH + 1.0f * S),
                          ImVec2((std::min)(row.x + row.w + 1.0f * S, xMax),
                                 row.y + cellH + 2.5f * S), bar);
    }

    for (int i = 0; i < total; ++i) {
        const Cell& c = cells[i];
        const ImU32 tint = tintOf(i);
        const float iy = c.y + pad;
        float ix = c.x + c.pw;
        for (char ch : c.icons) {
            const float w = iconW(ch);
            auto it = g_icons.find(ch);
            if (ch == '5') {
                DrawNeutralTile(dl, font, (std::min)(fontSz, iconH - 2.0f), ix, iy, iconH, tint);
            } else if (it != g_icons.end() && g_atlasTex) {
                dl->AddImage(reinterpret_cast<ImTextureID>(g_atlasTex),
                             ImVec2(ix, iy), ImVec2(ix + w, iy + iconH),
                             ImVec2(it->second.u0, it->second.v0),
                             ImVec2(it->second.u1, it->second.v1), tint);
            }
            ix += w;
        }
    }
    for (const EntityRow& row : entityRows) {
        const ImU32 tint = entityTintOf(row);
        const float iy = row.y + pad;
        float ix = row.x + row.pw;
        for (char ch : row.icons) {
            const float w = iconW(ch);
            auto it = g_icons.find(ch);
            if (ch == '5') {
                DrawNeutralTile(dl, font,
                                (std::min)(fontSz, iconH - 2.0f),
                                ix, iy, iconH, tint);
            } else if (it != g_icons.end() && g_atlasTex) {
                dl->AddImage(reinterpret_cast<ImTextureID>(g_atlasTex),
                             ImVec2(ix, iy), ImVec2(ix + w, iy + iconH),
                             ImVec2(it->second.u0, it->second.v0),
                             ImVec2(it->second.u1, it->second.v1), tint);
            }
            ix += w;
        }
    }

    const float sepW = font->CalcTextSizeA(fontSz, FLT_MAX, 0.0f, ">").x;
    for (const FlowDivider& divider : flowDividers) {
        const float ty = divider.y + (cellH - fontSz) * 0.5f;
        dl->AddText(font, fontSz,
                    ImVec2(divider.x - sepW * 0.5f, ty), kSepCol, ">");
    }
    for (const SetupMarker& marker : setupMarkers) {
        const float ty = marker.y + (cellH - fontSz) * 0.5f;
        dl->AddText(font, fontSz,
                    ImVec2(marker.x + 4.0f * S, ty),
                    IM_COL32(105, 235, 235, 235), "SETUP");
    }
    for (int i = 0; i < total; ++i) {
        const Cell& c = cells[i];
        const ImU32 tint = tintOf(i);
        const float ty = c.y + (cellH - fontSz) * 0.5f;
        float ix = c.x;
        if (!c.prefix.empty()) {
            dl->AddText(font, fontSz, ImVec2(ix, ty), tint, c.prefix.c_str());
        }
        ix += c.pw;
        for (char ch : c.icons) {
            if (ch != '5' && g_icons.find(ch) == g_icons.end()) {
                char b[2] = { ch, 0 };
                dl->AddText(font, fontSz, ImVec2(ix + 1.0f * S, ty), tint, b);
            }
            ix += iconW(ch);
        }
        if (!c.text.empty()) {
            dl->AddText(font, fontSz, ImVec2(ix + 1.0f * S, ty), tint, c.text.c_str());
        }
    }
    for (const EntityRow& row : entityRows) {
        const ImU32 tint = entityTintOf(row);
        const float ty = row.y + (cellH - fontSz) * 0.5f;
        float ix = row.x;
        if (!row.prefix.empty()) {
            dl->AddText(font, fontSz, ImVec2(ix, ty), tint,
                        row.prefix.c_str());
        }
        ix += row.pw;
        for (char ch : row.icons) {
            if (ch != '5' && g_icons.find(ch) == g_icons.end()) {
                char b[2] = { ch, 0 };
                dl->AddText(font, fontSz,
                            ImVec2(ix + 1.0f * S, ty), tint, b);
            }
            ix += iconW(ch);
        }
        if (!row.text.empty()) {
            dl->AddText(font, fontSz,
                        ImVec2(ix + 1.0f * S, ty), tint,
                        row.text.c_str());
        }
    }
}

void InvalidateTextLayouts() {
    g_richDocuments.clear();
    ++g_layoutInvalidationGeneration;
}

void ReleaseTextures() {
    ResetIconAtlas();
    InvalidateTextLayouts();
}

}
