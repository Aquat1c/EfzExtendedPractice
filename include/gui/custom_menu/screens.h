#pragma once

#include "../value_lock_state.h"

// Generic "list screen" framework for the custom menu: secondary screens
// (AUTO, CHARS, SETTINGS, HELP and sub-pages) are vertical lists expressed as
// arrays of `Row` descriptors, with builder helpers and the render/input
// driver defined here.

#include <cstdint>

struct ImDrawList;

namespace CustomMenu::Screens {

enum class RowKind : uint8_t {
    // Decorative / non-focusable
    Header = 0,  // section header (uppercase, dim)
    Info,        // plain text line (dim)
    Spacer,      // blank vertical gap
    Custom,      // custom non-focusable render block

    // Focusable
    Toggle,       // bool value; Activate/L/R toggles
    IntNumber,    // int value;  L/R small step, U/D big step, Shift=big
    IntSlider,    // int value rendered as a slider track
    FloatNumber,  // float value
    DoubleNumber, // double value
    Choices,      // int index into choices[]; L/R cycles inline
    ActionStrength, // paired choices; Activate opens primary picker, L/R adjusts secondary when used
    TriggerButton,  // contextual A/B/C/S, jump dir, or dash follow-up for auto-action triggers
    Dropdown,     // int index into choices[]; Activate opens modal popup
    MaskPicker,   // uint32 bitmask; Activate opens multi-select popup
    Submenu,      // drill into another list page; Back returns to the parent
    Action,       // fire callback on Activate; optional right-side value
};

struct Row;
using RowValueFormatter = const char* (*)(const Row& row);
using RowAdjuster = bool (*)(const Row& row, int direction);
using RowChoiceValueFormatter = const char* (*)(const Row& row, int choiceValue);
using RowChoiceHelpFormatter = const char* (*)(const Row& row, int choiceValue);
using RowChoiceFilter = bool (*)(const Row& row, int choiceValue);
using PairedChoiceChange = void (*)(int* primary, int* secondary);
using RowListBuilder = Row* (*)(int& count);
using RowCustomRenderer = void (*)(ImDrawList* dl, float x, float y, float w, float h);

struct Row {
    RowKind kind;
    const char* label;
    const char* helpText;

    // Data hooks - only the one relevant to `kind` is used.
    bool*  boolPtr;

    RowCustomRenderer customDraw;
    float customHeight;

    int*   intPtr;
    int    intMin;
    int    intMax;
    int    intStepSmall;
    int    intStepBig;

    float* floatPtr;
    float  floatMin;
    float  floatMax;
    float  floatStepSmall;
    float  floatStepBig;
    const char* floatFmt;

    double* doublePtr;
    double  doubleMin;
    double  doubleMax;
    double  doubleStepSmall;
    double  doubleStepBig;
    const char* doubleFmt;

    int*   choiceIdxPtr;
    const char* const* choices;
    int    choiceCount;
    const int* choiceCategoryMap;       // optional: choices[i] -> category index
    const char* const* categoryChoices; // optional: category labels for nested dropdowns
    int    categoryCount;

    int*   choice2IdxPtr;
    const char* const* choices2;
    int    choice2Count;

    unsigned int* maskPtr;          // for MaskPicker: legacy 32-bit bitmask backing store
    uint64_t* maskLoPtr;            // for MaskPicker: concrete 128-bit mask, low bits
    uint64_t* maskHiPtr;            // for MaskPicker: concrete 128-bit mask, high bits

    void (*action)();

    // Optional right-side display string for Action rows (e.g. a current value).
    const char* (*actionValue)();

    // Submenu rows open a nested list built every frame by submenuBuilder.
    RowListBuilder submenuBuilder;
    const char* submenuTitle;

