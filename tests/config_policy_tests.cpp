#include "utils/config_policy.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void Check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

using ConfigPolicy::HotkeyEdges;

void Poll(HotkeyEdges& e, int vk, bool down, bool tapped, bool live) {
    e.BeginPoll();
    e.Sample(vk, down, tapped, live);
}

}

int main() {
    using namespace ConfigPolicy;

    Check(ResolveHotkey(-1, 'U') == 0, "-1 disables the hotkey");
    Check(ResolveHotkey(0, 'U') == 'U', "0 keeps the stock key");
    Check(ResolveHotkey('K', 'U') == 'K', "configured key wins");

    Check(!IsPhysicalKeyVk(0xFC), "VK_NONAME has no key");
    Check(!IsPhysicalKeyVk(0xFF) && !IsPhysicalKeyVk(0) && !IsPhysicalKeyVk(-1), "out of range");
    Check(!IsPhysicalKeyVk(0x3A) && !IsPhysicalKeyVk(0xC5), "unassigned and gamepad VKs");
    Check(IsPhysicalKeyVk('1') && IsPhysicalKeyVk(0x1B) && IsPhysicalKeyVk(0x70), "real keys");
    Check(IsPhysicalKeyVk(0x19), "IME keys exist on Japanese keyboards");

    {
        HotkeyEdges e;
        Poll(e, 'U', false, true, true);
        Check(!e.Pressed('U'), "stale latch on the first sample never fires");
        Poll(e, 'U', true, true, true);
        Check(e.Pressed('U'), "key down while live fires");
        Poll(e, 'U', true, true, true);
        Check(!e.Pressed('U'), "held key with repeat latch does not refire");
        Poll(e, 'U', false, false, true);
        Check(!e.Pressed('U'), "release does not fire");
        Poll(e, 'U', false, true, true);
        Check(e.Pressed('U'), "tap between polls still fires");
    }
    {
        HotkeyEdges e;
        Poll(e, 'J', false, false, false);
        Poll(e, 'J', true, true, false);
        Check(!e.Pressed('J'), "press while not live does not fire");
        Poll(e, 'J', true, false, true);
        Check(!e.Pressed('J'), "key held from the not-live period does not fire");
        Poll(e, 'J', false, false, true);
        Poll(e, 'J', true, false, true);
        Check(e.Pressed('J'), "fresh press after release fires");
        Poll(e, 'J', false, false, false);
        Poll(e, 'J', false, true, false);
        Poll(e, 'J', false, false, true);
        Check(!e.Pressed('J'), "tap while not live never fires later");
    }
    {
        HotkeyEdges e;
        Poll(e, 'I', false, false, true);
        Poll(e, 'I', true, true, true);
        Check(e.Pressed('I'), "armed press");
        e.BeginPoll();
        Check(!e.Pressed('I'), "an unconsumed press lasts one poll");
    }
    {
        // Two hotkeys bound to one key sample it twice in one poll.
        HotkeyEdges e;
        Poll(e, 'K', false, false, true);
        e.BeginPoll();
        e.Sample('K', true, true, true);
        e.Sample('K', true, false, true);
        Check(e.Pressed('K'), "duplicate sample keeps the press");
    }
    {
        HotkeyEdges e;
        e.BeginPoll();
        e.Sample(0, true, true, true);
        e.Sample(-1, true, true, true);
        Check(!e.Pressed(0) && !e.Pressed(-1), "disabled key is inert");
    }

    {
        using namespace ConfigPolicy;
        const auto stick = [](int mask, int lx, int ly, int rx, int ry) {
            return PadBindingDown(mask, 0, 0, 0, lx, ly, rx, ry);
        };
        Check(stick(kPadLsUp, 0, 30000, 0, 0) && !stick(kPadLsDown, 0, 30000, 0, 0), "left stick up");
        Check(stick(kPadLsDown, 0, -32768, 0, 0), "left stick down at full range");
        Check(stick(kPadLsLeft, -30000, 0, 0, 0) && stick(kPadLsRight, 30000, 0, 0, 0), "left stick sides");
        Check(stick(kPadRsUp, 0, 0, 0, 30000) && stick(kPadRsDown, 0, 0, 0, -30000), "right stick vertical");
        Check(stick(kPadRsLeft, 0, 0, -30000, 0) && stick(kPadRsRight, 0, 0, 30000, 0), "right stick sides");
        Check(!stick(kPadLsUp, 0, 8000, 0, 0), "drift stays below the threshold");
        Check(!stick(kPadLsUp, 0, 0, 0, 30000), "sticks do not cross over");
        Check(PadBindingDown(kPadLT, 0, 31, 0, 0, 0, 0, 0) && !PadBindingDown(kPadLT, 0, 30, 0, 0, 0, 0, 0), "trigger threshold");
        Check(PadBindingDown(0x1000, 0x1000, 0, 0, 0, 0, 0, 0), "plain button");
        Check(!PadBindingDown(kPadLsUp, 0xFFFF, 255, 255, 0, 0, 0, 0), "stick bit ignores buttons");
        Check(!PadBindingDown(-1, 0xFFFF, 255, 255, 30000, 30000, 30000, 30000), "disabled");
    }

    {
        const int bindings[] = {1, 2, 3, -1};
        Check(ConfigPolicy::FindOtherBinding(bindings, 4, 0, 2) == 1, "finds the other holder");
        Check(ConfigPolicy::FindOtherBinding(bindings, 4, 1, 2) == -1, "self is not a conflict");
        Check(ConfigPolicy::FindOtherBinding(bindings, 4, 0, 9) == -1, "free button");
    }

    {
        std::string line = "\xEF\xBB\xBF[General]";
        StripUtf8Bom(line);
        Check(line == "[General]", "BOM stripped");
        StripUtf8Bom(line);
        Check(line == "[General]", "no BOM is untouched");
        std::string empty;
        StripUtf8Bom(empty);
        Check(empty.empty(), "empty line");

        std::string name;
        Check(ParseSectionHeader("[Hotkeys]", name) && name == "Hotkeys", "plain header");
        Check(ParseSectionHeader("[Hotkeys] ; keys", name) && name == "Hotkeys", "semicolon comment");
        Check(ParseSectionHeader("[Hotkeys]\t# keys", name) && name == "Hotkeys", "hash comment");
        Check(!ParseSectionHeader("[Hotkeys] x=1", name), "trailing text is not a header");
        Check(!ParseSectionHeader("[Hotkeys", name), "unterminated");
        Check(!ParseSectionHeader("key=[a]", name), "value with brackets");
    }
    return 0;
}
