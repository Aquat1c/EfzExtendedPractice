#include "../../../include/game/practice_menu/mission_title_screen.h"

#include "../../../include/gui/custom_menu/layout.h"
#include "../../../include/gui/custom_menu/theme.h"
#include "../../../include/gui/custom_menu/scale.h"
#include "../../../include/game/character_hotswap.h"
#include "../../../include/game/mission/mission_engine.h"
#include "../../../include/game/mission/mission_render.h"
#include "../../../include/game/mission/tutorial_support.h"
#include "../../../include/utils/config.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace PracticeMenu::TitleScreen {

namespace {

namespace L = CustomMenu::Layout;
namespace T = CustomMenu::Theme;
using CustomMenu::Scale::Snap;

const char* const kStageNames[] = {
    "Courtyard of the Full Moon", "Snowy Park (Night)", "Lunch Break Courtyard",
    "School Road Park (Day)", "School Road Park (Night)", "Sunset Rooftop",
    "Shopping Street", "Tree of Beginnings", "Minase House (Day)", "Gymnasium",
    "Rainy Field", "Behind the School", "World of Eternity",
    "Abandoned Station (Day)", "Shrine Near the Sky (Day)", "Kamio House",
    "The Infinite Sky", "Minase House (Night)", "Abandoned Station (Dusk)",
    "Fargo Research Facility", "Monomi Hill", "Shrine Near the Sky (Night)",
    "Snowy Park (Day)",
};
constexpr int kStageNameCount = static_cast<int>(sizeof(kStageNames) / sizeof(kStageNames[0]));

const char* const kMissionSections[] = {
    "TRIALS", "MISSIONS", "RECORDED", "RECORD & AUTHOR"
};
constexpr int kMissionSectionCount = 4;
constexpr int kAuthorActionCount = 2;

constexpr ImU32 kBg             = IM_COL32(0, 0, 0, 178);
constexpr ImU32 kPanel          = IM_COL32(0, 0, 0, 218);
constexpr ImU32 kPanelStrong    = IM_COL32(0, 0, 0, 238);
constexpr ImU32 kPanelBorder    = IM_COL32(255, 255, 255, 70);
constexpr ImU32 kPanelBorderHot = IM_COL32(230, 230, 230, 235);
constexpr ImU32 kAccent         = IM_COL32(255, 255, 255, 255);
constexpr ImU32 kAccentSoft     = IM_COL32(196, 196, 196, 235);
constexpr ImU32 kAccentFaint    = IM_COL32(255, 255, 255, 45);
constexpr ImU32 kSelection      = IM_COL32(255, 255, 255, 34);
constexpr ImU32 kSelectionDim   = IM_COL32(255, 255, 255, 16);
constexpr ImU32 kSelectionLine  = IM_COL32(255, 255, 255, 220);
constexpr ImU32 kRule           = IM_COL32(255, 255, 255, 42);
constexpr ImU32 kHintBg         = IM_COL32(0, 0, 0, 240);
constexpr ImU32 kGood           = IM_COL32(214, 214, 214, 255);
constexpr ImU32 kWarn           = IM_COL32(255, 214, 112, 255);
constexpr ImU32 kBrowserRowFill     = IM_COL32(0, 0, 0, 218);
constexpr ImU32 kBrowserRowDisabled = IM_COL32(0, 0, 0, 232);
constexpr ImU32 kBrowserRowRule     = IM_COL32(255, 255, 255, 78);
constexpr ImU32 kBrowserRowRuleHot  = IM_COL32(255, 255, 255, 190);

std::mutex g_mx;
Screen g_screen = Screen::None;
int g_tab = 0;
int g_selection = 0;
int g_scroll = 0;
int g_visibleRows = 9;
std::vector<MissionInfo> g_missions;
std::vector<int> g_visible;
int g_missionSelection[kMissionSectionCount] = {};
int g_missionScroll[kMissionSectionCount] = {};

bool g_railFocus = false;
int g_category = 0;
std::vector<std::pair<std::string, std::string>> g_packCategories;
struct CategoryState {
    std::string key;
    std::string label;
    int available = 0;
    int unavailable = 0;
    int cleared = 0;
    int sel = 0;
    int scroll = 0;
};
std::vector<CategoryState> g_categories;

std::string Upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string SourceLabel(const MissionInfo& mission) {
    std::string label = Upper(mission.source);
    if (!mission.disambiguateSource) return label;
    std::string qualifier;
    if (!mission.packFolder.empty()) {
        const std::size_t slash = mission.packFolder.find_last_of("\\/");
        qualifier = slash == std::string::npos
            ? mission.packFolder : mission.packFolder.substr(slash + 1);
    }
    if (qualifier.empty()) qualifier = mission.packId;
    if (!qualifier.empty()) label += " [" + Upper(qualifier) + "]";
    return label;
}

bool IsTutorial(const MissionInfo& m) {
    return m.type == "tutorial";
}

bool IsTrial(const MissionInfo& m) {
    return !IsTutorial(m) && (m.type.empty() || m.type == "combo" || m.type == "trial");
}

std::string DisplayCharName(const std::string& resourceName) {
    if (resourceName.empty()) return "ANY";
    const int selectId = CharacterHotswap::GetSelectIdForResourceName(resourceName.c_str());
    return selectId >= 0 ? CharacterHotswap::GetDisplayNameForSelectId(selectId)
                         : resourceName;
}

const char* StageName(int stage) {
    return stage >= 0 && stage < kStageNameCount ? kStageNames[stage] : "Current stage";
}

void ClampScrollLocked() {
    if (g_visible.empty()) {
        g_selection = 0;
        g_scroll = 0;
        return;
    }
    if (g_selection < 0) g_selection = 0;
    if (g_selection >= static_cast<int>(g_visible.size())) {
        g_selection = static_cast<int>(g_visible.size()) - 1;
    }
    if (g_selection < g_scroll) g_scroll = g_selection;
    if (g_selection >= g_scroll + g_visibleRows) {
        g_scroll = g_selection - g_visibleRows + 1;
    }
    const int maxScroll = (std::max)(0, static_cast<int>(g_visible.size()) - g_visibleRows);
    if (g_scroll > maxScroll) g_scroll = maxScroll;
    if (g_scroll < 0) g_scroll = 0;
}

void RebuildCategoriesLocked() {
    std::vector<CategoryState> next;
    auto ensure = [&](const std::string& key, const std::string& label) -> CategoryState& {
        for (CategoryState& c : next) if (c.key == key) return c;
        CategoryState c;
        c.key = key;
        c.label = label.empty() ? Upper(key) : label;
        for (const CategoryState& old : g_categories) {
            if (old.key == key) { c.sel = old.sel; c.scroll = old.scroll; break; }
        }
        next.push_back(std::move(c));
        return next.back();
    };
    for (const auto& pc : g_packCategories) ensure(pc.first, pc.second);
    for (const MissionInfo& m : g_missions) {
        if (!IsTutorial(m)) continue;
        const std::string key = m.category.empty() ? std::string("general") : m.category;
        CategoryState& cat = ensure(key, std::string());
        if (m.unavailableReason.empty()) {
            ++cat.available;
            if (m.cleared) ++cat.cleared;
        } else {
            ++cat.unavailable;
        }
    }
    g_categories = std::move(next);
    if (g_category >= static_cast<int>(g_categories.size())) g_category = 0;
}

void RebuildVisibleLocked() {
    g_visible.clear();
    if (g_screen == Screen::Tutorial) {
        RebuildCategoriesLocked();
        const std::string key = g_category < static_cast<int>(g_categories.size())
                              ? g_categories[g_category].key : std::string();
        for (int i = 0; i < static_cast<int>(g_missions.size()); ++i) {
            const MissionInfo& m = g_missions[i];
            const std::string mkey = m.category.empty() ? std::string("general") : m.category;
            if (IsTutorial(m) && mkey == key) g_visible.push_back(i);
        }
        std::stable_sort(g_visible.begin(), g_visible.end(), [](int lhs, int rhs) {
            return g_missions[lhs].order < g_missions[rhs].order;
        });
        if (g_category < static_cast<int>(g_categories.size())) {
            g_selection = g_categories[g_category].sel;
            g_scroll = g_categories[g_category].scroll;
        } else {
            g_selection = 0;
            g_scroll = 0;
        }
        ClampScrollLocked();
        return;
    }

    for (int i = 0; i < static_cast<int>(g_missions.size()); ++i) {
        const MissionInfo& m = g_missions[i];
        bool include = false;
        if (g_screen == Screen::Missions) {
            switch (g_tab) {
                case 0: include = !m.recorded && IsTrial(m); break;
                case 1: include = !m.recorded && !IsTutorial(m) && !IsTrial(m); break;
                case 2: include = m.recorded; break;
                default: break;
            }
        }
        if (include) g_visible.push_back(i);
    }
    std::stable_sort(g_visible.begin(), g_visible.end(), [](int lhs, int rhs) {
        const MissionInfo& a = g_missions[lhs];
        const MissionInfo& b = g_missions[rhs];
        if (a.source != b.source) return a.source < b.source;
        if (a.packId != b.packId) return a.packId < b.packId;
        if (a.packFolder != b.packFolder) return a.packFolder < b.packFolder;
        const int aCategoryOrder = a.categoryOrder > 0 ? a.categoryOrder : 0x7fffffff;
        const int bCategoryOrder = b.categoryOrder > 0 ? b.categoryOrder : 0x7fffffff;
        if (aCategoryOrder != bCategoryOrder) return aCategoryOrder < bCategoryOrder;
        const std::string& aCategory = a.categoryLabel.empty() ? a.category : a.categoryLabel;
        const std::string& bCategory = b.categoryLabel.empty() ? b.category : b.categoryLabel;
        if (aCategory != bCategory) return aCategory < bCategory;
        const int aOrder = a.order > 0 ? a.order : 0x7fffffff;
        const int bOrder = b.order > 0 ? b.order : 0x7fffffff;
        if (aOrder != bOrder) return aOrder < bOrder;
        if (a.difficulty != b.difficulty) return a.difficulty < b.difficulty;
        return a.name < b.name;
    });
    if (g_tab >= 0 && g_tab < kMissionSectionCount) {
        g_selection = g_missionSelection[g_tab];
        g_scroll = g_missionScroll[g_tab];
    } else {
        g_selection = 0;
        g_scroll = 0;
    }
    if (g_tab == kMissionSectionCount - 1) {
        if (g_selection < 0 || g_selection >= kAuthorActionCount) g_selection = 0;
        g_scroll = 0;
        return;
    }
    ClampScrollLocked();
}

void RememberCategoryPosLocked() {
    if (g_screen == Screen::Tutorial) {
        if (g_category < static_cast<int>(g_categories.size())) {
            g_categories[g_category].sel = g_selection;
            g_categories[g_category].scroll = g_scroll;
        }
    } else if (g_screen == Screen::Missions &&
               g_tab >= 0 && g_tab < kMissionSectionCount) {
        g_missionSelection[g_tab] = g_selection;
        g_missionScroll[g_tab] = g_scroll;
    }
}

const MissionInfo* SelectedLocked() {
    if (g_selection < 0 || g_selection >= static_cast<int>(g_visible.size())) return nullptr;
    const int index = g_visible[g_selection];
    return index >= 0 && index < static_cast<int>(g_missions.size()) ? &g_missions[index] : nullptr;
}

struct Snapshot {
    Screen screen = Screen::None;
    int tab = 0;
    int selection = 0;
    int scroll = 0;
    int visibleRows = 9;
    bool railFocus = false;
    int category = 0;
    std::vector<CategoryState> categories;
    std::vector<MissionInfo> entries;
    int missionSectionCounts[3] = {};
};

Snapshot CaptureSnapshot() {
    std::lock_guard<std::mutex> lk(g_mx);
    Snapshot out;
    out.screen = g_screen;
    out.tab = g_tab;
    out.selection = g_selection;
    out.scroll = g_scroll;
    out.visibleRows = g_visibleRows;
    out.railFocus = g_railFocus;
    out.category = g_category;
    out.categories = g_categories;
    for (const MissionInfo& mission : g_missions) {
        if (IsTutorial(mission)) continue;
        if (mission.recorded) ++out.missionSectionCounts[2];
        else if (IsTrial(mission)) ++out.missionSectionCounts[0];
        else ++out.missionSectionCounts[1];
    }
    out.entries.reserve(g_visible.size());
    for (int i : g_visible) {
        if (i >= 0 && i < static_cast<int>(g_missions.size())) out.entries.push_back(g_missions[i]);
    }
    return out;
}

struct Geom {
    float ls = 1.0f;
    float bodyPx = 11.0f;
    float headerPx = 17.0f;
    float smallPx = 9.0f;
    float margin = 24.0f;
    float headerBottom = 36.0f;
    float tabsTop = 42.0f;
    float tabsBottom = 68.0f;
    float contentTop = 78.0f;
    float hintTop = 446.0f;
    ImFont* body = nullptr;
    ImFont* header = nullptr;
};

Geom BeginScreen(ImDrawList* dl, const char* kicker, const char* title, const char* status) {
    CustomMenu::Scale::Update(Config::GetSettings().uiScale);
    const CustomMenu::Scale::Metrics& metrics = CustomMenu::Scale::Get();
    Geom g;
    g.ls = metrics.layoutScale;
    g.bodyPx = metrics.bodyPx;
    g.headerPx = metrics.headerPx;
    g.smallPx = (std::max)(8.0f, metrics.bodyPx - 2.0f);
    g.body = L::BodyFont();
    g.header = L::HeaderFont();
    g.margin = Snap(24.0f * g.ls);

    dl->AddRectFilled(ImVec2(0, 0), ImVec2(T::kCanvasW, T::kCanvasH), kBg);
    const float bandBottom = L::DrawTitleBand(dl, title, status);
    L::DrawString(dl, g.body, g.smallPx, 14.0f, (T::kBandH * g.ls - g.smallPx) * 0.5f,
                  T::kTextStatus, kicker);

    const float titleShift = bandBottom - T::kBandH;
    g.headerBottom = 36.0f + titleShift;
    g.tabsTop = 42.0f + titleShift;
    g.tabsBottom = 68.0f + titleShift;
    g.contentTop = 78.0f + titleShift;
    g.hintTop = 446.0f;
    return g;
}

void DrawTabs(ImDrawList* dl, const Geom& g, const char* const* labels, int active) {
    const float gap = 4.0f;
    const float width = (T::kCanvasW - g.margin * 2.0f -
                         gap * (kMissionSectionCount - 1)) /
                        static_cast<float>(kMissionSectionCount);
    const float h = g.tabsBottom - g.tabsTop;
    for (int i = 0; i < kMissionSectionCount; ++i) {
        const float x0 = g.margin + (width + gap) * i;
        L::DrawNativeBarCentered(dl, x0, g.tabsTop, width, h, labels[i], i == active);
    }
}

struct HintPair { const char* key; const char* value; };

void DrawHintBar(ImDrawList* dl, const Geom& g, const HintPair* hints, int count) {
    L::DrawInfoBox(dl, 8.0f, g.hintTop, T::kCanvasW - 16.0f, T::kCanvasH - g.hintTop - 6.0f);
    const float y = g.hintTop + (T::kCanvasH - 6.0f - g.hintTop - g.smallPx) * 0.5f;
    float x = g.margin;
    for (int i = 0; i < count; ++i) {
        L::DrawString(dl, g.body, g.smallPx, x, y, T::kTextActive, hints[i].key);
        x += L::MeasureTextW(g.body, g.smallPx, hints[i].key) + 5.0f;
        L::DrawString(dl, g.body, g.smallPx, x, y, T::kTextStatus, hints[i].value);
        x += L::MeasureTextW(g.body, g.smallPx, hints[i].value) + 18.0f;
    }
}

void DrawWrapped(ImDrawList* dl, ImFont* font, float fontSize, ImVec2 pos,
                 ImU32 color, const std::string& text, float width, float height) {
    if (text.empty()) return;
    dl->PushClipRect(pos, ImVec2(pos.x + width, pos.y + height), true);
    dl->AddText(font, fontSize, pos, color, text.c_str(), nullptr, width);
    dl->PopClipRect();
}

void DrawRichWrapped(void* device, ImDrawList* dl, ImFont* font, float fontSize,
                     ImVec2 pos, ImU32 color, const std::string& text,
                     float width, float height) {
    if (text.empty() || width <= 0.0f || height <= 0.0f) return;
    dl->PushClipRect(pos, ImVec2(pos.x + width, pos.y + height), true);
    Mission::Render::DrawRichText(device, dl, font, fontSize,
                                  pos.x, pos.y, color, text, width);
    dl->PopClipRect();
}

float DrawBadge(ImDrawList* dl, const Geom& g, float x, float y, const std::string& text, ImU32 color) {
    if (text.empty()) return 0.0f;
    const float w = L::MeasureTextW(g.body, g.smallPx, text.c_str()) + 12.0f;
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + 18.0f), IM_COL32(255, 255, 255, 13), 2.0f);
    dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + 18.0f), color, 2.0f, 0, 1.0f);
    L::DrawString(dl, g.body, g.smallPx, x + 6.0f, y + (18.0f - g.smallPx) * 0.5f, color, text.c_str());
    return w;
}

