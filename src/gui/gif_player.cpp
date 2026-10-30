#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <algorithm>
#if defined(EFZ_XP_COMPAT)
using std::max;
using std::min;
#endif
#include <windows.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#include <d3d9.h>
#include <vector>
#include <memory>
#include <cstring>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")

#include "../../include/gui/embedded_gif.h"
#include "../../include/core/logger.h"

namespace GifPlayer {

struct Frame {
    IDirect3DTexture9* tex = nullptr;
    UINT w = 0, h = 0;
    UINT delayMs = 100;

    Frame() = default;
    ~Frame() {
        if (tex) {
            tex->Release();
            tex = nullptr;
        }
    }
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;
    Frame(Frame&& other) noexcept
        : tex(other.tex), w(other.w), h(other.h), delayMs(other.delayMs) {
        other.tex = nullptr;
        other.w = 0;
        other.h = 0;
        other.delayMs = 100;
    }
    Frame& operator=(Frame&& other) noexcept {
        if (this != &other) {
            if (tex) {
                tex->Release();
            }
            tex = other.tex;
            w = other.w;
            h = other.h;
            delayMs = other.delayMs;
            other.tex = nullptr;
            other.w = 0;
            other.h = 0;
            other.delayMs = 100;
        }
        return *this;
    }
};

static ULONG_PTR s_gdiplusToken = 0;
static std::vector<Frame> s_frames;
static size_t s_index = 0;
static double s_accum = 0.0;
static bool s_inited = false;
static bool s_loadRequested = false;
static bool s_requestedThisFrame = false;
static DWORD s_lastFailedLoadTick = 0;

static bool EnsureGdiplus() {
    if (s_gdiplusToken) return true;
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    return Gdiplus::GdiplusStartup(&s_gdiplusToken, &gdiplusStartupInput, nullptr) == Gdiplus::Ok;
}

static bool BitmapFrameToTexture(LPDIRECT3DDEVICE9 dev, Gdiplus::Bitmap* bmp, Frame& out) {
    if (!bmp) return false;
    const UINT w = bmp->GetWidth();
    const UINT h = bmp->GetHeight();
    if (w == 0 || h == 0) return false;

    IDirect3DTexture9* tex = nullptr;
    if (FAILED(dev->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr)))
        return false;

    D3DLOCKED_RECT lr{};
    if (FAILED(tex->LockRect(0, &lr, nullptr, 0))) {
        tex->Release();
        return false;
    }

    Gdiplus::Rect rect(0, 0, static_cast<INT>(w), static_cast<INT>(h));
    Gdiplus::BitmapData data{};
    if (bmp->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) == Gdiplus::Ok) {
        const BYTE* scan0 = reinterpret_cast<const BYTE*>(data.Scan0);
        const size_t rowBytes = static_cast<size_t>(w) * sizeof(DWORD);
        for (UINT y = 0; y < h; ++y) {
            const BYTE* src = data.Stride >= 0
                ? scan0 + static_cast<size_t>(y) * static_cast<size_t>(data.Stride)
                : scan0 + static_cast<size_t>(h - 1 - y) * static_cast<size_t>(-data.Stride);
            BYTE* dst = reinterpret_cast<BYTE*>(lr.pBits) + static_cast<size_t>(y) * static_cast<size_t>(lr.Pitch);
            memcpy(dst, src, rowBytes);
        }
        bmp->UnlockBits(&data);
    } else {
        for (UINT y = 0; y < h; ++y) {
            DWORD* dst = reinterpret_cast<DWORD*>(reinterpret_cast<BYTE*>(lr.pBits) + y * lr.Pitch);
            for (UINT x = 0; x < w; ++x) {
                Gdiplus::Color c;
                bmp->GetPixel(x, y, &c);
                dst[x] = (c.GetA() << 24) | (c.GetR() << 16) | (c.GetG() << 8) | (c.GetB());
            }
        }
    }
    tex->UnlockRect(0);

    out.tex = tex;
    out.w = w; out.h = h;
    return true;
}

bool ShouldAttemptLoad() {
    if (s_inited || !s_loadRequested) return false;
    if (s_lastFailedLoadTick == 0) return true;
    return (GetTickCount() - s_lastFailedLoadTick) >= 5000;
}

