#pragma once
#include <d3d9.h>

namespace GifPlayer {
    bool ShouldAttemptLoad();
    bool Initialize(LPDIRECT3DDEVICE9 dev);
    void Shutdown();
    // Textures belong to one device; the next request reloads on the current device.
    void ReleaseDeviceResources();
    void Update(double dtSeconds);
    IDirect3DTexture9* GetTexture(unsigned& w, unsigned& h);
    bool WasRequestedThisFrame();
    void EndFrame();
}
