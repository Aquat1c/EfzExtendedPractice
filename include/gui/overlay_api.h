#pragma once

// Cross-mod ABI. d3d9Device is an IDirect3DDevice9*; callbacks run on the render thread in EndScene.
// Keep them fast and self-contained, and save and restore any device state they touch.

// Clients pass this; the host rejects mismatches, so a future signature can't be called wrongly.
#define EFZ_OVERLAY_ABI_VERSION 1u

extern "C"
{
    typedef void (*EfzOverlayRenderFn)(void* d3d9Device, unsigned rtWidth, unsigned rtHeight, void* user);

    // Returns false on an ABI mismatch or null fn; duplicate (fn,user) pairs are ignored.
    // Clients find the host through whichever loaded module exports this symbol.
    __declspec(dllexport) bool EFZ_RegisterOverlayRenderer(unsigned abiVersion, EfzOverlayRenderFn fn, void* user);

    __declspec(dllexport) bool EFZ_UnregisterOverlayRenderer(unsigned abiVersion, EfzOverlayRenderFn fn, void* user);
}

namespace OverlayApi
{
void DispatchRenderers(void* d3d9Device, unsigned rtWidth, unsigned rtHeight);
}
