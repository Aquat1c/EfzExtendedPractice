#pragma once
#include <windows.h>
#include <d3d9.h>
#include "compat/overlay_reset_gate.h"
#include "../3rdparty/imgui/imgui.h"
#include "../3rdparty/imgui/backends/imgui_impl_dx9.h"
#include "../3rdparty/imgui/backends/imgui_impl_win32.h"

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ImGui implementation for EFZ
namespace ImGuiImpl {
    // Holds admission across the complete overlay pass. A null device selects
    // this module's current host (for input and the standalone fallback).
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

    // Initialize ImGui with DX9
    // Called while DeviceRenderScope holds game-render admission.
    bool Initialize(IDirect3DDevice9* device);
    bool IsBoundToDevice(IDirect3DDevice9* device);
    
    // Shutdown ImGui
    void Shutdown();
    
    // Consolidated render function
    void RenderFrame();
    
    // Feed pre-NewFrame inputs (XInput aggregation + virtual cursor) and emit debug logs.
    // Call this after backend NewFrame calls and before ImGui::NewFrame() in the active render loop.
    void PreNewFrameInputs();
    
    // Placeholder retained to avoid dangling references.
    inline void PostNewFrameDiagnostics() {}
    
    // Check if ImGui is initialized
    bool IsInitialized();
    
    // Toggle ImGui visibility
    void ToggleVisibility();
    // Unconditional close. Unlike ToggleVisibility() this is not netplay-gated,
    // so teardown paths cannot leave menuOpen/g_imguiVisible desynced. Use this
    // wherever the menu must go away rather than be toggled.
    void ForceHide();

    // Open the standalone fallback ImGui window when in-game rendering is unavailable.
    bool ShowFallbackWindow();

    // True while the separate fallback ImGui host window owns the current menu.
    bool IsExternalFallbackHost();

    // Stops a hidden fallback host so the game window can initialize ImGui again.
    // Waits at most timeoutMs; never call from the fallback thread.
    bool ReleaseExternalFallbackHost(DWORD timeoutMs);

    // Check if ImGui is visible
    bool IsVisible();
    // Bumped on every visible/hidden flip.
    unsigned VisibilityGeneration();

    // Handle WndProc messages
    LRESULT WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Netplay boundary for the game-window subclass. Detach restores the
    // previous proc only while ours is the live top-level proc; Attach chains
    // over whatever is on top again. Both are no-ops for the fallback host.
    bool DetachWndProc();
    bool AttachWndProc();

    // Provide the current overlay window center (in ImGui screen coordinates)
    // so recenter actions (middle-click/L3) can snap to the UI instead of the raw client center.
    void SetOverlayCenter(const ImVec2& center);

    // Request focusing the main overlay window next frame (consumed in RenderGui).
    bool ConsumeOverlayFocusRequest();
}
