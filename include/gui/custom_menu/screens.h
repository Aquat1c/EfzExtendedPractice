#pragma once

#include "../value_lock_state.h"

#include <cstdint>

struct ImDrawList;

namespace CustomMenu::Screens {

enum class RowKind : uint8_t {
    Header = 0,
    Info,
    Spacer,
    Custom,

    Toggle,
    IntNumber,
    IntSlider,
    FloatNumber,
    DoubleNumber,
    Choices,
    ActionStrength,
    TriggerButton,
    Dropdown,
    MaskPicker,
    Submenu,
    Action,
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
    const char* const* categoryChoices;
    int    categoryCount;

    int*   choice2IdxPtr;
    const char* const* choices2;
    int    choice2Count;

    unsigned int* maskPtr;
    uint64_t* maskLoPtr;
    uint64_t* maskHiPtr;

    void (*action)();

    const char* (*actionValue)();

    RowListBuilder submenuBuilder;
    const char* submenuTitle;

    RowValueFormatter valueFormatter;
    RowAdjuster inlineAdjuster;
    RowChoiceValueFormatter choiceValueFormatter;
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

    void (*onChange)();

    bool useSwitchPlayerToggle;
};

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
Row TriggerButtonRow(const char* label,
                     int* action, int* strength,
                     int* dashFollowupMirror = nullptr,
                     void (*onChange)() = nullptr,
                     bool (*isDisabled)() = nullptr,
                     bool (*isHidden)() = nullptr);
const char* FormatTriggerButtonRow(const Row& row);
bool AdjustTriggerButtonRow(const Row& row, int direction);
Row DropdownRow(const char* label, int* idx, const char* const* items, int n,
                void (*onChange)() = nullptr,
                bool (*isDisabled)() = nullptr,
                bool (*isHidden)() = nullptr);
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

struct ScreenLayout {
    float panelX;
    float contentX;
    float contentW;
    float contentTopY;
    float contentBottomY;
    float animOffsetX;    // Pane slide offset; input and render must use the same value
    float animOffsetY;
    bool  inputEnabled;
};

struct ScrollState {
    float scrollPx = 0.0f;
    float maxScrollPx = 0.0f;
};

// Clamps focus to a valid focusable row. Call once per frame before input.
void ClampFocus(const Row* rows, int rowCount, int& focus);

void RenderList(ImDrawList* dl, const ScreenLayout& layout,
                const char* title,
                const Row* rows, int rowCount,
                int focus,
                const ScrollState& scroll);

// Call ClampFocus() first. Returns true only for a Back edge no open dropdown popup consumed;
// the caller handles closing the menu and tab switching.
bool HandleListInput(const ScreenLayout& layout,
                     const Row* rows, int rowCount,
                     int& focus,
                     ScrollState& scroll);

void RefreshSecondaryScreenMirrors();

void TickOpponent(ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge);
void TickOptions (ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge);
void TickMenu    (ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge);
void TickValues  (ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge);

bool IsValuesPlayerEditorActive();
bool IsValuesContinuousRecoveryActive();
bool IsValuesColumnEditorActive();
void CloseTopSubmenu();

constexpr int CrEditorRowCount = 8;
bool CrRowHidden(int player, int row);
const char* CrRowLabel(int row);
void CrFormatCell(int player, int row, char* buf, size_t bufSz);
void CrAdjustCell(int player, int row, int direction, bool bigStep);
void CrActivateCell(int player, int row);
void ResetContinuousRecoveryEditorState();

void CorrectValueLocksForEngineRegenUi(GuiValueLocks::State& locks);

void TickTriggers(ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge);
void TickMacros  (ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge);

void TickChars   (ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge);

void TickSettingsGeneral(ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge);
void TickSettingsHotkeys(ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge);
void TickSettingsDebug  (ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge);

void TickHelpStart    (ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge);
void TickHelpGuide    (ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge);
void TickHelpResources(ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge);
void TickHelpAbout    (ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge);

bool IsPopupActive();
void OpenDropdownPopup(const Row& row);
bool TickPopupIfOpen(ImDrawList* dl, const ScreenLayout& layout);
void ClosePopup();
bool IsSubmenuActive();
const char* ActiveSubmenuTitle();
const char* CurrentHelpText();
const char* CurrentPopupHelpText();
const char* CurrentPopupHint();

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

void OpenMissionBrowser();
void OpenPracticeRoot();
void NotifyMissionLibraryChanged();
void PrepareNewMissionAuthoringSession();

bool IsKeybindActive();
bool IsGamepadKeybindActive();
void OpenKeybind(const char* title, int* field,
                 const char* section, const char* key,
                 bool disallowMenuReserved = false);
void OpenGamepadKeybind(const char* title, int* field,
                        const char* section, const char* key);
bool TickKeybindIfActive(ImDrawList* dl, const ScreenLayout& layout);
void CloseKeybind();

}
