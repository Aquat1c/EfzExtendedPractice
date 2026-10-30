#include "../include/gui/custom_menu/renderer.h"
#include "../include/gui/custom_menu/anim.h"
#include "../include/gui/custom_menu/fonts.h"
#include "../include/game/practice_menu/mission_title_screen.h" // title screens use our fonts
#include "../include/game/mission/mission_pause_menu.h"         // session pause menu uses them too
#include "../include/game/mission/tutorial_session.h"           // tutorial pages/cards too
#include "../include/gui/custom_menu/theme.h"
#include "../include/gui/custom_menu/layout.h"
#include "../include/gui/custom_menu/scale.h"
#include "../include/gui/custom_menu/input.h"
#include "../include/gui/custom_menu/screens.h"
#include "../include/gui/custom_menu/sound.h"
#include "../include/gui/value_lock_state.h"
#include "../include/utils/update_check.h"
#include "../include/gui/imgui_impl.h"
#include "../include/gui/imgui_gui.h"
#include "../include/core/logger.h"
#include "../include/utils/utilities.h"
#include "../include/utils/config.h"
#include "../include/utils/i18n.h"
#include "../include/core/constants.h"
#include "../include/core/version.h"
#include "../include/game/character_hotswap.h"
#include "../3rdparty/imgui/imgui.h"

#include <atomic>
#include <windows.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <string>

namespace CustomMenu {

namespace {

    // 5 top-level tabs; leaf panes are identified by `Pane` (the Screens Tick* dispatch).
    // Per-pane focus + scroll state lives in ShellState so tabs remember their last row.

enum Pane {
    PANE_VALUES = 0,            // MAIN / VALUES  (2-column P1 | P2 editor)
    PANE_OPPONENT,              // MAIN / OPPONENT
    PANE_OPTIONS,               // MAIN / OPTIONS
    PANE_MENU,                  // MAIN / MENU
    PANE_TRIGGERS,              // AUTO / TRIGGERS
    PANE_MACROS,                // AUTO / MACROS
    PANE_CHARS,                 // CHARS
    PANE_SETTINGS_GENERAL,      // SETTINGS / GENERAL
    PANE_SETTINGS_HOTKEYS,      // SETTINGS / HOTKEYS
    PANE_SETTINGS_DEBUG,        // SETTINGS / DEBUG
    PANE_HELP_START,            // HELP / START
    PANE_HELP_GUIDE,            // HELP / GUIDE
    PANE_HELP_RESOURCES,        // HELP / RESOURCES
    PANE_HELP_ABOUT,            // HELP / ABOUT
    PANE_COUNT
};

enum TopTab {
    TT_MAIN = 0,
    TT_AUTO,
    TT_CHARS,
    TT_SETTINGS,
    TT_HELP,
    TT_COUNT
};

struct SubTab {
    const char* label;
    int pane;   // Pane
};

struct TopTabInfo {
    const char* label;
    const SubTab* subs;
    int subCount;
};

static const SubTab kSubs_Main[]     = { {"MENU",     PANE_MENU},
                                         {"VALUES",   PANE_VALUES},
                                         {"OPPONENT", PANE_OPPONENT},
                                         {"OPTIONS",  PANE_OPTIONS} };
static const SubTab kSubs_Auto[]     = { {"TRIGGERS", PANE_TRIGGERS},
                                         {"MACROS",   PANE_MACROS} };
static const SubTab kSubs_Chars[]    = { {"CHARS",    PANE_CHARS} };
static const SubTab kSubs_Settings[] = { {"GENERAL",  PANE_SETTINGS_GENERAL},
                                         {"HOTKEYS",  PANE_SETTINGS_HOTKEYS},
                                         {"DEBUG",    PANE_SETTINGS_DEBUG} };
static const SubTab kSubs_Help[]     = { {"START",     PANE_HELP_START},
                                         {"GUIDE",     PANE_HELP_GUIDE},
                                         {"RESOURCES", PANE_HELP_RESOURCES},
                                         {"ABOUT",     PANE_HELP_ABOUT} };

static const TopTabInfo kTopTabs[TT_COUNT] = {
    {"MAIN",     kSubs_Main,     4},
    {"AUTO",     kSubs_Auto,     2},
    {"CHARS",    kSubs_Chars,    1},
    {"SETTINGS", kSubs_Settings, 3},
    {"HELP",     kSubs_Help,     4},
};

    // Sub-tab labels are read by both ComputeTabRects() (hit rects) and RenderTabBar()
    // (glyphs); route every read through this one helper so they can never diverge.
    // Only HELP and HELP>ABOUT are badged. Static buffers keep the returned pointer
    // valid for the whole frame. Render thread only.
const char* TopTabLabel(int index) {
    if (index != TT_HELP || !UpdateCheck::IsUpdateAvailable()) {
        return kTopTabs[index].label;
    }
    static char s_badged[48];
    _snprintf_s(s_badged, sizeof(s_badged), _TRUNCATE,
                "%s %s", I18n::Tr(kTopTabs[index].label), UpdateCheck::BadgeText());
    return s_badged;
}

const char* SubTabLabel(const SubTab& sub) {
    if (sub.pane != PANE_HELP_ABOUT || !UpdateCheck::IsUpdateAvailable()) {
        return sub.label;
    }
    static char s_badged[48];
    _snprintf_s(s_badged, sizeof(s_badged), _TRUNCATE,
                "%s %s", I18n::Tr(sub.label), UpdateCheck::BadgeText());
    return s_badged;
}

inline int ClampTopTab(int t) {
    if (t < 0) return 0;
    if (t >= TT_COUNT) return TT_COUNT - 1;
    return t;
}
inline int ClampSubTab(int top, int s) {
    const int n = kTopTabs[ClampTopTab(top)].subCount;
    if (s < 0) return 0;
    if (s >= n) return n - 1;
    return s;
}

const char* PaneName(int pane) {
    for (int t = 0; t < TT_COUNT; ++t) {
        for (int s = 0; s < kTopTabs[t].subCount; ++s) {
            if (kTopTabs[t].subs[s].pane == pane) return kTopTabs[t].subs[s].label;
        }
    }
    return "UNKNOWN";
}

void LogMenuDetail(const char* fmt, ...) {
    if (!detailedLogging.load()) return;

    char buf[512];
    va_list args;
    va_start(args, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, args);
    va_end(args);

    LogOut(std::string("[CUSTOM_MENU] ") + buf, true);
}

    // Always-on [CUSTOM_MENU][TRACE] crash-localization logger; keep messages short.
void LogMenuTrace(const char* fmt, ...) {
    char buf[384];
    va_list args;
    va_start(args, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, args);
    va_end(args);
    LogOut(std::string("[CUSTOM_MENU][TRACE] ") + buf, true);
}

void LogMenuTiming(const char* fmt, ...) {
    char buf[384];
    va_list args;
    va_start(args, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, args);
    va_end(args);
    LogOut(std::string("[CUSTOM_MENU][TIMING] ") + buf, true);
}

void LogMenuSehStep(unsigned code, const char* step) {
    char buf[224];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE,
        "[CUSTOM_MENU][TRACE] SEH 0x%08X during %s",
        code, step ? step : "(unknown step)");
    LogOut(buf, true);
}

void LogMenuSehPane(unsigned code, const char* paneName) {
    char buf[192];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE,
        "[CUSTOM_MENU][TRACE] SEH 0x%08X in Tick%s",
        code, paneName ? paneName : "?");
    LogOut(buf, true);
}

// Free-function SEH wrapper used to bracket each menu-open / per-frame step.
    // __try cannot coexist with non-trivial unwindable locals under MSVC, so helpers are
typedef void (*MenuStepFn)();
static bool SehInvokeStep(const char* step, MenuStepFn fn) {
    __try {
        fn();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LogMenuSehStep((unsigned)GetExceptionCode(), step);
        return false;
    }
}

typedef void (*TickPaneFn)(ImDrawList*, const Screens::ScreenLayout&,
                           int&, Screens::ScrollState&, bool&);
static bool SehTickPane(const char* paneName, TickPaneFn fn,
                        ImDrawList* dl, const Screens::ScreenLayout& sl,
                        int& focus, Screens::ScrollState& scroll, bool& backEdge) {
    __try {
        fn(dl, sl, focus, scroll, backEdge);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LogMenuSehPane((unsigned)GetExceptionCode(), paneName);
        return false;
    }
}

const char* ScreenName(int pane) { return PaneName(pane); }

// ===== Shell / edit / focus state =====
enum class FocusRegion {
    Content = 0,
    SubTabs,
    TopTabs,
};

struct ShellState {
    int  activeTopTab = TT_MAIN;
    int  subIdxPerTop[TT_COUNT] = { 0, 0, 0, 0, 0 };
    int  focusPerPane[PANE_COUNT] = {};
    Screens::ScrollState scrollPerPane[PANE_COUNT];
    FocusRegion focusRegion = FocusRegion::Content;

    bool menuWasVisible = false;
    bool lastWasCustom  = false;
    unsigned lastVisibilityGen = 0;

    bool keybindWasActive = false;
};
ShellState g_shell;

inline int ActivePane() {
    const TopTabInfo& t = kTopTabs[ClampTopTab(g_shell.activeTopTab)];
    const int s = ClampSubTab(g_shell.activeTopTab, g_shell.subIdxPerTop[g_shell.activeTopTab]);
    return t.subs[s].pane;
}

bool SettingsTabActive() {
    return ClampTopTab(g_shell.activeTopTab) == TT_SETTINGS;
}

std::string g_transientStatusText;
DWORD g_transientStatusTick = 0;

bool ActiveTopHasSubTabs() {
    return kTopTabs[ClampTopTab(g_shell.activeTopTab)].subCount > 1;
}

void SetFocusRegion(FocusRegion region, const char* reason) {
    if (g_shell.focusRegion == region) return;
    LogMenuDetail("FocusRegion %d -> %d (%s)",
        static_cast<int>(g_shell.focusRegion),
        static_cast<int>(region),
        reason ? reason : "n/a");
    g_shell.focusRegion = region;
    if (!reason || !strstr(reason, "mouse")) {
        Sound::PlayCursor();
    }
}

void MoveFocusAboveContent(const char* reason) {
    SetFocusRegion(ActiveTopHasSubTabs() ? FocusRegion::SubTabs : FocusRegion::TopTabs,
                   reason ? reason : "content nav up");
}

    // Mirror EFZ's replay-menu submenu slide: accumulate 0..180, cosine for the offset.
struct AnimState {
    DWORD openTick = 0;
    DWORD closeTick = 0;
    DWORD paneTick = 0;
    int   paneDir = 1;
};
AnimState g_anim;

// Linear like efz.exe fadeScreenEffect / performFadeOutAnimation, in less time.
constexpr float kFadeAnimMs = 130.0f;
// About eight frames at 64 fps: a short slide under a fade, a long one would jump.
constexpr float kPaneAnimMs = 130.0f;
constexpr float kPaneSlidePx = 36.0f;
using Anim::AnimT;
using Anim::Clamp01;
using Anim::EfzCosEase;
std::atomic<bool> g_closeRequested{false};
std::atomic<DWORD> g_lastRenderTick{0};
constexpr DWORD kTransientStatusMs = 1800;

void SetTransientStatus(const char* text) {
    g_transientStatusText = text ? text : "";
    g_transientStatusTick = GetTickCount();
}

const char* CurrentTransientStatus() {
    if (g_transientStatusText.empty()) {
        return nullptr;
    }
    if ((GetTickCount() - g_transientStatusTick) > kTransientStatusMs) {
        g_transientStatusText.clear();
        return nullptr;
    }
    return g_transientStatusText.c_str();
}

float FooterBoxHeight() {
    return Scale::Snap(44.0f * Scale::Get().layoutScale);
}

float FooterReserveHeight() {
    return Scale::Snap(FooterBoxHeight() + 12.0f * Scale::Get().layoutScale);
}

float ContentBottomY() {
    return Scale::Snap(Theme::PanelBottomRight().y - FooterReserveHeight());
}

bool SettingsQuickSaveAllowed() {
    return SettingsTabActive()
        && !Screens::IsPopupActive()
        && !Screens::IsKeybindActive()
        && !Screens::IsTextEditorActive();
}

