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

typedef HRESULT(WINAPI* DirectDrawCreateFunc)(GUID*, LPVOID*, IUnknown*);
typedef HRESULT(WINAPI* DirectDrawEnumerateFunc)(LPDDENUMCALLBACKA, LPVOID);
typedef HRESULT(WINAPI* BlitFunc)(IDirectDrawSurface7*, LPRECT, IDirectDrawSurface7*, LPRECT, DWORD, LPDDBLTFX);
typedef HRESULT(WINAPI* FlipFunc)(IDirectDrawSurface7*, IDirectDrawSurface7*, DWORD);

#define DDRAW_BLIT_OFFSET 4
#define DDRAW_FLIP_OFFSET 11

struct OverlayMessage {
    std::string text;
    COLORREF color;
    std::chrono::steady_clock::time_point expireTime;
    int xPos;
    int yPos;
    bool isPermanent;
    int id;
    std::string category;
    unsigned char backgroundAlpha;
};

class DirectDrawHook {
private:
    static DirectDrawCreateFunc originalDirectDrawCreate;
    static DirectDrawEnumerateFunc originalDirectDrawEnumerate;
    static BlitFunc originalBlit;
    static FlipFunc originalFlip;
    
    static IDirectDrawSurface7* primarySurface;
    static HWND gameWindow;
    static std::mutex messagesMutex;
    static std::deque<OverlayMessage> messages;
    static std::vector<OverlayMessage> permanentMessages;
    static int nextMessageId;
    
    static HRESULT WINAPI HookedDirectDrawCreate(GUID* lpGUID, LPVOID* lplpDD, IUnknown* pUnkOuter);
    static HRESULT WINAPI HookedBlit(IDirectDrawSurface7* This, LPRECT lpDestRect, 
                                    IDirectDrawSurface7* lpDDSrcSurface, LPRECT lpSrcRect, 
                                    DWORD dwFlags, LPDDBLTFX lpDDBltFx);
    static HRESULT WINAPI HookedFlip(IDirectDrawSurface7* This, 
                                    IDirectDrawSurface7* lpDDSurfaceTargetOverride, 
                                    DWORD dwFlags);
    
    static void RenderText(HDC hdc, const std::string& text, int x, int y, COLORREF color);
    static void RenderSimpleText(IDirectDrawSurface7* surface, const std::string& text, int x, int y, COLORREF color);
    static void RenderAllMessages(IDirectDrawSurface7* surface);
    
    static std::string FitTextToWidth(const std::string& text, int maxWidth, HDC hdc = NULL);
    static std::string FitTextToWidthFromLeft(const std::string& text, int maxWidth, HDC hdc = NULL);

public:
    static bool Initialize();

    static bool isHooked;
    
    static void AddMessage(const std::string& text, const std::string& category, COLORREF color, 
                          int durationMs, int x, int y);
    
    static int AddPermanentMessage(const std::string& text, COLORREF color = RGB(255, 255, 0), 
                                 int x = 10, int y = 10, unsigned char backgroundAlpha = 180);
    
    static void UpdatePermanentMessage(int id, const std::string& newText, COLORREF newColor = RGB(255, 255, 0));
    
    static void RemovePermanentMessage(int id);
    
    static void RemoveMessagesByCategory(const std::string& category);

    static void ClearAllMessages();

    static void RenderD3D9Overlays(LPDIRECT3DDEVICE9 pDevice, UINT rtWidth = 0, UINT rtHeight = 0);


    static void Shutdown();

    static void SetupWindowProcedures();
    
    static bool InitializeD3D9();
    static bool WasLastD3D9InitDeferredForNetplay();
    static bool ShouldUseExternalMenuFallback();
    static bool SetD3D9Active(bool active);
    static void RepairD3D9EntryIfRestored();
    static void ShutdownD3D9();
};

extern int g_AirtechStatusId;
extern int g_JumpStatusId;
extern int g_FrameAdvantageId;
extern int g_FrameAdvantage2Id;
extern int g_FrameGapId;
extern int g_TriggerAfterBlockId;
extern int g_TriggerOnWakeupId;
extern int g_TriggerAfterHitstunId;
extern int g_TriggerAfterAirtechId;
extern int g_TriggerOnRGId;
extern int g_FramestepStatusId;

extern std::atomic<bool> g_ShowOverlayDebugBorders;
extern std::atomic<bool> g_ShowRGDebugToasts;
