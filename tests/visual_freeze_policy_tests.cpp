#include "utils/visual_freeze_policy.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void Check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

struct Sites {
    bool nop = false;
    bool AllOriginal() const { return !nop; }
    bool MoveTo(bool toNop) { nop = toNop; return true; }
};

enum class SavedMap { Empty, Real, Nop };

struct World {
    bool restoreFirst = true;  // g/h/i/j shape; false is e/f-classic
    Sites sites;
    SavedMap map = SavedMap::Empty;
    VisualFreezeOwner owner;
    bool flag = false;           // Practice +0xB4
    bool menuToggled = false;
    bool menuOpen = false;
    bool framestepHeld = false;
    bool poisoned = false;

    void Restore() {
        if (map != SavedMap::Empty) sites.nop = map == SavedMap::Nop;
    }
    void Capture() {
        map = sites.nop ? SavedMap::Nop : SavedMap::Real;
        if (map == SavedMap::Nop) poisoned = true;
        sites.nop = true;
    }
    // sub_1006BB00 on g/h; sub_1006B2A0 on e/f-classic.
    void Toggler(bool unfreeze) {
        if (restoreFirst) {
            Restore();
            if (!unfreeze) Capture();
        } else if (unfreeze) {
            Restore();
        } else {
            Capture();
        }
    }
    void WrappedToggler(bool unfreeze) {
        const bool retake = owner.BeginRevivalToggle(sites, flag);
        Toggler(unfreeze);
        owner.EndRevivalToggle(sites, retake, true);
    }
};

enum Event {
    FramestepPause,
    FramestepUnpause,
    MenuOpen,
    MenuClose,
    NativeHotkey,
    NativeUnfreeze,  // Practice init, or a load while the flag is clear; not wrapped.
    EventCount
};

void Apply(World& w, int event) {
    switch (event) {
    case FramestepPause:
        if (!w.framestepHeld && !w.menuOpen
            && w.owner.Acquire(w.sites, 2, w.flag) == VisualFreezeOwner::Acquired::Held) {
            w.framestepHeld = true;
        }
        break;
    case FramestepUnpause:
        if (w.framestepHeld && !w.menuOpen) {
            (void)w.owner.Release(w.sites, 2);
            w.framestepHeld = false;
        }
        break;
    case MenuOpen:
        if (w.menuOpen) break;
        w.menuOpen = true;
        // A framestep pause counts as already paused; the toggle never runs.
        if (!w.flag && !w.framestepHeld) {
            w.WrappedToggler(false);
            w.flag = true;
            w.menuToggled = true;
        }
        break;
    case MenuClose:
        if (!w.menuOpen) break;
        w.menuOpen = false;
        if (w.menuToggled) {
            w.WrappedToggler(true);
            w.flag = false;
            w.menuToggled = false;
        }
        (void)w.owner.RepairOrphan(w.sites, w.flag);
        break;
    case NativeHotkey:
        if (w.menuOpen) break;
        w.WrappedToggler(w.flag);
        w.flag = !w.flag;
        break;
    case NativeUnfreeze:
        if (w.menuOpen) break;
        w.Toggler(true);
        w.flag = false;
        break;
    default:
        break;
    }
}

void Walk(const World& w, int depth, std::vector<int>& path) {
    {
        World ended = w;
        (void)ended.owner.Boundary(ended.sites);
        Check(ended.sites.AllOriginal(), "boundary leaves the original bytes");
        Check(!ended.owner.written && ended.owner.users == 0, "boundary leaves no owner");
    }
    {
        // Sites NOPed with no pause anywhere are repaired by the next freeze.
        World next = w;
        if (!next.flag && !next.framestepHeld && !next.menuOpen) {
            (void)next.owner.RepairOrphan(next.sites, next.flag);
            Check(next.sites.AllOriginal(), "no unowned NOPs survive repair");
        }
    }
    Check(!w.poisoned, "the toggler never saves our NOPs as its originals");
    if (depth == 0) return;
    for (int event = 0; event < EventCount; ++event) {
        World next = w;
        Apply(next, event);
        path.push_back(event);
        Walk(next, depth - 1, path);
        path.pop_back();
    }
}

} // namespace

int main() {
    for (const bool restoreFirst : {true, false}) {
        World w;
        w.restoreFirst = restoreFirst;
        std::vector<int> path;
        Walk(w, 8, path);
    }
    {
        // Reported sequence: framestep pause, menu, unpause, menu again.
        World w;
        Apply(w, FramestepPause);
        Apply(w, MenuOpen);
        Apply(w, MenuClose);
        Check(w.sites.nop, "framestep keeps its NOPs across the menu");
        Apply(w, FramestepUnpause);
        Check(w.sites.AllOriginal(), "unpause restores the real bytes");
        Apply(w, MenuOpen);
        Apply(w, MenuClose);
        Check(w.sites.AllOriginal(), "no NOPs left with no owner");
        Check(w.map != SavedMap::Nop, "map holds real bytes");
    }
    {
        // Menu first, then framestep: Revival's restore must not cost us the hold.
        World w;
        Apply(w, MenuOpen);
        Apply(w, MenuClose);
        Apply(w, FramestepPause);
        Apply(w, MenuOpen);
        Apply(w, MenuClose);
        Check(w.sites.nop && w.owner.written, "hold survives a menu cycle");
        Check(w.owner.Boundary(w.sites) && w.sites.AllOriginal(), "boundary restores");
    }
    {
        // Revival's own pause is not an orphan.
        World w;
        Apply(w, NativeHotkey);
        Check(w.sites.nop, "native pause NOPs");
        Check(w.owner.Acquire(w.sites, 2, w.flag) == VisualFreezeOwner::Acquired::Refused,
              "freeze refused over a Revival-held pause");
        Check(w.sites.nop, "Revival's NOPs untouched");
    }
    {
        // Poisoned map from an unprotected path: every toggle restores NOPs, repair still recovers.
        World w;
        w.map = SavedMap::Nop;
        w.Toggler(true);
        Check(w.sites.nop, "poisoned map re-NOPs");
        Check(w.owner.Acquire(w.sites, 2, false) == VisualFreezeOwner::Acquired::Held,
              "freeze repairs the orphan first");
        Check(w.owner.Boundary(w.sites) && w.sites.AllOriginal(), "boundary restores");
    }
    return 0;
}
