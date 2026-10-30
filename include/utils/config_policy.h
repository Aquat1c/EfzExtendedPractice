#pragma once

#include <array>
#include <cstddef>
#include <string>

// Pure hotkey and INI policy; no Windows types so the host tests can cover it.
namespace ConfigPolicy {

// Negative = off, 0 = stock key, otherwise the configured VK. A result of 0 means no key.
constexpr int ResolveHotkey(int configured, int stock) {
    return configured < 0 ? 0 : (configured > 0 ? configured : stock);
}

// False for unassigned, reserved, gamepad and IME-internal VKs no keyboard can press.
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
    // Presses last one poll; an unconsumed one must not fire later.
    void BeginPoll() { pressed_.fill(false); }

    // tapped = bit 0, which also catches a tap between polls. Key repeat re-sets
    // it while held, so it only counts from the up state. A key's first sample
    // never counts: its bit 0 may be stale from before tracking began.
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

// line is already trimmed. A trailing comment is allowed; other trailing text is not a header.
inline bool ParseSectionHeader(const std::string& line, std::string& name) {
    if (line.empty() || line[0] != '[') return false;
    const std::size_t close = line.find(']');
    if (close == std::string::npos) return false;
    const std::size_t rest = line.find_first_not_of(" \t", close + 1);
    if (rest != std::string::npos && line[rest] != ';' && line[rest] != '#') return false;
    name = line.substr(1, close - 1);
    return true;
}

} // namespace ConfigPolicy