void HandleSettingsQuickSave() {
    if (!SettingsQuickSaveAllowed() || !Input::SwitchPlayer()) {
        return;
    }

    const bool ok = Config::SaveSettings();
    if (ok) {
        Sound::PlayDecision();
        SetTransientStatus("SETTINGS SAVED TO DISK");
        LogOut(std::string("[CUSTOM_MENU] Settings saved to disk via D button from ") + ScreenName(ActivePane()), false);
    } else {
        SetTransientStatus("SAVE FAILED");
        LogOut(std::string("[CUSTOM_MENU] Failed to save settings via D button from ") + ScreenName(ActivePane()), true);
    }
}

void EndCloseAnimation() {
    g_anim.closeTick = 0;
    Input::SetSuspended(false);
}

void BeginCloseAnimation() {
    if (!Config::GetSettings().menuAnimations) {
        ImGuiImpl::ToggleVisibility();
        return;
    }
    if (g_anim.closeTick == 0) {
        g_anim.closeTick = Anim::NowTick();
        Input::SetSuspended(true);
    }
}

void StartOpenAnimation() {
    EndCloseAnimation();
    g_closeRequested.store(false, std::memory_order_relaxed);
    g_anim.openTick = Anim::NowTick();
    g_anim.paneTick = g_anim.openTick;
    g_anim.paneDir = 1;
}

void StartPaneAnimation(int dir) {
    g_anim.paneTick = Anim::NowTick();
    g_anim.paneDir = (dir < 0) ? -1 : 1;
}

float CurrentPaneOffsetX() {
    const float ease = EfzCosEase(AnimT(g_anim.paneTick, kPaneAnimMs));
    return Scale::Snap((1.0f - ease) * kPaneSlidePx * static_cast<float>(g_anim.paneDir));
}

float CurrentFadeAlpha() {
    return g_anim.closeTick != 0 ? 1.0f - AnimT(g_anim.closeTick, kFadeAnimMs)
                                 : AnimT(g_anim.openTick, kFadeAnimMs);
}

// Scales the alpha of everything drawn in its lifetime.
struct MenuFadeScope {
    ImDrawList* dl = nullptr;
    int firstVertex = 0;
    float alpha = 1.0f;

    explicit MenuFadeScope(ImDrawList* list) : dl(list), alpha(CurrentFadeAlpha()) {
        if (dl) firstVertex = dl->VtxBuffer.Size;
    }

    ~MenuFadeScope() { Anim::ScaleVertexAlpha(dl, firstVertex, alpha); }
};

enum class MainMode {
    Browse = 0,
    Adjust
};

struct MainState {
    int row = 0;
    int player = 0; // 0=P1, 1=P2
    MainMode mode = MainMode::Browse;
};
MainState g_main;

constexpr int kCrColRowCount = Screens::CrEditorRowCount;

const char* MainModeName(MainMode mode) {
    switch (mode) {
        case MainMode::Browse: return "BROWSE";
        case MainMode::Adjust: return "ADJUST";
    }
    return "UNKNOWN";
}

struct CrMainState {
    int row = 0; // visible-row index (compact layout)
    int player = 0;
    MainMode mode = MainMode::Browse;
};
CrMainState g_cr;

int g_crVisiblePhysical[kCrColRowCount];
int g_crVisibleCount = 0;

void CrRebuildVisibleRows() {
    g_crVisibleCount = 0;
    for (int i = 0; i < kCrColRowCount; ++i) {
        if (!Screens::CrRowHidden(0, i) || !Screens::CrRowHidden(1, i)) {
            g_crVisiblePhysical[g_crVisibleCount++] = i;
        }
    }
    if (g_crVisibleCount == 0) {
        g_crVisiblePhysical[g_crVisibleCount++] = 0;
    }
    if (g_cr.row >= g_crVisibleCount) {
        g_cr.row = g_crVisibleCount - 1;
    }
}

int CrPhysicalRow(int visRow) {
    if (visRow < 0) visRow = 0;
    if (visRow >= g_crVisibleCount) visRow = g_crVisibleCount - 1;
    return g_crVisiblePhysical[visRow];
}

int CrP2FocusStart() {
    return g_crVisibleCount;
}

void SetCrMode(MainMode mode, const char* reason) {
    if (g_cr.mode == mode) return;
    g_cr.mode = mode;
    LogMenuDetail("CR mode -> %s (%s)", MainModeName(mode), reason ? reason : "?");
}

struct EditState {
    bool active = false;
    int  rowIdx = -1;
    char buf[16] = "";
    int  caret = 0;
};
EditState g_edit;

    // Hover counts as focus only when the mouse actually moved;
    // a stationary cursor must not override keyboard selection.
struct MouseState {
    float lastX = -1.0f;
    float lastY = -1.0f;
    bool  movedThisFrame = false;
};
MouseState g_mouse;

void UpdateMouseState() {
    auto m = Input::GetMouse();
    g_mouse.movedThisFrame = false;
    if (!m.valid) { return; }
    if (g_mouse.lastX < 0.0f && g_mouse.lastY < 0.0f) {
        g_mouse.lastX = m.x; g_mouse.lastY = m.y;
        return;
    }
    const float dx = m.x - g_mouse.lastX;
    const float dy = m.y - g_mouse.lastY;
    if ((dx * dx + dy * dy) > 1.0f) {
        g_mouse.movedThisFrame = true;
        g_mouse.lastX = m.x;
        g_mouse.lastY = m.y;
    }
}

bool ShiftHeld() {
    if (!Input::IsGameWindowActive()) return false;
    return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
}

std::string FitTextToWidth(ImFont* font, float px, const char* text, float maxW) {
    if (!text || !*text || maxW <= 0.0f) return "";
    if (Layout::MeasureTextW(font, px, text) <= maxW) return text;

    std::string out = text;
    const char* suffix = "...";
    while (!out.empty()) {
        // Drop one whole UTF-8 code point.
        while (!out.empty()) {
            const unsigned char c = static_cast<unsigned char>(out.back());
            out.pop_back();
            if ((c & 0xC0) != 0x80) break;
        }
        std::string candidate = out + suffix;
        if (Layout::MeasureTextW(font, px, candidate.c_str()) <= maxW) {
            return candidate;
        }
    }
    return suffix;
}

// ===== Values rows descriptor =====
struct ValueRow {
    const char* label;
    int   (*getInt)(const DisplayData&);
    void  (*setInt)(DisplayData&, int);
    double(*getDbl)(const DisplayData&);
    void  (*setDbl)(DisplayData&, double);
    int    minI, maxI;
    double minD, maxD;
    int    stepSmall, stepBig;
    double stepSmallD, stepBigD;
    bool   isDouble;
    const char* fmt; // printf format for formatting only; nullptr = default
};

int  GetHp1(const DisplayData& d) { return d.hp1; }
int  GetHp2(const DisplayData& d) { return d.hp2; }
int  GetMt1(const DisplayData& d) { return d.meter1; }
int  GetMt2(const DisplayData& d) { return d.meter2; }
double GetRf1(const DisplayData& d) { return d.rf1; }
double GetRf2(const DisplayData& d) { return d.rf2; }
double GetX1(const DisplayData& d) { return d.x1; }
double GetY1(const DisplayData& d) { return d.y1; }
double GetX2(const DisplayData& d) { return d.x2; }
double GetY2(const DisplayData& d) { return d.y2; }

void SetHp1(DisplayData& d, int v) { d.hp1 = v; }
void SetHp2(DisplayData& d, int v) { d.hp2 = v; }
void SetMt1(DisplayData& d, int v) { d.meter1 = v; }
void SetMt2(DisplayData& d, int v) { d.meter2 = v; }
void SetRf1(DisplayData& d, double v) { d.rf1 = v; }
void SetRf2(DisplayData& d, double v) { d.rf2 = v; }
void SetX1(DisplayData& d, double v) { d.x1 = v; }
void SetY1(DisplayData& d, double v) { d.y1 = v; }
void SetX2(DisplayData& d, double v) { d.x2 = v; }
void SetY2(DisplayData& d, double v) { d.y2 = v; }

// Two-column Values layout.
//   Left column (PLAYER 1 VALUES):  focus 0..5  = HP, METER, RF, X, Y, IC
//   Right column (PLAYER 2 VALUES): focus 6..11 = HP, METER, RF, X, Y, IC
// Both columns share the same 6 row Y positions so rows visually align across.
constexpr int kP1NumericCount = 5;
constexpr int kICColorP1Idx   = 5;
constexpr int kP1Total        = 6;   // 5 numeric + 1 IC color
constexpr int kP2Start        = 6;
constexpr int kP2NumericCount = 5;
constexpr int kICColorP2Idx   = 11;
constexpr int kP2Total        = 6;
constexpr int kColRowCount    = 6;   // rows per column
constexpr int kDataRowCount   = 12;

ValueRow g_valueRowsAll[12] = {
    {"HP",    GetHp1, SetHp1, nullptr, nullptr, 0, MAX_HP,     0.0, 0.0,      50,  500, 0.0,  0.0,   false, nullptr},
    {"METER", GetMt1, SetMt1, nullptr, nullptr, 0, MAX_METER,  0.0, 0.0,      50,  500, 0.0,  0.0,   false, nullptr},
    {"RF",    nullptr, nullptr, GetRf1, SetRf1, 0, 0,          0.0, (double)MAX_RF, 0, 0,   10.0, 100.0, true,  "%.0f"},
    {"X",     nullptr, nullptr, GetX1,  SetX1,  0, 0,          -2000.0, 2000.0, 0, 0, 1.0, 10.0,  true,  "%.1f"},
    {"Y",     nullptr, nullptr, GetY1,  SetY1,  0, 0,          -2000.0, 2000.0, 0, 0, 1.0, 10.0,  true,  "%.1f"},
    {"IC",    nullptr, nullptr, nullptr, nullptr, 0, 0,        0.0, 0.0,      0, 0, 0.0, 0.0,   false, nullptr},
    {"HP",    GetHp2, SetHp2, nullptr, nullptr, 0, MAX_HP,     0.0, 0.0,      50,  500, 0.0,  0.0,   false, nullptr},
    {"METER", GetMt2, SetMt2, nullptr, nullptr, 0, MAX_METER,  0.0, 0.0,      50,  500, 0.0,  0.0,   false, nullptr},
    {"RF",    nullptr, nullptr, GetRf2, SetRf2, 0, 0,          0.0, (double)MAX_RF, 0, 0,   10.0, 100.0, true,  "%.0f"},
    {"X",     nullptr, nullptr, GetX2,  SetX2,  0, 0,          -2000.0, 2000.0, 0, 0, 1.0, 10.0,  true,  "%.1f"},
    {"Y",     nullptr, nullptr, GetY2,  SetY2,  0, 0,          -2000.0, 2000.0, 0, 0, 1.0, 10.0,  true,  "%.1f"},
    {"IC",    nullptr, nullptr, nullptr, nullptr, 0, 0,        0.0, 0.0,      0, 0, 0.0, 0.0,   false, nullptr},
};

inline bool RowIsICColor(int idx) { return idx == kICColorP1Idx || idx == kICColorP2Idx; }
inline bool RowIsNumeric(int idx) { return idx >= 0 && idx < kDataRowCount && !RowIsICColor(idx); }
inline bool RowIsPosition(int idx) {
    const int row = (idx >= kP2Start) ? (idx - kP2Start) : idx;
    return row == 3 || row == 4;
}
inline int RowPlayer(int idx) { return (idx >= kP2Start) ? 2 : 1; }
const char* ICColorName(bool blue) { return blue ? "BLUE" : "RED"; }
std::string DescribeFocus(int screen, int focus);

bool RowIsLocked(const GuiValueLocks::State& locks, int rowIdx) {
    if (rowIdx < 0 || rowIdx >= kDataRowCount) return false;
    if (RowIsPosition(rowIdx)) return false;
    if (locks.globalValuesLocked) return true;
    if (RowIsICColor(rowIdx)) {
        return (RowPlayer(rowIdx) == 1) ? locks.p1IcManaged : locks.p2IcManaged;
    }
    return false;
}

const char* RowLockReason(const GuiValueLocks::State& locks, int rowIdx) {
    if (!RowIsLocked(locks, rowIdx)) return "Unlocked";
    if (locks.globalValuesLocked) return GuiValueLocks::DescribeGlobalReason(locks.globalReason);
    if (RowIsICColor(rowIdx)) return GuiValueLocks::DescribeIcManagedReason(locks, RowPlayer(rowIdx));
    return "Locked";
}