std::string DifficultyLabel(int difficulty) {
    if (difficulty <= 0) return "UNRATED";
    if (difficulty > 5) difficulty = 5;
    static const char* const labels[] = { "", "NOVICE", "BEGINNER", "INTERMEDIATE", "ADVANCED", "EXPERT" };
    return labels[difficulty];
}

void DrawScrollbar(ImDrawList* dl, float x, float y0, float y1, int total, int fit, int first) {
    if (total <= fit) return;
    const float trackH = y1 - y0;
    const float thumbH = trackH * static_cast<float>(fit) / static_cast<float>(total);
    const float thumbY = y0 + trackH * static_cast<float>(first) / static_cast<float>(total);
    dl->AddRectFilled(ImVec2(x, y0), ImVec2(x + 2.0f, y1), IM_COL32(255, 255, 255, 22));
    dl->AddRectFilled(ImVec2(x, thumbY), ImVec2(x + 2.0f, thumbY + thumbH), kAccentSoft);
}

bool DrawBrowserRowChrome(ImDrawList* dl, float x0, float y, float x1,
                          float rowH, bool selected,
                          bool focused, bool disabled = false) {
    const float inset = Snap(4.0f * CustomMenu::Scale::Get().layoutScale);
    const float left = x0 + inset;
    const float right = x1 - inset;
    const float h = rowH;
    const ImU32 fill = disabled ? kBrowserRowDisabled : kBrowserRowFill;
    dl->AddRectFilled(ImVec2(left, y), ImVec2(right, y + h), fill);
    if (selected) {
        const ImU32 selectionFill = focused && !disabled ? kSelection : kSelectionDim;
        const ImU32 selectionRule = focused && !disabled
                                  ? kSelectionLine : kBrowserRowRuleHot;
        dl->AddRectFilled(ImVec2(left, y), ImVec2(right, y + h), selectionFill);
        dl->AddRectFilled(ImVec2(left, y), ImVec2(left + 2.0f, y + h),
                          selectionRule);
    }
    dl->AddLine(ImVec2(left, y + h - 1.0f), ImVec2(right, y + h - 1.0f),
                selected && focused && !disabled
                    ? kSelectionLine
                    : (selected ? kBrowserRowRuleHot : kBrowserRowRule),
                1.0f);
    return selected && focused;
}

