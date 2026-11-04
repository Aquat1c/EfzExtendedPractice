#pragma once
#include "../3rdparty/imgui/imgui.h"
#include "../utils/utilities.h"

namespace ImGuiGui {
    void RenderGui();
    
    void RenderGameValuesTab();
    void RenderAutoActionTab();
    void RenderHelpTab();
    void RenderCharacterTab();
    void RenderDebugInputTab();
    
    struct ImGuiGuiState {
        bool visible;
        int currentTab;
        int requestedTab; 
        int mainMenuSubTab;   // 0=Opponent, 1=Values, 2=Options
        int autoActionSubTab; // 0=Triggers, 1=Macros
        int helpSubTab;
        int requestedMainMenuSubTab;
        int requestedAutoActionSubTab;
        int requestedHelpSubTab;
        DisplayData localData;
    };
    
    extern ImGuiGuiState guiState;
    
    void Initialize();
    
    void ApplyImGuiSettings();
    void RefreshLocalData();
    void ResetForPracticeSession(const char* reason, bool resetTabs = true);
    
    bool ShouldShowCharacterSettings();
    
    // Logical top-tab order: Main, Auto Actions, Characters, Palette, Settings, Help.
    void RequestTopTabCycle(int direction);
    void RequestActiveSubTabCycle(int direction);
    void RequestTopTabAbsolute(int logicalIndex);
    void RequestInitialNavFocus();
}
