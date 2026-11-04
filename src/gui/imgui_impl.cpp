#include "gui/custom_menu/palette_editor.h"
#include "gui/custom_menu/palette_editor_integration.h"
#include "gui/lifecycle_state.h"
#include "gui/pointer_mapping.h"
#include "gui/framebar.h"
#include "../include/gui/imgui_impl.h"
#include "compat/overlay_reset_api.h"
#include "compat/overlay_context_scope.h"
#include "../include/utils/xinput_shim.h"
#include "../include/core/logger.h"
#include "../include/gui/imgui_gui.h"
#include "../include/gui/custom_menu/fonts.h"
#include "../include/gui/custom_menu/input.h"
#include "../include/gui/custom_menu/renderer.h"
#include "../include/game/practice_hotkey_gate.h"
#include "../include/game/mission/mission_pause_menu.h"
#include "../include/game/mission/tutorial_session.h"
namespace PracticeOverlayGate { void SetMenuVisible(bool); }
#include "../include/gui/overlay.h" 
#include "../include/utils/utilities.h"
#include "../include/core/memory.h"
#include "../include/utils/switch_players.h"
#include "../include/utils/xp_compat.h"
#include <stdexcept>
#include <optional>
#include <Xinput.h>
#include <algorithm>
#include "../include/utils/pause_integration.h"
#include "../include/utils/config.h"
#include <cmath>
#include <climits>
#include <cwchar>
#include <chrono>
#include <thread>
#include "../include/input/input_handler.h"

// XInput linked dynamically via XInputShim for Wine compatibility

extern std::atomic<bool> g_isShuttingDown;

static GuiLifecycle::ImGuiInitialization g_imguiInitialization;

static ImGuiContext* g_imguiContext = nullptr;
static DWORD g_imguiContextThread = 0;
static std::atomic<DWORD> g_imguiWorkThread{0};
static EfzOverlayResetGate g_resetGate;
bool g_imguiVisible = false;
static std::atomic<unsigned> g_visibilityGeneration{0};
namespace CharacterSettings {
    std::atomic<bool> g_guiVisible{false};
}
static IDirect3DDevice9* g_d3dDevice = nullptr;
// g_originalWndProc stays valid across a detach so in-flight calls still forward.
// Attach/detach run on the lifecycle watcher while the window thread reads both.
static std::atomic<WNDPROC> g_originalWndProc{nullptr};
static std::atomic<bool> g_wndProcAttached{false};
static std::atomic<bool> g_wndProcProbeSeen{false};
// Window messages arrive on the creating thread only; no TLS in the XP injected-DLL build.
struct WndProcVisit {
    HWND hwnd = nullptr;
    UINT msg = 0;
    WPARAM wParam = 0;
    LPARAM lParam = 0;
};
static WndProcVisit g_wndProcVisit{};
static int g_wndProcDepth = 0;
static bool g_wndProcRingReported = false;
class WndProcVisitScope {
public:
    WndProcVisitScope(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) : saved_(g_wndProcVisit) {
        g_wndProcVisit = WndProcVisit{hWnd, msg, wParam, lParam};
        ++g_wndProcDepth;
    }
    ~WndProcVisitScope() {
        --g_wndProcDepth;
        g_wndProcVisit = saved_;
    }
private:
    WndProcVisit saved_;
};
static HWND g_imguiHostWindow = nullptr;
static bool g_externalFallbackHost = false;
static GuiLifecycle::FallbackWorker g_externalFallback;
static HWND g_externalFallbackWindow = nullptr;
static const char* kFallbackWindowClassName = "EFZ_TM_IMGUI_FALLBACK";

namespace {
HRESULT ResetGateResult(EfzOverlayResetGate::Result result) {
    switch (result) {
    case EfzOverlayResetGate::Result::Ok: return S_OK;
    case EfzOverlayResetGate::Result::InvalidArgument: return E_INVALIDARG;
    case EfzOverlayResetGate::Result::WrongThread: return RPC_E_WRONG_THREAD;
    default: return HRESULT_FROM_WIN32(ERROR_BUSY);
    }
}

HRESULT __cdecl BeforeOverlayReset(void* device, uint32_t epoch) {
    EfzOverlayResetGate::Access access(g_resetGate);
    if (!access.Acquired()) return HRESULT_FROM_WIN32(ERROR_BUSY);
    bool invalidate = false;
    const auto result = g_resetGate.BeforeReset(device, epoch, GetCurrentThreadId(), invalidate);
    if (result != EfzOverlayResetGate::Result::Ok) return ResetGateResult(result);
    if (invalidate) {
        if (!g_imguiContext || !g_imguiInitialization.IsReady()) return E_UNEXPECTED;
        EfzScopedImGuiContext context(g_imguiContext);
        // The context and WndProc chain must survive the reset.
        ImGuiImpl::InvalidateDeviceObjects();
        g_resetGate.DidInvalidateBackend();
    }
    return S_OK;
}

HRESULT __cdecl AfterOverlayReset(void* device, uint32_t epoch, uint32_t ready) {
    EfzOverlayResetGate::Access access(g_resetGate);
    if (!access.Acquired()) return HRESULT_FROM_WIN32(ERROR_BUSY);
    if (ready > 1) return E_INVALIDARG;
    const auto result = g_resetGate.AfterReset(device, epoch, GetCurrentThreadId(), 0);
    if (result != EfzOverlayResetGate::Result::Ok) return ResetGateResult(result);
    if (!ready) return S_OK;
    if (g_resetGate.BoundDevice() == device) {
        if (!g_resetGate.BackendInvalidatedFor(device)) return HRESULT_FROM_WIN32(ERROR_BUSY);
        if (!g_imguiContext || !g_imguiInitialization.IsReady()) return E_UNEXPECTED;
        const HRESULT cooperative = static_cast<IDirect3DDevice9*>(device)->TestCooperativeLevel();
        if (cooperative != D3D_OK) return cooperative;
        EfzScopedImGuiContext context(g_imguiContext);
        if (!ImGui_ImplDX9_CreateDeviceObjects()) {
            ImGui_ImplDX9_InvalidateDeviceObjects();
            return E_FAIL;
        }
    }
    return ResetGateResult(g_resetGate.AfterReset(device, epoch, GetCurrentThreadId(), 1));
}
}

extern "C" __declspec(dllexport) const EfzOverlayResetApiV1* __cdecl
EFZ_QueryOverlayResetApiV1(void) {
    static const EfzOverlayResetApiV1 api = {
        sizeof(EfzOverlayResetApiV1), 1, &BeforeOverlayReset, &AfterOverlayReset
    };
    return &api;
}

static bool g_useVirtualCursor = false;
static ImVec2 g_virtualCursorPos = ImVec2(200.0f, 200.0f);
static ImVec2 g_overlayCenter = ImVec2(0.0f, 0.0f);
static bool g_requestOverlayFocus = false;
static bool g_lastLeftDown = false;
static bool g_lastRightDown = false;
static bool g_resetVirtualCursorOnOpen = false;

static float g_lastFontScaleApplied = 0.0f;
static int   g_lastFontModeApplied  = -1;

static HWND GetActiveHostWindow() {
    if (g_imguiHostWindow && IsWindow(g_imguiHostWindow)) {
        return g_imguiHostWindow;
    }
    return FindEFZWindow();
}

struct PointerPresentation {
    HMODULE display = nullptr;
    HMODULE wrapper = nullptr;
    GuiPointer::Settings settings;
    bool enabled = true;
    bool active = false;
    DWORD displaySize = 0;
    DWORD callRva = 0;
    DWORD nextProbe = 0;
    unsigned attempts = 0;
    unsigned visibility = 0;
    int backbufferWidth = 0;
    int backbufferHeight = 0;
};
static PointerPresentation g_pointerPresentation;

static bool ReadModuleIdentity(HMODULE module, DWORD& timestamp, DWORD& size) {
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS32 nt{};
    SIZE_T read = 0;
    if (!module || !ReadProcessMemory(GetCurrentProcess(), module, &dos, sizeof(dos), &read)
        || read != sizeof(dos) || dos.e_magic != IMAGE_DOS_SIGNATURE
        || dos.e_lfanew <= 0 || dos.e_lfanew > 0x1000) return false;
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const BYTE*>(module) + dos.e_lfanew,
                           &nt, sizeof(nt), &read) || read != sizeof(nt)
        || nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_I386
        || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return false;
    timestamp = nt.FileHeader.TimeDateStamp;
    size = nt.OptionalHeader.SizeOfImage;
    return size > DWORD(dos.e_lfanew) + sizeof(nt);
}

static int ReadDisplayInteger(const wchar_t* path, const wchar_t* key, int fallback,
                              int minimum, int maximum) {
    wchar_t text[64]{};
    if (GetPrivateProfileStringW(L"Display", key, L"", text, 64, path) == 63) return fallback;
    const wchar_t* p = text;
    if (*p == L'+') ++p;
    unsigned base = 10;
    if (p[0] == L'0' && (p[1] == L'x' || p[1] == L'X')) { base = 16; p += 2; }
    if (!*p) return fallback;
    uint64_t value = 0;
    for (; *p; ++p) {
        unsigned digit = 16;
        if (*p >= L'0' && *p <= L'9') digit = unsigned(*p - L'0');
        else if (*p >= L'a' && *p <= L'f') digit = unsigned(*p - L'a') + 10;
        else if (*p >= L'A' && *p <= L'F') digit = unsigned(*p - L'A') + 10;
        if (digit >= base || value > (uint64_t(maximum) - digit) / base) return fallback;
        value = value * base + digit;
        if (value > unsigned(maximum)) return fallback;
    }
    return value >= unsigned(minimum) ? int(value) : fallback;
}