void DrawEntryList(ImDrawList* dl, const Geom& g, const Snapshot& s,
                   float x0, float y0, float x1, float y1) {
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), kPanel);
    dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), kPanelBorder);
    L::DrawString(dl, g.body, g.smallPx, x0 + 12.0f, y0 + 9.0f, kAccentSoft, "AVAILABLE SESSIONS");
    dl->AddLine(ImVec2(x0 + 10.0f, y0 + 28.0f), ImVec2(x1 - 10.0f, y0 + 28.0f), kRule, 1.0f);

    const float rowH = (std::max)(34.0f, g.bodyPx + g.smallPx + 11.0f);
    const float listTop = y0 + 34.0f;
    const int fit = (std::max)(1, static_cast<int>((y1 - listTop - 6.0f) / rowH));
    {
        std::lock_guard<std::mutex> lk(g_mx);
        g_visibleRows = fit;
        ClampScrollLocked();
    }
    const int first = (std::max)(0, (std::min)(s.scroll, (std::max)(0, static_cast<int>(s.entries.size()) - fit)));
    const int last = (std::min)(static_cast<int>(s.entries.size()), first + fit);
    float y = listTop;
    for (int i = first; i < last; ++i) {
        const MissionInfo& m = s.entries[i];
        const bool selected = i == s.selection;
        const bool onBar = DrawBrowserRowChrome(
            dl, x0, y, x1, rowH, selected, true, m.locked);
        char number[8];
        _snprintf_s(number, sizeof(number), _TRUNCATE, "%02d", i + 1);
        const float twoLineH = g.bodyPx + 2.0f + g.smallPx;
        const float primaryY = y + (rowH - twoLineH) * 0.5f;
        const float secondaryY = primaryY + g.bodyPx + 2.0f;
        const float singleY = y + (rowH - g.smallPx) * 0.5f;
        const auto rowText = [&](ImFont* f, float px, float tx, float ty, ImU32 col, const char* txt) {
            if (onBar) L::DrawOutlinedText(dl, f, px, tx, ty, col, txt);
            else L::DrawString(dl, f, px, tx, ty, col, txt);
        };
        rowText(g.body, g.smallPx, x0 + 12.0f, singleY,
                onBar ? (m.locked ? T::kBarTextDis : T::kBarTextSel)
                      : T::kTextDisabled, number);
        const ImU32 nameColor = m.locked ? (onBar ? T::kBarTextDis : T::kTextDisabled)
                              : (onBar ? T::kBarTextSel : T::kTextInactive);
        dl->PushClipRect(ImVec2(x0 + 40.0f, y), ImVec2(x1 - 64.0f, y + rowH), true);
        rowText(g.body, g.bodyPx, x0 + 40.0f, primaryY, nameColor, Upper(m.name).c_str());
        const std::string& category = m.categoryLabel.empty() ? m.category : m.categoryLabel;
        const std::string group = category.empty()
            ? SourceLabel(m)
            : SourceLabel(m) + " / " + Upper(category);
        rowText(g.body, g.smallPx, x0 + 40.0f, secondaryY,
                onBar ? (m.locked ? T::kBarTextDis : T::kBarTextSel)
                      : T::kTextStatus, group.c_str());
        dl->PopClipRect();
        char steps[16];
        _snprintf_s(steps, sizeof(steps), _TRUNCATE, "%d STEP%s", m.steps, m.steps == 1 ? "" : "S");
        const float sw = L::MeasureTextW(g.body, g.smallPx, steps);
        rowText(g.body, g.smallPx, x1 - 12.0f - sw, singleY,
                m.locked ? (onBar ? T::kBarTextDis : T::kTextDisabled)
                         : (onBar ? T::kBarTextSel : T::kTextStatus),
                m.locked ? "LOCKED" : steps);
        y += rowH;
    }

    DrawScrollbar(dl, x1 - 4.0f, listTop, y1 - 6.0f,
                  static_cast<int>(s.entries.size()), fit, first);
}