bool Initialize(LPDIRECT3DDEVICE9 dev) {
    if (s_inited) return true;
    if (!dev || !ShouldAttemptLoad()) return false;

    const DWORD start = GetTickCount();
    auto fail = [&]() -> bool {
        s_lastFailedLoadTick = GetTickCount();
        return false;
    };

    if (!EnsureGdiplus()) return fail();

    IStream* stream = SHCreateMemStream(kEmbeddedGif, (UINT)kEmbeddedGifSize);
    if (!stream) return fail();
    std::unique_ptr<Gdiplus::Bitmap> bmp(new Gdiplus::Bitmap(stream, FALSE));
    stream->Release();
    if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok) return fail();

    GUID dim;
    UINT count = bmp->GetFrameDimensionsCount();
    if (count == 0) return fail();
    bmp->GetFrameDimensionsList(&dim, 1);
    UINT frames = bmp->GetFrameCount(&dim);
    if (frames == 0) frames = 1;

    std::vector<UINT> delays(frames, 100);
    Gdiplus::PropertyItem* prop = nullptr;
    UINT size = bmp->GetPropertyItemSize(PropertyTagFrameDelay);
    if (size) {
        prop = (Gdiplus::PropertyItem*)malloc(size);
        if (prop && bmp->GetPropertyItem(PropertyTagFrameDelay, size, prop) == Gdiplus::Ok) {
            for (UINT i = 0; i < frames; ++i) {
                delays[i] = 10U * ((UINT*)prop->value)[i]; // 1/100s to ms
                // Avoid std::max/min macro conflicts from windows.h
                if (delays[i] < 10U) delays[i] = 10U;
            }
        }
        if (prop) free(prop);
    }

    s_frames.clear();
    s_frames.reserve(frames);
    for (UINT i = 0; i < frames; ++i) {
        bmp->SelectActiveFrame(&dim, i);
        Frame f; f.delayMs = (i < delays.size() ? delays[i] : 100);
        // Clone current frame to a standalone bitmap to avoid mutation surprises
        std::unique_ptr<Gdiplus::Bitmap> clone(bmp->Clone(0, 0, bmp->GetWidth(), bmp->GetHeight(), PixelFormat32bppARGB));
        if (!clone || clone->GetLastStatus() != Gdiplus::Ok) continue;
        if (BitmapFrameToTexture(dev, clone.get(), f)) s_frames.push_back(std::move(f));
    }
    if (s_frames.empty()) return fail();
    s_index = 0; s_accum = 0.0; s_inited = true;
    s_loadRequested = false;
    s_lastFailedLoadTick = 0;
    LogOut("[GIF] Embedded GIF loaded: " + std::to_string(s_frames.size()) +
           " frames in " + std::to_string(GetTickCount() - start) + "ms", true);
    return true;
}

void ReleaseDeviceResources() {
    s_frames.clear();
    s_index = 0;
    s_accum = 0.0;
    s_inited = false;
    s_lastFailedLoadTick = 0;
}

void Shutdown() {
    s_frames.clear();
    if (s_gdiplusToken) {
        Gdiplus::GdiplusShutdown(s_gdiplusToken);
        s_gdiplusToken = 0;
    }
    s_inited = false;
    s_loadRequested = false;
    s_requestedThisFrame = false;
    s_lastFailedLoadTick = 0;
}

void Update(double dtSeconds) {
    if (!s_inited || s_frames.empty()) return;
    s_accum += dtSeconds * 1000.0;
    UINT delay = s_frames[s_index].delayMs;
    while (s_accum >= delay) {
        s_accum -= delay;
        s_index = (s_index + 1) % s_frames.size();
        delay = s_frames[s_index].delayMs;
    }
}

IDirect3DTexture9* GetTexture(UINT& w, UINT& h) {
    s_loadRequested = true;
    s_requestedThisFrame = true;
    if (!s_inited || s_frames.empty()) return nullptr;
    w = s_frames[s_index].w; h = s_frames[s_index].h;
    return s_frames[s_index].tex;
}

bool WasRequestedThisFrame() {
    return s_requestedThisFrame;
}

void EndFrame() {
    s_requestedThisFrame = false;
}

} // namespace GifPlayer