std::string LockStatusText(const GuiValueLocks::State& locks, int rowIdx) {
    if (locks.globalValuesLocked) {
        switch (locks.globalReason) {
            case GuiValueLocks::GlobalReason::EngineF4:
                return "RF RECOVERY (F4) ACTIVE - HP/METER/RF/IC LOCKED";
            case GuiValueLocks::GlobalReason::EngineF5:
                return "AUTOMATIC RECOVERY (F5) ACTIVE - HP/METER/RF/IC LOCKED";
            case GuiValueLocks::GlobalReason::ContinuousRecovery:
                return "CONTINUOUS RECOVERY ACTIVE - HP/METER/RF/IC LOCKED";
            case GuiValueLocks::GlobalReason::PendingF4Ui:
                return "RF RECOVERY PENDING - HP/METER/RF/IC LOCKED";
            case GuiValueLocks::GlobalReason::None:
                break;
        }
    }

    if (RowIsICColor(rowIdx) && RowIsLocked(locks, rowIdx)) {
        return I18n::TrFormat("%s IC LOCKED - MANAGED BY %s",
                              RowPlayer(rowIdx) == 1 ? "P1" : "P2",
                              I18n::Tr(RowLockReason(locks, rowIdx)));
    }

    return "";
}

void LogLockedAction(const char* action, const GuiValueLocks::State& locks, int rowIdx) {
    if (!RowIsLocked(locks, rowIdx)) return;
    LogMenuDetail("%s blocked %s (%s)",
        action ? action : "Action",
        DescribeFocus(PANE_VALUES,rowIdx).c_str(),
        RowLockReason(locks, rowIdx));
}

inline bool FocusInLeftCol(int f) { return f >= 0 && f < kP2Start; }
inline int  FocusColRow(int f)    { return FocusInLeftCol(f) ? f : (f - kP2Start); }
inline int  MainTotalFocusItems() { return kDataRowCount; }

int ComposeFocus(int row, int player) {
    if (row < 0) row = 0;
    if (row >= kColRowCount) row = kColRowCount - 1;
    if (player < 0) player = 0;
    if (player > 1) player = 1;
    return (player == 0) ? row : (kP2Start + row);
}

void DecodeFocus(int focus, int& row, int& player) {
    if (focus < 0) focus = 0;
    if (focus >= kDataRowCount) focus = kDataRowCount - 1;
    player = (focus >= kP2Start) ? 1 : 0;
    row = (player == 0) ? focus : (focus - kP2Start);
}

int CurFocus() {
    return ComposeFocus(g_main.row, g_main.player);
}

std::string DescribeFocus(int pane, int focus) {
    if (pane != PANE_VALUES) {
        return std::string("index=") + std::to_string(focus);
    }
    if (focus < 0 || focus >= kDataRowCount) {
        return "none";
    }

    int row = 0;
    int player = 0;
    DecodeFocus(focus, row, player);
    const char* side = (player == 0) ? "P1" : "P2";
    return std::string(side) + " " + g_valueRowsAll[focus].label;
}

void SetMainMode(MainMode newMode, const char* reason) {
    if (g_main.mode == newMode) return;

    LogMenuDetail("Mode %s -> %s (%s) focus=%s",
        MainModeName(g_main.mode),
        MainModeName(newMode),
        reason ? reason : "n/a",
        DescribeFocus(PANE_VALUES, CurFocus()).c_str());
    g_main.mode = newMode;
}

void ResetMainState(const char* reason) {
    g_main.row = 0;
    g_main.player = 0;
    g_main.mode = MainMode::Browse;
    g_shell.focusRegion = FocusRegion::Content;
    LogMenuDetail("Main reset (%s) focus=%s mode=%s",
        reason ? reason : "n/a",
        DescribeFocus(PANE_VALUES, CurFocus()).c_str(),
        MainModeName(g_main.mode));
}

void SetActiveTopTab(int newTop, const char* reason) {
    newTop = ClampTopTab(newTop);
    if (g_shell.activeTopTab == newTop) return;

    const int oldTop = g_shell.activeTopTab;
    SetMainMode(MainMode::Browse, "top-tab change");
    LogMenuDetail("TopTab %s -> %s (%s)",
        kTopTabs[g_shell.activeTopTab].label,
        kTopTabs[newTop].label,
        reason ? reason : "n/a");
    Screens::ResetSubmenus();
    g_shell.activeTopTab = newTop;
    if (g_shell.focusRegion == FocusRegion::SubTabs && !ActiveTopHasSubTabs()) {
        g_shell.focusRegion = FocusRegion::TopTabs;
    }
    Sound::PlayCursor();
    StartPaneAnimation((newTop >= oldTop) ? 1 : -1);
}

void SetActiveSubTab(int top, int newSub, const char* reason) {
    top = ClampTopTab(top);
    newSub = ClampSubTab(top, newSub);
    if (g_shell.subIdxPerTop[top] == newSub) return;
    const int oldSub = g_shell.subIdxPerTop[top];
    SetMainMode(MainMode::Browse, "sub-tab change");
    LogMenuDetail("SubTab %s/%s -> %s/%s (%s)",
        kTopTabs[top].label,
        kTopTabs[top].subs[g_shell.subIdxPerTop[top]].label,
        kTopTabs[top].label,
        kTopTabs[top].subs[newSub].label,
        reason ? reason : "n/a");
    Screens::ResetSubmenus();
    g_shell.subIdxPerTop[top] = newSub;
    Sound::PlayCursor();
    StartPaneAnimation((newSub >= oldSub) ? 1 : -1);
}

void SetFocus(int newFocus, const char* reason) {
    if (ActivePane() != PANE_VALUES) return;
    if (newFocus < 0 || newFocus >= MainTotalFocusItems()) return;

    const int focus = CurFocus();
    if (focus == newFocus) return;

    LogMenuDetail("Focus %s -> %s (%s)",
        DescribeFocus(PANE_VALUES, focus).c_str(),
        DescribeFocus(PANE_VALUES, newFocus).c_str(),
        reason ? reason : "n/a");

    int row = 0;
    int player = 0;
    DecodeFocus(newFocus, row, player);
    g_main.row = row;
    g_main.player = player;
    if (!reason || !strstr(reason, "mouse")) {
        Sound::PlayCursor();
    }
}

void FormatNumericRow(const ValueRow& r, char* buf, size_t bufSz) {
    const DisplayData& d = ImGuiGui::guiState.localData;
    const char* fmt = r.fmt ? r.fmt : (r.isDouble ? "%.0f" : "%d");
    if (r.isDouble) {
        _snprintf_s(buf, bufSz, _TRUNCATE, fmt, r.getDbl(d));
    } else {
        _snprintf_s(buf, bufSz, _TRUNCATE, fmt, r.getInt(d));
    }
}

std::string FormatRowValueForLog(int rowIdx) {
    if (RowIsICColor(rowIdx)) {
        const bool blue = (rowIdx == kICColorP1Idx)
            ? ImGuiGui::guiState.localData.p1BlueIC
            : ImGuiGui::guiState.localData.p2BlueIC;
        return ICColorName(blue);
    }
    if (!RowIsNumeric(rowIdx)) {
        return "<n/a>";
    }

    char buf[32];
    FormatNumericRow(g_valueRowsAll[rowIdx], buf, sizeof(buf));
    return buf;
}

void LogRowValueChange(int rowIdx, const char* reason) {
    if (rowIdx < 0 || rowIdx >= kDataRowCount) return;
    LogMenuDetail("%s %s = %s",
        reason ? reason : "Row change",
        DescribeFocus(PANE_VALUES,rowIdx).c_str(),
        FormatRowValueForLog(rowIdx).c_str());
}

void AdjustNumericRow(int rowIdx, int sign, bool forceBig = false) {
    if (!RowIsNumeric(rowIdx)) return;
    const ValueRow& r = g_valueRowsAll[rowIdx];
    DisplayData& d = ImGuiGui::guiState.localData;

    if (r.isDouble) {
        double v = r.getDbl(d);
        const double step = (forceBig || ShiftHeld()) ? r.stepBigD : r.stepSmallD;
        v += sign * step;
        if (v < r.minD) v = r.minD;
        if (v > r.maxD) v = r.maxD;
        r.setDbl(d, v);
    } else {
        int v = r.getInt(d);
        const int step = (forceBig || ShiftHeld()) ? r.stepBig : r.stepSmall;
        v += sign * step;
        if (v < r.minI) v = r.minI;
        if (v > r.maxI) v = r.maxI;
        r.setInt(d, v);
    }
}

void EnterEditMode(int rowIdx) {
    if (!RowIsNumeric(rowIdx)) return;
    g_edit.active = true;
    g_edit.rowIdx = rowIdx;
    FormatNumericRow(g_valueRowsAll[rowIdx], g_edit.buf, sizeof(g_edit.buf));
    g_edit.caret = (int)strlen(g_edit.buf);
    LogMenuDetail("Edit begin %s value=%s",
        DescribeFocus(PANE_VALUES,rowIdx).c_str(),
        g_edit.buf);
}

void CancelEditMode() {
    if (g_edit.active) {
        LogMenuDetail("Edit cancel %s buffer=%s",
            DescribeFocus(PANE_VALUES,g_edit.rowIdx).c_str(),
            g_edit.buf);
    }
    g_edit.active = false;
    g_edit.rowIdx = -1;
    g_edit.buf[0] = '\0';
    g_edit.caret = 0;
    SetMainMode(MainMode::Browse, "edit cancel");
    SetCrMode(MainMode::Browse, "edit cancel");
}

void CommitEditMode() {
    if (!g_edit.active) return;
    const int rowIdx = g_edit.rowIdx;
    if (!RowIsNumeric(rowIdx)) { CancelEditMode(); return; }

    const ValueRow& r = g_valueRowsAll[rowIdx];
    DisplayData& d = ImGuiGui::guiState.localData;

    if (g_edit.buf[0] != '\0') {
        if (r.isDouble) {
            double v = atof(g_edit.buf);
            if (v < r.minD) v = r.minD;
            if (v > r.maxD) v = r.maxD;
            r.setDbl(d, v);
        } else {
            long v = strtol(g_edit.buf, nullptr, 10);
            if (v < r.minI) v = r.minI;
            if (v > r.maxI) v = r.maxI;
            r.setInt(d, (int)v);
        }
    }
    LogMenuDetail("Edit commit %s buffer=%s final=%s",
        DescribeFocus(PANE_VALUES,rowIdx).c_str(),
        g_edit.buf,
        FormatRowValueForLog(rowIdx).c_str());
    g_edit.active = false;
    g_edit.rowIdx = -1;
    g_edit.buf[0] = '\0';
    g_edit.caret = 0;
    SetMainMode(MainMode::Browse, "edit commit");
}

void ProcessEditTextInput() {
    if (!g_edit.active) return;
    ImGuiIO& io = ImGui::GetIO();
    const ValueRow& r = g_valueRowsAll[g_edit.rowIdx];
    const bool allowDot   = r.isDouble;
    const bool allowMinus = r.isDouble ? (r.minD < 0.0) : (r.minI < 0);

    for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
        const ImWchar c = io.InputQueueCharacters[i];
        const bool isDigit = (c >= '0' && c <= '9');
        const bool isDot   = (c == '.') && allowDot && (strchr(g_edit.buf, '.') == nullptr);
        const bool isMinus = (c == '-') && allowMinus && g_edit.caret == 0
                             && (strchr(g_edit.buf, '-') == nullptr);
        if (!isDigit && !isDot && !isMinus) continue;

        const int len = (int)strlen(g_edit.buf);
        if (len >= (int)sizeof(g_edit.buf) - 1) continue;

        for (int k = len; k >= g_edit.caret; --k) g_edit.buf[k + 1] = g_edit.buf[k];
        g_edit.buf[g_edit.caret] = (char)c;
        g_edit.caret++;
    }
    io.InputQueueCharacters.resize(0);
}
    // Layout rectangles - computed once per render so input and draw agree.
