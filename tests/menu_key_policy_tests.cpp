#include "game/menu_key_policy.h"

#include <cstdlib>
#include <iostream>

namespace {

void Check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

} // namespace

int main() {
    {
        MenuKeyLatch l;
        const auto s = StepMenuKey(l, true, false, false, false);
        Check(!s.openMenu && !s.passNative, "visible UI never passes native");
    }
    {
        // Non-Esc binding: Esc held across close stays swallowed until released.
        MenuKeyLatch l;
        StepMenuKey(l, true, false, true, false);
        auto s = StepMenuKey(l, false, false, true, false);
        Check(!s.openMenu && !s.passNative, "held closing Esc swallowed");
        s = StepMenuKey(l, false, false, true, false);
        Check(!s.passNative, "still swallowed while held");
        s = StepMenuKey(l, false, false, false, false);
        Check(s.passNative, "release passes native");
        s = StepMenuKey(l, false, false, true, false);
        Check(!s.openMenu && s.passNative, "fresh Esc reaches EFZ");
    }
    {
        // Non-Esc binding: one open per press.
        MenuKeyLatch l;
        auto s = StepMenuKey(l, false, false, false, true);
        Check(s.openMenu && !s.passNative, "menu key opens");
        s = StepMenuKey(l, false, false, false, true);
        Check(!s.openMenu && !s.passNative, "held menu key opens once");
        s = StepMenuKey(l, false, false, false, false);
        Check(s.passNative, "release passes native");
        s = StepMenuKey(l, false, false, false, true);
        Check(s.openMenu, "second press opens again");
    }
    {
        // Menu key held while the menu closes does not reopen it.
        MenuKeyLatch l;
        StepMenuKey(l, true, false, false, true);
        const auto s = StepMenuKey(l, false, false, false, true);
        Check(!s.openMenu && !s.passNative, "held key across close does not reopen");
    }
    {
        // Esc binding: fresh Esc opens once and never passes.
        MenuKeyLatch l;
        auto s = StepMenuKey(l, false, true, true, true);
        Check(s.openMenu && !s.passNative, "Esc opens");
        s = StepMenuKey(l, false, true, true, true);
        Check(!s.openMenu && !s.passNative, "held Esc opens once");
        s = StepMenuKey(l, false, true, false, false);
        Check(s.passNative, "nothing held passes native");
    }
    {
        // Tutorial owns Esc under a non-Esc binding: Esc opens, never passes.
        MenuKeyLatch l;
        const auto s = StepMenuKey(l, false, true, true, false);
        Check(s.openMenu && !s.passNative, "owned Esc opens");
    }
    return 0;
}
