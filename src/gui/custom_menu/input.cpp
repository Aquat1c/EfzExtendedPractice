#include "../include/gui/custom_menu/input.h"
#include "../include/core/logger.h"
#include "../include/utils/config.h"
#include "../include/utils/utilities.h"     // detectedBindings
#include "../include/utils/xinput_shim.h"
#include "../3rdparty/imgui/imgui.h"

#include <windows.h>
#include <Xinput.h>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <sstream>
#include <string>

namespace CustomMenu::Input {

bool IsGameWindowActive() {
    return g_efzWindowActive.load(std::memory_order_relaxed);
}

namespace {

// Never read ImGuiKey_Enter / GamepadFaceDown / other imgui_impl-fed keys:
// they mirror the game input bitmask that auto-actions, macros and automation
// write, producing spurious rising edges. Poll physical inputs only.

struct CurState {
    bool up       = false;
    bool down     = false;
    bool left     = false;
    bool right    = false;
    bool activate = false;   // any Enter / A source
    bool back     = false;   // any Esc / B source
    bool switchPlayer = false; // EFZ D / Y source
    bool topTabPrev = false;
    bool topTabNext = false;
    bool subTabPrev = false; // LT trigger / '['
    bool subTabNext = false; // RT trigger / ']'

    std::string upSource;
    std::string downSource;
    std::string leftSource;
    std::string rightSource;
    std::string activateSource;
    std::string backSource;
    std::string switchPlayerSource;
    std::string topTabPrevSource;
    std::string topTabNextSource;
    std::string subTabPrevSource;
    std::string subTabNextSource;
};

struct Edges {
    bool up, down, left, right, activate, back, switchPlayer;
    bool topTabPrev, topTabNext;
    bool subTabPrev, subTabNext;
};

CurState g_prev{};
CurState g_blockUntilRelease{};
Edges    g_cachedEdges{};
unsigned int g_cachedFrame = ~0u;

constexpr int kPseudoLeftTriggerMask = 0x10000;
constexpr int kPseudoRightTriggerMask = 0x20000;
constexpr BYTE kTriggerThreshold = 30;

void LogInputDetail(const char* fmt, ...) {
    if (!detailedLogging.load()) return;

    char buf[2048];
    va_list args;
    va_start(args, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, args);
    va_end(args);

    LogOut(std::string("[CUSTOM_MENU][INPUT] ") + buf, true);
}

bool VkDown(int vk) {
    if (vk <= 0) return false;
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

bool g_suspended = false;
bool g_topTabNumberPrev[9] = {};

bool InputAllowed() {
    return !g_suspended && IsGameWindowActive();
}

bool PadBindingDown(const XINPUT_STATE& state, int mask) {
    if (mask < 0) return false;
    if (mask == kPseudoLeftTriggerMask) return state.Gamepad.bLeftTrigger > kTriggerThreshold;
    if (mask == kPseudoRightTriggerMask) return state.Gamepad.bRightTrigger > kTriggerThreshold;
    return (state.Gamepad.wButtons & static_cast<WORD>(mask & 0xFFFF)) != 0;
}

void AppendSource(std::string& dst, const std::string& source) {
    if (!detailedLogging.load()) return;
    if (source.empty()) return;
    if (dst.find(source) != std::string::npos) return;
    if (!dst.empty()) dst += " + ";
    dst += source;
}

void Mark(bool& flag, std::string& sourceList, const std::string& source) {
    flag = true;
    AppendSource(sourceList, source);
}

std::string KeySource(const char* label, int vk) {
    if (!detailedLogging.load()) return {};
    std::ostringstream oss;
    oss << "key:" << label << "=" << GetKeyName(vk) << "(VK=" << vk << ")";
    return oss.str();
}

std::string PadControlSource(int index, const XINPUT_STATE& state,
                             bool genericSlot, const char* slotName,
                             const char* logical, const char* control) {
    if (!detailedLogging.load()) return {};
    char name[96] = {};
    XInputShim::GetPublishedControllerName(index, name, sizeof(name));

    std::ostringstream oss;
    oss << "pad" << index
        << (genericSlot ? ":Generic:" : ":XInput:")
        << (name[0] ? name : (slotName ? slotName : ""))
        << ":" << logical << "=" << control
        << "{pkt=" << state.dwPacketNumber
        << " btn=0x" << std::hex << std::uppercase << static_cast<unsigned>(state.Gamepad.wButtons)
        << std::dec
        << " LT=" << static_cast<int>(state.Gamepad.bLeftTrigger)
        << " RT=" << static_cast<int>(state.Gamepad.bRightTrigger)
        << " LX=" << state.Gamepad.sThumbLX
        << " LY=" << state.Gamepad.sThumbLY
        << " RX=" << state.Gamepad.sThumbRX
        << " RY=" << state.Gamepad.sThumbRY
        << "}";
    return oss.str();
}

template <typename Fn>
void ForEachRelevantPadState(const Fn& fn) {
    XInputShim::Snapshot snapshot{};
    XInputShim::CopySnapshot(snapshot);
    const int controllerIndex = Config::GetSettings().controllerIndex;
    if (controllerIndex >= 0 && controllerIndex <= 3) {
        if (snapshot.IsConnected(controllerIndex)) {
            fn(controllerIndex, snapshot.states[controllerIndex],
               snapshot.genericSlots[controllerIndex],
               snapshot.slotNames[controllerIndex]);
        }
        return;
    }

    for (int i = 0; i < 4; ++i) {
        if (!snapshot.IsConnected(i)) continue;
        fn(i, snapshot.states[i], snapshot.genericSlots[i], snapshot.slotNames[i]);
    }
}

CurState SampleCurrent() {
    CurState cur;
    if (!InputAllowed()) return cur;
    const auto& cfg = Config::GetSettings();

    // Keyboard - fixed VKs
    if (VkDown(VK_UP))     Mark(cur.up,       cur.upSource,       KeySource("fixed-up", VK_UP));
    if (VkDown(VK_DOWN))   Mark(cur.down,     cur.downSource,     KeySource("fixed-down", VK_DOWN));
    if (VkDown(VK_LEFT))   Mark(cur.left,     cur.leftSource,     KeySource("fixed-left", VK_LEFT));
    if (VkDown(VK_RIGHT))  Mark(cur.right,    cur.rightSource,    KeySource("fixed-right", VK_RIGHT));
    if (VkDown(VK_RETURN))    Mark(cur.activate, cur.activateSource, KeySource("fixed-activate", VK_RETURN));
    if (VkDown(VK_SEPARATOR)) Mark(cur.activate, cur.activateSource, KeySource("fixed-activate", VK_SEPARATOR));
    if (VkDown(VK_ESCAPE)) Mark(cur.back,     cur.backSource,     KeySource("fixed-back", VK_ESCAPE));
    if (VkDown(VK_PRIOR))  Mark(cur.topTabPrev, cur.topTabPrevSource, KeySource("fixed-top-prev", VK_PRIOR)); // Page Up
    if (VkDown(VK_NEXT))   Mark(cur.topTabNext, cur.topTabNextSource, KeySource("fixed-top-next", VK_NEXT));  // Page Down
    if (VkDown(VK_OEM_4))  Mark(cur.subTabPrev, cur.subTabPrevSource, KeySource("fixed-sub-prev", VK_OEM_4)); // '[' { bracket
    if (VkDown(VK_OEM_6))  Mark(cur.subTabNext, cur.subTabNextSource, KeySource("fixed-sub-next", VK_OEM_6)); // ']' } bracket

    // Configured direction keys intentionally overlap the fixed arrow keys.
    if (detectedBindings.directionsDetected) {
        if (VkDown(detectedBindings.upKey))    Mark(cur.up,    cur.upSource,    KeySource("binding-up", detectedBindings.upKey));
        if (VkDown(detectedBindings.downKey))  Mark(cur.down,  cur.downSource,  KeySource("binding-down", detectedBindings.downKey));
        if (VkDown(detectedBindings.leftKey))  Mark(cur.left,  cur.leftSource,  KeySource("binding-left", detectedBindings.leftKey));
        if (VkDown(detectedBindings.rightKey)) Mark(cur.right, cur.rightSource, KeySource("binding-right", detectedBindings.rightKey));
    }
    // User's configured attack keys: A/C = activate, B = back, D = switch player.
    if (detectedBindings.attacksDetected) {
        if (VkDown(detectedBindings.aButton)) Mark(cur.activate, cur.activateSource, KeySource("binding-A-activate", detectedBindings.aButton));
        if (VkDown(detectedBindings.cButton)) Mark(cur.activate, cur.activateSource, KeySource("binding-C-activate", detectedBindings.cButton));
        if (VkDown(detectedBindings.bButton)) Mark(cur.back,     cur.backSource,     KeySource("binding-B-back", detectedBindings.bButton));
        if (VkDown(detectedBindings.dButton)) Mark(cur.switchPlayer, cur.switchPlayerSource, KeySource("binding-D-switch", detectedBindings.dButton));
    }

    ForEachRelevantPadState([&](int index, const XINPUT_STATE& state,
                                bool genericSlot, const char* slotName) {
        const WORD b = state.Gamepad.wButtons;
        if (b & XINPUT_GAMEPAD_DPAD_UP)    Mark(cur.up,    cur.upSource,    PadControlSource(index, state, genericSlot, slotName, "up", "DPAD_UP"));
        if (b & XINPUT_GAMEPAD_DPAD_DOWN)  Mark(cur.down,  cur.downSource,  PadControlSource(index, state, genericSlot, slotName, "down", "DPAD_DOWN"));
        if (b & XINPUT_GAMEPAD_DPAD_LEFT)  Mark(cur.left,  cur.leftSource,  PadControlSource(index, state, genericSlot, slotName, "left", "DPAD_LEFT"));
        if (b & XINPUT_GAMEPAD_DPAD_RIGHT) Mark(cur.right, cur.rightSource, PadControlSource(index, state, genericSlot, slotName, "right", "DPAD_RIGHT"));
        if (b & XINPUT_GAMEPAD_A)          Mark(cur.activate, cur.activateSource, PadControlSource(index, state, genericSlot, slotName, "activate", "A"));
        if (b & XINPUT_GAMEPAD_B)          Mark(cur.back, cur.backSource, PadControlSource(index, state, genericSlot, slotName, "back", "B"));
        if (b & XINPUT_GAMEPAD_Y)          Mark(cur.switchPlayer, cur.switchPlayerSource, PadControlSource(index, state, genericSlot, slotName, "switch", "Y"));
        if (PadBindingDown(state, cfg.gpUiTopTabPrev)) Mark(cur.topTabPrev, cur.topTabPrevSource, PadControlSource(index, state, genericSlot, slotName, "top-prev", Config::GetGamepadButtonName(cfg.gpUiTopTabPrev).c_str()));
        if (PadBindingDown(state, cfg.gpUiTopTabNext)) Mark(cur.topTabNext, cur.topTabNextSource, PadControlSource(index, state, genericSlot, slotName, "top-next", Config::GetGamepadButtonName(cfg.gpUiTopTabNext).c_str()));
        if (PadBindingDown(state, cfg.gpUiSubTabPrev)) Mark(cur.subTabPrev, cur.subTabPrevSource, PadControlSource(index, state, genericSlot, slotName, "sub-prev", Config::GetGamepadButtonName(cfg.gpUiSubTabPrev).c_str()));
        if (PadBindingDown(state, cfg.gpUiSubTabNext)) Mark(cur.subTabNext, cur.subTabNextSource, PadControlSource(index, state, genericSlot, slotName, "sub-next", Config::GetGamepadButtonName(cfg.gpUiSubTabNext).c_str()));
    });

    return cur;
}

// Hold-repeat tuned for ~64 fps: 18 frames (~280 ms) delay, then 4 frames (~62 ms).
constexpr unsigned int kRepeatDelayFrames    = 18;
constexpr unsigned int kRepeatIntervalFrames = 4;

struct HoldState {
    unsigned int heldFrames = 0;
    unsigned int framesSinceFire = 0;
};
HoldState g_holdUp;
HoldState g_holdDown;
HoldState g_holdLeft;
HoldState g_holdRight;

bool TickHoldRepeat(HoldState& s, bool nowPressed, bool risingEdge) {
    if (!nowPressed) {
        s.heldFrames = 0;
        s.framesSinceFire = 0;
        return false;
    }
    if (risingEdge) {
        s.heldFrames = 1;
        s.framesSinceFire = 0;
        return true;
    }
    s.heldFrames++;
    if (s.heldFrames < kRepeatDelayFrames) {
        s.framesSinceFire++;
        return false;
    }
    s.framesSinceFire++;
    if (s.framesSinceFire >= kRepeatIntervalFrames) {
        s.framesSinceFire = 0;
        return true;
    }
    return false;
}

bool AnyBlockedRawHeld(const CurState& raw) {
    return (g_blockUntilRelease.up && raw.up)
        || (g_blockUntilRelease.down && raw.down)
        || (g_blockUntilRelease.left && raw.left)
        || (g_blockUntilRelease.right && raw.right)
        || (g_blockUntilRelease.activate && raw.activate)
        || (g_blockUntilRelease.back && raw.back)
        || (g_blockUntilRelease.switchPlayer && raw.switchPlayer)
        || (g_blockUntilRelease.topTabPrev && raw.topTabPrev)
        || (g_blockUntilRelease.topTabNext && raw.topTabNext)
        || (g_blockUntilRelease.subTabPrev && raw.subTabPrev)
        || (g_blockUntilRelease.subTabNext && raw.subTabNext);
}

void ApplyBlockUntilReleaseOne(bool rawPressed, bool& blockFlag,
                               bool& curPressed, std::string& curSource) {
    if (!blockFlag) return;
    if (!rawPressed) {
        blockFlag = false;
        return;
    }
    curPressed = false;
    curSource.clear();
}

CurState ApplyBlockUntilRelease(const CurState& raw) {
    CurState cur = raw;
    ApplyBlockUntilReleaseOne(raw.up,           g_blockUntilRelease.up,           cur.up,           cur.upSource);
    ApplyBlockUntilReleaseOne(raw.down,         g_blockUntilRelease.down,         cur.down,         cur.downSource);
    ApplyBlockUntilReleaseOne(raw.left,         g_blockUntilRelease.left,         cur.left,         cur.leftSource);
    ApplyBlockUntilReleaseOne(raw.right,        g_blockUntilRelease.right,        cur.right,        cur.rightSource);
    ApplyBlockUntilReleaseOne(raw.activate,     g_blockUntilRelease.activate,     cur.activate,     cur.activateSource);
    ApplyBlockUntilReleaseOne(raw.back,         g_blockUntilRelease.back,         cur.back,         cur.backSource);
    ApplyBlockUntilReleaseOne(raw.switchPlayer, g_blockUntilRelease.switchPlayer, cur.switchPlayer, cur.switchPlayerSource);
    ApplyBlockUntilReleaseOne(raw.topTabPrev,   g_blockUntilRelease.topTabPrev,   cur.topTabPrev,   cur.topTabPrevSource);
    ApplyBlockUntilReleaseOne(raw.topTabNext,   g_blockUntilRelease.topTabNext,   cur.topTabNext,   cur.topTabNextSource);
    ApplyBlockUntilReleaseOne(raw.subTabPrev,   g_blockUntilRelease.subTabPrev,   cur.subTabPrev,   cur.subTabPrevSource);
    ApplyBlockUntilReleaseOne(raw.subTabNext,   g_blockUntilRelease.subTabNext,   cur.subTabNext,   cur.subTabNextSource);
    return cur;
}

const char* SourceOrDash(const std::string& source) {
    return source.empty() ? "-" : source.c_str();
}

const char* FireKind(bool fired, bool risingEdge) {
    if (!fired) return "-";
    return risingEdge ? "press" : "repeat";
}

const Edges& SampleEdges() {
    const unsigned int f = ImGui::GetFrameCount();
    if (f == g_cachedFrame) return g_cachedEdges;
    g_cachedFrame = f;

    const CurState raw = SampleCurrent();
    const bool blockedRawHeld = AnyBlockedRawHeld(raw);
    CurState cur = ApplyBlockUntilRelease(raw);

    const bool risingUp    = cur.up    && !g_prev.up;
    const bool risingDown  = cur.down  && !g_prev.down;
    const bool risingLeft  = cur.left  && !g_prev.left;
    const bool risingRight = cur.right && !g_prev.right;

    g_cachedEdges.up       = TickHoldRepeat(g_holdUp,    cur.up,    risingUp);
    g_cachedEdges.down     = TickHoldRepeat(g_holdDown,  cur.down,  risingDown);
    g_cachedEdges.left     = TickHoldRepeat(g_holdLeft,  cur.left,  risingLeft);
    g_cachedEdges.right    = TickHoldRepeat(g_holdRight, cur.right, risingRight);
    g_cachedEdges.activate = cur.activate && !g_prev.activate;
    g_cachedEdges.back     = cur.back     && !g_prev.back;
    g_cachedEdges.switchPlayer = cur.switchPlayer && !g_prev.switchPlayer;
    g_cachedEdges.topTabPrev = cur.topTabPrev && !g_prev.topTabPrev;
    g_cachedEdges.topTabNext = cur.topTabNext && !g_prev.topTabNext;
    g_cachedEdges.subTabPrev = cur.subTabPrev && !g_prev.subTabPrev;
    g_cachedEdges.subTabNext = cur.subTabNext && !g_prev.subTabNext;

    if (g_cachedEdges.up || g_cachedEdges.down || g_cachedEdges.left ||
        g_cachedEdges.right || g_cachedEdges.activate || g_cachedEdges.back ||
        g_cachedEdges.switchPlayer || g_cachedEdges.topTabPrev ||
        g_cachedEdges.topTabNext || g_cachedEdges.subTabPrev ||
        g_cachedEdges.subTabNext) {
        const auto& cfg = Config::GetSettings();
        XInputShim::Snapshot padSnapshot{};
        XInputShim::CopySnapshot(padSnapshot);
        LogInputDetail(
            "Edge U=%d(%s) D=%d(%s) L=%d(%s) R=%d(%s) A=%d B=%d SW=%d TP=%d TN=%d SP=%d SN=%d | held U=%d D=%d L=%d R=%d A=%d B=%d SW=%d TP=%d TN=%d SP=%d SN=%d | rawHeld U=%d D=%d L=%d R=%d A=%d B=%d SW=%d TP=%d TN=%d SP=%d SN=%d blocked=%d | hold U=%u/%u D=%u/%u L=%u/%u R=%u/%u | cfgCtrl=%d masks connected=0x%X native=0x%X generic=0x%X | src U=[%s] D=[%s] L=[%s] R=[%s] A=[%s] B=[%s] SW=[%s] TP=[%s] TN=[%s] SP=[%s] SN=[%s]",
            g_cachedEdges.up ? 1 : 0,
            FireKind(g_cachedEdges.up, risingUp),
            g_cachedEdges.down ? 1 : 0,
            FireKind(g_cachedEdges.down, risingDown),
            g_cachedEdges.left ? 1 : 0,
            FireKind(g_cachedEdges.left, risingLeft),
            g_cachedEdges.right ? 1 : 0,
            FireKind(g_cachedEdges.right, risingRight),
            g_cachedEdges.activate ? 1 : 0,
            g_cachedEdges.back ? 1 : 0,
            g_cachedEdges.switchPlayer ? 1 : 0,
            g_cachedEdges.topTabPrev ? 1 : 0,
            g_cachedEdges.topTabNext ? 1 : 0,
            g_cachedEdges.subTabPrev ? 1 : 0,
            g_cachedEdges.subTabNext ? 1 : 0,
            cur.up ? 1 : 0,
            cur.down ? 1 : 0,
            cur.left ? 1 : 0,
            cur.right ? 1 : 0,
            cur.activate ? 1 : 0,
            cur.back ? 1 : 0,
            cur.switchPlayer ? 1 : 0,
            cur.topTabPrev ? 1 : 0,
            cur.topTabNext ? 1 : 0,
            cur.subTabPrev ? 1 : 0,
            cur.subTabNext ? 1 : 0,
            raw.up ? 1 : 0,
            raw.down ? 1 : 0,
            raw.left ? 1 : 0,
            raw.right ? 1 : 0,
            raw.activate ? 1 : 0,
            raw.back ? 1 : 0,
            raw.switchPlayer ? 1 : 0,
            raw.topTabPrev ? 1 : 0,
            raw.topTabNext ? 1 : 0,
            raw.subTabPrev ? 1 : 0,
            raw.subTabNext ? 1 : 0,
            blockedRawHeld ? 1 : 0,
            g_holdUp.heldFrames,
            g_holdUp.framesSinceFire,
            g_holdDown.heldFrames,
            g_holdDown.framesSinceFire,
            g_holdLeft.heldFrames,
            g_holdLeft.framesSinceFire,
            g_holdRight.heldFrames,
            g_holdRight.framesSinceFire,
            cfg.controllerIndex,
            padSnapshot.connectedMask,
            padSnapshot.nativeMask,
            padSnapshot.genericMask,
            SourceOrDash(cur.upSource),
            SourceOrDash(cur.downSource),
            SourceOrDash(cur.leftSource),
            SourceOrDash(cur.rightSource),
            SourceOrDash(cur.activateSource),
            SourceOrDash(cur.backSource),
            SourceOrDash(cur.switchPlayerSource),
            SourceOrDash(cur.topTabPrevSource),
            SourceOrDash(cur.topTabNextSource),
            SourceOrDash(cur.subTabPrevSource),
            SourceOrDash(cur.subTabNextSource));
    }

    g_prev = cur;
    return g_cachedEdges;
}

} // namespace

void ResetEdges() {
    // Held keys (menu-open press, phantom DirectInput directions) stay ignored until released.
    const CurState raw = SampleCurrent();
    g_blockUntilRelease = raw;
    g_prev = ApplyBlockUntilRelease(raw);
    for (int i = 0; i < 9; ++i) g_topTabNumberPrev[i] = InputAllowed() && VkDown('1' + i);
    g_cachedEdges = Edges{};
    g_cachedFrame = ImGui::GetFrameCount();
    g_holdUp = HoldState{};
    g_holdDown = HoldState{};
    g_holdLeft = HoldState{};
    g_holdRight = HoldState{};

    XInputShim::Snapshot padSnapshot{};
    XInputShim::CopySnapshot(padSnapshot);
    LogInputDetail(
        "ResetEdges held U=%d D=%d L=%d R=%d A=%d B=%d SW=%d TP=%d TN=%d SP=%d SN=%d | cfgCtrl=%d masks connected=0x%X native=0x%X generic=0x%X | src U=[%s] D=[%s] L=[%s] R=[%s] A=[%s] B=[%s] SW=[%s] TP=[%s] TN=[%s] SP=[%s] SN=[%s] | bindings Up=%s Down=%s Left=%s Right=%s A=%s B=%s C=%s D=%s",
        raw.up ? 1 : 0,
        raw.down ? 1 : 0,
        raw.left ? 1 : 0,
        raw.right ? 1 : 0,
        raw.activate ? 1 : 0,
        raw.back ? 1 : 0,
        raw.switchPlayer ? 1 : 0,
        raw.topTabPrev ? 1 : 0,
        raw.topTabNext ? 1 : 0,
        raw.subTabPrev ? 1 : 0,
        raw.subTabNext ? 1 : 0,
        Config::GetSettings().controllerIndex,
        padSnapshot.connectedMask,
        padSnapshot.nativeMask,
        padSnapshot.genericMask,
        SourceOrDash(raw.upSource),
        SourceOrDash(raw.downSource),
        SourceOrDash(raw.leftSource),
        SourceOrDash(raw.rightSource),
        SourceOrDash(raw.activateSource),
        SourceOrDash(raw.backSource),
        SourceOrDash(raw.switchPlayerSource),
        SourceOrDash(raw.topTabPrevSource),
        SourceOrDash(raw.topTabNextSource),
        SourceOrDash(raw.subTabPrevSource),
        SourceOrDash(raw.subTabNextSource),
        detectedBindings.directionsDetected ? GetKeyName(detectedBindings.upKey).c_str() : "<none>",
        detectedBindings.directionsDetected ? GetKeyName(detectedBindings.downKey).c_str() : "<none>",
        detectedBindings.directionsDetected ? GetKeyName(detectedBindings.leftKey).c_str() : "<none>",
        detectedBindings.directionsDetected ? GetKeyName(detectedBindings.rightKey).c_str() : "<none>",
        detectedBindings.attacksDetected ? GetKeyName(detectedBindings.aButton).c_str() : "<none>",
        detectedBindings.attacksDetected ? GetKeyName(detectedBindings.bButton).c_str() : "<none>",
        detectedBindings.attacksDetected ? GetKeyName(detectedBindings.cButton).c_str() : "<none>",
        detectedBindings.attacksDetected ? GetKeyName(detectedBindings.dButton).c_str() : "<none>");
}

bool NavUp()    { return SampleEdges().up;    }
bool NavDown()  { return SampleEdges().down;  }
bool NavLeft()  { return SampleEdges().left;  }
bool NavRight() { return SampleEdges().right; }
bool Activate() { return SampleEdges().activate; }
bool Back()     { return SampleEdges().back;  }
bool SwitchPlayer() { return SampleEdges().switchPlayer; }

bool TopTabPrev() {
    return SampleEdges().topTabPrev;
}

bool TopTabNext() {
    return SampleEdges().topTabNext;
}

bool SubTabPrev() { return SampleEdges().subTabPrev; }
bool SubTabNext() { return SampleEdges().subTabNext; }

// Own GetAsyncKeyState edges for '1'..'9': ImGui may swallow them during text entry.
int TopTabNumberEdge() {
    if (!InputAllowed()) {
        for (int i = 0; i < 9; ++i) g_topTabNumberPrev[i] = false;
        return 0;
    }

    int hitIdx = 0;
    for (int i = 0; i < 9; ++i) {
        const int vk = '1' + i;
        const bool now = (GetAsyncKeyState(vk) & 0x8000) != 0;
        const bool edge = now && !g_topTabNumberPrev[i];
        g_topTabNumberPrev[i] = now;
        if (edge && hitIdx == 0) hitIdx = i + 1;
    }
    if (hitIdx > 0) {
        LogInputDetail("TopTabNumberEdge %d", hitIdx);
    }
    return hitIdx;
}

// Mouse position comes from imgui_impl's PreNewFrameInputs, already in 640x480 canvas space.

float MouseWheel() {
    return g_suspended ? 0.0f : ImGui::GetIO().MouseWheel;
}

MousePos GetMouse() {
    MousePos m{0.0f, 0.0f, false};
    if (g_suspended) return m;
    ImGuiIO& io = ImGui::GetIO();
    if (io.MousePos.x <= -1.0e6f || io.MousePos.y <= -1.0e6f) return m;
    m.x = io.MousePos.x;
    m.y = io.MousePos.y;
    m.valid = true;
    return m;
}

bool MouseHovering(float x, float y, float w, float h) {
    auto m = GetMouse();
    if (!m.valid) return false;
    return m.x >= x && m.x <= x + w && m.y >= y && m.y <= y + h;
}

void SetSuspended(bool suspended) {
    g_suspended = suspended;
}

bool IsSuspended() {
    return g_suspended;
}

bool MouseClickedIn(float x, float y, float w, float h) {
    return !g_suspended && MouseHovering(x, y, w, h) && ImGui::IsMouseClicked(ImGuiMouseButton_Left, false);
}

bool MouseRightClickedIn(float x, float y, float w, float h) {
    return MouseHovering(x, y, w, h) && ImGui::IsMouseClicked(ImGuiMouseButton_Right, false);
}

bool MouseLeftEdge() {
    return !g_suspended && ImGui::IsMouseClicked(ImGuiMouseButton_Left, false);
}

} // namespace CustomMenu::Input