void DrawDetail(void* device, ImDrawList* dl, const Geom& g, const MissionInfo* m,
                float x0, float y0, float x1, float y1) {
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), kPanelStrong);
    dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), kPanelBorder);
    if (!m) {
        L::DrawString(dl, g.body, g.bodyPx, x0 + 18.0f, y0 + 18.0f, T::kTextDisabled, "NO ENTRIES IN THIS TAB");
        DrawWrapped(dl, g.body, g.bodyPx, ImVec2(x0 + 18.0f, y0 + 48.0f), T::kTextStatus,
                    "Add mission JSON files below assets\\missions or choose another tab.",
                    x1 - x0 - 36.0f, y1 - y0 - 60.0f);
        return;
    }

    const float x = x0 + 18.0f;
    const float badgeGap = 8.0f;
    float bx = x;
    bx += DrawBadge(dl, g, bx, y0 + 12.0f, DifficultyLabel(m->difficulty),
                    m->locked ? T::kTextDisabled : kAccentSoft) + badgeGap;
    if (m->hasSavestate)
        bx += DrawBadge(dl, g, bx, y0 + 12.0f, "FULL STATE", kGood) + badgeGap;
    if (m->hasDemo)
        bx += DrawBadge(dl, g, bx, y0 + 12.0f, "DEMO", kWarn) + badgeGap;

    DrawWrapped(dl, g.header, g.headerPx, ImVec2(x, y0 + 39.0f),
                m->locked ? T::kTextDisabled : T::kTextActive,
                Upper(m->name), x1 - x - 18.0f, 32.0f);

    L::DrawString(dl, g.body, g.smallPx, x, y0 + 76.0f, kAccentSoft, "OBJECTIVE");
    const std::string desc = m->description.empty() ? "No description provided." : m->description;
    DrawRichWrapped(device, dl, g.body, g.bodyPx, ImVec2(x, y0 + 91.0f),
                    m->description.empty() ? T::kTextDisabled : T::kTextInactive,
                    desc, x1 - x - 18.0f, 56.0f);

    dl->AddLine(ImVec2(x, y0 + 154.0f), ImVec2(x1 - 18.0f, y0 + 154.0f), kRule, 1.0f);
    std::string meta = DisplayCharName(m->character) + " vs " + DisplayCharName(m->dummy) +
                       "  |  " + StageName(m->stage) + "  |  " + SourceLabel(*m);
    if (!m->packVersion.empty()) meta += " v" + m->packVersion;
    const std::string& category = m->categoryLabel.empty() ? m->category : m->categoryLabel;
    if (!category.empty()) meta += " / " + Upper(category);
    if (!m->author.empty()) meta += " / " + Upper(m->author);
    DrawWrapped(dl, g.body, g.smallPx, ImVec2(x, y0 + 164.0f), T::kTextStatus,
                meta, x1 - x - 18.0f, 26.0f);

    const std::string& collectionDescription = !m->categoryDescription.empty()
        ? m->categoryDescription : m->packDescription;
    if (!collectionDescription.empty()) {
        DrawWrapped(dl, g.body, g.smallPx, ImVec2(x, y0 + 187.0f), T::kTextDisabled,
                    collectionDescription, x1 - x - 18.0f, 17.0f);
    }

    L::DrawString(dl, g.body, g.smallPx, x, y0 + 207.0f, kAccentSoft, "RECIPE");
    const float recipeTop = y0 + 222.0f;
    const float recipeBottom = m->hint.empty() ? (y1 - 14.0f) : (y0 + 266.0f);
    DrawRichWrapped(device, dl, g.body, g.bodyPx, ImVec2(x, recipeTop),
                    m->recipe.empty() ? T::kTextDisabled : T::kTextActive,
                    m->recipe.empty() ? "No recipe preview." : m->recipe,
                    x1 - x - 18.0f, recipeBottom - recipeTop);

    if (!m->hint.empty()) {
        L::DrawString(dl, g.body, g.smallPx, x, y0 + 274.0f, kAccentSoft, "COACH NOTE");
        DrawRichWrapped(device, dl, g.body, g.smallPx, ImVec2(x, y0 + 289.0f),
                        T::kTextStatus, m->hint, x1 - x - 18.0f,
                        (std::max)(0.0f, y1 - (y0 + 289.0f) - 6.0f));
    }
}

void DrawCreate(ImDrawList* dl, const Geom& g, float x0, float y0, float x1, float y1) {
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), kPanelStrong);
    dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), kPanelBorder);
    L::DrawNativeBar(dl, x0 + 12.0f, y0 + 16.0f, (x1 - 12.0f) - (x0 + 12.0f), 56.0f, true);
    L::DrawOutlinedText(dl, g.header, g.headerPx, x0 + 30.0f, y0 + 25.0f, T::kBarTextSel,
                        "RECORD NEW SESSION");
    L::DrawOutlinedText(dl, g.body, g.bodyPx, x0 + 30.0f, y0 + 49.0f, T::kBarTextSel,
                        "Choose fighters, arrange the start, then capture and review.");
    L::DrawString(dl, g.body, g.smallPx, x0 + 18.0f, y0 + 98.0f, kAccentSoft,
                  "PACKS & CATEGORIES");
    DrawWrapped(dl, g.body, g.bodyPx, ImVec2(x0 + 18.0f, y0 + 116.0f), T::kTextInactive,
                "Open Pack Workshop before recording, or choose Create Pack / Add Category in Review. "
                "Your selected destination and mission details survive a Retake.",
                x1 - x0 - 36.0f, 48.0f);
    L::DrawString(dl, g.body, g.smallPx, x0 + 18.0f, y0 + 170.0f, kAccentSoft,
                  "RECORD, REVIEW, PUBLISH");
    const std::string binding = Mission::Engine::Recorder::GetMacroRecordBindingLabel();
    const int countInMs = Config::GetSettings().missionRecorderCountInMs;
    char countInText[64] = {};
    if (countInMs > 0) {
        _snprintf_s(countInText, sizeof(countInText), _TRUNCATE,
                    "the %.1f-second count-in",
                    static_cast<double>(countInMs) / 1000.0);
    } else {
        _snprintf_s(countInText, sizeof(countInText), _TRUNCATE,
                    "recording (the count-in is disabled)");
    }
    const std::string flow =
        "1. Launch Practice and choose the fighters and stage.\n"
        "2. Arrange the exact starting position, resources, and dummy state. Nothing is recorded yet.\n"
        "3. Press " + binding + " to start " + countInText + ", then release the controls.\n"
        "4. The exact start is saved on the next clear frame; then perform the mission exactly as it should be demonstrated.\n"
        "5. Press " + binding + " again to stop safely in Review. Preview the demonstration, name and rate the session, then publish it to a pack or keep it as a Recorded draft.";
    DrawWrapped(dl, g.body, g.bodyPx, ImVec2(x0 + 18.0f, y0 + 190.0f), T::kTextInactive,
                flow,
                x1 - x0 - 36.0f, y1 - y0 - 204.0f);
}

