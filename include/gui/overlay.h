#pragma once
#include <windows.h>
#include <ddraw.h>
#include <string>
#include <vector>
#include <deque>
#include <chrono>
#include <mutex>
#include <d3d9.h>
#include <atomic>

// Function pointer types for DirectDraw
typedef HRESULT(WINAPI* DirectDrawCreateFunc)(GUID*, LPVOID*, IUnknown*);
typedef HRESULT(WINAPI* DirectDrawEnumerateFunc)(LPDDENUMCALLBACKA, LPVOID);
// Typedefs for Blt and Flip
typedef HRESULT(WINAPI* BlitFunc)(IDirectDrawSurface7*, LPRECT, IDirectDrawSurface7*, LPRECT, DWORD, LPDDBLTFX);
typedef HRESULT(WINAPI* FlipFunc)(IDirectDrawSurface7*, IDirectDrawSurface7*, DWORD);

// V-table offsets for hooking
#define DDRAW_BLIT_OFFSET 4
#define DDRAW_FLIP_OFFSET 11

// Structure to represent a text message in the overlay
struct OverlayMessage {
    std::string text;
    COLORREF color;
    std::chrono::steady_clock::time_point expireTime;
    int xPos;
    int yPos;
    bool isPermanent;  // If true, stays until explicitly removed
    int id;            // Unique ID for permanent messages
    std::string category; // Category for grouping messages
    unsigned char backgroundAlpha; // Per-message background opacity
};

class DirectDrawHook {
private:
    // Original DirectDraw functions
    static DirectDrawCreateFunc originalDirectDrawCreate;
    static DirectDrawEnumerateFunc originalDirectDrawEnumerate;
    // Original Blt and Flip pointers
    static BlitFunc originalBlit;
    static FlipFunc originalFlip;
    
    // Hook state
    static IDirectDrawSurface7* primarySurface;
    static HWND gameWindow;
    static std::mutex messagesMutex;
    static std::deque<OverlayMessage> messages;
    static std::vector<OverlayMessage> permanentMessages;
    static int nextMessageId;
    
    // Hook functions
    static HRESULT WINAPI HookedDirectDrawCreate(GUID* lpGUID, LPVOID* lplpDD, IUnknown* pUnkOuter);
    static HRESULT WINAPI HookedBlit(IDirectDrawSurface7* This, LPRECT lpDestRect, 
                                    IDirectDrawSurface7* lpDDSrcSurface, LPRECT lpSrcRect, 
                                    DWORD dwFlags, LPDDBLTFX lpDDBltFx);
    static HRESULT WINAPI HookedFlip(IDirectDrawSurface7* This, 
                                    IDirectDrawSurface7* lpDDSurfaceTargetOverride, 
                                    DWORD dwFlags);
    
    // Rendering helper functions
    static void RenderText(HDC hdc, const std::string& text, int x, int y, COLORREF color);
    static void RenderSimpleText(IDirectDrawSurface7* surface, const std::string& text, int x, int y, COLORREF color);
    static void RenderAllMessages(IDirectDrawSurface7* surface);
    
    // Text fitting utility
    static std::string FitTextToWidth(const std::string& text, int maxWidth, HDC hdc = NULL);
    // Text fitting from the left side (for right-aligned text)
    static std::string FitTextToWidthFromLeft(const std::string& text, int maxWidth, HDC hdc = NULL);

public:
    // Initialize the hook
    static bool Initialize();

    // Make isHooked visible for status checks
    static bool isHooked;
    
    // Add a temporary message with a category
    static void AddMessage(const std::string& text, const std::string& category, COLORREF color, 
                          int durationMs, int x, int y);
    
    // Add or update a permanent message
    static int AddPermanentMessage(const std::string& text, COLORREF color = RGB(255, 255, 0), 
                                 int x = 10, int y = 10, unsigned char backgroundAlpha = 180);
    
    // Update an existing permanent message
    static void UpdatePermanentMessage(int id, const std::string& newText, COLORREF newColor = RGB(255, 255, 0));
    
    // Remove a permanent message
    static void RemovePermanentMessage(int id);
    
    // Remove messages by category
    static void RemoveMessagesByCategory(const std::string& category);

    // Remove all messages
    static void ClearAllMessages();

    // Make this function public so it can be called from the global EndScene hook
    static void RenderD3D9Overlays(LPDIRECT3DDEVICE9 pDevice, UINT rtWidth = 0, UINT rtHeight = 0);


    static void Shutdown();

    // Set up window procedure hooks for ImGui input handling
    static void SetupWindowProcedures();
    
    // D3D9 Hooking for ImGui
    static bool InitializeD3D9();
    static bool WasLastD3D9InitDeferredForNetplay();
    static bool ShouldUseExternalMenuFallback();
    static bool SetD3D9Active(bool active);
    // Re-installs the EndScene JMP when a peer restored the exact bytes we hooked over.
    static void RepairD3D9EntryIfRestored();
    static void ShutdownD3D9();
};

// Global status message IDs
extern int g_AirtechStatusId;
extern int g_JumpStatusId;
extern int g_FrameAdvantageId;
// Additional ID for right-hand FA segment when split coloring is used
extern int g_FrameAdvantage2Id;
extern int g_FrameGapId;
// Individual trigger message IDs
extern int g_TriggerAfterBlockId;
extern int g_TriggerOnWakeupId;
extern int g_TriggerAfterHitstunId;
extern int g_TriggerAfterAirtechId;
extern int g_TriggerOnRGId;
extern int g_FramestepStatusId;

// Debug overlay borders toggle (controlled from ImGui)
extern std::atomic<bool> g_ShowOverlayDebugBorders;
// Gate RG debug toasts via ImGui Debug tab
extern std::atomic<bool> g_ShowRGDebugToasts;
