#include "../include/gui/custom_menu/fonts.h"
#include "../include/gui/custom_menu/resource_ids.h"
#include "../include/gui/custom_menu/scale.h"
#include "../include/game/mission/mission_render.h"
#include "../include/core/logger.h"
#include "../include/utils/config.h"
#include "../include/utils/i18n.h"
#include "../3rdparty/imgui/imgui.h"
#include "../3rdparty/imgui/backends/imgui_impl_dx9.h"

#include <windows.h>
#include <chrono>
#include <cmath>
#include <cstring>

extern HMODULE g_hSelfModule;

namespace CustomMenu::Fonts {

namespace {

ImFont* g_bodyFont   = nullptr;
ImFont* g_headerFont = nullptr;
ImFont* g_tutorialReadableFont = nullptr;
ImFont* g_tutorialHeaderFont = nullptr;

float g_appliedScale = 0.0f;
int g_appliedFontMode = -1;
int g_appliedLanguage = -1;

std::chrono::steady_clock::time_point g_lastAttempt{};
bool g_lastBuildFailed = false;

struct ResourceBlob {
    const void* data = nullptr;
    DWORD size = 0;
};

ResourceBlob LoadEmbeddedFont(int resourceId = IDR_ITC_BOLT_BOLD_REGULAR) {
    ResourceBlob blob;
    if (!g_hSelfModule) return blob;

    HRSRC hRes = FindResourceA(
        g_hSelfModule,
        MAKEINTRESOURCEA(resourceId),
        RT_RCDATA);
    if (!hRes) return blob;

    HGLOBAL hGlobal = LoadResource(g_hSelfModule, hRes);
    if (!hGlobal) return blob;

    blob.data = LockResource(hGlobal);
    blob.size = SizeofResource(g_hSelfModule, hRes);
    return blob;
}

bool AtlasContainsFont(const ImFontAtlas* atlas, const ImFont* font) {
    if (!atlas || !font) return false;
    for (int i = 0; i < atlas->Fonts.Size; ++i) {
        if (atlas->Fonts[i] == font) return true;
    }
    return false;
}

} // namespace

void InvalidateAtlasReferences() {
    g_bodyFont = nullptr;
    g_headerFont = nullptr;
    g_tutorialReadableFont = nullptr;
    g_tutorialHeaderFont = nullptr;
    Mission::Render::InvalidateTextLayouts();
}

bool Rebuild(float uiScale) {
    if (!ImGui::GetCurrentContext()) return false;

    ImGuiIO& io = ImGui::GetIO();

    auto now = std::chrono::steady_clock::now();
    const bool everAttempted = (g_lastAttempt.time_since_epoch().count() != 0);

    Scale::Update(uiScale);
    const Scale::Metrics& metrics = Scale::Get();
    // nearest hundredth
    float rounded = std::floor(metrics.fontScale * 100.0f + 0.5f) / 100.0f;
    const int fontMode = Config::GetSettings().uiFontMode;
    const int language = I18n::GetLanguage();

    const bool scaleChanged = !(std::fabs(rounded - g_appliedScale) < 0.005f);
    const bool modeChanged = fontMode != g_appliedFontMode || language != g_appliedLanguage;
    const bool fontsMissing =
        !AtlasContainsFont(io.Fonts, g_bodyFont) ||
        !AtlasContainsFont(io.Fonts, g_headerFont) ||
        !AtlasContainsFont(io.Fonts, g_tutorialReadableFont) ||
        !AtlasContainsFont(io.Fonts, g_tutorialHeaderFont) ||
        io.FontDefault == nullptr;
    if (!scaleChanged && !modeChanged && !fontsMissing) return true;

    if (!fontsMissing && everAttempted &&
        (now - g_lastAttempt) < std::chrono::milliseconds(500)) {
        // Missing faces must be repaired immediately; an intact atlas may defer.
        return true;
    }
    // A failed build must not rebuild the atlas every frame.
    if (g_lastBuildFailed && (now - g_lastAttempt) < std::chrono::milliseconds(1000)) {
        return false;
    }
    g_lastAttempt = now;
    g_lastBuildFailed = true;

    ResourceBlob blob = LoadEmbeddedFont();
    if (!blob.data || blob.size == 0) {
        LogOut("[CUSTOM_MENU][FONT] Failed to locate embedded ITC Bolt font resource.", true);
        return false;
    }

    // FontDataOwnedByAtlas frees the blob on Clear(); allocate a fresh copy per size.
    auto copyBlob = [&]() -> void* {
        void* copy = IM_ALLOC(blob.size);
        if (copy) memcpy(copy, blob.data, blob.size);
        return copy;
    };

    // Resource memory lives as long as the module; the atlas must never free it.
    const ResourceBlob jaBlob = LoadEmbeddedFont(IDR_NOTO_SANS_JP_SUBSET);
    if (!jaBlob.data || jaBlob.size == 0) {
        LogOut("[CUSTOM_MENU][FONT] Failed to locate embedded Noto font resource.", true);
    }
    // ITC Bolt lacks " # ' < > @ \ ^ _ { | } ~ and most accents; merging never
    // overwrites existing glyphs, so Noto only fills those gaps.
    auto fillBoltGaps = [&](float px) {
        if (!jaBlob.data || jaBlob.size == 0) return;
        static const ImWchar kLatin1[] = { 0x0020, 0x00FF, 0 };
        ImFontConfig cfg;
        cfg.MergeMode = true;
        cfg.FontDataOwnedByAtlas = false;
        cfg.OversampleH = 1;
        cfg.OversampleV = 1;
        cfg.PixelSnapH = true;
        io.Fonts->AddFontFromMemoryTTF(const_cast<void*>(jaBlob.data), (int)jaBlob.size, px, &cfg,
                                       kLatin1);
    };
    static_assert(sizeof(ImWchar) == sizeof(unsigned short), "glyph ranges are 16-bit");
    auto mergeJapanese = [&](float px) {
        if (language != I18n::Japanese || !jaBlob.data || jaBlob.size == 0) return;
        ImFontConfig cfg;
        cfg.MergeMode = true;
        cfg.FontDataOwnedByAtlas = false;
        cfg.OversampleH = 1;
        cfg.OversampleV = 1;
        cfg.PixelSnapH = true;
        // Noto's em box is ~1.45x taller than ITC Bolt's, so CJK at the same px reads
        // smaller than Latin caps. Merged glyphs keep the destination baseline.
        constexpr float kJapaneseGlyphScale = 1.2f;
        io.Fonts->AddFontFromMemoryTTF(const_cast<void*>(jaBlob.data), (int)jaBlob.size,
                                       Scale::Snap(px * kJapaneseGlyphScale), &cfg,
                                       reinterpret_cast<const ImWchar*>(I18n::JapaneseGlyphRanges()));
    };

    ImGui_ImplDX9_InvalidateDeviceObjects();
    InvalidateAtlasReferences();
    io.Fonts->Clear();
    io.Fonts->Flags |= ImFontAtlasFlags_NoPowerOfTwoHeight;
    io.Fonts->TexGlyphPadding = 1;

    // Rebuild the default font too so the custom typeface doesn't leak into the shared context.
    ImFont* mainFont = nullptr;
    {
        ImFontConfig cfgMain;
        cfgMain.OversampleH = 3;
        cfgMain.OversampleV = 3;
        cfgMain.PixelSnapH  = false;

        const float mainPx = Scale::Snap(13.0f * rounded);
        if (fontMode == 1) {
            const char* segoePath = "C:\\Windows\\Fonts\\segoeui.ttf";
            DWORD fa = GetFileAttributesA(segoePath);
            if (fa != INVALID_FILE_ATTRIBUTES && !(fa & FILE_ATTRIBUTE_DIRECTORY)) {
                mainFont = io.Fonts->AddFontFromFileTTF(segoePath, mainPx, &cfgMain);
            }
        }
        if (!mainFont) {
            cfgMain.SizePixels = mainPx;
            mainFont = io.Fonts->AddFontDefault(&cfgMain);
        }
        if (mainFont) mergeJapanese(mainPx);
    }

    ImFontConfig cfgBase;
    cfgBase.OversampleH = 1;   // pixel-snapped bitmap feel
    cfgBase.OversampleV = 1;
    cfgBase.PixelSnapH  = true;
    cfgBase.FontDataOwnedByAtlas = true;

    // Load body first so it's the default font.
    const float bodyPx   = metrics.bodyPx;
    const float headerPx = metrics.headerPx;

    // Tutorial draw lists are commonly enlarged 2-4x; denser faces preserve detail through that transform.
    {
        ImFontConfig cfgTutorial;
        cfgTutorial.OversampleH = 2;
        cfgTutorial.OversampleV = 2;
        cfgTutorial.PixelSnapH = false;
        const float tutorialPx = Scale::Snap(26.0f * rounded);
        cfgTutorial.SizePixels = tutorialPx;
        if (fontMode == 1) {
            const char* segoePath = "C:\\Windows\\Fonts\\segoeui.ttf";
            const DWORD fa = GetFileAttributesA(segoePath);
            if (fa != INVALID_FILE_ATTRIBUTES && !(fa & FILE_ATTRIBUTE_DIRECTORY)) {
                g_tutorialReadableFont =
                    io.Fonts->AddFontFromFileTTF(segoePath, tutorialPx, &cfgTutorial);
            }
        }
        if (!g_tutorialReadableFont) {
            g_tutorialReadableFont = io.Fonts->AddFontDefault(&cfgTutorial);
        }
    }

    ImFontConfig cfgBody = cfgBase;
    cfgBody.SizePixels = bodyPx;
    void* bodyBytes = copyBlob();
    g_bodyFont = bodyBytes
        ? io.Fonts->AddFontFromMemoryTTF(bodyBytes, (int)blob.size, bodyPx, &cfgBody)
        : nullptr;
    if (g_bodyFont) { fillBoltGaps(bodyPx); mergeJapanese(bodyPx); }

    ImFontConfig cfgHeader = cfgBase;
    cfgHeader.SizePixels = headerPx;
    void* headerBytes = copyBlob();
    g_headerFont = headerBytes
        ? io.Fonts->AddFontFromMemoryTTF(headerBytes, (int)blob.size, headerPx, &cfgHeader)
        : nullptr;
    if (g_headerFont) { fillBoltGaps(headerPx); mergeJapanese(headerPx); }

    ImFontConfig cfgTutorialHeader = cfgBase;
    const float tutorialHeaderPx = Scale::Snap(32.0f * rounded);
    cfgTutorialHeader.SizePixels = tutorialHeaderPx;
    void* tutorialHeaderBytes = copyBlob();
    g_tutorialHeaderFont = tutorialHeaderBytes
        ? io.Fonts->AddFontFromMemoryTTF(tutorialHeaderBytes, (int)blob.size,
                                        tutorialHeaderPx, &cfgTutorialHeader)
        : nullptr;
    if (g_tutorialHeaderFont) fillBoltGaps(tutorialHeaderPx);

    io.FontDefault = mainFont ? mainFont : g_bodyFont;

    if (!ImGui_ImplDX9_CreateDeviceObjects()) {
        LogOut("[CUSTOM_MENU][FONT] Failed to recreate DX9 device objects after font rebuild.", true);
        g_bodyFont = nullptr;
        g_headerFont = nullptr;
        g_tutorialReadableFont = nullptr;
        g_tutorialHeaderFont = nullptr;
        return false;
    }

    g_appliedScale = rounded;
    g_appliedFontMode = fontMode;
    g_appliedLanguage = language;
    char msg[192];
    _snprintf_s(msg, sizeof(msg), _TRUNCATE,
        "[CUSTOM_MENU][FONT] Loaded menu fonts: body=%.0fpx header=%.0fpx "
        "tutorial-readable=%.0fpx tutorial-header=%.0fpx scale=%.2f",
        bodyPx, headerPx, Scale::Snap(26.0f * rounded),
        Scale::Snap(32.0f * rounded), rounded);
    LogOut(msg, true);

    g_lastBuildFailed = !(g_bodyFont != nullptr && g_headerFont != nullptr);
    return !g_lastBuildFailed;
}

ImFont* Body()     { return g_bodyFont; }
ImFont* Header()   { return g_headerFont; }
ImFont* TutorialReadable() { return g_tutorialReadableFont; }
ImFont* TutorialHeader() { return g_tutorialHeaderFont; }
bool    IsLoaded() {
    if (!ImGui::GetCurrentContext()) return false;
    ImGuiIO& io = ImGui::GetIO();
    return AtlasContainsFont(io.Fonts, g_bodyFont) &&
           AtlasContainsFont(io.Fonts, g_headerFont) &&
           AtlasContainsFont(io.Fonts, g_tutorialReadableFont) &&
           AtlasContainsFont(io.Fonts, g_tutorialHeaderFont);
}

} // namespace CustomMenu::Fonts
