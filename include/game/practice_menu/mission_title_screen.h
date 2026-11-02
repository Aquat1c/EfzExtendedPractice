#pragma once
#include <string>
#include <vector>

struct ImDrawList;

namespace PracticeMenu::TitleScreen {

enum class Screen : int { None = 0, Missions, Tutorial };

struct MissionInfo {
    std::string name;
    std::string path;
    std::string description;
    std::string type;        // "combo" (trial) / "tutorial" / other (scenario)
    std::string character;
    std::string dummy;
    std::string source;
    std::string packId;
    std::string packFolder;
    std::string author;
    std::string packVersion;
    std::string packDescription;
    std::string category;
    std::string categoryLabel;
    std::string categoryDescription;
    int categoryOrder = 0;
    std::string recipe;
    std::string hint;
    int steps = 0;
    int stage = -1;
    int bgm = -1;
    int difficulty = 0;
    int order = 0;
    std::string lessonId;
    std::vector<std::string> recommendedAfter;
    std::string unavailableReason; // non-empty = capability-gated UNAVAILABLE row
    bool recorded = false;
    bool locked = false;
    bool cleared = false;
    bool updated = false;
    bool hasDemo = false;
    bool hasSavestate = false;
    bool disambiguateSource = false;
};

enum class ConfirmAction : int { None = 0, Launch, Record, Author };

// game-thread control
void Open(Screen screen, bool resumeLastLesson = false);
void Close();
bool Active();
Screen Current();

void SetMissions(std::vector<MissionInfo>&& missions);

void SetTutorialCategories(std::vector<std::pair<std::string, std::string>>&& cats);

void MoveSelection(int dir);
void MoveTab(int dir);
ConfirmAction Confirm();
bool Back();
std::string SelectedMissionPath();

// render-thread
bool WantsDraw();
void Draw(void* device, ImDrawList* dl);

}