struct MainLayout {
    ImVec2 panelTL;
    float  contentX;
    float  contentW;
    float  tabBarY;
    float  tabBarH;
    float  subBarY;                  // Y of the sub-tab strip (0 if none)
    float  subBarH;
    float  dataStartY;               // Y where the column headers begin
    float  rowY[kColRowCount];       // shared Y positions for the 6 rows (both columns)
    float  colLeftX;                 // left edge of P1 column
    float  colRightX;                // left edge of P2 column
    float  colW;                     // width of each column
    float  tabRectX[TT_COUNT];
    float  tabRectY;
    float  tabRectW[TT_COUNT];
    float  tabRectH;
    float  subRectX[8];
    float  subRectW[8];
    float  subRectY;
    float  subRectH;
    int    subRectCount;
};

void ApplyContentAnimation(MainLayout& L) {
    const float x = CurrentPaneOffsetX();
    L.colLeftX += x;
    L.colRightX += x;
}

MainLayout ComputeMainLayout(float contentTopY) {
    using namespace Theme;
    const Scale::Metrics& metrics = Scale::Get();
    MainLayout L{};
    L.panelTL   = PanelTopLeft();
    L.contentX  = L.panelTL.x;
    L.contentW  = kPanelW;
    L.tabBarY   = Scale::Snap(contentTopY);
    L.tabBarH   = metrics.tabBarHeight;

    const float colGap = Scale::Snap(14.0f * metrics.layoutScale);
    L.colW      = Scale::Snap((kPanelW - metrics.panelPadX * 2.0f - colGap) * 0.5f);
    L.colLeftX  = Scale::Snap(L.panelTL.x + metrics.panelPadX);
    L.colRightX = Scale::Snap(L.colLeftX + L.colW + colGap);

    // Sub-tab strip sits below the top bar when the active top tab has subs.
    float y = Scale::Snap(contentTopY + L.tabBarH + 2.0f);
    const int top = ClampTopTab(g_shell.activeTopTab);
    if (kTopTabs[top].subCount > 1) {
        L.subBarY = y;
        L.subBarH = metrics.tabBarHeight - 4.0f;
        y += L.subBarH + 4.0f;
    } else {
        L.subBarY = 0.0f;
        L.subBarH = 0.0f;
    }

    L.dataStartY = Scale::Snap(y);

    // Column headers share a Y; only VALUES uses rowY[] (other panes use ScreenLayout).
    y = Scale::Snap(L.dataStartY + metrics.rowHeight + metrics.sectionPadY);
    for (int i = 0; i < kColRowCount; ++i) {
        L.rowY[i] = Scale::Snap(y);
        y += metrics.rowHeight;
    }

    return L;
}

void ComputeTabRects(MainLayout& L) {
    using namespace Theme;
    const Scale::Metrics& metrics = Scale::Get();
    ImFont* bFont = Layout::BodyFont();
    const float bPx = bFont ? bFont->FontSize : 13.0f;

    float totalW = 0.0f;
    float widths[TT_COUNT];
    for (int i = 0; i < TT_COUNT; ++i) {
        widths[i] = Layout::MeasureTextW(bFont, bPx, TopTabLabel(i));
        totalW += widths[i];
        if (i + 1 < TT_COUNT) totalW += metrics.tabGapX;
    }
    float x = Scale::Snap(L.panelTL.x + (kPanelW - totalW) * 0.5f);
    if (x < L.panelTL.x + metrics.panelPadX) x = L.panelTL.x + metrics.panelPadX;
    const float textY = Scale::Snap(L.tabBarY + (metrics.tabBarHeight - bPx) * 0.5f);
    L.tabRectY = Scale::Snap(textY - 2.0f);
    L.tabRectH = Scale::Snap(bPx + 6.0f);
    for (int i = 0; i < TT_COUNT; ++i) {
        L.tabRectX[i] = Scale::Snap(x);
        L.tabRectW[i] = widths[i];
        x += widths[i] + metrics.tabGapX;
    }

    L.subRectCount = 0;
    if (L.subBarH <= 0.0f) return;
    const int top = ClampTopTab(g_shell.activeTopTab);
    const TopTabInfo& tt = kTopTabs[top];
    L.subRectCount = tt.subCount;
    float subTotalW = 0.0f;
    float subWidths[8];
    for (int i = 0; i < tt.subCount && i < 8; ++i) {
        subWidths[i] = Layout::MeasureTextW(bFont, bPx, SubTabLabel(tt.subs[i]));
        subTotalW += subWidths[i];
        if (i + 1 < tt.subCount) subTotalW += metrics.tabGapX;
    }
    float sx = Scale::Snap(L.panelTL.x + (kPanelW - subTotalW) * 0.5f);
    if (sx < L.panelTL.x + metrics.panelPadX) sx = L.panelTL.x + metrics.panelPadX;
    const float subTextY = Scale::Snap(L.subBarY + (L.subBarH - bPx) * 0.5f);
    L.subRectY = Scale::Snap(subTextY - 2.0f);
    L.subRectH = Scale::Snap(bPx + 6.0f);
    for (int i = 0; i < tt.subCount && i < 8; ++i) {
        L.subRectX[i] = Scale::Snap(sx);
        L.subRectW[i] = subWidths[i];
        sx += subWidths[i] + metrics.tabGapX;
    }
}

void SetActiveTopTab(int newTop, const char* reason);
void SetActiveSubTab(int top, int newSub, const char* reason);

bool PaneToTabs(int pane, int& top, int& sub) {
    for (int t = 0; t < TT_COUNT; ++t) {
        for (int s = 0; s < kTopTabs[t].subCount; ++s) {
            if (kTopTabs[t].subs[s].pane == pane) {
                top = t;
                sub = s;
                return true;
            }
        }
    }
    return false;
}

void ApplyPendingMenuNavigation() {
    Screens::MenuNavigationRequest req{};
    if (!Screens::ConsumeMenuNavigation(req)) return;

    int top = 0;
    int sub = 0;
    if (!PaneToTabs(req.pane, top, sub)) return;

    CancelEditMode();
    SetActiveTopTab(top, "menu shortcut");
    SetActiveSubTab(top, sub, "menu shortcut");
    ResetMainState("menu shortcut");

    if (req.pane >= 0 && req.pane < PANE_COUNT) {
        g_shell.focusPerPane[req.pane] = (req.focusRow < 0) ? 0 : req.focusRow;
        g_shell.scrollPerPane[req.pane] = Screens::ScrollState{};
    }
    SetFocusRegion(FocusRegion::Content, "menu shortcut");

    if (req.submenuBuilder) {
        Screens::OpenSubmenuDirect(req.submenuBuilder, req.submenuTitle, req.submenuFocusRow);
    }
    Input::ResetEdges();
}

bool HandleTabBarClick(const MainLayout& L) {
    if (Screens::IsPopupActive() || Screens::IsKeybindActive() || Screens::IsTextEditorActive()) return false;
    if (!Input::MouseLeftEdge()) return false;

    // Top tabs
    for (int i = 0; i < TT_COUNT; ++i) {
        if (!Input::MouseHovering(L.tabRectX[i] - 2.0f, L.tabRectY,
                                  L.tabRectW[i] + 4.0f, L.tabRectH)) {
            continue;
        }
        if (g_shell.activeTopTab != i) {
            CancelEditMode();
            SetActiveTopTab(i, "top tab click");
        } else {
            LogMenuDetail("Top tab click %s (already active)", kTopTabs[i].label);
        }
        return true;
    }

    for (int i = 0; i < L.subRectCount; ++i) {
        if (!Input::MouseHovering(L.subRectX[i] - 2.0f, L.subRectY,
                                  L.subRectW[i] + 4.0f, L.subRectH)) {
            continue;
        }
        const int top = ClampTopTab(g_shell.activeTopTab);
        if (g_shell.subIdxPerTop[top] != i) {
            CancelEditMode();
            SetActiveSubTab(top, i, "sub tab click");
        }
        return true;
    }

    return false;
}

void MaybeMouseReturnFocusToContent(const MainLayout& L, bool tabClickConsumed) {
    if (g_shell.focusRegion == FocusRegion::Content) return;
    if (tabClickConsumed) return;
    if (!g_mouse.movedThisFrame && !Input::MouseLeftEdge()) return;
    const bool keyboardOrPadEdge = Input::NavUp() || Input::NavDown() ||
                                   Input::NavLeft() || Input::NavRight() ||
                                   Input::Activate() || Input::Back() ||
                                   Input::SwitchPlayer() ||
                                   Input::TopTabPrev() || Input::TopTabNext() ||
                                   Input::SubTabPrev() || Input::SubTabNext();
    if (keyboardOrPadEdge) return;

    const auto m = Input::GetMouse();
    if (!m.valid) return;
    if (m.y >= L.dataStartY && m.y <= ContentBottomY()) {
        SetFocusRegion(FocusRegion::Content, "mouse over content");
    }
}

bool HandleFocusedTabInput() {
    if (Screens::IsPopupActive() || Screens::IsKeybindActive() ||
        Screens::IsTextEditorActive()) {
        return false;
    }
    if (g_shell.focusRegion == FocusRegion::Content) return false;

    const bool navUp = Input::NavUp();
    const bool navDown = Input::NavDown();
    const bool navLeft = Input::NavLeft();
    const bool navRight = Input::NavRight();
    const bool activate = Input::Activate();
    const bool back = Input::Back();
    const bool any = navUp || navDown || navLeft || navRight || activate || back;
    if (!any) return false;

    if (back) {
        LogMenuDetail("Back pressed -> close from tab focus");
        BeginCloseAnimation();
        return true;
    }

    if (g_shell.focusRegion == FocusRegion::TopTabs) {
        if (navLeft) {
            SetActiveTopTab((g_shell.activeTopTab + TT_COUNT - 1) % TT_COUNT, "top tab focus left");
        } else if (navRight) {
            SetActiveTopTab((g_shell.activeTopTab + 1) % TT_COUNT, "top tab focus right");
        } else if (activate) {
            SetFocusRegion(FocusRegion::Content, "top tab activate");
        } else if (navDown) {
            SetFocusRegion(ActiveTopHasSubTabs() ? FocusRegion::SubTabs : FocusRegion::Content,
                           "top tab focus down");
        }
        return true;
    }

    if (g_shell.focusRegion == FocusRegion::SubTabs) {
        const int top = ClampTopTab(g_shell.activeTopTab);
        const int subCount = kTopTabs[top].subCount;
        if (navLeft && subCount > 1) {
            SetActiveSubTab(top, (g_shell.subIdxPerTop[top] + subCount - 1) % subCount,
                            "sub tab focus left");
        } else if (navRight && subCount > 1) {
            SetActiveSubTab(top, (g_shell.subIdxPerTop[top] + 1) % subCount,
                            "sub tab focus right");
        } else if (navUp) {
            SetFocusRegion(FocusRegion::TopTabs, "sub tab focus up");
        } else if (navDown || activate) {
            SetFocusRegion(FocusRegion::Content,
                           navDown ? "sub tab focus down" : "sub tab activate");
        }
        return true;
    }

    return false;
}

void RenderValueRow(ImDrawList* dl, int rowIdx, float x, float y, float w, bool focused, bool disabled) {
    using namespace Theme;

    if (RowIsICColor(rowIdx)) {
        const bool blue = (rowIdx == kICColorP1Idx)
            ? ImGuiGui::guiState.localData.p1BlueIC
            : ImGuiGui::guiState.localData.p2BlueIC;
        const char* choices[2] = { "RED", "BLUE" };
        Layout::DrawRowInlineChoices(
            dl, x, y, w,
            g_valueRowsAll[rowIdx].label,
            choices, 2, blue ? 1 : 0,
            focused, disabled);
        return;
    }

    char buf[32];
    if (g_edit.active && g_edit.rowIdx == rowIdx) {
        const bool caretOn = ((GetTickCount() / 500) & 1) == 0;
        char tmp[24];
        strncpy_s(tmp, sizeof(tmp), g_edit.buf, _TRUNCATE);
        const int len = (int)strlen(tmp);
        if (caretOn && len + 1 < (int)sizeof(tmp)) {
            tmp[len] = '_'; tmp[len + 1] = '\0';
        }
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "[%s]", tmp[0] ? tmp : " ");
    } else {
        FormatNumericRow(g_valueRowsAll[rowIdx], buf, sizeof(buf));
    }
    Layout::DrawRowNumber(
        dl, x, y, w,
        g_valueRowsAll[rowIdx].label, buf,
        focused, disabled);
}