    // Optional formatter/callbacks for paired-choice rows.
    RowValueFormatter valueFormatter;
    RowAdjuster inlineAdjuster;
    RowChoiceValueFormatter choiceValueFormatter;
    // Header above a run of neighbouring choices sharing the same text; categorized popups only.
    RowChoiceValueFormatter choiceGroupFormatter;
    RowChoiceHelpFormatter choiceHelpFormatter;
    // Optional display filter. choiceValue remains the original stable value;
    // hidden mask choices keep their stored bits and per-entry data intact.
    RowChoiceFilter choiceFilter;
    PairedChoiceChange onPrimaryChoiceChange;
    PairedChoiceChange onSecondaryChoiceChange;

    // Runtime disabled / hidden tests. nullptr = always-enabled / always-shown.
    bool (*isDisabled)();
    bool (*isHidden)();

    // Fired after a value is mutated (via toggle / adjust / choice / edit).
    void (*onChange)();

    // Optional D-button alias for checkbox-style rows.
    bool useSwitchPlayerToggle;
};

// Row builders. Keep these short so screen definitions read like a small DSL.
Row Header(const char* label);
Row Info(const char* label);
Row Spacer();
Row Custom(float height,
           RowCustomRenderer draw,
           bool (*isHidden)() = nullptr);
Row Toggle(const char* label, bool* p,
           void (*onChange)() = nullptr,
           bool (*isDisabled)() = nullptr,
           bool (*isHidden)() = nullptr,
           bool useSwitchPlayerToggle = false);
Row IntNum(const char* label, int* p, int mn, int mx,
           int stepSmall = 1, int stepBig = 10,
           void (*onChange)() = nullptr,
           bool (*isDisabled)() = nullptr,
           bool (*isHidden)() = nullptr);
Row IntSlider(const char* label, int* p, int mn, int mx,
              int stepSmall = 1, int stepBig = 10,
              void (*onChange)() = nullptr,
              bool (*isDisabled)() = nullptr,
              bool (*isHidden)() = nullptr);
Row FloatNum(const char* label, float* p, float mn, float mx,
             float stepSmall, float stepBig,
             const char* fmt = "%.2f",
             void (*onChange)() = nullptr,
             bool (*isDisabled)() = nullptr,
             bool (*isHidden)() = nullptr);
Row DoubleNum(const char* label, double* p, double mn, double mx,
              double stepSmall, double stepBig,
              const char* fmt = "%.1f",
              void (*onChange)() = nullptr,
              bool (*isDisabled)() = nullptr,
              bool (*isHidden)() = nullptr);
Row ChoicesRow(const char* label, int* idx, const char* const* items, int n,
               void (*onChange)() = nullptr,
               bool (*isDisabled)() = nullptr,
               bool (*isHidden)() = nullptr);
Row ActionStrengthRow(const char* label,
                      int* actionIdx, const char* const* actions, int actionCount,
                      int* strengthIdx, const char* const* strengths, int strengthCount,
                      RowValueFormatter formatter = nullptr,
                      PairedChoiceChange onPrimaryChange = nullptr,
                      PairedChoiceChange onSecondaryChange = nullptr,
                      void (*onChange)() = nullptr,
                      bool (*isDisabled)() = nullptr,
                      bool (*isHidden)() = nullptr);
// Auto-action button column: reads/writes action+strength (or dash follow-up via intPtr).
Row TriggerButtonRow(const char* label,
                     int* action, int* strength,
                     int* dashFollowupMirror = nullptr,
                     void (*onChange)() = nullptr,
                     bool (*isDisabled)() = nullptr,
                     bool (*isHidden)() = nullptr);
const char* FormatTriggerButtonRow(const Row& row);
bool AdjustTriggerButtonRow(const Row& row, int direction);
// Dropdown: shows the selected choice label and opens a modal popup listing
// all options on Activate (for long option lists).
Row DropdownRow(const char* label, int* idx, const char* const* items, int n,
                void (*onChange)() = nullptr,
                bool (*isDisabled)() = nullptr,
                bool (*isHidden)() = nullptr);
// MaskPicker: like Dropdown but multi-select. The popup shows [X]/[ ] per item;
// Activate toggles a bit in `mask`. Closed by Back.
Row MaskPickerRow(const char* label, unsigned int* mask,
                  const char* const* items, int n,
                  void (*onChange)() = nullptr,
                  bool (*isDisabled)() = nullptr,
                  bool (*isHidden)() = nullptr);
Row MaskPickerRow64(const char* label, uint64_t* maskLo, uint64_t* maskHi,
                    const char* const* items, int n,
                    void (*onChange)() = nullptr,
                    bool (*isDisabled)() = nullptr,
                    bool (*isHidden)() = nullptr);
Row Submenu(const char* label, const char* title, RowListBuilder builder,
            const char* (*valueFn)() = nullptr,
            bool (*isDisabled)() = nullptr,
            bool (*isHidden)() = nullptr);
Row Action(const char* label, void (*fn)(),
           const char* (*valueFn)() = nullptr,
           bool (*isDisabled)() = nullptr,
           bool (*isHidden)() = nullptr);

// Driver

struct ScreenLayout {
    float panelX;
    float contentX;
    float contentW;
    float contentTopY;    // Y below the tab bar where list content begins
    float contentBottomY; // Y above the hint line; limits visible rows
    float animOffsetX;    // EFZ-style pane slide offset; input/render share it
    float animOffsetY;
    bool  inputEnabled;   // false when keyboard focus is on top/sub tabs
};

// Per-screen scrollable state. The renderer owns one of these per screen and
// passes it through; screens update scrollPx to keep the focused row visible.
struct ScrollState {
    float scrollPx = 0.0f;  // pixels scrolled from the top
    float maxScrollPx = 0.0f; // last-known max; cached for clamping
};

// Clamps focus to a valid focusable row. Call once per frame before input.
void ClampFocus(const Row* rows, int rowCount, int& focus);

// Render the row list inside `layout`. `title` draws as the first header.
// Rows that are `isHidden()` are skipped entirely.
void RenderList(ImDrawList* dl, const ScreenLayout& layout,
                const char* title,
                const Row* rows, int rowCount,
                int focus,
                const ScrollState& scroll);

// Handle input. Mutates `focus` and `scroll`. Caller is responsible for:
//   - calling ClampFocus() beforehand
//   - invoking the global Back-closes-menu / tab-switching logic outside
// Returns true if Back edge was observed AT the list level (i.e. not consumed
// by an open dropdown popup).
bool HandleListInput(const ScreenLayout& layout,
                     const Row* rows, int rowCount,
                     int& focus,
                     ScrollState& scroll);

// Per-screen entry points
// Each refreshes its transient state mirrors, delegates to the list driver,
// and reports a Back-edge so the caller can decide whether to close the menu.

void RefreshSecondaryScreenMirrors();

// MAIN top-tab sub-panes:
void TickOpponent(ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge);
void TickOptions (ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge);
void TickMenu    (ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge);
void TickValues  (ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge);

// VALUES sub-views rendered as P1|P2 column editors (see custom_menu/renderer.cpp).
bool IsValuesPlayerEditorActive();
bool IsValuesContinuousRecoveryActive();
bool IsValuesColumnEditorActive();
void CloseTopSubmenu();

// Continuous recovery column editor bindings (P1 left, P2 right).
constexpr int CrEditorRowCount = 8;
bool CrRowHidden(int player, int row);
const char* CrRowLabel(int row);
void CrFormatCell(int player, int row, char* buf, size_t bufSz);
void CrAdjustCell(int player, int row, int direction, bool bigStep);
void CrActivateCell(int player, int row);
void ResetContinuousRecoveryEditorState();

// Engine regen UI mirrors - used to fix F4/F5 param ambiguity and value locks.
void CorrectValueLocksForEngineRegenUi(GuiValueLocks::State& locks);

// AUTO top-tab sub-panes:
void TickTriggers(ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge);
void TickMacros  (ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge);

// CHARS top-tab (single pane):
void TickChars   (ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge);

// SETTINGS top-tab sub-panes:
void TickSettingsGeneral(ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge);
void TickSettingsHotkeys(ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge);
void TickSettingsDebug  (ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge);

// HELP top-tab sub-panes:
void TickHelpStart    (ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge);
void TickHelpGuide    (ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge);
void TickHelpResources(ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge);
void TickHelpAbout    (ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge);

// Popup (modal dropdown) state, exposed so the renderer can draw it last and
// query whether it is consuming input.
bool IsPopupActive();
bool TickPopupIfOpen(ImDrawList* dl, const ScreenLayout& layout);
// Discards any pending popup choice/variant without writing it back.
void ClosePopup();
bool IsSubmenuActive();
const char* ActiveSubmenuTitle();
const char* CurrentHelpText();
const char* CurrentPopupHelpText();
const char* CurrentPopupHint();

// Slides the current list in from the given side, as a sub-page change does.
void PlayContentSlide(int direction);
float CurrentSubmenuAlpha();
int CurrentPopupFocusedChoiceValue(const Row* sourceRow);
const char* FormatMaskSelectionSummary(const Row& row, int maxVisibleChoices = 3);
void ResetSubmenus();
void ResetMouseTracking();
void ResetHotswapMenuSeed();
bool IsTextEditorActive();
bool IsRawVkEditorActive();
void ResetTextEditor();
bool ConsumeFocusAboveRequest();

// Cross-tab navigation (queued from MAIN > MENU shortcuts; applied next frame).
struct MenuNavigationRequest {
    int pane = 0;
    int focusRow = 0;
    RowListBuilder submenuBuilder = nullptr;
    const char* submenuTitle = nullptr;
    int submenuFocusRow = 0;
};

// Pane ids mirror renderer.cpp `Pane` enum - keep in sync.
namespace MenuPane {
    constexpr int Values           = 0;
    constexpr int Opponent         = 1;
    constexpr int Options          = 2;
    constexpr int Menu             = 3;
    constexpr int Triggers         = 4;
    constexpr int Macros           = 5;
    constexpr int Chars            = 6;
    constexpr int SettingsGeneral  = 7;
    constexpr int SettingsHotkeys  = 8;
    constexpr int SettingsDebug    = 9;
    constexpr int HelpStart        = 10;
    constexpr int HelpGuide        = 11;
    constexpr int HelpResources    = 12;
    constexpr int HelpAbout        = 13;
}

void RequestMenuNavigation(const MenuNavigationRequest& request);
bool ConsumeMenuNavigation(MenuNavigationRequest& out);
void OpenSubmenuDirect(RowListBuilder builder, const char* title, int focusRow = 0);

// Queue the mission browser (packs + recorded) to open on the next menu frame.
// Used by the title MISSION entry after Practice launches.
void OpenMissionBrowser();
// Queue the ordinary Practice values pane with no nested submenu. Used by the
// recorder pause menu's explicit settings handoff.
void OpenPracticeRoot();
void NotifyMissionLibraryChanged();
// Starts a fresh recorder form while preserving the last chosen pack/category.
// Retake deliberately does not call this, so authored details survive it.
void PrepareNewMissionAuthoringSession();

// Hotkey-binding overlay: while active, every captured key or controller
// button is written into the active config setting; TickKeybindIfActive()
// renders and processes it. Returns true while consuming input.
bool IsKeybindActive();
bool IsGamepadKeybindActive();
void OpenKeybind(const char* title, int* field,
                 const char* section, const char* key,
                 bool disallowMenuReserved = false);
void OpenGamepadKeybind(const char* title, int* field,
                        const char* section, const char* key);
bool TickKeybindIfActive(ImDrawList* dl, const ScreenLayout& layout);
void CloseKeybind();

} // namespace CustomMenu::Screens