void DrawCoursePane(void* device, ImDrawList* dl, const Geom& g, const Snapshot& s,
                    float x0, float y0, float x1, float y1) {
    const bool focused = !s.railFocus;
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), kPanel);
    dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), focused ? kPanelBorderHot : kPanelBorder);

    const CategoryState* cat = s.category < static_cast<int>(s.categories.size())
                             ? &s.categories[s.category] : nullptr;
    L::DrawString(dl, g.body, g.smallPx, x0 + 12.0f, y0 + 9.0f, kAccentSoft, "LESSONS");
    if (cat) {
        char count[64];
        if (cat->unavailable > 0) {
            _snprintf_s(count, sizeof(count), _TRUNCATE, "%d / %d COMPLETE   %d UNAVAILABLE",
                        cat->cleared, cat->available, cat->unavailable);
        } else {
            _snprintf_s(count, sizeof(count), _TRUNCATE, "%d / %d COMPLETE",
                        cat->cleared, cat->available);
        }
        const float cw = L::MeasureTextW(g.body, g.smallPx, count);
        L::DrawString(dl, g.body, g.smallPx, x1 - 12.0f - cw, y0 + 9.0f, T::kTextStatus, count);
    }
    dl->AddLine(ImVec2(x0 + 10.0f, y0 + 28.0f), ImVec2(x1 - 10.0f, y0 + 28.0f), kRule, 1.0f);

    const float summaryH = 128.0f;
    const float summaryTop = y1 - summaryH;
    constexpr float rowH = 28.0f;
    const float listTop = y0 + 34.0f;
    const int fit = (std::max)(1, static_cast<int>((summaryTop - 8.0f - listTop) / rowH));
    {
        std::lock_guard<std::mutex> lk(g_mx);
        g_visibleRows = fit;
        ClampScrollLocked();
    }
    const int first = (std::max)(0, (std::min)(s.scroll, (std::max)(0, static_cast<int>(s.entries.size()) - fit)));
    const int last = (std::min)(static_cast<int>(s.entries.size()), first + fit);
    float y = listTop;
    for (int i = first; i < last; ++i) {
        const MissionInfo& m = s.entries[i];
        const bool selected = i == s.selection;
        const bool unavailable = !m.unavailableReason.empty();
        const bool onBar = DrawBrowserRowChrome(
            dl, x0, y, x1, rowH, selected, focused,
            unavailable);
        const auto rowText = [&](ImFont* f, float px, float tx, float ty, ImU32 col, const char* txt) {
            if (onBar) L::DrawOutlinedText(dl, f, px, tx, ty, col, txt);
            else L::DrawString(dl, f, px, tx, ty, col, txt);
        };
        char number[8];
        _snprintf_s(number, sizeof(number), _TRUNCATE, "%02d", i + 1);
        rowText(g.body, g.smallPx, x0 + 12.0f, y + (rowH - g.smallPx) * 0.5f,
                onBar ? (unavailable ? T::kBarTextDis : T::kBarTextSel)
                      : (selected ? kAccentSoft : T::kTextDisabled), number);
        const float mx = x0 + 34.0f;
        const float my = y + rowH * 0.5f;
        if (m.unavailableReason.empty() && m.cleared) {
            dl->AddRectFilled(ImVec2(mx, my - 4.0f), ImVec2(mx + 8.0f, my + 4.0f),
                              onBar ? T::kTextOutline : kGood);
        } else {
            dl->AddRect(ImVec2(mx, my - 4.0f), ImVec2(mx + 8.0f, my + 4.0f),
                        onBar ? (unavailable ? T::kBarTextDis : T::kTextOutline)
                              : IM_COL32(255, 255, 255, 70));
        }
        dl->PushClipRect(ImVec2(mx + 14.0f, y), ImVec2(x1 - 70.0f, y + rowH), true);
        rowText(g.body, g.bodyPx, mx + 14.0f, y + (rowH - g.bodyPx) * 0.5f,
                onBar ? (unavailable ? T::kBarTextDis : T::kBarTextSel)
                      : (unavailable ? T::kTextDisabled
                                     : (selected ? T::kTextActive : T::kTextInactive)),
                Upper(m.name).c_str());
        dl->PopClipRect();
        char tasks[24];
        if (!m.unavailableReason.empty()) {
            _snprintf_s(tasks, sizeof(tasks), _TRUNCATE, "UNAVAILABLE");
        } else if (m.updated) {
            _snprintf_s(tasks, sizeof(tasks), _TRUNCATE, "UPDATED");
        } else {
            _snprintf_s(tasks, sizeof(tasks), _TRUNCATE, "%d TASK%s", m.steps, m.steps == 1 ? "" : "S");
        }
        const float tw = L::MeasureTextW(g.body, g.smallPx, tasks);
        rowText(g.body, g.smallPx, x1 - 12.0f - tw, y + (rowH - g.smallPx) * 0.5f,
                m.unavailableReason.empty()
                    ? (onBar ? T::kBarTextSel : T::kTextStatus)
                    : (onBar ? T::kBarTextDis : T::kTextDisabled), tasks);
        y += rowH;
    }
    if (s.entries.empty()) {
        L::DrawString(dl, g.body, g.bodyPx, x0 + 14.0f, listTop + 6.0f, T::kTextDisabled,
                      "No lessons are available in this section.");
    }
    DrawScrollbar(dl, x1 - 4.0f, listTop, summaryTop - 8.0f,
                  static_cast<int>(s.entries.size()), fit, first);

    dl->AddLine(ImVec2(x0 + 10.0f, summaryTop), ImVec2(x1 - 10.0f, summaryTop), kRule, 1.0f);
    const MissionInfo* m = (s.selection >= 0 && s.selection < static_cast<int>(s.entries.size()))
                         ? &s.entries[s.selection] : nullptr;
    if (!m) return;
    const float sx = x0 + 14.0f;
    if (!m->unavailableReason.empty()) {
        L::DrawString(dl, g.body, g.smallPx, sx, summaryTop + 8.0f, kWarn, "UNAVAILABLE");
        DrawWrapped(dl, g.body, g.bodyPx, ImVec2(sx, summaryTop + 23.0f), T::kTextStatus,
                    m->unavailableReason, x1 - sx - 14.0f, 34.0f);
    } else {
        L::DrawString(dl, g.body, g.smallPx, sx, summaryTop + 8.0f, kAccentSoft, "YOU'LL PRACTICE");
        DrawRichWrapped(device, dl, g.body, g.bodyPx, ImVec2(sx, summaryTop + 23.0f),
                        m->description.empty() ? T::kTextDisabled : T::kTextInactive,
                        m->description.empty() ? "No summary provided." : m->description,
                        x1 - sx - 14.0f, 34.0f);
    }
    float metaY = summaryTop + 62.0f;
    if (!m->hint.empty()) {
        L::DrawString(dl, g.body, g.smallPx, sx, metaY, kAccentSoft, "TIP");
        DrawRichWrapped(device, dl, g.body, g.smallPx, ImVec2(sx + 30.0f, metaY),
                        T::kTextStatus, m->hint, x1 - sx - 44.0f, 28.0f);
        metaY += 32.0f;
    }
    std::string meta = DifficultyLabel(m->difficulty);
    if (!m->character.empty() || !m->dummy.empty()) {
        meta += "  |  " + DisplayCharName(m->character) + " vs " + DisplayCharName(m->dummy);
    }
    DrawWrapped(dl, g.body, g.smallPx, ImVec2(sx, metaY), T::kTextStatus,
                meta, x1 - sx - 14.0f, 16.0f);
}

void DrawCategoryRail(ImDrawList* dl, const Geom& g, const Snapshot& s,
                      float x0, float y0, float x1, float y1) {
    const bool focused = s.railFocus;
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), kPanelStrong);
    dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), focused ? kPanelBorderHot : kPanelBorder);
    L::DrawString(dl, g.body, g.smallPx, x0 + 12.0f, y0 + 9.0f, kAccentSoft, "CATEGORIES");
    dl->AddLine(ImVec2(x0 + 10.0f, y0 + 28.0f), ImVec2(x1 - 10.0f, y0 + 28.0f), kRule, 1.0f);

    constexpr float rowH = 44.0f;
    float y = y0 + 36.0f;
    for (int i = 0; i < static_cast<int>(s.categories.size()); ++i) {
        const CategoryState& c = s.categories[i];
        const bool current = i == s.category;
        const bool onBar = DrawBrowserRowChrome(
            dl, x0, y, x1, rowH, current, focused);
        dl->PushClipRect(ImVec2(x0 + 14.0f, y), ImVec2(x1 - 10.0f, y + rowH), true);
        if (onBar) {
            L::DrawOutlinedText(dl, g.body, g.bodyPx, x0 + 14.0f, y + 6.0f,
                                T::kBarTextSel, c.label.c_str());
        } else {
            L::DrawString(dl, g.body, g.bodyPx, x0 + 14.0f, y + 6.0f,
                          current ? T::kTextActive : T::kTextInactive, c.label.c_str());
        }
        char count[32];
        if (c.cleared >= c.available && c.available > 0 && c.unavailable > 0) {
            _snprintf_s(count, sizeof(count), _TRUNCATE, "COMPLETE   %d UNAVAILABLE",
                        c.unavailable);
        } else if (c.cleared >= c.available && c.available > 0) {
            _snprintf_s(count, sizeof(count), _TRUNCATE, "COMPLETE");
        } else if (c.unavailable > 0) {
            _snprintf_s(count, sizeof(count), _TRUNCATE, "%d / %d   %d UNAVAILABLE",
                        c.cleared, c.available, c.unavailable);
        } else {
            _snprintf_s(count, sizeof(count), _TRUNCATE, "%d / %d", c.cleared, c.available);
        }
        if (onBar) {
            L::DrawOutlinedText(dl, g.body, g.smallPx, x0 + 14.0f, y + 23.0f,
                                T::kBarTextSel, count);
        } else {
            L::DrawString(dl, g.body, g.smallPx, x0 + 14.0f, y + 23.0f,
                          c.cleared >= c.available && c.available > 0 ? kGood : T::kTextStatus,
                          count);
        }
        dl->PopClipRect();
        y += rowH;
        if (y + rowH > y1 - 4.0f) break;
    }
    if (s.categories.empty()) {
        L::DrawString(dl, g.body, g.smallPx, x0 + 12.0f, y0 + 38.0f, T::kTextDisabled,
                      "NO CATEGORIES");
    }
}

