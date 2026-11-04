#pragma once
#include <windows.h>
#include <d3d9.h>
#include "compat/overlay_reset_gate.h"
#include "../3rdparty/imgui/imgui.h"
#include "../3rdparty/imgui/backends/imgui_impl_dx9.h"
#include "../3rdparty/imgui/backends/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace ImGuiImpl {
    class DeviceRenderScope {
    public:
        explicit DeviceRenderScope(IDirect3DDevice9* device = nullptr);
        ~DeviceRenderScope();
        DeviceRenderScope(const DeviceRenderScope&) = delete;
        DeviceRenderScope& operator=(const DeviceRenderScope&) = delete;
        bool Admitted() const { return admitted_; }
        void Release();
    private:
        EfzOverlayResetGate::Access access_;
        ImGuiContext* previousContext_ = nullptr;
        bool admitted_ = false;
    };

    // Call while a DeviceRenderScope holds game-render admission.
    bool Initialize(IDirect3DDevice9* device);
    bool IsBoundToDevice(IDirect3DDevice9* device);
    
    void Shutdown();
    
    void RenderFrame();
    
    void NewFrameInputs();

    // Call after backend NewFrame calls and before ImGui::NewFrame().
    void PreNewFrameInputs();
    
    inline void PostNewFrameDiagnostics() {}
    
    bool IsInitialized();
    
    void ToggleVisibility();
    // Unconditional close: unlike ToggleVisibility() this is not netplay-gated, so teardown paths
    // cannot leave menuOpen and g_imguiVisible out of sync. Use it wherever the menu must go away.
    void ForceHide();

    bool ShowFallbackWindow();

    bool IsExternalFallbackHost();

    // Stops a hidden fallback host so the game window can initialize ImGui again.
    // Waits at most timeoutMs; never call from the fallback thread.
    bool ReleaseExternalFallbackHost(DWORD timeoutMs);

    bool IsVisible();
    unsigned VisibilityGeneration();

    LRESULT WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Netplay boundary for the game-window subclass; both are no-ops for the fallback host.
    // Detach restores the previous proc only while ours is on top; Attach chains over the current top.
    bool DetachWndProc();
    bool AttachWndProc();

    void SetOverlayCenter(const ImVec2& center);

    bool ConsumeOverlayFocusRequest();
}
