#include "../include/gui/overlay_api.h"

#include <mutex>
#include <vector>

#include <windows.h>

namespace
{
struct RendererEntry
{
    EfzOverlayRenderFn fn = nullptr;
    void* user = nullptr;
};

std::mutex g_mutex;
std::vector<RendererEntry> g_renderers;

void CallRendererGuarded(EfzOverlayRenderFn fn, void* user, void* device, unsigned rtW, unsigned rtH)
{
    __try
    {
        fn(device, rtW, rtH, user);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        // Isolate a misbehaving external renderer so it can't take down the host frame.
    }
}
}

extern "C" __declspec(dllexport) bool EFZ_RegisterOverlayRenderer(unsigned abiVersion, EfzOverlayRenderFn fn, void* user)
{
    if (abiVersion != EFZ_OVERLAY_ABI_VERSION || fn == nullptr)
    {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const RendererEntry& entry : g_renderers)
    {
        if (entry.fn == fn && entry.user == user)
        {
            return true;
        }
    }
    g_renderers.push_back(RendererEntry{fn, user});
    return true;
}

extern "C" __declspec(dllexport) bool EFZ_UnregisterOverlayRenderer(unsigned abiVersion, EfzOverlayRenderFn fn, void* user)
{
    if (abiVersion != EFZ_OVERLAY_ABI_VERSION)
    {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto it = g_renderers.begin(); it != g_renderers.end(); ++it)
    {
        if (it->fn == fn && it->user == user)
        {
            g_renderers.erase(it);
            return true;
        }
    }
    return false;
}

namespace OverlayApi
{
void DispatchRenderers(void* d3d9Device, unsigned rtWidth, unsigned rtHeight)
{
    std::vector<RendererEntry> snapshot;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_renderers.empty())
        {
            return;
        }
        snapshot = g_renderers;
    }
    for (const RendererEntry& entry : snapshot)
    {
        if (entry.fn != nullptr)
        {
            CallRendererGuarded(entry.fn, entry.user, d3d9Device, rtWidth, rtHeight);
        }
    }
}
}
