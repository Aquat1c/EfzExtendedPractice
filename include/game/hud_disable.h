#pragma once
// Hides the in-game HUD by hooking efz.exe's battle render pipeline; never touches EfzRevival.dll.
// Hooks are base+RVA, __fastcall+edx, and pass through outside a live Practice battle.

namespace HudDisable {

enum Element : unsigned {
    ElemTopBar     = 1u << 0,
    ElemTimer      = 1u << 1,
    ElemPortraits  = 1u << 2,
    ElemHpBars     = 1u << 3,   // health bars (green + red)
    ElemRoundDots  = 1u << 4,
    ElemNameplates = 1u << 5,
    ElemBottomBar  = 1u << 6,
    ElemSpMeter    = 1u << 7,   // super (SP) meter bars + level icons
    ElemRfGauge    = 1u << 8,
    ElemComboPanel = 1u << 9,   // combo / score / damage panel
};

enum GroupMask : unsigned {
    GroupTop    = ElemTopBar | ElemTimer | ElemPortraits | ElemHpBars | ElemRoundDots | ElemNameplates,
    GroupBottom = ElemBottomBar | ElemSpMeter | ElemRfGauge,
    GroupCombo  = ElemComboPanel,
    GroupAll    = GroupTop | GroupBottom | GroupCombo,
};

void Install();
void Remove();
// Netplay boundary: closed hooks pass every call straight through (no seam,
// no gate rewrite, no blit filtering); reopened on resume.
void SetAdmitted(bool admitted);

void SetHidden(bool hidden);
bool IsHidden();

void SetElementDisabled(unsigned bit, bool disabled);
bool IsElementDisabled(unsigned bit);

void SetElementMask(unsigned mask);

void ResetVisible();

}