const char* MissionSectionLabel(int section) {
    return section >= 0 && section < kMissionSectionCount
        ? kMissionSections[section] : "MISSIONS";
}

void DrawMissionLibraryPane(void* device, ImDrawList* dl, const Geom& g,
                            const Snapshot& s,
                            float x0, float y0, float x1, float y1) {
    const bool focused = !s.railFocus;
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), kPanel);
    dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1),
                focused ? kPanelBorderHot : kPanelBorder);
    L::DrawString(dl, g.body, g.smallPx, x0 + 12.0f, y0 + 9.0f,
                  kAccentSoft, MissionSectionLabel(s.tab));
    dl->AddLine(ImVec2(x0 + 10.0f, y0 + 28.0f),
                ImVec2(x1 - 10.0f, y0 + 28.0f), kRule, 1.0f);

    constexpr float summaryH = 142.0f;
    const float summaryTop = y1 - summaryH;
    const float listTop = y0 + 34.0f;
    const float rowH = (std::max)(34.0f, g.bodyPx + g.smallPx + 11.0f);
    const int fit = (std::max)(1, static_cast<int>(
        (summaryTop - listTop - 8.0f) / rowH));
    {
        std::lock_guard<std::mutex> lk(g_mx);
        g_visibleRows = fit;
    }
    const int maxFirst = (std::max)(0, static_cast<int>(s.entries.size()) - fit);
    const int first = (std::max)(0, (std::min)(s.scroll, maxFirst));
    const int last = (std::min)(static_cast<int>(s.entries.size()), first + fit);
    float y = listTop;
    for (int i = first; i < last; ++i) {
        const MissionInfo& mission = s.entries[i];
        const bool selected = i == s.selection;
        const bool disabled = mission.locked || !mission.unavailableReason.empty();
        const bool onBar = DrawBrowserRowChrome(
            dl, x0, y, x1, rowH, selected, focused, disabled);
        const auto rowText = [&](ImFont* font, float size, float tx, float ty,
                                 ImU32 color, const char* text) {
            if (onBar) L::DrawOutlinedText(dl, font, size, tx, ty, color, text);
            else L::DrawString(dl, font, size, tx, ty, color, text);
        };
        char number[8] = {};
        _snprintf_s(number, sizeof(number), _TRUNCATE, "%02d", i + 1);
        const float twoLineH = g.bodyPx + 2.0f + g.smallPx;
        const float primaryY = y + (rowH - twoLineH) * 0.5f;
        const float secondaryY = primaryY + g.bodyPx + 2.0f;
        rowText(g.body, g.smallPx, x0 + 12.0f,
                y + (rowH - g.smallPx) * 0.5f,
                onBar ? (disabled ? T::kBarTextDis : T::kBarTextSel)
                      : T::kTextDisabled,
                number);
        dl->PushClipRect(ImVec2(x0 + 40.0f, y),
                         ImVec2(x1 - 74.0f, y + rowH), true);
        rowText(g.body, g.bodyPx, x0 + 40.0f, primaryY,
                onBar ? (disabled ? T::kBarTextDis : T::kBarTextSel)
                      : (disabled ? T::kTextDisabled : T::kTextInactive),
                Upper(mission.name).c_str());
        const std::string& category = mission.categoryLabel.empty()
            ? mission.category : mission.categoryLabel;
        const std::string source = category.empty()
            ? SourceLabel(mission)
            : SourceLabel(mission) + " / " + Upper(category);
        rowText(g.body, g.smallPx, x0 + 40.0f, secondaryY,
                onBar ? (disabled ? T::kBarTextDis : T::kBarTextSel)
                      : T::kTextStatus,
                source.c_str());
        dl->PopClipRect();
        const std::string right = !mission.unavailableReason.empty()
            ? "UNAVAILABLE" : mission.locked ? "LOCKED"
            : DifficultyLabel(mission.difficulty);
        const float rightW = L::MeasureTextW(g.body, g.smallPx, right.c_str());
        rowText(g.body, g.smallPx, x1 - 12.0f - rightW,
                y + (rowH - g.smallPx) * 0.5f,
                onBar ? (disabled ? T::kBarTextDis : T::kBarTextSel)
                      : (disabled ? T::kTextDisabled : T::kTextStatus),
                right.c_str());
        y += rowH;
    }
    if (s.entries.empty()) {
        L::DrawString(dl, g.body, g.bodyPx, x0 + 14.0f, listTop + 7.0f,
                      T::kTextDisabled,
                      s.tab == 2 ? "No recorded drafts are available."
                                 : "No sessions are available in this section.");
    }
    DrawScrollbar(dl, x1 - 4.0f, listTop, summaryTop - 8.0f,
                  static_cast<int>(s.entries.size()), fit, first);

    dl->AddLine(ImVec2(x0 + 10.0f, summaryTop),
                ImVec2(x1 - 10.0f, summaryTop), kRule, 1.0f);
    const MissionInfo* selected =
        s.selection >= 0 && s.selection < static_cast<int>(s.entries.size())
            ? &s.entries[s.selection] : nullptr;
    if (!selected) return;
    const float sx = x0 + 14.0f;
    if (!selected->unavailableReason.empty()) {
        L::DrawString(dl, g.body, g.smallPx, sx, summaryTop + 8.0f,
                      kWarn, "UNAVAILABLE");
        DrawWrapped(dl, g.body, g.bodyPx, ImVec2(sx, summaryTop + 24.0f),
                    T::kTextStatus, selected->unavailableReason,
                    x1 - sx - 14.0f, 42.0f);
        return;
    }
    const std::string matchup = DifficultyLabel(selected->difficulty) +
        std::string("  |  ") + DisplayCharName(selected->character) + " vs " +
        DisplayCharName(selected->dummy);
    L::DrawString(dl, g.body, g.smallPx, sx, summaryTop + 8.0f,
                  kAccentSoft, matchup.c_str());
    DrawRichWrapped(device, dl, g.body, g.bodyPx,
                    ImVec2(sx, summaryTop + 25.0f),
                    selected->description.empty() ? T::kTextDisabled
                                                  : T::kTextInactive,
                    selected->description.empty() ? "No summary provided."
                                                  : selected->description,
                    x1 - sx - 14.0f, 35.0f);
    L::DrawString(dl, g.body, g.smallPx, sx, summaryTop + 67.0f,
                  kAccentSoft, "RECIPE");
    DrawRichWrapped(device, dl, g.body, g.bodyPx,
                    ImVec2(sx, summaryTop + 84.0f),
                    selected->recipe.empty() ? T::kTextDisabled : T::kTextActive,
                    selected->recipe.empty() ? "No recipe preview." : selected->recipe,
                    x1 - sx - 14.0f, 45.0f);
}

