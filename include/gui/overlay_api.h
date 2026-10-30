#pragma once

// Cross-mod overlay rendering ABI.
//
// The host (EfzExtendedPractice) owns the single Direct3D9 EndScene hook. Each frame,
// on the game's 640x480 render target, it invokes every registered renderer before its
// own ImGui pass, so the host's overlays and menu draw on top; the original EndScene
// runs last. Other mods register a callback here instead of hooking EndScene themselves,
// so only one mod owns the device hook (avoids two mods hooking each other's EndScene).
//
// The device is passed as void* (cast to IDirect3DDevice9*). Callbacks run on the
// render thread inside EndScene: keep them fast and self-contained, and save/restore
// any device state you touch (e.g. via a state block).

// ABI version. Clients pass this; the host rejects mismatches so an incompatible
// future signature can't be called with the wrong calling convention/layout.
#define EFZ_OVERLAY_ABI_VERSION 1u

extern "C"
{
    typedef void (*EfzOverlayRenderFn)(void* d3d9Device, unsigned rtWidth, unsigned rtHeight, void* user);

    // Registers a renderer. Returns false if abiVersion != EFZ_OVERLAY_ABI_VERSION or
    // fn is null. Duplicate (fn,user) pairs are ignored. A client discovers the host by
    // finding whichever loaded module exports this symbol.
    __declspec(dllexport) bool EFZ_RegisterOverlayRenderer(unsigned abiVersion, EfzOverlayRenderFn fn, void* user);

    // Unregisters a previously registered (fn,user). Returns true if it was present.
    __declspec(dllexport) bool EFZ_UnregisterOverlayRenderer(unsigned abiVersion, EfzOverlayRenderFn fn, void* user);
}

namespace OverlayApi
{
// Invoked from the host's EndScene hook to run all registered renderers.
void DispatchRenderers(void* d3d9Device, unsigned rtWidth, unsigned rtHeight);
}