static void LoadPointerPresentation(HMODULE display, HMODULE wrapper) {
    g_pointerPresentation = {};
    auto& state = g_pointerPresentation;
    state.display = display;
    state.wrapper = wrapper;
    DWORD wrapperTimestamp = 0, wrapperSize = 0, unused = 0;
    if (!ReadModuleIdentity(display, unused, state.displaySize)
        || !ReadModuleIdentity(wrapper, wrapperTimestamp, wrapperSize)) return;
    state.callRva = GuiPointer::FinalBlitRva(wrapperTimestamp);
    if (state.callRva == 0 || state.callRva > wrapperSize || wrapperSize - state.callRva < 6) {
        state.callRva = 0;
        return;
    }
    state.settings.mode = GuiPointer::ScaleMode::Fit;
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(display, path, MAX_PATH);
    if (!length || length >= MAX_PATH) return;
    wchar_t* slash = wcsrchr(path, L'\\');
    const wchar_t name[] = L"DisplayManager.ini";
    if (!slash || size_t(slash - path) + 1 + _countof(name) > _countof(path)) return;
    wcscpy_s(slash + 1, _countof(path) - size_t(slash + 1 - path), name);
    state.enabled = ReadDisplayInteger(path, L"Enabled", 1, 0, 1) != 0;
    state.settings.integerScale = ReadDisplayInteger(path, L"IntegerScale", 0, 0, INT_MAX);
    state.settings.customWidth = ReadDisplayInteger(path, L"CustomWidth", 640, 1, INT_MAX);
    state.settings.customHeight = ReadDisplayInteger(path, L"CustomHeight", 480, 1, INT_MAX);
    wchar_t mode[32]{};
    const DWORD count = GetPrivateProfileStringW(L"Display", L"Mode", L"FitToScreen", mode, 32, path);
    if (count != 31 && lstrcmpiW(mode, L"IntegerScaling") == 0)
        state.settings.mode = GuiPointer::ScaleMode::Integer;
    else if (count != 31 && lstrcmpiW(mode, L"CustomResolution") == 0)
        state.settings.mode = GuiPointer::ScaleMode::Custom;
}

static void RefreshPointerPresentation() {
    const HMODULE display = GetModuleHandleW(L"DisplayManager.dll");
    const HMODULE wrapper = GetModuleHandleW(L"ddraw.dll");
    if (display != g_pointerPresentation.display || wrapper != g_pointerPresentation.wrapper)
        LoadPointerPresentation(display, wrapper);
    auto& state = g_pointerPresentation;
    state.backbufferWidth = state.backbufferHeight = 0;
    IDirect3DSurface9* backbuffer = nullptr;
    if (g_d3dDevice && SUCCEEDED(g_d3dDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer))) {
        D3DSURFACE_DESC desc{};
        if (SUCCEEDED(backbuffer->GetDesc(&desc))) {
            state.backbufferWidth = int(desc.Width);
            state.backbufferHeight = int(desc.Height);
        }
        backbuffer->Release();
    }
    if (state.active || !state.enabled || !state.callRva || g_externalFallbackHost) return;
    const unsigned visibility = g_visibilityGeneration.load();
    if (state.visibility != visibility) {
        state.visibility = visibility;
        state.attempts = 0;
    }
    const DWORD now = GetTickCount();
    if (state.attempts >= 120 || (state.attempts != 0 && LONG(now - state.nextProbe) < 0)) return;
    ++state.attempts;
    state.nextProbe = now + 250;
    const BYTE* site = reinterpret_cast<const BYTE*>(wrapper) + state.callRva;
    BYTE bytes[6]{};
    SIZE_T read = 0;
    if (ReadProcessMemory(GetCurrentProcess(), site, bytes, sizeof(bytes), &read) && read == sizeof(bytes))
        state.active = GuiPointer::OwnsFinalBlit(bytes, sizeof(bytes), uint32_t(reinterpret_cast<uintptr_t>(site)),
            uint32_t(reinterpret_cast<uintptr_t>(display)), state.displaySize);
}

static ImVec2 MapClientPointer(HWND hwnd, float x, float y) {
    RECT client{};
    if (!hwnd || !GetClientRect(hwnd, &client)) return ImVec2(-FLT_MAX, -FLT_MAX);
    const auto& state = g_pointerPresentation;
    const bool adapted = !g_externalFallbackHost && state.active
        && state.backbufferWidth > 0 && state.backbufferHeight > 0;
    return GuiPointer::Map(x, y, client.right - client.left, client.bottom - client.top,
        state.backbufferWidth, state.backbufferHeight, adapted ? state.settings : GuiPointer::Settings{});
}

static void SetMappedPointer(HWND hwnd, const ImVec2& position) {
    RECT client{};
    if (!hwnd || !GetClientRect(hwnd, &client)) return;
    const auto& state = g_pointerPresentation;
    const bool adapted = !g_externalFallbackHost && state.active
        && state.backbufferWidth > 0 && state.backbufferHeight > 0;
    const ImVec2 point = GuiPointer::Unmap(position.x, position.y,
        client.right - client.left, client.bottom - client.top,
        state.backbufferWidth, state.backbufferHeight, adapted ? state.settings : GuiPointer::Settings{});
    if (point.x == -FLT_MAX) return;
    POINT screen{LONG(point.x), LONG(point.y)};
    if (ClientToScreen(hwnd, &screen)) SetCursorPos(screen.x, screen.y);
}

static ImVec2 ReadMappedPointer(HWND hwnd) {
    POINT point{};
    if (!hwnd || GetForegroundWindow() != hwnd || !GetCursorPos(&point)
        || !ScreenToClient(hwnd, &point)) return ImVec2(-FLT_MAX, -FLT_MAX);
    return MapClientPointer(hwnd, float(point.x), float(point.y));
}

static void ForwardWin32Input(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ImGuiContext& context = *ImGui::GetCurrentContext();
    const int firstEvent = context.InputEventsQueue.Size;
    ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
    if (msg == WM_MOUSEMOVE || msg == WM_NCMOUSEMOVE) {
        POINT point{LONG(short(LOWORD(lParam))), LONG(short(HIWORD(lParam)))};
        const bool valid = msg == WM_MOUSEMOVE || ScreenToClient(hwnd, &point);
        GuiPointer::ReplaceBackendMousePosition(context, firstEvent,
            valid ? MapClientPointer(hwnd, float(point.x), float(point.y)) : ImVec2(-FLT_MAX, -FLT_MAX));
    }
}

static inline float ClampF(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static bool IsVkDownForImGuiNav(int vk) {
    return vk > 0 && (GetAsyncKeyState(vk) & 0x8000) != 0;
}

static uint8_t ReadMenuGameplayInputs() {
    int localSide = SwitchPlayers::GetLocalSide();
    int localPlayer = 1;
    if (localSide == 1) {
        localPlayer = 2;
    }

    uint8_t inputs = GetPlayerInputs(localPlayer);
    if (inputs == 0 && localPlayer != 1) {
        inputs = GetPlayerInputs(1);
    }
    return inputs;
}

struct CachedWindowMetrics {
    HWND hwnd = nullptr;
    DWORD tick = 0;
    bool fullscreen = false;
    float clientW = 0.0f;
    float clientH = 0.0f;
};

static void GetCachedWindowMetrics(HWND hwnd, bool& fullscreen, float& clientW, float& clientH) {
    static CachedWindowMetrics s_cache{};
    if (!hwnd) {
        fullscreen = false;
        clientW = 0.0f;
        clientH = 0.0f;
        s_cache = {};
        return;
    }

    const DWORD now = GetTickCount();
    if (hwnd == s_cache.hwnd && s_cache.tick != 0 && (now - s_cache.tick) < 125) {
        fullscreen = s_cache.fullscreen;
        clientW = s_cache.clientW;
        clientH = s_cache.clientH;
        return;
    }

    RECT clientRect{};
    float cachedClientW = 0.0f;
    float cachedClientH = 0.0f;
    if (GetClientRect(hwnd, &clientRect)) {
        cachedClientW = static_cast<float>(clientRect.right - clientRect.left);
        cachedClientH = static_cast<float>(clientRect.bottom - clientRect.top);
    }

    WINDOWPLACEMENT wp{ sizeof(WINDOWPLACEMENT) };
    RECT wndRect{};
    HMONITOR mon = nullptr;
    MONITORINFO mi{ sizeof(MONITORINFO) };
    bool cachedFullscreen = false;
    if (GetWindowPlacement(hwnd, &wp)
        && GetWindowRect(hwnd, &wndRect)
        && (mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY)) != nullptr
        && GetMonitorInfo(mon, &mi)) {
        cachedFullscreen = EqualRect(&wndRect, &mi.rcMonitor) || EqualRect(&wndRect, &mi.rcWork);
    }

    s_cache.hwnd = hwnd;
    s_cache.tick = now;
    s_cache.fullscreen = cachedFullscreen;
    s_cache.clientW = cachedClientW;
    s_cache.clientH = cachedClientH;

    fullscreen = cachedFullscreen;
    clientW = cachedClientW;
    clientH = cachedClientH;
}

static bool IsFullscreen(HWND hwnd) {
    bool fullscreen = false;
    float clientW = 0.0f;
    float clientH = 0.0f;
    GetCachedWindowMetrics(hwnd, fullscreen, clientW, clientH);
    return fullscreen;
}

static void ReleaseGamepadNavInputs(ImGuiIO& io) {
    io.AddKeyEvent(ImGuiKey_GamepadFaceDown,  false);
    io.AddKeyEvent(ImGuiKey_GamepadFaceRight, false);
    io.AddKeyEvent(ImGuiKey_GamepadFaceLeft,  false);
    io.AddKeyEvent(ImGuiKey_GamepadFaceUp,    false);
    io.AddKeyEvent(ImGuiKey_GamepadL1,        false);
    io.AddKeyEvent(ImGuiKey_GamepadR1,        false);
    io.AddKeyEvent(ImGuiKey_GamepadBack,      false);
    io.AddKeyEvent(ImGuiKey_GamepadStart,     false);
    io.AddKeyEvent(ImGuiKey_GamepadL3,        false);
    io.AddKeyEvent(ImGuiKey_GamepadR3,        false);
    io.AddKeyEvent(ImGuiKey_GamepadDpadLeft,  false);
    io.AddKeyEvent(ImGuiKey_GamepadDpadRight, false);
    io.AddKeyEvent(ImGuiKey_GamepadDpadUp,    false);
    io.AddKeyEvent(ImGuiKey_GamepadDpadDown,  false);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickLeft,  false, 0.f);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, false, 0.f);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickUp,    false, 0.f);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickDown,  false, 0.f);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadL2,          false, 0.f);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadR2,          false, 0.f);
    io.AddKeyEvent(ImGuiKey_Enter, false);
    io.AddKeyEvent(ImGuiKey_Escape, false);
}

