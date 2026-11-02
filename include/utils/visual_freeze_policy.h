#pragma once
#include <cstdint>

// Ownership of the 10 efz.exe visual sites shared with Revival's patch toggler. Sites provides
// AllOriginal() and MoveTo(nop), which returns false on foreign bytes. Callers hold one lock.
struct VisualFreezeOwner {
    enum class Acquired { Refused, Held, RolledBack };

    uint8_t users = 0;
    bool written = false;

    // revivalPaused: Revival's own pause legitimately holds the NOPs.
    template <class Sites>
    bool RepairOrphan(Sites& sites, bool revivalPaused) {
        if (written || users != 0 || revivalPaused) return false;
        return sites.MoveTo(false);
    }

    template <class Sites>
    Acquired Acquire(Sites& sites, uint8_t bit, bool revivalPaused) {
        if (!written) {
            (void)RepairOrphan(sites, revivalPaused);
            if (!sites.AllOriginal()) return Acquired::Refused;
        }
        if (!sites.MoveTo(true) && users == 0 && sites.MoveTo(false)) {
            written = false;
            return Acquired::RolledBack;
        }
        written = true;
        users = static_cast<uint8_t>(users | bit);
        return Acquired::Held;
    }

    template <class Sites>
    bool Release(Sites& sites, uint8_t bit) {
        users = static_cast<uint8_t>(users & ~bit);
        if (users != 0 || !written) return true;
        if (!sites.MoveTo(false)) return false;
        written = false;
        return true;
    }

    // The toggler saves live bytes as its originals, so it must only ever see real bytes.
    // True: the caller retakes through EndRevivalToggle.
    template <class Sites>
    bool BeginRevivalToggle(Sites& sites, bool revivalPaused) {
        if (written) {
            (void)sites.MoveTo(false);
            return true;
        }
        (void)RepairOrphan(sites, revivalPaused);
        return false;
    }

    template <class Sites>
    void EndRevivalToggle(Sites& sites, bool retake, bool gateOpen) {
        if (retake && written && gateOpen) (void)sites.MoveTo(true);
    }

    template <class Sites>
    bool Boundary(Sites& sites) {
        users = 0;
        written = false;
        return sites.MoveTo(false);
    }
};