void RenderMainScreen(const MainLayout& L, const GuiValueLocks::State& locks) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (!dl) return;

    using namespace Theme;

    const int focus = CurFocus();
    const Scale::Metrics& metrics = Scale::Get();
    ImFont* bFont = Layout::BodyFont();
    const float bPx = bFont ? bFont->FontSize : 13.0f;
    const float activeRuleY = Scale::Snap(L.dataStartY + metrics.sectionPadY + bPx + 2.0f);

    Layout::DrawHeader(dl, L.colLeftX,  L.dataStartY, L.colW, "PLAYER 1 VALUES");
    Layout::DrawHeader(dl, L.colRightX, L.dataStartY, L.colW, "PLAYER 2 VALUES");
    if (g_main.player == 0) {
        dl->AddLine(
            ImVec2(L.colLeftX + kRuleInsetX - metrics.panelPadX, activeRuleY),
            ImVec2(L.colLeftX + L.colW - kRuleInsetX - metrics.panelPadX, activeRuleY),
            kTextActive, 1.0f);
    } else {
        dl->AddLine(
            ImVec2(L.colRightX + kRuleInsetX - metrics.panelPadX, activeRuleY),
            ImVec2(L.colRightX + L.colW - kRuleInsetX - metrics.panelPadX, activeRuleY),
            kTextActive, 1.0f);
    }

    for (int i = 0; i < kColRowCount; ++i) {
        const int p1RowIdx = i;
        const int p2RowIdx = kP2Start + i;
        RenderValueRow(dl, p1RowIdx, L.colLeftX,  L.rowY[i], L.colW,
                       focus == p1RowIdx, RowIsLocked(locks, p1RowIdx));
        RenderValueRow(dl, p2RowIdx, L.colRightX, L.rowY[i], L.colW,
                       focus == p2RowIdx, RowIsLocked(locks, p2RowIdx));
    }
}

struct DualColumnLayout {
    float colLeftX = 0.0f;
    float colRightX = 0.0f;
    float colW = 0.0f;
    float dataStartY = 0.0f;
    int   visibleCount = 0;
    float rowY[kCrColRowCount]{};
};

int CrComposeFocus(int visRow, int player) {
    if (visRow < 0) visRow = 0;
    if (visRow >= g_crVisibleCount) visRow = g_crVisibleCount - 1;
    if (player < 0) player = 0;
    if (player > 1) player = 1;
    return (player == 0) ? visRow : (CrP2FocusStart() + visRow);
}

void CrDecodeFocus(int focus, int& visRow, int& player) {
    if (focus < 0) focus = 0;
    const int maxFocus = CrP2FocusStart() + g_crVisibleCount - 1;
    if (focus > maxFocus) focus = maxFocus;
    player = (focus >= CrP2FocusStart()) ? 1 : 0;
    visRow = (player == 0) ? focus : (focus - CrP2FocusStart());
}

int CrCurFocus() {
    return CrComposeFocus(g_cr.row, g_cr.player);
}

void CrSetFocus(int newFocus, const char* reason) {
    int row = 0;
    int player = 0;
    CrDecodeFocus(newFocus, row, player);
    if (g_cr.row == row && g_cr.player == player) return;
    g_cr.row = row;
    g_cr.player = player;
    if (!reason || !strstr(reason, "mouse")) {
        Sound::PlayCursor();
    }
}

bool CrRowFocusable(int player, int visRow) {
    const int physical = CrPhysicalRow(visRow);
    return !Screens::CrRowHidden(player, physical);
}

int CrClampFocusIndex(int focus) {
    const int maxFocus = CrP2FocusStart() + g_crVisibleCount - 1;
    for (int i = 0; i <= maxFocus; ++i) {
        const int candidate = (focus + i) % (maxFocus + 1);
        int visRow = 0;
        int player = 0;
        CrDecodeFocus(candidate, visRow, player);
        if (CrRowFocusable(player, visRow)) return candidate;
    }
    return 0;
}

DualColumnLayout ComputeCrLayout(const MainLayout& L) {
    CrRebuildVisibleRows();
    const Scale::Metrics& metrics = Scale::Get();

    DualColumnLayout C{};
    C.colLeftX = L.colLeftX;
    C.colRightX = L.colRightX;
    C.colW = L.colW;
    C.dataStartY = L.dataStartY;
    C.visibleCount = g_crVisibleCount;
    float y = Scale::Snap(L.dataStartY + metrics.rowHeight + metrics.sectionPadY);
    for (int i = 0; i < g_crVisibleCount; ++i) {
        C.rowY[i] = Scale::Snap(y);
        y += metrics.rowHeight;
    }
    return C;
}

void RenderCrScreen(const DualColumnLayout& C) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (!dl) return;

    const int focus = CrCurFocus();

    Layout::DrawHeader(dl, C.colLeftX,  C.dataStartY, C.colW, "PLAYER 1 RECOVERY");
    Layout::DrawHeader(dl, C.colRightX, C.dataStartY, C.colW, "PLAYER 2 RECOVERY");

    for (int vi = 0; vi < C.visibleCount; ++vi) {
        const int physical = CrPhysicalRow(vi);
        const int p1Focus = CrComposeFocus(vi, 0);
        const int p2Focus = CrComposeFocus(vi, 1);
        char buf[48];

        if (!Screens::CrRowHidden(0, physical)) {
            Screens::CrFormatCell(0, physical, buf, sizeof(buf));
            Layout::DrawRowLabelValue(dl, C.colLeftX, C.rowY[vi], C.colW,
                                      Screens::CrRowLabel(physical), buf,
                                      focus == p1Focus, false);
        }
        if (!Screens::CrRowHidden(1, physical)) {
            Screens::CrFormatCell(1, physical, buf, sizeof(buf));
            Layout::DrawRowLabelValue(dl, C.colRightX, C.rowY[vi], C.colW,
                                      Screens::CrRowLabel(physical), buf,
                                      focus == p2Focus, false);
        }
    }
}

void HandleCrScreenInput(const DualColumnLayout& C) {
    const Scale::Metrics& metrics = Scale::Get();
    const bool keyboardOrPadEdge = Input::NavUp() || Input::NavDown() ||
                                   Input::NavLeft() || Input::NavRight() ||
                                   Input::Activate() || Input::Back() ||
                                   Input::SwitchPlayer();

    if (!keyboardOrPadEdge && g_mouse.movedThisFrame) {
        for (int vi = 0; vi < C.visibleCount; ++vi) {
            const int physical = CrPhysicalRow(vi);
            if (!Screens::CrRowHidden(0, physical) &&
                Input::MouseHovering(C.colLeftX, C.rowY[vi], C.colW, metrics.rowHeight)) {
                SetCrMode(MainMode::Browse, "mouse hover");
                CrSetFocus(CrComposeFocus(vi, 0), "mouse hover");
                break;
            }
            if (!Screens::CrRowHidden(1, physical) &&
                Input::MouseHovering(C.colRightX, C.rowY[vi], C.colW, metrics.rowHeight)) {
                SetCrMode(MainMode::Browse, "mouse hover");
                CrSetFocus(CrComposeFocus(vi, 1), "mouse hover");
                break;
            }
        }
    }

    if (!keyboardOrPadEdge && Input::MouseLeftEdge()) {
        for (int vi = 0; vi < C.visibleCount; ++vi) {
            const int physical = CrPhysicalRow(vi);
            int hit = -1;
            if (!Screens::CrRowHidden(0, physical) &&
                Input::MouseHovering(C.colLeftX, C.rowY[vi], C.colW, metrics.rowHeight)) {
                hit = CrComposeFocus(vi, 0);
            } else if (!Screens::CrRowHidden(1, physical) &&
                       Input::MouseHovering(C.colRightX, C.rowY[vi], C.colW, metrics.rowHeight)) {
                hit = CrComposeFocus(vi, 1);
            }
            if (hit < 0) continue;

            int visRow = 0;
            int player = 0;
            CrDecodeFocus(hit, visRow, player);
            if (CrCurFocus() != hit) {
                SetCrMode(MainMode::Browse, "mouse click");
                CrSetFocus(hit, "mouse click");
                return;
            }
            if (!CrRowFocusable(player, visRow)) return;
            SetCrMode(MainMode::Adjust, "mouse click adjust");
            Sound::PlayDecision();
            return;
        }
    }

    int focus = CrClampFocusIndex(CrCurFocus());
    int visRow = 0;
    int player = 0;
    CrDecodeFocus(focus, visRow, player);
    g_cr.row = visRow;
    g_cr.player = player;

    const bool navUp = Input::NavUp();
    const bool navDown = Input::NavDown();
    const bool navLeft = Input::NavLeft();
    const bool navRight = Input::NavRight();
    const bool activate = Input::Activate();
    const bool back = Input::Back();
    const bool switchPlayer = Input::SwitchPlayer();
    const int physical = CrPhysicalRow(g_cr.row);

    if (switchPlayer) {
        CrSetFocus(CrComposeFocus(g_cr.row, 1 - g_cr.player),
            (g_cr.mode == MainMode::Browse) ? "d button switch player" : "d button switch player (adjust)");
        return;
    }

    if (g_cr.mode == MainMode::Browse) {
        if (navUp && g_cr.row > 0) {
            CrSetFocus(CrComposeFocus(g_cr.row - 1, g_cr.player), "browse nav up");
        }
        if (navDown && g_cr.row < C.visibleCount - 1) {
            CrSetFocus(CrComposeFocus(g_cr.row + 1, g_cr.player), "browse nav down");
        }
        if (navLeft && g_cr.player > 0) {
            CrSetFocus(CrComposeFocus(g_cr.row, g_cr.player - 1), "browse player left");
        }
        if (navRight && g_cr.player < 1) {
            CrSetFocus(CrComposeFocus(g_cr.row, g_cr.player + 1), "browse player right");
        }
        if (activate) {
            if (!CrRowFocusable(g_cr.player, g_cr.row)) return;
            SetCrMode(MainMode::Adjust, "browse activate");
            Sound::PlayDecision();
            return;
        }
        if (back) {
            if (Screens::IsSubmenuActive()) {
                Screens::CloseTopSubmenu();
                Sound::PlayDecision();
            } else {
                BeginCloseAnimation();
                Sound::PlayDecision();
            }
            return;
        }
        return;
    }

    const bool bigStep = navUp || navDown;
    if (navLeft) {
        Screens::CrAdjustCell(g_cr.player, physical, -1, bigStep);
        Sound::PlayCursor();
    } else if (navRight) {
        Screens::CrAdjustCell(g_cr.player, physical, +1, bigStep);
        Sound::PlayCursor();
    } else if (navUp) {
        Screens::CrAdjustCell(g_cr.player, physical, -1, true);
        Sound::PlayCursor();
    } else if (navDown) {
        Screens::CrAdjustCell(g_cr.player, physical, +1, true);
        Sound::PlayCursor();
    } else if (activate) {
        Screens::CrActivateCell(g_cr.player, physical);
        Sound::PlayCursor();
    }

    if (back) {
        SetCrMode(MainMode::Browse, "adjust back");
        Sound::PlayDecision();
    }
}

void RenderTabBar(const MainLayout& L) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (!dl) return;

    const char* labels[TT_COUNT];
    for (int i = 0; i < TT_COUNT; ++i) labels[i] = TopTabLabel(i);

    Layout::DrawTabBar(
        dl, L.panelTL.x, L.tabBarY, Theme::kPanelW,
        labels, TT_COUNT,
        /*activeIdx=*/g_shell.activeTopTab,
        /*focusedIdx=*/g_shell.focusRegion == FocusRegion::TopTabs ? g_shell.activeTopTab : -1);

    const int top = ClampTopTab(g_shell.activeTopTab);
    const TopTabInfo& tt = kTopTabs[top];
    if (tt.subCount > 1 && L.subBarH > 0.0f) {
        const char* subLabels[8];
        const int n = tt.subCount > 8 ? 8 : tt.subCount;
        for (int i = 0; i < n; ++i) subLabels[i] = SubTabLabel(tt.subs[i]);
        Layout::DrawTabBar(
            dl, L.panelTL.x, L.subBarY, Theme::kPanelW,
            subLabels, n,
            /*activeIdx=*/g_shell.subIdxPerTop[top],
            /*focusedIdx=*/g_shell.focusRegion == FocusRegion::SubTabs ? g_shell.subIdxPerTop[top] : -1);
    }
}

