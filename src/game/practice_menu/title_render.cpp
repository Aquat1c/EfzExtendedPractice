#include "../../../include/game/practice_menu/title_render.h"
#include "../../../include/game/practice_menu/efz_title.h"

#include "../../../include/core/logger.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <string>

// title_ob2.dat loads into a PRIVATE surface (never the title's +1080 surface) so
// we never fight InGameNetplay over title_ob.dat.

using namespace PracticeMenu::EfzTitle;

namespace {

// title_ob2.dat is 120x95 with two lanes of 3 rows at a 14px pitch. Only the first 8px of a row
// are sprite; the cell padding holds non-keyed separator pixels, so never blit the full pitch.
constexpr int kSheetW     = 120;
constexpr int kRowH       = 8;
constexpr int kRowPitch   = 14;  // stride between rows within a lane
constexpr int kUnselBaseY = 4;   // top of the first unselected row (PRACTICE)
constexpr int kSelBaseY   = 49;  // top of the first selected row (PRACTICE + teal bar)

// Vanilla title menu draws at dest (195,130) with a 14px row pitch; the blit takes dest first.
constexpr int kDestX    = 195;
constexpr int kDestY    = 130;
constexpr int kDestStep = 14;

constexpr int kPaletteBase        = 193;
constexpr int kPaletteCountFallback = 56;

uint32_t g_surface = 0;
bool     g_loaded  = false;
bool     g_loadFailed = false;
char     g_transparent = 0;
std::string g_datPath;

void Log(const char* m) { LogOut(std::string("[PRACTICE_MENU][render] ") + m, true); }

// EFZ .dat byte 0 = number of palette colors. Load exactly that many, clamped to the 256 range.
int ReadDatPaletteCount(const char* path) {
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || f == nullptr) return 0;
    const int b0 = fgetc(f);
    fclose(f);
    if (b0 <= 0 || b0 > 255) return 0;
    int count = b0;
    if (count > 255 - kPaletteBase) count = 255 - kPaletteBase;
    return count;
}

bool ResolveDatPath(std::string& out) {
    HMODULE mod = GetModuleHandleA("efz_training_mode.dll");
    char path[MAX_PATH] = {0};
    if (!mod || GetModuleFileNameA(mod, path, MAX_PATH) == 0) return false;
    std::string dir(path);
    const size_t slash = dir.find_last_of("\\/");
    if (slash == std::string::npos) return false;
    dir.resize(slash + 1);

    const std::string candidates[] = { dir + "assets\\title_ob2.dat", dir + "title_ob2.dat" };
    for (const std::string& c : candidates) {
        if (GetFileAttributesA(c.c_str()) != INVALID_FILE_ATTRIBUTES) { out = c; return true; }
    }
    return false;
}

void RestoreTitlePalette(uint32_t sc) {
    const uintptr_t loadPalVa = Resolve(kVaLoadPalette);
    const uintptr_t setPalVa  = Resolve(kVaSetPalette);
    if (!loadPalVa || !setPalVa || !sc) return;
    __try {
        auto loadPal = reinterpret_cast<LoadPaletteFn>(loadPalVa);
        auto setPal  = reinterpret_cast<SetPaletteFn>(setPalVa);
        loadPal(static_cast<int>(sc + kOffPalette), "system\\title.dat", 0, 1, 192);
        loadPal(static_cast<int>(sc + kOffPalette), "system\\title_ob.dat", 0, 193, 48);
        setPal(reinterpret_cast<void*>(GraphicsSystem(sc)), static_cast<int>(sc + kOffPalette));
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

}

namespace PracticeMenu::Render {

bool EnsureLoaded(uint32_t sc) {
    if (g_loaded) return true;
    if (g_loadFailed || !sc) return false;

    if (g_datPath.empty() && !ResolveDatPath(g_datPath)) {
        Log("title_ob2.dat not found next to the DLL; rendering disabled");
        g_loadFailed = true;
        return false;
    }

    const uintptr_t loadImgVa = Resolve(kVaLoadImage);
    const uintptr_t readPixVa = Resolve(kVaReadPixel);
    if (!loadImgVa || !readPixVa) { g_loadFailed = true; return false; }

    __try {
        auto loadImg = reinterpret_cast<LoadImageFn>(loadImgVa);
        auto readPix = reinterpret_cast<ReadPixelFn>(readPixVa);

        g_surface = 0;
        loadImg(reinterpret_cast<void*>(GameContext(sc)), &g_surface, g_datPath.c_str(),
                0, static_cast<unsigned char>(kPaletteBase));
        if (g_surface == 0) { Log("loadCompressedImageFile returned null surface"); g_loadFailed = true; return false; }

        g_transparent = readPix(static_cast<int>(g_surface));

        g_loaded = true;
        Log("Loaded title_ob2.dat into private surface");
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("Exception while loading title_ob2.dat");
        g_loadFailed = true;
        return false;
    }
}

bool IsLoaded() { return g_loaded; }

void ApplyPalette(uint32_t sc) {
    if (!EnsureLoaded(sc)) return;
    const uintptr_t loadPalVa = Resolve(kVaLoadPalette);
    const uintptr_t setPalVa  = Resolve(kVaSetPalette);
    if (!loadPalVa || !setPalVa || !sc) return;
    __try {
        int paletteCount = ReadDatPaletteCount(g_datPath.c_str());
        if (paletteCount <= 0) paletteCount = kPaletteCountFallback;
        reinterpret_cast<LoadPaletteFn>(loadPalVa)(
            static_cast<int>(sc + kOffPalette), g_datPath.c_str(), 0, kPaletteBase, paletteCount);
        reinterpret_cast<SetPaletteFn>(setPalVa)(
            reinterpret_cast<void*>(GraphicsSystem(sc)), static_cast<int>(sc + kOffPalette));
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

void DrawBackground(uint32_t sc) {
    const uintptr_t blitVa = Resolve(kVaBlit);
    if (!blitVa || !sc) return;
    __try {
        auto blit = reinterpret_cast<BlitFn>(blitVa);
        void* gfxCtx = reinterpret_cast<void*>(GraphicsSystem(sc));
        const int bgSurface = *reinterpret_cast<int*>(sc + kOffBgSurface);
        if (bgSurface == 0) return;
        blit(gfxCtx, 0, 0, 320, 240, bgSurface, 0, 0, 320, 240, 0, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

void Release(uint32_t sc) {
    if (g_loaded) RestoreTitlePalette(sc);
}

void DrawRows(uint32_t sc, int selectedRow, int slideOffsetX) {
    if (!EnsureLoaded(sc)) return;

    const uintptr_t blitVa = Resolve(kVaBlit);
    if (!blitVa) return;

    __try {
        auto blit = reinterpret_cast<BlitFn>(blitVa);
        void* gfxCtx = reinterpret_cast<void*>(GraphicsSystem(sc));
        const int destX = kDestX + slideOffsetX;

        for (int row = 0; row < ROW_COUNT; ++row) {
            const int srcY = (row == selectedRow ? kSelBaseY : kUnselBaseY) + row * kRowPitch;
            const int destY = kDestY + row * kDestStep;
            blit(gfxCtx,
                 destX, destY, destX + kSheetW, destY + kRowH,
                 static_cast<int>(g_surface), 0, srcY, kSheetW, srcY + kRowH,
                 g_transparent, 0);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

}
