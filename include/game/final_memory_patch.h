#pragma once

// Patches the FM low-HP check ([this+0x108] HP CMP 3333 -> 10000) through the patch ledger;
// returns the number of patched sites.
int ApplyFinalMemoryHPBypass();

int RevertFinalMemoryHPBypass();

int SetFinalMemoryBypass(bool enabled);

bool IsFinalMemoryBypassEnabled();

bool IsFinalMemoryBypassInstalled();

int SyncFinalMemoryBypassForCurrentMode(const char* reason = nullptr);

// Always restores the original FM HP checks, even during suspend/shutdown;
// the requested preference is preserved for later reapply.
int ForceRestoreFinalMemoryHPBypass(const char* reason = nullptr);