void HandleMainScreenInput(const MainLayout& L, const GuiValueLocks::State& locks) {
    const Scale::Metrics& metrics = Scale::Get();
    if (g_edit.active) {
        if (RowIsLocked(locks, g_edit.rowIdx)) {
            LogMenuDetail("Edit cancelled %s (became locked by %s)",
                DescribeFocus(PANE_VALUES,g_edit.rowIdx).c_str(),
                RowLockReason(locks, g_edit.rowIdx));
            CancelEditMode();
            return;
        }

        ProcessEditTextInput();

        if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true)) {
            if (g_edit.caret > 0) {
                const int len = (int)strlen(g_edit.buf);
                for (int k = g_edit.caret - 1; k < len; ++k) g_edit.buf[k] = g_edit.buf[k + 1];
                g_edit.caret--;
            }
        }

        if (Input::Activate()) { CommitEditMode(); ImGuiGui::ApplyImGuiSettings(); return; }
        if (Input::Back())     { CancelEditMode(); return; }

        (void)Input::NavUp(); (void)Input::NavDown();
        (void)Input::NavLeft(); (void)Input::NavRight();
        (void)Input::SwitchPlayer();
        return;
    }

    const bool keyboardOrPadEdge = Input::NavUp() || Input::NavDown() ||
                                   Input::NavLeft() || Input::NavRight() ||
                                   Input::Activate() || Input::Back() ||
                                   Input::SwitchPlayer();

    // Mouse hover -> focus only if the mouse moved this frame; input edges win otherwise.
    if (!keyboardOrPadEdge && g_mouse.movedThisFrame) {
        for (int i = 0; i < kColRowCount; ++i) {
            if (Input::MouseHovering(L.colLeftX, L.rowY[i], L.colW, metrics.rowHeight)) {
                SetMainMode(MainMode::Browse, "mouse hover");
                SetFocus(i, "mouse hover");
                break;
            }
            if (Input::MouseHovering(L.colRightX, L.rowY[i], L.colW, metrics.rowHeight)) {
                SetMainMode(MainMode::Browse, "mouse hover");
                SetFocus(kP2Start + i, "mouse hover");
                break;
            }
        }
    }

    if (!keyboardOrPadEdge && Input::MouseLeftEdge()) {
        for (int i = 0; i < kColRowCount; ++i) {
            int hit = -1;
            if (Input::MouseHovering(L.colLeftX, L.rowY[i], L.colW, metrics.rowHeight))
                hit = i;
            else if (Input::MouseHovering(L.colRightX, L.rowY[i], L.colW, metrics.rowHeight))
                hit = kP2Start + i;
            if (hit < 0) continue;

            if (CurFocus() != hit) {
                SetMainMode(MainMode::Browse, "mouse click");
                SetFocus(hit, "mouse click");
                return;
            }

            if (RowIsLocked(locks, hit)) {
                LogLockedAction("Mouse edit", locks, hit);
                SetMainMode(MainMode::Browse, "locked row");
                return;
            }

            if (RowIsICColor(hit)) {
                if (hit == kICColorP1Idx) ImGuiGui::guiState.localData.p1BlueIC = !ImGuiGui::guiState.localData.p1BlueIC;
                else                       ImGuiGui::guiState.localData.p2BlueIC = !ImGuiGui::guiState.localData.p2BlueIC;
                SetMainMode(MainMode::Adjust, "mouse click toggle");
                Sound::PlayCursor();
                LogRowValueChange(hit, "Click toggle");
                ImGuiGui::ApplyImGuiSettings();
            } else if (RowIsNumeric(hit)) {
                SetMainMode(MainMode::Adjust, "mouse click edit");
                Sound::PlayDecision();
                EnterEditMode(hit);
            }
            return;
        }
    }

    // Keyboard/controller nav - explicit browse/adjust modes.
    //   Browse: Up/Down = row, Left/Right = player column, Enter = adjust.
    //   Adjust: Left/Right = small step (or set color), Up/Down = big step,
    //           Enter = text edit for numeric rows, Esc = back to browse.
    const int focus = CurFocus();
    const bool focusLocked = RowIsLocked(locks, focus);
    const bool navUp = Input::NavUp();
    const bool navDown = Input::NavDown();
    const bool navLeft = Input::NavLeft();
    const bool navRight = Input::NavRight();
    const bool activate = Input::Activate();
    const bool back = Input::Back();
    const bool switchPlayer = Input::SwitchPlayer();

    if (g_main.mode == MainMode::Adjust && focusLocked) {
        LogMenuDetail("Adjust cancelled %s (locked by %s)",
            DescribeFocus(PANE_VALUES,focus).c_str(),
            RowLockReason(locks, focus));
        SetMainMode(MainMode::Browse, "row locked");
    }

    if (switchPlayer) {
        SetFocus(ComposeFocus(g_main.row, 1 - g_main.player),
            (g_main.mode == MainMode::Browse) ? "d button switch player" : "d button switch player (adjust)");
        return;
    }

    if (g_main.mode == MainMode::Browse) {
        if (navUp && g_main.row > 0) {
            SetFocus(ComposeFocus(g_main.row - 1, g_main.player), "browse nav up");
        }
        if (navDown && g_main.row < kColRowCount - 1) {
            SetFocus(ComposeFocus(g_main.row + 1, g_main.player), "browse nav down");
        }
        if (navLeft && g_main.player > 0) {
            SetFocus(ComposeFocus(g_main.row, g_main.player - 1), "browse player left");
        }
        if (navRight && g_main.player < 1) {
            SetFocus(ComposeFocus(g_main.row, g_main.player + 1), "browse player right");
        }
        if (activate) {
            if (focusLocked) {
                LogLockedAction("Browse activate", locks, focus);
                return;
            }
            SetMainMode(MainMode::Adjust, "browse activate");
            Sound::PlayDecision();
            return;
        }
        if (back) {
            if (Screens::IsSubmenuActive()) {
                Screens::CloseTopSubmenu();
                Sound::PlayDecision();
            } else {
                LogMenuDetail("Back pressed -> close from %s", ScreenName(ActivePane()));
                Sound::PlayDecision();
                BeginCloseAnimation();
            }
            return;
        }
        return;
    }

    if (RowIsICColor(focus)) {
        bool& blue = (focus == kICColorP1Idx)
            ? ImGuiGui::guiState.localData.p1BlueIC
            : ImGuiGui::guiState.localData.p2BlueIC;
        bool changed = false;
        if (navLeft && blue)  { blue = false; changed = true; }
        if (navRight && !blue){ blue = true;  changed = true; }
        if (activate)         { blue = !blue; changed = true; }
        if (changed) {
            LogRowValueChange(focus, "Adjust color");
            Sound::PlayCursor();
            ImGuiGui::ApplyImGuiSettings();
        }
    } else if (RowIsNumeric(focus)) {
        bool changed = false;
        bool usedBig = false;
        if (navLeft)  { AdjustNumericRow(focus, -1, false); changed = true; }
        if (navRight) { AdjustNumericRow(focus, +1, false); changed = true; }
        if (navUp)    { AdjustNumericRow(focus, +1, true);  changed = true; usedBig = true; }
        if (navDown)  { AdjustNumericRow(focus, -1, true);  changed = true; usedBig = true; }
        if (changed) {
            const char* reason = usedBig
                ? ((navLeft || navRight) ? "Adjust mixed" : "Adjust big")
                : (ShiftHeld() ? "Adjust small (shift)" : "Adjust small");
            LogRowValueChange(focus, reason);
            Sound::PlayCursor();
            ImGuiGui::ApplyImGuiSettings();
        }
        if (activate) {
            Sound::PlayDecision();
            EnterEditMode(focus);
            return;
        }
    }

    if (back) {
        SetMainMode(MainMode::Browse, "adjust back");
        Sound::PlayDecision();
    }
}

void TearDownOnClose() {
    LogMenuTrace("MaybeRefreshOnOpen: visibility 1->0, tearing down");
    LogMenuDetail("Closed custom menu");
    EndCloseAnimation();
    CancelEditMode();
    SehInvokeStep("close: ResetSubmenus", &Screens::ResetSubmenus);
    SehInvokeStep("close: ResetHotswapMenuSeed", &Screens::ResetHotswapMenuSeed);
    SehInvokeStep("close: ResetTextEditor", &Screens::ResetTextEditor);
    SehInvokeStep("close: ClosePopup", &Screens::ClosePopup);
    SehInvokeStep("close: CloseKeybind", &Screens::CloseKeybind);
    g_shell.keybindWasActive = false;
}

// Returns true when the open sequence ran this frame.
bool MaybeRefreshOnOpen() {
    const bool visibleNow = ImGuiImpl::IsVisible();
    const bool customNow  = ImGuiImpl::IsExternalFallbackHost() || Config::GetSettings().useCustomMenu;

    // Render() is only called while the menu is shown; a generation change means it
    // was hidden (and maybe reopened) in between.
    const unsigned visibilityGen = ImGuiImpl::VisibilityGeneration();
    const bool flipped = visibilityGen != g_shell.lastVisibilityGen;
    g_shell.lastVisibilityGen = visibilityGen;
    if (g_shell.menuWasVisible && flipped) {
        TearDownOnClose();
        g_shell.menuWasVisible = false;
    }

    bool opened = false;
    if (visibleNow && !g_shell.menuWasVisible) {
        opened = true;
        LogMenuTrace("MaybeRefreshOnOpen: visibility 0->1 (custom=%d), beginning open sequence", customNow ? 1 : 0);
    // Drop the .pal lookup cache on open so newly added palette files are found.
        CharacterHotswap::InvalidateCustomPaletteCache();
        SehInvokeStep("open: ImGuiGui::RefreshLocalData", &ImGuiGui::RefreshLocalData);
        LogMenuTrace("open: RefreshLocalData done");
        ResetMainState("menu open");
        CancelEditMode();
        g_shell.keybindWasActive = false;
        g_mouse.lastX = g_mouse.lastY = -1.0f;
        g_mouse.movedThisFrame = false;
        SehInvokeStep("open: Screens::ResetMouseTracking", &Screens::ResetMouseTracking);
        StartOpenAnimation();
        LogMenuTrace("open: state reset done; calling Screens::ResetSubmenus");
        SehInvokeStep("open: Screens::ResetSubmenus", &Screens::ResetSubmenus);
        LogMenuTrace("open: calling Screens::ResetHotswapMenuSeed");
        SehInvokeStep("open: Screens::ResetHotswapMenuSeed", &Screens::ResetHotswapMenuSeed);
    // Snap edge detectors on open so held keys/buttons don't fire as rising edges on the
    // first input pass.
        LogMenuTrace("open: calling Input::ResetEdges");
        SehInvokeStep("open: Input::ResetEdges", &Input::ResetEdges);
        LogMenuTrace("open: open sequence complete");
        LogMenuDetail("Opened pane=%s focus=%s mode=%s",
            ScreenName(ActivePane()),
            DescribeFocus(ActivePane(), CurFocus()).c_str(),
            MainModeName(g_main.mode));
    } else if (visibleNow && customNow && !g_shell.lastWasCustom) {
        LogMenuTrace("MaybeRefreshOnOpen: switched to custom while visible");
        SehInvokeStep("switch-to-custom: RefreshLocalData", &ImGuiGui::RefreshLocalData);
        for (auto& f : g_shell.focusPerPane) f = 0;
        ResetMainState("switch to custom");
        CancelEditMode();
        g_shell.keybindWasActive = false;
        g_mouse.lastX = g_mouse.lastY = -1.0f;
        g_mouse.movedThisFrame = false;
        SehInvokeStep("switch-to-custom: ResetMouseTracking", &Screens::ResetMouseTracking);
        StartOpenAnimation();
        SehInvokeStep("switch-to-custom: ResetSubmenus", &Screens::ResetSubmenus);
        SehInvokeStep("switch-to-custom: ResetHotswapMenuSeed", &Screens::ResetHotswapMenuSeed);
        SehInvokeStep("switch-to-custom: Input::ResetEdges", &Input::ResetEdges);
        LogMenuDetail("Switched to custom menu while visible; pane=%s focus=%s mode=%s",
            ScreenName(ActivePane()),
            DescribeFocus(ActivePane(), CurFocus()).c_str(),
            MainModeName(g_main.mode));
    } else if (!visibleNow && g_shell.menuWasVisible) {
        TearDownOnClose();
    }

    g_shell.menuWasVisible = visibleNow;
    g_shell.lastWasCustom  = customNow;
    return opened;
}

} // namespace