void DrawMissionAuthorPane(ImDrawList* dl, const Geom& g, const Snapshot& s,
                           float x0, float y0, float x1, float y1) {
    const bool focused = !s.railFocus;
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), kPanel);
    dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1),
                focused ? kPanelBorderHot : kPanelBorder);
    L::DrawString(dl, g.body, g.smallPx, x0 + 12.0f, y0 + 9.0f,
                  kAccentSoft, "RECORD & AUTHOR");
    dl->AddLine(ImVec2(x0 + 10.0f, y0 + 28.0f),
                ImVec2(x1 - 10.0f, y0 + 28.0f), kRule, 1.0f);

    struct AuthorAction { const char* label; const char* detail; };
    const AuthorAction actions[kAuthorActionCount] = {
        { "START NEW RECORDING",
          "Choose fighters, arrange the exact start, then capture and review a demonstration." },
        { "PACK & MISSION WORKSHOP",
          "Create or rename editable packs and categories, and revise published mission details." },
    };
    constexpr float rowH = 62.0f;
    float y = y0 + 38.0f;
    for (int i = 0; i < kAuthorActionCount; ++i) {
        const bool selected = i == s.selection;
        const bool onBar = DrawBrowserRowChrome(
            dl, x0, y, x1, rowH, selected, focused);
        if (onBar) {
            L::DrawOutlinedText(dl, g.body, g.bodyPx, x0 + 16.0f, y + 9.0f,
                                T::kBarTextSel, actions[i].label);
        } else {
            L::DrawString(dl, g.body, g.bodyPx, x0 + 16.0f, y + 9.0f,
                          selected ? T::kTextActive : T::kTextInactive,
                          actions[i].label);
        }
        DrawWrapped(dl, g.body, g.smallPx, ImVec2(x0 + 16.0f, y + 29.0f),
                    onBar ? T::kBarTextSel : T::kTextStatus,
                    actions[i].detail, x1 - x0 - 32.0f, 27.0f);
        y += rowH;
    }

    const float infoY = y + 18.0f;
    L::DrawString(dl, g.body, g.smallPx, x0 + 16.0f, infoY,
                  kAccentSoft, "WORKFLOW");
    const std::string binding = Mission::Engine::Recorder::GetMacroRecordBindingLabel();
    const std::string flow =
        "Recording has its own setup, capture, and Review menus. Macro Record is " +
        binding + ". Pack edits change display metadata only; stable IDs, mission files, "
        "recorded inputs, and gameplay evidence are preserved.";
    DrawWrapped(dl, g.body, g.bodyPx, ImVec2(x0 + 16.0f, infoY + 18.0f),
                T::kTextInactive, flow, x1 - x0 - 32.0f,
                y1 - infoY - 28.0f);
}

void DrawMissionRail(ImDrawList* dl, const Geom& g, const Snapshot& s,
                     float x0, float y0, float x1, float y1) {
    const bool focused = s.railFocus;
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), kPanelStrong);
    dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1),
                focused ? kPanelBorderHot : kPanelBorder);
    L::DrawString(dl, g.body, g.smallPx, x0 + 12.0f, y0 + 9.0f,
                  kAccentSoft, "LIBRARY");
    dl->AddLine(ImVec2(x0 + 10.0f, y0 + 28.0f),
                ImVec2(x1 - 10.0f, y0 + 28.0f), kRule, 1.0f);
    constexpr float rowH = 52.0f;
    float y = y0 + 36.0f;
    for (int i = 0; i < kMissionSectionCount; ++i) {
        const bool current = i == s.tab;
        const bool onBar = DrawBrowserRowChrome(
            dl, x0, y, x1, rowH, current, focused);
        if (onBar) {
            L::DrawOutlinedText(dl, g.body, g.bodyPx, x0 + 14.0f, y + 8.0f,
                                T::kBarTextSel, kMissionSections[i]);
        } else {
            L::DrawString(dl, g.body, g.bodyPx, x0 + 14.0f, y + 8.0f,
                          current ? T::kTextActive : T::kTextInactive,
                          kMissionSections[i]);
        }
        char count[32] = {};
        if (i < 3) {
            _snprintf_s(count, sizeof(count), _TRUNCATE, "%d AVAILABLE",
                        s.missionSectionCounts[i]);
        } else {
            _snprintf_s(count, sizeof(count), _TRUNCATE, "2 TOOLS");
        }
        if (onBar) {
            L::DrawOutlinedText(dl, g.body, g.smallPx, x0 + 14.0f, y + 29.0f,
                                T::kBarTextSel, count);
        } else {
            L::DrawString(dl, g.body, g.smallPx, x0 + 14.0f, y + 29.0f,
                          T::kTextStatus, count);
        }
        y += rowH;
        if (y + rowH > y1 - 4.0f) break;
    }
}

}

void Open(Screen screen, bool resumeLastLesson) {
    std::lock_guard<std::mutex> lk(g_mx);
    g_screen = screen;
    g_tab = 0;
    g_railFocus = false;
    g_selection = 0;
    g_scroll = 0;
    if (screen == Screen::Missions) {
        for (int& selection : g_missionSelection) selection = 0;
        for (int& scroll : g_missionScroll) scroll = 0;
    }
    RebuildVisibleLocked();
    if (screen == Screen::Tutorial) {
        int bestIdx = -1;
        if (resumeLastLesson) {
            std::map<std::string, std::string> lastByPack;
            for (int i = 0; i < static_cast<int>(g_missions.size()); ++i) {
                const MissionInfo& m = g_missions[i];
                if (!IsTutorial(m) || m.packId.empty() ||
                    !m.unavailableReason.empty()) {
                    continue;
                }
                auto inserted = lastByPack.emplace(m.packId, std::string());
                if (inserted.second) {
                    inserted.first->second =
                        ::Mission::Tutorial::ProgressLastLesson(m.packId);
                }
                if (!inserted.first->second.empty() &&
                    inserted.first->second == m.lessonId) {
                    bestIdx = i;
                    break;
                }
            }
        }
        if (bestIdx < 0) {
            std::map<std::string, const MissionInfo*> lessonState;
            for (const MissionInfo& m : g_missions) {
                if (IsTutorial(m) && !m.packId.empty() && !m.lessonId.empty()) {
                    lessonState[m.packId + "\x1f" + m.lessonId] = &m;
                }
            }
            int bestOrder = 0;
            for (int i = 0; i < static_cast<int>(g_missions.size()); ++i) {
                const MissionInfo& m = g_missions[i];
                if (!IsTutorial(m) || m.cleared ||
                    !m.unavailableReason.empty()) {
                    continue;
                }
                bool recommended = true;
                for (const std::string& prerequisite : m.recommendedAfter) {
                    const auto it = lessonState.find(
                        m.packId + "\x1f" + prerequisite);
                    if (it == lessonState.end() ||
                        (!it->second->cleared &&
                         it->second->unavailableReason.empty())) {
                        recommended = false;
                        break;
                    }
                }
                if (!recommended) continue;
                if (bestIdx < 0 || m.order < bestOrder) {
                    bestIdx = i;
                    bestOrder = m.order;
                }
            }
            if (bestIdx < 0) {
                for (int i = 0; i < static_cast<int>(g_missions.size()); ++i) {
                    const MissionInfo& m = g_missions[i];
                    if (!IsTutorial(m) || m.cleared ||
                        !m.unavailableReason.empty()) {
                        continue;
                    }
                    if (bestIdx < 0 || m.order < bestOrder) {
                        bestIdx = i;
                        bestOrder = m.order;
                    }
                }
            }
        }
        if (bestIdx >= 0) {
            const std::string key = g_missions[bestIdx].category.empty()
                                  ? std::string("general") : g_missions[bestIdx].category;
            for (int c = 0; c < static_cast<int>(g_categories.size()); ++c) {
                if (g_categories[c].key == key) { g_category = c; break; }
            }
            RebuildVisibleLocked();
            for (int r = 0; r < static_cast<int>(g_visible.size()); ++r) {
                if (g_visible[r] == bestIdx) { g_selection = r; break; }
            }
            ClampScrollLocked();
        }
    }
}

void SetTutorialCategories(std::vector<std::pair<std::string, std::string>>&& cats) {
    std::lock_guard<std::mutex> lk(g_mx);
    g_packCategories = std::move(cats);
}

void Close() {
    std::lock_guard<std::mutex> lk(g_mx);
    RememberCategoryPosLocked();
    g_screen = Screen::None;
    g_railFocus = false;
}

bool Active() {
    std::lock_guard<std::mutex> lk(g_mx);
    return g_screen != Screen::None;
}

