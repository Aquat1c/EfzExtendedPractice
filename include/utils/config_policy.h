#pragma once

#include <array>
#include <cstddef>
#include <string>

// Pure hotkey and INI policy; no Windows types so the host tests can cover it.
namespace ConfigPolicy {

// wButtons bits XInput leaves free: GUIDE is reported only by XInputGetStateEx,
// TOUCHPAD only by the DirectInput PlayStation mapping.
constexpr int kPadGuide = 0x0400;
constexpr int kPadTouchpad = 0x0800;
// Bindings above wButtons are read from the triggers and stick axes.
constexpr int kPadLT = 0x10000;
constexpr int kPadRT = 0x20000;
constexpr int kPadLsUp = 0x40000;
constexpr int kPadLsDown = 0x80000;
constexpr int kPadLsLeft = 0x100000;
constexpr int kPadLsRight = 0x200000;
constexpr int kPadRsUp = 0x400000;
constexpr int kPadRsDown = 0x800000;
constexpr int kPadRsLeft = 0x1000000;
constexpr int kPadRsRight = 0x2000000;
constexpr int kPadTriggerThreshold = 30; // XINPUT_GAMEPAD_TRIGGER_THRESHOLD
constexpr int kPadStickThreshold = 20000; // well past drift, axes run to +-32767

// Negative or zero bindings are off. Stick Y is positive up, as XInput reports it.
constexpr bool PadBindingDown(int mask, unsigned buttons, int leftTrigger, int rightTrigger,
                              int lx, int ly, int rx, int ry) {
    return mask <= 0 ? false
        : mask == kPadLT ? leftTrigger > kPadTriggerThreshold
        : mask == kPadRT ? rightTrigger > kPadTriggerThreshold
        : mask == kPadLsUp ? ly >= kPadStickThreshold
        : mask == kPadLsDown ? ly <= -kPadStickThreshold
        : mask == kPadLsLeft ? lx <= -kPadStickThreshold
        : mask == kPadLsRight ? lx >= kPadStickThreshold
        : mask == kPadRsUp ? ry >= kPadStickThreshold
        : mask == kPadRsDown ? ry <= -kPadStickThreshold
        : mask == kPadRsLeft ? rx <= -kPadStickThreshold
        : mask == kPadRsRight ? rx >= kPadStickThreshold
        : (buttons & static_cast<unsigned>(mask & 0xFFFF)) != 0;
}

inline int FindOtherBinding(const int* bindings, int count, int self, int mask) {
    for (int i = 0; i < count; ++i) {
        if (i != self && bindings[i] == mask) return i;
    }
    return -1;
}

// Negative = off, 0 = stock key, otherwise the configured VK. A result of 0 means no key.
constexpr int ResolveHotkey(int configured, int stock) {
    return configured < 0 ? 0 : (configured > 0 ? configured : stock);
}

constexpr bool IsPhysicalKeyVk(int vk) {
    return vk > 0 && vk <= 0xFE &&
           !(vk == 0x07 || vk == 0x0A || vk == 0x0B || vk == 0x0E || vk == 0x0F ||
             (vk >= 0x3A && vk <= 0x40) || vk == 0x5E ||
             (vk >= 0x88 && vk <= 0x8F) || (vk >= 0x97 && vk <= 0x9F) ||
             vk == 0xB8 || vk == 0xB9 || (vk >= 0xC3 && vk <= 0xDA) ||
             vk == 0xE0 || vk == 0xE5 || vk == 0xE7 || vk == 0xE8 ||
             (vk >= 0xF6 && vk <= 0xFE));
}

// Per-key press detection fed from GetAsyncKeyState. Sample every tracked key on
// every poll, live or not: a press only counts if the key went down while live.
class HotkeyEdges {
public:
    void BeginPoll() { pressed_.fill(false); }

    // tapped = bit 0, which also catches a tap between polls. Key repeat re-sets it while held, so it
    // only counts from the up state. A key's first sample never counts: bit 0 may be stale.
    void Sample(int vk, bool down, bool tapped, bool live) {
        if (vk <= 0 || vk > 255) return;
        const std::size_t i = static_cast<std::size_t>(vk);
        if (known_[i] && live && !down_[i] && (down || tapped)) pressed_[i] = true;
        known_[i] = true;
        down_[i] = down;
    }

    bool Pressed(int vk) const {
        return vk > 0 && vk <= 255 && pressed_[static_cast<std::size_t>(vk)];
    }

private:
    std::array<bool, 256> known_{};
    std::array<bool, 256> down_{};
    std::array<bool, 256> pressed_{};
};

inline void StripUtf8Bom(std::string& line) {
    if (line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
}

inline bool ParseSectionHeader(const std::string& line, std::string& name) {
    if (line.empty() || line[0] != '[') return false;
    const std::size_t close = line.find(']');
    if (close == std::string::npos) return false;
    const std::size_t rest = line.find_first_not_of(" \t", close + 1);
    if (rest != std::string::npos && line[rest] != ';' && line[rest] != '#') return false;
    name = line.substr(1, close - 1);
    return true;
}

}