bool RequestAnimatedClose() {
    if (!Config::GetSettings().menuAnimations || !ImGuiImpl::IsVisible()) return false;
    constexpr DWORD kRenderingRecentlyMs = 100;
    if (GetTickCount() - g_lastRenderTick.load(std::memory_order_relaxed) > kRenderingRecentlyMs) return false;
    g_closeRequested.store(true, std::memory_order_relaxed);
    return true;
}

void PrepareFrame() {
    if (!ImGui::GetCurrentContext()) return;
    // The title MISSIONS/TUTORIAL screens and the pause menu also use the custom-menu
    // fonts; the atlas must be ready while they are up (menu closed).
    if (!ImGuiImpl::IsVisible() && !PracticeMenu::TitleScreen::WantsDraw() &&
        !Mission::PauseMenu::WantsDraw() &&
        !Mission::TutorialSession::WantsDraw() &&
        I18n::GetLanguage() != I18n::Japanese) return;

    Scale::Update(Config::GetSettings().uiScale);

    // Can invalidate/recreate the shared DX9 font texture; keep out of Render(), which runs
    // after ImGui::NewFrame() and after overlay text queued draws against the old atlas.
    (void)Fonts::Rebuild(Config::GetSettings().uiScale);
}

void Render() {
    if (!ImGui::GetCurrentContext()) return;

    static unsigned long s_renderFrame = 0;
    static unsigned long s_framesSinceOpen = 0xFFFFFFFFul;
    if (MaybeRefreshOnOpen()) {
        s_framesSinceOpen = 0;
        LogMenuTrace("Render: visibility edge 0->1 detected at frame %lu", s_renderFrame + 1);
    } else if (!ImGuiImpl::IsVisible()) {
        s_framesSinceOpen = 0xFFFFFFFFul;
    } else if (s_framesSinceOpen != 0xFFFFFFFFul && s_framesSinceOpen < 32) {
        ++s_framesSinceOpen;
    }

    if (!ImGuiImpl::IsVisible()) return;

    g_lastRenderTick.store(GetTickCount(), std::memory_order_relaxed);
    if (g_closeRequested.exchange(false, std::memory_order_relaxed)) {
        BeginCloseAnimation();
    }
    if (g_anim.closeTick != 0 && AnimT(g_anim.closeTick, kFadeAnimMs) >= 1.0f) {
        EndCloseAnimation();
        ImGuiImpl::ToggleVisibility();
        return;
    }

    ApplyPendingMenuNavigation();

    ++s_renderFrame;
    const DWORD renderStartMs = GetTickCount();
    const bool traceThisFrame = (s_framesSinceOpen < 8);
    if (traceThisFrame) {
        LogMenuTrace("Render frame=%lu postOpen=%lu pane=%s topTab=%d sub=%d focusReg=%d",
            s_renderFrame, s_framesSinceOpen, ScreenName(ActivePane()),
            g_shell.activeTopTab,
            g_shell.subIdxPerTop[ClampTopTab(g_shell.activeTopTab)],
            (int)g_shell.focusRegion);
    }

    UpdateMouseState();
    Scale::Update(Config::GetSettings().uiScale);
    const Scale::Metrics& metrics = Scale::Get();

    // Polled every frame: a digit held through a capture or edit must not read as a new press.
    const int topTabNumber = Input::TopTabNumberEdge();

    // Tab cycling is blocked while editing a value or while a dropdown popup is open.
    if (!g_edit.active && !Screens::IsPopupActive() && !Screens::IsKeybindActive() &&
        !Screens::IsTextEditorActive()) {
        if (Input::TopTabPrev()) {
            CancelEditMode();
            SetActiveTopTab((g_shell.activeTopTab + TT_COUNT - 1) % TT_COUNT, "top-tab prev");
        } else if (Input::TopTabNext()) {
            CancelEditMode();
            SetActiveTopTab((g_shell.activeTopTab + 1) % TT_COUNT, "top-tab next");
        }

        const int top = ClampTopTab(g_shell.activeTopTab);
        const int subCount = kTopTabs[top].subCount;
        if (subCount > 1) {
            if (Input::SubTabPrev()) {
                CancelEditMode();
                SetActiveSubTab(top, (g_shell.subIdxPerTop[top] + subCount - 1) % subCount, "sub-tab prev");
            } else if (Input::SubTabNext()) {
                CancelEditMode();
                SetActiveSubTab(top, (g_shell.subIdxPerTop[top] + 1) % subCount, "sub-tab next");
            }
        }

        if (topTabNumber >= 1 && topTabNumber <= TT_COUNT) {
            CancelEditMode();
            SetActiveTopTab(topTabNumber - 1, "number-key jump");
        }
    }

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (!dl) return;
    MenuFadeScope menuFade(dl);
    const ImVec2 origin = Layout::DrawPanel(dl, "TRAINING SETTINGS");
    GuiValueLocks::State valueLocks = GuiValueLocks::Compute(ImGuiGui::guiState.localData);
    Screens::CorrectValueLocksForEngineRegenUi(valueLocks);

    MainLayout L = ComputeMainLayout(origin.y);
    ComputeTabRects(L);
    const bool tabClickConsumed = HandleTabBarClick(L);
    MaybeMouseReturnFocusToContent(L, tabClickConsumed);
    const bool tabFocusConsumed = HandleFocusedTabInput();
    if (!ImGuiImpl::IsVisible()) return;

    RenderTabBar(L);

    const int activePane = ActivePane();
    static bool s_crEditorWasActive = false;
    const bool crEditor = activePane == PANE_VALUES && Screens::IsValuesContinuousRecoveryActive();
    if (crEditor && !s_crEditorWasActive) {
        g_cr = CrMainState{};
    }
    s_crEditorWasActive = crEditor;
    HandleSettingsQuickSave();
    MainLayout contentL = L;
    ApplyContentAnimation(contentL);

    const int contentFirstVertex = dl->VtxBuffer.Size;
    if (activePane == PANE_VALUES) {
        Screens::RefreshSecondaryScreenMirrors();

        Screens::ScreenLayout sl{};
        sl.panelX         = L.panelTL.x;
        sl.contentX       = Scale::Snap(L.panelTL.x + metrics.panelPadX);
        sl.contentW       = Theme::kPanelW - metrics.panelPadX * 2.0f;
        sl.contentTopY    = Scale::Snap(L.dataStartY);
        sl.contentBottomY = ContentBottomY();
        sl.animOffsetX    = CurrentPaneOffsetX();
        sl.animOffsetY    = 0.0f;
        sl.inputEnabled   = g_shell.focusRegion == FocusRegion::Content && !tabFocusConsumed;

        MainLayout contentL = L;
        ApplyContentAnimation(contentL);

        if (Screens::IsValuesColumnEditorActive()) {
            const bool valuesInputEnabled = !tabClickConsumed && sl.inputEnabled;
            if (Screens::IsValuesPlayerEditorActive()) {
                if (valuesInputEnabled &&
                    g_main.mode == MainMode::Browse &&
                    g_main.row == 0 &&
                    Input::NavUp()) {
                    MoveFocusAboveContent("values top nav up");
                } else if (valuesInputEnabled) {
                    HandleMainScreenInput(contentL, valueLocks);
                }
                dl->PushClipRect(ImVec2(L.panelTL.x, sl.contentTopY),
                                 ImVec2(L.panelTL.x + Theme::kPanelW, sl.contentBottomY), true);
                RenderMainScreen(contentL, valueLocks);
                dl->PopClipRect();
            } else if (Screens::IsValuesContinuousRecoveryActive()) {
                const DualColumnLayout crL = ComputeCrLayout(contentL);
                if (valuesInputEnabled &&
                    g_cr.mode == MainMode::Browse &&
                    g_cr.row == 0 &&
                    Input::NavUp()) {
                    MoveFocusAboveContent("cr top nav up");
                } else if (valuesInputEnabled) {
                    HandleCrScreenInput(crL);
                }
                dl->PushClipRect(ImVec2(L.panelTL.x, sl.contentTopY),
                                 ImVec2(L.panelTL.x + Theme::kPanelW, sl.contentBottomY), true);
                RenderCrScreen(crL);
                dl->PopClipRect();
            }
        } else {
            int& focus = g_shell.focusPerPane[PANE_VALUES];
            Screens::ScrollState& scroll = g_shell.scrollPerPane[PANE_VALUES];
            bool backEdge = false;
            SehTickPane("Values", &Screens::TickValues, dl, sl, focus, scroll, backEdge);

            if (g_shell.focusRegion == FocusRegion::Content && Screens::ConsumeFocusAboveRequest()) {
                MoveFocusAboveContent("list top nav up");
            }

            Screens::TickPopupIfOpen(dl, sl);
            Screens::TickKeybindIfActive(dl, sl);
            g_shell.keybindWasActive = Screens::IsKeybindActive();

            if (!tabClickConsumed && g_shell.focusRegion == FocusRegion::Content && backEdge) {
                LogMenuDetail("Back pressed -> close from %s", ScreenName(activePane));
                BeginCloseAnimation();
                return;
            }
        }
    } else {
        Screens::ScreenLayout sl{};
        sl.panelX         = L.panelTL.x;
        sl.contentX       = Scale::Snap(L.panelTL.x + metrics.panelPadX);
        sl.contentW       = Theme::kPanelW - metrics.panelPadX * 2.0f;
        sl.contentTopY    = Scale::Snap(L.dataStartY);
        sl.contentBottomY = ContentBottomY();
        sl.animOffsetX    = CurrentPaneOffsetX();
        sl.animOffsetY    = 0.0f;
        sl.inputEnabled   = g_shell.focusRegion == FocusRegion::Content && !tabFocusConsumed;

        const DWORD mirrorStart = GetTickCount();
        if (traceThisFrame) {
            LogMenuTrace("RefreshSecondaryScreenMirrors begin");
        }
        Screens::RefreshSecondaryScreenMirrors();
        const DWORD mirrorMs = GetTickCount() - mirrorStart;
        if (traceThisFrame || mirrorMs >= 50) {
            LogMenuTrace("RefreshSecondaryScreenMirrors end ms=%lu",
                static_cast<unsigned long>(mirrorMs));
        }

        int& focus = g_shell.focusPerPane[activePane];
        Screens::ScrollState& scroll = g_shell.scrollPerPane[activePane];
        bool backEdge = false;

        if (traceThisFrame) {
            LogMenuTrace("Dispatch pane=%s (id=%d) focus=%d",
                ScreenName(activePane), activePane, focus);
        }

        const DWORD paneStartMs = GetTickCount();
        switch (activePane) {
            case PANE_OPPONENT:           SehTickPane("Opponent",        &Screens::TickOpponent,        dl, sl, focus, scroll, backEdge); break;
            case PANE_OPTIONS:            SehTickPane("Options",         &Screens::TickOptions,         dl, sl, focus, scroll, backEdge); break;
            case PANE_MENU:               SehTickPane("Menu",            &Screens::TickMenu,            dl, sl, focus, scroll, backEdge); break;
            case PANE_TRIGGERS:           SehTickPane("Triggers",        &Screens::TickTriggers,        dl, sl, focus, scroll, backEdge); break;
            case PANE_MACROS:             SehTickPane("Macros",          &Screens::TickMacros,          dl, sl, focus, scroll, backEdge); break;
            case PANE_CHARS:              SehTickPane("Chars",           &Screens::TickChars,           dl, sl, focus, scroll, backEdge); break;
            case PANE_SETTINGS_GENERAL:   SehTickPane("SettingsGeneral", &Screens::TickSettingsGeneral, dl, sl, focus, scroll, backEdge); break;
            case PANE_SETTINGS_HOTKEYS:   SehTickPane("SettingsHotkeys", &Screens::TickSettingsHotkeys, dl, sl, focus, scroll, backEdge); break;
            case PANE_SETTINGS_DEBUG:     SehTickPane("SettingsDebug",   &Screens::TickSettingsDebug,   dl, sl, focus, scroll, backEdge); break;
            case PANE_HELP_START:         SehTickPane("HelpStart",       &Screens::TickHelpStart,       dl, sl, focus, scroll, backEdge); break;
            case PANE_HELP_GUIDE:         SehTickPane("HelpGuide",       &Screens::TickHelpGuide,       dl, sl, focus, scroll, backEdge); break;
            case PANE_HELP_RESOURCES:     SehTickPane("HelpResources",   &Screens::TickHelpResources,   dl, sl, focus, scroll, backEdge); break;
            case PANE_HELP_ABOUT:         SehTickPane("HelpAbout",       &Screens::TickHelpAbout,       dl, sl, focus, scroll, backEdge); break;
            default:
                LogMenuTrace("Dispatch: unknown pane %d", activePane);
                break;
        }
        const DWORD paneMs = GetTickCount() - paneStartMs;
        static DWORD s_lastSlowPaneLog = 0;
        const DWORD paneLogNow = GetTickCount();
        if (paneMs >= 50 && (s_lastSlowPaneLog == 0 || (paneLogNow - s_lastSlowPaneLog) >= 1000)) {
            s_lastSlowPaneLog = paneLogNow;
            LogMenuTiming("Pane %s tick/render took %lums focus=%d submenu=%d popup=%d keybind=%d textEditor=%d",
                ScreenName(activePane),
                static_cast<unsigned long>(paneMs),
                focus,
                Screens::IsSubmenuActive() ? 1 : 0,
                Screens::IsPopupActive() ? 1 : 0,
                Screens::IsKeybindActive() ? 1 : 0,
                Screens::IsTextEditorActive() ? 1 : 0);
        }
        if (traceThisFrame) {
            LogMenuTrace("Dispatch pane=%s done backEdge=%d", ScreenName(activePane), backEdge ? 1 : 0);
        }
        if (g_shell.focusRegion == FocusRegion::Content && Screens::ConsumeFocusAboveRequest()) {
            MoveFocusAboveContent("list top nav up");
        }

        Screens::TickPopupIfOpen(dl, sl);
        Screens::TickKeybindIfActive(dl, sl);
        g_shell.keybindWasActive = Screens::IsKeybindActive();

        if (!tabClickConsumed && g_shell.focusRegion == FocusRegion::Content && backEdge) {
            LogMenuDetail("Back pressed -> close from %s", ScreenName(activePane));
            BeginCloseAnimation();
            return;
        }
    }
    Anim::ScaleVertexAlpha(dl, contentFirstVertex,
                           AnimT(g_anim.paneTick, kPaneAnimMs) * Screens::CurrentSubmenuAlpha());

    ImFont* bFont = Layout::BodyFont();
    const float bPx = bFont ? bFont->FontSize : 13.0f;
    const char* hint;
    std::string statusText;
    std::string descriptionText;
    auto setDescription = [&](const char* text) {
        if (text && *text) descriptionText = text;
    };
    if (const char* transientStatus = CurrentTransientStatus()) {
        statusText = transientStatus;
    }
    if (g_edit.active) {
        hint = "0-9 / . / BACKSPACE    ENTER COMMIT    ESC CANCEL";
        setDescription("Type an exact value for the selected row, then commit or cancel the edit.");
    } else if (Screens::IsKeybindActive()) {
        hint = Screens::IsGamepadKeybindActive()
            ? "RELEASE INPUTS   PRESS BUTTON   MENU CANCEL   DEL DISABLE"
            : "RELEASE KEYS THEN PRESS NEW HOTKEY   ESC CANCEL";
        setDescription(Screens::IsGamepadKeybindActive()
            ? "Bind a controller button to this command after releasing the current input."
            : "Bind a keyboard hotkey to this command after releasing the current keys.");
    } else if (Screens::IsPopupActive()) {
        hint = Screens::CurrentPopupHint();
        setDescription(Screens::CurrentPopupHelpText());
    } else if (Screens::IsRawVkEditorActive()) {
        hint = "TYPE VK CODE   APPLY   ESC CANCEL";
        setDescription("Type a raw virtual-key code, then apply it or return without changing it.");
    } else if (Screens::IsTextEditorActive()) {
        hint = "TYPE TEXT   CTRL+V PASTE   DONE COMMIT   ESC CANCEL";
        setDescription("Edit the selected text field, then commit it or return without changing it.");
    } else if (g_shell.focusRegion == FocusRegion::TopTabs) {
        hint = SettingsTabActive()
            ? "L/R CHANGE TAB   DOWN ENTER SUBTABS   D SAVE   ESC CLOSE"
            : "L/R CHANGE TAB   DOWN ENTER SUBTABS   ESC CLOSE";
        setDescription("Switch between the main menu groups: Main, Auto, Chars, Settings, and Help.");
    } else if (g_shell.focusRegion == FocusRegion::SubTabs) {
        hint = SettingsTabActive()
            ? "L/R CHANGE SUBTAB   UP TABS   DOWN ENTER OPTIONS   D SAVE   ESC CLOSE"
            : "L/R CHANGE SUBTAB   UP TABS   DOWN ENTER OPTIONS   ESC CLOSE";
        setDescription("Switch between pages inside the current menu group.");
    } else if (Screens::IsValuesContinuousRecoveryActive()) {
        if (g_cr.mode == MainMode::Browse) {
            hint = "UP/DOWN ROW   L/R OR D SWITCH PLAYER   ENTER ADJUST   ESC BACK";
            setDescription("Choose a continuous-recovery value for either player before adjusting it.");
        } else {
            hint = "L/R SMALL   U/D BIG   D SWITCH PLAYER   ENTER TOGGLE   ESC BACK";
            setDescription("Adjust the selected continuous-recovery value for the current player.");
        }
    } else if (Screens::IsValuesPlayerEditorActive()) {
        const int focus = CurFocus();
        const bool focusLocked = RowIsLocked(valueLocks, focus);
        statusText = LockStatusText(valueLocks, focus);
        static char s_desc[256];
        const std::string focusName = std::string(RowPlayer(focus) == 1 ? "P1 " : "P2 ") +
                                      I18n::Tr(g_valueRowsAll[focus].label);
        _snprintf_s(s_desc, sizeof(s_desc), _TRUNCATE,
                    I18n::Tr("Edit %s for the current match; locked values are managed by recovery settings."),
                    focusName.c_str());
        setDescription(s_desc);
        if (g_main.mode == MainMode::Browse) {
            if (focusLocked) {
                hint = "UP/DOWN ROW   L/R OR D SWITCH PLAYER   ENTER LOCKED   PGUP/PGDN TABS";
            } else {
                hint = "UP/DOWN ROW   L/R OR D SWITCH PLAYER   ENTER ADJUST   PGUP/PGDN TABS";
            }
        } else if (focusLocked) {
            hint = "THIS ROW IS LOCKED   D SWITCH PLAYER   ESC BACK";
        } else if (RowIsICColor(focus)) {
            hint = "L/R SET COLOR   D SWITCH PLAYER   ENTER TOGGLE   ESC BACK";
        } else {
            hint = "L/R SMALL   U/D BIG   D SWITCH PLAYER   ENTER TYPE   ESC BACK";
        }
    } else if (Screens::IsSubmenuActive()) {
        hint = SettingsTabActive()
            ? "UP/DOWN MOVE   ENTER SELECT   D SAVE   ESC BACK"
            : "UP/DOWN MOVE   ENTER SELECT   ESC BACK";
        setDescription(Screens::CurrentHelpText());
    } else if (ActivePane() == PANE_VALUES && !Screens::IsValuesColumnEditorActive()) {
        hint = "UP/DOWN MOVE   L/R ADJUST   ENTER SELECT   ESC BACK";
        setDescription(Screens::CurrentHelpText());
    } else if (SettingsTabActive()) {
        hint = "U/D MOVE   L/R ADJUST   D SAVE   ENTER PICK   PGUP/PGDN TAB   [ ] SUB";
        setDescription(Screens::CurrentHelpText());
    } else if (ActivePane() == PANE_TRIGGERS && !Screens::IsSubmenuActive()) {
        hint = "U/D MOVE   L/R ADJUST   D NEXT TRIGGER   ENTER PICK   PGUP/PGDN TAB   [ ] SUB";
        setDescription(Screens::CurrentHelpText());
    } else if (ActiveTopHasSubTabs()) {
        hint = "U/D MOVE   L/R ADJUST   SHIFT+L/R 2ND   ENTER PICK   PGUP/PGDN TAB   [ ] SUB";
        setDescription(Screens::CurrentHelpText());
    } else {
        hint = "U/D MOVE   L/R ADJUST   SHIFT+L/R 2ND   ENTER PICK   PGUP/PGDN TAB";
        setDescription(Screens::CurrentHelpText());
    }
    if (descriptionText.empty()) {
        setDescription("Select a row to adjust its setting or open its detailed menu.");
    }

    const float hintBoxX = Scale::Snap(L.panelTL.x + metrics.panelPadX);
    const float hintBoxW = Theme::kPanelW - metrics.panelPadX * 2.0f;
    const float hintBoxH = FooterBoxHeight();
    const float hintBoxY = Scale::Snap(Theme::PanelBottomRight().y - hintBoxH - 6.0f);
    dl->AddRectFilled(
        ImVec2(hintBoxX, hintBoxY),
        ImVec2(hintBoxX + hintBoxW, hintBoxY + hintBoxH),
        Theme::kStripStrong);
    dl->AddRect(
        ImVec2(hintBoxX + 0.5f, hintBoxY + 0.5f),
        ImVec2(hintBoxX + hintBoxW - 0.5f, hintBoxY + hintBoxH - 0.5f),
        Theme::kRule, 0.0f, 0, 1.0f);

    const float textPadX = Scale::Snap(8.0f * metrics.layoutScale);
    const float maxTextW = hintBoxW - textPadX * 2.0f;
    const std::string topLine = I18n::Tr(!statusText.empty() ? statusText.c_str() : descriptionText.c_str());
    const std::string topDraw = FitTextToWidth(bFont, bPx, topLine.c_str(), maxTextW);
    const ImU32 topCol = !statusText.empty() ? Theme::kTextStatus : Theme::kTextActive;
    const float topY = Scale::Snap(hintBoxY + 5.0f * metrics.layoutScale);
    const float topW = Layout::MeasureTextW(bFont, bPx, topDraw.c_str());
    float topX = Scale::Snap(hintBoxX + (hintBoxW - topW) * 0.5f);
    if (topX < hintBoxX + textPadX) topX = hintBoxX + textPadX;
    Layout::DrawString(dl, bFont, bPx, topX, topY, topCol, topDraw.c_str());

    const std::string hintDraw = FitTextToWidth(bFont, bPx, I18n::Tr(hint), maxTextW);
    const float hintY = Scale::Snap(hintBoxY + hintBoxH - bPx - 5.0f * metrics.layoutScale);
    const float hw = Layout::MeasureTextW(bFont, bPx, hintDraw.c_str());
    float hintX = Scale::Snap(hintBoxX + (hintBoxW - hw) * 0.5f);
    if (hintX < hintBoxX + textPadX) hintX = hintBoxX + textPadX;
    Layout::DrawString(dl, bFont, bPx, hintX, hintY, Theme::kTextInactive, hintDraw.c_str());

    const DWORD renderMs = GetTickCount() - renderStartMs;
    static DWORD s_lastSlowRenderLog = 0;
    const DWORD renderLogNow = GetTickCount();
    if (renderMs >= 75 && (s_lastSlowRenderLog == 0 || (renderLogNow - s_lastSlowRenderLog) >= 1000)) {
        s_lastSlowRenderLog = renderLogNow;
        LogMenuTiming("Render frame took %lums pane=%s topTab=%d sub=%d focusRegion=%d popup=%d submenu=%d",
            static_cast<unsigned long>(renderMs),
            ScreenName(ActivePane()),
            g_shell.activeTopTab,
            g_shell.subIdxPerTop[ClampTopTab(g_shell.activeTopTab)],
            static_cast<int>(g_shell.focusRegion),
            Screens::IsPopupActive() ? 1 : 0,
            Screens::IsSubmenuActive() ? 1 : 0);
    }
}

} // namespace CustomMenu