Screen Current() {
    std::lock_guard<std::mutex> lk(g_mx);
    return g_screen;
}

void SetMissions(std::vector<MissionInfo>&& missions) {
    std::lock_guard<std::mutex> lk(g_mx);
    for (std::size_t i = 0; i < missions.size(); ++i) {
        for (std::size_t j = i + 1; j < missions.size(); ++j) {
            if (_stricmp(missions[i].source.c_str(), missions[j].source.c_str()) != 0) {
                continue;
            }
            const bool distinctPack =
                (!missions[i].packFolder.empty() || !missions[j].packFolder.empty())
                    ? missions[i].packFolder != missions[j].packFolder
                    : missions[i].packId != missions[j].packId;
            if (distinctPack) {
                missions[i].disambiguateSource = true;
                missions[j].disambiguateSource = true;
            }
        }
    }
    g_missions = std::move(missions);
    RebuildVisibleLocked();
}

void MoveSelection(int dir) {
    std::lock_guard<std::mutex> lk(g_mx);
    if (g_screen == Screen::None || dir == 0) return;
    if (g_railFocus) {
        RememberCategoryPosLocked();
        if (g_screen == Screen::Tutorial) {
            if (g_categories.empty()) return;
            const int count = static_cast<int>(g_categories.size());
            g_category = (g_category + (dir > 0 ? 1 : -1) + count) % count;
        } else {
            g_tab = (g_tab + (dir > 0 ? 1 : -1) + kMissionSectionCount) %
                    kMissionSectionCount;
        }
        RebuildVisibleLocked();
        return;
    }
    if (g_screen == Screen::Missions &&
        g_tab == kMissionSectionCount - 1) {
        g_selection = (g_selection + (dir > 0 ? 1 : -1) +
                       kAuthorActionCount) % kAuthorActionCount;
        RememberCategoryPosLocked();
        return;
    }
    if (g_visible.empty()) return;
    const int count = static_cast<int>(g_visible.size());
    g_selection = (g_selection + (dir > 0 ? 1 : -1) + count) % count;
    ClampScrollLocked();
    RememberCategoryPosLocked();
}

void MoveTab(int dir) {
    std::lock_guard<std::mutex> lk(g_mx);
    if (g_screen == Screen::None || dir == 0) return;
    g_railFocus = dir > 0;
}

ConfirmAction Confirm() {
    std::lock_guard<std::mutex> lk(g_mx);
    if (g_railFocus) {
        g_railFocus = false;
        return ConfirmAction::None;
    }
    if (g_screen == Screen::Missions &&
        g_tab == kMissionSectionCount - 1) {
        return g_selection == 0 ? ConfirmAction::Record : ConfirmAction::Author;
    }
    const MissionInfo* selected = SelectedLocked();
    if (!selected || selected->locked || selected->path.empty() ||
        !selected->unavailableReason.empty()) {
        return ConfirmAction::None;
    }
    return ConfirmAction::Launch;
}

bool Back() {
    std::lock_guard<std::mutex> lk(g_mx);
    if (g_railFocus) {
        g_railFocus = false;
        return true;
    }
    return false;
}

std::string SelectedMissionPath() {
    std::lock_guard<std::mutex> lk(g_mx);
    const MissionInfo* selected = SelectedLocked();
    return selected ? selected->path : std::string();
}

bool WantsDraw() {
    std::lock_guard<std::mutex> lk(g_mx);
    return g_screen != Screen::None;
}

void Draw(void* device, ImDrawList* dl) {
    if (!dl) return;
    const Snapshot s = CaptureSnapshot();
    if (s.screen == Screen::None) return;

    if (s.screen == Screen::Tutorial) {
        int available = 0, unavailable = 0, cleared = 0;
        for (const CategoryState& c : s.categories) {
            available += c.available;
            unavailable += c.unavailable;
            cleared += c.cleared;
        }
        char status[64];
        if (unavailable > 0) {
            _snprintf_s(status, sizeof(status), _TRUNCATE,
                        "%d / %d COMPLETE   %d UNAVAILABLE",
                        cleared, available, unavailable);
        } else {
            _snprintf_s(status, sizeof(status), _TRUNCATE,
                        "%d / %d COMPLETE", cleared, available);
        }
        const Geom g = BeginScreen(dl, "LEARN BY DOING", "TUTORIAL", status);
        const float y0 = g.headerBottom + 6.0f;
        const float y1 = g.hintTop - 10.0f;
        const float railX1 = T::kCanvasW - g.margin;
        const float railW = (std::max)(164.0f,
            (std::min)(220.0f, Snap(164.0f * g.ls)));
        const float railX0 = railX1 - railW;
        const float paneGap = Snap(12.0f * g.ls);
        DrawCoursePane(device, dl, g, s, g.margin, y0, railX0 - paneGap, y1);
        DrawCategoryRail(dl, g, s, railX0, y0, railX1, y1);
        if (s.railFocus) {
            const HintPair hints[] = {
                { "UP / DOWN", "CHANGE CATEGORY" },
                { "LEFT / CONFIRM", "LESSONS" },
                { "CANCEL / ESC", "LESSONS" },
            };
            DrawHintBar(dl, g, hints, 3);
        } else {
            const MissionInfo* sel =
                (s.selection >= 0 && s.selection < static_cast<int>(s.entries.size()))
                    ? &s.entries[s.selection] : nullptr;
            const HintPair hints[] = {
                { "UP / DOWN", "SELECT" },
                { "RIGHT", "CATEGORIES" },
                { "CONFIRM", sel && !sel->unavailableReason.empty() ? "UNAVAILABLE"
                                                                    : "START LESSON" },
                { "CANCEL / ESC", "BACK" },
            };
            DrawHintBar(dl, g, hints, 4);
        }
        return;
    }

    const int totalAvailable = s.missionSectionCounts[0] +
                               s.missionSectionCounts[1] +
                               s.missionSectionCounts[2];
    char status[48];
    _snprintf_s(status, sizeof(status), _TRUNCATE, "%d AVAILABLE", totalAvailable);
    const Geom g = BeginScreen(dl, "PRACTICE LIBRARY", "MISSIONS", status);
    const float y0 = g.headerBottom + 6.0f;
    const float y1 = g.hintTop - 10.0f;
    const float railX1 = T::kCanvasW - g.margin;
    const float scaledRailW = Snap(164.0f * g.ls);
    const float railW = (std::max)(164.0f,
                                   (std::min)(220.0f, scaledRailW));
    const float railX0 = railX1 - railW;
    const float paneGap = Snap(12.0f * g.ls);
    if (s.tab == kMissionSectionCount - 1) {
        DrawMissionAuthorPane(dl, g, s, g.margin, y0, railX0 - paneGap, y1);
    } else {
        DrawMissionLibraryPane(device, dl, g, s, g.margin, y0,
                               railX0 - paneGap, y1);
    }
    DrawMissionRail(dl, g, s, railX0, y0, railX1, y1);
    if (s.railFocus) {
        const HintPair hints[] = {
            { "UP / DOWN", "CHANGE SECTION" },
            { "LEFT / CONFIRM", "BROWSE" },
            { "CANCEL / ESC", "BROWSE" },
        };
        DrawHintBar(dl, g, hints, 3);
        return;
    }
    const MissionInfo* selected =
        s.selection >= 0 && s.selection < static_cast<int>(s.entries.size())
            ? &s.entries[s.selection] : nullptr;
    const bool authorSection = s.tab == kMissionSectionCount - 1;
    const HintPair hints[] = {
        { "UP / DOWN", "SELECT" },
        { "RIGHT", "LIBRARY" },
        { "CONFIRM", authorSection
                         ? (s.selection == 0 ? "START RECORDING" : "OPEN WORKSHOP")
                         : selected && !selected->unavailableReason.empty()
                               ? "UNAVAILABLE"
                               : selected && selected->locked ? "LOCKED" : "PLAY" },
        { "CANCEL / ESC", "BACK" },
    };
    DrawHintBar(dl, g, hints, 4);
}

}