static void UpdateVirtualCursor(ImGuiIO& io) {
    const auto& cfg = Config::GetSettings();
    HWND hwnd = GetActiveHostWindow();
    float clientW = io.DisplaySize.x;
    float clientH = io.DisplaySize.y;
    bool fullscreen = false;
    if (hwnd) {
        float physicalW = 0.0f, physicalH = 0.0f;
        GetCachedWindowMetrics(hwnd, fullscreen, physicalW, physicalH);
    }
    
    static ImVec2 s_lastMousePos = ImVec2(-1.0f, -1.0f);
    static bool s_mouseHasMoved = false;
    
    if (fullscreen && hwnd) {
        POINT pt;
        if (GetCursorPos(&pt) && ScreenToClient(hwnd, &pt)) {
            ImVec2 currentPos((float)pt.x, (float)pt.y);
            if (s_lastMousePos.x >= 0.0f && s_lastMousePos.y >= 0.0f) {
                float dx = currentPos.x - s_lastMousePos.x;
                float dy = currentPos.y - s_lastMousePos.y;
                float distSq = dx * dx + dy * dy;
                if (distSq > 4.0f) {
                    s_mouseHasMoved = true;
                }
            }
            s_lastMousePos = currentPos;
        }
    }
    
    const bool allowCursor = cfg.enableVirtualCursor && g_imguiVisible && hwnd && fullscreen && s_mouseHasMoved;
    bool wasActive = g_useVirtualCursor;
    g_useVirtualCursor = allowCursor;
    
    if (!g_imguiVisible) {
        s_mouseHasMoved = false;
    }

    static bool s_lastWindowFocused = false;
    bool focusedNow = (hwnd && GetForegroundWindow() == hwnd);
    bool regainedFocus = (focusedNow && !s_lastWindowFocused);
    s_lastWindowFocused = focusedNow;
    static bool s_lastMiddleDown = false;
    static bool s_ignoreMiddleUntilUp = false;
    static int  s_unstickFrames = 0;
    static bool s_lastLThumbDown = false;

    static bool s_releasedGamepadWhileHidden = false;
    if (!g_imguiVisible) {
        if (!s_releasedGamepadWhileHidden) {
            ReleaseGamepadNavInputs(io);
            s_releasedGamepadWhileHidden = true;
        }
        g_useVirtualCursor = false;
        io.MouseDrawCursor = false;
        return;
    }
    s_releasedGamepadWhileHidden = false;

    // The custom menu reads io.MousePos for real hover only; gamepad motion fed
    // here makes the menu self-navigate, so skip the virtual cursor for it.
    const bool customMenuOwnsNavigation =
        g_externalFallbackHost || cfg.useCustomMenu || CustomMenu::PaletteEditorPanel::IsOpen();
    if (customMenuOwnsNavigation) {
        ReleaseGamepadNavInputs(io);
        g_useVirtualCursor = false;
        return;
    }

    if (g_useVirtualCursor && (!wasActive || regainedFocus)) {
        ImVec2 target = (g_overlayCenter.x > 0.f && g_overlayCenter.y > 0.f)
            ? g_overlayCenter
            : ImVec2(clientW * 0.5f, clientH * 0.5f);
        g_virtualCursorPos = target;
    }

    if (focusedNow && g_imguiVisible) {
        SHORT mm = GetAsyncKeyState(VK_MBUTTON);
        bool middleDown = (mm & 0x8000) != 0;
        bool middlePressed = middleDown && !s_lastMiddleDown;
        if (middlePressed) {
            ImVec2 target = (g_overlayCenter.x > 0.f && g_overlayCenter.y > 0.f)
                ? g_overlayCenter
                : ImVec2(clientW * 0.5f, clientH * 0.5f);
            g_virtualCursorPos = target;
            if (hwnd) {
                ShowWindow(hwnd, SW_RESTORE);
                BringWindowToTop(hwnd);
                SetForegroundWindow(hwnd);
                SetActiveWindow(hwnd);
                SetFocus(hwnd);
            }
            SetMappedPointer(hwnd, target);
            ReleaseCapture();
            io.AddMouseButtonEvent(0, false); // left up
            io.AddMouseButtonEvent(1, false); // right up
            io.AddMouseButtonEvent(2, false); // middle up
            s_ignoreMiddleUntilUp = true;
            s_unstickFrames = 2;
            io.AddMousePosEvent(g_virtualCursorPos.x, g_virtualCursorPos.y);
            g_requestOverlayFocus = true;
        }
        if (s_ignoreMiddleUntilUp) {
            if (middleDown) {
                io.AddMouseButtonEvent(2, false);
            } else {
                s_ignoreMiddleUntilUp = false;
            }
        }
        if (s_unstickFrames > 0) {
            io.AddMouseButtonEvent(0, false);
            io.AddMouseButtonEvent(1, false);
            io.AddMouseButtonEvent(2, false);
            io.AddMousePosEvent(g_virtualCursorPos.x, g_virtualCursorPos.y);
            --s_unstickFrames;
        }
        s_lastMiddleDown = middleDown;
    }

    // Copy one coherent snapshot per frame; never retain pointers into the cache.
    XInputShim::RefreshSnapshotOncePerFrame();
    XInputShim::Snapshot padSnapshot{};
    XInputShim::CopySnapshot(padSnapshot);

    static int s_lastActivePad = 0;
    XINPUT_STATE states[4]{};
    bool connected[4] = {false,false,false,false};
    unsigned connectedMask = 0;
    for (int i = 0; i < 4; ++i) {
        connected[i] = padSnapshot.CopyState(i, states[i]);
        if (connected[i]) connectedMask |= (1u << i);
    }

    const int cfgIndex = cfg.controllerIndex;
    if (cfgIndex < 0 || cfgIndex > 3) {
    const int dzL = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
        for (int i = 0; i < 4; ++i) if (connected[i]) {
            const auto& gp = states[i].Gamepad;
            bool anyBtn = gp.wButtons != 0;
            bool trig = gp.bLeftTrigger > 30 || gp.bRightTrigger > 30;
            bool lx = (gp.sThumbLX > dzL || gp.sThumbLX < -dzL);
            bool ly = (gp.sThumbLY > dzL || gp.sThumbLY < -dzL);
            if (anyBtn || trig || lx || ly) { s_lastActivePad = i; break; }
        }
    } else if (cfgIndex >= 0 && cfgIndex <= 3 && connected[cfgIndex]) {
        s_lastActivePad = cfgIndex;
    }

    const bool anyConnected = connectedMask != 0;
    if (g_imguiVisible) io.BackendFlags |= ImGuiBackendFlags_HasGamepad; else {
        if (anyConnected) io.BackendFlags |= ImGuiBackendFlags_HasGamepad; else io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
    }
    auto axisToAnalog = [](SHORT v, SHORT dz) -> float {
        int iv = (int)v;
        if (iv > dz) iv -= dz; else if (iv < -dz) iv += dz; else iv = 0;
        float n = (float)iv / (32767.0f - dz);
        if (n > 1.f) n = 1.f; if (n < -1.f) n = -1.f;
        return n;
    };
    const float navAnalogThreshold = ClampF(cfg.guiNavAnalogThreshold, 0.05f, 0.95f);

    bool kFaceDown=false, kFaceRight=false, kFaceLeft=false, kFaceUp=false;
    bool kL1=false, kR1=false, kBack=false, kStart=false, kL3=false, kR3=false;
    bool kDpadL=false, kDpadR=false, kDpadU=false, kDpadD=false;
    float aLLeft=0.f, aLRight=0.f, aLUp=0.f, aLDown=0.f;
    float aL2=0.f, aR2=0.f;
    if (anyConnected) {
        for (int i = 0; i < 4; ++i) if (connected[i]) {
            const auto& gp = states[i].Gamepad;
            kFaceDown |= (gp.wButtons & XINPUT_GAMEPAD_A) != 0;
            kFaceRight |= (gp.wButtons & XINPUT_GAMEPAD_B) != 0;
            kFaceLeft |= (gp.wButtons & XINPUT_GAMEPAD_X) != 0;
            kFaceUp |= (gp.wButtons & XINPUT_GAMEPAD_Y) != 0;
            kL1 |= (gp.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
            kR1 |= (gp.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
            kBack |= (gp.wButtons & XINPUT_GAMEPAD_BACK) != 0;
            kStart |= (gp.wButtons & XINPUT_GAMEPAD_START) != 0;
            kL3 |= (gp.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
            kR3 |= (gp.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;
            kDpadL |= (gp.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
            kDpadR |= (gp.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;
            kDpadU |= (gp.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0;
            kDpadD |= (gp.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0;

            float lxNorm = axisToAnalog(gp.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
            float lyNorm = axisToAnalog(gp.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
            {
                float cLeft  = (lxNorm < 0.f) ? -lxNorm : 0.f;
                float cRight = (lxNorm > 0.f) ?  lxNorm : 0.f;
                float cUp    = (lyNorm > 0.f) ?  lyNorm : 0.f;
                float cDown  = (lyNorm < 0.f) ? -lyNorm : 0.f;
                aLLeft  = (aLLeft  > cLeft)  ? aLLeft  : cLeft;
                aLRight = (aLRight > cRight) ? aLRight : cRight;
                aLUp    = (aLUp    > cUp)    ? aLUp    : cUp;
                aLDown  = (aLDown  > cDown)  ? aLDown  : cDown;
            }

            {
                float cL2 = gp.bLeftTrigger / 255.0f;
                float cR2 = gp.bRightTrigger / 255.0f;
                aL2 = (aL2 > cL2) ? aL2 : cL2;
                aR2 = (aR2 > cR2) ? aR2 : cR2;
            }
        }
    }

    {
        const uint8_t menuInputs = ReadMenuGameplayInputs();
        kDpadL |= (menuInputs & INPUT_LEFT)  != 0;
        kDpadR |= (menuInputs & INPUT_RIGHT) != 0;
        kDpadU |= (menuInputs & INPUT_UP)    != 0;
        kDpadD |= (menuInputs & INPUT_DOWN)  != 0;
        kFaceDown  |= (menuInputs & INPUT_A) != 0;
        kFaceRight |= (menuInputs & INPUT_B) != 0;
        kFaceLeft  |= (menuInputs & INPUT_C) != 0;
        kFaceUp    |= (menuInputs & INPUT_D) != 0;
    }

    if (detectedBindings.directionsDetected) {
        kDpadL |= IsVkDownForImGuiNav(detectedBindings.leftKey);
        kDpadR |= IsVkDownForImGuiNav(detectedBindings.rightKey);
        kDpadU |= IsVkDownForImGuiNav(detectedBindings.upKey);
        kDpadD |= IsVkDownForImGuiNav(detectedBindings.downKey);
    }
    if (detectedBindings.attacksDetected) {
        kFaceDown  |= IsVkDownForImGuiNav(detectedBindings.aButton);
        kFaceRight |= IsVkDownForImGuiNav(detectedBindings.bButton);
        kFaceLeft  |= IsVkDownForImGuiNav(detectedBindings.cButton);
        kFaceUp    |= IsVkDownForImGuiNav(detectedBindings.dButton);
    }

    {
        bool rawDir[4] = {
            kDpadL || (aLLeft  >= navAnalogThreshold),
            kDpadR || (aLRight >= navAnalogThreshold),
            kDpadU || (aLUp    >= navAnalogThreshold),
            kDpadD || (aLDown  >= navAnalogThreshold),
        };

        static bool  s_prevRaw[4]        = {false, false, false, false};
        static float s_holdTime[4]       = {0, 0, 0, 0};
        static float s_nextRepeatAt[4]   = {0, 0, 0, 0};

        const float holdThreshold = 1.0f;

        bool gated[4];
        for (int i = 0; i < 4; i++) {
            if (rawDir[i]) {
                if (!s_prevRaw[i]) {
                    gated[i] = true;
                    s_holdTime[i] = 0.f;
                    s_nextRepeatAt[i] = holdThreshold;
                } else {
                    s_holdTime[i] += io.DeltaTime;
                    if (s_holdTime[i] >= s_nextRepeatAt[i]) {
                        gated[i] = true;
                        float elapsed = s_holdTime[i] - holdThreshold;
                        float interval;
                        if (elapsed < 0.5f)       interval = 0.18f;
                        else if (elapsed < 1.5f)   interval = 0.09f;
                        else                        interval = 0.045f;
                        s_nextRepeatAt[i] = s_holdTime[i] + interval;
                    } else {
                        gated[i] = false;
                    }
                }
            } else {
                gated[i] = false;
                s_holdTime[i] = 0.f;
                s_nextRepeatAt[i] = 0.f;
            }
            s_prevRaw[i] = rawDir[i];
        }

        // Nav flows only through these gated bools; non-zero LStick analog
        // double-triggers in ImGui's NavUpdate.
        kDpadL = gated[0]; kDpadR = gated[1];
        kDpadU = gated[2]; kDpadD = gated[3];
        aLLeft = 0.f; aLRight = 0.f; aLUp = 0.f; aLDown = 0.f;
    }

    if (anyConnected) {
        io.AddKeyEvent(ImGuiKey_GamepadFaceDown,  kFaceDown);
        io.AddKeyEvent(ImGuiKey_GamepadFaceRight, kFaceRight);
        io.AddKeyEvent(ImGuiKey_GamepadFaceLeft,  kFaceLeft);
        io.AddKeyEvent(ImGuiKey_GamepadFaceUp,    kFaceUp);
        io.AddKeyEvent(ImGuiKey_GamepadL1,        kL1);
        io.AddKeyEvent(ImGuiKey_GamepadR1,        kR1);
        io.AddKeyEvent(ImGuiKey_GamepadBack,      kBack);
        io.AddKeyEvent(ImGuiKey_GamepadStart,     kStart);
        io.AddKeyEvent(ImGuiKey_GamepadL3,        kL3);
        io.AddKeyEvent(ImGuiKey_GamepadR3,        kR3);
        io.AddKeyEvent(ImGuiKey_GamepadDpadLeft,  kDpadL);
        io.AddKeyEvent(ImGuiKey_GamepadDpadRight, kDpadR);
        io.AddKeyEvent(ImGuiKey_GamepadDpadUp,    kDpadU);
        io.AddKeyEvent(ImGuiKey_GamepadDpadDown,  kDpadD);
        io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickLeft,  false, 0.f);
        io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, false, 0.f);
        io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickUp,    false, 0.f);
        io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickDown,  false, 0.f);
        io.AddKeyAnalogEvent(ImGuiKey_GamepadL2,          aL2 > 0.05f, aL2);
        io.AddKeyAnalogEvent(ImGuiKey_GamepadR2,          aR2 > 0.05f, aR2);
    } else {
        io.AddKeyEvent(ImGuiKey_GamepadFaceDown,  false);
        io.AddKeyEvent(ImGuiKey_GamepadFaceRight, false);
        io.AddKeyEvent(ImGuiKey_GamepadFaceLeft,  false);
        io.AddKeyEvent(ImGuiKey_GamepadFaceUp,    false);
        io.AddKeyEvent(ImGuiKey_GamepadL1,        false);
        io.AddKeyEvent(ImGuiKey_GamepadR1,        false);
        io.AddKeyEvent(ImGuiKey_GamepadBack,      false);
        io.AddKeyEvent(ImGuiKey_GamepadStart,     false);
        io.AddKeyEvent(ImGuiKey_GamepadL3,        false);
        io.AddKeyEvent(ImGuiKey_GamepadR3,        false);
        io.AddKeyEvent(ImGuiKey_GamepadDpadLeft,  false);
        io.AddKeyEvent(ImGuiKey_GamepadDpadRight, false);
        io.AddKeyEvent(ImGuiKey_GamepadDpadUp,    false);
        io.AddKeyEvent(ImGuiKey_GamepadDpadDown,  false);
        io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickLeft,  false, 0.f);
        io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, false, 0.f);
        io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickUp,    false, 0.f);
        io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickDown,  false, 0.f);
        io.AddKeyAnalogEvent(ImGuiKey_GamepadL2,          false, 0.f);
        io.AddKeyAnalogEvent(ImGuiKey_GamepadR2,          false, 0.f);
    }

    io.AddKeyEvent(ImGuiKey_Enter,  kFaceDown);
    io.AddKeyEvent(ImGuiKey_Escape, kFaceRight || kBack);

    int selPad = -1;
    if (cfgIndex >= 0 && cfgIndex <= 3) selPad = cfgIndex; else selPad = s_lastActivePad;
    if (selPad < 0 || selPad > 3 || !connected[selPad]) {
        for (int i = 0; i < 4; ++i) if (connected[i]) { selPad = i; break; }
    }

    if (selPad >= 0 && selPad <= 3 && connected[selPad]) {
    const auto& selGp = states[selPad].Gamepad;
        bool lThumbDown = (selGp.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
        bool lThumbPressed = lThumbDown && !s_lastLThumbDown && focusedNow && g_imguiVisible;
        if (lThumbPressed) {
            ImVec2 target = (g_overlayCenter.x > 0.f && g_overlayCenter.y > 0.f)
                ? g_overlayCenter
                : ImVec2(clientW * 0.5f, clientH * 0.5f);
            g_virtualCursorPos = target;
            g_requestOverlayFocus = true;
        }
        s_lastLThumbDown = lThumbDown;

        {
            const auto& cfg2 = Config::GetSettings();
            auto isBindingDown = [&](int mask)->bool {
                if (mask < 0) return false;
                if (mask == 0x10000) return selGp.bLeftTrigger > 30;
                if (mask == 0x20000) return selGp.bRightTrigger > 30;
                return (selGp.wButtons & (WORD)mask) != 0;
            };
            static bool s_lastTopPrev=false, s_lastTopNext=false, s_lastSubPrev=false, s_lastSubNext=false;
            bool nowTopPrev = isBindingDown(cfg2.gpUiTopTabPrev);
            bool nowTopNext = isBindingDown(cfg2.gpUiTopTabNext);
            bool nowSubPrev = isBindingDown(cfg2.gpUiSubTabPrev);
            bool nowSubNext = isBindingDown(cfg2.gpUiSubTabNext);
            bool pressTopPrev = nowTopPrev && !s_lastTopPrev;
            bool pressTopNext = nowTopNext && !s_lastTopNext;
            bool pressSubPrev = nowSubPrev && !s_lastSubPrev;
            bool pressSubNext = nowSubNext && !s_lastSubNext;
            if (g_imguiVisible) {
                if (pressTopPrev) ImGuiGui::RequestTopTabCycle(-1);
                if (pressTopNext) ImGuiGui::RequestTopTabCycle(+1);
                if (pressSubPrev) ImGuiGui::RequestActiveSubTabCycle(-1);
                if (pressSubNext) ImGuiGui::RequestActiveSubTabCycle(+1);
            }
            s_lastTopPrev = nowTopPrev;
            s_lastTopNext = nowTopNext;
            s_lastSubPrev = nowSubPrev;
            s_lastSubNext = nowSubNext;
        }

        float dt = io.DeltaTime > 0.f ? io.DeltaTime : (1.f/60.f);

        if (Config::GetSettings().guiScrollRightStickEnable) {
            auto applyDeadzone = [](SHORT v, SHORT dz) -> float {
                int iv = (int)v;
                if (iv > dz) iv -= dz; else if (iv < -dz) iv += dz; else iv = 0;
                float n = (float)iv / (32767.0f - dz);
                if (n > 1.f) n = 1.f; if (n < -1.f) n = -1.f;
                return n;
            };
            
            float rx = applyDeadzone(selGp.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
            float ry = applyDeadzone(selGp.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
            const float dz = 0.15f;
            if (fabsf(rx) < dz) rx = 0.0f;
            if (fabsf(ry) < dz) ry = 0.0f;
            if (rx != 0.0f || ry != 0.0f) {
                float scale = Config::GetSettings().guiScrollRightStickScale;
                // Deliberately inverted: positive stick Y gives a negative wheel delta.
                float wheelX = rx * scale * dt;
                float wheelY = -ry * scale * dt;
                io.AddMouseWheelEvent(wheelX, wheelY);
            }
        }
    } else {
        s_lastLThumbDown = false;
    }

    if (g_useVirtualCursor && hwnd) {
        g_virtualCursorPos = ReadMappedPointer(hwnd);
    }

    if (g_useVirtualCursor) {
        io.MouseDrawCursor = true;
        io.AddMousePosEvent(g_virtualCursorPos.x, g_virtualCursorPos.y);
    } else {
        io.MouseDrawCursor = false;
    }
}

static bool TrainingUiInputActive() {
    return XInputShim::IsPollingActive() &&
        (g_imguiVisible || Mission::PauseMenu::WantsDraw() || Mission::TutorialSession::WantsDraw());
}

LRESULT CALLBACK ImGuiWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NULL) g_wndProcProbeSeen.store(true, std::memory_order_release);
    // A peer re-subclass can close the WndProc chain into a ring; hand the
    // identical re-entering message to the class procedure once and stop.
    if (g_wndProcDepth > 0 && g_wndProcVisit.hwnd == hWnd && g_wndProcVisit.msg == msg
        && g_wndProcVisit.wParam == wParam && g_wndProcVisit.lParam == lParam) {
        if (!g_wndProcRingReported) {
            g_wndProcRingReported = true;
            LogOut("[IMGUI] WndProc chain is a ring (a peer re-subclassed over us); routing the message to the window class procedure", true);
        }
        const WNDPROC classProc = reinterpret_cast<WNDPROC>(GetClassLongPtr(hWnd, GCLP_WNDPROC));
        if (classProc && classProc != ImGuiWndProc) {
            return CallWindowProc(classProc, hWnd, msg, wParam, lParam);
        }
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    WndProcVisitScope visit(hWnd, msg, wParam, lParam);

    ImGuiImpl::DeviceRenderScope input;
    const auto forward = [&]() -> LRESULT {
        input.Release();
        const WNDPROC original = g_originalWndProc.load(std::memory_order_acquire);
        return original
            ? CallWindowProc(original, hWnd, msg, wParam, lParam)
            : DefWindowProc(hWnd, msg, wParam, lParam);
    };
    if (!input.Admitted() || !g_imguiInitialization.IsReady() || !TrainingUiInputActive()) {
        return forward();
    }

    // Arrows would double-navigate via the Win32 backend; PreNewFrameInputs owns all directional nav.
    if (g_imguiVisible && (msg == WM_KEYDOWN || msg == WM_KEYUP)) {
        if (wParam == VK_UP || wParam == VK_DOWN || wParam == VK_LEFT || wParam == VK_RIGHT) {
            return forward();
        }
    }

    // Hidden/out-of-Battle menus must not accumulate input events.
    ForwardWin32Input(hWnd, msg, wParam, lParam);

    if (g_imguiVisible) {
        ImGuiIO& io = ImGui::GetIO();
        const bool isMouseMsg = (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL;
        const bool isKeyMsg = (msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP || msg == WM_CHAR);
        if ((isMouseMsg && io.WantCaptureMouse) || (isKeyMsg && io.WantCaptureKeyboard)) {
            return 1;
        }
    }

    return forward();
}

LRESULT CALLBACK FallbackWindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CLOSE) {
        if (g_imguiVisible) {
            ImGuiImpl::ToggleVisibility();
        }
        return 0;
    }

    if (g_imguiInitialization.IsReady() && g_externalFallbackHost) {
        const LRESULT handled = ImGuiImpl::WndProc(hWnd, msg, wParam, lParam);
        if (handled != 0) {
            return handled;
        }
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

namespace ImGuiImpl {
    DeviceRenderScope::DeviceRenderScope(IDirect3DDevice9* device)
        : access_(g_resetGate) {
        if (!access_.Acquired()) return;
        if (!device) device = g_d3dDevice;
        if (!g_resetGate.CanRender(device, GetCurrentThreadId())
            || (g_imguiContext && g_imguiContextThread != GetCurrentThreadId())) {
            access_.Release();
            return;
        }
        previousContext_ = ImGui::GetCurrentContext();
        if (g_imguiContext) ImGui::SetCurrentContext(g_imguiContext);
        g_imguiWorkThread.store(GetCurrentThreadId(), std::memory_order_release);
        admitted_ = true;
    }

    DeviceRenderScope::~DeviceRenderScope() { Release(); }

    void DeviceRenderScope::Release() {
        if (admitted_) {
            g_imguiWorkThread.store(0, std::memory_order_release);
            ImGui::SetCurrentContext(previousContext_);
            admitted_ = false;
        }
        access_.Release();
    }

    static float GetDpiScale() {
        HWND hwnd = GetActiveHostWindow();
        if (!hwnd) {
            LogOut("[IMGUI] GetDpiScale: No EFZ window found, returning 1.0", true);
            return 1.0f;
        }
        
        typedef UINT(WINAPI* GetDpiForWindowFunc)(HWND);
        HMODULE user32 = GetModuleHandleA("user32.dll");
        if (user32) {
            GetDpiForWindowFunc pGetDpiForWindow = (GetDpiForWindowFunc)GetProcAddress(user32, "GetDpiForWindow");
            if (pGetDpiForWindow) {
                UINT dpi = pGetDpiForWindow(hwnd);
                float scale = (float)dpi / 96.0f;
                LogOut((std::string("[IMGUI] DPI detected: ") + std::to_string(dpi) + " (scale=" + std::to_string(scale) + ")").c_str(), true);
                return scale;
            }
        }
        
        HDC hdc = GetDC(hwnd);
        if (hdc) {
            int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
            ReleaseDC(hwnd, hdc);
            float scale = (float)dpiX / 96.0f;
            LogOut((std::string("[IMGUI] DPI detected (fallback): ") + std::to_string(dpiX) + " (scale=" + std::to_string(scale) + ")").c_str(), true);
            return scale;
        }
        
        LogOut("[IMGUI] GetDpiScale: Could not detect DPI, returning 1.0", true);
        return 1.0f;
    }

    static void UpdateFontAtlasForScale(float uiScale)
    {
        if (!ImGui::GetCurrentContext()) return;
        ImGuiIO& io = ImGui::GetIO();

        static auto s_lastAttempt = std::chrono::steady_clock::time_point{};
        auto now = std::chrono::steady_clock::now();
        bool timeOk = (s_lastAttempt.time_since_epoch().count() == 0) || (now - s_lastAttempt >= std::chrono::milliseconds(1000));

        const int fontMode = Config::GetSettings().uiFontMode;
        float dpiScale = GetDpiScale();
        float s = uiScale * dpiScale;
        if (s < 0.70f) s = 0.70f; else if (s > 2.50f) s = 2.50f;
        float sRounded = floorf(s * 100.0f + 0.5f) / 100.0f;

        const bool scaleChanged = !(fabsf(sRounded - g_lastFontScaleApplied) < 0.01f);
        const bool fontChanged  = (fontMode != g_lastFontModeApplied);
        if (!scaleChanged && !fontChanged) return;
        if (!timeOk && scaleChanged && !fontChanged) return;

        s_lastAttempt = now;

        const float basePx = 13.0f;
        const float targetPx = roundf(basePx * sRounded);

        ImGui_ImplDX9_InvalidateDeviceObjects();
        CustomMenu::Fonts::InvalidateAtlasReferences();
        io.Fonts->Clear();
        io.Fonts->Flags |= ImFontAtlasFlags_NoPowerOfTwoHeight;
        io.Fonts->TexGlyphPadding = 1;

        ImFontConfig cfg;
        cfg.OversampleH = 3;
        cfg.OversampleV = 3;
        cfg.PixelSnapH = false;
        cfg.SizePixels = targetPx;

        ImFont* defaultFont = nullptr;
        if (fontMode == 1) {
            const char* segoePath = "C:\\Windows\\Fonts\\segoeui.ttf";
            DWORD fa = GetFileAttributesA(segoePath);
            if (fa != INVALID_FILE_ATTRIBUTES && !(fa & FILE_ATTRIBUTE_DIRECTORY)) {
                defaultFont = io.Fonts->AddFontFromFileTTF(segoePath, targetPx, &cfg);
            }
        }
        if (!defaultFont) {
            defaultFont = io.Fonts->AddFontDefault(&cfg);
        }
        io.FontDefault = defaultFont;

        if (!ImGui_ImplDX9_CreateDeviceObjects()) {
            LogOut("[IMGUI] Warning: Failed to recreate DX9 device objects after font rebuild.", true);
            return;
        }

        g_lastFontScaleApplied = sRounded;
        g_lastFontModeApplied  = fontMode;
        LogOut((std::string("[IMGUI] Rebuilt font atlas: scale=") + std::to_string(sRounded) +
            ", px=" + std::to_string((int)targetPx) + ", font=" + (fontMode==0?"Default":"Segoe UI")).c_str(), true);
    }
    namespace {
        bool InitializeForHost(IDirect3DDevice9* device, HWND hostWindow, bool hookWndProc, bool externalFallback) {
            if (!g_resetGate.CanRender(device, GetCurrentThreadId())) return false;
            if (g_imguiInitialization.IsReady()) {
                if (g_externalFallbackHost != externalFallback || g_imguiHostWindow != hostWindow)
                    return false;
                if (g_d3dDevice == device) return true;
                ImGui::SetCurrentContext(g_imguiContext);
                CustomMenu::PaletteEditorPanel::ReleaseTextures();
                if (g_d3dDevice) ImGui_ImplDX9_Shutdown();
                g_resetGate.UnbindBackend();
                g_d3dDevice = nullptr;
                if (!ImGui_ImplDX9_Init(device)) return false;
                g_d3dDevice = device;
                return g_resetGate.BindBackend(device, GetCurrentThreadId());
            }

            if (!g_imguiInitialization.TryBegin()) {
                return false;
            }

            if (!device) {
                LogOut("[IMGUI] Error: No valid D3D device provided", true);
                g_imguiInitialization.Finish(false);
                return false;
            }

            if (!hostWindow) {
                LogOut("[IMGUI] Error: No valid host window for ImGui", true);
                g_imguiInitialization.Finish(false);
                return false;
            }

            LogOut(std::string("[IMGUI] Initializing ImGui for ") + (externalFallback ? "fallback window" : "game window"), true);

            bool createdContext = false;
            g_d3dDevice = device;
            g_imguiHostWindow = hostWindow;
            g_externalFallbackHost = externalFallback;
            g_originalWndProc.store(nullptr, std::memory_order_release);

            IMGUI_CHECKVERSION();
            g_imguiContext = ImGui::CreateContext();
            g_imguiContextThread = GetCurrentThreadId();
            ImGui::SetCurrentContext(g_imguiContext);
            createdContext = true;
            ImGuiIO& io = ImGui::GetIO(); (void)io;

            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

            {
                const auto& cfg = Config::GetSettings();
                io.KeyRepeatDelay = ClampF(cfg.guiNavRepeatDelay, 0.05f, 1.0f);
                io.KeyRepeatRate  = ClampF(cfg.guiNavRepeatRate,  0.01f, 0.50f);
            }

            io.Fonts->Flags |= ImFontAtlasFlags_NoPowerOfTwoHeight;
            io.Fonts->TexGlyphPadding = 1;

            ImGui::StyleColorsDark();

            ImGuiStyle& style = ImGui::GetStyle();
            style.Alpha = 1.0f;
            if (style.WindowMinSize.x < 1.0f) style.WindowMinSize.x = 16.0f;
            if (style.WindowMinSize.y < 1.0f) style.WindowMinSize.y = 16.0f;

            if (!ImGui_ImplWin32_Init(hostWindow)) {
                LogOut("[IMGUI] Error: ImGui_ImplWin32_Init failed", true);
                ImGui::DestroyContext(g_imguiContext);
                g_imguiContext = nullptr;
                g_imguiContextThread = 0;
                g_d3dDevice = nullptr;
                g_imguiHostWindow = nullptr;
                g_externalFallbackHost = false;
                g_imguiInitialization.Finish(false);
                return false;
            }

            if (!ImGui_ImplDX9_Init(device)) {
                LogOut("[IMGUI] Error: ImGui_ImplDX9_Init failed", true);
                ImGui_ImplWin32_Shutdown();
                ImGui::DestroyContext(g_imguiContext);
                g_imguiContext = nullptr;
                g_imguiContextThread = 0;
                g_d3dDevice = nullptr;
                g_imguiHostWindow = nullptr;
                g_externalFallbackHost = false;
                g_imguiInitialization.Finish(false);
                return false;
            }

            {
                float initScale = Config::GetSettings().uiScale;
                UpdateFontAtlasForScale(initScale);
            }

            if (hookWndProc) {
                SetLastError(0);
                const WNDPROC previousProc = (WNDPROC)SetWindowLongPtr(hostWindow, GWLP_WNDPROC, (LONG_PTR)ImGuiWndProc);
                DWORD wndProcErr = GetLastError();
                g_originalWndProc.store(previousProc, std::memory_order_release);
                g_wndProcAttached.store(!(previousProc == nullptr && wndProcErr != 0), std::memory_order_release);
                if (!previousProc && wndProcErr != 0) {
                    LogOut("[IMGUI] Error: Failed to hook window procedure (err=" + std::to_string(wndProcErr) + ")", true);
                    CustomMenu::PaletteEditorPanel::ReleaseTextures();
                    ImGui_ImplDX9_Shutdown();
                    ImGui_ImplWin32_Shutdown();
                    if (createdContext) {
                        ImGui::DestroyContext(g_imguiContext);
                        g_imguiContext = nullptr;
                        g_imguiContextThread = 0;
                    }
                    g_d3dDevice = nullptr;
                    g_imguiHostWindow = nullptr;
                    g_externalFallbackHost = false;
                    g_imguiInitialization.Finish(false);
                    return false;
                }
            }

            io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
            io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
            io.BackendFlags &= ~ImGuiBackendFlags_HasSetMousePos;

            ImGuiGui::Initialize();

            g_resetGate.BindBackend(device, GetCurrentThreadId());
            g_imguiInitialization.Finish(true);
            LogOut(std::string("[IMGUI] ImGui initialized successfully for ") + (externalFallback ? "fallback window" : "game window"), true);
            return true;
        }

        void SetVisibilityInternal(bool visible) {
            const DWORD thread = GetCurrentThreadId();
            const bool inOwnedFrame = g_imguiWorkThread.load(std::memory_order_acquire) == thread;
            EfzOverlayResetGate::Access access(g_resetGate);
            const bool useContext = inOwnedFrame || (access.Acquired()
                && g_resetGate.CanRender(g_d3dDevice, thread)
                && g_imguiContext && g_imguiContextThread == thread);
            std::optional<EfzScopedImGuiContext> context;
            if (useContext) context.emplace(g_imguiContext);
            const bool wasVisible = g_imguiVisible;
            g_imguiVisible = visible;
            if (!visible) CustomMenu::PaletteEditorPanel::RequestSuspend();
            if (wasVisible != visible) g_visibilityGeneration.fetch_add(1, std::memory_order_acq_rel);
            if (wasVisible != visible && useContext) {
                ImGuiIO& io = ImGui::GetIO();
                io.ClearEventsQueue();
                io.ClearInputKeys();
                io.ClearInputMouse();
            }
            ::menuOpen.store(visible);
            CharacterSettings::g_guiVisible.store(g_imguiVisible, std::memory_order_relaxed);

            if (g_externalFallbackHost && g_externalFallbackWindow && IsWindow(g_externalFallbackWindow)) {
                if (visible) {
                    ShowWindow(g_externalFallbackWindow, SW_SHOWNORMAL);
                    SetForegroundWindow(g_externalFallbackWindow);
                    SetFocus(g_externalFallbackWindow);
                } else {
                    ShowWindow(g_externalFallbackWindow, SW_HIDE);
                }
            }

            if (g_imguiVisible) {
                LogOut("[IMGUI] ImGui interface opened - will render continuously until closed", true);
                g_resetVirtualCursorOnOpen = true;
                ImGuiGui::RequestInitialNavFocus();
                const bool customMenuOwnsNavigation =
                    g_externalFallbackHost || Config::GetSettings().useCustomMenu || CustomMenu::PaletteEditorPanel::IsOpen();
                if (!wasVisible && customMenuOwnsNavigation &&
                    useContext) {
                    // The menu-open press may still be held; every open gets a fresh release gate.
                    CustomMenu::Input::ResetEdges();
                }
                if (useContext) {
                    ImGuiIO& io = ImGui::GetIO();
                    XInputShim::RefreshSnapshotOncePerFrame();
                    XInputShim::Snapshot padSnapshot{};
                    XInputShim::CopySnapshot(padSnapshot);
                    char buf[256];
                    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
                    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
                    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "[IMGUI] Open: NavEnableGamepad=%d BackendHasGamepad=%d GamepadMask=0x%X (xinput=0x%X generic=0x%X, forced)",
                        (io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) ? 1 : 0,
                        (io.BackendFlags & ImGuiBackendFlags_HasGamepad) ? 1 : 0,
                        padSnapshot.connectedMask,
                        padSnapshot.nativeMask,
                        padSnapshot.genericMask);
                    LogOut(buf, true);
                    LogOut("[IMGUI] Keyboard fallback for nav is active (Arrow/Enter/Escape)", true);
                }
                ImGuiGui::RefreshLocalData();
            } else {
                LogOut("[IMGUI] ImGui interface closed", true);
                if (useContext) {
                    ImGuiIO& io = ImGui::GetIO();
                    char buf[256];
                    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "[IMGUI] Close: NavEnableGamepad=%d BackendHasGamepad=%d",
                        (io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) ? 1 : 0,
                        (io.BackendFlags & ImGuiBackendFlags_HasGamepad) ? 1 : 0);
                    LogOut(buf, true);
                }
                StartHotkeyCooldown();
            }

            PauseIntegration::OnMenuVisibilityChanged(g_imguiVisible);
            PracticeHotkeyGate::NotifyMenuVisibility(g_imguiVisible);
            PracticeOverlayGate::SetMenuVisible(g_imguiVisible);
            CharacterSettings::g_guiVisible.store(g_imguiVisible, std::memory_order_relaxed);

            static bool stateLogged = false;
            if (!stateLogged) {
                LogOut("[IMGUI] Visibility state persistence confirmed", true);
                stateLogged = true;
            }
        }

        void RunFallbackWindowThread() {

            const HINSTANCE instance = GetModuleHandleA(nullptr);
            WNDCLASSA wc = {};
            wc.lpfnWndProc = FallbackWindowProc;
            wc.hInstance = instance;
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.lpszClassName = kFallbackWindowClassName;
            ATOM atom = RegisterClassA(&wc);
            if (atom == 0) {
                DWORD le = GetLastError();
                if (le != ERROR_CLASS_ALREADY_EXISTS) {
                    LogOut("[IMGUI] Failed to register fallback window class", true);
                    g_externalFallback.Finish(true);
                    return;
                }
            }

            const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
            RECT rect{0, 0, 960, 720};
            AdjustWindowRect(&rect, style, FALSE);
            HWND hwnd = CreateWindowExA(
                0,
                kFallbackWindowClassName,
                "EfzExtendedPractice",
                style,
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                rect.right - rect.left,
                rect.bottom - rect.top,
                nullptr,
                nullptr,
                instance,
                nullptr);
            if (!hwnd) {
                LogOut("[IMGUI] Failed to create fallback window", true);
                UnregisterClassA(kFallbackWindowClassName, instance);
                g_externalFallback.Finish(true);
                return;
            }
            g_externalFallbackWindow = hwnd;

            LPDIRECT3D9 d3d9 = Direct3DCreate9(D3D_SDK_VERSION);
            if (!d3d9) {
                LogOut("[IMGUI] Failed to create D3D9 object for fallback window", true);
                DestroyWindow(hwnd);
                g_externalFallbackWindow = nullptr;
                UnregisterClassA(kFallbackWindowClassName, instance);
                g_externalFallback.Finish(true);
                return;
            }

            D3DPRESENT_PARAMETERS d3dpp = {};
            d3dpp.Windowed = TRUE;
            d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
            d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
            d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
            d3dpp.hDeviceWindow = hwnd;

            IDirect3DDevice9* device = nullptr;
            HRESULT hr = d3d9->CreateDevice(
                D3DADAPTER_DEFAULT,
                D3DDEVTYPE_HAL,
                hwnd,
                D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE,
                &d3dpp,
                &device);
            if (FAILED(hr) || !device) {
                LogOut("[IMGUI] Failed to create D3D9 device for fallback window", true);
                d3d9->Release();
                DestroyWindow(hwnd);
                g_externalFallbackWindow = nullptr;
                UnregisterClassA(kFallbackWindowClassName, instance);
                g_externalFallback.Finish(true);
                return;
            }

            bool initialized = false;
            {
                DeviceRenderScope initialization(device);
                initialized = initialization.Admitted()
                    && InitializeForHost(device, hwnd, false, true);
            }
            if (!initialized) {
                LogOut("[IMGUI] Failed to initialize ImGui for fallback window", true);
                device->Release();
                d3d9->Release();
                DestroyWindow(hwnd);
                g_externalFallbackWindow = nullptr;
                UnregisterClassA(kFallbackWindowClassName, instance);
                g_externalFallback.Finish(true);
                return;
            }

            ShowWindow(hwnd, SW_HIDE);
            UpdateWindow(hwnd);
            (void)g_externalFallback.PublishReady();
            int idleFrames = 0;
            int notResetCount = 0;
            // Caller holds a DeviceRenderScope. The DX9 backend owns a device
            // reference; shut it down or the device outlives the release.
            const auto releaseDevice = [&]() {
                CustomMenu::PaletteEditorPanel::ReleaseTextures();
                if (g_d3dDevice) ImGui_ImplDX9_Shutdown();
                g_d3dDevice = nullptr;
                g_resetGate.UnbindBackend();
                device->Release();
                device = nullptr;
                d3d9->Release();
                d3d9 = nullptr;
            };

            while (!g_externalFallback.StopRequested() && !g_isShuttingDown.load(std::memory_order_relaxed)) {
                MSG msg;
                while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                    if (msg.message == WM_QUIT) {
                        g_externalFallback.RequestStop();
                        break;
                    }
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }

                if (g_externalFallback.StopRequested() || g_isShuttingDown.load(std::memory_order_relaxed)) {
                    break;
                }

                if (!g_imguiVisible || !XInputShim::IsPollingActive()) {
                    // A second live D3D9 device blocks the game's exclusive Reset.
                    ++idleFrames;
                    if (device && idleFrames > 60) {
                        DeviceRenderScope idle(device);
                        if (idle.Admitted()) {
                            releaseDevice();
                            LogOut("[IMGUI] fallback device released while idle "
                                   "(cooperative exclusive fullscreen)", true);
                        }
                    }
                    Sleep(16);
                    continue;
                }

                if (!device) {
                    idleFrames = 0;
                    d3d9 = Direct3DCreate9(D3D_SDK_VERSION);
                    if (!d3d9) {
                        Sleep(250);
                        continue;
                    }
                    if (FAILED(d3d9->CreateDevice(
                            D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                            D3DCREATE_SOFTWARE_VERTEXPROCESSING |
                                D3DCREATE_FPU_PRESERVE,
                            &d3dpp, &device)) ||
                        !device) {
                        d3d9->Release();
                        d3d9 = nullptr;
                        Sleep(250);
                        continue;
                    }
                    bool rebound = false;
                    {
                        DeviceRenderScope rebind(device);
                        rebound = rebind.Admitted()
                            && InitializeForHost(device, hwnd, false, true);
                    }
                    if (!rebound) {
                        device->Release();
                        device = nullptr;
                        d3d9->Release();
                        d3d9 = nullptr;
                        Sleep(250);
                        continue;
                    }
                }
                idleFrames = 0;

                HRESULT coop = device->TestCooperativeLevel();
                if (coop == D3DERR_DEVICELOST) {
                    Sleep(16);
                    continue;
                }
                if (coop == D3DERR_DEVICENOTRESET) {
                    DeviceRenderScope reset(device);
                    if (!reset.Admitted()) {
                        Sleep(16);
                        continue;
                    }
                    // Windowed reset cannot succeed during game-exclusive fullscreen.
                    if (++notResetCount > 30) {
                        releaseDevice();
                        notResetCount = 0;
                        LogOut("[IMGUI] fallback device stood down (game "
                               "exclusive fullscreen active)", true);
                        continue;
                    }
                    InvalidateDeviceObjects();
                    if (SUCCEEDED(device->Reset(&d3dpp))) {
                        ImGui_ImplDX9_CreateDeviceObjects();
                        notResetCount = 0;
                    } else {
                        Sleep(16);
                    }
                    continue;
                }
                notResetCount = 0;

                device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(12, 12, 16), 1.0f, 0);
                if (SUCCEEDED(device->BeginScene())) {
                    RenderFrame();
                    device->EndScene();
                }
                device->Present(nullptr, nullptr, nullptr, nullptr);
            }

            g_externalFallback.RequestStop();
            // Shutdown try-locks the reset gate; a game-thread admission probe can hold it briefly.
            // A released host must not outlive its thread, so retry until it is gone unless the process is exiting.
            for (unsigned attempt = 0; g_imguiInitialization.IsReady() && g_externalFallbackHost
                    && (attempt < 200 || !g_isShuttingDown.load(std::memory_order_relaxed)); ++attempt) {
                Shutdown();
                if (g_imguiInitialization.IsReady() && g_externalFallbackHost) Sleep(1);
            }

            if (device) device->Release();
            if (d3d9) d3d9->Release();
            DestroyWindow(hwnd);
            g_externalFallbackWindow = nullptr;
            UnregisterClassA(kFallbackWindowClassName, instance);
            g_externalFallback.Finish(false);
        }
    }

    bool Initialize(IDirect3DDevice9* device) {
        if (g_externalFallbackHost) {
            return false;
        }
        return InitializeForHost(device, FindEFZWindow(), true, false);
    }

    bool IsBoundToDevice(IDirect3DDevice9* device) {
        return g_imguiInitialization.IsReady() && g_imguiContext && g_d3dDevice == device
            && !g_externalFallbackHost;
    }
    
    void Shutdown() {
        EfzOverlayResetGate::Access access(g_resetGate);
        if (!access.Acquired()
            || (g_imguiContext && g_imguiContextThread != GetCurrentThreadId()))
            return;
        if (!g_imguiInitialization.IsReady())
            return;

        EfzScopedImGuiContext context(g_imguiContext);
        
        LogOut("[IMGUI] Shutting down ImGui", true);

        HWND hostWindow = GetActiveHostWindow();
        const WNDPROC originalWndProc = g_originalWndProc.load(std::memory_order_acquire);
        if (hostWindow && originalWndProc) {
            WNDPROC currentWndProc = reinterpret_cast<WNDPROC>(GetWindowLongPtr(hostWindow, GWLP_WNDPROC));
            if (currentWndProc == ImGuiWndProc) {
                SetWindowLongPtr(hostWindow, GWLP_WNDPROC, (LONG_PTR)originalWndProc);
            } else {
                LogOut("[IMGUI] Skipping WndProc restore because another hook owns the window proc", true);
            }
        }
        
        CustomMenu::PaletteEditorPanel::ReleaseTextures();
        if (g_d3dDevice) ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        context.ForgetDestroyed(g_imguiContext);
        ImGui::DestroyContext(g_imguiContext);
        g_imguiContext = nullptr;
        g_imguiContextThread = 0;
        g_resetGate.UnbindBackend();
        
        CustomMenu::Fonts::InvalidateAtlasReferences();
        g_lastFontScaleApplied = 0.0f;
        g_lastFontModeApplied = -1;

        g_imguiInitialization.Finish(false);
        g_imguiVisible = false;
        g_d3dDevice = nullptr;
        g_originalWndProc.store(nullptr, std::memory_order_release);
        g_wndProcAttached.store(false, std::memory_order_release);
        g_imguiHostWindow = nullptr;
        g_externalFallbackHost = false;
    }

    bool DetachWndProc() {
        const WNDPROC originalWndProc = g_originalWndProc.load(std::memory_order_acquire);
        if (!g_imguiInitialization.IsReady() || g_externalFallbackHost
            || !g_wndProcAttached.load(std::memory_order_acquire) || !originalWndProc) {
            return false;
        }
        HWND hostWindow = g_imguiHostWindow;
        if (!hostWindow || !IsWindow(hostWindow)) {
            return false;
        }
        const WNDPROC currentWndProc = reinterpret_cast<WNDPROC>(GetWindowLongPtr(hostWindow, GWLP_WNDPROC));
        if (currentWndProc != ImGuiWndProc) {
            // Someone chained over us; rewriting the slot would drop them.
            LogOut("[IMGUI] WndProc detach skipped: another hook owns the window proc", true);
            return false;
        }
        SetLastError(0);
        const LONG_PTR previous = SetWindowLongPtr(hostWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(originalWndProc));
        const DWORD err = GetLastError();
        if (previous == 0 && err != 0) {
            LogOut("[IMGUI] WndProc detach failed (err=" + std::to_string(err) + ")", true);
            return false;
        }
        if (reinterpret_cast<WNDPROC>(previous) != ImGuiWndProc) {
            SetWindowLongPtr(hostWindow, GWLP_WNDPROC, previous);
            LogOut("[IMGUI] WndProc detach raced a peer subclass; restored it", true);
            return false;
        }
        g_wndProcAttached.store(false, std::memory_order_release);
        LogOut("[IMGUI] WndProc detached for netplay suspend", true);
        return true;
    }

    bool AttachWndProc() {
        if (!g_imguiInitialization.IsReady() || g_externalFallbackHost) {
            return false;
        }
        HWND hostWindow = g_imguiHostWindow;
        if (!hostWindow || !IsWindow(hostWindow)) {
            return false;
        }
        const WNDPROC currentWndProc = reinterpret_cast<WNDPROC>(GetWindowLongPtr(hostWindow, GWLP_WNDPROC));
        if (currentWndProc == ImGuiWndProc) {
            g_wndProcAttached.store(true, std::memory_order_release);
            return true;
        }
        if (g_wndProcAttached.load(std::memory_order_acquire)) {
            // A peer on top either chains to us or replaced us. Re-attaching over a live
            // chain would close a ring, so only a message that misses us proves a wipe.
            g_wndProcProbeSeen.store(false, std::memory_order_release);
            DWORD_PTR ignored = 0;
            if (!SendMessageTimeoutA(hostWindow, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 250, &ignored)
                || g_wndProcProbeSeen.load(std::memory_order_acquire)) {
                return false;
            }
            g_wndProcAttached.store(false, std::memory_order_release);
            LogOut("[IMGUI] WndProc was replaced while attached; re-attaching", true);
        }
        // Publish the forward target before the slot: this runs off the window
        // thread, so a message can reach ImGuiWndProc right after the write.
        const WNDPROC savedOriginal = g_originalWndProc.exchange(currentWndProc, std::memory_order_acq_rel);
        SetLastError(0);
        const LONG_PTR previous = SetWindowLongPtr(hostWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ImGuiWndProc));
        const DWORD err = GetLastError();
        if (previous == 0 && err != 0) {
            g_originalWndProc.store(savedOriginal, std::memory_order_release);
            LogOut("[IMGUI] WndProc attach failed (err=" + std::to_string(err) + ")", true);
            return false;
        }
        if (reinterpret_cast<WNDPROC>(previous) != currentWndProc) {
            // A peer subclassed between the read and the write. Put it back on top;
            // calls already inside ImGuiWndProc forward to it, not past it.
            g_originalWndProc.store(reinterpret_cast<WNDPROC>(previous), std::memory_order_release);
            SetWindowLongPtr(hostWindow, GWLP_WNDPROC, previous);
            LogOut("[IMGUI] WndProc attach raced a peer subclass; restored it and left ours detached", true);
            return false;
        }
        g_wndProcAttached.store(true, std::memory_order_release);
        LogOut("[IMGUI] WndProc re-attached after netplay suspend", true);
        return true;
    }

    bool IsInitialized() {
        return g_imguiInitialization.IsReady() && g_d3dDevice;
    }

    bool ShowFallbackWindow() {
        if (g_onlineModeActive.load(std::memory_order_relaxed)) {
            LogOut("[IMGUI] Ignoring fallback window request while netplay suspend is active", true);
            return false;
        }

        if (g_externalFallback.IsReady() && g_externalFallbackHost) {
            if (!g_imguiVisible) {
                SetVisibilityInternal(true);
            } else if (g_externalFallbackWindow && IsWindow(g_externalFallbackWindow)) {
                ShowWindow(g_externalFallbackWindow, SW_SHOWNORMAL);
                SetForegroundWindow(g_externalFallbackWindow);
            }
            return true;
        }

        if (g_externalFallback.TryStart()) {
            try {
                std::thread(RunFallbackWindowThread).detach();
            } catch (const std::exception&) {
                g_externalFallback.Finish(true);
                return false;
            }
        }

        const DWORD start = GetTickCount();
        while (!g_externalFallback.IsReady()
            && !g_externalFallback.HasFailed()
            && (GetTickCount() - start) < 3000) {
            Sleep(10);
        }

        if (!g_externalFallback.IsReady()) {
            LogOut("[IMGUI] Fallback window did not become ready", true);
            return false;
        }

        SetVisibilityInternal(true);
        return true;
    }

    bool IsExternalFallbackHost() {
        return g_externalFallbackHost;
    }

    bool ReleaseExternalFallbackHost(DWORD timeoutMs) {
        if (!g_externalFallbackHost && !g_externalFallback.IsRunning()) return true;
        // The fallback thread shuts ImGui down itself; it cannot wait for its own exit.
        if (g_imguiVisible || g_imguiContextThread == GetCurrentThreadId()) return false;
        if (g_externalFallback.IsRunning()) {
            g_externalFallback.RequestStop();
            const DWORD start = GetTickCount();
            while (g_externalFallback.IsRunning()
                && (GetTickCount() - start) < timeoutMs) {
                Sleep(10);
            }
        }
        return !g_externalFallback.IsRunning()
            && !g_externalFallbackHost;
    }
    
    void ToggleVisibility() {
        if (g_onlineModeActive.load(std::memory_order_relaxed)) {
            LogOut("[IMGUI] Ignoring visibility toggle while netplay suspend is active", true);
            return;
        }

        SetVisibilityInternal(!g_imguiVisible);
    }

    void ForceHide() {
    // Deliberately no netplay early-return: callers close the menu because the world is
    // changing underneath it (netplay suspend, leaving Match, feature teardown).
        if (g_imguiVisible) {
            SetVisibilityInternal(false);
        }
        menuOpen.store(false);
    }
    
    bool IsVisible() {
        return g_imguiVisible;
    }

    unsigned VisibilityGeneration() {
        return g_visibilityGeneration.load(std::memory_order_acquire);
    }
    
    LRESULT WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        DeviceRenderScope input;
        if (!input.Admitted() || !g_imguiInitialization.IsReady() || !TrainingUiInputActive()) return 0;
        ForwardWin32Input(hWnd, msg, wParam, lParam);

        if (g_imguiVisible) {
            ImGuiIO& io = ImGui::GetIO();
            const bool isMouseMsg = (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL;
            const bool isKeyMsg = (msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP || msg == WM_CHAR);
            if ((isMouseMsg && io.WantCaptureMouse) || (isKeyMsg && io.WantCaptureKeyboard)) {
                return 1;
            }
        }
        return 0;
    }
    
    void NewFrameInputs() {
        CustomMenu::PaletteEditorPanel::SetDevice(g_d3dDevice);
        CustomMenu::SyncPalettePreview();
        ImGuiContext& context = *ImGui::GetCurrentContext();
        ImGuiIO& io = context.IO;
        const bool inputActive = TrainingUiInputActive();
        const HWND hwnd = GetActiveHostWindow();
        const bool wantSetMousePos = io.WantSetMousePos && inputActive && GetForegroundWindow() == hwnd;
        if (inputActive || FrameBar::g_enabled.load()) {
            RefreshPointerPresentation();
            if (wantSetMousePos) SetMappedPointer(hwnd, io.MousePos);
            const ImVec2 point = ReadMappedPointer(hwnd);
            io.AddMousePosEvent(point.x, point.y);
        }
        const int firstEvent = context.InputEventsQueue.Size;
        io.WantSetMousePos = false;
        ImGui_ImplWin32_NewFrame();
        io.WantSetMousePos = wantSetMousePos;
        GuiPointer::DiscardBackendMousePositions(context, firstEvent);
    }

    void PreNewFrameInputs() {
        if (!g_imguiInitialization.IsReady() || g_isShuttingDown.load()) return;
        if (!ImGui::GetCurrentContext()) return;
        ImGuiIO& io = ImGui::GetIO();
        io.KeyRepeatDelay = 9999.f;
        io.KeyRepeatRate = 9999.f;
        io.DisplaySize = ImVec2(640.0f, 480.0f);
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
        if (!TrainingUiInputActive()) return;
        UpdateVirtualCursor(io);
    }

    void RenderFrame() {
        DeviceRenderScope render;
        if (!render.Admitted()) return;
        if (!g_imguiInitialization.IsReady() || !g_d3dDevice || g_isShuttingDown.load()) {
            return;
        }

        if (!g_imguiVisible) {
            return;
        }
        
        bool frameStarted = false;
        try {
            const bool useCustomMenu = g_externalFallbackHost || Config::GetSettings().useCustomMenu || CustomMenu::PaletteEditorPanel::IsOpen();

            if (useCustomMenu) {
                CustomMenu::PrepareFrame();
            }

            ImGui_ImplDX9_NewFrame();
            NewFrameInputs();
            // Feed our corrected mouse/gamepad inputs after backend NewFrame, before ImGui::NewFrame.
            PreNewFrameInputs();
            ImGui::NewFrame();
            frameStarted = true;
            // Skip rendering if minimized to avoid style asserts (DisplaySize == 0)
            ImGuiIO& io = ImGui::GetIO();
            if (io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f) {
                ImGui::EndFrame();
                return;
            }
            PauseIntegration::MaintainFreezeWhileMenuVisible();

            if (useCustomMenu) {
                CustomMenu::Render();
            } else {
                UpdateFontAtlasForScale(Config::GetSettings().uiScale);
                ImGuiGui::RenderGui();
            }

            ImGui::EndFrame();
            ImGui::Render();
            if (io.DisplaySize.x > 0.0f && io.DisplaySize.y > 0.0f) {
                ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            }
        } catch (...) {
            if (frameStarted) ImGui::EndFrame();
        }
    }

    void SetOverlayCenter(const ImVec2& center) {
        g_overlayCenter = center;
    }

    bool ConsumeOverlayFocusRequest() {
        bool v = g_requestOverlayFocus;
        g_requestOverlayFocus = false;
        return v;
    }
}
